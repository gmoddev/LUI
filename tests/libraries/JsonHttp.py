"""Exercise the vendored HTTP adapter through real loopback IO and VM retirement."""
import argparse
import ctypes as C
import hashlib
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import tempfile
import threading
import time

from Run import Stage, Options, RunOptions, Bind
from Workbench import Grants, Stats


class Handler(BaseHTTPRequestHandler):
    Requests = []
    protocol_version = 'HTTP/1.1'
    PendingStarted = threading.Event()
    PendingReleased = threading.Event()
    PendingClosed = threading.Event()
    PeerClosed = False
    PeerCloseKind = None

    def log_message(Self, *Arguments):
        pass

    def do_GET(Self):
        Self.Respond()

    def do_POST(Self):
        Self.Respond()

    def Respond(Self):
        Body = Self.rfile.read(int(Self.headers.get('Content-Length', '0'))).decode()
        Handler.Requests.append({'Path': Self.path, 'Body': Body, 'ContentType': Self.headers.get('Content-Type')})
        if Self.path == '/pending':
            Handler.PendingStarted.set()
            if Handler.PendingReleased.wait(5):
                Self.connection.settimeout(2)
                try:
                    Handler.PeerClosed = Self.connection.recv(1) == b''
                    Handler.PeerCloseKind = 'EOF' if Handler.PeerClosed else 'UnexpectedData'
                except ConnectionResetError:
                    Handler.PeerClosed = True
                    Handler.PeerCloseKind = 'Reset'
            Handler.PendingClosed.set()
            Self.close_connection = True
            return
        Status = 200
        if Self.path == '/empty':
            Result = ''
        elif Self.path == '/null':
            Result = 'null'
        elif Self.path == '/bad':
            Result = 'SENSITIVE_RESPONSE_CANARY'
        elif Self.path == '/limit':
            Result = json.dumps('x' * 200)
        elif Self.path == '/slow':
            time.sleep(0.1)
            Result = '{}'
        elif Self.path == '/status':
            Status, Result = 409, '{"Reason":"Conflict"}'
        else:
            Result = json.dumps({'Value': json.loads(Body) if Body else 'omitted', 'ContentType': Self.headers.get('Content-Type')})
        Data = Result.encode()
        try:
            Self.send_response(Status)
            Self.send_header('Content-Length', str(len(Data)))
            Self.send_header('Connection', 'close')
            Self.end_headers()
            Self.wfile.write(Data)
        except (BrokenPipeError, ConnectionResetError):
            pass


