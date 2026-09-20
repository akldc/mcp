# MCP 项目构建、启动与测试指南

本文给出从项目根目录完成构建、启动和简单验证的最短流程。除非特别说明，以下命令都应在项目根目录执行：

```bash
cd /path/to/mcp
```

建议统一使用 `build/` 作为构建目录。`test_all.sh` 和 AI 交互演示默认都从该目录查找程序；`build_old/` 只是旧构建产物的备份，不参与当前构建。

## 1. 准备依赖

项目需要：

- 支持 C++20 的编译器
- CMake 3.20 或更高版本
- pkg-config
- libcurl
- jsoncpp
- pthread/Threads（通常由系统提供）

先检查当前环境：

```bash
cmake --version
c++ --version
pkg-config --modversion jsoncpp
```

macOS 使用 Homebrew 安装依赖：

```bash
brew install cmake pkg-config curl jsoncpp
```

Ubuntu/Debian 安装依赖：

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libcurl4-openssl-dev libjsoncpp-dev
```

## 2. 构建整个项目

首次配置并构建 Debug 版本：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build build --parallel
```

该命令会构建公共库、客户端库、服务器、插件和示例程序。主要产物如下：

```text
build/
├── common/libmcp_common.a
├── mcp_client/libmcp_client.a
├── mcp_server/mcp_server
├── mcp_server/plugins/         # 六个内置插件
├── examples/mcp_basic_example
├── examples/rag_mcp_example
└── tests/mcp_client_stdio_test
```

macOS 的插件通常是 `.dylib`，Linux 通常是 `.so`。

以后修改源码后，只需增量构建：

```bash
cmake --build build --parallel
```

如果不需要示例程序，可以这样配置：

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_MCP_EXAMPLES=OFF
cmake --build build --parallel
```

## 3. 启动 MCP Server

### STDIO 模式（默认）

```bash
mkdir -p build/logs

./build/mcp_server/mcp_server \
  -p ./build/mcp_server/plugins \
  -l ./build/logs
```

STDIO 模式通过标准输入接收一行一个 JSON-RPC 请求，并通过标准输出返回响应。直接在终端启动后没有普通命令提示符是正常现象；它正在等待客户端或 JSON-RPC 输入。按 `Ctrl+C` 可停止服务。

通常不需要手工启动 STDIO Server。MCP 客户端会把它作为子进程启动并与之通信。

### SSE 模式

```bash
mkdir -p build/logs

./build/mcp_server/mcp_server -s \
  -p ./build/mcp_server/plugins \
  -l ./build/logs
```

SSE 服务固定监听 `127.0.0.1:8080`。当前命令行没有修改监听地址或端口的选项。可在另一个终端检查服务：

```bash
curl --noproxy '*' http://127.0.0.1:8080/health
```

预期输出：

```json
{"status" : "ok"}
```

当前 C++ 客户端主要使用 STDIO；SSE 服务端可做基础 HTTP/SSE 通信，但现有 C++ 客户端的 SSE 连接仍有兼容性问题。

## 4. 简单测试

### 测试一：检查程序和命令行参数

```bash
./build/mcp_server/mcp_server --help
./build/examples/mcp_basic_example --help
./build/examples/rag_mcp_example --help
```

三个命令都正常显示帮助信息，说明主要可执行文件已经生成。

### 测试二：运行 C++ 客户端示例（推荐）

```bash
./build/examples/mcp_basic_example
```

正常情况下会看到：

- 成功初始化 MCP 客户端
- 发现可用工具列表
- 调用 `calculator`
- `123 + 456` 返回 `579`

该流程验证的是：

```text
C++ 示例客户端 -> STDIO -> MCP Server -> 插件加载 -> calculator 工具调用
```

它不需要 DashScope API Key，也不会启用 RAG。

### 测试三：运行自动测试

```bash
ctest --test-dir build --output-on-failure
```

CTest 会运行两个不依赖网络的集成测试：一个验证 C++ `MCPClient`，另一个直接验证 Server 的 JSON-RPC 协议和插件。

### 测试四：运行项目测试入口

```bash
./test_all.sh basic
```

该命令依次执行 CMake 配置、编译和 CTest，适用于 Linux 和 macOS，不再依赖 GNU `timeout`。

### 测试五：运行 C++ RAG 示例

```bash
DASHSCOPE_API_KEY="sk-xxx" \
  ./build/examples/rag_mcp_example --query "查询北京天气"
```

该示例实际经过 C++ `MCPAgentIntegration` 和 `ToolRetriever`，需要网络和 DashScope API Key。

### 测试六：运行 Python AI + RAG 交互演示

```bash
DASHSCOPE_API_KEY="sk-xxx" \
DASHSCOPE_EMBEDDING_KEY="sk-xxx" \
python3 examples/ai_rag_agent_demo.py
```

需要验证与 Python MCP SDK 的互操作性时，先在虚拟环境中安装 `tests/manual/requirements.txt`，然后运行 `tests/manual/mcp_sdk_stdio_client.py` 或 `tests/manual/mcp_sdk_sse_client.py`。这些是手工测试，不会被默认 CTest 执行。

如需指定模型：

```bash
DASHSCOPE_API_KEY="sk-xxx" \
DASHSCOPE_EMBEDDING_KEY="sk-xxx" \
DASHSCOPE_MODEL="qwen-turbo" \
python3 examples/ai_rag_agent_demo.py
```

## 5. 常用维护命令

仅重新编译某个目标：

```bash
cmake --build build --target mcp_server --parallel
cmake --build build --target mcp_client --parallel
cmake --build build --target mcp_basic_example --parallel
cmake --build build --target rag_mcp_example --parallel
```

查看本次构建使用的主要配置：

```bash
cmake -LA -N build
```

执行 CMake 的目标清理，再重新构建：

```bash
cmake --build build --target clean
cmake --build build --parallel
```

如果切换了操作系统、CPU 架构或编译器，不要复用旧构建目录。应删除或改名旧目录，然后重新执行第 2 节的配置和构建命令。

## 6. 最短操作流程

日常开发时可以只记住下面三步：

```bash
# 1. 配置（首次或 CMake 配置变化后执行）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# 2. 编译
cmake --build build --parallel

# 3. 无 API Key 的端到端简单验证
ctest --test-dir build --output-on-failure
```
