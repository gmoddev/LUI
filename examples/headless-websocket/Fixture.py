"""Local synthetic HTTP/1.1 + RFC 6455 peer; Python standard library only.

This fixture is test infrastructure, not a framework WebSocket server. Its
deliberately malformed routes exercise the client's hostile-input boundary.
"""
import base64
import hashlib
import json
import socket
import struct
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def Frame(Opcode, Payload=b"", Final=True, Masked=False):
    Prefix = bytes([(128 if Final else 0) | Opcode])
    Flag = 128 if Masked else 0
    Length = len(Payload)
    if Length < 126:
        Prefix += bytes([Flag | Length])
    elif Length < 65536:
        Prefix += bytes([Flag | 126]) + struct.pack("!H", Length)
    else:
        Prefix += bytes([Flag | 127]) + struct.pack("!Q", Length)
    if Masked:
        Mask = b"bad!"
        return Prefix + Mask + bytes(Byte ^ Mask[Index % 4] for Index, Byte in enumerate(Payload))
    return Prefix + Payload


class Peer(ThreadingHTTPServer):
    daemon_threads = True
    block_on_close = False

    def __init__(self, Address):
        super().__init__(Address, Handler)
        self.Barriers = {Name: threading.Event() for Name in ("upgrade", "flood", "active")}
        self.Release = threading.Event()
        self.Requests = []
        self.RequestLock = threading.Lock()
        self.Pongs = []
        self.Messages = []


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_Arguments):
        pass

    def Reply(self, Status, Body):
        self.send_response(Status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(Body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(Body)
        self.close_connection = True

    def Exact(self, Count):
        Result = self.rfile.read(Count)
        if len(Result) != Count:
            raise EOFError()
        return Result

    def ReadFrame(self):
        Header = self.Exact(2)
        Opcode = Header[0] & 15
        assert Header[0] & 128 and not Header[0] & 112, "client frame flags"
        assert Header[1] & 128, "client masking required"
        Length = Header[1] & 127
        if Length == 126:
            Length = struct.unpack("!H", self.Exact(2))[0]
        elif Length == 127:
            Length = struct.unpack("!Q", self.Exact(8))[0]
        assert Length <= 1024 * 1024, "fixture input bound"
        Mask = self.Exact(4)
        Payload = self.Exact(Length)
        return Opcode, bytes(Byte ^ Mask[Index % 4] for Index, Byte in enumerate(Payload))

    def Send(self, Wire, Partial=False):
        if Partial:
            # Independent sends force the parser to accept arbitrary boundaries;
            # TCP is allowed to combine any subset back into a single read.
            for Index in range(0, len(Wire), 3):
                self.connection.sendall(Wire[Index:Index + 3])
        else:
            self.connection.sendall(Wire)

    def Upgrade(self):
        Key = self.headers.get("Sec-WebSocket-Key", "")
        assert len(base64.b64decode(Key, validate=True)) == 16
        assert self.headers.get("Sec-WebSocket-Version") == "13"
        assert self.headers.get("Upgrade", "").lower() == "websocket"
        Accept = base64.b64encode(hashlib.sha1((Key + GUID).encode("ascii")).digest()).decode("ascii")
        if self.path in ("/bad-accept", "/bad-canary"):
            Accept = "invalid"
        Fields = ["HTTP/1.1 101 Switching Protocols", "Upgrade: websocket", "Connection: keep-alive, Upgrade",
                  "Sec-WebSocket-Accept: " + Accept]
        if self.path == "/bad-protocol":
            Fields.append("Sec-WebSocket-Protocol: not-requested")
        elif "lui.echo.v1" in self.headers.get("Sec-WebSocket-Protocol", "").split(", "):
            Fields.append("Sec-WebSocket-Protocol: lui.echo.v1")
        if self.path == "/bad-extension":
            Fields.append("Sec-WebSocket-Extensions: permessage-deflate")
        if self.path == "/bad-duplicate-accept":
            Fields.append("Sec-WebSocket-Accept: " + Accept)
        if self.path == "/bad-canary":
            Fields.append("Set-Cookie: WEBSOCKET_SECRET_CANARY")
        self.Send(("\r\n".join(Fields) + "\r\n\r\n").encode("ascii"), self.path == "/partial")

    def do_GET(self):
        with self.server.RequestLock:
            self.server.Requests.append(self.path)
        if self.path == "/config":
            self.Reply(200, json.dumps({"Protocol": "lui.echo.v1"}).encode())
            return
        if self.path.startswith("/barrier/"):
            Name = self.path[len("/barrier/"):]
            assert self.server.Barriers[Name].wait(5), "peer barrier timeout"
            self.Reply(200, b"{}")
            return
        if self.path == "/stall-upgrade":
            self.server.Barriers["upgrade"].set()
            self.server.Release.wait(10)
            self.close_connection = True
            return
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/echo")
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            return
        if self.path == "/bad-status":
            self.Reply(200, b"{}")
            return
        self.close_connection = True
        self.connection.settimeout(8)
        try:
            self.Upgrade()
            if self.path.startswith("/bad-"):
                return
            if self.path == "/fragments":
                self.Send(Frame(1, b"Hel", False) + Frame(9, b"probe") + Frame(0, b"lo"), True)
                self.Send(Frame(2, b"a\x00", False) + Frame(0, b"\xff"), True)
            elif self.path == "/partial":
                self.Send(Frame(1, b"partial"), True)
            elif self.path == "/invalid-utf8":
                self.Send(Frame(1, b"\xc0\xaf"))
            elif self.path == "/masked-server":
                self.Send(Frame(1, b"bad", Masked=True))
            elif self.path == "/fragmented-control":
                self.Send(Frame(9, b"bad", False))
            elif self.path == "/invalid-close":
                self.Send(Frame(8, struct.pack("!H", 1005)))
            elif self.path == "/oversized":
                self.Send(Frame(2, b"x" * 257))
            elif self.path == "/nonminimal":
                self.Send(b"\x81\x7e\x00\x01x")
            elif self.path == "/flood":
                self.Send(b"".join(Frame(1, b"flood") for _Index in range(64)))
                self.server.Barriers["flood"].set()
            elif self.path == "/remote-close":
                self.Send(Frame(1, b"before-close") + Frame(8, struct.pack("!H", 1001) + b"Leaving"))
            elif self.path == "/abrupt":
                return
            elif self.path == "/active":
                self.server.Barriers["active"].set()
            while True:
                Opcode, Payload = self.ReadFrame()
                if Opcode in (1, 2):
                    self.server.Messages.append((self.path, Opcode, Payload))
                    self.Send(Frame(Opcode, Payload), self.path == "/partial")
                elif Opcode == 8:
                    self.Send(Frame(8, Payload))
                    return
                elif Opcode == 9:
                    if self.path == "/ping-timeout":
                        self.Send(Frame(1, b"ping-seen"))
                    else:
                        self.Send(Frame(10, Payload))
                        if self.path == "/ping":
                            self.Send(Frame(1, b"alive"))
                elif Opcode == 10:
                    self.server.Pongs.append(Payload)
                else:
                    raise AssertionError("unexpected client opcode")
        except (EOFError, OSError):
            pass


if __name__ == "__main__":
    with Peer(("127.0.0.1", 18081)) as Server:
        print("[LUI:Example] Synthetic HTTP/WebSocket peer ready on 127.0.0.1:18081", flush=True)
        try:
            Server.serve_forever()
        except KeyboardInterrupt:
            Server.Release.set()
