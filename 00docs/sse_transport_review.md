# MCP SSE Transport 审查与重构报告

审查范围为 `mcp_server/src/transport/SseTransport.*` 和
`mcp_client/src/mcp_client.cpp` 中的 SSE 部分。本次保留旧版 MCP 的双通道
语义，不迁移至 Streamable HTTP。

## 1. Current Architecture

重构前，服务端用一个全局 `client_connected_`/`sse_active_` 和两条全局队列
表示所有连接；`GET /sse` 创建流，`POST /messages` 直接进入 incoming queue。
客户端先同步执行一次 SSE `GET`，随后又在线程中建立第二次 `GET`，并从 POST
URL 自行拼接 session 参数。

最终架构如下：

```text
             GET /sse
Client --------------------------> Server
       <========================== chunked SSE stream
       event: endpoint
       data: /messages?session_id=S1

       POST /messages?session_id=S1
Client --------------------------> Server -> incoming_messages_ -> dispatcher

Client <========================== outgoing_messages_ <- Write(JSON-RPC)
                                  data: JSON-RPC\n\n
```

服务端只允许一个 active session；SSE provider、outgoing queue 和 session id 都
属于该会话。客户端仅运行一个接收/重连线程，POST 使用 endpoint 事件实际下发的
地址。

## 2. Server Review

- **SSE response**：原实现设置了 `Content-Type`、`Cache-Control` 和
  `Connection`，但使用无长度 content provider。现改为
  `set_chunked_content_provider("text/event-stream", ...)`，更适合无限 SSE
  response，并保留 `Cache-Control: no-cache`。
- **DataSink**：`sink.write` 向当前 HTTP response stream 写字节；失败是关闭
  session 的依据。`sink.is_writable()` 仅作提前提示，不能证明对端仍健康。TCP
  断开也可能直到下一次写（这里至多下一次 keepalive）才被发现。
- **keepalive**：每 15 秒发送 `: ping\n\n`。这是单向 SSE keepalive，不是
  应用层 ping-pong；不会进入 JSON-RPC。
- **session / lifecycle**：`GET /sse` 创建 session id 并立即发送 endpoint。
  第二条 SSE 连接返回 409；`POST` 需带当前 id，且连接仍 active，否则返回 409。
  `CloseSession` 是唯一的会话关闭路径，会失效 id、清空遗留 outgoing 消息并唤醒
  等待者。已接收的 incoming 请求不清空，避免在上层已开始处理时静默丢请求。
- **queues**：incoming queue 仍交给 `Read()`/`ReadAsync()`；outgoing queue 在
  session 锁保护下写入，在旧会话结束时清空，因此不会投递给下一会话。
- **Stop**：先标记 server 停止、关闭 active session 并通知 CV，再停止 httplib
  server、join listener thread。provider 不会保留或异步使用已经结束的 DataSink。

## 3. Client Review

- **GET /sse**：不再先执行阻塞 GET 再启动第二个 GET；唯一 SSE receive thread
  通过 libcurl multi 维持该 GET，`connectSSE()` 等待 endpoint event。
- **parser**：write callback 先把字节追加到受锁保护的 buffer，再抽取完整的
  `\n\n` 或 `\r\n\r\n` 事件。因此可处理拆包和一次 callback 中多个事件。它识别
  `event:`、多行 `data:`、空行和 `:` 注释；注释被忽略。
- **endpoint / POST**：`event: endpoint` 保存 server 给出的 POST 地址并从其
  query 提取 session id。普通 JSON-RPC data 进入原有 response/notification
  处理；POST 的 `{status:"received"}` 仅是确认，不会误入 response queue。
- **disconnect / reconnect / Stop**：SSE transfer 结束后以 1、2、4、8、16、30
  秒的上限退避重连。Stop 清除 `running_`、唤醒 backoff、在最长一次 1 秒 multi
  poll 后 join，且不会重连。连接配置使用 connect timeout、TCP keepalive 及 45 秒
  low-speed detection；15 秒 SSE keepalive 不会被误判为空闲超时。

## 4. Problems