Checks = '''
local Json = require('@Json/Json')
local JsonClient = require('@JsonClient/JsonClient')
local Result = Instance.new('TextLabel', {Name = 'Result', Text = 'Pending'})
local Client = JsonClient.Open()
task.spawn(function()
    local Ok, Failure = pcall(function()
        local Data, Response = Client:RequestAsync(Origin .. '/echo', {Method = 'POST', Value = false})
        assert(Response.Success and (Data :: any).Value == false)
        Data = Client:RequestAsync(Origin .. '/echo', {Method = 'POST', Value = Json.Null})
        assert((Data :: any).Value == Json.Null)
        Data = Client:RequestAsync(Origin .. '/echo', {Method = 'POST'})
        assert((Data :: any).Value == 'omitted')
        Data = Client:RequestAsync(Origin .. '/echo', {Method = 'POST', Value = {A = 1}, Headers = {{Name = 'cOnTeNt-TyPe', Value = 'application/custom+json'}}})
        assert((Data :: any).ContentType == 'application/custom+json')
        Data = Client:GetAsync(Origin .. '/empty'); assert(Data == nil)
        Data = Client:GetAsync(Origin .. '/null'); assert(Data == Json.Null)
        Data, Response = Client:GetAsync(Origin .. '/status')
        assert(not Response.Success and Response.StatusCode == 409 and (Data :: any).Reason == 'Conflict')
        local Accepted, Failure = pcall(function() Client:GetAsync(Origin .. '/bad') end)
        assert(not Accepted and not string.find(tostring(Failure), 'SENSITIVE_RESPONSE_CANARY', 1, true))
        assert(not pcall(function() Client:RequestAsync(Origin .. '/limit', {MaxResponseBytes = 16}) end))
        assert(not pcall(function() Client:RequestAsync(Origin .. '/slow', {TimeoutMs = 10}) end))
        -- Each refusal must happen before network admission.
        local Cycle: any = {}; Cycle.Self = Cycle
        assert(not pcall(function() Client:RequestAsync(Origin .. '/must-not-send', {Value = Cycle}) end))
        assert(not pcall(function() Client:RequestAsync(Origin .. '/must-not-send', {MaxResponseBytes = false :: any}) end))
        assert(not pcall(function() Client:RequestAsync(Origin .. '/must-not-send', {Headers = {[2] = {Name = 'X', Value = 'x'}} :: any}) end))
        local Meta: any = setmetatable({}, {__index = function() error('METATABLE_CANARY') end})
        Accepted, Failure = pcall(function() Client:RequestAsync(Origin .. '/must-not-send', {Headers = {Meta}}) end)
        assert(not Accepted and not string.find(tostring(Failure), 'METATABLE_CANARY', 1, true))
    end)
    Result.Text = if Ok then 'PASS' else tostring(Failure)
end)
'''


