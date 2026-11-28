// Included by Network.cpp to share private transport types and its single worker.
struct HostedWebSocketPolicy {
    size_t MaxMessage=1048576,MaxFrame=1048576,MaxQueue=4194304,MaxMessages=16;
    unsigned CloseTimeout=5000,IdleTimeout=0,PingInterval=0,PongTimeout=10000;
    std::vector<std::string> Protocols,Origins;
};
struct HostedWebSocketRoute { std::shared_ptr<const HostedWebSocketPolicy> Policy; int Handler=0,Authorize=0; };
struct HostedServer {
    NetworkContext* Network = nullptr;
    int Id = 0;
    std::shared_ptr<NetworkListener> Listener;
    std::unordered_map<std::string, int> Routes; // Registry refs; scheduler owner only.
    std::unordered_map<std::string, HostedWebSocketRoute> WebSocketRoutes; // Owner only.
    std::shared_ptr<const std::unordered_map<std::string,std::shared_ptr<const HostedWebSocketPolicy>>> WebSocketPolicies; // Frozen before Start; no Lua refs.
    std::atomic<bool> Open{true}, Started{false};
    bool StopRequested=false,StopPosted=false,Collected=false; // Owner only; failed stop posts retry on the owner pump.
    std::atomic<size_t> Slots{0};
    size_t MaxConnections = 16, MaxBody = 1024 * 1024, MaxResponse = 1024 * 1024;
    unsigned TimeoutMs = 10000;
    std::shared_ptr<Lui::Tls::Settings> TlsSettings; // Immutable host credentials.
};

struct HostedSession {
    HostedSession(NetworkContext* Network, std::shared_ptr<HostedServer> Server,
        std::shared_ptr<NetworkConnection> Connection)
        : Network(Network), Server(std::move(Server)), Connection(std::move(Connection)), Deadline(Network->Io),
          Slots(Network->HostedSlots) {
        LocalAddress=this->Connection->LocalAddress;RemoteAddress=this->Connection->RemoteAddress;
        LocalPort=this->Connection->LocalPort;RemotePort=this->Connection->RemotePort;
        ++*Slots; ++this->Server->Slots;
    }
    ~HostedSession() { --*Slots; --Server->Slots; }
    NetworkContext* Network;
    std::shared_ptr<HostedServer> Server;
    std::shared_ptr<NetworkConnection> Connection;
    std::shared_ptr<Lui::Tls::Stream> Tls;
    asio::steady_timer Deadline;
    std::shared_ptr<std::atomic<size_t>> Slots;
    uint64_t Id = 0;
    std::atomic<bool> Open{true};
    std::unique_ptr<Lui::Http::Parser> Parser;
    std::shared_ptr<Lui::Http::Message> Request; // Immutable until response written.
    std::array<char, 16384> Buffer;
    std::string Pending; // At most one read suffix; reads stop during dispatch/write.
    enum class Phase { Http,Authorizing,Committing,Upgraded,Closed };
    std::atomic<Phase> State{Phase::Http};
    std::weak_ptr<WebSocketConnection> Upgrade;
    std::shared_ptr<const HostedWebSocketPolicy> UpgradePolicy;
    std::string UpgradeWire,SelectedProtocol,LocalAddress,RemoteAddress;
    unsigned short LocalPort=0,RemotePort=0;
    unsigned Requests = 0;
    bool Received = false, Waiting = false, Writing = false, CloseAfter = false;
};

