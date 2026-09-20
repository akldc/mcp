# MCP Server 源码学习导读

这份导读只看 `mcp_server/`，建议按“入口 → 协议分发 → 传输 → 插件 → 通知”的顺序读。先用 STDIO 跑通一次 `tools/list` 和 `tools/call`，再看 SSE；这样能把协议处理逻辑与网络细节分开理解。后续学习 `mcp_client`、`MCPToolManager` 和 Agent 层时，可以把这里的 JSON-RPC 请求/响应当作双方的边界。

## 一张图看层级

```mermaid
flowchart LR
    C[客户端 JSON-RPC] --> T[ITransport: Stdio 或 SSE]
    T --> S[Server::Connect 读取和写回]
    S --> D[Server::HandleRequest / functionMap]
    D --> H[main.cpp 注册的 tools/call 等回调]
    H --> L[PluginsLoader::m_plugins]
    L --> P[PluginAPI::HandleRequest]
    P -. 主动通知 .-> N[ClientNotificationCallbackImpl]
    N --> Q[Server::SendNotification 队列]
    Q --> W[Server::WriterLoop 写线程]
    W --> T
```

`Server` 负责协议分发和并发写出，不负责具体工具计算；`ITransport` 负责收发字节，不判断 `tools/call` 的语义；插件负责工具的定义与执行。`main.cpp` 是把这几层装配起来的地方。

## 目录与核心对象

| 位置 | 应重点理解的对象 | 职责 |
| --- | --- | --- |
| [`src/main.cpp`](../mcp_server/src/main.cpp) | `main`、`ClientNotificationCallbackImpl` | 解析参数、选传输、初始化日志、加载插件、注册回调、进入主循环 |
| [`src/server/Server.h`](../mcp_server/src/server/Server.h)、[`Server.cpp`](../mcp_server/src/server/Server.cpp) | `vx::mcp::Server` | `functionMap` 方法路由、请求处理、响应写出、通知队列与写线程、停止流程 |
| [`src/interface/ITransport.h`](../mcp_server/src/interface/ITransport.h) | `vx::ITransport` | `Start/Stop`、`Read/Write`、异步接口的抽象边界 |
| [`src/transport/StdioTransport.cpp`](../mcp_server/src/transport/StdioTransport.cpp) | `vx::transport::Stdio` | 从标准输入逐行读取 JSON，在标准输出逐行写回 |
| [`src/transport/SseTransport.cpp`](../mcp_server/src/transport/SseTransport.cpp) | `vx::transport::SSE` | HTTP `/sse` 事件流、`/messages` 请求入口，以及传给 `Server` 的收发队列 |
| [`src/loader/PluginsLoader.h`](../mcp_server/src/loader/PluginsLoader.h)、[`PluginsLoader.cpp`](../mcp_server/src/loader/PluginsLoader.cpp) | `vx::mcp::PluginsLoader`、`PluginEntry` | 遍历动态库，加载工厂符号，调用插件初始化，保存到 `m_plugins` |
| [`src/interface/PluginAPI.h`](../mcp_server/src/interface/PluginAPI.h) | `PluginAPI`、`PluginTool`、`NotificationSystem` | 主程序与动态库之间的函数指针接口和数据结构 |
| [`src/utils/MCPBuilder.h`](../mcp_server/src/utils/MCPBuilder.h) | `MCPBuilder` | 构造 JSON-RPC 响应、错误、文本内容与通知 |
| [`plugins/calculator/Calculator.cpp`](../mcp_server/plugins/calculator/Calculator.cpp)、[`plugins/notification/Notification.cpp`](../mcp_server/plugins/notification/Notification.cpp) | 示例插件 | 前者适合学习普通工具调用，后者适合学习服务端主动通知 |

构建入口是 [`mcp_server/CMakeLists.txt`](../mcp_server/CMakeLists.txt)：编译 `mcp_server` 可执行文件，并为 6 个示例插件分别建立共享库目标。`include/` 中的 `json.hpp`、`httplib.h`、`popl.hpp` 等是本模块使用的第三方头文件。`HttpStreamTransport` 虽编进可执行文件，但 `main` 当前没有选择它，而且部分方法仍是空实现；学习运行路径时先跳过。

## 1. `main()`：按实际执行顺序追启动流程

你给出的五步抓住了主线。对照当前代码，还要注意对象创建和日志初始化的确切位置：

