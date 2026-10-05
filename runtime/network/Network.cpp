#include "../internal/Network.h"
#include "../internal/State.h"
#include "../internal/Scheduler.h"
#include "http/Serializer.h"
#include "tls/Provider.h"

#include "lua.h"
#include "lualib.h"
#include <asio.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using Tcp = asio::ip::tcp;
using ErrorCode = asio::error_code;

template<typename... Arguments>
static int Raise(lua_State* State, const char* Format, Arguments... Values) {
    luaL_error(State, Format, Values...);
    return 0;
}

struct NetworkConnection;
struct NetworkDatagramSocket;
struct HttpOperation;
struct HostedServer;
struct HostedSession;
static void RegisterHostedTypes(lua_State* State);
static void DispatchHosted(LuiRuntime* Runtime, const std::shared_ptr<HostedSession>& Session);
static void ReleaseHosted(LuiRuntime* Runtime);
struct NetworkListener {
    explicit NetworkListener(asio::io_context& Io) : Io(Io) {}
    asio::io_context& Io;
    std::vector<std::unique_ptr<Tcp::acceptor>> Acceptors;
    std::atomic<bool> Open{true};
    std::atomic<bool> AcceptPending{false};
    std::vector<Tcp::endpoint> BoundEndpoints;
    std::deque<std::shared_ptr<NetworkConnection>> Ready; // Network worker only.
    std::array<bool, 2> AcceptActive{false, false}; // Network worker only.
    int WaiterReference = 0; // Network worker only.
    unsigned short Port = 0;
};

struct NetworkConnection {
    explicit NetworkConnection(asio::io_context& Io) : Socket(Io) {}
    int Id = 0; // Scheduler owner only.
    bool GcPending = false; // Scheduler owner only.
    Tcp::socket Socket;
    std::atomic<bool> Open{true};
    std::atomic<bool> ReadPending{false};
    std::atomic<size_t> QueuedWriteBytes{0};
    std::string LocalAddress;
    std::string RemoteAddress;
    unsigned short LocalPort = 0;
    unsigned short RemotePort = 0;
    struct WriteEntry { int Reference; std::shared_ptr<std::string> Bytes; };
    std::deque<WriteEntry> Writes; // Network worker only.
    bool Writing = false; // Network worker only.
    struct SignalListener { int Id; int Reference; bool Active; };
    std::vector<SignalListener> ClosedListeners; // Scheduler owner only.
};

struct NetworkContext {
    explicit NetworkContext(LuiRuntime* Owner) : Runtime(Owner), Work(asio::make_work_guard(Io)),
        Worker([this] { Io.run(); }) {}
    LuiRuntime* Runtime;
    asio::io_context Io;
    asio::executor_work_guard<asio::io_context::executor_type> Work;
    int NextId = 1;
    int NextSignalId = 1;
    size_t HttpOutstanding = 0; // Scheduler owner only; includes queued completions.
    uint64_t HttpSequence = 0; // Scheduler owner only.
    std::atomic<uint64_t> HttpCancelThrough{0};
    std::atomic<bool> HttpCancelPending{false};
    std::unordered_map<int, std::shared_ptr<HttpOperation>> HttpOperations; // Network worker only.
    std::unordered_map<int, std::shared_ptr<NetworkListener>> Listeners;
    std::unordered_map<int, std::shared_ptr<NetworkConnection>> Connections;
    std::unordered_map<int, std::shared_ptr<NetworkDatagramSocket>> Datagrams; // Scheduler owner only.
    std::unordered_map<int, std::shared_ptr<HostedServer>> Servers; // Scheduler owner only.
    std::unordered_map<int, std::shared_ptr<HostedSession>> HandlerTasks; // Scheduler owner only.
    std::unordered_map<uint64_t, std::shared_ptr<HostedSession>> Sessions; // Network worker only.
    std::shared_ptr<std::atomic<size_t>> HostedSlots = std::make_shared<std::atomic<size_t>>(0);
    uint64_t NextSession = 1; // Network worker only.
    std::shared_ptr<Lui::Tls::Settings> TlsSettings; // Network worker only; immutable after loading.
    std::thread Worker; // Start only after all worker-owned state is initialized.
};

int ConfigureNetworkTls(LuiRuntime* Runtime, const LuiTlsOptionsV1* Options) {
    if (!Options || Options->StructSize < sizeof(LuiTlsOptionsV1) ||
        Options->AbiVersion != LUI_EXTENSION_ABI_VERSION || Runtime->ApplicationStarted ||
        Runtime->Network || Runtime->TlsSettings || Options->TrustAnchorsBytes > 256 * 1024 ||
        Options->ServerPkcs12Bytes > 256 * 1024 || Options->PasswordBytes > 1024 ||
        (!Options->TrustAnchorsPem && Options->TrustAnchorsBytes) ||
        (!Options->ServerPkcs12 && Options->ServerPkcs12Bytes) || (!Options->Password && Options->PasswordBytes)) {
        Runtime->LastError = "[LUI:Tls] InvalidOptions"; return 0;
    }
    auto Settings = Lui::Tls::Settings::Load(
        std::string_view(Options->TrustAnchorsPem ? Options->TrustAnchorsPem : "", Options->TrustAnchorsBytes),
        std::string_view(Options->ServerPkcs12 ? static_cast<const char*>(Options->ServerPkcs12) : "", Options->ServerPkcs12Bytes),
        std::string(Options->Password ? Options->Password : "", Options->PasswordBytes), Runtime->LastError);
    if (!Settings) return 0;
    Runtime->TlsSettings = std::move(Settings);
    return 1;
}

static std::shared_ptr<Lui::Tls::Settings> GetTlsSettings(NetworkContext* Network, std::string& Error) {
    if (!Network->TlsSettings) Network->TlsSettings = Network->Runtime->TlsSettings;
    if (!Network->TlsSettings) Network->TlsSettings = Lui::Tls::Settings::Load({}, {}, {}, Error);
    return Network->TlsSettings;
}

static LuiRuntime* GetRuntime(lua_State* State) {
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiRuntime");
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, -1));
    lua_pop(State, 1);
    return Runtime;
}

static bool HasMeta(lua_State* State, int Index, const char* Name) {
    if (lua_type(State, Index) != LUA_TUSERDATA || !lua_getmetatable(State, Index)) return false;
    lua_getfield(State, LUA_REGISTRYINDEX, Name);
    bool Same = lua_rawequal(State, -1, -2) != 0;
    lua_pop(State, 2);
    return Same;
}

static NetworkContext* Context(LuiRuntime* Runtime) {
    if (!Runtime->Network) Runtime->Network = new NetworkContext(Runtime);
    return Runtime->Network;
}

static std::string NetworkError(const ErrorCode& Error) {
    if (Error == asio::error::operation_aborted) return "[LUI:Network] Canceled";
    if (Error == asio::error::eof) return "[LUI:Network] UnexpectedEof";
    if (Error == asio::error::connection_refused) return "[LUI:Network] ConnectionRefused";
    if (Error == asio::error::connection_reset) return "[LUI:Network] ConnectionReset";
    if (Error == asio::error::address_in_use) return "[LUI:Network] AddressInUse";
    if (Error == asio::error::access_denied) return "[LUI:Network] PermissionDenied";
    if (Error == asio::error::host_not_found) return "[LUI:Network] NameNotFound";
    if (Error == asio::error::message_size) return "[LUI:Network] MessageTooLarge";
    return "[LUI:Network] IoError: " + Error.message();
}

static void Queue(LuiRuntime* Runtime, NetworkCompletion Completion) {
    std::lock_guard<std::mutex> Lock(Runtime->CompletionMutex);
    if (!Runtime->ShuttingDown) Runtime->NetworkCompletions.push_back(std::move(Completion));
}

static bool QueueClosed(LuiRuntime* Runtime, const std::shared_ptr<NetworkConnection>& Connection) {
    if (!Connection->Open.exchange(false)) return false;
    Queue(Runtime, {NetworkCompletion::Kind::ClosedSignal, 0, {}, "", Connection});
    return true;
}

static void CaptureEndpoints(NetworkConnection* Connection) {
    ErrorCode Error;
    auto Local = Connection->Socket.local_endpoint(Error);
    if (!Error) {
        Connection->LocalAddress = Local.address().to_string();
        Connection->LocalPort = Local.port();
    }
    auto Remote = Connection->Socket.remote_endpoint(Error);
    if (!Error) {
        Connection->RemoteAddress = Remote.address().to_string();
        Connection->RemotePort = Remote.port();
    }
}

