#include "agent_rpc/mcp/mcp_client.h"
#include "agent_rpc/common/logger.h"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <json/json.h>
#include <ctime>
#include <curl/curl.h>
#include <signal.h>

namespace agent_rpc {
namespace mcp {

// MCPClient 实现
MCPClient::MCPClient() {
    // Logger 使用全局宏，不需要实例化
}

MCPClient::~MCPClient() {
    disconnect();
}

bool MCPClient::connect(const std::string& server_path, const std::vector<std::string>& args) {
    // 使用 STDIO 模式连接
    MCPConnectionConfig config;
    config.transport = MCPTransportType::STDIO;
    config.server_path = server_path;
    config.server_args = args;
    return connect(config);
}

bool MCPClient::connect(const MCPConnectionConfig& config) {
    if (connected_) {
        LOG_WARN("MCP client already connected");
        return true;
    }
    
    config_ = config;
    transport_type_ = config.transport;
    
    bool success = false;
    
    if (transport_type_ == MCPTransportType::STDIO) {
        server_path_ = config.server_path;
        server_args_ = config.server_args;
        
        if (!startMCPServer()) {    //创建 Server进程
            LOG_ERROR("Failed to start MCP server");
            return false;
        }
        
        connected_ = true;
        running_ = true;
        
        // 启动通知处理线程
        notification_thread_ = std::thread([this]() {
            processNotificationsStdio();
        });
        
        LOG_INFO("MCP client connected to server (STDIO): " + server_path_);
        success = true;
    } else if (transport_type_ == MCPTransportType::SSE) {
        if (!connectSSE()) {
            LOG_ERROR("Failed to connect to MCP server via SSE");
            return false;
        }
        
        connected_ = true;
        running_ = true;
        
        LOG_INFO("MCP client connected to server (SSE): " + config.sse_url);
        success = true;
    }
    
    return success;
}

MCPTransportType MCPClient::getTransportType() const {
    return transport_type_;
}

void MCPClient::disconnect() {
    if (!connected_) {
        return;
    }
    
    running_ = false;
    connected_ = false;
    
    // 停止通知线程
    if (notification_thread_.joinable()) {
        notification_thread_.join();
    }
    
    if (transport_type_ == MCPTransportType::STDIO) {
        stopMCPServer();
    } else if (transport_type_ == MCPTransportType::SSE) {
        disconnectSSE();
    }
    
    LOG_INFO("MCP client disconnected");
}

bool MCPClient::isConnected() const {
    return connected_;
}

std::vector<MCPTool> MCPClient::listTools() {
    std::vector<MCPTool> tools;
    
    if (!connected_) {
        LOG_ERROR("MCP client not connected");
        return tools;
    }
    
    MCPRequest request;
    request.method = "tools/list";
    request.id = "list_tools_" + std::to_string(std::time(nullptr));
    
    if (!sendRequest(request)) {
        LOG_ERROR("Failed to send tools/list request");
        return tools;
    }
    
    MCPResponse response = receiveResponse();
    if (response.is_error) {
        LOG_ERROR("Error listing tools: " + response.error);
        return tools;
    }
    
    // 解析响应
    try {
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(response.result, root)) {
            const Json::Value& tools_array = root["tools"];
            for (const auto& tool : tools_array) {
                MCPTool mcp_tool;
                mcp_tool.name = tool["name"].asString();
                mcp_tool.description = tool["description"].asString();
                mcp_tool.input_schema = tool["inputSchema"].toStyledString();
                tools.push_back(mcp_tool);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse tools list: " + std::string(e.what()));
    }
    
    return tools;
}

MCPResponse MCPClient::callTool(const std::string& tool_name, const std::string& arguments) {
    MCPResponse response;
    response.is_error = true;
    response.error = "Not connected";
    
    if (!connected_) {
        LOG_ERROR("MCP client not connected");
        return response;
    }
    
    MCPRequest request;  // id method params
    request.method = "tools/call";
    request.id = "call_tool_" + std::to_string(std::time(nullptr));
    
    // 构建参数
    Json::Value params;
    params["name"] = tool_name;
    
    Json::Value args_json;
    Json::Reader reader;
    if (reader.parse(arguments, args_json)) {
        params["arguments"] = args_json;
    } else {
        params["arguments"] = Json::Value(Json::objectValue);
    }
    
    // 只设置 params 部分，不要包含 method 和 id
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    request.params = Json::writeString(builder, params);   //基于 builder 的配置，将 params（JSON 对象）序列化为 JSON 字符串；
    
    if (!sendRequest(request)) {
        LOG_ERROR("Failed to send tools/call request");
        response.error = "Failed to send request";
        return response;
    }
    
    response = receiveResponse();
    if (response.is_error) {
        LOG_ERROR("Error calling tool " + tool_name + ": " + response.error);
    } else {
        LOG_INFO("Successfully called tool: " + tool_name);
    }
    
    return response;
}

std::vector<MCPPrompt> MCPClient::listPrompts() {
    std::vector<MCPPrompt> prompts;
    
    if (!connected_) {
        LOG_ERROR("MCP client not connected");
        return prompts;
    }
    
    MCPRequest request;
    request.method = "prompts/list";
    request.id = "list_prompts_" + std::to_string(std::time(nullptr));
    
    if (!sendRequest(request)) {
        LOG_ERROR("Failed to send prompts/list request");
        return prompts;
    }
    
    MCPResponse response = receiveResponse();
    if (response.is_error) {
        LOG_ERROR("Error listing prompts: " + response.error);
        return prompts;
    }
    
    // 解析响应
    try {
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(response.result, root)) {
            const Json::Value& prompts_array = root["prompts"];
            for (const auto& prompt : prompts_array) {
                MCPPrompt mcp_prompt;
                mcp_prompt.name = prompt["name"].asString();
                mcp_prompt.description = prompt["description"].asString();
                mcp_prompt.arguments = prompt["arguments"].toStyledString();
                prompts.push_back(mcp_prompt);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse prompts list: " + std::string(e.what()));
    }
    
    return prompts;
}

MCPResponse MCPClient::getPrompt(const std::string& prompt_name, const std::string& arguments) {
    MCPResponse response;
    response.is_error = true;
    response.error = "Not connected";
    
    if (!connected_) {
        LOG_ERROR("MCP client not connected");
        return response;
    }
    
    MCPRequest request;
    request.method = "prompts/get";
    request.id = "get_prompt_" + std::to_string(std::time(nullptr));
    
    // 构建参数
    Json::Value params;
    params["name"] = prompt_name;
    
    Json::Value args_json;
    Json::Reader reader;
    if (reader.parse(arguments, args_json)) {
        params["arguments"] = args_json;
    } else {
        params["arguments"] = Json::Value();
    }
    
    Json::Value request_obj;
    request_obj["method"] = request.method;
    request_obj["params"] = params;
    request_obj["id"] = request.id;
    
    request.params = request_obj.toStyledString();
    
    if (!sendRequest(request)) {
        LOG_ERROR("Failed to send prompts/get request");
        response.error = "Failed to send request";
        return response;
    }
    
    response = receiveResponse();
    if (response.is_error) {
        LOG_ERROR("Error getting prompt " + prompt_name + ": " + response.error);
    } else {
        LOG_INFO("Successfully got prompt: " + prompt_name);
    }
    
    return response;
}

std::vector<MCPResource> MCPClient::listResources() {
    std::vector<MCPResource> resources;
    
    if (!connected_) {
        LOG_ERROR("MCP client not connected");
        return resources;
    }
    
    MCPRequest request;
    request.method = "resources/list";
    request.id = "list_resources_" + std::to_string(std::time(nullptr));
    
    if (!sendRequest(request)) {
        LOG_ERROR("Failed to send resources/list request");
        return resources;
    }
    
    MCPResponse response = receiveResponse();
    if (response.is_error) {
        LOG_ERROR("Error listing resources: " + response.error);
        return resources;
    }
    
    // 解析响应
    try {
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(response.result, root)) {
            const Json::Value& resources_array = root["resources"];
            for (const auto& resource : resources_array) {
                MCPResource mcp_resource;
                mcp_resource.name = resource["name"].asString();
                mcp_resource.description = resource["description"].asString();
                mcp_resource.uri = resource["uri"].asString();
                mcp_resource.mime_type = resource["mimeType"].asString();
                resources.push_back(mcp_resource);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to parse resources list: " + std::string(e.what()));
    }
    
    return resources;
}

MCPResponse MCPClient::readResource(const std::string& uri) {
    MCPResponse response;
    response.is_error = true;
    response.error = "Not connected";
    
    if (!connected_) {
        LOG_ERROR("MCP client not connected");
        return response;
    }
    
    MCPRequest request;
    request.method = "resources/read";
    request.id = "read_resource_" + std::to_string(std::time(nullptr));
    
    // 构建参数
    Json::Value params;
    params["uri"] = uri;
    
    Json::Value request_obj;
    request_obj["method"] = request.method;
    request_obj["params"] = params;
    request_obj["id"] = request.id;
    
    request.params = request_obj.toStyledString();
    
    if (!sendRequest(request)) {
        LOG_ERROR("Failed to send resources/read request");
        response.error = "Failed to send request";
        return response;
    }
    
    response = receiveResponse();
    if (response.is_error) {
        LOG_ERROR("Error reading resource " + uri + ": " + response.error);
    } else {
        LOG_INFO("Successfully read resource: " + uri);
    }
    
    return response;
}

void MCPClient::setNotificationCallback(std::function<void(const std::string&, const std::string&)> callback) {
    notification_callback_ = callback;
}

bool MCPClient::sendRequest(const MCPRequest& request) {
    if (transport_type_ == MCPTransportType::STDIO) {
        return sendRequestStdio(request);
    } else if (transport_type_ == MCPTransportType::SSE) {
        return sendRequestSSE(request);
    }
    return false;
}

bool MCPClient::sendRequestStdio(const MCPRequest& request) {
    if (stdin_pipe_ == -1) {
        LOG_ERROR("MCP server stdin pipe not available");
        return false;
    }
    
    std::string json_request = buildJSONRPCRequest(request);
    json_request += "\n";  // MCP协议使用换行符分隔消息
    
    ssize_t written = write(stdin_pipe_, json_request.c_str(), json_request.length());
    if (written != static_cast<ssize_t>(json_request.length())) {
        LOG_ERROR("Failed to write request to MCP server");
        return false;
    }
    
    return true;
}

MCPResponse MCPClient::receiveResponse() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    
    // 等待响应，最多等待30秒
    if (queue_cv_.wait_for(lock, std::chrono::seconds(30), [this] { return !response_queue_.empty(); })) {
        MCPResponse response = response_queue_.front();
        response_queue_.pop();
        return response;
    }
    
    MCPResponse timeout_response;
    timeout_response.is_error = true;
    timeout_response.error = "Request timeout";
    return timeout_response;
}

void MCPClient::processNotifications() {
    if (transport_type_ == MCPTransportType::STDIO) {
        processNotificationsStdio();
    } else if (transport_type_ == MCPTransportType::SSE) {
        processNotificationsSSE();
    }
}

void MCPClient::processNotificationsStdio() {
    if (stdout_pipe_ == -1) {
        LOG_ERROR("MCP server stdout pipe not available");
        return;
    }
    
    char buffer[4096];
    std::string line;
    
    while (running_) {
        ssize_t bytes_read = read(stdout_pipe_, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            line += buffer;
            
            // 处理完整的行
            size_t pos = 0;
            while ((pos = line.find('\n')) != std::string::npos) {
                std::string message = line.substr(0, pos);
                line.erase(0, pos + 1);
                
                if (!message.empty()) {
                    MCPResponse response = parseJSONRPCResponse(message);
                    
                    // 检查是否是通知
                    if (response.id.empty() && !response.error.empty()) {
                        // 这是一个通知
                        if (notification_callback_) {
                            // 解析通知内容
                            try {
                                Json::Value root;
                                Json::Reader reader;
                                if (reader.parse(message, root)) {
                                    std::string method = root["method"].asString();
                                    if (method == "notifications/message") {
                                        const Json::Value& params = root["params"];
                                        std::string plugin_name = params["pluginName"].asString();
                                        std::string notification = params["notification"].asString();
                                        notification_callback_(plugin_name, notification);                    //client接收到notification的回调动作，一般用于server向client发送更新插件的某些功能
                                    }
                                }
                            } catch (const std::exception& e) {
                                LOG_WARN("Failed to parse notification: " + std::string(e.what()));
                            }
                        }
                    } else {
                        // 这是一个响应
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        response_queue_.push(response);
                        queue_cv_.notify_one();
                    }
                }
            }
        } else if (bytes_read == 0) {
            // 管道关闭
            break;
        } else {
            // 读取错误
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("Error reading from MCP server stdout");
                break;
            }
        }
    }
}

bool MCPClient::startMCPServer() {
    // 创建管道
    int client_to_server_pipe[2];
    int server_to_client_pipe[2];
    
    // pipe() 创建一个管道，返回两个文件描述符，分别用于读和写 0读端 1写端
    if (pipe(client_to_server_pipe) == -1 || pipe(server_to_client_pipe) == -1) {
        LOG_ERROR("Failed to create pipes for MCP server");
        return false;
    }
    
    // 创建子进程
    server_pid_ = fork();                 // 子进程server pid
    if (server_pid_ == -1) {
        LOG_ERROR("Failed to fork process for MCP server");
        return false;
    }
    
    if (server_pid_ == 0) {                                     //server进程由client进程启动
        // 子进程：运行MCP服务器
        close(client_to_server_pipe[1]);  // 关闭写端
        close(server_to_client_pipe[0]); // 关闭读端
        
        // 重定向stdin和stdout
        //dup2(fd1,fd2); 将fd1的文件描述符复制到fd2，fd2原来的文件描述符会被关闭
        //fd2被fd1覆盖
        dup2(client_to_server_pipe[0], STDIN_FILENO);              //将Server的stdin/stdout重定向到管道的两端，Server以为自己从stdin/out中读写，其实是从管道读和写入管道
        dup2(server_to_client_pipe[1], STDOUT_FILENO);
        
        // 准备参数
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(server_path_.c_str()));    // c_str 返回一个指向字符串的const char*,且该字符串以\0结尾  const_cast将其转换为 char*,因为这是execv要求的
        
        for (const auto& arg : server_args_) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr); // execv 要求第二个参数（argv）必须是一个以 nullptr 结尾的字符串数组
        
        // 执行MCP服务器
        // int execv(const char *path, char *const argv[]);
        execv(server_path_.c_str(), argv.data());
        
        // 如果execv失败
        LOG_ERROR("Failed to execute MCP server: " + server_path_);
        exit(1);
    } else {
        // 父进程
        close(client_to_server_pipe[0]);  // 关闭读端
        close(server_to_client_pipe[1]); // 关闭写端
        
        stdin_pipe_ = client_to_server_pipe[1];
        stdout_pipe_ = server_to_client_pipe[0];
        
        // 设置非阻塞模式
        fcntl(stdout_pipe_, F_SETFL, O_NONBLOCK);
        
        // 等待服务器启动
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        return true;
    }
}

void MCPClient::stopMCPServer() {
    if (server_pid_ > 0) {
        kill(server_pid_, SIGTERM);
        waitpid(server_pid_, nullptr, 0);
        server_pid_ = -1;
    }
    
    if (stdin_pipe_ != -1) {
        close(stdin_pipe_);
        stdin_pipe_ = -1;
    }
    
    if (stdout_pipe_ != -1) {
        close(stdout_pipe_);
        stdout_pipe_ = -1;
    }
}

std::string MCPClient::buildJSONRPCRequest(const MCPRequest& request) {
    Json::Value root;
    root["jsonrpc"] = "2.0";
    root["method"] = request.method;
    root["id"] = request.id;
    
    if (!request.params.empty()) {
        Json::Value params;
        Json::Reader reader;
        if (reader.parse(request.params, params)) {   // JSON-RPC 2.0 规定 params 必须是对象 / 数组  , objectValue / arrayValue
            root["params"] = params;
        }
    }
    
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, root);
}

