# MCP SSE Transport 审查与重构任务说明

请对当前项目中 **MCP Client / MCP Server 的 SSE Transport 实现**进行一次完整审查，并在审查后直接完成代码重构与修复。

本项目同时支持：

- `stdio transport`
- `SSE transport`

本次任务 **只聚焦 SSE transport**。

---

# 一、总体目标

本项目希望继续保留 **旧版 MCP SSE Transport 的经典双通道模型**：

```text
Client
  |
  | GET /sse
  v
Server
  |
  | 建立 SSE 长连接
  |
  | event: endpoint
  | data: /messages?session_id=xxx
  v
Client


Client
  |
  | POST /messages?session_id=xxx
  | JSON-RPC Request / Notification
  v
Server


Server
  |
  | SSE data event
  | JSON-RPC Response / Notification
  v
Client
```

即：

```text
Server -> Client    使用 GET /sse 建立 SSE 长连接
Client -> Server    使用独立 POST /messages 通道
```

这是本项目明确希望保留的设计。

---

# 二、重要约束

本次任务不是重新设计整个 MCP 项目，也不是迁移到新的 Streamable HTTP Transport。

请遵循以下原则：

```text
正确性
>
生命周期清晰
>
代码可读性
>
健壮性
>
保持现有架构
>
增加功能数量
```

## 必须保留

保留旧版 MCP SSE Transport 的整体模型：

```text
GET /sse
POST /messages
session_id
Server -> Client SSE
Client -> Server POST
```

## 可以重构

允许重构：

- Server SSE transport
- Client SSE transport
- SSE connection lifecycle
- session 管理
- SSE parser
- SSE writer
- reconnect
- keepalive
- transport 状态
- transport 层线程模型
- transport 层资源管理

## 尽量不要修改

除非 transport 无法工作，否则不要修改：

- ToolManager
- ResourceManager
- PromptManager
- PluginLoader
- Plugin API
- JSON-RPC 业务处理逻辑
- request dispatcher
- tool/resource/prompt 插件
- stdio transport 的业务行为

如果 transport 层接口必须调整，优先：

```text
通过 wrapper / adapter 保持上层接口兼容
```

不要借此任务重构整个项目。

---

# 三、不要过度设计

本项目不是高并发 SSE 服务，也不是完整的通用消息中间件。

因此以下能力 **默认不要求实现**：

```text
多节点 session replication
session 持久化
event replay
Last-Event-ID 恢复
跨进程 session resume
离线消息补发
复杂 ACK 机制
完整 ping-pong 应用层心跳
无限多 Client 高并发管理
WebSocket fallback
消息持久化
```

如果当前项目没有明确需求，请不要为了“更标准”而实现这些能力。

对于不支持的能力，应当明确写在代码注释和审查报告中，例如：

```text
Not supported by design:
- session resume
- event replay
- Last-Event-ID recovery
- multi-node session persistence
```

目标是：

> 实现一个简单、可靠、逻辑正确、能够清楚解释的旧版 MCP SSE Transport。

---

# 四、任务分两个阶段

必须按顺序执行。

---

# 阶段一：审查当前 Server / Client SSE 实现

首先不要修改代码。

请完整阅读当前仓库中的 SSE transport 实现，并输出：

```text
00docs/sse_transport_review.md
```

如果没有 `00docs/` 目录可以创建。

---

# 五、审查 Server 端实现

请找到所有 Server SSE 相关代码，包括但不限于：

```text
GET /sse
POST /messages
set_content_provider
set_chunked_content_provider
DataSink
sink.write
is_writable
session_id
outgoing queue
incoming queue
condition_variable
client_connected_
sse_active_
heartbeat
keepalive
terminate
server thread
```

重点审查以下内容。

---

## 1. SSE HTTP Response 是否正确

检查 `/sse` endpoint。

至少确认：

```text
Content-Type: text/event-stream
Cache-Control: no-cache
```

