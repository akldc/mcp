# MCP 项目现状检查（2026-09-14，含复查）

## 本轮复查结论

本轮对头文件、CMake 依赖和一处 STDIO 输出做了小范围修正后，**根项目完整构建通过，`mcp_server` 也能单独构建为可执行文件；C++ 客户端能够启动服务器并完成工具调用**。在 macOS 上，新构建的 `rag_mcp_example --mcp-server <服务器路径>` 发现 12 个工具，调用 `calculator` 计算 `123 + 456` 返回 `579`，示例进程退出码为 0。该示例未启用 RAG，因此这不代表向量检索或 AI 联调通过。

上述客户端结果仅适用于 STDIO。后续复测表明 SSE 服务端可以完成基本 HTTP/SSE 请求，但现有 C++ 客户端的 SSE `connect()` 会超时失败，详见下文。

本轮实际变更：`mcp_client/CMakeLists.txt` 使用仓库已有的 `mcp_server/include/json.hpp`，并通过 `PkgConfig::JSONCPP` 正确传递 jsoncpp 链接信息；客户端 5 个源文件改用该内置 JSON 头；`mcp_server/src/main.cpp` 将启动日志路径从 stdout 移至 stderr，以免污染 STDIO JSON-RPC 响应。本轮开始时 `mcp_client.cpp` 已包含 `<signal.h>`，没有再次改动它。

复查命令及结果：

| 检查 | 结果 |
| --- | --- |
| `cmake -S . -B /private/tmp/mcp-check-20260914 -DCMAKE_BUILD_TYPE=Release`，再执行 `cmake --build /private/tmp/mcp-check-20260914 --parallel 8` | **通过**；产出 `mcp_server/mcp_server`、`mcp_client/libmcp_client.a`、`examples/rag_mcp_example` 和 6 个插件 |
| `cmake -S mcp_server -B /private/tmp/mcp-server-only-20260914 -DCMAKE_BUILD_TYPE=Release`，再执行 `cmake --build /private/tmp/mcp-server-only-20260914 --parallel 8` | **通过**；产出独立的 `mcp_server` 可执行文件及插件 |
| 在 `/private/tmp/mcp-check-20260914/mcp_server` 目录运行 `../examples/rag_mcp_example --mcp-server ./mcp_server` | **通过**；发现 12 个工具，计算器返回 `579`，退出码 0。此工作目录使服务器默认 `./plugins` 与 `./logs` 指向临时构建目录 |
| 独立服务器 `--help` | 退出码 0，显示插件选项 |
| 独立服务器 STDIO 请求后关闭输入 | `initialize` 返回正确响应，但服务器仍以 `SIGABRT`（退出码 `-6`）结束；这是尚未解决的退出阶段故障 |

## 可复现的编译与启动步骤

以下命令均从**项目根目录**执行。先确认 `cmake --version`、`c++ --version`、`pkg-config --modversion jsoncpp` 可用；根项目还需要 libcurl 与 Threads。为避开仓库中已被 Git 跟踪的旧 `build/`，示例使用 `/private/tmp/`；在 Linux 上可以换成 `/tmp/` 下的任意新目录。`mcp_client` 是**静态库**，不能像服务器那样直接运行，启动客户端要使用链接该库的 `rag_mcp_example` 或自己的程序。

### 1. 根项目编译（同时得到 server、client 和示例）

```bash
ROOT_BUILD=/private/tmp/mcp-root-check
cmake -S . -B "$ROOT_BUILD" -DCMAKE_BUILD_TYPE=Release
# 若只想先验证 client 库编译，可单独构建此目标
cmake --build "$ROOT_BUILD" --target mcp_client --parallel 8
# 再构建全部目标（含 server、插件、示例）
cmake --build "$ROOT_BUILD" --parallel 8

# 检查产物；macOS 插件扩展名为 .dylib，Linux 为 .so
ls -l "$ROOT_BUILD/mcp_server/mcp_server"
ls -l "$ROOT_BUILD/mcp_client/libmcp_client.a"
ls -l "$ROOT_BUILD/examples/rag_mcp_example"
```

本机实测上述等价命令完整通过。`BUILD_MCP_EXAMPLES=OFF` 时只生成库和服务器，不会生成用于启动客户端的示例。

### 2. 只编译 server

