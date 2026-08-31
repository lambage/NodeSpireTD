#!/usr/bin/env python3
"""Minimal client for talking to the BlenderMCP addon socket server (default port 9876)."""
import json
import socket
import sys


def send_command(command_type: str, params: dict | None = None, host: str = "localhost", port: int = 9876, timeout: float = 30.0):
    payload = {"type": command_type, "params": params or {}}
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(json.dumps(payload).encode("utf-8"))
        sock.settimeout(timeout)
        chunks = []
        while True:
            try:
                data = sock.recv(65536)
            except socket.timeout:
                break
            if not data:
                break
            chunks.append(data)
            try:
                json.loads(b"".join(chunks).decode("utf-8"))
                break
            except ValueError:
                continue
    raw = b"".join(chunks).decode("utf-8")
    return json.loads(raw) if raw else None


def run_code(code: str, **kwargs):
    return send_command("execute_code", {"code": code}, **kwargs)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        script_path = sys.argv[1]
        with open(script_path, "r", encoding="utf-8") as f:
            src = f.read()
        result = run_code(src)
    else:
        result = send_command("get_scene_info")
    print(json.dumps(result, indent=2))
