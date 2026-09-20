#!/usr/bin/env python3
"""Manual interoperability check using the official Python MCP SDK over STDIO."""

import argparse
import asyncio
import os

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def run(args):
    os.makedirs(args.logs, exist_ok=True)
    parameters = StdioServerParameters(
        command=args.server,
        args=["--plugins", args.plugins, "--logs", args.logs],
    )

    async with stdio_client(parameters) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            tools = await session.list_tools()
            print("Available tools:", [tool.name for tool in tools.tools])
            result = await session.call_tool(
                "calculator", {"expression": "123 + 456"})
            print("calculator result:", result)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--server", default="./build/mcp_server/mcp_server")
    parser.add_argument(
        "--plugins", default="./build/mcp_server/plugins")
    parser.add_argument("--logs", default="./build/manual_test_logs")
    args = parser.parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