```bash
SERVER_BUILD=/private/tmp/mcp-server-check
cmake -S mcp_server -B "$SERVER_BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$SERVER_BUILD" --parallel 8
ls -l "$SERVER_BUILD/mcp_server"
ls "$SERVER_BUILD/plugins"
```

这对应 README 的 `cd mcp_server && cmake .. && make` 流程，只是把构建目录移到临时位置。本机实测通过，生成服务器可执行文件和 6 个插件。

### 3. 启动 STDIO server，并做最小协议测试

服务器将日志写入 `-l` 指定的目录，需先建目录。在终端启动时，STDIO 模式会等待标准输入；可以手工逐行输入 JSON-RPC，也可以用下面的管道测试：

```bash
ROOT_BUILD=/private/tmp/mcp-root-check
mkdir -p "$ROOT_BUILD/mcp_server/logs"
printf '%s\n' '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"smoke","version":"1"}},"id":"init"}' \
  | "$ROOT_BUILD/mcp_server/mcp_server" \
      -p "$ROOT_BUILD/mcp_server/plugins" \
      -l "$ROOT_BUILD/mcp_server/logs"
```

预期 stdout 中出现含 `"id":"init"`、`"serverInfo"` 的 JSON 响应，日志文件路径在 stderr。**当前版本在管道输入结束（EOF）后仍可能以 `SIGABRT` 退出**；不要把“收到正确响应”误认为服务器正常退出。要继续交互，可保持标准输入打开并依次发送 `ping`、`tools/list`、`tools/call` 请求。

### 4. 启动 C++ client 并验证工具调用（STDIO）

```bash
ROOT_BUILD=/private/tmp/mcp-root-check
mkdir -p "$ROOT_BUILD/mcp_server/logs"
(
  cd "$ROOT_BUILD/mcp_server"
  ../examples/rag_mcp_example --mcp-server ./mcp_server
)
```

这里的工作目录很重要：示例没有传服务器参数，服务器会按默认值从 `./plugins` 加载插件、向 `./logs` 写日志。本机实测示例退出码为 0，输出 `Available Tools (12)`，调用 `calculator` 得到 `123 + 456 = 579`。没有传 `--enable-rag`，所以这只验证 C++ 客户端的 STDIO 启动、工具发现和工具调用。

README 的 `./test_all.sh` 模式 2 也用于基础协议检查，但它固定读取仓库 `build/` 的产物，且使用 GNU `timeout`；本机 macOS 没有该命令，本次未把它作为验证依据。`ctest` 当前没有注册的测试用例。

## SSE 模式：服务端已验证，C++ 客户端尚不可用

**前几轮的客户端成功结果只涉及 STDIO。** 本轮在允许本机回环连接的环境中，使用上述根项目构建产物验证了 SSE 服务端：`GET /health` 返回 HTTP 200；`GET /sse` 返回 `event: endpoint` 与 `/messages?session_id=...`；`POST /messages` 接收 `initialize`，随后该 JSON-RPC 响应从 SSE 事件流返回。服务器监听固定的 `127.0.0.1:8080`；若端口被占用会启动失败，当前 CLI 没有端口选项。

手工验证流程（三个终端；需允许本机 `127.0.0.1:8080` 监听和访问）：

```bash
# 终端 A：启动服务端，保持运行
ROOT_BUILD=/private/tmp/mcp-root-check
mkdir -p "$ROOT_BUILD/mcp_server/logs"
"$ROOT_BUILD/mcp_server/mcp_server" -s \
  -p "$ROOT_BUILD/mcp_server/plugins" \
  -l "$ROOT_BUILD/mcp_server/logs"
```

```bash
# 终端 B：健康检查，再打开持续的 SSE 事件流
curl --noproxy '*' http://127.0.0.1:8080/health
curl --noproxy '*' -N -H 'Accept: text/event-stream' http://127.0.0.1:8080/sse
```

打开事件流后，应看到 `event: endpoint` 和一行 `data: /messages?session_id=...`。保留这个 `curl -N` 进程，再在第三个终端 POST；将下面 URL 中的 `<从事件中读取的session_id>` 换成实际值：

```bash
curl --noproxy '*' -sS -X POST \
  'http://127.0.0.1:8080/messages?session_id=<从事件中读取的session_id>' \
  -H 'Content-Type: application/json' \
  --data '{"jsonrpc":"2.0","method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"sse-smoke","version":"1"}},"id":"sse-init"}'
```