以及当前 HTTP 库所需的其他必要设置。

请判断当前项目应该使用：

```cpp
set_content_provider(...)
```

还是：

```cpp
set_chunked_content_provider(...)
```

如果 `cpp-httplib` 对 SSE 更推荐 chunked provider，则优先改成：

```cpp
res.set_chunked_content_provider(
    "text/event-stream",
    ...
);
```

但不要为了形式修改其他不相关逻辑。

---

## 2. DataSink 使用方式

检查：

```cpp
httplib::DataSink& sink
```

及：

```cpp
sink.write(...)
```

的使用是否正确。

明确：

```text
sink.write()
```

表示向当前 HTTP response stream 写数据。

当：

```cpp
sink.write(...)
```

失败时，可以认为当前 SSE stream 已无法继续正常发送。

但不要错误地认为：

```text
write成功 == 对端一定健康
```

也不要认为：

```text
Client网络断开后Server必然立即发现
```

请在报告中说明 TCP/HTTP 断连检测存在延迟。

---

# 六、检查 SSE Event 格式

Server 发送普通 JSON-RPC 消息时，应使用：

```text
data: <json>

```

实际字节：

```text
data: {...}\n\n
```

如果需要发送 endpoint：

```text
event: endpoint
data: /messages?session_id=xxx

```

这是允许的。

---

# 七、Keepalive 设计

项目希望保留简单的 Server 单向 keepalive：

```text
Server
   |
   | : ping\n\n
   v
Client
```

其中：

```text
: ping
```

是 SSE comment。

Client 收到后应该：

```text
忽略
```

不进入 JSON-RPC parser。

请统一称它为：

```text
SSE keepalive
```

而不要把它描述为完整：

```text
ping-pong heartbeat
```

因为 SSE 长连接是：

```text
Server -> Client
```

单向流。

本项目默认 **不实现应用层 ping-pong**。

不要设计：

```text
Server ping
Client POST pong
```

除非当前代码确实强依赖这一能力。

---

# 八、Server 端断连检测

当前设计允许使用：

```text
周期性 SSE keepalive
+
sink.write()
+
底层 HTTP/TCP 错误
```

来发现连接失效。

需要检查：

```cpp
sink.is_writable()
```

如果使用了它，请明确它只是辅助检查。

不要认为：

```text
is_writable == Client一定在线
```

真正的连接结束应主要依赖：

```text
实际 write 失败
HTTP stream 被关闭
Server shutdown
```

---

# 九、重点检查当前 Server 的 connection 状态设计

重点搜索：

```cpp
client_connected_
sse_active_
```

如果它们是 Server 全局状态，请分析：

```text
它们是不是实际上只能表示一个 Client？
```

如果是，则必须明确：

> 当前 Transport 是 single-client transport。

本次重构 **默认允许保持 single-client**。

不要求为了 session_id 强行实现多 Client。

但是如果保持 single-client，则要求：

```text
状态必须清晰
session 生命周期必须真实存在
不能出现假的多 Client 支持
```

---

# 十、重点检查 thread_local connection state

如果当前代码存在类似：

```cpp
static thread_local bool first_call = true;
static thread_local auto last_ping = clock::now();
```

请重点修复。

因为：

```text
thread_local 生命周期属于线程
```

而不是：

```text
SSE connection
```

一条新的 `/sse` connection 不应该复用某个 worker thread 上旧 connection 的状态。

请改成：

```text
per-connection context
```

例如可以使用：

```cpp
struct SSEConnectionContext {
    std::string session_id;
    bool endpoint_sent;
    std::chrono::steady_clock::time_point last_write_time;
};
```

具体名字可以根据现有代码调整。

不要机械照搬。

---

# 十一、Session 机制审查

请检查：

```text
session_id 是在哪里创建的
```

以及：

```text
POST /messages?session_id=xxx
```

