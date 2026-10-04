// Private implementation included by Network.cpp. No VM access on the worker.
using Udp = asio::ip::udp;

struct NetworkDatagramSocket {
    explicit NetworkDatagramSocket(asio::io_context& Io) : Socket(Io) {}
    Udp::socket Socket;
    std::atomic<bool> Open{true};
    std::atomic<bool> ReceivePending{false};
    std::atomic<size_t> QueuedSendBytes{0};
    std::atomic<size_t> QueuedSends{0};
    std::string LocalAddress;
    unsigned short LocalPort = 0;
    size_t MaxDatagramBytes = 65507;
    bool IPv6 = false;
    struct SendEntry { int Reference; Udp::endpoint Endpoint; std::shared_ptr<std::string> Bytes; };
    std::deque<SendEntry> Sends; // Network worker only.
    bool Sending = false; // Network worker only.
};

static std::shared_ptr<NetworkDatagramSocket> GetUdpSocket(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    int Id = CheckedId(State, "LuiUdpSocketMeta");
    if (!Runtime->Network) luaL_error(State, "[LUI:Network] Closed");
    auto Found = Runtime->Network->Datagrams.find(Id);
    if (Found == Runtime->Network->Datagrams.end()) luaL_error(State, "[LUI:Network] Closed");
    return Found->second;
}

static int BindUdp(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    const auto Grants = LUI_CAPABILITY_NETWORK_SERVER | LUI_CAPABILITY_NETWORK_RAW;
    if ((Runtime->GrantedCapabilities & Grants) != Grants)
        return Raise(State, "[LUI:Network] network.server and network.raw grants are required");
    luaL_checktype(State, 2, LUA_TTABLE);
    std::string Address = OptionString(State, 2, "Address", "loopback");
    std::string Family = OptionString(State, 2, "Family", "");
    unsigned short Port = OptionPort(State, 2, true);
    lua_getfield(State, 2, "MaxDatagramBytes");
    double Limit = lua_isnil(State, -1) ? 65507 : luaL_checknumber(State, -1);
    lua_pop(State, 1);
    if (!std::isfinite(Limit) || std::floor(Limit) != Limit || Limit < 1 || Limit > 65507)
        return Raise(State, "[LUI:Network] MaxDatagramBytes must be an integer between 1 and 65507");
    ErrorCode Error;
    bool SemanticAddress = Address == "loopback" || Address == "any";
    auto Ip = SemanticAddress ? asio::ip::address{} : asio::ip::make_address(Address, Error);
    if (Error) return Raise(State, "[LUI:Network] Address must be a numeric local address");
    if (Family.empty()) Family = !SemanticAddress && Ip.is_v6() ? "IPv6" : "IPv4";
    if (Family != "IPv4" && Family != "IPv6")
        return Raise(State, "[LUI:Network] UDP Family must be IPv4 or IPv6");
    if (!SemanticAddress && Ip.is_v6() != (Family == "IPv6"))
        return Raise(State, "[LUI:Network] Address and Family disagree");
    if (SemanticAddress) Ip = asio::ip::make_address(Family == "IPv6"
        ? (Address == "any" ? "::" : "::1") : (Address == "any" ? "0.0.0.0" : "127.0.0.1"));
    if (Ip.is_multicast() || (Ip.is_v6() && Ip.to_v6().is_v4_mapped()))
        return Raise(State, "[LUI:Network] unsupported UDP address");
    if ((Runtime->NetworkPolicyFlags & LUI_NETWORK_POLICY_SERVER_LOOPBACK_ONLY) && !Ip.is_loopback())
        return Raise(State, "[LUI:Network] PolicyDenied");
    if (Runtime->ServerPortMin && (Port < Runtime->ServerPortMin || Port > Runtime->ServerPortMax))
        return Raise(State, "[LUI:Network] PolicyDenied");
    auto* Network = Context(Runtime);
    if (Network->Datagrams.size() >= 16) return Raise(State, "[LUI:Network] TooManySockets");
    auto Socket = std::make_shared<NetworkDatagramSocket>(Network->Io);
    Socket->IPv6 = Family == "IPv6";
    Socket->MaxDatagramBytes = static_cast<size_t>(Limit);
    Udp::endpoint Endpoint(Ip, Port);
    Socket->Socket.open(Endpoint.protocol(), Error);
    if (!Error && Socket->IPv6) Socket->Socket.set_option(asio::ip::v6_only(true), Error);
    if (!Error) Socket->Socket.bind(Endpoint, Error);
    if (Error) return Raise(State, "%s", NetworkError(Error).c_str());
    auto Local = Socket->Socket.local_endpoint(Error);
    if (Error) return Raise(State, "%s", NetworkError(Error).c_str());
    Socket->LocalAddress = Local.address().to_string();
    Socket->LocalPort = Local.port();
    int Id = Network->NextId++;
    *static_cast<int*>(lua_newuserdata(State, sizeof(int))) = Id;
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiUdpSocketMeta");
    lua_setmetatable(State, -2);
    Network->Datagrams.emplace(Id, std::move(Socket));
    return 1;
}

