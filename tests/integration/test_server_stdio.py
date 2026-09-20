#!/usr/bin/env python3
"""Cross-platform MCP Server STDIO protocol integration test."""

import argparse
import json
import os
import queue
import subprocess
import sys
import threading
import time


class ServerProcess:
    def __init__(self, server, plugins, logs):
        os.makedirs(logs, exist_ok=True)
        self.process = subprocess.Popen(
            [server, "--plugins", plugins, "--logs", logs],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=self._read_stdout, daemon=True)
        self.reader.start()

    def _read_stdout(self):
        for line in self.process.stdout:
            self.lines.put(line)

    def request(self, request, timeout=5.0):
        if self.process.poll() is not None:
            raise RuntimeError(f"server exited with code {self.process.returncode}")

        self.process.stdin.write(json.dumps(request, separators=(",", ":")) + "\n")
        self.process.stdin.flush()
        request_id = str(request["id"])
        deadline = time.monotonic() + timeout

        while time.monotonic() < deadline:
            remaining = max(0.0, deadline - time.monotonic())
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty as exc:
                raise TimeoutError(f"timeout waiting for response {request_id}") from exc

            try:
                message = json.loads(line)
            except json.JSONDecodeError:
                continue
            if str(message.get("id")) == request_id:
                return message

        raise TimeoutError(f"timeout waiting for response {request_id}")

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def rpc(method, request_id, params=None):
    return {
        "jsonrpc": "2.0",
        "method": method,
        "params": params or {},
        "id": request_id,
    }


def run(args):
    server = ServerProcess(args.server, args.plugins, args.logs)
    try:
        initialized = server.request(rpc("initialize", "init", {
            "protocolVersion": "2024-11-05",
            "capabilities": {},
            "clientInfo": {"name": "protocol-test", "version": "1.0"},
        }))
        require("serverInfo" in initialized.get("result", {}),
                "initialize did not return serverInfo")

        ping = server.request(rpc("ping", "ping"))
        require(ping.get("id") == "ping" and "result" in ping,
                "ping response is invalid")

        listed = server.request(rpc("tools/list", "tools"))
        tools = listed.get("result", {}).get("tools", [])
        require(any(tool.get("name") == "calculator" for tool in tools),
                "calculator missing from tools/list")

        calculated = server.request(rpc("tools/call", "calculator", {
            "name": "calculator",
            "arguments": {"expression": "1+2"},
        }))
        calc_result = calculated.get("result", {})
        calc_text = " ".join(
            item.get("text", "") for item in calc_result.get("content", [])
            if item.get("type") == "text"
        )
        require(not calc_result.get("isError", True) and "1+2 = 3" in calc_text,
                f"unexpected calculator response: {calculated}")

        slept = server.request(rpc("tools/call", "sleep", {
            "name": "sleep",
            "arguments": {"milliseconds": 10},
        }))
        require(not slept.get("result", {}).get("isError", True),
                f"sleep failed: {slept}")

        missing = server.request(rpc("tools/call", "missing", {
            "name": "no_such_tool",
            "arguments": {},
        }))
        require(missing.get("result", {}).get("isError") is True,
                "missing tool did not return isError=true")
    finally:
        server.stop()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--plugins", required=True)
    parser.add_argument("--logs", required=True)
    args = parser.parse_args()

    try:
        run(args)
    except Exception as exc:
        print(f"[FAIL] MCP Server STDIO protocol: {exc}", file=sys.stderr)
        return 1

    print("[PASS] MCP Server STDIO protocol")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