1. **先创建对象，再解析参数。** `main()` 先创建 `PluginsLoader` 和全局 `shared_ptr<Server>`，注册 `SIGINT` 处理函数，再用 `popl::OptionParser` 解析 `--plugins/-p`、`--logs/-l`、`--sse/-s`、`--name/-n`、`--verbose/-v`。默认插件目录是 `./plugins`，日志目录是 `./logs`。
2. **选择传输。** 指定 `-s` 时创建 `shared_ptr<SSE>`；否则创建 `shared_ptr<Stdio>`。两者向上都以 `shared_ptr<ITransport>` 传给 `Server`。
3. **初始化日志、加载插件。** 创建 AixLog 文件 sink，调用 `loader->LoadPlugins(plugins_directory)`。加载器递归寻找平台共享库，获取 `CreatePlugin` / `DestroyPlugin`，调用 `PluginAPI::Initialize()`，并将成功加载的 `PluginEntry` 放入 `m_plugins`。macOS 识别 `.dylib` 和 `.so`，Linux 识别 `.so`，Windows 识别 `.dll`。
4. **给每个插件接上通知回调。** `main()` 给已加载插件的 `notifications` 设置 `SendToClient = ClientNotificationCallbackImpl`。插件之后可借此将不带请求 ID 的通知交回服务器。
5. **注册插件相关协议方法。** `Server` 构造函数已给 `functionMap` 注册默认处理函数；`main()` 用 `OverrideCallback` 替换 `tools/list`、`tools/call`、`prompts/list`、`prompts/get`、`resources/list`、`resources/read` 这六项，让它们使用当前加载的插件。`initialize`、`ping` 等仍走 `Server.cpp` 中的默认处理函数。
6. **进入运行循环。** 最后一行 `server->Connect(transport)`；它在当前线程中运行到传输结束或停止。`main` 没有调用 `ConnectAsync()`，无论选 STDIO 还是 SSE 都是如此。

`OverrideCallback` 只会替换 `functionMap` 中**已经存在**的方法；它不是任意新增方法的注册接口。各回调以 `&loader` 捕获加载器，因而理解回调时也要记住 `loader` 在 `main()` 的作用域中保持存活。

## 2. `Server.cpp`：请求与响应主线

重点按 [`Server::Connect`](../mcp_server/src/server/Server.cpp) → `Server::HandleRequest` → `functionMap` 中对应函数的顺序读。

```text
Connect(transport)
  ├─ 保存 transport_，启动 WriterLoop 线程
  ├─ transport_->Start()
  └─ while (!isStopping_)
       ├─ transport->Read()                 // 当前线程等一条消息
       ├─ json::parse(json_string)
       ├─ HandleRequest(request)
       │    └─ functionMap[request["method"]](request)
       └─ 若返回非 null：锁 output_mutex_，transport_->Write(response.dump())
```

`Connect()` 的“同步”是指**请求处理主循环**：它一次读取、分发并写回一条请求；不是说整个服务器只有一个线程。构造函数给 `functionMap` 填好 `initialize`、`ping`、工具/资源/提示方法和若干客户端通知方法。`HandleRequest()` 先检查 `method`，再查表调用；找不到时构造 JSON-RPC 错误。返回 `nullptr` 的处理函数不会写响应，这用于 `notifications/initialized` 等通知消息。

`ConnectAsync()` 是另一条实现：它启动 reader 线程，使用 `ReadAsync()` 取消息；**当前 `main()` 未使用**，初学时可以放在 `Connect()` 之后再读。`Stop()` 负责停传输、唤醒并 join 写线程和可能存在的读线程。

以工具调用为例，建议跟一遍这条具体路径：

```json
{"jsonrpc":"2.0","id":"calc-1","method":"tools/call","params":{"name":"calculator","arguments":{"expression":"1+2"}}}
```

`Connect()` 解析它，`HandleRequest()` 通过 `functionMap["tools/call"]` 进入 `main.cpp` 覆盖的回调。回调遍历 `loader->GetPlugins()` 与每个插件的 `GetTool()`，按工具名找到插件，调用其 `HandleRequest(request.dump().c_str())`；插件返回 JSON 字符串后，服务器解析为 `result` 并写回带相同 `id` 的响应。`tools/list` 则不执行插件工具，只读取各插件公布的元数据和 `inputSchema`。默认的 `Server::ToolsCallCmd()` 不是当前运行时真正处理工具调用的函数，这点读代码时很容易混淆。

## 3. `PluginsLoader` 与 `PluginAPI`：从动态库到工具

`PluginsLoader::m_plugins` 是已加载插件列表，每个 `PluginEntry` 保存动态库路径与句柄、`PluginAPI*`、工厂/销毁函数指针。加载步骤是 `dlopen`（Windows 为 `LoadLibrary`）→ 找到 `CreatePlugin`/`DestroyPlugin` → `CreatePlugin()` → `Initialize()` → 加入列表。析构时依次调用 `Shutdown()`、`DestroyPlugin()`、关闭动态库。