static void CloseHosted(const std::shared_ptr<HostedSession>& Session) noexcept {
    if(auto Socket=Session->Upgrade.lock()) {
        try { FinishWebSocket(Socket,"[LUI:WebSocket] Canceled"); }
        catch(...) { AbortWebSocket(Socket,1); }
        Session->State=HostedSession::Phase::Closed;Session->Network->Sessions.erase(Session->Id);return;
    }
    if (!Session->Open.exchange(false)) return;
    Session->State=HostedSession::Phase::Closed;
    try { Session->Deadline.cancel(); } catch(...) {}
    ErrorCode Error;
    if(Session->Connection)Session->Connection->Socket.close(Error);
    Session->Network->Sessions.erase(Session->Id);
}
template<typename Function>
static auto HostedGuard(std::shared_ptr<HostedSession> Session, Function Callback) {
    return [Session=std::move(Session),Callback=std::move(Callback)](auto&&... Arguments) mutable {
        try { Callback(std::forward<decltype(Arguments)>(Arguments)...); }
        catch(const std::exception&) { CloseHosted(Session); }
    };
}
static void AbortHostedServer(const std::shared_ptr<HostedServer>& Server) noexcept {
    Server->Open=false;
    for(auto& Acceptor:Server->Listener->Acceptors){ErrorCode Error;Acceptor->close(Error);}
    auto& Sessions=Server->Network->Sessions;
    for(auto Iterator=Sessions.begin();Iterator!=Sessions.end();) {
        auto Session=(Iterator++)->second;
        if(Session->Server==Server)CloseHosted(Session);
    }
}
template<typename Function>
static auto HostedServerGuard(std::shared_ptr<HostedServer> Server,Function Callback) {
    return [Server=std::move(Server),Callback=std::move(Callback)](auto&&... Arguments) mutable {
        try{Callback(std::forward<decltype(Arguments)>(Arguments)...);}
        catch(const std::exception&){AbortHostedServer(Server);}
    };
}
static void BeginHostedRequest(const std::shared_ptr<HostedSession>& Session);
static void SendHosted(const std::shared_ptr<HostedSession>& Session, std::shared_ptr<std::string> Wire, bool Close) {
    if (!Session->Open || !Session->Server->Open || Session->Writing) return;
    Session->Writing = true;
    Session->Waiting = false;
    auto Handler = HostedGuard(Session,[Session, Wire, Close](const ErrorCode& Error, size_t) {
            Session->Writing = false;
            if (!Session->Open) return;
            if (Error || !Session->Server->Open) { CloseHosted(Session); return; }
            if (Close) {
                if (Session->Tls) Session->Tls->Socket.async_shutdown([Session](const ErrorCode&) { CloseHosted(Session); });
                else CloseHosted(Session);
                return;
            }
            Session->Request.reset();
            BeginHostedRequest(Session);
        });
    if (Session->Tls) asio::async_write(Session->Tls->Socket, asio::buffer(*Wire), std::move(Handler));
    else asio::async_write(Session->Connection->Socket, asio::buffer(*Wire), std::move(Handler));
}
static void HostedStatus(const std::shared_ptr<HostedSession>& Session, unsigned Code, const char* Reason,
    bool Close = true) {
    Lui::Http::Message Response;
    Response.StatusCode = Code;
    Response.Reason = Reason;
    Response.Body = Reason;
    if(Code==426)Response.Headers.push_back({"Sec-WebSocket-Version","13"});
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
        if (Session->Request->WebSocketUpgrade) {
            if(Eof || Session->Requests>=99){HostedStatus(Session,400,"Bad Request");return;}
            auto Path=Session->Request->Target.substr(0,Session->Request->Target.find('?'));
            const auto& Policies=Session->Server->WebSocketPolicies;
            if(!Policies || !Policies->count(Path)){HostedStatus(Session,400,"Bad Request");return;}
            Session->UpgradePolicy=Policies->at(Path);
            std::string Origin;
            auto Error=Lui::WebSocket::BuildServerHandshake(*Session->Request,Session->UpgradePolicy->Protocols,
                Session->UpgradeWire,Session->SelectedProtocol,Origin);
            if(Error==Lui::WebSocket::Error::UnsupportedVersion){HostedStatus(Session,426,"Upgrade Required");return;}
            if(Error!=Lui::WebSocket::Error::None){HostedStatus(Session,400,"Bad Request");return;}
            if(!Origin.empty() && std::find(Session->UpgradePolicy->Origins.begin(),Session->UpgradePolicy->Origins.end(),Origin)==Session->UpgradePolicy->Origins.end()) {
                HostedStatus(Session,403,"Forbidden");return;
            }
            Session->State=HostedSession::Phase::Authorizing;
        }
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
    auto Handler = HostedGuard(Session,[Session](const ErrorCode& Error, size_t Count) {
            if (!Session->Open) return;
            if (Error && Error != asio::error::eof) { CloseHosted(Session); return; }
            if (Error == asio::error::eof && !Session->Received && Count == 0) { CloseHosted(Session); return; }
            FeedHosted(Session, {Session->Buffer.data(), Count}, Error == asio::error::eof);
        });
    if (Session->Tls) Session->Tls->Socket.async_read_some(asio::buffer(Session->Buffer), std::move(Handler));
    else Session->Connection->Socket.async_read_some(asio::buffer(Session->Buffer), std::move(Handler));
}
static void BeginHostedRequest(const std::shared_ptr<HostedSession>& Session) {
    if (!Session->Open || !Session->Server->Open) { CloseHosted(Session); return; }
    Lui::Http::Limits Bounds;
    Bounds.BodyBytes = Session->Server->MaxBody;
    Session->Parser = std::make_unique<Lui::Http::Parser>(Lui::Http::MessageKind::Request, Bounds, "GET", bool(Session->Server->WebSocketPolicies));
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
        HostedServerGuard(Server,[Server, Connection, Index](const ErrorCode& Error) {
            if (!Server->Open) return;
            if (Error) {
                // Terminal accept failure closes the listener rather than spinning on an error.
                AbortHostedServer(Server);
                return;
            }
            if (Server->Slots >= Server->MaxConnections || *Server->Network->HostedSlots >= 32) {
                // No request/response was admitted. Abort rather than leaving a graceful
                // disconnect in the background while an excess peer is still writing.
                ErrorCode CloseError;
                Connection->Socket.set_option(asio::socket_base::linger(true, 0), CloseError);
                Connection->Socket.close(CloseError);
            } else {
                CaptureEndpoints(Connection.get());
                auto Session = std::make_shared<HostedSession>(Server->Network, Server, Connection);
                Session->Id = Server->Network->NextSession++;
                Server->Network->Sessions.emplace(Session->Id, Session);
                if (Server->TlsSettings) {
                    Session->Tls = std::make_shared<Lui::Tls::Stream>(Connection->Socket, Server->TlsSettings, true);
                    Session->Deadline.expires_after(std::chrono::milliseconds(Server->TimeoutMs));
                    Session->Deadline.async_wait([Session](const ErrorCode& Error) { if (!Error) CloseHosted(Session); });
                    Session->Tls->Socket.async_handshake(asio::ssl::stream_base::server, HostedGuard(Session,[Session](const ErrorCode& Error) {
                        if (!Session->Open) return;
                        if (Error || !Session->Server->Open) { CloseHosted(Session); return; }
                        BeginHostedRequest(Session);
                    }));
                } else BeginHostedRequest(Session);
            }
            AcceptHosted(Server, Index);
        }));
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
    CheckModuleEffect(State);
    auto* Runtime = GetRuntime(State);
    if (!(Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_SERVER))
        return Raise(State, "[LUI:Capabilities] NetworkServerRequired");
    luaL_checktype(State, 2, LUA_TTABLE);
    auto* Network = Context(Runtime);
    if (Network->Servers.size() >= 4) return Raise(State, "[LUI:HttpServer] TooManyServers");
    auto Server = std::make_shared<HostedServer>();
    Server->Network = Network;
    Server->TimeoutMs = static_cast<unsigned>(HttpNumber(State, 2, "TimeoutMs", 10000, 1, 60000));
    Server->MaxConnections = HttpNumber(State, 2, "MaxConnections", 16, 1, 32);
    Server->MaxBody = HttpNumber(State, 2, "MaxRequestBytes", 1024 * 1024, 0, 8 * 1024 * 1024);
    Server->MaxResponse = HttpNumber(State, 2, "MaxResponseBytes", 1024 * 1024, 0, 8 * 1024 * 1024);
    lua_getfield(State, 2, "TLS");
    if (!lua_isnil(State, -1)) luaL_checktype(State, -1, LUA_TBOOLEAN);
    const bool Secure = lua_toboolean(State, -1) != 0;
    lua_pop(State, 1);
    if (Secure) {
        Server->TlsSettings = Runtime->TlsSettings;
        if (!Server->TlsSettings || !Server->TlsSettings->Server)
            return Raise(State, "[LUI:HttpServer] TlsCredentialRequired");
    }
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
    CheckModuleEffect(State);
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
    if (Server->Routes.count(Key) || (Method=="GET" && Server->WebSocketRoutes.count(Path))) return Raise(State, "[LUI:HttpServer] DuplicateRoute");
    if (Server->Routes.size()+Server->WebSocketRoutes.size() >= 128) return Raise(State, "[LUI:HttpServer] TooManyRoutes");
    Server->Routes.emplace(std::move(Key), lua_ref(State, 4));
    return 0;
}
static int ReferenceHostedRoute(lua_State* State) {
    auto* Route=static_cast<HostedWebSocketRoute*>(lua_touserdata(State,1));
    Route->Handler=lua_ref(State,2);
    if(!lua_isnil(State,3))Route->Authorize=lua_ref(State,3);
    return 0;
}
static int ServerRouteWebSocketImpl(lua_State* State) {
    CheckModuleEffect(State);auto Server=GetHostedServer(State);
    if(!Server->Open || Server->Started)return Raise(State,"[LUI:HttpServer] Closed");
    size_t Length=0;const char* Text=luaL_checklstring(State,2,&Length);
    if(Length>8192)return Raise(State,"[LUI:HttpServer] InvalidRoute");
    std::string Path(Text,Length),Wire;Lui::Http::Message Probe;Probe.Method="GET";Probe.Target=Path;
    if(Path.find('?')!=std::string::npos || Lui::Http::SerializeRequest(Probe,"localhost",Wire)!=Lui::Http::ErrorCode::None)return Raise(State,"[LUI:HttpServer] InvalidRoute");
    luaL_checktype(State,3,LUA_TFUNCTION);
    auto Limits=GetWebSocketState(GetRuntime(State));auto Policy=std::make_shared<HostedWebSocketPolicy>();
    Policy->MaxMessage=Limits->MaxMessageBytes;Policy->MaxFrame=Limits->MaxFrameBytes;Policy->MaxQueue=Limits->MaxQueuedBytes;Policy->MaxMessages=Limits->MaxQueuedMessages;
    int Authorize=0;
    if(!lua_isnoneornil(State,4)) {
        luaL_checktype(State,4,LUA_TTABLE);
        Policy->MaxMessage=WebSocketNumber(State,4,"MaxMessageBytes",Policy->MaxMessage,1,Limits->MaxMessageBytes);
        Policy->MaxFrame=WebSocketNumber(State,4,"MaxFrameBytes",std::min(Policy->MaxFrame,Policy->MaxMessage),1,std::min(Policy->MaxMessage,size_t(Limits->MaxFrameBytes)));
        Policy->MaxQueue=WebSocketNumber(State,4,"MaxQueuedBytes",Policy->MaxQueue,1,Limits->MaxQueuedBytes);
        Policy->MaxMessages=WebSocketNumber(State,4,"MaxQueuedMessages",Policy->MaxMessages,1,Limits->MaxQueuedMessages);
        Policy->CloseTimeout=unsigned(WebSocketNumber(State,4,"CloseTimeoutMs",5000,1,10000));
        Policy->IdleTimeout=unsigned(WebSocketNumber(State,4,"IdleTimeoutMs",0,0,600000));
        Policy->PingInterval=unsigned(WebSocketNumber(State,4,"PingIntervalMs",0,0,600000));
        Policy->PongTimeout=unsigned(WebSocketNumber(State,4,"PongTimeoutMs",10000,1,60000));
        for(const char* Key:{"Protocols","Origins"}) {
            lua_getfield(State,4,Key);
            if(!lua_isnil(State,-1)) {
                luaL_checktype(State,-1,LUA_TTABLE);int Table=lua_gettop(State),Count=WebSocketDenseArray(State,Table,16);
                auto& Values=std::strcmp(Key,"Protocols")==0?Policy->Protocols:Policy->Origins;
                for(int Index=1;Index<=Count;++Index) {
                    lua_rawgeti(State,Table,Index);size_t Bytes=0;auto Value=luaL_checklstring(State,-1,&Bytes);
                    if(Bytes>(std::strcmp(Key,"Protocols")==0?128u:2048u))return Raise(State,"[LUI:WebSocket] InvalidOptions");
                    std::string Item(Value,Bytes);
                    if(std::strcmp(Key,"Protocols")==0) {if(!Lui::WebSocket::ValidProtocol(Item))return Raise(State,"[LUI:WebSocket] InvalidOptions");}
                    else {Lui::Http::Url Origin;if(Bytes>2048 || Lui::Http::ParseUrl(Item,Origin)!=Lui::Http::ErrorCode::None || Item!=std::string(Origin.Secure?"https://":"http://")+Origin.Authority)return Raise(State,"[LUI:WebSocket] InvalidOptions");}
                    if(std::find(Values.begin(),Values.end(),Item)!=Values.end())return Raise(State,"[LUI:WebSocket] InvalidOptions");
                    Values.push_back(std::move(Item));lua_pop(State,1);
                }
            }
            lua_pop(State,1);
        }
        lua_getfield(State,4,"Authorize");if(!lua_isnil(State,-1))luaL_checktype(State,-1,LUA_TFUNCTION);
        Authorize=lua_gettop(State);
    }
    if(!Server->Open || Server->Started)return Raise(State,"[LUI:HttpServer] Closed");
    if(Server->Routes.count("GET "+Path)||Server->WebSocketRoutes.count(Path))return Raise(State,"[LUI:HttpServer] DuplicateRoute");
    if(Server->Routes.size()+Server->WebSocketRoutes.size()>=128)return Raise(State,"[LUI:HttpServer] TooManyRoutes");
    HostedWebSocketRoute Route{Policy,0,0};
    lua_pushcfunction(State,ReferenceHostedRoute,"HttpServer.RouteReferences");lua_pushlightuserdata(State,&Route);lua_pushvalue(State,3);
    if(Authorize)lua_pushvalue(State,Authorize);else lua_pushnil(State);
    if(lua_pcall(State,3,0,0)!=LUA_OK) {
        if(Route.Handler)lua_unref(State,Route.Handler);if(Route.Authorize)lua_unref(State,Route.Authorize);
        lua_error(State);
    }
    try {Server->WebSocketRoutes.emplace(std::move(Path),std::move(Route));}
    catch(...) {lua_unref(State,Route.Handler);if(Route.Authorize)lua_unref(State,Route.Authorize);throw;}
    return 0;
}
static int ServerRouteWebSocket(lua_State* State) {
    try{return ServerRouteWebSocketImpl(State);}catch(const std::bad_alloc&){return Raise(State,"[LUI:WebSocket] ResourceExhausted");}
}
static int ServerStart(lua_State* State) {
    CheckModuleEffect(State);
    auto Server = GetHostedServer(State);
    if (!Server->Open) return Raise(State, "[LUI:HttpServer] Closed");
    if (!Server->Started) {
        try {
            if(!Server->WebSocketRoutes.empty()) {
                auto Policies=std::make_shared<std::unordered_map<std::string,std::shared_ptr<const HostedWebSocketPolicy>>>();
                for(const auto& Route:Server->WebSocketRoutes)Policies->emplace(Route.first,Route.second.Policy);
                Server->WebSocketPolicies=std::move(Policies);
            }
            Server->Started=true;
            asio::post(Server->Network->Io, HostedServerGuard(Server,[Server] {for(size_t Index=0;Index<Server->Listener->Acceptors.size();++Index)AcceptHosted(Server,Index);}));
        }catch(const std::bad_alloc&){Server->Started=false;return Raise(State,"[LUI:HttpServer] ResourceExhausted");}
    }
    return 0;
}
static void ReleaseRoutes(LuiRuntime* Runtime, const std::shared_ptr<HostedServer>& Server) {
    for (const auto& Route : Server->Routes) lua_unref(Runtime->State, Route.second);
    Server->Routes.clear();
    for(const auto& Route:Server->WebSocketRoutes){lua_unref(Runtime->State,Route.second.Handler);if(Route.second.Authorize)lua_unref(Runtime->State,Route.second.Authorize);}
    Server->WebSocketRoutes.clear();
}
static void PostServerStop(const std::shared_ptr<HostedServer>& Server) noexcept {
    if(!Server->StopRequested || Server->StopPosted)return;
    try {
        if(Server->Network->Runtime->WebSocketState)CheckWebSocketFault(Server->Network->Runtime->WebSocketState,12);
        asio::post(Server->Network->Io,[Server]{AbortHostedServer(Server);});Server->StopPosted=true;
    }catch(const std::bad_alloc&) {} // The owner root remains until a pump or repeated Close can retry.
}
static void DrainHostedStops(LuiRuntime* Runtime) {
    if(!Runtime->Network)return;
    auto& Servers=Runtime->Network->Servers;
    for(auto Iterator=Servers.begin();Iterator!=Servers.end();) {
        auto Server=(Iterator++)->second;PostServerStop(Server);
        if(Server->Collected && Server->StopPosted)Servers.erase(Server->Id);
    }
}
static void StopServer(LuiRuntime* Runtime, const std::shared_ptr<HostedServer>& Server) {
    Server->Open=false;Server->StopRequested=true;PostServerStop(Server);
    ReleaseRoutes(Runtime, Server);
}
static int ServerClose(lua_State* State) {
    CheckModuleEffect(State);
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
        Found->second->Collected=true;
        if(Found->second->StopPosted)Runtime->Network->Servers.erase(Found);
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
    else if (std::strcmp(Key, "RouteWebSocket") == 0) lua_pushcfunction(State, ServerRouteWebSocket, "HttpServer.RouteWebSocket");
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

static int BuildHostedTaskImpl(lua_State* State) {
    auto* Session = static_cast<HostedSession*>(lua_touserdata(State, 1));
    auto* Runtime = GetRuntime(State);
    const auto& Request = *Session->Request;
    auto Path = Request.Target.substr(0, Request.Target.find('?'));
    int Handler = Request.WebSocketUpgrade ? Session->Server->WebSocketRoutes.at(Path).Authorize : Session->Server->Routes.at(Request.Method + " " + Path);
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
    PushEndpoint(State, Session->LocalAddress, Session->LocalPort); lua_setfield(State, -2, "LocalEndpoint");
    PushEndpoint(State, Session->RemoteAddress, Session->RemotePort); lua_setfield(State, -2, "RemoteEndpoint");
    lua_setreadonly(State, -1, true);
    lua_xmove(State, Thread, 1);
    int Reference = lua_ref(State, -1);
    lua_pushinteger(State, Reference);
    return 1;
}
static int BuildHostedTask(lua_State* State) {
    try{return BuildHostedTaskImpl(State);}catch(const std::bad_alloc&){return Raise(State,"[LUI:HttpServer] ResourceExhausted");}
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
static int BuildHostedWebSocketTask(lua_State* State);
static void RegisterHostedTypes(lua_State* State) {
    lua_pushcfunction(State, BuildHostedTask, "HttpServer.Dispatch"); lua_setfield(State, LUA_REGISTRYINDEX, "LuiHostedTask");
    lua_pushcfunction(State, BuildHostedReply, "HttpServer.Reply"); lua_setfield(State, LUA_REGISTRYINDEX, "LuiHostedReply");
    lua_pushcfunction(State,BuildHostedWebSocketTask,"HttpServer.WebSocketHandler");lua_setfield(State,LUA_REGISTRYINDEX,"LuiHostedWebSocketTask");
    luaL_newmetatable(State, "LuiHttpServerMeta");
    lua_pushcfunction(State, ServerIndex, "HttpServer.__index"); lua_setfield(State, -2, "__index");
    lua_pushcfunction(State, ReadOnly, "HttpServer.__newindex"); lua_setfield(State, -2, "__newindex");
    lua_pushcfunction(State, ServerGc, "HttpServer.__gc"); lua_setfield(State, -2, "__gc");
    lua_pop(State, 1);
}
static void HostedFailure(LuiRuntime* Runtime, const std::shared_ptr<HostedSession>& Session, const char* Detail) noexcept {
    try {
        std::string Diagnostic = "[LUI:HttpServer] HandlerFailed: " + std::string(Detail ? Detail : "invalid response");
        if (Runtime->LogCallback) Runtime->LogCallback(Runtime->LogContext, "Error", Diagnostic.c_str());
        else std::fprintf(stderr, "%s\n", Diagnostic.c_str());
    }catch(...){}
    try {asio::post(Session->Network->Io,HostedGuard(Session,[Session] { HostedStatus(Session, 500, "Internal Server Error"); }));}
    catch(const std::exception&){} // Keep the existing bounded request deadline authoritative.
}
static void PrepareHostedWebSocket(LuiRuntime* Runtime,const std::shared_ptr<HostedSession>& Session) {
    std::shared_ptr<WebSocketConnection> Socket;
    try {
        auto Limits=GetWebSocketState(Runtime);const auto& Policy=*Session->UpgradePolicy;
        const uint64_t Reserved=3ull*Policy.MaxQueue+2ull*Policy.MaxMessage+512*1024;
        if(Limits->LiveConnections>=Limits->MaxConnections || Reserved>Limits->MaxReservedBytes || Limits->ReservedBytes>Limits->MaxReservedBytes-Reserved ||
            Session->Network->NextId<=0 || Session->Network->NextId==2147483647) {
        ++Limits->Rejected;asio::post(Session->Network->Io,HostedGuard(Session,[Session]{HostedStatus(Session,503,"Service Unavailable");}));return;
        }
        CheckWebSocketFault(Limits,8);
        Socket=std::make_shared<WebSocketConnection>(Session->Network);Socket->Id=Session->Network->NextId++;
        Socket->Reservation=std::make_shared<WebSocketReservation>(Limits,Reserved);Socket->ServerRole=true;
        Socket->HostedLease=Session;
        std::weak_ptr<HostedSession> Weak=Session;
        Socket->HostedClosed=[Weak]() noexcept {
            if(auto Session=Weak.lock()) {
                try{Session->Deadline.cancel();}catch(...){}
                Session->Open=false;
                ErrorCode Error;if(Session->Connection)Session->Connection->Socket.close(Error);
                if(Session->State!=HostedSession::Phase::Upgraded)Session->State=HostedSession::Phase::Closed;
                Session->Network->Sessions.erase(Session->Id);
            }
        };
        Socket->MaxMessageBytes=Policy.MaxMessage;Socket->MaxFrameBytes=Policy.MaxFrame;Socket->MaxQueuedBytes=Policy.MaxQueue;Socket->MaxQueuedMessages=Policy.MaxMessages;
        Socket->CloseTimeoutMs=Policy.CloseTimeout;Socket->IdleTimeoutMs=Policy.IdleTimeout;Socket->PingIntervalMs=Policy.PingInterval;Socket->PongTimeoutMs=Policy.PongTimeout;
        Socket->Parser=std::make_unique<Lui::WebSocket::Parser>(Policy.MaxMessage,Policy.MaxFrame,true);
        Socket->Subprotocol=Session->SelectedProtocol;Socket->HandshakeWire=Session->UpgradeWire;
        Session->Network->WebSockets.emplace(Socket->Id,Socket);
        asio::post(Session->Network->Io,WebSocketGuard(Socket,[Session,Socket] {
            Session->Upgrade=Socket;
            if(WebSocketFault(Socket->Reservation->State,13)) {
                auto State=Socket->Reservation->State;std::unique_lock<std::mutex> Lock(State->QualificationMutex);
                State->QualificationCommitEntered=true;State->QualificationChanged.notify_all();
                if(!State->QualificationChanged.wait_for(Lock,std::chrono::seconds(3),[&]{return State->QualificationCommitReleased;})) {
                    State->QualificationCommitTimedOut=true;throw std::bad_alloc();
                }
            }
            if(!Session->Open || !Session->Server->Open){FinishWebSocket(Socket,"[LUI:WebSocket] Canceled");return;}
            Session->State=HostedSession::Phase::Committing;
            Socket->Connection=std::move(Session->Connection);Socket->Tls=std::move(Session->Tls);
            Session->Network->ActiveWebSockets.emplace(Socket->Id,Socket);
            CheckWebSocketFault(Socket->Reservation->State,9);
            auto Complete=WebSocketGuard(Socket,[Session,Socket](const ErrorCode& Error,size_t) {
                if(Error || !Session->Open || !Session->Server->Open || Socket->Phase==3){FinishWebSocket(Socket,"[LUI:WebSocket] Canceled");return;}
                Session->Deadline.cancel();Session->Open=false;Session->State=HostedSession::Phase::Upgraded;
                Socket->ServerReady=true;Socket->Phase=1;Socket->LastActivity=std::chrono::steady_clock::now();
                NetworkCompletion Completion;Completion.Type=NetworkCompletion::Kind::HostedWebSocket;Completion.Session=Session;Completion.WebSocket=Socket;
                Queue(Session->Network->Runtime,std::move(Completion));
                StartWebSocketIdle(Socket);StartWebSocketPing(Socket);
                std::string Pending=std::move(Session->Pending);Session->Pending.clear();FeedWebSocket(Socket,Pending);ReadWebSocket(Socket);
            });
            if(Socket->Tls)asio::async_write(Socket->Tls->Socket,asio::buffer(Socket->HandshakeWire),std::move(Complete));
            else asio::async_write(Socket->Connection->Socket,asio::buffer(Socket->HandshakeWire),std::move(Complete));
        }));
    }catch(const std::bad_alloc&) {
        if(Socket)Session->Network->WebSockets.erase(Socket->Id);
        try{asio::post(Session->Network->Io,HostedGuard(Session,[Session]{HostedStatus(Session,503,"Service Unavailable");}));}catch(const std::bad_alloc&){}
    }
}
static int BuildHostedWebSocketTaskImpl(lua_State* State) {
    auto* Completion=static_cast<NetworkCompletion*>(lua_touserdata(State,1));auto Session=Completion->Session;
    const auto Path=Session->Request->Target.substr(0,Session->Request->Target.find('?'));
    const auto& Route=Session->Server->WebSocketRoutes.at(Path);
    // Reuse the immutable HTTP request materializer on a protected temporary
    // thread, then call the connection handler with socket and request.
    auto* Runtime=GetRuntime(State);lua_State* Thread=lua_newthread(State);if(Runtime->Sandboxed)luaL_sandboxthread(Thread);
    lua_getref(State,Route.Handler);lua_xmove(State,Thread,1);
    PushWebSocket(State,Completion->WebSocket);lua_xmove(State,Thread,1);
    const auto& Request=*Session->Request;
    lua_createtable(State,0,9);
    lua_pushlstring(State,Request.Method.data(),Request.Method.size());lua_setfield(State,-2,"Method");
    lua_pushlstring(State,Path.data(),Path.size());lua_setfield(State,-2,"Path");
    lua_pushlstring(State,Request.Target.data(),Request.Target.size());lua_setfield(State,-2,"RawTarget");
    lua_pushstring(State,"HTTP/1.1");lua_setfield(State,-2,"HttpVersion");lua_pushliteral(State,"");lua_setfield(State,-2,"Body");
    PushHttpFields(State,Request.Headers);lua_setfield(State,-2,"Headers");PushHttpFields(State,Request.Trailers);lua_setfield(State,-2,"Trailers");
    PushEndpoint(State,Session->LocalAddress,Session->LocalPort);lua_setfield(State,-2,"LocalEndpoint");
    PushEndpoint(State,Session->RemoteAddress,Session->RemotePort);lua_setfield(State,-2,"RemoteEndpoint");
    lua_setreadonly(State,-1,true);lua_xmove(State,Thread,1);
    int Reference=lua_ref(State,-1);lua_pushinteger(State,Reference);return 1;
}
static int BuildHostedWebSocketTask(lua_State* State) {
    try{return BuildHostedWebSocketTaskImpl(State);}catch(const std::bad_alloc&){return Raise(State,"[LUI:WebSocket] ResourceExhausted");}
}
static void DispatchHostedWebSocket(LuiRuntime* Runtime,const std::shared_ptr<HostedSession>& Session,const std::shared_ptr<WebSocketConnection>& Socket) {
    if(!Session->Server->Open || !Socket->ServerReady || Session->State!=HostedSession::Phase::Upgraded){AbandonWebSocket(Socket);return;}
    try{CheckWebSocketFault(Socket->Reservation->State,10);}catch(const std::bad_alloc&){AbandonWebSocket(Socket);return;}
    NetworkCompletion Completion;Completion.Session=Session;Completion.WebSocket=Socket;
    lua_getfield(Runtime->State,LUA_REGISTRYINDEX,"LuiHostedWebSocketTask");lua_pushlightuserdata(Runtime->State,&Completion);
    if(lua_pcall(Runtime->State,1,1,0)!=LUA_OK){lua_pop(Runtime->State,1);AbandonWebSocket(Socket);return;}
    int Reference=int(lua_tointeger(Runtime->State,-1));lua_pop(Runtime->State,1);
    try {
        CheckWebSocketFault(Socket->Reservation->State,11);
        Runtime->Network->HandlerTasks.emplace(Reference,Session);Runtime->Tasks.push_back({Reference,std::chrono::steady_clock::now(),2});
    }catch(const std::bad_alloc&) {
        Runtime->Network->HandlerTasks.erase(Reference);lua_unref(Runtime->State,Reference);AbandonWebSocket(Socket);
    }
}
static void DispatchHosted(LuiRuntime* Runtime, const std::shared_ptr<HostedSession>& Session) {
    if (!Session->Open || !Session->Server->Open) return;
    auto Path = Session->Request->Target.substr(0, Session->Request->Target.find('?'));
    if(Session->Request->WebSocketUpgrade) {
        auto Route=Session->Server->WebSocketRoutes.find(Path);
        if(Route==Session->Server->WebSocketRoutes.end())return;
        if(!Route->second.Authorize){PrepareHostedWebSocket(Runtime,Session);return;}
    } else
    if (!Session->Server->Routes.count(Session->Request->Method + " " + Path)) {
        asio::post(Session->Network->Io, [Session] { HostedStatus(Session, 404, "Not Found", Session->CloseAfter); });
        return;
    }
    lua_getfield(Runtime->State, LUA_REGISTRYINDEX, "LuiHostedTask");
    lua_pushlightuserdata(Runtime->State, Session.get());
    if (lua_pcall(Runtime->State, 1, 1, 0) != LUA_OK) {
        HostedFailure(Runtime, Session, Session->Request->WebSocketUpgrade?"authorization failed":lua_tostring(Runtime->State, -1)); lua_pop(Runtime->State, 1); return;
    }
    int Reference = static_cast<int>(lua_tointeger(Runtime->State, -1)); lua_pop(Runtime->State, 1);
    try {
        Runtime->Network->HandlerTasks.emplace(Reference, Session);
        Runtime->Tasks.push_back({Reference, std::chrono::steady_clock::now(), 1});
    }catch(const std::bad_alloc&) {
        Runtime->Network->HandlerTasks.erase(Reference);lua_unref(Runtime->State,Reference);
        // The existing request deadline retires the session if dispatch cannot be admitted.
    }
}
bool NetworkTaskReady(LuiRuntime* Runtime, int Reference) {
    if (!Runtime->Network) return true;
    auto Found = Runtime->Network->HandlerTasks.find(Reference);
    return Found == Runtime->Network->HandlerTasks.end() ||
        (Found->second->Server->Open && (Found->second->Open || Found->second->State==HostedSession::Phase::Upgraded));
}
bool CompleteNetworkTask(LuiRuntime* Runtime, int Reference, lua_State* Thread, int Status) {
    if (!Runtime->Network) return false;
    auto Found = Runtime->Network->HandlerTasks.find(Reference);
    if (Found == Runtime->Network->HandlerTasks.end()) return false;
    auto Session = Found->second;
    Runtime->Network->HandlerTasks.erase(Found);
    if(Session->Request->WebSocketUpgrade) {
        if(Session->State==HostedSession::Phase::Upgraded) {
            if(auto Socket=Session->Upgrade.lock()) {
                bool Success=Status==LUA_OK && Thread;
                try {asio::post(Socket->Network->Io,WebSocketGuard(Socket,[Socket,Success] {
                    if(Success)StartWebSocketClose(Socket,1000,{});else FinishWebSocket(Socket,"[LUI:WebSocket] HandlerFailed");
                }));}catch(const std::bad_alloc&){AbandonWebSocket(Socket);}
            }
        } else if(Session->Open && Session->Server->Open) {
            const bool Valid=Status==LUA_OK && Thread && lua_gettop(Thread)==1 && lua_type(Thread,1)==LUA_TBOOLEAN;
            if(Valid && lua_toboolean(Thread,1))PrepareHostedWebSocket(Runtime,Session);
            else try{asio::post(Session->Network->Io,HostedGuard(Session,[Session,Valid]{HostedStatus(Session,Valid?403:500,Valid?"Forbidden":"Internal Server Error");}));}catch(const std::bad_alloc&){}
        }
        return true;
    }
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