Server 是否真的读取并验证了这个 session_id。

如果当前行为类似：

```text
生成 session_id
↓
发给 Client
↓
Client POST 时带回来
↓
Server 根本不读取 session_id
```

则这是明确问题。

需要修复。

---

# 十二、本项目推荐的 Session 设计

默认保持：

```text
single-client
```

即可。

因此不需要设计复杂 SessionManager。

可以采用简单模型：

```text
SSE Server
   |
   +-- active_session
           |
           +-- session_id
           +-- connection_active
           +-- outgoing queue
           +-- connection context
```

要求：

```text
同时只允许一个 active SSE session
```

如果第二个 Client 尝试建立 `/sse`：

请选择一种清晰策略：

### 方案 A

拒绝第二条连接：

```text
409 Conflict
```

或者其他合理 HTTP 状态。

### 方案 B

关闭旧 connection，接受新 connection。

二选一即可。

优先选择更容易保持当前代码稳定的方案。

不要实现复杂多 Client 管理。

---

# 十三、POST /messages session 验证

Client 建立 `/sse` 后，Server 下发：

```text
event: endpoint
data: /messages?session_id=<id>
```

之后：

```text
POST /messages?session_id=<id>
```

必须检查：

```text
session_id是否存在
session_id是否等于当前active session
当前SSE connection是否active
```

如果不匹配，应返回明确错误。

例如：

```text
400 Bad Request
404 Session Not Found
409 Invalid Session
```

根据现有代码风格选择一种即可。

不要无条件接受所有 POST。

---

# 十四、Server outgoing queue

当前逻辑可以继续保留：

```text
MCP上层
  |
  | Write(json)
  v
outgoing_messages_
  |
  | notify
  v
SSE provider
  |
  | sink.write
  v
Client
```

这种 producer-consumer 模型是可以接受的。

需要检查：

```text
outgoing queue 是否只属于当前 active session
```

如果 single-client，则可以继续使用一个全局队列。

但连接结束时应明确：

```text
是否清空旧 session 遗留消息
```

避免新 connection 收到旧 connection 的消息。

---

# 十五、Server incoming queue

Client：

```text
POST /messages
```

之后进入：

```text
incoming_messages_
```

再由：

```text
Read()
ReadAsync()
```

交给上层。

该结构可以保留。

只需要确保：

```text
POST必须经过有效session验证
```

---

# 十六、Server Provider 循环

请优化当前 SSE provider 的等待机制。

如果存在：

```cpp
wait_for(... 200ms ...)
```

不断轮询：

```text
outgoing queue
connection state
keepalive time
```

可以改进为：

```text
有消息 -> 条件变量立即唤醒

Stop -> 条件变量立即唤醒

无消息 -> 等到下一个 keepalive deadline
```

例如：

```text
wait_until(next_keepalive_time)
```

避免每 200ms 无意义唤醒。

但如果重构风险较高，可以保留简单实现。

这属于：

```text
P2/P3优化
```

不是核心功能要求。

---

# 十七、Server connection termination

请统一 connection 关闭逻辑。

不要多个地方分别写：

```cpp
client_connected_ = false;
sse_active_ = false;
notify_all();
```

建议封装成类似：

```cpp
TerminateSSEConnection(...)
```

或者：

```cpp
CloseSession(...)
```

统一完成：

```text
标记connection inactive
标记session inactive
通知outgoing_cv
通知incoming_cv
清理connection context
必要时清空旧outgoing queue
```

不要使用复杂状态机。

---

# 十八、Server Stop

检查：

```cpp
SSE::Stop()
```

确保：

```text
停止HTTP Server
终止SSE stream
唤醒等待线程
join server thread
状态恢复一致
```

避免：

```text
Stop以后provider仍在等待
```

或者：

```text
keepalive仍尝试写已释放DataSink
```

---

# 十九、审查 Client 端实现

请找到所有 SSE Client 相关代码，包括：