static int ReceiveFromAsync(lua_State* State) {
    auto Socket = GetUdpSocket(State);
    if (!Socket->Open) return Raise(State, "[LUI:Network] Closed");
    CheckAwait(State);
    if (Socket->ReceivePending.exchange(true)) return Raise(State, "[LUI:Network] ReceivePending");
    // One extra byte detects silent truncation on POSIX. Windows may report
    // message_size instead; neither path exposes a partial datagram to Luau.
    auto Bytes = std::make_shared<std::string>(Socket->MaxDatagramBytes + 1, '\0');
    auto Remote = std::make_shared<Udp::endpoint>();
    auto* Runtime = GetRuntime(State);
    int Reference = BeginAwait(State);
    asio::post(Runtime->Network->Io, [Runtime, Socket, Bytes, Remote, Reference] {
        if (!Socket->Open) {
            Socket->ReceivePending = false;
            Queue(Runtime, {NetworkCompletion::Kind::None, Reference, {}, "[LUI:Network] Canceled", {}});
            return;
        }
        Socket->Socket.async_receive_from(asio::buffer(*Bytes), *Remote,
            [Runtime, Socket, Bytes, Remote, Reference](const ErrorCode& Error, size_t Count) {
                Socket->ReceivePending = false;
                NetworkCompletion Completion;
                Completion.Type = NetworkCompletion::Kind::Datagram;
                Completion.Reference = Reference;
                if (!Socket->Open) Completion.Error = "[LUI:Network] Canceled";
                else if (Error == asio::error::message_size || Count > Socket->MaxDatagramBytes)
                    Completion.Error = "[LUI:Network] MessageTooLarge";
                else if (Error) Completion.Error = NetworkError(Error);
                else {
                    Bytes->resize(Count);
                    Completion.Bytes = std::move(*Bytes);
                    Completion.RemoteAddress = Remote->address().to_string();
                    Completion.RemotePort = Remote->port();
                }
                Queue(Runtime, std::move(Completion));
            });
    });
    return lua_yield(State, 0);
}

static void StartUdpSend(LuiRuntime* Runtime, const std::shared_ptr<NetworkDatagramSocket>& Socket) {
    if (Socket->Sending) return;
    while (!Socket->Sends.empty() && !Socket->Open) {
        auto Entry = std::move(Socket->Sends.front());
        Socket->Sends.pop_front();
        Socket->QueuedSendBytes -= Entry.Bytes->size();
        --Socket->QueuedSends;
        Queue(Runtime, {NetworkCompletion::Kind::None, Entry.Reference, {}, "[LUI:Network] Canceled", {}});
    }
    if (Socket->Sends.empty()) return;
    Socket->Sending = true;
    auto Entry = Socket->Sends.front();
    Socket->Socket.async_send_to(asio::buffer(*Entry.Bytes), Entry.Endpoint,
        [Runtime, Socket, Entry](const ErrorCode& Error, size_t Count) {
            Socket->Sends.pop_front();
            Socket->Sending = false;
            Socket->QueuedSendBytes -= Entry.Bytes->size();
            --Socket->QueuedSends;
            std::string Failure;
            if (!Socket->Open) Failure = "[LUI:Network] Canceled";
            else if (Error) Failure = NetworkError(Error);
            else if (Count != Entry.Bytes->size()) Failure = "[LUI:Network] MessageTooLarge";
            Queue(Runtime, {NetworkCompletion::Kind::None, Entry.Reference, {}, Failure, {}});
            StartUdpSend(Runtime, Socket);
        });
}

