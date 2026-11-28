"""Actual Luau WebSocket service + bounded standard-library protocol peer.

Usage: python WebSocket.py <LuiHeadlessHost> [LuiCli.dll]
No Internet traffic, package installation or wall-clock sleeps are used. Peer
events provide the ordering barriers for stalled-handshake and inbox tests.
"""
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import threading

Root = pathlib.Path(__file__).resolve().parents[2]
Spec = importlib.util.spec_from_file_location("LuiWebSocketPeer", Root / "examples/headless-websocket/Fixture.py")
Fixture = importlib.util.module_from_spec(Spec)
Spec.loader.exec_module(Fixture)


def Main():
    Host = str(pathlib.Path(sys.argv[1]).resolve())
    Results = []
    with Fixture.Peer(("127.0.0.1", 0)) as Peer, tempfile.TemporaryDirectory(prefix="LuiWebSocket-") as Folder:
        Worker = threading.Thread(target=Peer.serve_forever, daemon=True)
        Worker.start()
        Project = pathlib.Path(Folder)
        (Project / "Src").mkdir()
        Manifest = Project / "lui.json"
        Port = Peer.server_port
        Ws = f"ws://127.0.0.1:{Port}"
        Http = f"http://127.0.0.1:{Port}"
        Prelude = """local Service = app:GetService('WebSocketService')
local function Failure(Call)
    local Ok, Error = pcall(Call)
    assert(not Ok and string.find(tostring(Error), '[LUI:WebSocket]', 1, true), tostring(Error))
end
"""

        def Run(Name, Body, Expected=0, Granted=True, Origin=None, Deadline=5000, Raw=False, NoRequests=False):
            Source = Body if Raw else Prelude + "\ntask.spawn(function()\n" + Body + "\nprint('[LUI:WebSocketTest] " + Name + " passed')\nend)"
            (Project / "Src/Main.luau").write_text(Source, encoding="utf-8")
            Manifest.write_text(json.dumps({
                "SchemaVersion": 2, "Script": "Src/Main.luau", "Capabilities": ["network.client"],
                "Extensions": [], "Assets": [], "ExtensionDeclarations": [],
                "Diagnostics": {"Mode": "Development"},
                "NetworkPolicy": {"ClientLoopbackOnly": True, "ClientPortMin": Port, "ClientPortMax": Port,
                                  "ServerLoopbackOnly": True, "ServerPortMin": 0, "ServerPortMax": 0},
                "Modules": {"Root": "Src", "Files": ["Main.luau"], "Requires": [], "Dependencies": []},
            }), encoding="utf-8")
            Arguments = [Host, "--project", str(Manifest), "--diagnostics", "development", "--timeout-ms", str(Deadline)]
            if Granted:
                Arguments += ["--grant", "network.client", "--http-origin", Origin or Http]
            Before = len(Peer.Requests)
            Result = subprocess.run(Arguments, capture_output=True, text=True, timeout=12, encoding="utf-8", errors="replace")
            Output = Result.stdout + Result.stderr
            assert Result.returncode == Expected, (Name, Result.returncode, Output)
            if Expected == 0 and not Raw:
                assert f"[LUI:WebSocketTest] {Name} passed" in Output, (Name, Output)
            if "Exit=" in Output:
                assert "NativeResources=0" in Output, (Name, Output)
            assert "WEBSOCKET_SECRET_CANARY" not in Output, (Name, Output)
            if NoRequests:
                assert len(Peer.Requests) == Before, (Name, Peer.Requests[Before:])
            Results.append({"Name": Name, "Exit": Result.returncode})
            return Output

        try:
            Run("text-and-binary", f"""
local Socket = Service:ConnectAsync('{Ws}/echo')
assert(Socket.State == 'Open' and Socket.Subprotocol == '')
Socket:SendAsync('Hello')
local Text = Socket:ReceiveAsync()
assert(Text and not Text.Binary and Text.Data == 'Hello')
local Bytes = buffer.fromstring('a\\000b\\255')
local Submitted = false
task.spawn(function() Submitted=true; Socket:SendAsync(Bytes) end)
task.defer(function() assert(Submitted); buffer.writeu8(Bytes, 0, 90) end)
local Binary = Socket:ReceiveAsync()
assert(Binary and Binary.Binary and Binary.Data == 'a\\000b\\255')
Socket:SendAsync('binary string', true)
assert(Socket:ReceiveAsync().Binary)
Socket:Close(1000, 'Done')
assert(Socket:ReceiveAsync() == nil and Socket.State == 'Closed')
assert(Socket.CloseCode == 1000)
Socket:Close(); Socket:Close(); Socket:Cancel(); Socket:Cancel()
""")
            Run("fragmented-messages-and-ping", f"""
local Socket = Service:ConnectAsync('{Ws}/fragments')
local Text = Socket:ReceiveAsync()
local Binary = Socket:ReceiveAsync()
assert(Text and Text.Data == 'Hello' and not Text.Binary)
assert(Binary and Binary.Data == 'a\\000\\255' and Binary.Binary)
Socket:Close()
assert(Socket:ReceiveAsync() == nil)
""")
            assert b"probe" in Peer.Pongs, Peer.Pongs
            Run("partial-network-io", f"""
local Socket = Service:ConnectAsync('{Ws}/partial')
assert(Socket:ReceiveAsync().Data == 'partial')
Socket:SendAsync(string.rep('x', 4096))
assert(Socket:ReceiveAsync().Data == string.rep('x', 4096))
Socket:Close(); assert(Socket:ReceiveAsync() == nil)
""")
            Run("subprotocol", f"""
local Socket = Service:ConnectAsync('{Ws}/echo', {{Protocols={{'lui.echo.v1'}}}})
assert(Socket.Subprotocol == 'lui.echo.v1')
Socket:Close(); assert(Socket:ReceiveAsync() == nil)
""")
            Run("automatic-ping-pong", f"""
local Socket = Service:ConnectAsync('{Ws}/ping', {{PingIntervalMs=10, PongTimeoutMs=1000}})
assert(Socket:ReceiveAsync().Data == 'alive')
Socket:Close(); assert(Socket:ReceiveAsync() == nil)
""")
            Run("missing-pong-timeout", f"""
local Socket = Service:ConnectAsync('{Ws}/ping-timeout', {{PingIntervalMs=10, PongTimeoutMs=30}})
assert(Socket:ReceiveAsync().Data == 'ping-seen')
Failure(function() Socket:ReceiveAsync() end)
""")
            for Route in ("bad-accept", "bad-protocol", "bad-extension", "bad-duplicate-accept", "bad-status", "redirect"):
                Run(Route, f"Failure(function() Service:ConnectAsync('{Ws}/{Route}') end)")
            Run("diagnostic-canary-redaction", f"Failure(function() Service:ConnectAsync('{Ws}/bad-canary', {{Headers={{{{Name='Authorization',Value='WEBSOCKET_SECRET_CANARY'}}}}}}) end)")
            Run("failed-connect-retirement", f"""
for Index=1,12 do Failure(function() Service:ConnectAsync('{Ws}/bad-accept') end) end
local Socket=Service:ConnectAsync('{Ws}/echo')
Socket:Close(); assert(Socket:ReceiveAsync()==nil)
""")
            for Route in ("invalid-utf8", "masked-server", "fragmented-control", "invalid-close", "nonminimal"):
                Run(Route, f"local Socket = Service:ConnectAsync('{Ws}/{Route}'); Failure(function() Socket:ReceiveAsync() end); Socket:Cancel()")
            Run("oversized-message", f"""
local Socket = Service:ConnectAsync('{Ws}/oversized', {{MaxMessageBytes=256, MaxFrameBytes=256}})
Failure(function() Socket:ReceiveAsync() end)
Socket:Cancel()
""")
            Run("remote-close-order", f"""
local Socket = Service:ConnectAsync('{Ws}/remote-close')
assert(Socket:ReceiveAsync().Data == 'before-close')
assert(Socket:ReceiveAsync() == nil)
assert(Socket.CloseCode == 1001 and Socket.CloseReason == 'Leaving')
Socket:Close()
""")
            Run("abrupt-disconnect", f"local Socket=Service:ConnectAsync('{Ws}/abrupt'); Failure(function() Socket:ReceiveAsync() end)")
            Run("inbox-saturation", f"""
local Socket = Service:ConnectAsync('{Ws}/flood', {{MaxQueuedMessages=2, MaxQueuedBytes=256}})
assert(app:GetService('HttpService'):GetAsync('{Http}/barrier/flood').Success)
local Failed = false
for Index=1,65 do
    local Ok, Message = pcall(function() return Socket:ReceiveAsync() end)
    if not Ok then Failed=true; break end
    if Message == nil then break end
end
assert(Failed)
Socket:Cancel()
""")
            Run("precanceled-no-io", f"""
local Token = app:GetService('HttpService'):CreateCancellationToken()
Token:Cancel(); Token:Cancel()
Failure(function() Service:ConnectAsync('{Ws}/echo', {{CancellationToken=Token}}) end)
""", NoRequests=True)
            Run("cancel-handshake-barrier", f"""
local Token = app:GetService('HttpService'):CreateCancellationToken()
local Finished = false
task.spawn(function()
    Failure(function() Service:ConnectAsync('{Ws}/stall-upgrade', {{CancellationToken=Token}}) end)
    Finished = true
end)
assert(app:GetService('HttpService'):GetAsync('{Http}/barrier/upgrade').Success)
Token:Cancel()
task.defer(function() assert(Finished or Token.IsCanceled) end)
""")
            Run("cancel-active-token-isolation", f"""
local Http = app:GetService('HttpService')
local Token = Http:CreateCancellationToken()
local A = Service:ConnectAsync('{Ws}/active', {{CancellationToken=Token}})
local B = Service:ConnectAsync('{Ws}/echo')
Token:Cancel()
Failure(function() A:ReceiveAsync() end)
B:SendAsync('independent'); assert(B:ReceiveAsync().Data == 'independent')
B:Close(); assert(B:ReceiveAsync() == nil)
""")
            Run("overlapping-receives", f"""
local Socket = Service:ConnectAsync('{Ws}/active')
local Second = false
task.spawn(function()
    Failure(function() Socket:ReceiveAsync() end)
    assert(Second)
end)
task.defer(function()
    Failure(function() Socket:ReceiveAsync() end)
    Second = true
    Socket:Cancel()
end)
""")
            Concurrent = Run("concurrent-connections", f"""
local Count, Connected = 0, 0
local Sockets = {{}}
for Index=1,4 do
    task.spawn(function()
        local Socket = Service:ConnectAsync('{Ws}/echo')
        Sockets[Index] = Socket
        Connected += 1
        if Connected == 4 then
            for Position, Active in Sockets do
                task.spawn(function()
                    Active:SendAsync(tostring(Position))
                    assert(Active:ReceiveAsync().Data == tostring(Position))
                    Active:Close(); assert(Active:ReceiveAsync() == nil)
                    Count += 1
                    if Count == 4 then print('[LUI:WebSocketTest] all concurrent sockets drained') end
                end)
            end
        end
    end)
end
""")
            assert "all concurrent sockets drained" in Concurrent, Concurrent
            Run("send-validation-and-size", f"""
local Socket=Service:ConnectAsync('{Ws}/echo', {{MaxMessageBytes=16, MaxFrameBytes=16}})
Failure(function() Socket:SendAsync(string.rep('x',17)) end)
Failure(function() Socket:SendAsync('\\192\\175') end)
Failure(function() Socket:Close(1005, '') end)
Failure(function() Socket:Close(1000, string.rep('x',124)) end)
Socket:SendAsync('still valid'); assert(Socket:ReceiveAsync().Data == 'still valid')
Socket:Close(); assert(Socket:ReceiveAsync() == nil)
""")
            Run("close-after-admitted-sends", f"""
local Socket = Service:ConnectAsync('{Ws}/ordered-close')
for Index=1,3 do task.spawn(function() Socket:SendAsync('ordered-' .. tostring(Index)) end) end
task.defer(function() Socket:Close() end)
local Closed = false
for Index=1,4 do if Socket:ReceiveAsync() == nil then Closed=true; break end end
assert(Closed)
""")
            assert [Payload for Path, _Opcode, Payload in Peer.Messages if Path == "/ordered-close"] == [b"ordered-1", b"ordered-2", b"ordered-3"]
            Run("outbound-queue-saturation", f"""
local Socket = Service:ConnectAsync('{Ws}/outbox', {{MaxQueuedMessages=1}})
task.spawn(function() Socket:SendAsync('admitted') end)
task.defer(function()
    Failure(function() Socket:SendAsync('rejected') end)
    Socket:Close()
end)
local Closed = false
for Index=1,2 do if Socket:ReceiveAsync() == nil then Closed=true; break end end
assert(Closed)
""")
            assert [Payload for Path, _Opcode, Payload in Peer.Messages if Path == "/outbox"] == [b"admitted"]
            Run("origin-denied-no-io", f"Failure(function() Service:ConnectAsync('{Ws}/echo') end)",
                Origin=f"http://localhost:{Port}", NoRequests=True)
            Run("capability-denied", "assert(true)", Granted=False, Expected=1, Raw=True, NoRequests=True)
            Run("handshake-timeout", f"Failure(function() Service:ConnectAsync('{Ws}/stall-upgrade', {{ConnectTimeoutMs=30}}) end)")
            Run("active-idle-timeout", f"local Socket=Service:ConnectAsync('{Ws}/active', {{IdleTimeoutMs=30}}); Failure(function() Socket:ReceiveAsync() end)")
            Run("shutdown-pending-handshake", f"Service:ConnectAsync('{Ws}/stall-upgrade')", Expected=124, Deadline=100)
            Run("shutdown-active-resource", f"local Socket=Service:ConnectAsync('{Ws}/active'); Socket:ReceiveAsync()", Expected=124, Deadline=100)
            for Index in range(3):
                Run(f"fresh-process-generation-{Index}", f"local Socket=Service:ConnectAsync('{Ws}/echo'); Socket:Close(); assert(Socket:ReceiveAsync()==nil)")
        finally:
            Peer.Release.set()
            Peer.shutdown()
            Worker.join(5)

    # Run the exact shipped captured applications, including the vendored lock.
    with Fixture.Peer(("127.0.0.1", 18081)) as Peer:
        Worker = threading.Thread(target=Peer.serve_forever, daemon=True)
        Worker.start()
        try:
            for Name, Marker in (("minimal.lui.json", "Minimal WebSocket passed"),
                                 ("advanced.lui.json", "Advanced WebSocket passed"),
                                 ("lui.json", "Headless HTTP Json and WebSocket passed")):
                Path = str(Root / "examples/headless-websocket" / Name)
                Command = [Host, "--project", Path]
                if len(sys.argv) == 3:
                    Command = ["dotnet", str(pathlib.Path(sys.argv[2]).resolve()), "run", Path,
                               "--mode", "headless", "--host", Host]
                Command += ["--grant", "network.client", "--http-origin", "http://127.0.0.1:18081", "--diagnostics", "development"]
                Result = subprocess.run(Command, capture_output=True, text=True, timeout=12, encoding="utf-8", errors="replace")
                Output = Result.stdout + Result.stderr
                assert Result.returncode == 0 and Marker in Output and "NativeResources=0" in Output, (Name, Result.returncode, Output)
                Results.append({"Name": "shipped-" + Name, "Exit": Result.returncode})
        finally:
            Peer.Release.set()
            Peer.shutdown()
            Worker.join(5)
    print("[LUI:WebSocketHeadless] " + json.dumps({"Passed": len(Results), "Cases": Results}, separators=(",", ":")))


if __name__ == "__main__":
    Main()