```text
GET /sse
curl_easy_perform
write callback
SSE parser
POST /messages
session_id
endpoint event
reconnect
sleep
thread
stop
shutdown
```

---

# 二十、Client SSE 连接流程

最终希望 Client 逻辑保持简单：

```text
Start
  |
  v
GET /sse
  |
  v
接收 event: endpoint
  |
  v
保存 /messages?session_id=xxx
  |
  v
后续 Request 通过 POST /messages
  |
  v
Response / Notification 从 SSE stream 收到
```

不要改变旧版 MCP SSE 双通道设计。

---

# 二十一、Client SSE Parser

这是重点审查项。

绝对不要假设：

```text
一次 curl callback == 一个完整 SSE event
```

因为实际 HTTP/TCP 流可能：

```text
一个event被拆成多次callback
```

或者：

```text
一次callback包含多个event
```

需要维护接收 buffer。

例如：

```cpp
std::string sse_buffer_;
```

每次 curl callback：

```text
append bytes
↓
查找完整 event 分隔符
↓
解析完整 event
↓
剩余 partial bytes 保留
```

至少处理：

```text
\n\n
\r\n\r\n
```

---

# 二十二、Client Parser 至少支持

```text
data:
event:
:
空行
```

其中：

```text
: ping
```

必须：

```text
直接忽略
```

不能进入 JSON-RPC parser。

---

# 二十三、Client endpoint event

当收到：

```text
event: endpoint
data: /messages?session_id=xxx
```

Client 应保存：

```text
message endpoint
session_id
```

后续 Client -> Server 请求统一走这个 endpoint。

---

# 二十四、Client JSON-RPC data event

普通：

```text
data: {"jsonrpc":"2.0", ...}
```

应交给当前 MCP Client 上层已有处理逻辑。

不要重构 JSON-RPC 业务模块。

---

# 二十五、Client 断连检测

Client 可以继续使用：

```cpp
curl_easy_perform()
```

维持 SSE GET。

当 SSE connection：

```text
Server主动关闭
网络错误
HTTP错误
Client Stop
```

时：

```text
curl_easy_perform()
```

会最终退出。

请正确区分：

```text
主动Stop
```

和：

```text
异常断线
```

不要主动 stop 后又触发 reconnect。

---

# 二十六、Client curl 配置

请检查是否合理设置：

```text
connect timeout
TCP keepalive
low speed detection
```

可以考虑：

```cpp
CURLOPT_CONNECTTIMEOUT
CURLOPT_TCP_KEEPALIVE
CURLOPT_TCP_KEEPIDLE
CURLOPT_TCP_KEEPINTVL
```

必要时可以考虑：

```cpp
CURLOPT_LOW_SPEED_LIMIT
CURLOPT_LOW_SPEED_TIME
```

但不要设置过短的普通 request timeout。

SSE 是长连接。

不要出现：

```text
5秒没业务消息就认为连接失败
```

Server 的 SSE keepalive 会周期性提供数据。

---

# 二十七、Client reconnect

当前如果是：

```cpp
while (...) {
    curl_easy_perform(...);
    sleep(1);
}
```

可以保留“断线后重新 GET /sse”的设计。

但进行小幅改进。

推荐：

```text
1s
2s
4s
8s
16s
30s
30s
...
```

bounded exponential backoff。

不需要实现复杂 reconnect framework。

可以不加 jitter。

---

# 二十八、Reconnect 的明确语义

必须在代码和文档中明确：

当前项目支持：

```text
transport reconnect
```

即：

```text
旧SSE断开
↓
重新GET /sse
↓
Server创建新session
↓
Client获得新的POST endpoint
```

当前项目 **不支持**：

```text
session resume
event replay
Last-Event-ID恢复
```

因此：

```text
reconnect != resume
```

请明确写出来。

---

# 二十九、Reconnect 后的 session

连接断开后：

```text
旧session失效
```