MCPResponse MCPClient::parseJSONRPCResponse(const std::string& response) {
    MCPResponse mcp_response;
    
    try {
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(response, root)) {
            mcp_response.id = root["id"].asString();
            
            if (root.isMember("error")) {
                mcp_response.is_error = true;
                mcp_response.error = root["error"]["message"].asString();
            } else {
                mcp_response.is_error = false;
                mcp_response.result = root["result"].toStyledString();
            }
        }
    } catch (const std::exception& e) {
        mcp_response.is_error = true;
        mcp_response.error = "Failed to parse response: " + std::string(e.what());
    }
    
    return mcp_response;
}

// ============================================================================
// SSE (Server-Sent Events) 传输实现
// ============================================================================

bool MCPClient::connectSSE() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        LOG_ERROR("Failed to initialize libcurl for SSE");
        return false;
    }

    running_ = true;
    clearSSESession();
    sse_event_thread_ = std::thread([this] { processNotificationsSSE(); });

    std::unique_lock<std::mutex> lock(sse_mutex_);
    const auto timeout = std::chrono::milliseconds(
        config_.connect_timeout_ms > 0 ? config_.connect_timeout_ms : 5000);
    if (sse_cv_.wait_for(lock, timeout, [this] {
            return !sse_message_endpoint_.empty() || !running_.load();
        })) {
        LOG_INFO("SSE connection established, session: " + sse_session_id_);
        return !sse_message_endpoint_.empty();
    }
    lock.unlock();
    LOG_ERROR("Timed out waiting for the SSE endpoint event");
    disconnectSSE();
    return false;
}

