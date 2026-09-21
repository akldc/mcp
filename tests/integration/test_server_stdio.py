#!/usr/bin/env python3
"""Cross-platform MCP Server STDIO protocol integration test."""

import argparse
import json
import os
import queue
import signal
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
    plugin_suffix = ".dylib" if sys.platform == "darwin" else ".so"
    broken_plugin = os.path.join(args.plugins, "intentionally-broken" + plugin_suffix)
    with open(broken_plugin, "w", encoding="utf-8") as file:
        file.write("not a dynamic library\n")

    server = None
    try:
        server = ServerProcess(args.server, args.plugins, args.logs)
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

        prompts_response = server.request(rpc("prompts/list", "prompts"))
        prompts = prompts_response.get("result", {}).get("prompts", [])
        require(any(prompt.get("name") == "code-review" for prompt in prompts),
                "code-review missing from prompts/list")

        resources_response = server.request(rpc("resources/list", "resources"))
        resources = resources_response.get("result", {}).get("resources", [])
        require(any(resource.get("uri") == "bacio:///quote" for resource in resources),
                "bacio quote missing from resources/list")

        prompt_result = server.request(rpc("prompts/get", "prompt-get", {
            "name": "code-review",
            "arguments": {"language": "cpp", "code": "int main(){}"},
        }))
        require("result" in prompt_result,
                f"prompts/get failed: {prompt_result}")

        resource_result = server.request(rpc("resources/read", "resource-read", {
            "uri": "bacio:///quote",
        }))
        require("result" in resource_result,
                f"resources/read failed: {resource_result}")

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

        if hasattr(signal, "SIGHUP"):
            sleep_result = {}
            sleep_error = []

            def call_sleep_during_reload():
                try:
                    sleep_result.update(server.request(rpc("tools/call", "long-sleep", {
                        "name": "sleep",
                        "arguments": {"milliseconds": 250},
                    })))
                except Exception as exc:  # surfaced in the main test thread below
                    sleep_error.append(exc)

            worker = threading.Thread(target=call_sleep_during_reload)
            worker.start()
            time.sleep(0.05)
            os.kill(server.process.pid, signal.SIGHUP)
            worker.join(timeout=5)
            require(not worker.is_alive(), "sleep request did not finish during SIGHUP")
            require(not sleep_error, f"sleep request failed during SIGHUP: {sleep_error}")
            require(not sleep_result.get("result", {}).get("isError", True),
                    f"sleep request returned an error during SIGHUP: {sleep_result}")

            after_reload = server.request(rpc("tools/list", "after-reload"))
            reloaded_tools = after_reload.get("result", {}).get("tools", [])
            require(any(tool.get("name") == "calculator" for tool in reloaded_tools),
                    "tools were not available after SIGHUP reload")

            prompts_after_reload = server.request(
                rpc("prompts/list", "prompts-after-reload"))
            require(prompts_after_reload.get("result", {}).get("prompts"),
                    "prompts were not available after SIGHUP reload")

            resources_after_reload = server.request(
                rpc("resources/list", "resources-after-reload"))
            require(resources_after_reload.get("result", {}).get("resources"),
                    "resources were not available after SIGHUP reload")
    finally:
        if server is not None:
            server.stop()
        try:
            os.remove(broken_plugin)
        except FileNotFoundError:
            pass


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
