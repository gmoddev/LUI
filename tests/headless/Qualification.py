"""Exercise the actual headless executable with isolated captured projects.

Usage: python Qualification.py <LuiHeadlessHost executable>
Only loopback synthetic traffic is generated. No external service is required.
"""

import http.server
import hashlib
import json
import pathlib
import signal
import subprocess
import sys
import tempfile
import threading


class Peer(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    Release = threading.Event()
    Arrived = threading.Event()

    def log_message(self, *_Arguments):
        pass

    def do_GET(self):
        if self.path in ("/stall", "/signal"):
            if self.path == "/signal":
                self.Arrived.set()
            self.Release.wait(5)
            return
        Body = b"a\x00b"
        self.send_response(200)
        self.send_header("Content-Length", str(len(Body)))
        self.end_headers()
        try:
            self.wfile.write(Body)
        except (BrokenPipeError, ConnectionResetError):
            pass


def Main():
    Host = str(pathlib.Path(sys.argv[1]).resolve())
    Results = []
    with tempfile.TemporaryDirectory(prefix="LuiHeadless μ-") as Folder:
        Root = pathlib.Path(Folder)
        (Root / "Src").mkdir()
        Manifest = Root / "lui.json"

        def Prepare(Source, Capabilities=(), Extensions=(), Declarations=()):
            (Root / "Src" / "Main.luau").write_text(Source, encoding="utf-8")
            Manifest.write_text(json.dumps({
                "SchemaVersion": 2, "Script": "Src/Main.luau", "Capabilities": list(Capabilities),
                "Extensions": list(Extensions), "Assets": [], "ExtensionDeclarations": list(Declarations),
                "Diagnostics": {"Mode": "Development"},
                "Modules": {"Root": "Src", "Files": ["Main.luau"], "Requires": [], "Dependencies": []},
            }), encoding="utf-8")

        def Run(Name, Source, Expected=0, Capabilities=(), Arguments=(), Contains=(), Absent=(),
                Extensions=(), Declarations=()):
            Prepare(Source, Capabilities, Extensions, Declarations)
            Result = subprocess.run([Host, "--project", str(Manifest), *Arguments], capture_output=True,
                                    text=True, timeout=12, encoding="utf-8", errors="replace")
            Output = Result.stdout + Result.stderr
            assert Result.returncode == Expected, (Name, Result.returncode, Output)
            for Text in Contains:
                assert Text in Output, (Name, Text, Output)
            for Text in Absent:
                assert Text not in Output, (Name, Text, Output)
            if "Exit=" in Output:
                assert "NativeResources=0" in Output, (Name, Output)
            Results.append({"Name": Name, "Exit": Result.returncode})

        Run("idle", "assert(1 + 1 == 2)", Contains=("Exit=0", "HttpLiveBeforeShutdown=0"))
        Run("restricted-print", "print('HEADLESS_PRIVATE_CANARY')", Absent=("HEADLESS_PRIVATE_CANARY",))
        Run("development-print", "print('HEADLESS_PUBLIC_PRINT')", Arguments=("--diagnostics", "development"),
            Contains=("HEADLESS_PUBLIC_PRINT",))
        Run("deferred-and-delayed", "local Count = 0; task.defer(function() Count += 1 end); "
            "task.delay(0.03,function() assert(Count == 1) end)", Contains=("Exit=0",))
        Run("deferred-failure-redacted", "task.delay(0.03,function() error('HEADLESS_PRIVATE_CANARY') end)",
            Expected=1, Contains=("RuntimeFailed",), Absent=("HEADLESS_PRIVATE_CANARY",))
        Run("ui-refused", "Instance.new('Window')", Expected=1)
        Run("no-network-grant", "app:GetService('HttpService')", Expected=1)
        Run("manifest-does-not-grant", "assert(true)", Expected=1, Capabilities=("network.client",),
            Contains=("CapabilityDenied",))
        Run("explicit-http-policy", "assert(true)", Expected=1, Capabilities=("network.client",),
            Arguments=("--grant", "network.client"), Contains=("HttpPolicyRequired",))
        Run("invalid-poll", "assert(true)", Expected=1, Arguments=("--poll-ms", "0"), Contains=("InvalidArguments",))
        Run("unsupported-native-grant", "assert(true)", Expected=1,
            Arguments=("--grant", "NativeExtensions"), Contains=("UnsupportedCapability",))
        Run("scheduled-deadline", "task.delay(60, function() error('late') end)", Expected=124,
            Arguments=("--timeout-ms", "30"), Contains=("Exit=124",))
        Run("live-http-server", "local Server = app:GetService('HttpServerService'):CreateServer({Port=0}); Server:Start()",
            Expected=124, Capabilities=("network.server",),
            Arguments=("--grant", "network.server", "--timeout-ms", "30"), Contains=("Exit=124",))
        Run("live-tcp-listener", "local Listener = app:GetService('NetworkService'):ListenTcp({Port=0})",
            Expected=124, Capabilities=("network.server", "network.raw"),
            Arguments=("--grant", "network.server", "--grant", "network.raw", "--timeout-ms", "30"),
            Contains=("Exit=124",))
        Run("closed-server-idle", "local Server = app:GetService('HttpServerService'):CreateServer({Port=0}); "
            "Server:Start(); Server:Close(); Server:Close()", Capabilities=("network.server",),
            Arguments=("--grant", "network.server"), Contains=("Exit=0",))
        # Preparation captures these pinned synthetic bytes but the host must
        # refuse the provider before attempting native Query/Init or app entry.
        NativeBytes = b"HEADLESS_NOT_EXECUTABLE_NATIVE_FIXTURE"
        (Root / "Native").mkdir()
        (Root / "Native" / "Proof.dll").write_bytes(NativeBytes)
        (Root / "Native" / "Proof.json").write_text(json.dumps({
            "SchemaVersion": 2, "AbiVersion": 1, "Provider": "Native",
            "Extension": {"Name": "HeadlessProof", "Version": 1, "RequiredCapabilities": ["NativeExtensions"],
                          "Provenance": {"Kind": "LocalVendor", "Id": "Synthetic-1"},
                          "Bindings": [{"Platform": "WindowsX64", "Path": "Native/Proof.dll",
                                        "Sha256": hashlib.sha256(NativeBytes).hexdigest()}]},
            "Services": [{"Name": "HeadlessProofService", "Methods": [{"Name": "Ping", "Arguments": [], "Returns": "number"}],
                          "Signals": []}],
        }), encoding="utf-8")
        Run("native-provider-refused-before-entry", "error('HEADLESS_PRIVATE_CANARY')", Expected=1,
            Capabilities=("NativeExtensions",), Extensions=("HeadlessProof",), Declarations=("Native/Proof.json",),
            Contains=("NativeProvidersUnsupported",), Absent=("HEADLESS_PRIVATE_CANARY",))
        Run("retired-process-work", "task.delay(1,function() error('HEADLESS_RETIRED_CANARY') end)", Expected=124,
            Arguments=("--timeout-ms", "30"), Absent=("HEADLESS_RETIRED_CANARY",))
        for Index in range(3):
            Run(f"fresh-process-generation-{Index}", "assert(true)",
                Contains=("Exit=0", "HttpLiveBeforeShutdown=0"), Absent=("HEADLESS_RETIRED_CANARY",))

        with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Peer) as Server:
            Server.daemon_threads = True
            Worker = threading.Thread(target=Server.serve_forever, daemon=True)
            Worker.start()
            Origin = f"http://127.0.0.1:{Server.server_port}"
            Arguments = ("--grant", "network.client", "--http-origin", Origin)
            Prefix = "local Http = app:GetService('HttpService'); local Url = " + json.dumps(Origin) + "; "
            try:
                Run("async-binary-http", Prefix + "task.spawn(function() local R = Http:GetAsync(Url .. '/ok'); "
                    "assert(R.StatusCode == 200 and R.Body == 'a' .. string.char(0) .. 'b') end)",
                    Capabilities=("network.client",), Arguments=Arguments,
                    Contains=("Exit=0", "HttpLiveBeforeShutdown=0", "HttpReservedBeforeShutdown=0"))
                Run("public-policy-refuses-loopback", Prefix + "task.spawn(function() local Ok = pcall(function() "
                    "Http:GetAsync(Url .. '/ok') end); assert(not Ok) end)",
                    Capabilities=("network.client",), Arguments=(*Arguments, "--http-public-only"),
                    Contains=("Exit=0", "HttpLiveBeforeShutdown=0"))
                Run("http-cancellation", Prefix + "task.spawn(function() local Ok, E = pcall(function() "
                    "Http:GetAsync(Url .. '/stall') end); assert(not Ok and string.find(E, 'Canceled', 1, true)) end); "
                    "task.defer(function() Http:CancelAll() end)", Capabilities=("network.client",),
                    Arguments=Arguments, Contains=("Exit=0", "HttpLiveBeforeShutdown=0"))
                Run("http-timeout", Prefix + "task.spawn(function() local Ok, E = pcall(function() "
                    "Http:RequestAsync({Url = Url .. '/stall', TimeoutMs = 30}) end); "
                    "assert(not Ok and string.find(E, 'TimedOut', 1, true)) end)", Capabilities=("network.client",),
                    Arguments=Arguments, Contains=("Exit=0", "HttpLiveBeforeShutdown=0"))
                Run("shutdown-outstanding-http", Prefix + "task.spawn(function() Http:GetAsync(Url .. '/stall') end)",
                    Expected=124, Capabilities=("network.client",), Arguments=(*Arguments, "--timeout-ms", "30"),
                    Contains=("Exit=124", "NativeResources=0"))
                if sys.platform != "win32":
                    Prepare(Prefix + "task.spawn(function() Http:GetAsync(Url .. '/signal') end)", ("network.client",))
                    Child = subprocess.Popen([Host, "--project", str(Manifest), *Arguments], stdout=subprocess.PIPE,
                                             stderr=subprocess.PIPE, text=True)
                    try:
                        assert Peer.Arrived.wait(5), "Signal peer barrier was not reached"
                        Child.send_signal(signal.SIGTERM)
                        Output, Errors = Child.communicate(timeout=5)
                        assert Child.returncode == 130 and "NativeResources=0" in Output, (Child.returncode, Output, Errors)
                        Results.append({"Name": "signal-outstanding-http", "Exit": Child.returncode})
                    finally:
                        if Child.poll() is None:
                            Child.kill()
                            Child.communicate(timeout=5)
            finally:
                Peer.Release.set()
                Server.shutdown()
                Worker.join(5)

    print(json.dumps({"Status": "PASS", "Cases": Results}, indent=2))


if __name__ == "__main__":
    Main()