void MCPClient::disconnectSSE() {
    running_ = false;
    sse_cv_.notify_all();
    if (sse_event_thread_.joinable()) {
        sse_event_thread_.join();
    }
    clearSSESession();
    curl_global_cleanup();
    LOG_INFO("SSE connection closed");
}

// POST 发送 MCP json-rpc 请求
bool MCPClient::sendRequestSSE(const MCPRequest& request) {
    std::string post_url;
    {
        std::lock_guard<std::mutex> lock(sse_mutex_);
        post_url = sse_message_endpoint_;
    }
    if (!running_.load() || post_url.empty()) {
        LOG_ERROR("SSE endpoint is not available");
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("Failed to create CURL handle for SSE request");
        return false;
    }

    std::string json_request = buildJSONRPCRequest(request);
    curl_easy_setopt(curl, CURLOPT_URL, post_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_request.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_request.length());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(config_.request_timeout_ms));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(config_.connect_timeout_ms));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    std::string response_data;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, 
        +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            std::string* data = static_cast<std::string*>(userdata);
            data->append(ptr, size * nmemb);
            return size * nmemb;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);  // 调上面 callback 的时候，把 &response_data 当 userdata 传进去
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!config_.api_key.empty()) {
        std::string auth_header = "Authorization: Bearer " + config_.api_key;
        headers = curl_slist_append(headers, auth_header.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (!config_.verify_ssl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    
    const CURLcode res = curl_easy_perform(curl);  // 真正发送请求，默认阻塞
    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);  // 获取响应状态吗
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || http_status < 200 || http_status >= 300) {
        LOG_ERROR("SSE request failed: " + std::string(curl_easy_strerror(res)));
        return false;
    }
    // POST only acknowledges receipt. JSON-RPC responses always arrive on SSE.
    return true;
}