POST 的 HTTP 响应是 `{"status":"received"}`；真正的 JSON-RPC `initialize` 结果应在第二个终端的 SSE 事件流中。上述流程本机已通过。某些环境的 HTTP 客户端会走代理，本机测试时显式禁用代理；最初的受限沙箱也不允许绑定回环端口，不能把这类环境错误判为 SSE 代码错误。

**现有 C++ `MCPClient` 的 SSE 连接实测失败。** 临时测试程序把 `MCPConnectionConfig.transport` 设为 `SSE`、`sse_url` 设为 `http://127.0.0.1:8080`、连接超时设为 2 秒后，`connect()` 返回 `false`，日志为 `SSE connection failed: Timeout was reached`。代码在 `connectSSE()` 中对永不主动结束的 `/sse` 流同步执行 `curl_easy_perform()`，直到超时才返回；因此不能把服务端 SSE 可用等同于 C++ 客户端 SSE 可用。配置里的 `sse_url` 应是服务器**基地址**，客户端会自行附加 `/sse`，不应填完整 `/sse` URL。客户端当前还试图从 `id:` 行或 JSON 中提取 session ID，但服务端首次发送的是 `event: endpoint`，其中 URL 放在 `data:` 行；这也是后续兼容性问题。此轮只记录问题，未修改 SSE 实现。

以下“首次检查”部分保留修改前的事实记录，其构建失败结论已被本轮复查结果取代。

## 首次检查结论（修改前）

**当前工作树不能通过根目录的完整构建，也不能认定为“正常编译、正常执行”。** 在本机 macOS/AppleClang 环境下，CMake 配置成功；`mcp_server` 和 6 个插件能编译并处理基本 STDIO 请求；完整构建在 `mcp_client` 编译阶段失败。服务器处理请求后，在标准输入关闭的退出阶段发生 `SIGABRT`。RAG、C++ 客户端、SSE 和 AI 联调未获得端到端验证。

首次检查时只新增此报告，未修改源码、README、现有构建目录或测试脚本。检查对象是**包含原有未提交修改的当前工作树**，不代表仓库 `main` 提交版本。

## 环境与检查方法

| 项目 | 本次环境 / 方法 |
| --- | --- |
| 系统 | macOS arm64（AppleClang 17.0.0） |
| 构建工具 | CMake 4.4.3、C++20、Python 3.9.6 |
| 已找到的依赖 | libcurl 8.7.1、jsoncpp 1.9.8、Threads |
| 构建目录 | `/private/tmp/mcp-audit-20260914`，避免改动仓库中已被跟踪的 `build/` |
| 命令 | `cmake -S . -B /private/tmp/mcp-audit-20260914 -DCMAKE_BUILD_TYPE=Release`；`cmake --build /private/tmp/mcp-audit-20260914 --parallel 8` |
| 运行检查 | 用 Python 标准库启动上述新编译的服务器，通过 STDIO 发送 JSON-RPC 请求并收集退出码 |

## 实测结果

| 检查项 | 结果 | 证据 / 范围 |
| --- | --- | --- |
| CMake 配置 | 通过 | 找到 curl、jsoncpp、Threads，成功生成构建文件 |
| 根项目完整构建 | **失败** | `mcp_client/src/mcp_client.cpp:563`：`use of undeclared identifier 'kill'`；该文件未显式包含声明 `kill` 的 `<signal.h>` / `<csignal>` |
| 服务器及插件编译 | 通过 | 在完整构建失败前生成 `mcp_server` 和 6 个 macOS `.dylib` 插件；服务器路径为 `/private/tmp/mcp-audit-20260914/mcp_server/mcp_server` |
| STDIO 协议基础请求 | 通过 | `initialize` 返回协议版本 `2024-11-05`、服务器版本 `0.7.0`；`ping` 返回空结果；`tools/list` 返回 12 个工具 |
| 工具调用 | 通过 | `calculator` 的 `1+2` 返回 `1+2 = 3`；`sleep` 的 10 ms 调用成功；不存在的 `no_tool` 返回 `isError: true` |
| 标准输入关闭后的退出 | **失败** | 已输出全部请求响应，但进程退出码 `-6`（`SIGABRT`），stderr 为 `libc++abi: terminating due to uncaught exception of type std::__1::system_error: mutex lock failed: Invalid argument`；日志显示 `Server::Stop()` 已完成，异常发生在随后的退出阶段。根因尚未定位，不能仅凭日志断定具体析构点 |
| CTest | 无项目测试 | `ctest --test-dir /private/tmp/mcp-audit-20260914 -N` 显示 `Total Tests: 0`；根 CMake 配置没有注册 `add_test` |

