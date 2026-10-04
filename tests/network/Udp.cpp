#include "LuiRuntime.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <unordered_map>

struct UdpResults {
    std::unordered_map<std::string, int> Logs;
    std::string Error;
};

static void LUI_CALL OnUdpLog(void* Context, const char* Level, const char* Message) {
    auto* Result = static_cast<UdpResults*>(Context);
    if (std::string(Level) == "Error") Result->Error = Message;
    ++Result->Logs[Message];
}

static int CheckUdp(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:NetworkTest] %s\n", Message);
    return Condition ? 0 : 1;
}

static constexpr uint64_t UdpGrants = LUI_CAPABILITY_NETWORK_CLIENT |
    LUI_CAPABILITY_NETWORK_SERVER | LUI_CAPABILITY_NETWORK_RAW;

static int RunUdpCase(const char* Name, const char* Script, const LuiNetworkPolicyV1* Policy = nullptr,
    uint64_t Capabilities = UdpGrants) {
    int Failures = 0;
    auto* Runtime = Lui_Create();
    UdpResults Result;
    Lui_SetLogCallback(Runtime, &Result, OnUdpLog);
    LuiCapabilityDeclarationV1 Grant{sizeof(Grant), LUI_EXTENSION_ABI_VERSION, Capabilities};
    Failures += CheckUdp(Lui_DeclareCapabilities(Runtime, &Grant) == 1, "UDP grants rejected");
    if (Policy) Failures += CheckUdp(Lui_SetNetworkPolicy(Runtime, Policy) == 1, "UDP policy rejected");
    Failures += CheckUdp(Lui_RunScript(Runtime, Script, Name) == 1, Lui_GetLastError(Runtime));
    auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!Failures && Result.Error.empty() && !Result.Logs["[LUI:NetworkTest] UDP_DONE"] &&
        std::chrono::steady_clock::now() < Deadline) {
        Lui_Pump(Runtime);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // Catch duplicate terminal resumes and erroneous late work after completion.
    for (int Index = 0; Index < 10; ++Index) {
        Lui_Pump(Runtime);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Failures += CheckUdp(Result.Error.empty(), Result.Error.c_str());
    Failures += CheckUdp(Result.Logs["[LUI:NetworkTest] UDP_DONE"] == 1, Name);
    Lui_Destroy(Runtime);
    return Failures;
}

int TestUdp() {
    int Failures = RunUdpCase("UdpDatagramRoundTrip", R"(
        local Network = app:GetService('NetworkService')
        local Receiver = Network:BindUdp({Port = 0})
        local Sender = Network:BindUdp({Port = 0})
        assert(Receiver.IsOpen and Receiver.LocalEndpoint.Port > 0)
        assert(Receiver.LocalEndpoint.Address == '127.0.0.1')
        assert(not pcall(function() Receiver.IsOpen = false end))
        assert(not pcall(function() Receiver.LocalEndpoint.Port = 1 end))
        assert(not pcall(function() Receiver:ReceiveFromAsync() end))
        assert(not pcall(function() Sender:SendToAsync(Receiver.LocalEndpoint, 'x') end))
        local Messages = {'', 'a\0b', string.rep('M', 65507)}
        task.spawn(function()
            for Index, Expected in Messages do
                local Datagram = Receiver:ReceiveFromAsync()
                assert(buffer.tostring(Datagram.Data) == Expected)
                assert(Datagram.RemoteEndpoint.Port == Sender.LocalEndpoint.Port)
                assert(Datagram.RemoteEndpoint.Address == '127.0.0.1')
                assert(not pcall(function() Datagram.Data = buffer.create(0) end))
                assert(not pcall(function() Datagram.RemoteEndpoint.Address = '0.0.0.0' end))
                -- The record is read-only; its owned buffer remains mutable.
                if buffer.len(Datagram.Data) > 0 then buffer.writeu8(Datagram.Data, 0, 0) end
                Receiver:SendToAsync(Datagram.RemoteEndpoint, 'ack')
            end
            Receiver:Close()
            print('[LUI:NetworkTest] UDP_DONE')
        end)
        task.spawn(function()
            for Index, Bytes in Messages do
                Sender:SendToAsync(Receiver.LocalEndpoint, Index == 2 and buffer.fromstring(Bytes) or Bytes)
                assert(buffer.tostring(Sender:ReceiveFromAsync().Data) == 'ack')
            end
            Sender:Close()
            Sender:Close()
            assert(not Sender.IsOpen)
            assert(not pcall(function() Sender:ReceiveFromAsync() end))
        end)
    )");
    Failures += RunUdpCase("UdpIPv6", R"(
        local Network = app:GetService('NetworkService')
        local Receiver = Network:BindUdp({Port = 0, Family = 'IPv6'})
        local Sender = Network:BindUdp({Port = 0, Address = '::1'})
        assert(Receiver.LocalEndpoint.Address == '::1')
        task.spawn(function()
            local Datagram = Receiver:ReceiveFromAsync()
            assert(buffer.tostring(Datagram.Data) == 'IPv6' and Datagram.RemoteEndpoint.Address == '::1')
            Receiver:Close()
            print('[LUI:NetworkTest] UDP_DONE')
        end)
        task.spawn(function() Sender:SendToAsync(Receiver.LocalEndpoint, 'IPv6') Sender:Close() end)
    )");
    Failures += RunUdpCase("UdpOversizeAndRecovery", R"(
        local Network = app:GetService('NetworkService')
        local Receiver = Network:BindUdp({Port = 0, MaxDatagramBytes = 8})
        local Sender = Network:BindUdp({Port = 0})
        local function ExpectError(Code, Callback)
            local Ok, Error = pcall(Callback)
            assert(not Ok and string.find(Error, Code), tostring(Error))
        end
        ExpectError('MessageTooLarge', function() Receiver:SendToAsync(Sender.LocalEndpoint, string.rep('x', 9)) end)
        ExpectError('MessageTooLarge', function() Sender:SendToAsync(Receiver.LocalEndpoint, buffer.create(65508)) end)
        task.spawn(function()
            for Index = 1, 3 do
                ExpectError('MessageTooLarge', function() Receiver:ReceiveFromAsync() end)
                assert(Receiver.IsOpen)
                Receiver:SendToAsync(Sender.LocalEndpoint, 'ack')
            end
            assert(buffer.tostring(Receiver:ReceiveFromAsync().Data) == '12345678')
            Receiver:Close()
            print('[LUI:NetworkTest] UDP_DONE')
        end)
        task.spawn(function()
            for Index, Length in {9, 64, 65507} do
                Sender:SendToAsync(Receiver.LocalEndpoint, string.rep('x', Length))
                Sender:ReceiveFromAsync()
            end
            Sender:SendToAsync(Receiver.LocalEndpoint, '12345678')
            Sender:Close()
        end)
    )");
    Failures += RunUdpCase("UdpSendOwnsBuffer", R"(
        local Network = app:GetService('NetworkService')
        local Receiver = Network:BindUdp({Port = 0})
        local Sender = Network:BindUdp({Port = 0})
        local Bytes = buffer.fromstring('original\0bytes')
        task.spawn(function()
            assert(buffer.tostring(Receiver:ReceiveFromAsync().Data) == 'original\0bytes')
            Receiver:Close()
            print('[LUI:NetworkTest] UDP_DONE')
        end)
        task.spawn(function() Sender:SendToAsync(Receiver.LocalEndpoint, Bytes) Sender:Close() end)
        task.defer(function() buffer.fill(Bytes, 0, 42) end)
    )");
    Failures += RunUdpCase("UdpPendingReceiveCancellation", R"(
        local Network = app:GetService('NetworkService')
        local Socket = Network:BindUdp({Port = 0})
        task.spawn(function()
            local Ok, Error = pcall(function() Socket:ReceiveFromAsync() end)
            assert(not Ok and string.find(Error, 'Canceled'))
            print('[LUI:NetworkTest] UDP_DONE')
        end)
        task.spawn(function()
            local Ok, Error = pcall(function() Socket:ReceiveFromAsync() end)
            assert(not Ok and string.find(Error, 'ReceivePending'))
            Socket:Close()
            Socket:Close()
        end)
    )");
    Failures += RunUdpCase("UdpLimitsAndValidation", R"(
        local Network = app:GetService('NetworkService')
        for Index, Options in {
            {Port = 0, MaxDatagramBytes = 0}, {Port = 0, MaxDatagramBytes = 65508},
            {Port = 0, MaxDatagramBytes = 1.5}, {Port = 0, Family = 'DualStack'},
            {Port = -1}, {Port = 1.5}, {Port = 65536}, {Port = 0, Address = 'localhost'},
            {Port = 0, Address = '127.0.0.1', Family = 'IPv6'}, {Port = 0, Address = '::ffff:127.0.0.1'},
            {Port = 0, Address = '127.0.0.1\0suffix'},
        } do assert(not pcall(function() Network:BindUdp(Options) end)) end
        local Sockets = {}
        for Index = 1, 16 do Sockets[Index] = Network:BindUdp({Port = 0}) end
        local Ok, Error = pcall(function() Network:BindUdp({Port = 0}) end)
        assert(not Ok and string.find(Error, 'TooManySockets'))
        local Duplicate, BindError = pcall(function() Network:BindUdp({Port = Sockets[1].LocalEndpoint.Port}) end)
        -- Quota is checked first; use existing sockets to test destination validation.
        assert(not Duplicate and string.find(BindError, 'TooManySockets'))
        for Index, Endpoint in {
            {Address = 'localhost', Port = 1}, {Address = '::1', Port = 1},
            {Address = '0.0.0.0', Port = 1}, {Address = '224.0.0.1', Port = 1},
            {Address = '255.255.255.255', Port = 1}, {Address = '127.0.0.1', Port = 0},
            {Address = '127.0.0.1\0suffix', Port = 1},
        } do assert(not pcall(function() Sockets[1]:SendToAsync(Endpoint, 'x') end)) end
        local Finished = 0
        for Index, Socket in Sockets do
            task.spawn(function()
                local Received, Failure = pcall(function() Socket:ReceiveFromAsync() end)
                assert(not Received and string.find(Failure, 'Canceled'))
                Finished += 1
                if Finished == 16 then print('[LUI:NetworkTest] UDP_DONE') end
            end)
        end
        task.defer(function()
            for Index, Socket in Sockets do Socket:Close() end
            local Bound, Failure = pcall(function() Network:BindUdp({Port = 0}) end)
            assert(not Bound and string.find(Failure, 'TooManySockets'))
        end)
    )");
    Failures += RunUdpCase("UdpBindConflict", R"(
        local Network = app:GetService('NetworkService')
        local Socket = Network:BindUdp({Port = 0})
        local Ok, Error = pcall(function() Network:BindUdp({Port = Socket.LocalEndpoint.Port}) end)
        assert(not Ok and string.find(Error, 'AddressInUse'))
        Socket:Close()
        print('[LUI:NetworkTest] UDP_DONE')
    )");
    Failures += RunUdpCase("UdpOutstandingLimit", R"(
        local Network = app:GetService('NetworkService')
        local Sockets = {}
        local Finished = 0
        local Limited = false
        local function Complete(Callback)
            local Ok, Error = pcall(Callback)
            assert(Ok or string.find(Error, 'Canceled'), tostring(Error))
            Finished += 1
            if Finished == 128 then
                assert(Limited)
                print('[LUI:NetworkTest] UDP_DONE')
            end
        end
        -- Native results cannot drain while this batch of owner tasks runs.
        -- Empty sends count as operations, even though they cost no payload bytes.
        for Index = 1, 16 do
            local Socket = Network:BindUdp({Port = 0})
            Sockets[Index] = Socket
            task.spawn(function() Complete(function() Socket:ReceiveFromAsync() end) end)
            for Send = 1, 7 do
                task.spawn(function() Complete(function() Socket:SendToAsync(Socket.LocalEndpoint, '') end) end)
            end
        end
        task.spawn(function()
            local Ok, Error = pcall(function() Sockets[1]:SendToAsync(Sockets[1].LocalEndpoint, '') end)
            assert(not Ok and string.find(Error, 'TooManyOperations'), tostring(Error))
            Limited = true
            for Index, Socket in Sockets do Socket:Close() end
        end)
    )");
    Failures += RunUdpCase("UdpBindGrant", R"(
        local Network = app:GetService('NetworkService')
        assert(not pcall(function() Network:BindUdp({Port = 0}) end))
        print('[LUI:NetworkTest] UDP_DONE')
    )", nullptr, LUI_CAPABILITY_NETWORK_CLIENT | LUI_CAPABILITY_NETWORK_RAW);
    Failures += RunUdpCase("UdpSendGrant", R"(
        local Socket = app:GetService('NetworkService'):BindUdp({Port = 0})
        assert(not pcall(function() Socket:SendToAsync(Socket.LocalEndpoint, 'x') end))
        Socket:Close()
        print('[LUI:NetworkTest] UDP_DONE')
    )", nullptr, LUI_CAPABILITY_NETWORK_SERVER | LUI_CAPABILITY_NETWORK_RAW);
    LuiNetworkPolicyV1 Loopback{sizeof(Loopback), LUI_EXTENSION_ABI_VERSION,
        LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY | LUI_NETWORK_POLICY_SERVER_LOOPBACK_ONLY, 0, 0, 0, 0};
    Failures += RunUdpCase("UdpLoopbackPolicy", R"(
        local Network = app:GetService('NetworkService')
        local Bound, Failure = pcall(function() Network:BindUdp({Port = 0, Address = 'any'}) end)
        assert(not Bound and string.find(Failure, 'PolicyDenied'))
        local Socket = Network:BindUdp({Port = 0})
        local Sent, Error = pcall(function() Socket:SendToAsync({Address = '192.0.2.1', Port = 9000}, '') end)
        assert(not Sent and string.find(Error, 'PolicyDenied'))
        task.spawn(function() Socket:SendToAsync(Socket.LocalEndpoint, 'allowed') end)
        task.spawn(function()
            assert(buffer.tostring(Socket:ReceiveFromAsync().Data) == 'allowed')
            Socket:Close()
            print('[LUI:NetworkTest] UDP_DONE')
        end)
    )", &Loopback);
    LuiNetworkPolicyV1 Ports{sizeof(Ports), LUI_EXTENSION_ABI_VERSION, 0, 9000, 9000, 9000, 9000};
    Failures += RunUdpCase("UdpServerPortPolicy", R"(
        local Bound, Error = pcall(function() app:GetService('NetworkService'):BindUdp({Port = 0}) end)
        assert(not Bound and string.find(Error, 'PolicyDenied'))
        print('[LUI:NetworkTest] UDP_DONE')
    )", &Ports);
    Ports.ServerPortMin = Ports.ServerPortMax = 0;
    Failures += RunUdpCase("UdpClientPortPolicy", R"(
        local Socket = app:GetService('NetworkService'):BindUdp({Port = 0})
        local Sent, Error = pcall(function() Socket:SendToAsync({Address = '127.0.0.1', Port = 9001}, 'x') end)
        assert(not Sent and string.find(Error, 'PolicyDenied'))
        Socket:Close()
        print('[LUI:NetworkTest] UDP_DONE')
    )", &Ports);

    auto* Runtime = Lui_Create();
    LuiCapabilityDeclarationV1 Grant{sizeof(Grant), LUI_EXTENSION_ABI_VERSION, UdpGrants};
    Failures += CheckUdp(Lui_DeclareCapabilities(Runtime, &Grant) == 1 && Lui_RunScript(Runtime, R"(
        local Network = app:GetService('NetworkService')
        for Index = 1, 16 do
            local Socket = Network:BindUdp({Port = 0})
            task.spawn(function() Socket:ReceiveFromAsync() end)
        end
    )", "UdpTeardown") == 1, Lui_GetLastError(Runtime));
    Lui_Pump(Runtime);
    Lui_Destroy(Runtime);
    if (!Failures) std::puts("[LUI:NetworkTest] UDP boundaries, binary/empty/maximum payloads, IPv6, oversize recovery, ownership, cancellation, limits, grants, policy, and teardown passed");
    return Failures;
}
