# MCP (Model Context Protocol) 完整实现

MCP 协议的完整 C++ 实现，包含 MCP 客户端、MCP 服务器和 RAG-MCP 智能工具检索模块。

从 [agent-communication](../) 项目中独立提取的子项目。

## 项目结构

```
mcp_standalone/
├── CMakeLists.txt              # 根构建文件
├── README.md
├── common/                     # 公共组件（日志、类型定义）
│   ├── include/agent_rpc/common/
│   │   ├── types.h             # 通用类型定义
│   │   └── logger.h            # 日志系统接口
│   └── src/
│       └── logger.cpp          # 日志实现
├── mcp_client/                 # MCP 客户端库
│   ├── include/agent_rpc/mcp/
│   │   ├── mcp_client.h        # MCP 客户端接口（STDIO + SSE）
│   │   ├── mcp_agent_integration.h  # AI Agent 集成辅助类
│   │   └── rag/                # RAG-MCP 模块
│   │       ├── embedding_service.h  # DashScope 向量化服务
│   │       ├── embedding_cache.h    # LRU 向量缓存
│   │       ├── vector_index.h       # 向量索引
│   │       ├── tool_retriever.h     # 工具检索器
│   │       └── tool_validator.h     # 工具验证器
│   └── src/
│       ├── mcp_client.cpp           # 客户端实现
│       ├── mcp_tool_manager.cpp     # 工具管理器
│       ├── mcp_agent_integration.cpp # Agent 集成
│       └── rag/                     # RAG 实现
├── mcp_server/                 # MCP 服务器（独立可执行程序）
│   ├── src/
│   │   ├── main.cpp            # 服务器入口
│   │   ├── server/Server.{h,cpp}    # 核心服务器
│   │   ├── transport/          # 传输层
│   │   │   ├── StdioTransport  # 标准 I/O 传输
│   │   │   ├── SseTransport    # SSE 传输
│   │   │   └── HttpStreamTransport  # HTTP 流传输
│   │   ├── interface/          # 接口定义
│   │   │   ├── ITransport.h    # 传输接口
│   │   │   └── PluginAPI.h     # 插件 API
│   │   ├── loader/             # 插件加载器
│   │   ├── manager/            # 插件生命周期与工具/资源/提示注册表
│   │   └── utils/              # 工具类
│   ├── plugins/                # 内置插件
│   │   ├── calculator/         # 计算器
│   │   ├── weather/            # 天气查询
│   │   ├── sleep/              # 延时工具
│   │   ├── code-review/        # 代码审查
│   │   ├── notification/       # 通知
│   │   └── bacio-quote/        # 名言
├── examples/                   # 示例代码
│   ├── mcp_basic_example.cpp   # 普通 C++ MCP 示例
│   ├── rag_mcp_example.cpp     # C++ RAG 工具检索示例
│   └── ai_rag_agent_demo.py    # Python AI + RAG 交互演示
├── tests/                      # 可由 CTest 运行的自动测试
│   ├── integration/
│   │   ├── mcp_client_stdio_test.cpp
│   │   ├── plugin_reload_concurrency_test.cpp
│   │   └── test_server_stdio.py
│   └── manual/                 # Python MCP SDK 手工互操作测试
└── 00docs/                     # 详细文档
    ├── mcp-plugin-development.md
    └── rag-mcp-guide.md
```

## 核心组件

### 1. MCP 客户端 (`mcp_client`)

支持两种传输模式的 MCP 客户端：

- **STDIO 模式**: 通过标准输入输出与本地 MCP Server 进程通信
- **SSE 模式**: 通过 HTTP Server-Sent Events 与远程 MCP Server 通信

主要类：
- `MCPClient` - MCP 客户端（JSON-RPC 协议）
- `MCPToolManager` - 工具管理器
- `MCPServiceIntegrator` - 服务集成器
- `MCPAgentIntegration` - AI Agent 简化集成接口

### 2. RAG-MCP 模块

基于检索增强生成的智能工具选择：

- `EmbeddingService` - 阿里百炼 DashScope 文本向量化
- `EmbeddingCache` - LRU 缓存，减少 API 调用
- `VectorIndex` - 向量索引，余弦相似度搜索
- `ToolRetriever` - 工具检索器，整合以上组件
- `ToolValidator` - 可选的工具验证

### 3. MCP 服务器 (`mcp_server`)

完整的 MCP 服务器实现（MIT License, by Giuseppe Mastrangelo）：

- 插件化架构，支持动态加载 `.so` 插件
- Linux/macOS 支持通过 `SIGHUP` 全量热重载插件
- 三种传输模式：STDIO / SSE / HTTP Stream
- 内置 6 个示例插件
- 跨平台支持（Linux / macOS / Windows）

## 依赖项

