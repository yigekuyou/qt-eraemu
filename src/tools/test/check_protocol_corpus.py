#!/usr/bin/env python3
"""Compare engine analysis over actual LSP/MCP subprocesses; stdlib only."""
import argparse
import collections
import json
import os
from pathlib import Path
import selectors
import subprocess
import tempfile
import time


class Peer:
    def __init__(self, executable, lsp):
        self.lsp = lsp
        self.buffer = b""
        self.log = tempfile.TemporaryFile()
        self.process = subprocess.Popen([str(executable)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=self.log)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)

    def send(self, method, params=None, request_id=None):
        message = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            message["params"] = params
        if request_id is not None:
            message["id"] = request_id
        body = json.dumps(message, ensure_ascii=False).encode("utf-8")
        wire = (f"Content-Length: {len(body)}\r\n\r\n".encode() + body
                if self.lsp else body + b"\n")
        self.process.stdin.write(wire)
        self.process.stdin.flush()

    def receive(self):
        deadline = time.monotonic() + 30
        while True:
            if self.lsp:
                split = self.buffer.find(b"\r\n\r\n")
                if split >= 0:
                    headers = dict(line.split(b":", 1) for line in self.buffer[:split].split(b"\r\n"))
                    size = int(headers[b"Content-Length"])
                    end = split + 4 + size
                    if len(self.buffer) >= end:
                        body, self.buffer = self.buffer[split + 4:end], self.buffer[end:]
                        return json.loads(body)
            elif b"\n" in self.buffer:
                body, self.buffer = self.buffer.split(b"\n", 1)
                return json.loads(body)
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self.selector.select(remaining):
                raise AssertionError("Protocol response timed out")
            chunk = os.read(self.process.stdout.fileno(), 65536)
            if not chunk:
                raise AssertionError("Server exited before responding")
            self.buffer += chunk

    def reply(self, request_id):
        response = self.receive()
        assert response.get("id") == request_id, response
        assert "error" not in response, response
        return response["result"]

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.selector.close()
        self.process.stdout.close()
        self.log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    args = parser.parse_args()
    root = args.corpus.resolve()
    files = sorted(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() in {".erb", ".erh"})
    assert 0 < len(files) <= 256, "MCP workspace requires 1..256 documents"
    # This test corpus is UTF-8; engine file decoding has separate Qt regression tests.
    sources = {p.as_uri(): p.read_text(encoding="utf-8-sig") for p in files}
    lsp = Peer(args.bin_dir.resolve() / "eralsp", True)
    mcp = Peer(args.bin_dir.resolve() / "eramcp", False)
    try:
        mcp.send("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                               "clientInfo": {"name": "corpus-test", "version": "1"}}, 1)
        mcp.reply(1)
        mcp.send("notifications/initialized")
        mcp.send("tools/list", {}, 2)
        assert len(mcp.reply(2)["tools"]) == 6
        mcp.send("tools/call", {"name": "era_validate_workspace", "arguments": {
            "documents": [{"fileName": uri, "source": text} for uri, text in sources.items()]}}, 3)
        result = mcp.reply(3)
        assert not result.get("isError"), result
        summary = result["structuredContent"]
        assert json.loads(result["content"][0]["text"]) == summary
        documents = {d["fileName"]: d for d in summary["documents"]}
        assert set(documents) == set(sources)
        lsp.send("initialize", {"rootUri": root.as_uri(), "capabilities": {}}, 1)
        assert lsp.reply(1)["capabilities"]["positionEncoding"] == "utf-16"
        lsp.send("initialized", {})
        counts = collections.Counter()
        symbols = 0
        for index, (uri, text) in enumerate(sources.items()):
            lsp.send("textDocument/didOpen", {"textDocument": {
                "uri": uri, "languageId": "erb", "version": 1, "text": text}})
            response = lsp.receive()
            assert response.get("method") == "textDocument/publishDiagnostics", response
            assert response["params"]["uri"] == uri, response
            actual = [(d["code"], d["range"]["start"]["line"], d["range"]["start"]["character"],
                       d["range"]["end"]["character"], d["message"]) for d in response["params"]["diagnostics"]]
            expected = [(d["code"], d["line"], d["startCol"], d["endCol"], d["message"])
                        for d in documents[uri]["diagnostics"]]
            assert actual == expected, f"Diagnostic mismatch: {uri}\nLSP={actual}\nMCP={expected}"
            counts.update(d[0] for d in actual)
            request_id = index + 10
            lsp.send("textDocument/documentSymbol", {"textDocument": {"uri": uri}}, request_id)
            actual_symbols = lsp.reply(request_id)
            assert [(s["name"], s["location"]["range"]["start"]["line"]) for s in actual_symbols] == [
                (s["name"], s["line"]) for s in documents[uri]["symbols"]], uri
            symbols += len(actual_symbols)
            lsp.send("textDocument/didClose", {"textDocument": {"uri": uri}})
            assert lsp.receive()["params"]["diagnostics"] == []
        lsp.send("shutdown", request_id=999)
        assert lsp.reply(999) is None
        lsp.send("exit")
        lsp.process.wait(timeout=5)
        assert lsp.process.returncode == 0
        print(json.dumps({"status": "PASS", "files": len(files), "symbols": symbols,
                          "diagnostics": sum(counts.values()), "byCode": dict(counts)}, ensure_ascii=False, indent=2))
    finally:
        lsp.close()
        mcp.close()


if __name__ == "__main__":
    main()
