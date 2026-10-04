// Included by Network.cpp to share private transport types and its single worker.
struct HostedServer {
    NetworkContext* Network = nullptr;
    int Id = 0;
    std::shared_ptr<NetworkListener> Listener;
    std::unordered_map<std::string, int> Routes; // Registry refs; scheduler owner only.
    std::atomic<bool> Open{true}, Started{false};
    std::atomic<size_t> Slots{0};
    size_t MaxConnections = 16, MaxBody = 1024 * 1024, MaxResponse = 1024 * 1024;
    unsigned TimeoutMs = 10000;
};

struct HostedSession {
    HostedSession(NetworkContext* Network, std::shared_ptr<HostedServer> Server,
        std::shared_ptr<NetworkConnection> Connection)
        : Network(Network), Server(std::move(Server)), Connection(std::move(Connection)), Deadline(Network->Io),
          Slots(Network->HostedSlots) { ++*Slots; ++this->Server->Slots; }
    ~HostedSession() { --*Slots; --Server->Slots; }
    NetworkContext* Network;
    std::shared_ptr<HostedServer> Server;
    std::shared_ptr<NetworkConnection> Connection;
    asio::steady_timer Deadline;
    std::shared_ptr<std::atomic<size_t>> Slots;
    uint64_t Id = 0;
    std::atomic<bool> Open{true};
    std::unique_ptr<Lui::Http::Parser> Parser;
    std::shared_ptr<Lui::Http::Message> Request; // Immutable until response written.
    std::array<char, 16384> Buffer;
    std::string Pending; // At most one read suffix; reads stop during dispatch/write.
    unsigned Requests = 0;
    bool Received = false, Waiting = false, Writing = false, CloseAfter = false;
};