static void PushEndpoint(lua_State* State, const std::string& Address, unsigned short Port) {
    lua_createtable(State, 0, 2);
    lua_pushlstring(State, Address.data(), Address.size());
    lua_setfield(State, -2, "Address");
    lua_pushinteger(State, Port);
    lua_setfield(State, -2, "Port");
    lua_setreadonly(State, -1, true);
}

static void CheckAwait(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    if (Runtime->ActiveTaskThread != State || !Runtime->ActiveTaskReference)
        luaL_error(State, "[LUI:Network] async methods require a task.spawn or task.defer coroutine");
    if (Runtime->PendingAsyncReferences.size() >= 128)
        luaL_error(State, "[LUI:Network] TooManyOperations");
}

static int BeginAwait(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    Runtime->PendingAsyncReferences.insert(Runtime->ActiveTaskReference);
    Runtime->ActiveTaskHasWaiter = true;
    return Runtime->ActiveTaskReference;
}

static std::string OptionString(lua_State* State, int Table, const char* Key, const char* Default) {
    lua_getfield(State, Table, Key);
    size_t Length = 0;
    const char* Data = lua_isnil(State, -1) ? nullptr : luaL_checklstring(State, -1, &Length);
    std::string Value = Data ? std::string(Data, Length) : Default;
    lua_pop(State, 1);
    if (Value.find('\0') != std::string::npos)
        luaL_error(State, "[LUI:Network] %s contains a null byte", Key);
    return Value;
}

static unsigned short OptionPort(lua_State* State, int Table, bool AllowZero) {
    lua_getfield(State, Table, "Port");
    double Value = luaL_checknumber(State, -1);
    lua_pop(State, 1);
    if (!std::isfinite(Value) || std::floor(Value) != Value || Value < (AllowZero ? 0 : 1) || Value > 65535)
        luaL_error(State, "[LUI:Network] Port must be an integer between %d and 65535", AllowZero ? 0 : 1);
    return static_cast<unsigned short>(Value);
}

static int CheckedId(lua_State* State, const char* Meta) {
    if (!HasMeta(State, 1, Meta)) luaL_error(State, "[LUI:Network] invalid network object");
    return *static_cast<int*>(lua_touserdata(State, 1));
}

static std::shared_ptr<NetworkListener> GetListener(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    int Id = CheckedId(State, "LuiTcpListenerMeta");
    if (!Runtime->Network) luaL_error(State, "[LUI:Network] Closed");
    auto Found = Runtime->Network->Listeners.find(Id);
    if (Found == Runtime->Network->Listeners.end()) luaL_error(State, "[LUI:Network] Closed");
    return Found->second;
}

static std::shared_ptr<NetworkConnection> GetConnection(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    int Id = CheckedId(State, "LuiTcpConnectionMeta");
    if (!Runtime->Network) luaL_error(State, "[LUI:Network] Closed");
    auto Found = Runtime->Network->Connections.find(Id);
    if (Found == Runtime->Network->Connections.end()) luaL_error(State, "[LUI:Network] Closed");
    return Found->second;
}

static void PushConnection(lua_State* State, const std::shared_ptr<NetworkConnection>& Connection) {
    auto* Network = Context(GetRuntime(State));
    if (Network->Connections.size() >= 64) {
        Connection->Open = false;
        asio::post(Network->Io, [Connection] { ErrorCode Error; Connection->Socket.close(Error); });
        luaL_error(State, "[LUI:Network] TooManyConnections");
    }
    int Id = Network->NextId++;
    Connection->Id = Id;
    *static_cast<int*>(lua_newuserdata(State, sizeof(int))) = Id;
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiTcpConnectionMeta");
    lua_setmetatable(State, -2);
    Network->Connections.emplace(Id, Connection);
}

static int BindTcp(lua_State* State, bool Raw) {
    auto* Runtime = GetRuntime(State);
    if (!(Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_SERVER) ||
        (Raw && !(Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_RAW)))
        return Raise(State, "[LUI:Network] network.server and network.raw grants are required");
    luaL_checktype(State, 2, LUA_TTABLE);
    std::string Address = OptionString(State, 2, "Address", "loopback");
    std::string Family = OptionString(State, 2, "Family", "");
    unsigned short Port = OptionPort(State, 2, true);
    ErrorCode Error;
    const bool SemanticAddress = Address == "loopback" || Address == "any";
    auto Ip = SemanticAddress ? asio::ip::address{} : asio::ip::make_address(Address, Error);
    if (Error) return Raise(State, "[LUI:Network] Address must be a numeric local address");
    if (Family.empty()) Family = !SemanticAddress && Ip.is_v6() ? "IPv6" : "IPv4";
    if (Family != "IPv4" && Family != "IPv6" && Family != "DualStack")
        return Raise(State, "[LUI:Network] Family must be IPv4, IPv6, or DualStack");
    if ((!SemanticAddress && Family == "DualStack") ||
        (!SemanticAddress && ((Family == "IPv4" && !Ip.is_v4()) ||
            (Family == "IPv6" && !Ip.is_v6()))))
        return Raise(State, "[LUI:Network] Address and Family disagree");
    if ((Runtime->NetworkPolicyFlags & LUI_NETWORK_POLICY_SERVER_LOOPBACK_ONLY) &&
        (Address == "any" || (!SemanticAddress && !Ip.is_loopback())))
        return Raise(State, "[LUI:Network] PolicyDenied");
    if (Runtime->ServerPortMin &&
        (Port < Runtime->ServerPortMin || Port > Runtime->ServerPortMax))
        return Raise(State, "[LUI:Network] PolicyDenied");
    auto* Network = Context(Runtime);
    if (Network->Listeners.size() + Network->Servers.size() >= 16) return Raise(State, "[LUI:Network] TooManyListeners");
    auto Listener = std::make_shared<NetworkListener>(Network->Io);
    std::vector<asio::ip::address> Addresses;
    if (Family == "DualStack" || Family == "IPv6")
        Addresses.push_back(asio::ip::make_address(Address == "any" ? "::" : "::1"));
    if (Family == "DualStack" || Family == "IPv4")
        Addresses.push_back(asio::ip::make_address(Address == "any" ? "0.0.0.0" : "127.0.0.1"));
    if (!SemanticAddress) Addresses[0] = Ip;
    // For an ephemeral paired listener, retry if another process takes the
    // selected IPv4 port between the two binds. A failure never leaves half a listener.
    const int Attempts = Port == 0 && Family == "DualStack" ? 16 : 1;
    for (int Attempt = 0; Attempt < Attempts; ++Attempt) {
        Listener->Acceptors.clear();
        Listener->BoundEndpoints.clear();
        unsigned short SharedPort = Port;
        for (const auto& BindAddress : Addresses) {
            auto Acceptor = std::make_unique<Tcp::acceptor>(Network->Io);
            Tcp::endpoint Endpoint(BindAddress, SharedPort);
            Acceptor->open(Endpoint.protocol(), Error);
            if (!Error && BindAddress.is_v6()) Acceptor->set_option(asio::ip::v6_only(true), Error);
            if (!Error) Acceptor->bind(Endpoint, Error);
            if (!Error) Acceptor->listen(asio::socket_base::max_listen_connections, Error);
            if (Error) break;
            auto Bound = Acceptor->local_endpoint(Error);
            if (Error) break;
            SharedPort = Bound.port();
            Listener->BoundEndpoints.push_back(Bound);
            Listener->Acceptors.push_back(std::move(Acceptor));
        }
        if (!Error && Listener->Acceptors.size() == Addresses.size()) {
            Listener->Port = SharedPort;
            break;
        }
        Listener->Acceptors.clear();
        Listener->BoundEndpoints.clear();
        if (Error != asio::error::address_in_use || Attempt + 1 == Attempts) break;
    }
    if (Error) return Raise(State, "%s", NetworkError(Error).c_str());
    int Id = Network->NextId++;
    Network->Listeners.emplace(Id, std::move(Listener));
    *static_cast<int*>(lua_newuserdata(State, sizeof(int))) = Id;
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiTcpListenerMeta");
    lua_setmetatable(State, -2);
    return 1;
}

static int ListenTcp(lua_State* State) { return BindTcp(State, true); }