Client reconnect 后：

```text
获取新的session_id
```

不要尝试继续使用旧：

```text
/messages?session_id=old
```

新的 endpoint event 收到后应覆盖旧 endpoint。

---

# 三十、Client Stop

Client 调用：

```text
Stop
```

以后必须：

```text
结束当前SSE connection
退出reconnect loop
join接收线程
不再重新GET /sse
```

如果 reconnect 正处于 backoff：

```text
Stop
```

最好能立即唤醒。

可以用：

```text
condition_variable
```

代替裸：

```text
sleep()
```

但不要因此引入复杂线程框架。

---

# 三十一、线程模型

尽量减少线程数量。

Server 端优先保持：

```text
cpp-httplib worker
+
现有server thread
```

Client 端优先保持：

```text
一个SSE receive/reconnect thread
```

如果现有代码存在额外：

```text
heartbeat thread
reconnect thread
receive thread
```

请判断是否可以合并。

目标是：

```text
transport线程生命周期容易解释
```

而不是追求复杂异步框架。

---

# 三十二、不要引入复杂 Connection State Machine

如果当前多个 bool：

```cpp
client_connected_
sse_active_
running_
```

确实造成状态混乱，可以适当整理。

但是不要实现复杂十几个状态的状态机。

可以保留：

```text
server_running
session_active
client_stopping
```

这种简单状态。

如果 enum 能明显提高可读性，可以使用简单 enum。

---

# 三十三、资源管理

重点检查：

```text
CURL*
curl_slist*
thread
condition_variable
DataSink生命周期
connection context
session id
queue
```

确保没有：

```text
use-after-free
thread未join
旧connection状态污染新connection
Stop后继续reconnect
旧session消息进入新session
```

允许适度 RAII 重构。

但只处理 transport 层。

---

# 三十四、stdio transport

SSE 重构完成以后必须确保：

```text
stdio transport行为不受影响
```

SSE 特有概念：

```text
session_id
keepalive
reconnect
HTTP endpoint
```

不要进入 stdio transport。

---

# 三十五、阶段一审查报告格式

请在：

```text
00docs/sse_transport_review.md
```

中包含以下内容。

## 1. Current Architecture

画出当前：

```text
Client
Server
GET /sse
POST /messages
incoming queue
outgoing queue
```

流程。

## 2. Server Review

逐项说明：

```text
SSE response
DataSink
keepalive
session
connection lifecycle
outgoing queue
incoming queue
Stop
```

## 3. Client Review

说明：

```text
GET /sse
curl callback
parser
endpoint
POST
disconnect
reconnect
Stop
```

## 4. Problems

按照：

```text
P0：明确功能/协议错误
P1：高风险生命周期问题
P2：健壮性问题
P3：优化
```

分类。

每个问题写：

```text
文件
类
函数
当前行为
问题
触发条件
修复方式
```

## 5. Explicitly Unsupported Features

明确写：

```text
session resume
Last-Event-ID recovery
event replay
multi-node session
persistent session
application ping-pong heartbeat
high-concurrency multi-client support
```

如果最终决定支持其中某项，再从列表删除。

---

# 阶段二：直接完成代码重构

完成：

```text
00docs/sse_transport_review.md
```

之后直接修改代码。

不要等用户确认。

---

# 三十六、Server 推荐重构目标

最终 Server 逻辑应接近：

```text
GET /sse
   |
   v
检查当前是否已有active session
   |
   v
创建session_id
   |
   v
建立SSE stream
   |
   v
发送endpoint event
   |
   v
等待:
   - outgoing message
   - keepalive deadline
   - Stop
   |
   +------ outgoing ------> data: JSON\n\n
   |
   +------ keepalive -----> : ping\n\n
   |
   +------ write fail ----> CloseSession
```

---

# 三十七、POST /messages 推荐逻辑