static void CloseHosted(const std::shared_ptr<HostedSession>& Session) {
    if (!Session->Open.exchange(false)) return;
    Session->Deadline.cancel();
    ErrorCode Error;
    Session->Connection->Socket.close(Error);
    Session->Network->Sessions.erase(Session->Id);
}
static void BeginHostedRequest(const std::shared_ptr<HostedSession>& Session);
static void SendHosted(const std::shared_ptr<HostedSession>& Session, std::shared_ptr<std::string> Wire, bool Close) {
    if (!Session->Open || !Session->Server->Open || Session->Writing) return;
    Session->Writing = true;
    Session->Waiting = false;
    asio::async_write(Session->Connection->Socket, asio::buffer(*Wire),
        [Session, Wire, Close](const ErrorCode& Error, size_t) {
            Session->Writing = false;
            if (!Session->Open) return;
            if (Error || Close || !Session->Server->Open) { CloseHosted(Session); return; }
            Session->Request.reset();
            BeginHostedRequest(Session);
        });
}
static void HostedStatus(const std::shared_ptr<HostedSession>& Session, unsigned Code, const char* Reason,
    bool Close = true) {
    Lui::Http::Message Response;
    Response.StatusCode = Code;
    Response.Reason = Reason;
    Response.Body = Reason;
    Response.KeepAlive = !Close;
    auto Wire = std::make_shared<std::string>();
    const bool Head = Session->Request && Session->Request->Method == "HEAD";
    if (Lui::Http::SerializeResponse(Response, *Wire, Head) != Lui::Http::ErrorCode::None) {
        CloseHosted(Session); return;
    }
    SendHosted(Session, std::move(Wire), Close);
}
static void ReadHosted(const std::shared_ptr<HostedSession>& Session);
static void FeedHosted(const std::shared_ptr<HostedSession>& Session, std::string_view Bytes, bool Eof = false) {
    if (!Session->Open || !Session->Server->Open) { CloseHosted(Session); return; }
    Session->Received = Session->Received || !Bytes.empty();
    auto Parsed = Session->Parser->Feed(Bytes, Eof);
    if (Parsed.Status == Lui::Http::ParseStatus::Failed) {
        Session->CloseAfter = true;
        HostedStatus(Session, Parsed.Error == Lui::Http::ErrorCode::LimitExceeded ? 413 : 400,
            Parsed.Error == Lui::Http::ErrorCode::LimitExceeded ? "Payload Too Large" : "Bad Request");
        return;
    }
    if (Parsed.Status == Lui::Http::ParseStatus::Complete) {
        Session->Pending.assign(Bytes.substr(Parsed.Consumed));
        Session->Request = std::make_shared<Lui::Http::Message>(*Session->Parser->GetResult());
        Session->Parser.reset();
        Session->Waiting = true;
        Session->CloseAfter = Eof || !Session->Request->KeepAlive || ++Session->Requests >= 100;
        NetworkCompletion Completion;
        Completion.Type = NetworkCompletion::Kind::HostedRequest;
        Completion.Session = Session;
        Queue(Session->Network->Runtime, std::move(Completion));
        return;
    }
    ReadHosted(Session);
}
static void ReadHosted(const std::shared_ptr<HostedSession>& Session) {
    if (!Session->Open) return;
    Session->Connection->Socket.async_read_some(asio::buffer(Session->Buffer),
        [Session](const ErrorCode& Error, size_t Count) {
            if (!Session->Open) return;
            if (Error && Error != asio::error::eof) { CloseHosted(Session); return; }
            if (Error == asio::error::eof && !Session->Received && Count == 0) { CloseHosted(Session); return; }
            FeedHosted(Session, {Session->Buffer.data(), Count}, Error == asio::error::eof);
        });
}
static void BeginHostedRequest(const std::shared_ptr<HostedSession>& Session) {
    if (!Session->Open || !Session->Server->Open) { CloseHosted(Session); return; }
    Lui::Http::Limits Bounds;
    Bounds.BodyBytes = Session->Server->MaxBody;
    Session->Parser = std::make_unique<Lui::Http::Parser>(Lui::Http::MessageKind::Request, Bounds);
    Session->Received = false;
    Session->Deadline.expires_after(std::chrono::milliseconds(Session->Server->TimeoutMs));
    Session->Deadline.async_wait([Session](const ErrorCode& Error) {
        if (!Error) CloseHosted(Session); // Includes idle, parsing, scheduler wait, handler, and write.
    });
    std::string Pending = std::move(Session->Pending);
    Session->Pending.clear();
    if (!Pending.empty()) FeedHosted(Session, Pending);
    else ReadHosted(Session);
}
static void AcceptHosted(const std::shared_ptr<HostedServer>& Server, size_t Index) {
    if (!Server->Open || !Server->Started) return;
    auto Connection = std::make_shared<NetworkConnection>(Server->Network->Io);
    Server->Listener->Acceptors[Index]->async_accept(Connection->Socket,
        [Server, Connection, Index](const ErrorCode& Error) {
            if (!Server->Open) return;
            if (Error) {
                // Terminal accept failure closes the listener rather than spinning on an error.
                Server->Open = false;
                for (auto& Acceptor : Server->Listener->Acceptors) {
                    ErrorCode CloseError; Acceptor->close(CloseError);
                }
                auto Sessions = Server->Network->Sessions;
                for (const auto& Entry : Sessions) if (Entry.second->Server == Server) CloseHosted(Entry.second);
                return;
            }
            if (Server->Slots >= Server->MaxConnections || *Server->Network->HostedSlots >= 32) {
                ErrorCode CloseError; Connection->Socket.close(CloseError);
            } else {
                CaptureEndpoints(Connection.get());
                auto Session = std::make_shared<HostedSession>(Server->Network, Server, Connection);
                Session->Id = Server->Network->NextSession++;
                Server->Network->Sessions.emplace(Session->Id, Session);
                BeginHostedRequest(Session);
            }
            AcceptHosted(Server, Index);
        });
}