| Priority | File / function | Current behavior and trigger | Fix |
| --- | --- | --- | --- |
| P0 | `SseTransport.cpp`, `HandlePostMessage` | endpoint 含 session id，但 POST 从不读取它；任意客户端可写入 incoming queue。 | 要求 `session_id`，验证等于 active session 且 SSE active，否则 409。 |
| P0 | `mcp_client.cpp`, `connectSSE` / `processNotificationsSSE` | 首次 `curl_easy_perform` 会等待长流超时，之后还会创建第二条 GET；服务器单连接状态会混乱。 | 改为唯一 receive/reconnect thread，并等待 endpoint event。 |
| P0 | `mcp_client.cpp`, `sseWriteCallback` | endpoint data 当 JSON-RPC 解析，未保存 POST endpoint；只接受 `\n\n`。 | 完整 SSE parser 保存 endpoint，支持 LF/CRLF 和 comments。 |
| P1 | `SseTransport.cpp`, provider | `thread_local first_call/last_ping` 属于 httplib worker，不属于连接；新连接可能不发 endpoint。 | 使用每连接 `SSEConnectionContext`。 |
| P1 | `SseTransport.cpp`, state updates | 多处独立改写两个全局 bool；旧 outgoing 消息可流入新连接。 | 单一 `CloseSession` 路径及 session 锁，关闭时清空 outgoing queue。 |
| P1 | `mcp_client.cpp`, shared fields | callback、POST 和接收线程并发读写 session/buffer，造成数据竞争。 | 使用 `sse_mutex_` 和 `sse_cv_`。 |
| P2 | `SseTransport.cpp`, provider wait | 每 200 ms 轮询，即使没有消息。 | 在 message/Stop 时立即唤醒，否则 `wait_until` 下一个 keepalive deadline。 |
| P2 | `mcp_client.cpp`, POST | POST 的 HTTP acknowledgement 被解析成 JSON-RPC response。 | 仅把 SSE data 作为 JSON-RPC response。 |
| P3 | `mcp_client.cpp`, reconnect | 固定 1 秒 sleep，Stop 期间无明确唤醒语义。 | 有界指数退避和 condition variable；multi poll 保证 Stop 可及时 join。 |

## 5. Explicitly Unsupported Features

Not supported by design:

- session resume；
- Last-Event-ID recovery；
- event replay；
- multi-node 或 persistent session；
- application-level ping-pong heartbeat；
- 高并发 multi-client support。

## Refactoring Result

已修复 session 真实性、连接上下文、SSE framing、endpoint 消费、POST response
误路由、旧消息泄漏和 reconnect 生命周期。保留 simple single-client、内存队列和
单向 keepalive 的设计。重连代表新的 transport connection：旧 session 随断线失效，
新 GET 获得新 endpoint；**reconnect 不等于 resume**。

测试新增 `tests/integration/test_server_sse.py`，覆盖 endpoint framing、有效
POST、SSE JSON-RPC response、无效 session 和第二条 SSE 连接被拒绝；
`mcp_client_sse_parser_test` 直接验证 client callback 的拆包、CRLF、comment 忽略和
一次 callback 两个 data event。前者在 port 8080 被开发者既有服务占用时跳过，避免
干扰该服务；完整行为在空闲端口环境下由 CTest 执行。stdio 回归测试保持通过。

## 修改说明（修改前、修改后与原因）

### 服务端 SSE 状态与会话

**修改前**：服务端使用 `client_connected_` 与 `sse_active_` 两个全局布尔值表示
连接状态。每条 `/sse` 流在 provider 中自行修改它们；endpoint 虽生成 session id，
但服务端没有保存该 id，也没有在 `/messages` 中验证。

**修改后**：删除了不能表达会话归属的 `client_connected_`，保留 `sse_active_` 表示
是否有活跃流，并新增 `active_session_id_`、`session_mutex_` 作为当前单会话的权威
状态。新增 `IsActiveSession()` 和 `CloseSession()`，集中处理校验与关闭。

**修改原因**：原来的全局 bool 容易在多个路径中产生不一致状态，也会让 session id
成为“只下发、不生效”的标识。新状态明确该 transport 是 single-client，能让 POST
请求与正在运行的 SSE 流真实绑定，并防止旧会话遗留消息进入新连接。

