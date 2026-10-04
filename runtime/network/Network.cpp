#include "../internal/Network.h"
#include "../internal/State.h"
#include "../internal/Scheduler.h"

#include "lua.h"
#include "lualib.h"
#include <asio.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
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
};

struct NetworkContext {
    explicit NetworkContext(LuiRuntime* Owner) : Runtime(Owner), Work(asio::make_work_guard(Io)),
        Worker([this] { Io.run(); }) {}
    LuiRuntime* Runtime;
    asio::io_context Io;
    asio::executor_work_guard<asio::io_context::executor_type> Work;
    std::thread Worker;
    int NextId = 1;
    std::unordered_map<int, std::shared_ptr<NetworkListener>> Listeners;
    std::unordered_map<int, std::shared_ptr<NetworkConnection>> Connections;
};

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
    return "[LUI:Network] IoError: " + Error.message();
}

static void Queue(LuiRuntime* Runtime, NetworkCompletion Completion) {
    std::lock_guard<std::mutex> Lock(Runtime->CompletionMutex);
    if (!Runtime->ShuttingDown) Runtime->NetworkCompletions.push_back(std::move(Completion));
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
    *static_cast<int*>(lua_newuserdata(State, sizeof(int))) = Id;
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiTcpConnectionMeta");
    lua_setmetatable(State, -2);
    Network->Connections.emplace(Id, Connection);
}

static int ListenTcp(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    if ((Runtime->GrantedCapabilities & (LUI_CAPABILITY_NETWORK_SERVER | LUI_CAPABILITY_NETWORK_RAW)) !=
        (LUI_CAPABILITY_NETWORK_SERVER | LUI_CAPABILITY_NETWORK_RAW))
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
    if (Network->Listeners.size() >= 16) return Raise(State, "[LUI:Network] TooManyListeners");
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
    auto Connection = std::make_shared<NetworkConnection>(Network->Io);
    auto Resolver = std::make_shared<Tcp::resolver>(Network->Io);
    auto Timer = std::make_shared<asio::steady_timer>(Network->Io);
    auto Done = std::make_shared<std::atomic<bool>>(false);
    int Reference = BeginAwait(State);
    asio::post(Network->Io, [Runtime, Reference, Resolver, Connection, Timer, Done, Host, Port, LoopbackOnly] {
    Timer->expires_after(std::chrono::seconds(10));
    Timer->async_wait([Runtime, Reference, Resolver, Connection, Done](const ErrorCode& Error) {
        if (Error || Done->exchange(true)) return;
        Resolver->cancel();
        ErrorCode CloseError;
        Connection->Socket.close(CloseError);
        Queue(Runtime, {NetworkCompletion::Kind::None, Reference, {}, "[LUI:Network] TimedOut", {}});
    });
    Resolver->async_resolve(Host, std::to_string(Port),
        [Runtime, Reference, Resolver, Connection, Timer, Done, LoopbackOnly](const ErrorCode& Error, Tcp::resolver::results_type Results) {
            if (Done->load()) return;
            if (Error) {
                if (!Done->exchange(true)) {
                    Timer->cancel();
                    Queue(Runtime, {NetworkCompletion::Kind::None, Reference, {}, NetworkError(Error), {}});
                }
                return;
            }
            std::vector<Tcp::endpoint> Endpoints;
            for (const auto& Result : Results) {
                if (!LoopbackOnly || Result.endpoint().address().is_loopback())
                    Endpoints.push_back(Result.endpoint());
            }
            if (Endpoints.empty()) {
                if (!Done->exchange(true)) {
                    Timer->cancel();
                    Queue(Runtime, {NetworkCompletion::Kind::None, Reference, {},
                        "[LUI:Network] PolicyDenied", {}});
                }
                return;
            }
            asio::async_connect(Connection->Socket, Endpoints,
                [Runtime, Reference, Connection, Timer, Done](const ErrorCode& ConnectError, const Tcp::endpoint&) {
                    if (Done->exchange(true)) return;
                    Timer->cancel();
                    if (!ConnectError) CaptureEndpoints(Connection.get());
                    Queue(Runtime, {ConnectError ? NetworkCompletion::Kind::None : NetworkCompletion::Kind::Connection,
                        Reference, {}, ConnectError ? NetworkError(ConnectError) : "", Connection});
                });
        });
    });
    return lua_yield(State, 0);
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
        NetworkCompletion Completion;
        Completion.Reference = Reference;
        if (Error == asio::error::eof && !Exact && Count == 0) Completion.Type = NetworkCompletion::Kind::EndOfStream;
        else if (Error) Completion.Error = !Connection->Open ? "[LUI:Network] Canceled" :
            (Error == asio::error::eof && Exact ? "[LUI:Network] UnexpectedEof" : NetworkError(Error));
        else {
            Completion.Type = NetworkCompletion::Kind::Bytes;
            Completion.Bytes.assign(Bytes->data(), Count);
        }
        Queue(Runtime, std::move(Completion));
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
    if (Connection->Open.exchange(false)) {
        auto* Network = Context(GetRuntime(State));
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
        if (Found->second->Open.exchange(false)) {
            auto Connection = Found->second;
            asio::post(Runtime->Network->Io, [Connection] { ErrorCode Error; Connection->Socket.close(Error); });
        }
        Runtime->Network->Connections.erase(Found);
    }
    return 0;
}

static int MaterializeResult(lua_State* State);

void RegisterNetworkTypes(lua_State* State) {
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
}

void PushNetworkService(lua_State* State) {
    lua_newtable(State);
    lua_pushcfunction(State, ListenTcp, "NetworkService.ListenTcp"); lua_setfield(State, -2, "ListenTcp");
    lua_pushcfunction(State, ConnectTcp, "NetworkService.ConnectTcp"); lua_setfield(State, -2, "ConnectTcp");
    lua_setreadonly(State, -1, true);
}

static int MaterializeResult(lua_State* State) {
    auto* Completion = static_cast<NetworkCompletion*>(lua_touserdata(State, 1));
    if (!Completion->Error.empty()) {
        lua_pushlstring(State, Completion->Error.data(), Completion->Error.size());
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
    Runtime->Network = nullptr;
    for (int Reference : Runtime->PendingAsyncReferences) lua_unref(Runtime->State, Reference);
    Runtime->PendingAsyncReferences.clear();
    delete Network;
}