struct DialOperation {
    DialOperation(asio::io_context& Io, LuiRuntime* Owner, int TaskReference)
        : Runtime(Owner), Reference(TaskReference), Resolver(Io), Deadline(Io), Pace(Io), Io(Io) {}
    LuiRuntime* Runtime;
    int Reference;
    Tcp::resolver Resolver;
    asio::steady_timer Deadline;
    asio::steady_timer Pace;
    asio::io_context& Io;
    std::vector<Tcp::endpoint> Endpoints;
    std::vector<std::shared_ptr<NetworkConnection>> Attempts;
    size_t Next = 0;
    size_t Active = 0;
    bool Done = false;
    bool PacePending = false;
    std::string LastError;
    std::function<void(std::shared_ptr<NetworkConnection>, std::string)> Completion;
};

static void FinishDial(const std::shared_ptr<DialOperation>& Operation,
    const std::shared_ptr<NetworkConnection>& Winner, const std::string& Error) {
    if (Operation->Done) return;
    Operation->Done = true;
    Operation->Deadline.cancel();
    Operation->Pace.cancel();
    Operation->Resolver.cancel();
    for (const auto& Attempt : Operation->Attempts) {
        if (Attempt == Winner) continue;
        ErrorCode CloseError;
        Attempt->Socket.close(CloseError);
    }
    if (Winner) CaptureEndpoints(Winner.get());
    if (Operation->Completion) {
        auto Completion = std::move(Operation->Completion);
        Completion(Winner, Error);
        return;
    }
    Queue(Operation->Runtime, {Winner ? NetworkCompletion::Kind::Connection : NetworkCompletion::Kind::None,
        Operation->Reference, {}, Error, Winner});
}

static void StartDial(const std::shared_ptr<DialOperation>& Operation);

static void ScheduleDialPace(const std::shared_ptr<DialOperation>& Operation) {
    if (Operation->Done || Operation->PacePending || Operation->Next >= Operation->Endpoints.size() ||
        Operation->Active >= 2) return;
    Operation->PacePending = true;
    Operation->Pace.expires_after(std::chrono::milliseconds(250));
    Operation->Pace.async_wait([Operation](const ErrorCode& Error) {
        Operation->PacePending = false;
        if (!Error) StartDial(Operation);
    });
}

static void StartDial(const std::shared_ptr<DialOperation>& Operation) {
    if (Operation->Done || Operation->Active >= 2 || Operation->Next >= Operation->Endpoints.size()) return;
    auto Endpoint = Operation->Endpoints[Operation->Next++];
    auto Connection = std::make_shared<NetworkConnection>(Operation->Io);
    Operation->Attempts.push_back(Connection);
    ++Operation->Active;
    Connection->Socket.async_connect(Endpoint,
        [Operation, Connection](const ErrorCode& Error) {
            if (Operation->Done) return;
            --Operation->Active;
            if (!Error) {
                FinishDial(Operation, Connection, "");
                return;
            }
            ErrorCode CloseError;
            Connection->Socket.close(CloseError);
            Operation->LastError = NetworkError(Error);
            if (Operation->Next >= Operation->Endpoints.size() && Operation->Active == 0)
                FinishDial(Operation, {}, Operation->LastError);
            else ScheduleDialPace(Operation);
        });
    ScheduleDialPace(Operation);
}

static void BeginDial(const std::shared_ptr<DialOperation>& Operation, const std::string& Host,
    unsigned short Port, bool LoopbackOnly, unsigned TimeoutMs = 10000) {
    asio::post(Operation->Io, [Operation, Host, Port, LoopbackOnly, TimeoutMs] {
        if (Operation->Done) return;
        Operation->Deadline.expires_after(std::chrono::milliseconds(TimeoutMs));
        Operation->Deadline.async_wait([Operation](const ErrorCode& Error) {
            if (!Error) FinishDial(Operation, {}, "[LUI:Network] TimedOut");
        });
        Operation->Resolver.async_resolve(Host, std::to_string(Port),
            [Operation, LoopbackOnly](const ErrorCode& Error, Tcp::resolver::results_type Results) {
                if (Operation->Done) return;
                if (Error) {
                    FinishDial(Operation, {}, NetworkError(Error));
                    return;
                }
                std::vector<Tcp::endpoint> IPv4;
                std::vector<Tcp::endpoint> IPv6;
                bool FirstIPv6 = false;
                bool FirstSet = false;
                for (const auto& Result : Results) {
                    const auto Endpoint = Result.endpoint();
                    if (LoopbackOnly && !Endpoint.address().is_loopback()) continue;
                    if (!FirstSet) {
                        FirstIPv6 = Endpoint.address().is_v6();
                        FirstSet = true;
                    }
                    auto& Family = Endpoint.address().is_v6() ? IPv6 : IPv4;
                    if (Family.size() < 16) Family.push_back(Endpoint);
                }
                for (size_t Index = 0; Operation->Endpoints.size() < 16 &&
                    (Index < IPv4.size() || Index < IPv6.size()); ++Index) {
                    const auto& First = FirstIPv6 ? IPv6 : IPv4;
                    const auto& Second = FirstIPv6 ? IPv4 : IPv6;
                    if (Index < First.size()) Operation->Endpoints.push_back(First[Index]);
                    if (Operation->Endpoints.size() < 16 && Index < Second.size())
                        Operation->Endpoints.push_back(Second[Index]);
                }
                if (Operation->Endpoints.empty()) {
                    FinishDial(Operation, {}, LoopbackOnly ? "[LUI:Network] PolicyDenied" :
                        "[LUI:Network] NameNotFound");
                    return;
                }
                StartDial(Operation);
            });
    });
}

static int ConnectTcp(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    if ((Runtime->GrantedCapabilities & (LUI_CAPABILITY_NETWORK_CLIENT | LUI_CAPABILITY_NETWORK_RAW)) !=
        (LUI_CAPABILITY_NETWORK_CLIENT | LUI_CAPABILITY_NETWORK_RAW))
        return Raise(State, "[LUI:Network] network.client and network.raw grants are required");
    luaL_checktype(State, 2, LUA_TTABLE);
    std::string Host = OptionString(State, 2, "Address", "loopback");
    if (Host == "loopback") Host = "127.0.0.1";
    if (Host.empty() || Host.size() > 253 || Host == "any")
        return Raise(State, "[LUI:Network] invalid remote address");
    unsigned short Port = OptionPort(State, 2, false);
    if (Runtime->ClientPortMin &&
        (Port < Runtime->ClientPortMin || Port > Runtime->ClientPortMax))
        return Raise(State, "[LUI:Network] PolicyDenied");
    bool LoopbackOnly = (Runtime->NetworkPolicyFlags & LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY) != 0;
    if (LoopbackOnly) {
        ErrorCode AddressError;
        auto NumericAddress = asio::ip::make_address(Host, AddressError);
        if ((!AddressError && !NumericAddress.is_loopback()) ||
            (AddressError && Host != "localhost"))
            return Raise(State, "[LUI:Network] PolicyDenied");
    }
    CheckAwait(State);
    auto* Network = Context(Runtime);
    if (Network->Connections.size() >= 64) return Raise(State, "[LUI:Network] TooManyConnections");
    int Reference = BeginAwait(State);
    auto Operation = std::make_shared<DialOperation>(Network->Io, Runtime, Reference);
    BeginDial(Operation, Host, Port, LoopbackOnly);
    return lua_yield(State, 0);
}

struct HttpOperation {
    HttpOperation(NetworkContext* Network, int Reference, std::string Wire, std::string Method, size_t MaxBody)
        : Network(Network), Reference(Reference), Wire(std::move(Wire)), Method(std::move(Method)), Deadline(Network->Io) {
        Lui::Http::Limits Bounds;
        Bounds.BodyBytes = MaxBody;
        Parser = std::make_unique<Lui::Http::Parser>(Lui::Http::MessageKind::Response, Bounds, this->Method);
    }
    NetworkContext* Network;
    int Reference;
    std::string Wire, Method;
    uint64_t Sequence = 0;
    asio::steady_timer Deadline;
    std::shared_ptr<DialOperation> Dial;
    std::shared_ptr<NetworkConnection> Connection;
    std::shared_ptr<Lui::Tls::Stream> Tls;
    std::shared_ptr<Lui::Http::Message> ClosingResponse;
    std::unique_ptr<Lui::Http::Parser> Parser;
    std::array<char, 16384> Buffer;
    unsigned Informational = 0;
    size_t MaxBody = 0;
    bool Done = false;
};