// 从响应队列中拿取响应
MCPResponse MCPClient::receiveResponseSSE() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    
    // 等待响应
    int timeout_ms = config_.request_timeout_ms > 0 ? config_.request_timeout_ms : 30000;
    if (queue_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                          [this] { return !response_queue_.empty(); })) {
        MCPResponse response = response_queue_.front();
        response_queue_.pop();
        return response;
    }
    
    MCPResponse timeout_response;
    timeout_response.is_error = true;
    timeout_response.error = "SSE request timeout";
    return timeout_response;
}

// 长期连接 sse, 不断接收服务器推送的数据
void MCPClient::processNotificationsSSE() {
    // Reconnect is transport reconnect, not session resume: every GET /sse
    // gets a new endpoint and invalidates the old session on the server.
    unsigned int backoff_seconds = 1;  // 断线重连的退避时间
    while (running_.load()) {    // 每一次循环都是一次 SSE 连接，连接断开后，重新连接
        clearSSESession();

        CURL* curl = curl_easy_init();  // 一次具体的请求，这里就是 GET /sse
        CURLM* multi = curl_multi_init();
        if (!curl || !multi) {
            if (curl) { curl_easy_cleanup(curl); }
            if (multi) { curl_multi_cleanup(multi); }
            LOG_ERROR("Failed to create libcurl SSE handles");
            running_ = false;
            sse_cv_.notify_all();
            return;
        }

        const std::string url = sseStreamUrl();  // 构造url 类似 http://127.0.0.1:8080/sse
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: text/event-stream");  // 客户端希望服务器返回 SSE stream，而且不要使用缓存。
        headers = curl_slist_append(headers, "Cache-Control: no-cache");
        if (!config_.api_key.empty()) {
            const std::string auth_header = "Authorization: Bearer " + config_.api_key;
            headers = curl_slist_append(headers, auth_header.c_str());
        }
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sseWriteCallback);  // 关键！！ 设置回调，服务器每推送一部分 SSE 数据过来，libcurl 就调用 sseWriteCallback
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, this); // 将this也就是MCPClient* 传给上一行的回调函数，就是userdata
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(config_.connect_timeout_ms));
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
        // The server emits a keepalive every 15 seconds, so this detects a
        // stalled stream without treating ordinary idle time as a failure.
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 45L);
        if (!config_.verify_ssl) {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        }

        curl_multi_add_handle(multi, curl);  // 把这个 GET /sse 请求交给 multi handle 管理
        int transfers_running = 0;
        curl_multi_perform(multi, &transfers_running); // transfers_running 是当前正在进行的传输数量（这里只有1/0），libcurl设置的；当连接由于各种原因结束，libcurl会将这个transfer标记为完成，下次执行perform后transfers_running==0
        while (running_.load() && transfers_running > 0) {  // 如果连接一直正常，永远卡在这个循环
            int ready = 0;
            curl_multi_poll(multi, nullptr, 0, 1000, &ready);
            curl_multi_perform(multi, &transfers_running);
        }

        CURLcode result = CURLE_OK;
        int messages = 0;
        while (CURLMsg* message = curl_multi_info_read(multi, &messages)) {  // 读取transfer结束的原因
            if (message->msg == CURLMSG_DONE) { result = message->data.result; }
        }
        curl_multi_remove_handle(multi, curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        curl_multi_cleanup(multi);

        if (!running_.load()) { break; }
        bool session_was_established = false;
        {
            std::lock_guard<std::mutex> lock(sse_mutex_);
            session_was_established = !sse_message_endpoint_.empty();
        }
        // A stream that reached the endpoint event was a successful
        // connection. A later disconnect starts a fresh backoff sequence.
        if (session_was_established) { backoff_seconds = 1; }
        LOG_WARN("SSE stream ended: " + std::string(curl_easy_strerror(result)) + "; reconnecting");
        std::unique_lock<std::mutex> lock(sse_mutex_);
        sse_cv_.wait_for(lock, std::chrono::seconds(backoff_seconds), [this] { return !running_.load(); });  //等待退避时间，下一轮while循环重新建立sse
        backoff_seconds = std::min(backoff_seconds * 2U, 30U);
    }
}