static std::shared_ptr<HostedServer> GetHostedServer(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    int Id = CheckedId(State, "LuiHttpServerMeta");
    if (!Runtime->Network) luaL_error(State, "[LUI:HttpServer] Closed");
    auto Found = Runtime->Network->Servers.find(Id);
    if (Found == Runtime->Network->Servers.end()) luaL_error(State, "[LUI:HttpServer] Closed");
    return Found->second;
}
static int CreateServer(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    if (!(Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_SERVER))
        return Raise(State, "[LUI:HttpServer] network.server grant is required");
    luaL_checktype(State, 2, LUA_TTABLE);
    auto* Network = Context(Runtime);
    if (Network->Servers.size() >= 4) return Raise(State, "[LUI:HttpServer] TooManyServers");
    auto Server = std::make_shared<HostedServer>();
    Server->Network = Network;
    Server->TimeoutMs = static_cast<unsigned>(HttpNumber(State, 2, "TimeoutMs", 10000, 1, 60000));
    Server->MaxConnections = HttpNumber(State, 2, "MaxConnections", 16, 1, 32);
    Server->MaxBody = HttpNumber(State, 2, "MaxRequestBytes", 1024 * 1024, 0, 8 * 1024 * 1024);
    Server->MaxResponse = HttpNumber(State, 2, "MaxResponseBytes", 1024 * 1024, 0, 8 * 1024 * 1024);
    BindTcp(State, false); // Same bind rollback, paired families, and host policy as TCP.
    Server->Id = *static_cast<int*>(lua_touserdata(State, -1));
    Server->Listener = Network->Listeners.at(Server->Id);
    Network->Listeners.erase(Server->Id);
    Network->Servers.emplace(Server->Id, Server);
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiHttpServerMeta");
    lua_setmetatable(State, -2);
    return 1;
}
static int ServerRoute(lua_State* State) {
    auto Server = GetHostedServer(State);
    if (!Server->Open || Server->Started) return Raise(State, "[LUI:HttpServer] routes require an unstarted server");
    size_t MethodLength, PathLength;
    const char* MethodData = luaL_checklstring(State, 2, &MethodLength);
    const char* PathData = luaL_checklstring(State, 3, &PathLength);
    if (MethodLength > 64 || PathLength > 8192) return Raise(State, "[LUI:HttpServer] LimitExceeded");
    std::string Method(MethodData, MethodLength), Path(PathData, PathLength);
    if (Path.find('?') != std::string::npos) return Raise(State, "[LUI:HttpServer] routes must exclude the query");
    Lui::Http::Message Probe; Probe.Method = Method; Probe.Target = Path;
    std::string Wire;
    if (Lui::Http::SerializeRequest(Probe, "localhost", Wire) != Lui::Http::ErrorCode::None)
        return Raise(State, "[LUI:HttpServer] InvalidRoute");
    luaL_checktype(State, 4, LUA_TFUNCTION);
    std::string Key = Method + " " + Path;
    if (Server->Routes.count(Key)) return Raise(State, "[LUI:HttpServer] DuplicateRoute");
    if (Server->Routes.size() >= 128) return Raise(State, "[LUI:HttpServer] TooManyRoutes");
    Server->Routes.emplace(std::move(Key), lua_ref(State, 4));
    return 0;
}
static int ServerStart(lua_State* State) {
    auto Server = GetHostedServer(State);
    if (!Server->Open) return Raise(State, "[LUI:HttpServer] Closed");
    if (!Server->Started.exchange(true)) asio::post(Server->Network->Io, [Server] {
        for (size_t Index = 0; Index < Server->Listener->Acceptors.size(); ++Index) AcceptHosted(Server, Index);
    });
    return 0;
}
static void ReleaseRoutes(LuiRuntime* Runtime, const std::shared_ptr<HostedServer>& Server) {
    for (const auto& Route : Server->Routes) lua_unref(Runtime->State, Route.second);
    Server->Routes.clear();
}
static void StopServer(LuiRuntime* Runtime, const std::shared_ptr<HostedServer>& Server) {
    if (Server->Open.exchange(false)) asio::post(Server->Network->Io, [Server] {
        for (auto& Acceptor : Server->Listener->Acceptors) { ErrorCode Error; Acceptor->close(Error); }
        auto Sessions = Server->Network->Sessions;
        for (const auto& Entry : Sessions) if (Entry.second->Server == Server) CloseHosted(Entry.second);
    });
    ReleaseRoutes(Runtime, Server);
}
static int ServerClose(lua_State* State) {
    auto Server = GetHostedServer(State);
    StopServer(GetRuntime(State), Server);
    return 0;
}
static int ServerGc(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    if (!Runtime->Network) return 0;
    int Id = *static_cast<int*>(lua_touserdata(State, 1));
    auto Found = Runtime->Network->Servers.find(Id);
    if (Found != Runtime->Network->Servers.end()) {
        StopServer(Runtime, Found->second);
        Runtime->Network->Servers.erase(Found);
    }
    return 0;
}
static int ServerIndex(lua_State* State) {
    auto Server = GetHostedServer(State);
    const char* Key = luaL_checkstring(State, 2);
    if (std::strcmp(Key, "Port") == 0) lua_pushinteger(State, Server->Listener->Port);
    else if (std::strcmp(Key, "IsListening") == 0) lua_pushboolean(State, Server->Open && Server->Started);
    else if (std::strcmp(Key, "BoundEndpoints") == 0) {
        lua_createtable(State, static_cast<int>(Server->Listener->BoundEndpoints.size()), 0);
        int Index = 1;
        for (const auto& Endpoint : Server->Listener->BoundEndpoints) {
            PushEndpoint(State, Endpoint.address().to_string(), Endpoint.port()); lua_rawseti(State, -2, Index++);
        }
        lua_setreadonly(State, -1, true);
    }
    else if (std::strcmp(Key, "Route") == 0) lua_pushcfunction(State, ServerRoute, "HttpServer.Route");
    else if (std::strcmp(Key, "Start") == 0) lua_pushcfunction(State, ServerStart, "HttpServer.Start");
    else if (std::strcmp(Key, "Close") == 0) lua_pushcfunction(State, ServerClose, "HttpServer.Close");
    else lua_pushnil(State);
    return 1;
}
void PushHttpServerService(lua_State* State) {
    lua_newtable(State);
    lua_pushcfunction(State, CreateServer, "HttpServerService.CreateServer"); lua_setfield(State, -2, "CreateServer");
    lua_setreadonly(State, -1, true);
}