static void FinishHttp(const std::shared_ptr<HttpOperation>& Operation, std::string Error,
    std::shared_ptr<Lui::Http::Message> Response = {}) {
    if (Operation->Done) return;
    if (Response && Error.empty() && Operation->Tls && !Operation->ClosingResponse) {
        // Fully framed authenticated content is settled. Cleanup retains its
        // slot until close_notify completes or the existing deadline/cancel fires.
        Operation->ClosingResponse = Response;
        Operation->Parser.reset();
        Operation->Wire.clear();
        Operation->Tls->Socket.async_shutdown([Operation](const ErrorCode&) {
            FinishHttp(Operation, "", Operation->ClosingResponse);
        });
        return;
    }
    if (Operation->ClosingResponse) { Response = std::move(Operation->ClosingResponse); Error.clear(); }
    Operation->Done = true;
    Operation->Deadline.cancel();
    if (Operation->Dial && !Operation->Dial->Done)
        FinishDial(Operation->Dial, {}, "[LUI:Network] Canceled");
    if (Operation->Connection) {
        ErrorCode CloseError;
        Operation->Connection->Socket.close(CloseError);
    }
    Operation->Parser.reset();
    Operation->Network->HttpOperations.erase(Operation->Reference);
    NetworkCompletion Completion;
    Completion.Type = NetworkCompletion::Kind::HttpResponse;
    Completion.Reference = Operation->Reference;
    Completion.Error = std::move(Error);
    Completion.HttpResponse = std::move(Response);
    Queue(Operation->Network->Runtime, std::move(Completion));
}

static std::string HttpError(std::string Error) {
    const std::string Prefix = "[LUI:Network] ";
    if (Error.substr(0, Prefix.size()) == Prefix) Error.erase(0, Prefix.size());
    // Stable transport code only; native descriptions stay outside the response contract.
    if (auto Colon = Error.find(':'); Colon != std::string::npos) Error.resize(Colon);
    return "[LUI:Http] " + Error;
}

static void ReadHttp(const std::shared_ptr<HttpOperation>& Operation) {
    if (Operation->Done) return;
    auto Handler = [Operation](const ErrorCode& Error, size_t Count) {
            if (Operation->Done) return;
            if (Error && Error != asio::error::eof) {
                FinishHttp(Operation, Operation->Tls ? "[LUI:Http] " + Operation->Tls->ErrorName(Error) : HttpError(NetworkError(Error)));
                return;
            }
            std::string_view Bytes(Operation->Buffer.data(), Count);
            while (true) {
                auto Parsed = Operation->Parser->Feed(Bytes, Error == asio::error::eof);
                Bytes.remove_prefix(Parsed.Consumed);
                if (Parsed.Status == Lui::Http::ParseStatus::Failed) {
                    FinishHttp(Operation, std::string("[LUI:Http] ") + Lui::Http::GetErrorName(Parsed.Error));
                    return;
                }
                if (Parsed.Status != Lui::Http::ParseStatus::Complete) break;
                const auto* Response = Operation->Parser->GetResult();
                if (Response->StatusCode >= 200) {
                    // One exchange per socket. Extra octets cannot become another response.
                    if (!Bytes.empty()) FinishHttp(Operation, "[LUI:Http] InvalidFraming");
                    else FinishHttp(Operation, "", std::make_shared<Lui::Http::Message>(*Response));
                    return;
                }
                if (++Operation->Informational > 8) {
                    FinishHttp(Operation, "[LUI:Http] LimitExceeded");
                    return;
                }
                Lui::Http::Limits Bounds;
                Bounds.BodyBytes = Operation->MaxBody;
                Operation->Parser = std::make_unique<Lui::Http::Parser>(Lui::Http::MessageKind::Response, Bounds, Operation->Method);
                // Feed EOF into the fresh parser too, so an informational-only stream fails.
                if (Bytes.empty() && !Error) break;
            }
            ReadHttp(Operation);
        };
    if (Operation->Tls) Operation->Tls->Socket.async_read_some(asio::buffer(Operation->Buffer), std::move(Handler));
    else Operation->Connection->Socket.async_read_some(asio::buffer(Operation->Buffer), std::move(Handler));
}

static void WriteHttp(const std::shared_ptr<HttpOperation>& Operation) {
    auto Handler = [Operation](const ErrorCode& Error, size_t) {
        if (Operation->Done) return;
        if (Error) {
            FinishHttp(Operation, Operation->Tls ? "[LUI:Http] " + Operation->Tls->ErrorName(Error) : HttpError(NetworkError(Error)));
            return;
        }
        ReadHttp(Operation);
    };
    if (Operation->Tls) asio::async_write(Operation->Tls->Socket, asio::buffer(Operation->Wire), std::move(Handler));
    else asio::async_write(Operation->Connection->Socket, asio::buffer(Operation->Wire), std::move(Handler));
}

static size_t HttpNumber(lua_State* State, int Table, const char* Key, size_t Default, size_t Min, size_t Max) {
    lua_getfield(State, Table, Key);
    double Number = lua_isnil(State, -1) ? static_cast<double>(Default) : luaL_checknumber(State, -1);
    lua_pop(State, 1);
    if (!std::isfinite(Number) || std::floor(Number) != Number || Number < Min || Number > Max)
        luaL_error(State, "[LUI:Http] %s is outside its integer bounds", Key);
    return static_cast<size_t>(Number);
}

static std::string HttpString(lua_State* State, int Table, const char* Key, const char* Default) {
    lua_getfield(State, Table, Key);
    size_t Length = 0;
    const char* Data = lua_isnil(State, -1) ? nullptr : luaL_checklstring(State, -1, &Length);
    if (Length > 8192) luaL_error(State, "[LUI:Http] LimitExceeded");
    std::string Value = Data ? std::string(Data, Length) : Default;
    lua_pop(State, 1);
    return Value;
}