| 依赖 | 用途 | 模块 |
|------|------|------|
| C++20 编译器 | 编译 | 全部 |
| CMake >= 3.20 | 构建 | 全部 |
| libcurl | HTTP/SSE 客户端 | mcp_client |
| jsoncpp | JSON 解析 | mcp_client |
| nlohmann/json | JSON（header-only, 已内置） | mcp_server, mcp_client(RAG) |
| Threads | 多线程 | 全部 |

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
```

### 仅构建 MCP 服务器

```bash
cmake -S mcp_server -B build-server -DCMAKE_BUILD_TYPE=Release
cmake --build build-server --parallel
```

### 构建选项

```bash
# 不构建示例
cmake -S . -B build -DBUILD_MCP_EXAMPLES=OFF

# 不构建测试
cmake -S . -B build -DBUILD_TESTING=OFF
```

## 快速开始

### 启动 MCP 服务器

```bash
# STDIO 模式（默认）
./build/mcp_server/mcp_server -p ./build/mcp_server/plugins

# SSE 模式
./build/mcp_server/mcp_server -s -p ./build/mcp_server/plugins
```

### 热重载插件（Linux/macOS）

重新编译或替换插件动态库后，向 MCP Server 进程发送 `SIGHUP`：

```bash
kill -HUP <mcp_server_pid>
```

信号处理函数只设置重载标志。Server 的维护线程在正常执行上下文中检测该标志，禁止新插件调用进入，等待正在执行的调用结束，然后按“注销注册表 → Shutdown → DestroyPlugin → `dlclose` → 重新扫描并加载 → 重新注册”完成全量重载。维护线程通过 condition variable 定时等待，不进行 busy wait，因此 STDIO Server 空闲时也能及时重载。

### 运行 C++ 示例

普通 MCP 示例不需要 API Key：

```bash
./build/examples/mcp_basic_example
```

C++ RAG 工具检索示例需要 DashScope API Key：

```bash
DASHSCOPE_API_KEY="sk-xxx" \
  ./build/examples/rag_mcp_example --query "计算 123 + 456"
```

### 使用 MCP 客户端

```cpp
#include "agent_rpc/mcp/mcp_client.h"

using namespace agent_rpc::mcp;

// STDIO 模式连接
MCPClient client;
client.connect("/path/to/mcp_server", {"--plugins", "./plugins"});

// 列出工具
auto tools = client.listTools();
for (const auto& tool : tools) {
    std::cout << tool.name << ": " << tool.description << std::endl;
}

// 调用工具
auto response = client.callTool("calculator", R"({"expression": "2+3"})");
std::cout << response.result << std::endl;

client.disconnect();
```

### 使用 RAG-MCP 智能工具检索

```cpp
#include "agent_rpc/mcp/mcp_agent_integration.h"

using namespace agent_rpc::mcp;

MCPAgentConfig config;
config.enable_mcp = true;
config.mcp_server_path = "/path/to/mcp_server";
config.rag_config.enabled = true;
config.rag_config.api_key = std::getenv("DASHSCOPE_API_KEY");

MCPAgentIntegration mcp;
mcp.initialize(config);

// 智能检索最相关的工具
auto tools = mcp.getRelevantTools("计算 123 + 456");

// 获取 LLM 函数调用格式
std::string json = mcp.getRelevantToolsAsJson("查询天气");
```

### 自动测试

默认测试不访问外网，也不需要 API Key：

```bash
ctest --test-dir build --output-on-failure
```

当前包含三项集成测试：

| 测试 | 覆盖范围 |
|------|----------|
| `mcp_client_stdio` | C++ `MCPClient` 启动 Server、发现工具并调用 calculator |
| `plugin_reload_concurrency` | reload 阻止新调用，并等待长耗时存量调用结束后重新加载 |
| `mcp_server_stdio_protocol` | 原始 JSON-RPC initialize、ping、tools/list、calculator、sleep 和错误处理 |

也可以用统一脚本完成配置、构建和测试：

```bash
./test_all.sh basic
```

### AI + RAG 交互演示

Python 演示使用 DashScope LLM 和 Embedding，通过 MCP 调用工具。它是需要网络和 API Key 的手工演示，不属于默认自动测试：

```bash
DASHSCOPE_API_KEY="sk-xxx" \
DASHSCOPE_EMBEDDING_KEY="sk-xxx" \
DASHSCOPE_MODEL="qwen-turbo" \
python3 examples/ai_rag_agent_demo.py
```

也可以运行 `./test_all.sh ai`。

### Python MCP SDK 手工互操作测试

这组测试不属于默认 CTest。建议在虚拟环境中安装依赖：

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r tests/manual/requirements.txt

# STDIO
python3 tests/manual/mcp_sdk_stdio_client.py

# SSE：先在另一个终端启动 mcp_server -s
python3 tests/manual/mcp_sdk_sse_client.py
```

## 开发自定义插件

参见 [00docs/mcp-plugin-development.md](00docs/mcp-plugin-development.md)。

## RAG-MCP 详细指南

参见 [00docs/rag-mcp-guide.md](00docs/rag-mcp-guide.md)。

## 协议版本

- MCP Client: JSON-RPC 2.0
- MCP Server: v0.7.0

## License

MCP Server 部分基于 MIT License (Copyright (C) 2025 Giuseppe Mastrangelo)。
其余部分遵循原项目许可证。