### 服务端连接 provider 与 SSE 输出

**修改前**：使用普通无长度 content provider；首帧与 keepalive 时间记录在
`thread_local` 变量中，并通过每 200 ms 的 `wait_for` 轮询队列。

**修改后**：改用 `set_chunked_content_provider`，并增加每连接的
`SSEConnectionContext`，保存 session id 与最近写入时间。provider 收到消息或 Stop
立即唤醒，无消息时等待至下一次 keepalive deadline；它输出 endpoint、JSON-RPC
`data:` event 或 `: ping` comment，并在实际写失败时统一关闭会话。

**修改原因**：`thread_local` 生命周期属于 httplib worker thread，不属于一条 SSE
连接，重用 worker 时可能污染新连接。chunked provider 更符合持续 SSE response；按
deadline 等待既减少空轮询，也保留 15 秒 keepalive 用于延迟发现断线。

### 服务端 POST 与停止流程

**修改前**：`HandlePostMessage()` 仅检查是否“有客户端连接”，任何 session id 或
没有 session id 的请求都能进入 incoming queue。Stop 与 provider 分散改写连接状态。

**修改后**：`HandlePostMessage()` 要求 `session_id` 存在且匹配 active session，
否则返回 409。Stop 调用统一关闭逻辑、唤醒等待者、停止 HTTP server 并 join listener
thread。

**修改原因**：POST 通道是旧版 MCP SSE 双通道的客户端到服务端入口，必须与 endpoint
下发的 session 关联。统一终止流程避免 Stop 后 provider 继续等待或下一会话收到旧
outgoing queue 内容。

### 客户端连接与重连

**修改前**：`connectSSE()` 先同步 `curl_easy_perform()` 执行一条 SSE GET，再启动
线程创建第二条 SSE GET；session id 试图从普通 JSON response 读取。重连固定 sleep
一秒。

**修改后**：删除不再需要的长期持有 `curl_handle_` / `curl_multi_` 成员。单一
`sse_event_thread_` 使用 libcurl multi 管理唯一 SSE GET；`connectSSE()` 等待该流收到
endpoint event。新增 `sse_message_endpoint_`、`sse_mutex_` 和 `sse_cv_`，并以 1 到
30 秒的有界指数退避重新建立 transport connection。每次成功收到 endpoint 后，后续
断线会重新从 1 秒开始退避。

**修改原因**：两条 GET 与服务端单会话设计冲突，也使初始连接可能一直阻塞。endpoint
event 才是旧版协议提供 POST 地址和 session 的来源。新成员变量还消除了 receive
callback、POST 调用和重连线程同时读写 session/buffer 的数据竞争。重连得到的是新的
session，并非旧 session resume。

### 客户端 SSE parser 与 POST

**修改前**：callback 假设 `\n\n` 分隔，未处理 CRLF、`event: endpoint` 或多条
`data:`；它会将 POST acknowledgement 错当成 JSON-RPC response。

**修改后**：parser 使用 `sse_response_buffer_` 保留未完成字节，支持 `\n\n` 和
`\r\n\r\n`、`event:`、多行 `data:`、空行与 `:` comment。endpoint 被保存为实际 POST
URL；只有 SSE data event 会进入 JSON-RPC response/notification 队列。

**修改原因**：HTTP/TCP callback 边界并不等于 SSE event 边界。正确累积与解析可避免
拆包丢消息、合包漏消息和 keepalive 被误解析；POST 的 HTTP body 只表示服务端已接收
请求，真正的 JSON-RPC 返回必须从 SSE stream 读取。

### 测试

**修改前**：仓库只有 stdio 相关集成测试，未覆盖 SSE session、endpoint 或 parser。

**修改后**：增加服务端 SSE 黑盒测试以及客户端 parser 测试，并注册到 CTest。

**修改原因**：这些测试覆盖本次重构最容易回归的协议边界：endpoint/session 绑定、
无效 session 拒绝、单客户端策略、拆包、CRLF、comment 与多 event callback。