为排查被首个编译错误遮住的问题，另在**第二个临时目录**配置 `-DCMAKE_CXX_FLAGS='-include signal.h'`（仅影响临时编译命令，未编辑源码）。编译继续后，在 `mcp_client/src/mcp_agent_integration.cpp:14` 因找不到 `<nlohmann/json.hpp>` 再次失败。当前环境无该头文件；`mcp_client/CMakeLists.txt` 也没有为这个头文件声明或配置依赖。仓库内有 `mcp_server/include/json.hpp`，但其路径并非代码要求的 `nlohmann/json.hpp`。因此即使修复 `kill` 的声明问题，当前环境仍无法完成根项目构建。此补充检查不能等同于原样构建成功。

## 代码与 README 的对应情况

- 根目录 `CMakeLists.txt` 构建 `mcp_common`、`mcp_client`、`mcp_server`，默认开启示例；`examples/CMakeLists.txt` 实际只构建 `rag_mcp_example`。`ai_mcp_integration_example.cpp` 存在，但目标被注释，原因是缺少 `ai_interface` 模块。
- 服务器的命令行选项为 `-p/--plugins`、`-l/--logs`、`-s/--sse` 等；README 中传给客户端的 `--plugins` 与当前服务器参数一致。macOS 插件产物是 `.dylib`，README 的“.so 插件”描述只适用于部分平台。
- README 称服务器支持 STDIO、SSE、HTTP Stream。当前 `main.cpp` 仅根据 `-s` 在 STDIO 与 SSE 之间选择；`HttpStreamTransport.cpp` 虽编入目标，但多个方法为空，其中两个非 `void` 方法没有返回值，编译器给出警告。**HTTP Stream 不能视为已实现或可用**。本次未启动 SSE，不能据此判断其运行状态。
- README 中的 `./test_all.sh` 基础模式固定使用仓库 `build/` 里的旧产物，并调用 `timeout`；本机未找到 `timeout`/`gtimeout`。因此没有用该脚本替代新构建的验证，也未运行它以免写入现有 `build/test_logs`。测试脚本的断言只核查少数字段，不检查服务器退出码，可能漏报本次发现的退出崩溃。
- AI 集成测试依赖 DashScope API Key、网络与模型服务；本次未运行。RAG 向量检索和天气等外部服务插件也未实测，不能据基础工具通过推断其可用。
- `main.cpp` 启动时向 **stdout** 打印 `Log file: ...`，这不是 JSON-RPC 消息。此次测试客户端明确跳过了该行；严格把 STDIO 每行当作协议消息的客户端可能受影响。
- README 的“完整实现”“跨平台支持”目前是项目目标/描述，不是本次检查已确认的质量结论。本次仅在上述 macOS 环境做了实际编译和运行检查。

## 后续建议

1. 定位服务器在 STDIO EOF 后的退出崩溃；建议对退出码和响应内容都做自动断言。stdout 协议输出污染已在本轮修复。
2. 补齐可运行的 CTest/跨平台测试入口，更新 macOS 对 `timeout`、插件扩展名及日志目录的说明。
3. 修复 C++ 客户端的 SSE 连接与 endpoint 事件处理，再验证 SSE 工具调用；明确 HTTP Stream 的实现状态，并验证 RAG 和外部 AI 服务的端到端行为，之后据实调整 README 的能力描述。C++ 客户端 STDIO 工具调用及 SSE 服务端基础请求已分别通过。

## 工作区注意事项

检查开始前，`git status` 已显示多处未提交源码修改、被跟踪的 `build/` 产物变化，以及未跟踪的 `test_mcp/` 等内容。本报告没有清理、覆盖或归因这些既有内容。本轮修正范围见顶部“本轮复查结论”；实际编译和运行产物均位于 `/private/tmp/`。