// libcurl 回调函数，每当服务器推送 SSE 数据过来，libcurl 就调用这个函数
// 将一次sse收到的数据追加到缓存；然后检查‘\n\n‘,切分出来完整的event，交给processSSEEvent处理
size_t MCPClient::sseWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {    // llbcurl规定的函数回调函数签名
    MCPClient* client = static_cast<MCPClient*>(userdata);
    size_t total_size = size * nmemb;
    std::vector<std::string> complete_events;
    {
        std::lock_guard<std::mutex> lock(client->sse_mutex_);
        client->sse_response_buffer_.append(ptr, total_size);
        while (true) {
            const auto lf = client->sse_response_buffer_.find("\n\n");
            const auto crlf = client->sse_response_buffer_.find("\r\n\r\n");
            const auto pos = std::min(lf, crlf);
            if (pos == std::string::npos) { break; }
            const size_t delimiter_size = pos == crlf ? 4 : 2;
            complete_events.push_back(client->sse_response_buffer_.substr(0, pos));
            client->sse_response_buffer_.erase(0, pos + delimiter_size);
        }
    }
    for (const auto& event : complete_events) { client->processSSEEvent(event); }
    return total_size;
}

void MCPClient::processSSEEvent(const std::string& event) {
    std::string event_type;
    std::vector<std::string> data_lines;
    std::istringstream stream(event);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        if (line.empty() || line.front() == ':') { continue; } // 遇到:开头直接忽略，一般是:ping, SSE keepalive/comment
        const auto colon = line.find(':');
        const std::string field = line.substr(0, colon);
        std::string value = colon == std::string::npos ? "" : line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') { value.erase(0, 1); }
        if (field == "event") {
            event_type = value;
        } else if (field == "data") {
            data_lines.push_back(std::move(value));
        }
    }
    if (data_lines.empty()) { return; }

    std::string data;
    for (size_t i = 0; i < data_lines.size(); ++i) {
        if (i != 0) { data.push_back('\n'); }
        data += data_lines[i];
    }
    if (event_type == "endpoint") {  // 处理 第一次发送GET /sse 后，服务器返回的 endpoint event，里面包含了 session_id 和 message_endpoint
        const auto session_marker = data.find("session_id=");
        const std::string session_id = session_marker == std::string::npos
            ? "" : data.substr(session_marker + std::strlen("session_id="));
        {
            std::lock_guard<std::mutex> lock(sse_mutex_);
            sse_message_endpoint_ = absoluteSSEEndpoint(data);
            sse_session_id_ = session_id;
        }
        sse_cv_.notify_all();
        return;
    }
    processSSEData(data);  // 如果不是endpoint，一般是对某个POST请求的响应，处理data
}