static int HttpRequest(lua_State* State, bool Get) {
    auto* Runtime = GetRuntime(State);
    if (!(Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_CLIENT))
        return Raise(State, "[LUI:Http] network.client grant is required");
    Lui::Http::Message Request;
    std::string Url;
    size_t Timeout = 10000, MaxBody = 1024 * 1024;
    if (Get) {
        size_t Length;
        const char* Text = luaL_checklstring(State, 2, &Length);
        if (Length > 8192) return Raise(State, "[LUI:Http] LimitExceeded");
        Url.assign(Text, Length);
        Request.Method = "GET";
    } else {
        luaL_checktype(State, 2, LUA_TTABLE);
        Url = HttpString(State, 2, "Url", "");
        Request.Method = HttpString(State, 2, "Method", "GET");
        Timeout = HttpNumber(State, 2, "TimeoutMs", 10000, 1, 60000);
        MaxBody = HttpNumber(State, 2, "MaxResponseBytes", 1024 * 1024, 0, 8 * 1024 * 1024);
        lua_getfield(State, 2, "Body");
        if (!lua_isnil(State, -1)) {
            size_t Length = 0;
            const char* Data = lua_type(State, -1) == LUA_TBUFFER
                ? static_cast<const char*>(lua_tobuffer(State, -1, &Length)) : luaL_checklstring(State, -1, &Length);
            if (Length > 1024 * 1024) return Raise(State, "[LUI:Http] LimitExceeded");
            Request.Body.assign(Data, Length);
        }
        lua_pop(State, 1);
        lua_getfield(State, 2, "Headers");
        if (!lua_isnil(State, -1)) {
            luaL_checktype(State, -1, LUA_TTABLE);
            int Headers = lua_gettop(State);
            int Count = lua_objlen(State, Headers);
            if (Count > 96) return Raise(State, "[LUI:Http] LimitExceeded");
            int Fields = 0;
            lua_pushnil(State);
            while (lua_next(State, Headers)) {
                double Key = lua_type(State, -2) == LUA_TNUMBER ? lua_tonumber(State, -2) : 0;
                if (!std::isfinite(Key) || std::floor(Key) != Key || Key < 1 || Key > Count || ++Fields > 96)
                    return Raise(State, "[LUI:Http] InvalidHeader: Headers must be a dense array");
                lua_pop(State, 1);
            }
            if (Fields != Count) return Raise(State, "[LUI:Http] InvalidHeader: Headers must be a dense array");
            for (int Index = 1; Index <= Count; ++Index) {
                lua_rawgeti(State, Headers, Index);
                luaL_checktype(State, -1, LUA_TTABLE);
                int Field = lua_gettop(State);
                auto Name = HttpString(State, Field, "Name", "");
                auto Value = HttpString(State, Field, "Value", "");
                if (Name.size() > 8192 || Value.size() > 8192) return Raise(State, "[LUI:Http] LimitExceeded");
                Request.Headers.push_back({std::move(Name), std::move(Value)});
                lua_pop(State, 1);
            }
        }
        lua_pop(State, 1);
    }
    Lui::Http::Url Destination;
    auto UrlError = Lui::Http::ParseUrl(Url, Destination);
    if (UrlError != Lui::Http::ErrorCode::None)
        return Raise(State, "[LUI:Http] %s", Lui::Http::GetErrorName(UrlError));
    bool LoopbackOnly = (Runtime->NetworkPolicyFlags & LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY) != 0;
    if (Runtime->ClientPortMin && (Destination.Port < Runtime->ClientPortMin || Destination.Port > Runtime->ClientPortMax))
        return Raise(State, "[LUI:Http] PolicyDenied");
    if (LoopbackOnly) {
        ErrorCode Error;
        auto Address = asio::ip::make_address(Destination.Host, Error);
        if ((!Error && !Address.is_loopback()) || (Error && Destination.Host != "localhost"))
            return Raise(State, "[LUI:Http] PolicyDenied");
    }
    Request.Target = Destination.Target;
    if (Request.Headers.empty()) Request.Headers.push_back({"Accept-Encoding", "identity"});
    else {
        bool Encoding = false;
        for (const auto& Header : Request.Headers) {
            std::string Name = Header.Name;
            for (char& Byte : Name) if (Byte >= 'A' && Byte <= 'Z') Byte += 'a' - 'A';
            if (Name == "accept-encoding") Encoding = true;
        }
        if (!Encoding) Request.Headers.push_back({"Accept-Encoding", "identity"});
    }
    std::string Wire;
    auto SerializeError = Lui::Http::SerializeRequest(Request, Destination.Authority, Wire);
    if (SerializeError != Lui::Http::ErrorCode::None)
        return Raise(State, "[LUI:Http] %s", Lui::Http::GetErrorName(SerializeError));
    CheckAwait(State);
    auto* Network = Context(Runtime);
    if (Network->HttpOutstanding >= 32) return Raise(State, "[LUI:Http] TooManyRequests");
    int Reference = BeginAwait(State);
    ++Network->HttpOutstanding;
    auto Operation = std::make_shared<HttpOperation>(Network, Reference, std::move(Wire), Request.Method, MaxBody);
    Operation->Sequence = ++Network->HttpSequence;
    Operation->MaxBody = MaxBody;
    asio::post(Network->Io, [Operation, Destination, LoopbackOnly, Timeout] {
        Operation->Network->HttpOperations.emplace(Operation->Reference, Operation);
        if (Operation->Sequence <= Operation->Network->HttpCancelThrough.load()) {
            FinishHttp(Operation, "[LUI:Http] Canceled");
            return;
        }
        Operation->Deadline.expires_after(std::chrono::milliseconds(Timeout));
        Operation->Deadline.async_wait([Operation](const ErrorCode& Error) {
            if (!Error) FinishHttp(Operation, "[LUI:Http] TimedOut");
        });
        Operation->Dial = std::make_shared<DialOperation>(Operation->Network->Io, Operation->Network->Runtime, 0);
        std::weak_ptr<HttpOperation> Weak = Operation;
        Operation->Dial->Completion = [Weak, Destination](std::shared_ptr<NetworkConnection> Connection, std::string Error) {
            auto Operation = Weak.lock();
            if (!Operation || Operation->Done) return;
            if (!Error.empty()) { FinishHttp(Operation, HttpError(std::move(Error))); return; }
            Operation->Connection = std::move(Connection);
            if (!Destination.Secure) { WriteHttp(Operation); return; }
            auto Settings = GetTlsSettings(Operation->Network, Error);
            if (!Settings) { FinishHttp(Operation, "[LUI:Http] TlsProviderUnavailable"); return; }
            Operation->Tls = std::make_shared<Lui::Tls::Stream>(Operation->Connection->Socket, Settings, false);
            if (!Operation->Tls->SetPeerName(Destination.Host)) {
                FinishHttp(Operation, "[LUI:Http] TlsHandshakeFailed"); return;
            }
            Operation->Tls->Socket.async_handshake(asio::ssl::stream_base::client,
                [Operation](const ErrorCode& Error) {
                    if (Operation->Done) return;
                    if (Error) { FinishHttp(Operation, "[LUI:Http] " + Operation->Tls->ErrorName(Error, true)); return; }
                    WriteHttp(Operation);
                });
        };
        BeginDial(Operation->Dial, Destination.Host, Destination.Port, LoopbackOnly, static_cast<unsigned>(Timeout));
    });
    return lua_yield(State, 0);
}

static int RequestAsync(lua_State* State) { return HttpRequest(State, false); }
static int GetAsync(lua_State* State) { return HttpRequest(State, true); }
static int CancelHttp(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    if (!(Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_CLIENT))
        return Raise(State, "[LUI:Http] network.client grant is required");
    if (auto* Network = Runtime->Network) {
        Network->HttpCancelThrough = Network->HttpSequence;
        if (!Network->HttpCancelPending.exchange(true)) asio::post(Network->Io, [Network] {
            Network->HttpCancelPending = false;
            const uint64_t Through = Network->HttpCancelThrough.load();
            // Coalesce floods while retaining later cancellation cutoffs. Starts after a
            // barrier check the cutoff too; requests submitted after the last call survive.
            auto Operations = Network->HttpOperations;
            for (const auto& Entry : Operations)
                if (Entry.second->Sequence <= Through) FinishHttp(Entry.second, "[LUI:Http] Canceled");
        });
    }
    return 0;
}

static void CloseListenerWorker(LuiRuntime* Runtime, const std::shared_ptr<NetworkListener>& Listener) {
    for (const auto& Acceptor : Listener->Acceptors) {
        ErrorCode Error;
        Acceptor->close(Error);
    }
    for (const auto& Connection : Listener->Ready) {
        ErrorCode Error;
        Connection->Socket.close(Error);
    }
    Listener->Ready.clear();
    if (Listener->WaiterReference) {
        int Reference = Listener->WaiterReference;
        Listener->WaiterReference = 0;
        Listener->AcceptPending = false;
        Queue(Runtime, {NetworkCompletion::Kind::None, Reference, {}, "[LUI:Network] Canceled", {}});
    }
}

static void StartAcceptWorker(LuiRuntime* Runtime, const std::shared_ptr<NetworkListener>& Listener,
    size_t Index) {
    if (!Listener->Open || Listener->AcceptActive[Index] || !Listener->WaiterReference) return;
    Listener->AcceptActive[Index] = true;
    auto Connection = std::make_shared<NetworkConnection>(Listener->Io);
    Listener->Acceptors[Index]->async_accept(Connection->Socket,
        [Runtime, Listener, Connection, Index](const ErrorCode& Error) {
            Listener->AcceptActive[Index] = false;
            if (!Listener->Open) return;
            if (Error) {
                Listener->Open = false;
                if (Listener->WaiterReference) {
                    int Reference = Listener->WaiterReference;
                    Listener->WaiterReference = 0;
                    Listener->AcceptPending = false;
                    Queue(Runtime, {NetworkCompletion::Kind::None, Reference, {}, NetworkError(Error), {}});
                }
                for (const auto& Acceptor : Listener->Acceptors) {
                    ErrorCode CloseError;
                    Acceptor->close(CloseError);
                }
                return;
            }
            CaptureEndpoints(Connection.get());
            if (Listener->WaiterReference) {
                int Reference = Listener->WaiterReference;
                Listener->WaiterReference = 0;
                Listener->AcceptPending = false;
                Queue(Runtime, {NetworkCompletion::Kind::Connection, Reference, {}, "", Connection});
            } else if (Listener->Ready.size() < 2) {
                Listener->Ready.push_back(Connection);
            } else {
                ErrorCode CloseError;
                Connection->Socket.close(CloseError);
            }
        });
}

