#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// Exercise the transport callback at its public libcurl boundary while keeping
// the production client API unchanged.
#define private public
#include "agent_rpc/mcp/mcp_client.h"
#undef private

#include <cassert>

int main() {
    using agent_rpc::mcp::MCPClient;

    MCPClient client;
    client.config_.sse_url = "http://127.0.0.1:8080";

    const std::string first = "event: endpoint\r\ndata: /messages?session_id=S1\r\n\r";
    assert(MCPClient::sseWriteCallback(const_cast<char*>(first.data()), 1, first.size(), &client) == first.size());
    assert(client.sse_message_endpoint_.empty()); // event was split across callbacks

    const std::string second =
        "\n: ping\n\n"
        "data: {\"jsonrpc\":\"2.0\",\"id\":\"one\",\"result\":{}}\n\n"
        "data: {\"jsonrpc\":\"2.0\",\"id\":\"two\",\"result\":{}}\n\n";
    assert(MCPClient::sseWriteCallback(const_cast<char*>(second.data()), 1, second.size(), &client) == second.size());

    assert(client.sse_message_endpoint_ == "http://127.0.0.1:8080/messages?session_id=S1");
    assert(client.sse_session_id_ == "S1");
    assert(client.response_queue_.size() == 2); // comment was ignored; both data events were retained
    assert(client.response_queue_.front().id == "one");
    client.response_queue_.pop();
    assert(client.response_queue_.front().id == "two");
    return 0;
}
