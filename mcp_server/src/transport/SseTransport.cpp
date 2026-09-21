#include "SseTransport.h"

#include <iostream>
#include <httplib.h>
#include <chrono>
#include <utility>
#include <iomanip>
#include <cstring>

#include "aixlog.hpp"
#include "json.hpp"

namespace vx::transport {

    SSE::SSE(const int port, std::string host) : host_(std::move(host)), port_(port), server_(std::make_unique<httplib::Server>()) {
        SetupRoutes();
    }

    SSE::~SSE() {
        SSE::Stop();
    }

    std::pair<size_t, std::string> SSE::Read() {
        std::unique_lock<std::mutex> lock(incoming_mutex_);

        incoming_cv_.wait(lock, [this]() { // 等 incoming_messages_ 不为空 或 server_running_ 为 false 时唤醒
            return !incoming_messages_.empty() || !server_running_.load();
        });

        if (!server_running_.load() && incoming_messages_.empty()) {
            return {0, ""};
        }

        std::string message = std::move(incoming_messages_.front());
        incoming_messages_.pop();
        return { message.length(), message };
    }

    void SSE::Write(const std::string& json_data) {
        // Hold the session lock while putting a message in the queue so a
        // closed session cannot leave a message for its successor.
        std::lock_guard<std::mutex> session_lock(session_mutex_);
        if (!sse_active_.load()) { return; }
        std::lock_guard<std::mutex> queue_lock(outgoing_mutex_);
        outgoing_messages_.push(json_data);

        outgoing_cv_.notify_one();
    }

    std::future<std::pair<size_t, std::string>> SSE::ReadAsync() {
        return std::async(std::launch::async, [this]() -> std::pair<size_t, std::string> {
            LOG(TRACE) << "READ ASYNC CALLED!!!" << std::endl;
            return Read();
        });
    }

    std::future<void> SSE::WriteAsync(const std::string& json_data) {
        return std::async(std::launch::async, [this, json_data] () {
            Write(json_data);
        });
    }

