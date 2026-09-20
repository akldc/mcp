#!/usr/bin/env bash
# 项目统一验证入口。默认构建并运行不依赖网络的 CTest 测试。

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${MCP_BUILD_DIR:-$PROJECT_DIR/build}"
MODE="${1:-basic}"

run_basic_tests() {
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}" \
        -DBUILD_TESTING=ON
    cmake --build "$BUILD_DIR" --parallel
    ctest --test-dir "$BUILD_DIR" --output-on-failure
}

run_ai_demo() {
    python3 "$PROJECT_DIR/examples/ai_rag_agent_demo.py"
}

case "$MODE" in
    basic)
        run_basic_tests
        ;;
    ai)
        run_ai_demo
        ;;
    all)
        run_basic_tests
        run_ai_demo
        ;;
    *)
        echo "Usage: $0 [basic|ai|all]" >&2
        exit 2
        ;;
esac