static int AcceptAsync(lua_State* State) {
    auto Listener = GetListener(State);
    if (!Listener->Open) return Raise(State, "[LUI:Network] Closed");
    CheckAwait(State);
    auto* Runtime = GetRuntime(State);
    auto* Network = Context(Runtime);
    if (Listener->AcceptPending.exchange(true))
        return Raise(State, "[LUI:Network] ConcurrentAccept");
    int Reference = BeginAwait(State);
    asio::post(Network->Io, [Runtime, Reference, Listener] {
        if (!Listener->Open) {
            Listener->AcceptPending = false;
            Queue(Runtime, {NetworkCompletion::Kind::None, Reference, {}, "[LUI:Network] Canceled", {}});
            return;
        }
        if (!Listener->Ready.empty()) {
            auto Connection = Listener->Ready.front();
            Listener->Ready.pop_front();
            Listener->AcceptPending = false;
            Queue(Runtime, {NetworkCompletion::Kind::Connection, Reference, {}, "", Connection});
            return;
        }
        Listener->WaiterReference = Reference;
        for (size_t Index = 0; Index < Listener->Acceptors.size(); ++Index)
            StartAcceptWorker(Runtime, Listener, Index);
    });
    return lua_yield(State, 0);
}

static int ListenerClose(lua_State* State) {
    auto Listener = GetListener(State);
    if (Listener->Open.exchange(false)) {
        auto* Network = Context(GetRuntime(State));
        auto* Runtime = GetRuntime(State);
        asio::post(Network->Io, [Runtime, Listener] { CloseListenerWorker(Runtime, Listener); });
    }
    return 0;
}

static int Read(lua_State* State, bool Exact) {
    auto Connection = GetConnection(State);
    if (!Connection->Open) return Raise(State, "[LUI:Network] Closed");
    double Requested = lua_isnoneornil(State, 2) && !Exact ? 65536 : luaL_checknumber(State, 2);
    if (!std::isfinite(Requested) || std::floor(Requested) != Requested || Requested < 1 || Requested > 65536)
        return Raise(State, "[LUI:Network] read size must be between 1 and 65536");
    CheckAwait(State);
    auto Bytes = std::make_shared<std::vector<char>>(static_cast<size_t>(Requested));
    if (Connection->ReadPending.exchange(true)) return Raise(State, "[LUI:Network] ConcurrentRead");
    auto* Runtime = GetRuntime(State);
    int Reference = BeginAwait(State);
    auto Handler = [Runtime, Reference, Connection, Bytes, Exact](const ErrorCode& Error, size_t Count) {
        Connection->ReadPending = false;
        const bool WasOpen = Connection->Open.load();
        NetworkCompletion Completion;
        Completion.Reference = Reference;
        if (Error == asio::error::eof && !Exact && Count == 0) Completion.Type = NetworkCompletion::Kind::EndOfStream;
        else if (Error) Completion.Error = !WasOpen ? "[LUI:Network] Canceled" :
            (Error == asio::error::eof && Exact ? "[LUI:Network] UnexpectedEof" : NetworkError(Error));
        else {
            Completion.Type = NetworkCompletion::Kind::Bytes;
            Completion.Bytes.assign(Bytes->data(), Count);
        }
        Queue(Runtime, std::move(Completion));
        if (Error && WasOpen && QueueClosed(Runtime, Connection)) {
            ErrorCode CloseError;
            Connection->Socket.close(CloseError);
        }
    };
    auto* Network = Context(Runtime);
    asio::post(Network->Io, [Connection, Bytes, Exact, Handler = std::move(Handler)]() mutable {
        if (Exact) asio::async_read(Connection->Socket, asio::buffer(*Bytes), std::move(Handler));
        else Connection->Socket.async_read_some(asio::buffer(*Bytes), std::move(Handler));
    });
    return lua_yield(State, 0);
}

static int ReadAsync(lua_State* State) { return Read(State, false); }
static int ReadExactAsync(lua_State* State) { return Read(State, true); }

static void StartWrite(LuiRuntime* Runtime, std::shared_ptr<NetworkConnection> Connection) {
    if (Connection->Writing || Connection->Writes.empty()) return;
    Connection->Writing = true;
    auto Entry = Connection->Writes.front();
    asio::async_write(Connection->Socket, asio::buffer(*Entry.Bytes),
        [Runtime, Connection, Entry](const ErrorCode& Error, size_t) {
            Connection->QueuedWriteBytes -= Entry.Bytes->size();
            Connection->Writes.pop_front();
            Connection->Writing = false;
            Queue(Runtime, {NetworkCompletion::Kind::None, Entry.Reference, {}, Error
                ? (!Connection->Open ? "[LUI:Network] Canceled" : NetworkError(Error)) : "", {}});
            if (Error && QueueClosed(Runtime, Connection)) {
                ErrorCode CloseError;
                Connection->Socket.close(CloseError);
            }
            StartWrite(Runtime, Connection);
        });
}

static int WriteAsync(lua_State* State) {
    auto Connection = GetConnection(State);
    if (!Connection->Open) return Raise(State, "[LUI:Network] Closed");
    size_t Length = 0;
    const char* Source = nullptr;
    if (lua_type(State, 2) == LUA_TSTRING) Source = lua_tolstring(State, 2, &Length);
    else if (lua_type(State, 2) == LUA_TBUFFER) Source = static_cast<const char*>(lua_tobuffer(State, 2, &Length));
    else return Raise(State, "[LUI:Network] WriteAsync expects a string or buffer");
    if (Length > 256 * 1024) return Raise(State, "[LUI:Network] WriteTooLarge");
    CheckAwait(State);
    auto Bytes = std::make_shared<std::string>(Source, Length);
    size_t Current = Connection->QueuedWriteBytes.load();
    do {
        if (Current + Length > 256 * 1024) return Raise(State, "[LUI:Network] WriteQueueFull");
    } while (!Connection->QueuedWriteBytes.compare_exchange_weak(Current, Current + Length));
    auto* Runtime = GetRuntime(State);
    int Reference = BeginAwait(State);
    auto* Network = Context(Runtime);
    asio::post(Network->Io, [Runtime, Connection, Reference, Bytes] {
        Connection->Writes.push_back({Reference, Bytes});
        StartWrite(Runtime, Connection);
    });
    return lua_yield(State, 0);
}

static int ConnectionClose(lua_State* State) {
    auto Connection = GetConnection(State);
    auto* Runtime = GetRuntime(State);
    if (QueueClosed(Runtime, Connection)) {
        auto* Network = Context(Runtime);
        asio::post(Network->Io, [Connection] { ErrorCode Error; Connection->Socket.close(Error); });
    }
    return 0;
}

static int Shutdown(lua_State* State) {
    auto Connection = GetConnection(State);
    std::string Direction = luaL_checkstring(State, 2);
    Tcp::socket::shutdown_type Value;
    if (Direction == "Read") Value = Tcp::socket::shutdown_receive;
    else if (Direction == "Write") Value = Tcp::socket::shutdown_send;
    else if (Direction == "Both") Value = Tcp::socket::shutdown_both;
    else return Raise(State, "[LUI:Network] Shutdown direction must be Read, Write, or Both");
    if (!Connection->Open) return Raise(State, "[LUI:Network] Closed");
    auto* Network = Context(GetRuntime(State));
    asio::post(Network->Io, [Connection, Value] { ErrorCode Error; Connection->Socket.shutdown(Value, Error); });
    return 0;
}

struct NetworkSignalValue { int ConnectionId; };
struct NetworkSubscriptionValue { int ConnectionId; int ListenerId; };

static int NetworkSignalConnect(lua_State* State) {
    if (!HasMeta(State, 1, "LuiNetworkSignalMeta"))
        return Raise(State, "[LUI:Network] expected Closed signal");
    luaL_checktype(State, 2, LUA_TFUNCTION);
    auto* Runtime = GetRuntime(State);
    int ConnectionId = static_cast<NetworkSignalValue*>(lua_touserdata(State, 1))->ConnectionId;
    if (!Runtime->Network) return Raise(State, "[LUI:Network] Closed");
    auto Found = Runtime->Network->Connections.find(ConnectionId);
    if (Found == Runtime->Network->Connections.end() || !Found->second->Open)
        return Raise(State, "[LUI:Network] Closed");
    auto& Listeners = Found->second->ClosedListeners;
    Listeners.erase(std::remove_if(Listeners.begin(), Listeners.end(),
        [](const NetworkConnection::SignalListener& Listener) { return !Listener.Active; }),
        Listeners.end());
    if (Listeners.size() >= 64) return Raise(State, "[LUI:Network] TooManyListeners");
    int Reference = lua_ref(State, 2);
    int ListenerId = Runtime->Network->NextSignalId++;
    Listeners.push_back({ListenerId, Reference, true});
    *static_cast<NetworkSubscriptionValue*>(lua_newuserdata(State, sizeof(NetworkSubscriptionValue))) =
        {ConnectionId, ListenerId};
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiNetworkSubscriptionMeta");
    lua_setmetatable(State, -2);
    return 1;
}

