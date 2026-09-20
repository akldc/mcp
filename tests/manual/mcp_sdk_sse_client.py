#!/usr/bin/env python3
"""Manual interoperability check using the official Python MCP SDK over SSE."""

import argparse
import asyncio

from mcp import ClientSession
from mcp.client.sse import sse_client


async def run(url):
    async with sse_client(url) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            tools = await session.list_tools()
            print("Available tools:", [tool.name for tool in tools.tools])
            result = await session.call_tool(
                "calculator", {"expression": "123 + 456"})
            print("calculator result:", result)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://127.0.0.1:8080/sse")
    args = parser.parse_args()
    asyncio.run(run(args.url))


if __name__ == "__main__":
    main()