```text
POST /messages?session_id=xxx
        |
        v
解析session_id
        |
        v
是否等于当前active session
        |
     yes/no
      /   \
     /     \
 incoming   返回错误
 queue
```

---

# 三十八、Client 推荐重构目标

```text
SSE thread
   |
   v
GET /sse
   |
   v
curl_easy_perform
   |
   +----------------------------+
   |                            |
receive callback              return
   |                            |
   v                            v
SSEParser                  classify reason
   |                            |
   +-- endpoint                 |
   |     |                      |
   |     v                      |
   |  save POST URL             |
   |                            |
   +-- data                     |
   |     |                      |
   |     v                      |
   |  JSON-RPC handler          |
   |                            |
   +-- comment                  |
         ignore                 |
                                |
                         abnormal disconnect?
                                |
                          yes /     \ no
                              /       \
                         backoff      exit
                            |
                            v
                       reconnect
```

---

# 三十九、测试要求

优先使用现有测试框架。

如果没有，可以增加简单 transport 测试。

至少验证：

## Test 1

正常建立：

```text
GET /sse
endpoint event
POST /messages
SSE response
```

## Test 2

SSE comment：

```text
: ping\n\n
```

Client 忽略。

## Test 3

SSE parser 拆包：

```text
callback1:
data: {"json

callback2:
rpc":"2.0"}\n\n
```

能够正确合并。

## Test 4

一次 callback 多个 event：

```text
data: A\n\ndata: B\n\n
```

解析两个 event。

## Test 5

无效 session：

```text
POST /messages?session_id=wrong
```

Server 拒绝。

## Test 6

Client 断开：

Server 最终结束 SSE session。

## Test 7

Server 关闭：

Client SSE connection 退出并开始 reconnect。

## Test 8

Server 恢复：

Client reconnect 成功，并获取新 session。

## Test 9

Client Stop：

```text
Stop()
```

之后不再 reconnect。

## Test 10

旧 session：

reconnect 以后旧 session POST 应失败。

---

# 四十、代码修改后重新更新审查报告

完成代码后，在：

```text
00docs/sse_transport_review.md
```

增加：

```text
## Refactoring Result
```

包含：

```text
已修复
保留的设计
明确不支持
剩余限制
```

---

# 四十一、最终输出总结

最终回复中请给出：

## Changed Files

所有修改文件。

## Server Changes

Server SSE 修改内容。

## Client Changes

Client SSE 修改内容。

## Fixed Issues

对应：

```text
P0
P1
P2
P3
```

哪些已经修复。

## Unsupported By Design

至少明确：

```text
session resume
event replay
Last-Event-ID recovery
application-level ping-pong
high-concurrency multi-client
```

如果实际实现不同，以最终代码为准。

## Final Transport Flow

画出最终通信图。

例如：

```text
                GET /sse
Client ----------------------------> Server
       <============================
             SSE stream

       event: endpoint
       data: /messages?session_id=S1
       <============================


       POST /messages?session_id=S1
Client ----------------------------> Server
               JSON-RPC


Server
  |
  | outgoing queue
  v
data: JSON-RPC
  |
  +===============================> Client
```

---

# 四十二、核心设计结论

本项目最终希望得到的是：

> 一个基于旧版 MCP HTTP + SSE Transport 语义的简单实现。

它应满足：

```text
单Client即可
真实session关联
标准SSE事件格式
SSE comment keepalive
正确SSE流式parser
Server通过write/HTTP错误发现断连
Client通过curl stream结束发现Server断连
简单可靠的transport reconnect
reconnect后创建新session
明确不支持session resume/event replay
线程和资源生命周期清晰
不影响stdio transport
```

不要为了实现“完整 SSE 系统”引入过多框架、状态机或高级能力。

最终目标：

> 让代码结构足够简单，能够在面试中清晰解释 Server / Client 两端的 SSE 建连、消息传输、keepalive、session、断连检测、重连和资源清理流程。