static int NetworkSignalDisconnect(lua_State* State) {
    if (!HasMeta(State, 1, "LuiNetworkSubscriptionMeta"))
        return Raise(State, "[LUI:Network] expected signal connection");
    auto* Runtime = GetRuntime(State);
    if (!Runtime->Network) return 0;
    auto* Value = static_cast<NetworkSubscriptionValue*>(lua_touserdata(State, 1));
    auto Found = Runtime->Network->Connections.find(Value->ConnectionId);
    if (Found == Runtime->Network->Connections.end()) return 0;
    for (auto& Listener : Found->second->ClosedListeners) {
        if (Listener.Id == Value->ListenerId && Listener.Active) {
            Listener.Active = false;
            lua_unref(State, Listener.Reference);
            Listener.Reference = 0;
            break;
        }
    }
    return 0;
}

static int NetworkSignalIndex(lua_State* State) {
    const char* Key = luaL_checkstring(State, 2);
    if (std::strcmp(Key, "Connect") == 0)
        lua_pushcfunction(State, NetworkSignalConnect, "TcpConnection.Closed.Connect");
    else lua_pushnil(State);
    return 1;
}

static int NetworkSubscriptionIndex(lua_State* State) {
    const char* Key = luaL_checkstring(State, 2);
    if (std::strcmp(Key, "Disconnect") == 0)
        lua_pushcfunction(State, NetworkSignalDisconnect, "TcpConnection.Closed.Disconnect");
    else lua_pushnil(State);
    return 1;
}

static void ReleaseClosedListeners(LuiRuntime* Runtime,
    const std::shared_ptr<NetworkConnection>& Connection) {
    for (auto& Listener : Connection->ClosedListeners) {
        if (Listener.Active) lua_unref(Runtime->State, Listener.Reference);
        Listener.Active = false;
        Listener.Reference = 0;
    }
    Connection->ClosedListeners.clear();
}

static void FireClosedSignal(LuiRuntime* Runtime, const std::shared_ptr<NetworkConnection>& Connection) {
    const size_t Count = Connection->ClosedListeners.size();
    for (size_t Index = 0; Index < Count; ++Index) {
        auto& Listener = Connection->ClosedListeners[Index];
        if (!Listener.Active) continue;
        lua_getref(Runtime->State, Listener.Reference);
        if (!Runtime->VmDepth) Runtime->InterruptCount = 0;
        ++Runtime->VmDepth;
        int Status = lua_pcall(Runtime->State, 0, 0, 0);
        --Runtime->VmDepth;
        if (Status != LUA_OK) {
            const char* Message = lua_tostring(Runtime->State, -1);
            Runtime->LastError = std::string("[LUI:Network] Closed callback failed: ") +
                (Message ? Message : "unknown error");
            if (Runtime->LogCallback)
                Runtime->LogCallback(Runtime->LogContext, "Error", Runtime->LastError.c_str());
            else std::fprintf(stderr, "%s\n", Runtime->LastError.c_str());
            lua_pop(Runtime->State, 1);
        }
    }
    ReleaseClosedListeners(Runtime, Connection);
    if (Connection->GcPending && Runtime->Network)
        Runtime->Network->Connections.erase(Connection->Id);
}

static int ListenerIndex(lua_State* State) {
    auto Listener = GetListener(State);
    const char* Key = luaL_checkstring(State, 2);
    if (std::strcmp(Key, "Port") == 0) lua_pushinteger(State, Listener->Port);
    else if (std::strcmp(Key, "IsListening") == 0) lua_pushboolean(State, Listener->Open);
    else if (std::strcmp(Key, "BoundEndpoints") == 0) {
        lua_createtable(State, static_cast<int>(Listener->BoundEndpoints.size()), 0);
        int Index = 1;
        for (const auto& Endpoint : Listener->BoundEndpoints) {
            PushEndpoint(State, Endpoint.address().to_string(), Endpoint.port());
            lua_rawseti(State, -2, Index++);
        }
        lua_setreadonly(State, -1, true);
    }
    else if (std::strcmp(Key, "AcceptAsync") == 0) lua_pushcfunction(State, AcceptAsync, "TcpListener.AcceptAsync");
    else if (std::strcmp(Key, "Close") == 0) lua_pushcfunction(State, ListenerClose, "TcpListener.Close");
    else lua_pushnil(State);
    return 1;
}

static int ConnectionIndex(lua_State* State) {
    auto Connection = GetConnection(State);
    const char* Key = luaL_checkstring(State, 2);
    if (std::strcmp(Key, "IsOpen") == 0) lua_pushboolean(State, Connection->Open);
    else if (std::strcmp(Key, "LocalEndpoint") == 0)
        PushEndpoint(State, Connection->LocalAddress, Connection->LocalPort);
    else if (std::strcmp(Key, "RemoteEndpoint") == 0)
        PushEndpoint(State, Connection->RemoteAddress, Connection->RemotePort);
    else if (std::strcmp(Key, "Closed") == 0) {
        int ConnectionId = *static_cast<int*>(lua_touserdata(State, 1));
        *static_cast<NetworkSignalValue*>(lua_newuserdata(State, sizeof(NetworkSignalValue))) = {ConnectionId};
        lua_getfield(State, LUA_REGISTRYINDEX, "LuiNetworkSignalMeta");
        lua_setmetatable(State, -2);
    }
    else if (std::strcmp(Key, "ReadAsync") == 0) lua_pushcfunction(State, ReadAsync, "TcpConnection.ReadAsync");
    else if (std::strcmp(Key, "ReadExactAsync") == 0) lua_pushcfunction(State, ReadExactAsync, "TcpConnection.ReadExactAsync");
    else if (std::strcmp(Key, "WriteAsync") == 0) lua_pushcfunction(State, WriteAsync, "TcpConnection.WriteAsync");
    else if (std::strcmp(Key, "Shutdown") == 0) lua_pushcfunction(State, Shutdown, "TcpConnection.Shutdown");
    else if (std::strcmp(Key, "Close") == 0) lua_pushcfunction(State, ConnectionClose, "TcpConnection.Close");
    else lua_pushnil(State);
    return 1;
}

static int ReadOnly(lua_State* State) { return Raise(State, "[LUI:Network] network objects are read-only"); }

static int ListenerGc(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    if (!Runtime->Network) return 0;
    int Id = *static_cast<int*>(lua_touserdata(State, 1));
    auto Found = Runtime->Network->Listeners.find(Id);
    if (Found != Runtime->Network->Listeners.end()) {
        if (Found->second->Open.exchange(false)) {
            auto Listener = Found->second;
            asio::post(Runtime->Network->Io, [Runtime, Listener] { CloseListenerWorker(Runtime, Listener); });
        }
        Runtime->Network->Listeners.erase(Found);
    }
    return 0;
}

static int ConnectionGc(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    if (!Runtime->Network) return 0;
    int Id = *static_cast<int*>(lua_touserdata(State, 1));
    auto Found = Runtime->Network->Connections.find(Id);
    if (Found != Runtime->Network->Connections.end()) {
        auto Connection = Found->second;
        if (Connection->Open.exchange(false)) {
            ReleaseClosedListeners(Runtime, Connection);
            asio::post(Runtime->Network->Io, [Connection] { ErrorCode Error; Connection->Socket.close(Error); });
        }
        if (!Connection->ClosedListeners.empty()) Connection->GcPending = true;
        else Runtime->Network->Connections.erase(Found);
    }
    return 0;
}

static int MaterializeResult(lua_State* State);

// Uses the same private worker, capability policy, and scheduler completion bridge.
#include "Udp.inl"

void RegisterNetworkTypes(lua_State* State) {
    RegisterHostedTypes(State);
    RegisterUdpTypes(State);
    lua_pushcfunction(State, MaterializeResult, "NetworkResult");
    lua_setfield(State, LUA_REGISTRYINDEX, "LuiNetworkMaterialize");
    luaL_newmetatable(State, "LuiTcpListenerMeta");
    lua_pushcfunction(State, ListenerIndex, "TcpListener.__index"); lua_setfield(State, -2, "__index");
    lua_pushcfunction(State, ReadOnly, "TcpListener.__newindex"); lua_setfield(State, -2, "__newindex");
    lua_pushcfunction(State, ListenerGc, "TcpListener.__gc"); lua_setfield(State, -2, "__gc");
    lua_pop(State, 1);
    luaL_newmetatable(State, "LuiTcpConnectionMeta");
    lua_pushcfunction(State, ConnectionIndex, "TcpConnection.__index"); lua_setfield(State, -2, "__index");
    lua_pushcfunction(State, ReadOnly, "TcpConnection.__newindex"); lua_setfield(State, -2, "__newindex");
    lua_pushcfunction(State, ConnectionGc, "TcpConnection.__gc"); lua_setfield(State, -2, "__gc");
    lua_pop(State, 1);
    luaL_newmetatable(State, "LuiNetworkSignalMeta");
    lua_pushcfunction(State, NetworkSignalIndex, "TcpConnection.Closed.__index");
    lua_setfield(State, -2, "__index");
    lua_pop(State, 1);
    luaL_newmetatable(State, "LuiNetworkSubscriptionMeta");
    lua_pushcfunction(State, NetworkSubscriptionIndex, "TcpConnection.Closed.Subscription.__index");
    lua_setfield(State, -2, "__index");
    lua_pop(State, 1);
}

