#!/usr/bin/env python3
"""Black-box checks for the legacy MCP GET /sse + POST /messages transport."""

import argparse
import http.client
import json
import os
import re
import socket
import subprocess
import sys
import time


def wait_for_server(process):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError("SSE server exited before accepting connections")
        try:
            with socket.create_connection(("127.0.0.1", 8080), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    raise AssertionError("SSE server did not listen on port 8080")


def port_is_available():
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        probe.bind(("127.0.0.1", 8080))
        return True
    except OSError:
        return False
    finally:
        probe.close()


def read_until(sock, marker, timeout=5):
    sock.settimeout(timeout)
    data = b""
    while marker not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise AssertionError("SSE connection closed before expected frame")
        data += chunk
    return data


def post(path, body):
    connection = http.client.HTTPConnection("127.0.0.1", 8080, timeout=3)
    connection.request("POST", path, body=body, headers={"Content-Type": "application/json"})
    response = connection.getresponse()
    payload = response.read()
    connection.close()
    return response.status, payload


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", required=True)
    parser.add_argument("--plugins", required=True)
    parser.add_argument("--logs", required=True)
    args = parser.parse_args()

    # The server's legacy SSE listener intentionally has a fixed port.  Do not
    # interfere with a developer's already-running server during ctest.
    if not port_is_available():
        print("SKIPPED: TCP port 8080 is already in use")
        return

    os.makedirs(args.logs, exist_ok=True)
    process = subprocess.Popen(
        [args.server, "--sse", "--plugins", args.plugins, "--logs", args.logs],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    stream = None
    try:
        wait_for_server(process)
        stream = socket.create_connection(("127.0.0.1", 8080), timeout=3)
        stream.sendall(
            b"GET /sse HTTP/1.1\r\nHost: 127.0.0.1\r\n"
            b"Accept: text/event-stream\r\nConnection: keep-alive\r\n\r\n"
        )
        first_frame = read_until(stream, b"\n\n")
        assert b"200" in first_frame.split(b"\r\n", 1)[0], first_frame
        assert b"Content-Type: text/event-stream" in first_frame, first_frame
        match = re.search(rb"event: endpoint\ndata: (/messages\?session_id=([^\n]+))\n\n", first_frame)
        assert match, first_frame
        endpoint = match.group(1).decode("ascii")

        # A single active SSE session is intentional; an unrelated stream must
        # not replace it and an invalid POST must never reach the incoming queue.
        status, _ = post("/messages?session_id=wrong", b"{}")
        assert status == 409, status
        connection = http.client.HTTPConnection("127.0.0.1", 8080, timeout=3)
        connection.request("GET", "/sse")
        assert connection.getresponse().status == 409
        connection.close()

        request = json.dumps({"jsonrpc": "2.0", "id": "sse-test", "method": "tools/list", "params": {}})
        status, _ = post(endpoint, request.encode("utf-8"))
        assert status == 200, status
        reply = read_until(stream, b"\n\n")
        assert b"data: " in reply and b'"id":"sse-test"' in reply, reply
    finally:
        if stream:
            stream.close()
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"SSE integration test failed: {error}", file=sys.stderr)
        raise
