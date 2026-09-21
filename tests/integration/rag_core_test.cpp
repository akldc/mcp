#include "agent_rpc/mcp/rag/embedding_cache.h"
#include "agent_rpc/mcp/rag/vector_index.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using agent_rpc::mcp::rag::CacheConfig;
using agent_rpc::mcp::rag::EmbeddingCache;
using agent_rpc::mcp::rag::IndexedTool;
using agent_rpc::mcp::rag::VectorIndex;

namespace {

int fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

IndexedTool makeTool(std::string name, std::vector<float> embedding) {
    IndexedTool tool;
    tool.name = std::move(name);
    tool.embedding = std::move(embedding);
    return tool;
}

} // namespace

int main() {
    VectorIndex index;
    index.addTool(makeTool("exact", {1.0f, 0.0f}));
    index.addTool(makeTool("partial", {0.8f, 0.6f}));
    index.addTool(makeTool("opposite", {-1.0f, 0.0f}));

    if (!index.search({1.0f, 0.0f}, 0).empty()) {
        return fail("top_k=0 should return no results");
    }
    if (!index.search({1.0f, 0.0f}, -1).empty()) {
        return fail("negative top_k should return no results");
    }

    const auto top_two = index.search({1.0f, 0.0f}, 2, -1.0f);
    if (top_two.size() != 2 || top_two[0].tool.name != "exact" ||
        top_two[1].tool.name != "partial") {
        return fail("Top-K results should be ordered by descending similarity");
    }
    if (std::fabs(top_two[0].similarity - 1.0f) > 1e-6f) {
        return fail("exact match should have cosine similarity 1");
    }

    CacheConfig zero_capacity;
    zero_capacity.max_size = 0;
    EmbeddingCache cache(zero_capacity);
    cache.put("query", {1.0f, 2.0f});
    if (cache.size() != 0 || cache.get("query").has_value()) {
        return fail("zero-capacity cache should never store entries");
    }

    std::cout << "RAG core tests passed\n";
    return 0;
}