void PushNetworkService(lua_State* State) {
    lua_newtable(State);
    lua_pushcfunction(State, ListenTcp, "NetworkService.ListenTcp"); lua_setfield(State, -2, "ListenTcp");
    lua_pushcfunction(State, ConnectTcp, "NetworkService.ConnectTcp"); lua_setfield(State, -2, "ConnectTcp");
    lua_pushcfunction(State, BindUdp, "NetworkService.BindUdp"); lua_setfield(State, -2, "BindUdp");
    lua_setreadonly(State, -1, true);
}

void PushHttpService(lua_State* State) {
    lua_newtable(State);
    lua_pushcfunction(State, RequestAsync, "HttpService.RequestAsync"); lua_setfield(State, -2, "RequestAsync");
    lua_pushcfunction(State, GetAsync, "HttpService.GetAsync"); lua_setfield(State, -2, "GetAsync");
    lua_pushcfunction(State, CancelHttp, "HttpService.CancelAll"); lua_setfield(State, -2, "CancelAll");
    lua_setreadonly(State, -1, true);
}

static void PushHttpFields(lua_State* State, const std::vector<Lui::Http::Field>& Fields) {
    lua_createtable(State, static_cast<int>(Fields.size()), 0);
    int Index = 1;
    for (const auto& Field : Fields) {
        lua_createtable(State, 0, 2);
        lua_pushlstring(State, Field.Name.data(), Field.Name.size()); lua_setfield(State, -2, "Name");
        lua_pushlstring(State, Field.Value.data(), Field.Value.size()); lua_setfield(State, -2, "Value");
        lua_setreadonly(State, -1, true);
        lua_rawseti(State, -2, Index++);
    }
    lua_setreadonly(State, -1, true);
}

// Shares the private worker and TCP binding path; no backend or raw API exposure.
#include "http/Server.inl"

static int MaterializeResult(lua_State* State) {
    auto* Completion = static_cast<NetworkCompletion*>(lua_touserdata(State, 1));
    if (!Completion->Error.empty()) {
        lua_pushlstring(State, Completion->Error.data(), Completion->Error.size());
    } else if (Completion->Type == NetworkCompletion::Kind::HttpResponse) {
        const auto& Response = *Completion->HttpResponse;
        lua_createtable(State, 0, 6);
        lua_pushinteger(State, Response.StatusCode); lua_setfield(State, -2, "StatusCode");
        lua_pushboolean(State, Response.StatusCode >= 200 && Response.StatusCode < 300); lua_setfield(State, -2, "Success");
        lua_pushlstring(State, Response.Reason.data(), Response.Reason.size()); lua_setfield(State, -2, "StatusMessage");
        lua_pushlstring(State, Response.Body.data(), Response.Body.size()); lua_setfield(State, -2, "Body");
        PushHttpFields(State, Response.Headers); lua_setfield(State, -2, "Headers");
        PushHttpFields(State, Response.Trailers); lua_setfield(State, -2, "Trailers");
        lua_setreadonly(State, -1, true);
    } else if (Completion->Type == NetworkCompletion::Kind::Datagram) {
        lua_createtable(State, 0, 2);
        void* Buffer = lua_newbuffer(State, Completion->Bytes.size());
        std::memcpy(Buffer, Completion->Bytes.data(), Completion->Bytes.size());
        lua_setfield(State, -2, "Data");
        PushEndpoint(State, Completion->RemoteAddress, Completion->RemotePort);
        lua_setfield(State, -2, "RemoteEndpoint");
        lua_setreadonly(State, -1, true);
    } else if (Completion->Type == NetworkCompletion::Kind::Bytes) {
        void* Buffer = lua_newbuffer(State, Completion->Bytes.size());
        std::memcpy(Buffer, Completion->Bytes.data(), Completion->Bytes.size());
    } else if (Completion->Type == NetworkCompletion::Kind::EndOfStream) {
        lua_pushnil(State);
    } else if (Completion->Type == NetworkCompletion::Kind::Connection) {
        PushConnection(State, Completion->Connection);
    } else {
        return 0;
    }
    return 1;
}

int DrainNetworkCompletions(LuiRuntime* Runtime) {
    int Count = 0;
    while (Count < 64) {
        NetworkCompletion Completion;
        {
            std::lock_guard<std::mutex> Lock(Runtime->CompletionMutex);
            if (Runtime->NetworkCompletions.empty()) break;
            Completion = std::move(Runtime->NetworkCompletions.front());
            Runtime->NetworkCompletions.pop_front();
        }
        if (Completion.Type == NetworkCompletion::Kind::ClosedSignal) {
            FireClosedSignal(Runtime, Completion.Connection);
            ++Count;
            continue;
        }
        if (Completion.Type == NetworkCompletion::Kind::HostedRequest) {
            DispatchHosted(Runtime, Completion.Session);
            ++Count;
            continue;
        }
        if (Completion.Type == NetworkCompletion::Kind::HttpResponse && Runtime->Network->HttpOutstanding)
            --Runtime->Network->HttpOutstanding;
        if (Runtime->PendingAsyncReferences.erase(Completion.Reference) == 0) continue;
        lua_getref(Runtime->State, Completion.Reference);
        lua_State* Thread = lua_tothread(Runtime->State, -1);
        lua_pop(Runtime->State, 1);
        if (Thread) {
            if (Completion.Type == NetworkCompletion::Kind::Connection &&
                Runtime->Network->Connections.size() >= 64) {
                auto Connection = std::move(Completion.Connection);
                Connection->Open = false;
                asio::post(Runtime->Network->Io,
                    [Connection] { ErrorCode Error; Connection->Socket.close(Error); });
                Completion.Error = "[LUI:Network] TooManyConnections";
            }
            if (Completion.Type == NetworkCompletion::Kind::None && Completion.Error.empty()) {
                ResumeScheduledTask(Runtime, Completion.Reference, 0);
            } else if (!lua_checkstack(Thread, 1)) {
                Runtime->LastError = "[LUI:Network] OutOfMemory while resuming network task";
                if (Runtime->LogCallback) Runtime->LogCallback(Runtime->LogContext, "Error", Runtime->LastError.c_str());
                CompleteNetworkTask(Runtime, Completion.Reference, nullptr, LUA_ERRMEM);
                lua_unref(Runtime->State, Completion.Reference);
            } else {
                lua_getfield(Runtime->State, LUA_REGISTRYINDEX, "LuiNetworkMaterialize");
                lua_pushlightuserdata(Runtime->State, &Completion);
                const int MaterializeStatus = lua_pcall(Runtime->State, 1, 1, 0);
                lua_xmove(Runtime->State, Thread, 1);
                if (MaterializeStatus != LUA_OK || !Completion.Error.empty())
                    ResumeScheduledTask(Runtime, Completion.Reference, 0, true);
                else ResumeScheduledTask(Runtime, Completion.Reference, 1);
            }
        }
        ++Count;
    }
    return Count;
}

void CloseNetwork(LuiRuntime* Runtime) {
    if (!Runtime->Network) return;
    NetworkContext* Network = Runtime->Network;
    Network->Work.reset();
    Network->Io.stop();
    if (Network->Worker.joinable()) Network->Worker.join();
    Runtime->NetworkCompletions.clear();
    ReleaseHosted(Runtime);
    for (const auto& Pair : Network->Connections)
        ReleaseClosedListeners(Runtime, Pair.second);
    Runtime->Network = nullptr;
    for (int Reference : Runtime->PendingAsyncReferences) lua_unref(Runtime->State, Reference);
    Runtime->PendingAsyncReferences.clear();
    delete Network;
}