def Execute(Library, Root, Source, Grant, Checker, Pending=False):
    with tempfile.TemporaryDirectory(prefix='lui-json-http-') as Temporary:
        Directory = Path(Temporary)
        Stage(Root, Directory, Source)
        Manifest = Directory / 'lui.json'
        Data = json.loads(Manifest.read_text())
        Data['Capabilities'] = ['network.client'] if Grant else []
        Manifest.write_text(json.dumps(Data), encoding='utf-8')
        if Checker:
            Checked = subprocess.run([str(Checker.resolve()), str(Root / 'types/LUI.d.luau'), '--project', str(Manifest), '--diagnostics', 'development'], capture_output=True, text=True)
            assert Checked.returncode == 0, Checked.stdout + Checked.stderr
        Project = C.c_void_p()
        Runtime = Library.Lui_Create()
        Domain = Library.Lui_CreateNativeDomainV2()
        try:
            assert Runtime and Domain and Library.Lui_SetNativeDomainV2(Runtime, Domain) == 0
            GrantsValue = Grants(C.sizeof(Grants), 1, Grant)
            assert Library.Lui_DeclareCapabilities(Runtime, C.byref(GrantsValue)) == 1
            Input = Options(); Input.StructSize = C.sizeof(Input); Input.AbiVersion = 1
            Bytes = str(Manifest.resolve()).encode()
            Error = C.create_string_buffer(256)
            assert Library.Lui_ProjectPrepare(C.byref(Input), Bytes, len(Bytes), C.byref(Project), Error, len(Error)), Error.value
            Run = RunOptions(C.sizeof(RunOptions), 1, 1, 0, 0)
            assert Library.Lui_RunProject(Runtime, Project, C.byref(Run)), Library.Lui_GetLastError(Runtime)
            Deadline = time.monotonic() + 8
            while time.monotonic() < Deadline:
                Library.Lui_Pump(Runtime)
                if Pending and Handler.PendingStarted.is_set():
                    break
                Tree = json.loads(Library.Lui_GetPreviewTreeJson(Runtime).decode())
                Result = next(Node for Node in Tree if Node['name'] == 'Result')['text']
                if Result != 'Pending':
                    assert Result == 'PASS', Result
                    break
                time.sleep(0.002)
            else:
                raise TimeoutError('JSON workflow did not complete')
        finally:
            Library.Lui_Destroy(Runtime)
            if Pending:
                Handler.PendingReleased.set()
                assert Handler.PendingClosed.wait(3) and Handler.PeerClosed
            if Project:
                Library.Lui_ProjectRelease(Project)
            Deadline = time.monotonic() + 5
            while time.monotonic() < Deadline:
                Library.Lui_PumpNativeDomainV2(Domain)
                State = Stats(); State.StructSize = C.sizeof(State); State.AbiVersion = 2
                assert Library.Lui_GetNativeStatsV2(Domain, C.byref(State)) == 0
                Values = {Name: getattr(State, Name) for Name, _ in Stats._fields_[2:]}
                if not any(Values.values()):
                    break
                time.sleep(0.002)
            Library.Lui_ReleaseNativeDomainV2(Domain)
            assert not any(Values.values()), Values
        return {'Status': 'PASS', 'NativeDomainRetirement': Values, 'SynchronousRuntimeDestruction': 'PASS'}


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--runtime', required=True, type=Path)
    Parser.add_argument('--checker', type=Path)
    Parser.add_argument('--output', required=True, type=Path)
    Arguments = Parser.parse_args()
    Library = C.CDLL(str(Arguments.runtime.resolve())); Bind(Library)
    for Name, Output, Inputs in (
        ('Lui_CreateNativeDomainV2', C.c_void_p, []),
        ('Lui_ReleaseNativeDomainV2', None, [C.c_void_p]),
        ('Lui_PumpNativeDomainV2', C.c_uint32, [C.c_void_p]),
        ('Lui_GetNativeStatsV2', C.c_uint32, [C.c_void_p, C.POINTER(Stats)]),
        ('Lui_SetNativeDomainV2', C.c_uint32, [C.c_void_p, C.c_void_p]),
        ('Lui_DeclareCapabilities', C.c_int, [C.c_void_p, C.POINTER(Grants)]),
        ('Lui_Pump', C.c_int, [C.c_void_p]),
        ('Lui_GetPreviewTreeJson', C.c_char_p, [C.c_void_p]),
    ):
        Function = getattr(Library, Name)
        Function.restype, Function.argtypes = Output, Inputs
    Root = Path(__file__).resolve().parents[2]
    Denied = "local Client = require('@JsonClient/JsonClient'); assert(not pcall(Client.Open)); local Result = Instance.new('TextLabel', {Name = 'Result', Text = 'PASS'})"
    Results = [Execute(Library, Root, Denied, 0, Arguments.checker)]
    Server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    Thread = threading.Thread(target=Server.serve_forever, daemon=True); Thread.start()
    try:
        Origin = 'http://127.0.0.1:' + str(Server.server_port)
        Results.append(Execute(Library, Root, 'local Origin = ' + json.dumps(Origin) + '\n' + Checks, 16, Arguments.checker))
        Pending = "local Result = Instance.new('TextLabel', {Name = 'Result', Text = 'Pending'}); local Client = require('@JsonClient/JsonClient').Open(); task.spawn(function() Client:GetAsync(" + json.dumps(Origin + '/pending') + ") end)"
        Results.append(Execute(Library, Root, Pending, 16, Arguments.checker, Pending=True))
    finally:
        Server.shutdown(); Server.server_close(); Thread.join()
    assert len(Handler.Requests) == 11 and not any(Item['Path'] == '/must-not-send' for Item in Handler.Requests), Handler.Requests
    Report = {'Status': 'PASS', 'RuntimeSha256': hashlib.sha256(Arguments.runtime.read_bytes()).hexdigest(), 'Cases': Results, 'Requests': len(Handler.Requests), 'PendingRequestPeerClosedOnDestroy': Handler.PeerClosed, 'PeerCloseKind': Handler.PeerCloseKind}
    Arguments.output.write_text(json.dumps(Report, indent=2) + '\n')
    print('[LUI:JsonHttp] Native HTTP, grant refusal, invalid inputs and retirement passed')


if __name__ == '__main__':
    Main()