    bool SSE::Start() {
        if (server_running_.load()) {
            return false;
        }

        server_running_.store(true);

        server_thread_ = std::thread([this]() {
            LOG(INFO) << "Starting SSE server on " << host_ << ":" << port_ << std::endl;

            if (!server_->listen(host_.c_str(), port_)) {
                LOG(ERROR) << "Failed to start SSE server on " << host_ << ":" << port_ << std::endl;
                server_running_.store(false);
                incoming_cv_.notify_all();
                outgoing_cv_.notify_all();
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return server_running_.load();
    }

    void SSE::Stop() {
        server_running_.store(false);
        std::string session_id;
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            session_id = active_session_id_;
        }
        if (!session_id.empty()) { CloseSession(session_id); }

        if (server_) {
            server_->stop();
        }

        incoming_cv_.notify_all();
        outgoing_cv_.notify_all();

        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    void SSE::SetupRoutes() {
        server_->Options("/.*", [this](const httplib::Request& req, httplib::Response& res) {
            HandleOptionsRequest(req, res);
        });

        server_->Get("/health", [](const httplib::Request& req, httplib::Response& res) {
            res.set_content("{\"status\" : \"ok\"}", "application/json");
        });

        server_->Post("/messages", [this](const httplib::Request& req, httplib::Response& res) {
            HandlePostMessage(req, res);
        });

        server_->Get("/sse", [this](const httplib::Request& req, httplib::Response& res) {    // client 首先 GET /sse 建立连接
            HandleSSEConnection(req, res);
        });
    }

    void SSE::HandleSSEConnection(const httplib::Request&, httplib::Response& res) {
        // This is deliberately a single-client transport.  Rejecting a second
        // stream keeps the session and the two queues unambiguous.
        const std::string session_id = vx::utils::SessionBuilder::GenerateUniqueSessionID();
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            if (sse_active_.load()) { // 如果已经sse_active_，说明已经有一个client连接了，拒绝第二个连接
                res.status = 409;
                res.set_content("{\"error\":\"An SSE session is already active\"}", "application/json");
                return;
            }
            active_session_id_ = session_id;
            sse_active_.store(true);
        }

        struct SSEConnectionContext {
            std::string session_id;
            std::chrono::steady_clock::time_point last_write_time;  // 最后write的时间，用于keepalive
        };
        auto context = std::make_shared<SSEConnectionContext>(
            SSEConnectionContext{session_id, std::chrono::steady_clock::now()});

        SetCORSHeaders(res);
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_chunked_content_provider("text/event-stream",  // 设置chunked content provider，后续httplib会调用这个lambda函数来写数据
            [this, context](size_t, httplib::DataSink& sink) -> bool {
                constexpr auto keepalive_interval = std::chrono::seconds(15);
                auto terminate = [this, context]() {
                    CloseSession(context->session_id);
                    return false;
                };

                const std::string endpoint = "event: endpoint\ndata: /messages?session_id=" + context->session_id + "\n\n";  // 第一次发送endpoint事件，告诉client后续post消息的url
                if (!sink.write(endpoint.data(), endpoint.size())) {
                    LOG(DEBUG) << "Unable to write SSE endpoint event" << std::endl;
                    return terminate();
                }
                context->last_write_time = std::chrono::steady_clock::now();

                while (server_running_.load() && IsActiveSession(context->session_id)) {
                    std::string message;
                    {
                        std::unique_lock<std::mutex> lock(outgoing_mutex_);
                        const auto deadline = context->last_write_time + keepalive_interval;
                        outgoing_cv_.wait_until(lock, deadline, [this]() {
                            return !outgoing_messages_.empty() || !server_running_.load() || !sse_active_.load();
                        });
                        if (!outgoing_messages_.empty()) {
                            message = std::move(outgoing_messages_.front());
                            outgoing_messages_.pop();
                        }
                    }

                    if (!server_running_.load() || !IsActiveSession(context->session_id)) {
                        return terminate();
                    }

                    // is_writable is only an early hint; failed write is the
                    // authoritative indication that the stream has ended.
                    if (sink.is_writable && !sink.is_writable()) {
                        LOG(DEBUG) << "SSE sink is no longer writable" << std::endl;
                        return terminate();
                    }

                    const std::string frame = message.empty()
                        ? ": ping\n\n"                                      // 如果没有消息，说明wait_until超时了，发送ping keepalive
                        : "data: " + message + "\n\n";
                    if (!sink.write(frame.data(), frame.size())) {
                        LOG(DEBUG) << "SSE write failed; closing session" << std::endl;
                        return terminate();
                    }
                    context->last_write_time = std::chrono::steady_clock::now();
                }
                return terminate();
            });
    }

    void SSE::HandlePostMessage(const httplib::Request& req, httplib::Response& res) {                 //收到post消息，向incoming写，通知read可以读了
        SetCORSHeaders(res);

        if (!req.has_param("session_id") || !IsActiveSession(req.get_param_value("session_id"))) {
            res.status = 409;
            res.set_content("{\"error\":\"Invalid or inactive SSE session\"}", "application/json");
            return;
        }

        std::string message = req.body;
        if (message.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"Empty message\"}", "application/json");
            return;
        }

        LOG(DEBUG) << "Received message via POST: " << message << std::endl;
        {
            std::lock_guard<std::mutex> lock(incoming_mutex_);
            incoming_messages_.push(message);
        }
        incoming_cv_.notify_one();

        res.status = 200;
        res.set_content("{\"status\":\"received\"}", "application/json");
    }

    bool SSE::IsActiveSession(const std::string& session_id) const {
        std::lock_guard<std::mutex> lock(session_mutex_);
        return sse_active_.load() && !active_session_id_.empty() && active_session_id_ == session_id;
    }

    void SSE::CloseSession(const std::string& session_id) {
        {
            std::lock_guard<std::mutex> lock(session_mutex_);
            if (active_session_id_ != session_id) { return; }
            active_session_id_.clear();
            sse_active_.store(false);
        }
        {
            std::lock_guard<std::mutex> lock(outgoing_mutex_);
            std::queue<std::string> empty;
            outgoing_messages_.swap(empty);
        }
        incoming_cv_.notify_all();
        outgoing_cv_.notify_all();
    }

    void SSE::HandleOptionsRequest(const httplib::Request& req, httplib::Response& res) {
        SetCORSHeaders(res);
        res.status = 200;
    }

    void SSE::SetCORSHeaders(httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, x-api-key");
        res.set_header("Access-Control-Expose-Headers", "Content-Type, Authorization, x-api-key");
        res.set_header("Access-Control-Max-Age", "86400");
    }

}