static int SendToAsync(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    if (!(Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_CLIENT))
        return Raise(State, "[LUI:Network] network.client grant is required");
    auto Socket = GetUdpSocket(State);
    if (!Socket->Open) return Raise(State, "[LUI:Network] Closed");
    luaL_checktype(State, 2, LUA_TTABLE);
    std::string Address = OptionString(State, 2, "Address", "");
    unsigned short Port = OptionPort(State, 2, false);
    ErrorCode Error;
    auto Ip = asio::ip::make_address(Address, Error);
    if (Error) return Raise(State, "[LUI:Network] UDP destination must be a numeric address");
    if (Ip.is_unspecified() || Ip.is_multicast() ||
        (Ip.is_v4() && Ip.to_v4() == asio::ip::address_v4::broadcast()) ||
        (Ip.is_v6() && Ip.to_v6().is_v4_mapped()))
        return Raise(State, "[LUI:Network] unsupported UDP destination");
    if (Ip.is_v6() != Socket->IPv6) return Raise(State, "[LUI:Network] Address and Family disagree");
    if (((Runtime->NetworkPolicyFlags & LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY) && !Ip.is_loopback()) ||
        (Runtime->ClientPortMin && (Port < Runtime->ClientPortMin || Port > Runtime->ClientPortMax)))
        return Raise(State, "[LUI:Network] PolicyDenied");
    size_t Length = 0;
    const char* Source = nullptr;
    if (lua_type(State, 3) == LUA_TSTRING) Source = lua_tolstring(State, 3, &Length);
    else if (lua_type(State, 3) == LUA_TBUFFER) Source = static_cast<const char*>(lua_tobuffer(State, 3, &Length));
    else return Raise(State, "[LUI:Network] SendToAsync expects a string or buffer");
    if (Length > Socket->MaxDatagramBytes) return Raise(State, "[LUI:Network] MessageTooLarge");
    CheckAwait(State);
    if (Socket->QueuedSends.load() >= 32 || Socket->QueuedSendBytes.load() + Length > 256 * 1024)
        return Raise(State, "[LUI:Network] SendQueueFull");
    auto Bytes = std::make_shared<std::string>(Source, Length);
    ++Socket->QueuedSends;
    Socket->QueuedSendBytes += Length;
    int Reference = BeginAwait(State);
    asio::post(Runtime->Network->Io, [Runtime, Socket, Reference, Endpoint = Udp::endpoint(Ip, Port), Bytes] {
        Socket->Sends.push_back({Reference, Endpoint, Bytes});
        StartUdpSend(Runtime, Socket);
    });
    return lua_yield(State, 0);
}

static int UdpClose(lua_State* State) {
    auto Socket = GetUdpSocket(State);
    if (Socket->Open.exchange(false))
        asio::post(GetRuntime(State)->Network->Io, [Socket] { ErrorCode Error; Socket->Socket.close(Error); });
    return 0;
}

static int UdpGc(lua_State* State) {
    auto* Runtime = GetRuntime(State);
    if (!Runtime->Network) return 0;
    int Id = *static_cast<int*>(lua_touserdata(State, 1));
    auto Found = Runtime->Network->Datagrams.find(Id);
    if (Found == Runtime->Network->Datagrams.end()) return 0;
    auto Socket = Found->second;
    if (Socket->Open.exchange(false))
        asio::post(Runtime->Network->Io, [Socket] { ErrorCode Error; Socket->Socket.close(Error); });
    Runtime->Network->Datagrams.erase(Found);
    return 0;
}

static int UdpIndex(lua_State* State) {
    const char* Key = luaL_checkstring(State, 2);
    auto Socket = GetUdpSocket(State);
    if (std::strcmp(Key, "IsOpen") == 0) lua_pushboolean(State, Socket->Open);
    else if (std::strcmp(Key, "LocalEndpoint") == 0) PushEndpoint(State, Socket->LocalAddress, Socket->LocalPort);
    else if (std::strcmp(Key, "ReceiveFromAsync") == 0) lua_pushcfunction(State, ReceiveFromAsync, "UdpSocket.ReceiveFromAsync");
    else if (std::strcmp(Key, "SendToAsync") == 0) lua_pushcfunction(State, SendToAsync, "UdpSocket.SendToAsync");
    else if (std::strcmp(Key, "Close") == 0) lua_pushcfunction(State, UdpClose, "UdpSocket.Close");
    else lua_pushnil(State);
    return 1;
}

static void RegisterUdpTypes(lua_State* State) {
    luaL_newmetatable(State, "LuiUdpSocketMeta");
    lua_pushcfunction(State, UdpIndex, "UdpSocket.__index"); lua_setfield(State, -2, "__index");
    lua_pushcfunction(State, ReadOnly, "UdpSocket.__newindex"); lua_setfield(State, -2, "__newindex");
    lua_pushcfunction(State, UdpGc, "UdpSocket.__gc"); lua_setfield(State, -2, "__gc");
    lua_pop(State, 1);
}
