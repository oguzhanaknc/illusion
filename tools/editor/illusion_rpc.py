"""Client of the illusion editor protocol (JSON-RPC 2.0 over newline-delimited JSON), standard library only.

Launch a headless editor and talk to it over its stdin and stdout:

    from illusion_rpc import EditorClient

    with EditorClient.launch("build/bin/Release/illusion_editor", ["--workspace", "/tmp/games"]) as editor:
        project = editor.call("project.create", directory="tetris", name="Tetris")
        board = editor.call("entity.create", name="Board")["entity"]

Or connect to an editor started with ``--rpc-listen`` through its endpoint file (AF_UNIX sockets; on Windows
this needs a Python with ``socket.AF_UNIX``):

    editor = EditorClient.connect_project("/path/to/project")

Failed calls raise RpcError with the JSON-RPC ``code``, ``message`` and ``data``.
"""

from __future__ import annotations

import itertools
import json
import os
import socket
import subprocess
import sys
from collections import deque
from typing import Any, Deque, Dict, List, Optional


class RpcError(Exception):
    """A JSON-RPC error reply (code 0: the editor did not answer)."""

    def __init__(self, code: int, message: str, data: Any = None):
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message
        self.data = data


class _PipeTransport:
    def __init__(self, process: subprocess.Popen):
        self.process = process

    def send(self, line: bytes) -> None:
        assert self.process.stdin is not None
        self.process.stdin.write(line)
        self.process.stdin.flush()

    def receive(self) -> Optional[bytes]:
        assert self.process.stdout is not None
        line = self.process.stdout.readline()
        return line if line else None

    def close(self) -> None:
        if self.process.stdin is not None and not self.process.stdin.closed:
            self.process.stdin.close()


class _SocketTransport:
    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.reader = sock.makefile("rb")

    def send(self, line: bytes) -> None:
        self.sock.sendall(line)

    def receive(self) -> Optional[bytes]:
        line = self.reader.readline()
        return line if line else None

    def close(self) -> None:
        self.reader.close()
        self.sock.close()


class EditorClient:
    """A connection to an illusion editor. Not thread-safe."""

    def __init__(self, transport, process: Optional[subprocess.Popen] = None):
        self._transport = transport
        self._process = process
        self._ids = itertools.count(1)
        self.notifications: Deque[Dict[str, Any]] = deque()

    @classmethod
    def launch(cls, editor: str, arguments: Optional[List[str]] = None) -> "EditorClient":
        """Starts ``editor --headless --rpc-stdio`` with ``arguments``; its log goes to our stderr."""
        command = [editor, *(arguments or []), "--headless", "--rpc-stdio"]
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=sys.stderr)
        return cls(_PipeTransport(process), process)

    @classmethod
    def connect(cls, address: str, token: str, client: str = "python") -> "EditorClient":
        """Connects to the AF_UNIX socket ``address`` and authenticates with ``token``."""
        if not hasattr(socket, "AF_UNIX"):
            raise RpcError(0, "this Python has no AF_UNIX sockets; use EditorClient.launch()")
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(address)
        editor = cls(_SocketTransport(sock))
        editor.call("rpc.authenticate", token=token, client=client)
        return editor

    @classmethod
    def connect_project(cls, project: str, client: str = "python") -> "EditorClient":
        """Connects to the editor whose endpoint file is in ``project``/.illusion/run."""
        path = os.path.join(project, ".illusion", "run", "editor-endpoint.json")
        with open(path, "r", encoding="utf-8") as file:
            endpoint = json.load(file)
        return cls.connect(endpoint["address"], endpoint["token"], client)

    def call(self, method: str, params: Optional[Dict[str, Any]] = None, **named: Any) -> Any:
        """Calls ``method`` with ``params`` (or keyword arguments) and returns its result."""
        request_id = next(self._ids)
        message = {"jsonrpc": "2.0", "id": request_id, "method": method, "params": {**(params or {}), **named}}
        self._transport.send(json.dumps(message, separators=(",", ":")).encode("utf-8") + b"\n")
        while True:
            line = self._transport.receive()
            if line is None:
                raise RpcError(0, "the editor closed the connection")
            reply = json.loads(line)
            if "method" in reply:
                self.notifications.append(reply)
                continue
            if reply.get("id") not in (request_id, None):
                continue
            if "error" in reply:
                error = reply["error"]
                raise RpcError(error.get("code", 0), error.get("message", ""), error.get("data"))
            return reply["result"]

    def next_notification(self) -> Optional[Dict[str, Any]]:
        """The next notification (e.g. an ``event``), waiting for one; None when the connection closed."""
        while not self.notifications:
            line = self._transport.receive()
            if line is None:
                return None
            reply = json.loads(line)
            if "method" in reply:
                self.notifications.append(reply)
        return self.notifications.popleft()

    def close(self, timeout: float = 30.0) -> Optional[int]:
        """Closes the connection; a launched editor is asked to quit. Returns its exit code."""
        code = None
        if self._process is not None:
            try:
                self.call("editor.quit")
            except (RpcError, OSError):
                pass
            self._transport.close()
            try:
                code = self._process.wait(timeout)
            except subprocess.TimeoutExpired:
                self._process.kill()
                code = self._process.wait()
            if self._process.stdout is not None:
                self._process.stdout.close()
        else:
            self._transport.close()
        return code

    def __enter__(self) -> "EditorClient":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()