static int BuildHostedTask(lua_State* State) {
    auto* Session = static_cast<HostedSession*>(lua_touserdata(State, 1));
    auto* Runtime = GetRuntime(State);
    const auto& Request = *Session->Request;
    auto Path = Request.Target.substr(0, Request.Target.find('?'));
    int Handler = Session->Server->Routes.at(Request.Method + " " + Path);
    lua_State* Thread = lua_newthread(State);
    if (Runtime->Sandboxed) luaL_sandboxthread(Thread);
    lua_getref(State, Handler); lua_xmove(State, Thread, 1);
    lua_createtable(State, 0, 9);
    lua_pushlstring(State, Request.Method.data(), Request.Method.size()); lua_setfield(State, -2, "Method");
    lua_pushlstring(State, Path.data(), Path.size()); lua_setfield(State, -2, "Path");
    lua_pushlstring(State, Request.Target.data(), Request.Target.size()); lua_setfield(State, -2, "RawTarget");
    lua_pushstring(State, "HTTP/1.1"); lua_setfield(State, -2, "HttpVersion");
    lua_pushlstring(State, Request.Body.data(), Request.Body.size()); lua_setfield(State, -2, "Body");
    PushHttpFields(State, Request.Headers); lua_setfield(State, -2, "Headers");
    PushHttpFields(State, Request.Trailers); lua_setfield(State, -2, "Trailers");
    PushEndpoint(State, Session->Connection->LocalAddress, Session->Connection->LocalPort); lua_setfield(State, -2, "LocalEndpoint");
    PushEndpoint(State, Session->Connection->RemoteAddress, Session->Connection->RemotePort); lua_setfield(State, -2, "RemoteEndpoint");
    lua_setreadonly(State, -1, true);
    lua_xmove(State, Thread, 1);
    int Reference = lua_ref(State, -1);
    lua_pushinteger(State, Reference);
    return 1;
}
struct HostedReply { HostedSession* Session; std::string Wire; };
static int BuildHostedReply(lua_State* State) {
    auto* Reply = static_cast<HostedReply*>(lua_touserdata(State, 1));
    luaL_checktype(State, 2, LUA_TTABLE);
    Lui::Http::Message Response;
    Response.StatusCode = static_cast<unsigned>(HttpNumber(State, 2, "StatusCode", 200, 200, 599));
    Response.Reason = HttpString(State, 2, "StatusMessage", "");
    Response.KeepAlive = !Reply->Session->CloseAfter;
    lua_getfield(State, 2, "Body");
    if (!lua_isnil(State, -1)) {
        size_t Length = 0;
        const char* Data = lua_type(State, -1) == LUA_TBUFFER
            ? static_cast<const char*>(lua_tobuffer(State, -1, &Length)) : luaL_checklstring(State, -1, &Length);
        if (Length > Reply->Session->Server->MaxResponse) return Raise(State, "[LUI:HttpServer] LimitExceeded");
        Response.Body.assign(Data, Length);
    }
    lua_pop(State, 1);
    lua_getfield(State, 2, "Headers");
    if (!lua_isnil(State, -1)) {
        luaL_checktype(State, -1, LUA_TTABLE);
        int Table = lua_gettop(State), Count = lua_objlen(State, Table), Fields = 0;
        if (Count > 96) return Raise(State, "[LUI:HttpServer] LimitExceeded");
        lua_pushnil(State);
        while (lua_next(State, Table)) {
            double Key = lua_type(State, -2) == LUA_TNUMBER ? lua_tonumber(State, -2) : 0;
            if (!std::isfinite(Key) || std::floor(Key) != Key || Key < 1 || Key > Count || ++Fields > 96)
                return Raise(State, "[LUI:HttpServer] InvalidHeader");
            lua_pop(State, 1);
        }
        if (Fields != Count) return Raise(State, "[LUI:HttpServer] InvalidHeader");
        for (int Index = 1; Index <= Count; ++Index) {
            lua_rawgeti(State, Table, Index); luaL_checktype(State, -1, LUA_TTABLE);
            int Field = lua_gettop(State);
            auto Name = HttpString(State, Field, "Name", "");
            auto Value = HttpString(State, Field, "Value", "");
            Response.Headers.push_back({std::move(Name), std::move(Value)}); lua_pop(State, 1);
        }
    }
    lua_pop(State, 1);
    Lui::Http::Limits Bounds; Bounds.BodyBytes = Reply->Session->Server->MaxResponse;
    auto Error = Lui::Http::SerializeResponse(Response, Reply->Wire, Reply->Session->Request->Method == "HEAD", Bounds);
    if (Error != Lui::Http::ErrorCode::None) return Raise(State, "[LUI:HttpServer] %s", Lui::Http::GetErrorName(Error));
    return 0;
}
static void RegisterHostedTypes(lua_State* State) {
    lua_pushcfunction(State, BuildHostedTask, "HttpServer.Dispatch"); lua_setfield(State, LUA_REGISTRYINDEX, "LuiHostedTask");
    lua_pushcfunction(State, BuildHostedReply, "HttpServer.Reply"); lua_setfield(State, LUA_REGISTRYINDEX, "LuiHostedReply");
    luaL_newmetatable(State, "LuiHttpServerMeta");
    lua_pushcfunction(State, ServerIndex, "HttpServer.__index"); lua_setfield(State, -2, "__index");
    lua_pushcfunction(State, ReadOnly, "HttpServer.__newindex"); lua_setfield(State, -2, "__newindex");
    lua_pushcfunction(State, ServerGc, "HttpServer.__gc"); lua_setfield(State, -2, "__gc");
    lua_pop(State, 1);
}
static void HostedFailure(LuiRuntime* Runtime, const std::shared_ptr<HostedSession>& Session, const char* Detail) {
    std::string Diagnostic = "[LUI:HttpServer] HandlerFailed: " + std::string(Detail ? Detail : "invalid response");
    if (Runtime->LogCallback) Runtime->LogCallback(Runtime->LogContext, "Error", Diagnostic.c_str());
    else std::fprintf(stderr, "%s\n", Diagnostic.c_str());
    asio::post(Session->Network->Io, [Session] { HostedStatus(Session, 500, "Internal Server Error"); });
}
static void DispatchHosted(LuiRuntime* Runtime, const std::shared_ptr<HostedSession>& Session) {
    if (!Session->Open || !Session->Server->Open) return;
    auto Path = Session->Request->Target.substr(0, Session->Request->Target.find('?'));
    if (!Session->Server->Routes.count(Session->Request->Method + " " + Path)) {
        asio::post(Session->Network->Io, [Session] { HostedStatus(Session, 404, "Not Found", Session->CloseAfter); });
        return;
    }
    lua_getfield(Runtime->State, LUA_REGISTRYINDEX, "LuiHostedTask");
    lua_pushlightuserdata(Runtime->State, Session.get());
    if (lua_pcall(Runtime->State, 1, 1, 0) != LUA_OK) {
        HostedFailure(Runtime, Session, lua_tostring(Runtime->State, -1)); lua_pop(Runtime->State, 1); return;
    }
    int Reference = static_cast<int>(lua_tointeger(Runtime->State, -1)); lua_pop(Runtime->State, 1);
    Runtime->Network->HandlerTasks.emplace(Reference, Session);
    Runtime->Tasks.push_back({Reference, std::chrono::steady_clock::now(), 1});
}
bool NetworkTaskReady(LuiRuntime* Runtime, int Reference) {
    if (!Runtime->Network) return true;
    auto Found = Runtime->Network->HandlerTasks.find(Reference);
    return Found == Runtime->Network->HandlerTasks.end() || (Found->second->Open && Found->second->Server->Open);
}
bool CompleteNetworkTask(LuiRuntime* Runtime, int Reference, lua_State* Thread, int Status) {
    if (!Runtime->Network) return false;
    auto Found = Runtime->Network->HandlerTasks.find(Reference);
    if (Found == Runtime->Network->HandlerTasks.end()) return false;
    auto Session = Found->second;
    Runtime->Network->HandlerTasks.erase(Found);
    if (!Session->Open || !Session->Server->Open) return true;
    if (Status != LUA_OK || !Thread || lua_gettop(Thread) != 1) {
        HostedFailure(Runtime, Session, Thread ? lua_tostring(Thread, -1) : "handler canceled");
        return true;
    }
    HostedReply Reply{Session.get(), {}};
    lua_getfield(Runtime->State, LUA_REGISTRYINDEX, "LuiHostedReply");
    lua_pushlightuserdata(Runtime->State, &Reply);
    lua_pushvalue(Thread, 1); lua_xmove(Thread, Runtime->State, 1);
    // Response table metamethods can execute application Luau. Preserve the same
    // reentrancy and interrupt accounting used by ordinary scheduler dispatch.
    if (!Runtime->VmDepth) Runtime->InterruptCount = 0;
    ++Runtime->VmDepth;
    int BuildStatus = lua_pcall(Runtime->State, 2, 0, 0);
    --Runtime->VmDepth;
    if (BuildStatus != LUA_OK) {
        HostedFailure(Runtime, Session, lua_tostring(Runtime->State, -1)); lua_pop(Runtime->State, 1);
    } else {
        auto Wire = std::make_shared<std::string>(std::move(Reply.Wire));
        asio::post(Session->Network->Io, [Session, Wire] { SendHosted(Session, Wire, Session->CloseAfter); });
    }
    return true;
}
static void ReleaseHosted(LuiRuntime* Runtime) {
    for (const auto& Entry : Runtime->Network->Servers) ReleaseRoutes(Runtime, Entry.second);
    Runtime->Network->HandlerTasks.clear(); // Task refs belong to Tasks/PendingAsyncReferences.
}
