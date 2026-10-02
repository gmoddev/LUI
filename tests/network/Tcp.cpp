#include "LuiRuntime.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

struct Results {
    bool Done = false;
    bool Canceled = false;
    bool UnexpectedEof = false;
    bool ReadCanceled = false;
    std::string Error;
};

static void LUI_CALL OnLog(void* Context, const char* Level, const char* Message) {
    auto* Result = static_cast<Results*>(Context);
    if (std::string(Level) == "Error") Result->Error = Message;
    if (std::string(Message) == "TCP_DONE") Result->Done = true;
    if (std::string(Message) == "TCP_CANCELED") Result->Canceled = true;
    if (std::string(Message) == "TCP_UNEXPECTED_EOF") Result->UnexpectedEof = true;
    if (std::string(Message) == "TCP_READ_CANCELED") Result->ReadCanceled = true;
}

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:NetworkTest] %s\n", Message);
    return Condition ? 0 : 1;
}

static bool PumpUntil(LuiRuntime* Runtime, bool& Flag) {
    auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!Flag && std::chrono::steady_clock::now() < Deadline) {
        Lui_Pump(Runtime);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return Flag;
}

int main() {
    int Failures = 0;
    LuiRuntime* Denied = Lui_Create();
    Failures += Check(Lui_RunScript(Denied,
        "assert(not pcall(function() app:GetService('NetworkService') end))", "NetworkDenied") == 1,
        "NetworkService was available without a grant");
    Lui_Destroy(Denied);

    LuiRuntime* Partial = Lui_Create();
    LuiCapabilityDeclarationV1 ClientOnly{sizeof(ClientOnly), LUI_EXTENSION_ABI_VERSION,
        LUI_CAPABILITY_NETWORK_CLIENT | LUI_CAPABILITY_NETWORK_RAW};
    Failures += Check(Lui_DeclareCapabilities(Partial, &ClientOnly) == 1 &&
        Lui_RunScript(Partial,
            "local N = app:GetService('NetworkService')\n"
            "assert(not pcall(function() N:ListenTcp({Port = 0}) end))\n"
            "assert(app:GetService('PlatformService'):Supports('network.client'))\n"
            "assert(not app:GetService('PlatformService'):Supports('network.server'))",
            "NetworkDirection") == 1, Lui_GetLastError(Partial));
    Lui_Destroy(Partial);

    LuiRuntime* Runtime = Lui_Create();
    Results Result;
    Lui_SetLogCallback(Runtime, &Result, OnLog);
    LuiCapabilityDeclarationV1 Grant{sizeof(Grant), LUI_EXTENSION_ABI_VERSION,
        LUI_CAPABILITY_NETWORK_CLIENT | LUI_CAPABILITY_NETWORK_SERVER | LUI_CAPABILITY_NETWORK_RAW};
    Failures += Check(Lui_DeclareCapabilities(Runtime, &Grant) == 1, "network grants rejected");
    const char* Script = R"(
        local Network = app:GetService('NetworkService')
        local Listener = Network:ListenTcp({Port = 0})
        assert(Listener.Port > 0 and Listener.IsListening)
        assert(#Listener.BoundEndpoints == 1 and Listener.BoundEndpoints[1].Port == Listener.Port)
        task.spawn(function()
            local Peer = Listener:AcceptAsync()
            assert(Peer.LocalEndpoint.Port == Listener.Port and Peer.RemoteEndpoint.Port > 0)
            local Bytes = Peer:ReadExactAsync(5)
            assert(buffer.tostring(Bytes) == 'hello')
            Peer:WriteAsync(buffer.fromstring('world'))
            Peer:Shutdown('Write')
        end)
        task.spawn(function()
            local Client = Network:ConnectTcp({Address = '127.0.0.1', Port = Listener.Port})
            assert(Client.RemoteEndpoint.Port == Listener.Port and Client.LocalEndpoint.Port > 0)
            Client:WriteAsync('hello')
            local Bytes = Client:ReadExactAsync(5)
            assert(buffer.tostring(Bytes) == 'world')
            assert(Client:ReadAsync(8) == nil)
            Client:Close()
            Client:Close()
            Listener:Close()
            Listener:Close()
            print('TCP_DONE')
        end)
    )";
    Failures += Check(Lui_RunScript(Runtime, Script, "TcpRoundTrip") == 1, Lui_GetLastError(Runtime));
    Failures += Check(PumpUntil(Runtime, Result.Done), "TCP round trip timed out");
    Failures += Check(Result.Error.empty(), Result.Error.c_str());

    const char* EarlyEof = R"(
        local Network = app:GetService('NetworkService')
        local Listener = Network:ListenTcp({Port = 0})
        task.spawn(function()
            local Peer = Listener:AcceptAsync()
            local Ok, Error = pcall(function() Peer:ReadExactAsync(2) end)
            assert(not Ok and string.find(Error, 'UnexpectedEof'))
            Peer:Close()
            Listener:Close()
            print('TCP_UNEXPECTED_EOF')
        end)
        task.spawn(function()
            local Client = Network:ConnectTcp({Port = Listener.Port})
            Client:WriteAsync('x')
            Client:Shutdown('Write')
        end)
    )";
    Failures += Check(Lui_RunScript(Runtime, EarlyEof, "TcpEarlyEof") == 1, Lui_GetLastError(Runtime));
    Failures += Check(PumpUntil(Runtime, Result.UnexpectedEof), "exact read did not report early EOF");
    Failures += Check(Result.Error.empty(), Result.Error.c_str());

    const char* CancelRead = R"(
        local Network = app:GetService('NetworkService')
        local Listener = Network:ListenTcp({Port = 0})
        task.spawn(function()
            local Peer = Listener:AcceptAsync()
            task.defer(function() Peer:Close() end)
            local Ok, Error = pcall(function() Peer:ReadAsync(1) end)
            assert(not Ok and string.find(Error, 'Canceled'))
            Listener:Close()
            print('TCP_READ_CANCELED')
        end)
        task.spawn(function()
            local Client = Network:ConnectTcp({Port = Listener.Port})
        end)
    )";
    Failures += Check(Lui_RunScript(Runtime, CancelRead, "TcpCancelRead") == 1, Lui_GetLastError(Runtime));
    Failures += Check(PumpUntil(Runtime, Result.ReadCanceled), "pending read was not canceled");
    Failures += Check(Result.Error.empty(), Result.Error.c_str());

    const char* Cancel = R"(
        local Network = app:GetService('NetworkService')
        local Listener = Network:ListenTcp({Port = 0})
        task.spawn(function()
            local Ok, Error = pcall(function() Listener:AcceptAsync() end)
            assert(not Ok and string.find(Error, 'Canceled'))
            print('TCP_CANCELED')
        end)
        task.defer(function() Listener:Close() end)
    )";
    Failures += Check(Lui_RunScript(Runtime, Cancel, "TcpCancel") == 1, Lui_GetLastError(Runtime));
    Failures += Check(PumpUntil(Runtime, Result.Canceled), "pending accept was not canceled");
    Failures += Check(Result.Error.empty(), Result.Error.c_str());
    Lui_Destroy(Runtime);

    LuiRuntime* Teardown = Lui_Create();
    Failures += Check(Lui_DeclareCapabilities(Teardown, &Grant) == 1 &&
        Lui_RunScript(Teardown,
            "local N = app:GetService('NetworkService')\n"
            "local L = N:ListenTcp({Port = 0})\n"
            "task.spawn(function() L:AcceptAsync() end)", "TcpTeardown") == 1,
        Lui_GetLastError(Teardown));
    Lui_Pump(Teardown);
    Lui_Destroy(Teardown);

    if (!Failures) std::puts("[LUI:NetworkTest] TCP grants, round trip, EOF, read/accept cancellation, and teardown passed");
    return Failures ? 1 : 0;
}