`PluginAPI` 不是一个有虚函数的 C++ 基类，而是包含 `GetName`、`GetType`、`GetToolCount`、`GetTool`、`HandleRequest` 等函数指针的结构体。学习插件可先看 `Calculator.cpp`：`PluginTool` 数组定义工具名称、描述和 JSON Schema，`CreatePlugin()` 返回该模块的 `PluginAPI`，`HandleRequest` 再根据请求里的 `params.name` 与 `params.arguments` 执行运算。

注意插件 `HandleRequest` 返回的是分配出来的 `char*`；`main.cpp` 当前在读取结果后用 `delete[]` 释放。写新插件时需遵守这项内存约定。更详细的插件开发说明见 [插件开发文档](mcp-plugin-development.md)。

## 4. `WriterLoop`：服务端主动通知

函数名是 **`WriterLoop`**，与同步处理 RPC 的 `Connect()` 分工如下：

```text
插件（例如 logging_test / progress_test）
  → notifications->SendToClient(...)
  → main.cpp: ClientNotificationCallbackImpl(...)
  → Server::SendNotification(...) 把 JSON 字符串放入 notification_queue_
  → queue_cv_ 唤醒 WriterLoop
  → WriterLoop 调用 transport_->Write(...)
  → 客户端收到不带 id 的通知
```

`WriterLoop` 是 `Connect()` 启动的**独立写线程**，不负责处理请求。`SendNotification` 在队列锁内复制通知字符串，随后唤醒写线程；响应的直接写入和通知的写入都使用 `output_mutex_`，以免同一传输的输出互相穿插。通知可在插件执行期间先于该次工具调用的最终响应到达；客户端应按是否有 `id` 区分通知与响应。`plugins/notification/Notification.cpp` 的 `logging_test` 和 `progress_test` 是最直观的阅读例子，其中后者会在执行过程中多次发进度通知。

不要把两个方向的“通知”混为一谈：客户端发来的 `notifications/initialized` 等消息走 `Connect()`/`HandleRequest()`，通常不产生响应；插件主动发给客户端的通知则走上面的 `SendNotification()`/`WriterLoop` 队列。

## 5. 两种实际可选的传输

- **STDIO：** `Stdio::Read()` 从 `stdin` 读一行，`Stdio::Write()` 往 `stdout` 写一行 JSON。`Start()`/`Stop()` 基本不做事。建议先用它理解协议，因为没有 HTTP 会话细节。
- **SSE：** `SSE::Start()` 在另一线程监听 `127.0.0.1:8080`。`GET /sse` 建立持续的事件流；`POST /messages` 把请求放入传输层的 `incoming_messages_`；`SSE::Read()` 将其交给同一个 `Server::Connect()`；`SSE::Write()` 将响应/通知放进 `outgoing_messages_`，由 `/sse` 的内容提供器推给客户端。`GET /health` 可用于检查监听是否启动。它仍是“HTTP 负责收发、Server 负责协议分发”的分层。

当前 C++ 客户端的 SSE 连接还有超时问题，服务端 SSE 基础握手与 `initialize` 已单独验证；请勿据此推断 C++ 客户端 SSE 已可用。复现步骤和已知限制见 [项目现状报告](../PROJECT_STATUS_2026-09-14.md)。

## 建议的逐文件阅读路线

1. `main.cpp`：先只找对象创建、六个 `OverrideCallback` 和最后的 `Connect()`；暂时跳过每个回调的细节。
2. `ITransport.h` 与 `StdioTransport.{h,cpp}`：明确 `Read/Write` 的边界和一行一个 JSON 的传输形式。
3. `Server.h` 与 `Server.cpp` 的构造函数、`Connect()`、`HandleRequest()`：跟踪一条 `tools/list`，再跟踪一条 `tools/call`。
4. 回到 `main.cpp` 的 `tools/list`、`tools/call` 回调，顺着 `PluginsLoader` 进入 `Calculator.cpp`。
5. 看 `Notification.cpp`、`ClientNotificationCallbackImpl`、`SendNotification()`、`WriterLoop()`，理解为什么响应与通知会从不同线程写出。
6. 最后读 `SseTransport.cpp` 和未在入口使用的 `ConnectAsync()`；把它们与已经理解的 STDIO 路径逐项对照。

阅读时可随时问四个问题：**谁拥有对象？谁读取请求？谁决定调用哪个方法或插件？谁写出最终消息？** 沿着这四条线索，`Server`、传输层和插件层的职责会比较清楚。

本导读描述当前源码，并不表示所有分支已完整实现。尤其 `HttpStreamTransport` 尚未接入入口，STDIO 输入结束后的退出阶段仍存在已记录的 `SIGABRT`；这些现状不妨碍先理解主请求链路。