void MCPClient::processSSEData(const std::string& data) {
    MCPResponse response = parseJSONRPCResponse(data);
    if (!response.id.empty()) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        response_queue_.push(std::move(response));    // 入队
        queue_cv_.notify_one();
        return;
    }

    if (!notification_callback_) { return; }
    try { // 如果client设置了通知回调,执行；一般是调用tool_manager_->processNotification，用于 refreshTools();（没有实际应用）
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(data, root) && root["method"].asString() == "notifications/message") {
            const Json::Value& params = root["params"];
            notification_callback_(params["pluginName"].asString(), params["notification"].asString());
        }
    } catch (const std::exception& error) {
        LOG_WARN("Failed to parse SSE notification: " + std::string(error.what()));
    }
}

void MCPClient::clearSSESession() {
    std::lock_guard<std::mutex> lock(sse_mutex_);
    sse_session_id_.clear();
    sse_message_endpoint_.clear();
    sse_response_buffer_.clear();
}

std::string MCPClient::sseStreamUrl() const {
    std::string url = config_.sse_url;
    while (!url.empty() && url.back() == '/') { url.pop_back(); }
    if (url.size() >= 4 && url.compare(url.size() - 4, 4, "/sse") == 0) { return url; }
    return url + "/sse";
}

std::string MCPClient::absoluteSSEEndpoint(const std::string& endpoint) const {
    if (endpoint.rfind("http://", 0) == 0 || endpoint.rfind("https://", 0) == 0) { return endpoint; }
    const std::string stream_url = sseStreamUrl();
    const auto scheme = stream_url.find("://");
    const auto path = scheme == std::string::npos ? std::string::npos : stream_url.find('/', scheme + 3);
    if (!endpoint.empty() && endpoint.front() == '/' && path != std::string::npos) {
        return stream_url.substr(0, path) + endpoint;
    }
    const auto slash = stream_url.rfind('/');
    return (slash == std::string::npos ? stream_url + "/" : stream_url.substr(0, slash + 1)) + endpoint;
}

} // namespace mcp
} // namespace agent_rpc
