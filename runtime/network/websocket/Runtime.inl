// Private integration with the existing Asio worker, dialer, TLS provider and
// owner completion queue. This file is included only by Network.cpp.
struct WebSocketReservation {
    std::shared_ptr<WebSocketRuntimeState> State;
    uint64_t Bytes;
    WebSocketReservation(std::shared_ptr<WebSocketRuntimeState> State, uint64_t Bytes) : State(std::move(State)), Bytes(Bytes) {
        auto Live = this->State->LiveConnections.fetch_add(1) + 1;
        auto Reserved = this->State->ReservedBytes.fetch_add(Bytes) + Bytes;
        this->State->PeakConnections = std::max(this->State->PeakConnections.load(), Live);
        this->State->PeakReservedBytes = std::max(this->State->PeakReservedBytes.load(), Reserved);
        ++this->State->Accepted;
    }
    ~WebSocketReservation() { State->ReservedBytes.fetch_sub(Bytes); --State->LiveConnections; }
};
static bool WebSocketFault(const std::shared_ptr<WebSocketRuntimeState>& State, int Stage) {
    int Expected = Stage; return State->FaultStage.compare_exchange_strong(Expected, 0);
}
static void CheckWebSocketFault(const std::shared_ptr<WebSocketRuntimeState>& State, int Stage) {
    if (WebSocketFault(State, Stage)) throw std::bad_alloc();
}
struct WebSocketAccounting {
    std::atomic<size_t> InboundBytes{0}, InboundMessages{0}, OutboundBytes{0}, OutboundMessages{0};
};
struct WebSocketPayload {
    std::shared_ptr<WebSocketReservation> Reservation;
    std::shared_ptr<WebSocketAccounting> Accounting;
    bool Outbound = false, Binary = false;
    std::string Data;
    WebSocketPayload(std::shared_ptr<WebSocketReservation> Reservation, std::shared_ptr<WebSocketAccounting> Accounting,
        std::string Data, bool Binary, bool Outbound) : Reservation(std::move(Reservation)), Accounting(std::move(Accounting)),
        Outbound(Outbound), Binary(Binary), Data(std::move(Data)) {
        if (Outbound) {
            this->Accounting->OutboundBytes.fetch_add(this->Data.size()); ++this->Accounting->OutboundMessages;
            this->Reservation->State->OutboundBytes.fetch_add(this->Data.size()); ++this->Reservation->State->PendingSends;
        } else {
            this->Accounting->InboundBytes.fetch_add(this->Data.size()); ++this->Accounting->InboundMessages;
            this->Reservation->State->InboundBytes.fetch_add(this->Data.size()); ++this->Reservation->State->ReceivedMessages;
        }
    }
    ~WebSocketPayload() {
        const size_t Bytes = Data.size(); std::string().swap(Data);
        if (Outbound) {
            Accounting->OutboundBytes.fetch_sub(Bytes); --Accounting->OutboundMessages;
            Reservation->State->OutboundBytes.fetch_sub(Bytes); --Reservation->State->PendingSends;
        } else {
            Accounting->InboundBytes.fetch_sub(Bytes); --Accounting->InboundMessages;
            Reservation->State->InboundBytes.fetch_sub(Bytes);
        }
    }
};
struct WebSocketWrite {
    int Reference = 0;
    uint8_t Opcode = 0;
    std::shared_ptr<WebSocketPayload> Payload;
    std::string Control, Wire;
};
struct WebSocketConnection {
    // Reservation is released after all payloads, protocol storage and handlers.
    std::shared_ptr<WebSocketReservation> Reservation;
    explicit WebSocketConnection(NetworkContext* Network) : Network(Network), Deadline(Network->Io), CloseDeadline(Network->Io),
        IdleTimer(Network->Io), PingTimer(Network->Io), PongTimer(Network->Io) {}
    NetworkContext* Network;
    std::shared_ptr<void> HostedLease;
    std::function<void()> HostedClosed;
    bool ServerRole = false;
    std::atomic<bool> ServerReady{false};
    bool OwnerPublished = false;
    int Id = 0, ConnectReference = 0;
    int OwnerConnectReference = 0;
    std::array<int, 16> SendReferences{}; // Owner only, includes queued results.
    std::array<std::atomic<int>, 18> Deliveries{}; // Positive pending, negative queued, zero retired; protected by CompletionMutex when queuing.
    std::atomic<int> Emergency{0}; // Allocation-free worker-to-owner failure handoff.
    std::atomic<int> TerminalFailure{0};
    int ReceiveReference = 0; // Owner-only admission flag; cleared before resume.
    bool OwnerCloseRequested = false, CancelPosted = false; // Owner only.
    int Waiter = 0; // Worker only.
    std::atomic<int> Phase{0}; // Connecting, Open, Closing, Closed.
    std::mutex SnapshotMutex;
    uint16_t CloseCode = 0;
    std::string CloseReason, Error, Subprotocol;
    HttpTokenRoot Cancellation;
    std::shared_ptr<WebSocketAccounting> Accounting = std::make_shared<WebSocketAccounting>();
    std::shared_ptr<DialOperation> Dial;
    std::shared_ptr<NetworkConnection> Connection;
    std::shared_ptr<Lui::Tls::Stream> Tls;
    std::unique_ptr<Lui::WebSocket::Handshake> Handshake;
    std::unique_ptr<Lui::WebSocket::Parser> Parser;
    std::string HandshakeWire;
    std::array<char, 16384> Buffer;
    std::deque<std::shared_ptr<WebSocketPayload>> Inbox;
    std::deque<std::shared_ptr<WebSocketWrite>> Writes;
    asio::steady_timer Deadline, CloseDeadline, IdleTimer, PingTimer, PongTimer;
    unsigned CloseTimeoutMs = 5000, IdleTimeoutMs = 0, PingIntervalMs = 0, PongTimeoutMs = 10000;
    size_t MaxMessageBytes = 1024 * 1024, MaxFrameBytes = 1024 * 1024, MaxQueuedBytes = 4 * 1024 * 1024, MaxQueuedMessages = 16;
    std::chrono::steady_clock::time_point LastActivity;
    uint64_t PingSequence = 0;
    std::string ExpectedPong;
    uint16_t LocalCloseCode = 1000;
    std::string LocalCloseReason;
    bool Writing = false, Reading = false, CloseRequested = false, CloseQueued = false, CloseSent = false, PeerClosed = false;
    bool PingPending = false, AwaitingPong = false;
    bool PingAcknowledged = false;
    std::function<void()> Start;
    unsigned ControlWrites = 0;
};
static void StartNextWebSocket(NetworkContext* Network);
static void AbandonWebSocket(const std::shared_ptr<WebSocketConnection>& Socket);
static void AbortWebSocket(const std::shared_ptr<WebSocketConnection>& Socket, int Failure) noexcept {
    if (Socket->Phase.exchange(3) != 3) ++Socket->Reservation->State->Closed;
    ErrorCode Ignored;
    auto CancelTimer = [](asio::steady_timer& Timer) noexcept { try { Timer.cancel(); } catch (...) {} };
    CancelTimer(Socket->Deadline); CancelTimer(Socket->CloseDeadline); CancelTimer(Socket->IdleTimer);
    CancelTimer(Socket->PingTimer); CancelTimer(Socket->PongTimer);
    if (Socket->Connection) Socket->Connection->Socket.close(Ignored);
    if (Socket->Dial) {
        Socket->Dial->Done = true; Socket->Dial->Resolver.cancel();
        CancelTimer(Socket->Dial->Deadline); CancelTimer(Socket->Dial->Pace);
        for (const auto& Attempt : Socket->Dial->Attempts) Attempt->Socket.close(Ignored);
        Socket->Dial->Completion = {}; Socket->Dial->Failure = {};
    }
    Socket->Network->ActiveWebSockets.erase(Socket->Id);
    if (Socket->HostedClosed) { try { Socket->HostedClosed(); } catch (...) {} }
    Socket->TerminalFailure = Failure;
    Socket->ServerReady = false;
    Socket->Emergency = Failure;
    Lui::Native::NotifyOwner(Socket->Network->OwnerDomain);
    if (Socket->Network->ConnectingWebSocket == Socket->Id) {
        Socket->Network->ConnectingWebSocket = 0;
        StartNextWebSocket(Socket->Network);
    }
}
static void StartNextWebSocket(NetworkContext* Network) {
    while (!Network->ConnectingWebSocket && !Network->ConnectingWebSockets.empty()) {
        auto Socket = std::move(Network->ConnectingWebSockets.front()); Network->ConnectingWebSockets.pop_front();
        if (Socket->Phase == 3) continue;
        Network->ConnectingWebSocket = Socket->Id;
        // Each Start is guarded and captures a weak Socket, avoiding a cycle.
        try { Socket->Start(); }
        catch (const std::bad_alloc&) { AbortWebSocket(Socket, 1); }
        catch (const std::exception&) { AbortWebSocket(Socket, 2); }
    }
}
template<typename Function>
static auto WebSocketGuard(std::shared_ptr<WebSocketConnection> Socket, Function Callback) {
    return [Socket = std::move(Socket), Callback = std::move(Callback)](auto&&... Arguments) mutable {
        try { Callback(std::forward<decltype(Arguments)>(Arguments)...); }
        catch (const std::bad_alloc&) { AbortWebSocket(Socket, 1); }
        catch (const std::exception&) { AbortWebSocket(Socket, 2); }
    };
}
static void DrainWebSocketFailures(LuiRuntime* Runtime) {
    if (!Runtime->Network) return;
    std::array<std::shared_ptr<WebSocketConnection>,8> Abandoned{}; size_t AbandonedCount=0;
    for (const auto& Entry : Runtime->Network->WebSockets) {
        if (Entry.second->ServerRole && !Entry.second->OwnerPublished && !Entry.second->ServerReady && Entry.second->Phase==3) {
            if(AbandonedCount<Abandoned.size())Abandoned[AbandonedCount++]=Entry.second;
            continue;
        }
        const auto& Socket = Entry.second; const int Failure = Socket->Emergency.exchange(0);
        if (!Failure) continue;
        try {
            std::array<int, 18> References{}; References[0] = Socket->OwnerConnectReference; References[1] = Socket->ReceiveReference;
            std::copy(Socket->SendReferences.begin(), Socket->SendReferences.end(), References.begin() + 2);
            for (int Reference : References) if (Reference) {
                NetworkCompletion Completion; Completion.WebSocket = Socket; Completion.Reference = Reference;
                Completion.Type = Reference == Socket->OwnerConnectReference ? NetworkCompletion::Kind::WebSocket : NetworkCompletion::Kind::None;
                Completion.Error = Failure == 1 ? "[LUI:WebSocket] ResourceExhausted" : "[LUI:WebSocket] IoError";
                bool Queued = false;
                {
                    std::lock_guard<std::mutex> Lock(Runtime->CompletionMutex);
                    for (auto& Delivery : Socket->Deliveries) if (Delivery.load() == Reference) {
                        Runtime->NetworkCompletions.push_back(std::move(Completion)); Delivery = -Reference; Queued = true; break;
                    }
                }
                if (Queued) Lui::Native::NotifyOwner(Runtime->Network->OwnerDomain);
            }
        } catch (const std::bad_alloc&) { Socket->Emergency = Failure; }
    }
    for(size_t Index=0;Index<AbandonedCount;++Index)AbandonWebSocket(Abandoned[Index]);
}

static std::shared_ptr<WebSocketRuntimeState> GetWebSocketState(LuiRuntime* Runtime) {
    if (!Runtime->WebSocketState) Runtime->WebSocketState = std::make_shared<WebSocketRuntimeState>();
    return Runtime->WebSocketState;
}
extern "C" LUI_API int LUI_CALL Lui_SetWebSocketOptionsV1(LuiRuntime* Runtime, const LuiWebSocketOptionsV1* Options) {
    if (!Runtime || Runtime->Owner != std::this_thread::get_id() || !Options || Options->StructSize != sizeof(*Options) ||
        Options->AbiVersion != 1 || Options->Reserved || !Options->MaxConnections || Options->MaxConnections > 8 ||
        !Options->MaxMessageBytes || Options->MaxMessageBytes > 1024 * 1024 || !Options->MaxFrameBytes ||
        Options->MaxFrameBytes > Options->MaxMessageBytes || !Options->MaxQueuedBytes || Options->MaxQueuedBytes > 4 * 1024 * 1024 ||
        !Options->MaxQueuedMessages || Options->MaxQueuedMessages > 16 || Options->MaxReservedBytes < 512 * 1024 ||
        Options->MaxReservedBytes > 128ull * 1024 * 1024 || Runtime->ApplicationStarted || Runtime->ShuttingDown || Runtime->Network ||
        (Runtime->WebSocketState && Runtime->WebSocketState->Configured)) return 0;
    try {
        auto Next = std::make_shared<WebSocketRuntimeState>();
        Next->MaxConnections = Options->MaxConnections; Next->MaxMessageBytes = Options->MaxMessageBytes;
        Next->MaxFrameBytes = Options->MaxFrameBytes; Next->MaxQueuedBytes = Options->MaxQueuedBytes;
        Next->MaxQueuedMessages = Options->MaxQueuedMessages; Next->MaxReservedBytes = Options->MaxReservedBytes;
        Next->Configured = true; Runtime->WebSocketState = std::move(Next); return 1;
    } catch (...) { return 0; }
}
extern "C" LUI_API int LUI_CALL Lui_GetWebSocketStatsV1(LuiRuntime* Runtime, LuiWebSocketStatsV1* Stats) {
    if (!Runtime || Runtime->Owner != std::this_thread::get_id() || !Stats || Stats->StructSize != sizeof(*Stats) ||
        Stats->AbiVersion != 1 || Runtime->ShuttingDown) return 0;
    LuiWebSocketStatsV1 Value{sizeof(Value), 1};
    if (const auto& State = Runtime->WebSocketState) {
        Value.LiveConnections = State->LiveConnections; Value.PeakConnections = State->PeakConnections;
        Value.ReservedBytes = State->ReservedBytes; Value.PeakReservedBytes = State->PeakReservedBytes;
        Value.InboundBytes = State->InboundBytes; Value.OutboundBytes = State->OutboundBytes; Value.PendingSends = State->PendingSends;
        Value.ReceivedMessages = State->ReceivedMessages; Value.SentMessages = State->SentMessages;
        Value.Accepted = State->Accepted; Value.Rejected = State->Rejected; Value.Closed = State->Closed;
    }
    *Stats = Value; return 1;
}
static std::string WebSocketError(std::string Error) {
    auto At = Error.find("] ");
    if (At != std::string::npos) Error.erase(0, At + 2);
    if (auto Colon = Error.find(':'); Colon != std::string::npos) Error.resize(Colon);
    return "[LUI:WebSocket] " + Error;
}
static void QueueWebSocket(const std::shared_ptr<WebSocketConnection>& Socket, int Reference,
    NetworkCompletion::Kind Kind, std::string Error = {}, std::shared_ptr<WebSocketPayload> Payload = {}) {
    if (!Reference) return;
    CheckWebSocketFault(Socket->Reservation->State, 5);
    NetworkCompletion Completion;
    Completion.Type = Kind; Completion.Reference = Reference; Completion.Error = std::move(Error);
    Completion.WebSocket = Socket; Completion.WebSocketData = std::move(Payload);
    auto* Runtime = Socket->Network->Runtime; bool Queued = false;
    {
        std::lock_guard<std::mutex> Lock(Runtime->CompletionMutex);
        if (!Runtime->ShuttingDown) for (auto& Delivery : Socket->Deliveries) if (Delivery.load() == Reference) {
            Runtime->NetworkCompletions.push_back(std::move(Completion)); Delivery = -Reference; Queued = true; break;
        }
    }
    if (Queued) Lui::Native::NotifyOwner(Socket->Network->OwnerDomain);
}
static void FinishWebSocket(const std::shared_ptr<WebSocketConnection>& Socket, std::string Error, uint16_t Code = 1006, std::string Reason = {}) {
    if (Socket->Phase == 3) return;
    {
        std::lock_guard<std::mutex> Lock(Socket->SnapshotMutex);
        Socket->Error = std::move(Error); Socket->CloseCode = Code; Socket->CloseReason = std::move(Reason);
        Socket->Phase = 3;
    }
    Socket->Deadline.cancel(); Socket->CloseDeadline.cancel(); Socket->IdleTimer.cancel();
    Socket->PingTimer.cancel(); Socket->PongTimer.cancel();
    if (Socket->Dial && !Socket->Dial->Done) FinishDial(Socket->Dial, {}, "[LUI:Network] Canceled");
    if (Socket->Connection) { ErrorCode Ignored; Socket->Connection->Socket.close(Ignored); }
    Socket->Network->ActiveWebSockets.erase(Socket->Id);
    if (Socket->HostedClosed) Socket->HostedClosed();
    if (Socket->ServerRole) Lui::Native::NotifyOwner(Socket->Network->OwnerDomain);
    ++Socket->Reservation->State->Closed;
    if (Socket->ConnectReference) {
        QueueWebSocket(Socket, std::exchange(Socket->ConnectReference, 0), NetworkCompletion::Kind::WebSocket,
            Socket->Error.empty() ? "[LUI:WebSocket] Closed" : Socket->Error);
    }
    if (Socket->Waiter) {
        QueueWebSocket(Socket, std::exchange(Socket->Waiter, 0), NetworkCompletion::Kind::EndOfStream, Socket->Error);
    }
    for (const auto& Write : Socket->Writes)
        QueueWebSocket(Socket, Write->Reference, NetworkCompletion::Kind::None,
            Socket->Error.empty() ? "[LUI:WebSocket] Closed" : Socket->Error, Write->Payload);
    Socket->Writes.clear();
    Socket->Handshake.reset(); Socket->Parser.reset();
    // HandshakeWire may still back an outstanding async_write. Its handler
    // retains Socket until cancellation completes, so keep that storage intact.
    if (Socket->Network->ConnectingWebSocket == Socket->Id) {
        Socket->Network->ConnectingWebSocket = 0; StartNextWebSocket(Socket->Network);
    }
}
static void WriteWebSocket(const std::shared_ptr<WebSocketConnection>& Socket);
static void ReadWebSocket(const std::shared_ptr<WebSocketConnection>& Socket);
static void StartWebSocketPing(const std::shared_ptr<WebSocketConnection>& Socket);
static void WebSocketControl(const std::shared_ptr<WebSocketConnection>& Socket, uint8_t Opcode, std::string Data) {
    if (Socket->Phase == 3) return;
    if (Socket->ControlWrites >= 4) { FinishWebSocket(Socket, "[LUI:WebSocket] QueueFull", 1008); return; }
    auto Write = std::make_shared<WebSocketWrite>(); Write->Opcode = Opcode; Write->Control = std::move(Data);
    ++Socket->ControlWrites;
    // Controls can pass application writes, but never the active async_write.
    Socket->Writes.insert(Socket->Writes.begin() + (Socket->Writing ? 1 : 0), std::move(Write));
    WriteWebSocket(Socket);
}
static void WebSocketCloseFrame(const std::shared_ptr<WebSocketConnection>& Socket) {
    if (Socket->CloseQueued || Socket->Phase == 3) return;
    Socket->CloseQueued = true;
    std::string Payload;
    if (Socket->LocalCloseCode != 1005) {
        Payload.push_back(static_cast<char>(Socket->LocalCloseCode >> 8)); Payload.push_back(static_cast<char>(Socket->LocalCloseCode));
        Payload += Socket->LocalCloseReason;
    }
    WebSocketControl(Socket, 8, std::move(Payload));
}
static void StartWebSocketClose(const std::shared_ptr<WebSocketConnection>& Socket, uint16_t Code, std::string Reason) {
    if (Socket->Phase == 3 || Socket->CloseRequested) return;
    Socket->Phase = 2; Socket->CloseRequested = true; Socket->LocalCloseCode = Code; Socket->LocalCloseReason = std::move(Reason);
    Socket->PingTimer.cancel(); Socket->PongTimer.cancel();
    Socket->CloseDeadline.expires_after(std::chrono::milliseconds(Socket->CloseTimeoutMs));
    Socket->CloseDeadline.async_wait(WebSocketGuard(Socket, [Socket](const ErrorCode& Error) {
        if (!Error) FinishWebSocket(Socket, "[LUI:WebSocket] TimedOut");
    }));
    if (Socket->Writes.empty()) WebSocketCloseFrame(Socket);
}
static void WriteWebSocket(const std::shared_ptr<WebSocketConnection>& Socket) {
    if (Socket->Phase == 3 || Socket->Writing || Socket->Writes.empty()) return;
    const auto Write = Socket->Writes.front();
    auto Encode = Socket->ServerRole ? Lui::WebSocket::EncodeServerFrame : Lui::WebSocket::EncodeClientFrame;
    auto Code = Encode(static_cast<Lui::WebSocket::Opcode>(Write->Opcode), Write->Payload ? Write->Payload->Data : Write->Control, Write->Wire, true);
    if (Code != Lui::WebSocket::Error::None) { FinishWebSocket(Socket, WebSocketError(Lui::WebSocket::GetErrorName(Code))); return; }
    Socket->Writing = true;
    auto Handler = WebSocketGuard(Socket, [Socket, Write](const ErrorCode& Error, size_t) {
        if (Socket->Phase == 3) return;
        Socket->Writing = false;
        if (Error) { FinishWebSocket(Socket, Socket->Tls ? WebSocketError(Socket->Tls->ErrorName(Error)) : WebSocketError(NetworkError(Error))); return; }
        Socket->Writes.pop_front();
        if (Write->Payload) { ++Socket->Reservation->State->SentMessages; QueueWebSocket(Socket, Write->Reference, NetworkCompletion::Kind::None, {}, Write->Payload); }
        else --Socket->ControlWrites;
        if (Write->Opcode == 9 && Socket->Phase == 1) {
            Socket->PingPending = false; Socket->AwaitingPong = !Socket->PingAcknowledged;
            if (Socket->AwaitingPong) {
                Socket->PongTimer.expires_after(std::chrono::milliseconds(Socket->PongTimeoutMs));
                Socket->PongTimer.async_wait(WebSocketGuard(Socket, [Socket](const ErrorCode& Error) { if (!Error) FinishWebSocket(Socket, "[LUI:WebSocket] PongTimedOut"); }));
            }
        }
        if (Write->Opcode == 8) Socket->CloseSent = true;
        if (Write->Opcode == 8 && Socket->PeerClosed) {
            uint16_t Code; std::string Reason;
            { std::lock_guard<std::mutex> Lock(Socket->SnapshotMutex); Code = Socket->CloseCode; Reason = Socket->CloseReason; }
            FinishWebSocket(Socket, "", Code, std::move(Reason)); return;
        }
        if (Socket->Writes.empty() && Socket->CloseRequested && !Socket->CloseQueued) WebSocketCloseFrame(Socket);
        else WriteWebSocket(Socket);
    });
    if (Socket->Tls) asio::async_write(Socket->Tls->Socket, asio::buffer(Write->Wire), std::move(Handler));
    else asio::async_write(Socket->Connection->Socket, asio::buffer(Write->Wire), std::move(Handler));
}
static void StartWebSocketIdle(const std::shared_ptr<WebSocketConnection>& Socket) {
    if (!Socket->IdleTimeoutMs || Socket->Phase == 3) return;
    Socket->IdleTimer.expires_at(Socket->LastActivity + std::chrono::milliseconds(Socket->IdleTimeoutMs));
    Socket->IdleTimer.async_wait(WebSocketGuard(Socket, [Socket](const ErrorCode& Error) {
        if (Error || Socket->Phase == 3) return;
        if (std::chrono::steady_clock::now() >= Socket->LastActivity + std::chrono::milliseconds(Socket->IdleTimeoutMs))
            FinishWebSocket(Socket, "[LUI:WebSocket] IdleTimedOut");
        else StartWebSocketIdle(Socket);
    }));
}
static void StartWebSocketPing(const std::shared_ptr<WebSocketConnection>& Socket) {
    if (!Socket->PingIntervalMs || Socket->Phase != 1) return;
    Socket->PingTimer.expires_after(std::chrono::milliseconds(Socket->PingIntervalMs));
    Socket->PingTimer.async_wait(WebSocketGuard(Socket, [Socket](const ErrorCode& Error) {
        if (Error || Socket->Phase != 1) return;
        if (!Socket->PingPending && !Socket->AwaitingPong) {
            Socket->ExpectedPong.assign(8, '\0'); auto Sequence = ++Socket->PingSequence;
            for (unsigned Index = 0; Index < 8; ++Index) Socket->ExpectedPong[Index] = static_cast<char>(Sequence >> (Index * 8));
            Socket->PingPending = true; Socket->PingAcknowledged = false; WebSocketControl(Socket, 9, Socket->ExpectedPong);
        }
        StartWebSocketPing(Socket);
    }));
}
static void WebSocketEvent(const std::shared_ptr<WebSocketConnection>& Socket, Lui::WebSocket::Event Event) {
    using Kind = Lui::WebSocket::EventKind;
    if (Event.Kind == Kind::Ping) { if (!Socket->CloseSent) WebSocketControl(Socket, 10, std::move(Event.Data)); return; }
    if (Event.Kind == Kind::Pong) {
        if ((Socket->AwaitingPong || Socket->PingPending) && Event.Data == Socket->ExpectedPong) {
            Socket->PingAcknowledged = true; Socket->AwaitingPong = false; Socket->PongTimer.cancel();
        }
        return;
    }
    if (Event.Kind == Kind::Close) {
        Socket->PeerClosed = true;
        { std::lock_guard<std::mutex> Lock(Socket->SnapshotMutex); Socket->CloseCode = Event.CloseCode; Socket->CloseReason = Event.Data; }
        // A peer close terminates untransmitted reliable messages explicitly.
        auto First = Socket->Writes.begin() + (Socket->Writing ? 1 : 0);
        for (auto At = First; At != Socket->Writes.end(); ++At) {
            QueueWebSocket(Socket, (*At)->Reference, NetworkCompletion::Kind::None, "[LUI:WebSocket] Closed", (*At)->Payload);
            if (!(*At)->Payload) --Socket->ControlWrites;
            if ((*At)->Opcode == 8) Socket->CloseQueued = false;
        }
        Socket->Writes.erase(First, Socket->Writes.end());
        if (Socket->CloseSent && !Socket->Writing) { FinishWebSocket(Socket, "", Event.CloseCode, std::move(Event.Data)); return; }
        StartWebSocketClose(Socket, Event.CloseCode, std::move(Event.Data));
        // A local close may already have been requested but not yet queued.
        if (!Socket->Writing && !Socket->CloseQueued) WebSocketCloseFrame(Socket);
        return;
    }
    if (Socket->CloseRequested) return; // RFC permits ignoring data after local close.
    if (Socket->Accounting->InboundMessages >= Socket->MaxQueuedMessages || Event.Data.size() > Socket->MaxQueuedBytes ||
        Socket->Accounting->InboundBytes > Socket->MaxQueuedBytes - Event.Data.size()) {
        ++Socket->Reservation->State->Rejected; FinishWebSocket(Socket, "[LUI:WebSocket] QueueFull", 1009); return;
    }
    auto Payload = std::make_shared<WebSocketPayload>(Socket->Reservation, Socket->Accounting, std::move(Event.Data), Event.Kind == Kind::Binary, false);
    if (Socket->Waiter) QueueWebSocket(Socket, std::exchange(Socket->Waiter, 0), NetworkCompletion::Kind::WebSocketMessage, {}, std::move(Payload));
    else Socket->Inbox.push_back(std::move(Payload));
}
static void FeedWebSocket(const std::shared_ptr<WebSocketConnection>& Socket, std::string_view Bytes) {
    while (!Bytes.empty() && Socket->Phase != 3 && !Socket->PeerClosed) {
        auto Result = Socket->Parser->Feed(Bytes); Bytes.remove_prefix(Result.Consumed);
        if (Result.Status == Lui::WebSocket::ParseStatus::Failed) {
            FinishWebSocket(Socket, WebSocketError(Lui::WebSocket::GetErrorName(Result.Code)),
                Result.Code == Lui::WebSocket::Error::InvalidUtf8 ? 1007 : Result.Code == Lui::WebSocket::Error::MessageTooLarge ? 1009 : 1002);
            return;
        }
        if (Result.Status != Lui::WebSocket::ParseStatus::Event) break;
        WebSocketEvent(Socket, Socket->Parser->TakeEvent());
    }
}
static void ReadWebSocket(const std::shared_ptr<WebSocketConnection>& Socket) {
    if (Socket->Phase == 3 || Socket->Reading || Socket->PeerClosed) return;
    Socket->Reading = true;
    auto Handler = WebSocketGuard(Socket, [Socket](const ErrorCode& Error, size_t Count) {
        Socket->Reading = false;
        if (Socket->Phase == 3) return;
        if (Error) { FinishWebSocket(Socket, Socket->Tls ? WebSocketError(Socket->Tls->ErrorName(Error)) : WebSocketError(NetworkError(Error))); return; }
        Socket->LastActivity = std::chrono::steady_clock::now();
        FeedWebSocket(Socket, std::string_view(Socket->Buffer.data(), Count)); ReadWebSocket(Socket);
    });
    if (Socket->Tls) Socket->Tls->Socket.async_read_some(asio::buffer(Socket->Buffer), std::move(Handler));
    else Socket->Connection->Socket.async_read_some(asio::buffer(Socket->Buffer), std::move(Handler));
}
static void ReadWebSocketHandshake(const std::shared_ptr<WebSocketConnection>& Socket) {
    if (Socket->Phase == 3) return;
    auto Handler = WebSocketGuard(Socket, [Socket](const ErrorCode& Error, size_t Count) {
        if (Socket->Phase == 3) return;
        if (Error) { FinishWebSocket(Socket, Socket->Tls ? WebSocketError(Socket->Tls->ErrorName(Error)) : WebSocketError(NetworkError(Error))); return; }
        auto Bytes = std::string_view(Socket->Buffer.data(), Count); auto Result = Socket->Handshake->Feed(Bytes);
        if (Result.Status == Lui::WebSocket::ParseStatus::Failed) { FinishWebSocket(Socket, WebSocketError(Lui::WebSocket::GetErrorName(Result.Code))); return; }
        if (Result.Status != Lui::WebSocket::ParseStatus::Event) { ReadWebSocketHandshake(Socket); return; }
        Socket->Subprotocol = Socket->Handshake->SelectedProtocol(); Socket->Handshake.reset(); Socket->HandshakeWire.clear();
        Socket->Deadline.cancel(); Socket->Phase = 1; Socket->LastActivity = std::chrono::steady_clock::now();
        QueueWebSocket(Socket, std::exchange(Socket->ConnectReference, 0), NetworkCompletion::Kind::WebSocket);
        Socket->Network->ConnectingWebSocket = 0; StartNextWebSocket(Socket->Network);
        StartWebSocketIdle(Socket); StartWebSocketPing(Socket);
        Bytes.remove_prefix(Result.Consumed); FeedWebSocket(Socket, Bytes); ReadWebSocket(Socket);
    });
    if (Socket->Tls) Socket->Tls->Socket.async_read_some(asio::buffer(Socket->Buffer), std::move(Handler));
    else Socket->Connection->Socket.async_read_some(asio::buffer(Socket->Buffer), std::move(Handler));
}
static void WriteWebSocketHandshake(const std::shared_ptr<WebSocketConnection>& Socket) {
    auto Handler = WebSocketGuard(Socket, [Socket](const ErrorCode& Error, size_t) {
        if (Socket->Phase == 3) return;
        if (Error) { FinishWebSocket(Socket, Socket->Tls ? WebSocketError(Socket->Tls->ErrorName(Error)) : WebSocketError(NetworkError(Error))); return; }
        ReadWebSocketHandshake(Socket);
    });
    if (Socket->Tls) asio::async_write(Socket->Tls->Socket, asio::buffer(Socket->HandshakeWire), std::move(Handler));
    else asio::async_write(Socket->Connection->Socket, asio::buffer(Socket->HandshakeWire), std::move(Handler));
}
static void CancelWebSockets(NetworkContext* Network, const HttpTokenRoot& Token) {
    std::array<std::shared_ptr<WebSocketConnection>, 8> Active{}; size_t Count = 0;
    for (const auto& Entry : Network->ActiveWebSockets) Active[Count++] = Entry.second;
    for (size_t Index = 0; Index < Count; ++Index) if (Active[Index]->Cancellation == Token)
        WebSocketGuard(Active[Index], [Socket = Active[Index]] { FinishWebSocket(Socket, "[LUI:WebSocket] Canceled"); })();
}
static void CloseWebSockets(NetworkContext* Network) {
    // Worker is already joined; do not queue any callbacks into a retiring VM.
    if (const auto& State = Network->Runtime->WebSocketState) {
        // Also retire an unclaimed stage-7 qualification pin on a failed test
        // path, while every Asio object still has its live io_context.
        State->QualificationRetainedDial.reset();
        State->QualificationRetiredSocket.reset();
    }
    for (const auto& Entry : Network->WebSockets) {
        auto& Socket = Entry.second; Socket->Phase = 3;
        Socket->Deadline.cancel(); Socket->CloseDeadline.cancel(); Socket->IdleTimer.cancel(); Socket->PingTimer.cancel(); Socket->PongTimer.cancel();
        if (Socket->Dial) { Socket->Dial->Resolver.cancel(); Socket->Dial->Deadline.cancel(); Socket->Dial->Pace.cancel(); Socket->Dial->Completion = {}; }
        if (Socket->Connection) { ErrorCode Ignored; Socket->Connection->Socket.close(Ignored); }
        Socket->Inbox.clear(); Socket->Writes.clear();
    }
    Network->ActiveWebSockets.clear(); Network->WebSockets.clear();
    Network->ConnectingWebSockets.clear(); Network->ConnectingWebSocket = 0;
}
static void AbandonWebSocket(const std::shared_ptr<WebSocketConnection>& Socket) {
    try {
        asio::post(Socket->Network->Io, WebSocketGuard(Socket, [Socket] { FinishWebSocket(Socket, "[LUI:WebSocket] Canceled"); }));
        Socket->Network->WebSockets.erase(Socket->Id);
    } catch (const std::bad_alloc&) {
        // A failed post retains the bounded root for whole-generation cleanup.
        // There is no Lua-visible handle and no callback can be delivered again.
    }
}

static size_t WebSocketNumber(lua_State* State, int Table, const char* Key, size_t Default, size_t Min, size_t Max) {
    lua_getfield(State, Table, Key); double Number = lua_isnil(State, -1) ? static_cast<double>(Default) : luaL_checknumber(State, -1); lua_pop(State, 1);
    if (!std::isfinite(Number) || std::floor(Number) != Number || Number < Min || Number > Max) luaL_error(State, "[LUI:WebSocket] InvalidOptions");
    return static_cast<size_t>(Number);
}
static std::string WebSocketString(lua_State* State, int Table, const char* Key, size_t Max = 8192) {
    lua_getfield(State, Table, Key); size_t Length = 0;
    auto Data = lua_isnil(State, -1) ? nullptr : luaL_checklstring(State, -1, &Length);
    if (Length > Max) luaL_error(State, "[LUI:WebSocket] InvalidOptions");
    std::string Value(Data ? Data : "", Length); lua_pop(State, 1); return Value;
}
static int WebSocketDenseArray(lua_State* State, int Table, int Max) {
    const int Count = lua_objlen(State, Table); if (Count > Max) luaL_error(State, "[LUI:WebSocket] InvalidOptions");
    int Fields = 0; lua_pushnil(State);
    while (lua_next(State, Table)) {
        double Key = lua_type(State, -2) == LUA_TNUMBER ? lua_tonumber(State, -2) : 0;
        if (!std::isfinite(Key) || std::floor(Key) != Key || Key < 1 || Key > Count || ++Fields > Max)
            luaL_error(State, "[LUI:WebSocket] InvalidOptions");
        lua_pop(State, 1);
    }
    if (Fields != Count) luaL_error(State, "[LUI:WebSocket] InvalidOptions"); return Count;
}
static int ConnectWebSocketImpl(lua_State* State) {
    CheckModuleEffect(State); auto* Runtime = GetRuntime(State);
    if (!(Runtime->GrantedCapabilities & LUI_CAPABILITY_NETWORK_CLIENT)) return Raise(State, "[LUI:WebSocket] PermissionDenied");
    size_t Length = 0; const char* UrlText = luaL_checklstring(State, 2, &Length);
    if (Length > 8192) return Raise(State, "[LUI:WebSocket] InvalidUrl");
    Lui::Http::Url Destination;
    auto Code = Lui::WebSocket::ParseUrl(std::string_view(UrlText, Length), Destination);
    if (Code != Lui::WebSocket::Error::None) return Raise(State, "[LUI:WebSocket] %s", Lui::WebSocket::GetErrorName(Code));
    const auto Policy = HttpState(Runtime); const auto Limits = GetWebSocketState(Runtime); auto Origin = HttpOrigin(Destination);
    if ((!Policy->Origins.empty() && std::find(Policy->Origins.begin(), Policy->Origins.end(), Origin) == Policy->Origins.end()) ||
        (Runtime->ClientPortMin && (Destination.Port < Runtime->ClientPortMin || Destination.Port > Runtime->ClientPortMax))) {
        ++Limits->Rejected; return Raise(State, "[LUI:WebSocket] PolicyDenied");
    }
    const bool LoopbackOnly = (Runtime->NetworkPolicyFlags & LUI_NETWORK_POLICY_CLIENT_LOOPBACK_ONLY) != 0;
    ErrorCode AddressError; auto Address = asio::ip::make_address(Destination.Host, AddressError);
    if ((LoopbackOnly && ((!AddressError && !Address.is_loopback()) || (AddressError && Destination.Host != "localhost"))) ||
        ((Policy->Flags & LUI_HTTP_PUBLIC_ONLY) && !AddressError && !HttpPublicAddress(Address))) {
        ++Limits->Rejected; return Raise(State, "[LUI:WebSocket] PolicyDenied");
    }
    unsigned Timeout = 10000; size_t MaxMessage = Limits->MaxMessageBytes, MaxFrame = Limits->MaxFrameBytes;
    size_t MaxQueue = Limits->MaxQueuedBytes, MaxMessages = Limits->MaxQueuedMessages;
    unsigned CloseTimeout = 5000, IdleTimeout = 0, PingInterval = 0, PongTimeout = 10000;
    std::vector<Lui::Http::Field> Headers; std::vector<std::string> Protocols; std::string RequestOrigin; HttpTokenRoot Cancellation;
    if (!lua_isnoneornil(State, 3)) {
        luaL_checktype(State, 3, LUA_TTABLE);
        Timeout = static_cast<unsigned>(WebSocketNumber(State, 3, "ConnectTimeoutMs", Timeout, 1, 60000));
        CloseTimeout = static_cast<unsigned>(WebSocketNumber(State, 3, "CloseTimeoutMs", CloseTimeout, 1, 10000));
        IdleTimeout = static_cast<unsigned>(WebSocketNumber(State, 3, "IdleTimeoutMs", 0, 0, 600000));
        PingInterval = static_cast<unsigned>(WebSocketNumber(State, 3, "PingIntervalMs", 0, 0, 600000));
        PongTimeout = static_cast<unsigned>(WebSocketNumber(State, 3, "PongTimeoutMs", PongTimeout, 1, 60000));
        MaxMessage = WebSocketNumber(State, 3, "MaxMessageBytes", MaxMessage, 1, Limits->MaxMessageBytes);
        MaxFrame = WebSocketNumber(State, 3, "MaxFrameBytes", std::min(MaxFrame, MaxMessage), 1, std::min(MaxMessage, static_cast<size_t>(Limits->MaxFrameBytes)));
        MaxQueue = WebSocketNumber(State, 3, "MaxQueuedBytes", MaxQueue, 1, Limits->MaxQueuedBytes);
        MaxMessages = WebSocketNumber(State, 3, "MaxQueuedMessages", MaxMessages, 1, Limits->MaxQueuedMessages);
        RequestOrigin = WebSocketString(State, 3, "Origin");
        lua_getfield(State, 3, "CancellationToken");
        if (!lua_isnil(State, -1)) {
            auto* Root = static_cast<HttpTokenRoot*>(lua_touserdatatagged(State, -1, HttpTokenTag));
            if (!Root || !*Root || (*Root)->State != Policy) return Raise(State, "[LUI:WebSocket] InvalidCancellationToken");
            Cancellation = *Root;
            if (Cancellation->Canceled) return Raise(State, "[LUI:WebSocket] Canceled");
        }
        lua_pop(State, 1);
        lua_getfield(State, 3, "Protocols");
        if (!lua_isnil(State, -1)) {
            luaL_checktype(State, -1, LUA_TTABLE); int Table = lua_gettop(State), Count = WebSocketDenseArray(State, Table, 16);
            for (int Index = 1; Index <= Count; ++Index) {
                lua_rawgeti(State, Table, Index); size_t Bytes = 0; auto Text = luaL_checklstring(State, -1, &Bytes);
                if (Bytes > 128) return Raise(State, "[LUI:WebSocket] InvalidOptions"); Protocols.emplace_back(Text, Bytes); lua_pop(State, 1);
            }
        }
        lua_pop(State, 1); lua_getfield(State, 3, "Headers");
        if (!lua_isnil(State, -1)) {
            luaL_checktype(State, -1, LUA_TTABLE); int Table = lua_gettop(State), Count = WebSocketDenseArray(State, Table, 32); size_t Bytes = 0;
            for (int Index = 1; Index <= Count; ++Index) {
                lua_rawgeti(State, Table, Index); luaL_checktype(State, -1, LUA_TTABLE); int Field = lua_gettop(State);
                auto Name = WebSocketString(State, Field, "Name"); auto Value = WebSocketString(State, Field, "Value");
                Bytes += Name.size() + Value.size(); if (Bytes > 16384) return Raise(State, "[LUI:WebSocket] InvalidOptions");
                Headers.push_back({std::move(Name), std::move(Value)}); lua_pop(State, 1);
            }
        }
        lua_pop(State, 1);
    }
    std::string Wire, Accept;
    Code = Lui::WebSocket::BuildHandshake(Destination, Headers, Protocols, RequestOrigin, Wire, Accept);
    if (Code != Lui::WebSocket::Error::None) return Raise(State, "[LUI:WebSocket] %s", Lui::WebSocket::GetErrorName(Code));
    CheckAwait(State);
    const uint64_t Reserved = 3ull * MaxQueue + 2ull * MaxMessage + 512 * 1024;
    if (Limits->LiveConnections >= Limits->MaxConnections || Reserved > Limits->MaxReservedBytes || Limits->ReservedBytes > Limits->MaxReservedBytes - Reserved) {
        ++Limits->Rejected; return Raise(State, "[LUI:WebSocket] ResourceExhausted");
    }
    auto* Network = Context(Runtime);
    if (Network->NextId <= 0 || Network->NextId == 2147483647) return Raise(State, "[LUI:WebSocket] ResourceExhausted");
    auto Socket = std::make_shared<WebSocketConnection>(Network); Socket->Id = Network->NextId++;
    Socket->Reservation = std::make_shared<WebSocketReservation>(Limits, Reserved); Socket->Cancellation = std::move(Cancellation);
    Socket->HandshakeWire = std::move(Wire); Socket->Handshake = std::make_unique<Lui::WebSocket::Handshake>(Accept, Protocols);
    Socket->Parser = std::make_unique<Lui::WebSocket::Parser>(MaxMessage, MaxFrame); Socket->MaxMessageBytes = MaxMessage; Socket->MaxFrameBytes = MaxFrame;
    Socket->MaxQueuedBytes = MaxQueue; Socket->MaxQueuedMessages = MaxMessages; Socket->CloseTimeoutMs = CloseTimeout;
    Socket->IdleTimeoutMs = IdleTimeout; Socket->PingIntervalMs = PingInterval; Socket->PongTimeoutMs = PongTimeout;
    Socket->ConnectReference = Runtime->ActiveTaskReference; Socket->OwnerConnectReference = Socket->ConnectReference;
    Network->WebSockets.emplace(Socket->Id, Socket); Socket->Deliveries[0] = Socket->ConnectReference;
    try { CheckWebSocketFault(Limits, 1); BeginAwait(State); asio::post(Network->Io, WebSocketGuard(Socket, [Socket, Destination, LoopbackOnly, Timeout, Policy] {
        CheckWebSocketFault(Socket->Reservation->State, 4);
        Socket->Network->ActiveWebSockets.emplace(Socket->Id, Socket);
        if (Socket->Cancellation && Socket->Cancellation->Canceled) { FinishWebSocket(Socket, "[LUI:WebSocket] Canceled"); return; }
        Socket->Deadline.expires_after(std::chrono::milliseconds(Timeout));
        Socket->Deadline.async_wait(WebSocketGuard(Socket, [Socket](const ErrorCode& Error) { if (!Error) FinishWebSocket(Socket, "[LUI:WebSocket] TimedOut"); }));
        std::weak_ptr<WebSocketConnection> QueuedWeak = Socket;
        Socket->Start = [QueuedWeak, Destination, LoopbackOnly, Timeout, Policy] {
        auto Socket = QueuedWeak.lock(); if (!Socket || Socket->Phase == 3) return;
        WebSocketGuard(Socket, [Socket, Destination, LoopbackOnly, Timeout, Policy] {
        Socket->Dial = std::make_shared<DialOperation>(Socket->Network->Io, Socket->Network->Runtime, 0);
        Socket->Dial->RetentionLease = Socket->Reservation;
        Socket->Dial->HttpPublicOnly = (Policy->Flags & LUI_HTTP_PUBLIC_ONLY) != 0;
        std::weak_ptr<WebSocketConnection> Weak = Socket;
        Socket->Dial->Failure = [Weak](int Failure) { if (auto Socket = Weak.lock()) AbortWebSocket(Socket, Failure); };
        Socket->Dial->Completion = WebSocketGuard(Socket, [Weak, Destination](std::shared_ptr<NetworkConnection> Connection, std::string Error) {
            auto Socket = Weak.lock(); if (!Socket || Socket->Phase == 3) return;
            if (!Error.empty()) { FinishWebSocket(Socket, WebSocketError(std::move(Error))); return; }
            Socket->Connection = std::move(Connection);
            if (!Destination.Secure) { WriteWebSocketHandshake(Socket); return; }
            auto Settings = GetTlsSettings(Socket->Network, Error);
            if (!Settings) { FinishWebSocket(Socket, "[LUI:WebSocket] TlsProviderUnavailable"); return; }
            Socket->Tls = std::make_shared<Lui::Tls::Stream>(Socket->Connection->Socket, Settings, false);
            if (!Socket->Tls->SetPeerName(Destination.Host)) { FinishWebSocket(Socket, "[LUI:WebSocket] TlsHandshakeFailed"); return; }
            Socket->Tls->Socket.async_handshake(asio::ssl::stream_base::client, WebSocketGuard(Socket, [Socket](const ErrorCode& Error) {
                if (Socket->Phase == 3) return;
                if (Error) { FinishWebSocket(Socket, WebSocketError(Socket->Tls->ErrorName(Error, true))); return; }
                WriteWebSocketHandshake(Socket);
            }));
        });
        if (WebSocketFault(Socket->Reservation->State, 7)) {
            Socket->Reservation->State->QualificationRetainedDial = Socket->Dial;
            Socket->Reservation->State->QualificationRetiredSocket = Socket;
            FinishWebSocket(Socket, "[LUI:WebSocket] Canceled");
            return;
        }
        BeginDial(Socket->Dial, Destination.Host, Destination.Port, LoopbackOnly, Timeout);
        })();
        };
        Socket->Network->ConnectingWebSockets.push_back(Socket); StartNextWebSocket(Socket->Network);
    })); } catch (...) {
        Runtime->PendingAsyncReferences.erase(Socket->ConnectReference); Runtime->ActiveTaskHasWaiter = false;
        Network->WebSockets.erase(Socket->Id); throw;
    }
    return lua_yield(State, 0);
}
static int ConnectWebSocket(lua_State* State) {
    try { return ConnectWebSocketImpl(State); } catch (const std::bad_alloc&) { return Raise(State, "[LUI:WebSocket] ResourceExhausted"); }
}
static std::shared_ptr<WebSocketConnection> CheckedWebSocket(lua_State* State) {
    const int Id = CheckedId(State, "LuiWebSocketMeta"); auto* Runtime = GetRuntime(State);
    if (!Runtime->Network) luaL_error(State, "[LUI:WebSocket] Closed");
    auto At = Runtime->Network->WebSockets.find(Id);
    if (At == Runtime->Network->WebSockets.end()) luaL_error(State, "[LUI:WebSocket] Closed"); return At->second;
}
static int ReceiveWebSocket(lua_State* State) {
    CheckModuleEffect(State); auto Socket = CheckedWebSocket(State);
    // An emergency retires every admitted operation, including posted worker
    // jobs. Never admit another async operation on that permanently failed
    // socket: a reused Lua registry reference cannot match an old posted job.
    if (Socket->TerminalFailure) return Raise(State, "[LUI:WebSocket] %s", Socket->TerminalFailure == 1 ? "ResourceExhausted" : "IoError");
    CheckAwait(State);
    if (Socket->ReceiveReference) return Raise(State, "[LUI:WebSocket] ReadPending");
    Socket->ReceiveReference = GetRuntime(State)->ActiveTaskReference; const int Reference = Socket->ReceiveReference;
    Socket->Deliveries[1] = Reference;
    try { CheckWebSocketFault(Socket->Reservation->State, 2); BeginAwait(State); asio::post(Socket->Network->Io, WebSocketGuard(Socket, [Socket, Reference] {
        if (!Socket->Inbox.empty()) {
            auto Payload = std::move(Socket->Inbox.front()); Socket->Inbox.pop_front();
            QueueWebSocket(Socket, Reference, NetworkCompletion::Kind::WebSocketMessage, {}, std::move(Payload));
        } else if (Socket->Phase == 3) QueueWebSocket(Socket, Reference, NetworkCompletion::Kind::EndOfStream,
            Socket->TerminalFailure == 1 ? "[LUI:WebSocket] ResourceExhausted" : Socket->TerminalFailure == 2 ? "[LUI:WebSocket] IoError" : Socket->Error);
        else Socket->Waiter = Reference;
    })); } catch (const std::bad_alloc&) {
        auto* Runtime = GetRuntime(State); Runtime->PendingAsyncReferences.erase(Socket->ReceiveReference); Runtime->ActiveTaskHasWaiter = false;
        Socket->ReceiveReference = 0; Socket->Deliveries[1] = 0; return Raise(State, "[LUI:WebSocket] ResourceExhausted");
    }
    return lua_yield(State, 0);
}
static int SendWebSocketImpl(lua_State* State) {
    CheckModuleEffect(State); auto Socket = CheckedWebSocket(State);
    if (Socket->Phase != 1 || Socket->OwnerCloseRequested || Socket->CancelPosted) return Raise(State, "[LUI:WebSocket] Closed");
    size_t Length = 0; bool Binary = lua_type(State, 2) == LUA_TBUFFER;
    const char* Data = Binary ? static_cast<const char*>(lua_tobuffer(State, 2, &Length)) : luaL_checklstring(State, 2, &Length);
    if (!lua_isnoneornil(State, 3)) { luaL_checktype(State, 3, LUA_TBOOLEAN); Binary = lua_toboolean(State, 3) != 0; }
    if (Length > Socket->MaxMessageBytes || Length > Socket->MaxFrameBytes) return Raise(State, "[LUI:WebSocket] MessageTooLarge");
    if (!Binary && !Lui::WebSocket::ValidUtf8(std::string_view(Data, Length))) return Raise(State, "[LUI:WebSocket] InvalidUtf8");
    if (Socket->Accounting->OutboundMessages >= Socket->MaxQueuedMessages || Length > Socket->MaxQueuedBytes ||
        Socket->Accounting->OutboundBytes > Socket->MaxQueuedBytes - Length) {
        ++Socket->Reservation->State->Rejected; return Raise(State, "[LUI:WebSocket] QueueFull");
    }
    CheckAwait(State);
    auto Slot = std::find(Socket->SendReferences.begin(), Socket->SendReferences.end(), 0);
    if (Slot == Socket->SendReferences.end()) return Raise(State, "[LUI:WebSocket] QueueFull");
    auto Payload = std::make_shared<WebSocketPayload>(Socket->Reservation, Socket->Accounting, std::string(Data, Length), Binary, true);
    auto Write = std::make_shared<WebSocketWrite>(); Write->Payload = Payload; Write->Opcode = Binary ? 2 : 1;
    Write->Reference = GetRuntime(State)->ActiveTaskReference; *Slot = Write->Reference;
    const size_t DeliveryIndex = static_cast<size_t>(Slot - Socket->SendReferences.begin()) + 2; Socket->Deliveries[DeliveryIndex] = Write->Reference;
    try { CheckWebSocketFault(Socket->Reservation->State, 3); BeginAwait(State); asio::post(Socket->Network->Io, WebSocketGuard(Socket, [Socket, Write] {
        if (Socket->Phase == 3 || Socket->PeerClosed) { QueueWebSocket(Socket, Write->Reference, NetworkCompletion::Kind::None, "[LUI:WebSocket] Closed", Write->Payload); return; }
        Socket->Writes.push_back(Write); WriteWebSocket(Socket);
    })); } catch (...) {
        auto* Runtime = GetRuntime(State); Runtime->PendingAsyncReferences.erase(Write->Reference); Runtime->ActiveTaskHasWaiter = false; *Slot = 0; Socket->Deliveries[DeliveryIndex] = 0; throw;
    }
    return lua_yield(State, 0);
}
static int SendWebSocket(lua_State* State) {
    try { return SendWebSocketImpl(State); } catch (const std::bad_alloc&) { return Raise(State, "[LUI:WebSocket] ResourceExhausted"); }
}
static int CloseWebSocket(lua_State* State) {
    CheckModuleEffect(State); auto Socket = CheckedWebSocket(State);
    if (Socket->Phase == 3 || Socket->Phase == 2 || Socket->OwnerCloseRequested || Socket->CancelPosted) return 0;
    double Number = lua_isnoneornil(State, 2) ? 1000 : luaL_checknumber(State, 2);
    if (!std::isfinite(Number) || std::floor(Number) != Number || Number < 0 || Number > 65535) return Raise(State, "[LUI:WebSocket] InvalidOptions");
    size_t Length = 0; const char* Text = lua_isnoneornil(State, 3) ? "" : luaL_checklstring(State, 3, &Length);
    if (Lui::WebSocket::ValidateClose(static_cast<uint16_t>(Number), std::string_view(Text, Length)) != Lui::WebSocket::Error::None)
        return Raise(State, "[LUI:WebSocket] InvalidOptions");
    try {
        std::string Reason(Text, Length);
        asio::post(Socket->Network->Io, WebSocketGuard(Socket, [Socket, Code = static_cast<uint16_t>(Number), Reason = std::move(Reason)]() mutable { StartWebSocketClose(Socket, Code, std::move(Reason)); }));
        Socket->OwnerCloseRequested = true;
    } catch (const std::bad_alloc&) { return Raise(State, "[LUI:WebSocket] ResourceExhausted"); }
    return 0;
}
static int CancelWebSocket(lua_State* State) {
    CheckModuleEffect(State); auto Socket = CheckedWebSocket(State); if (Socket->Phase == 3 || Socket->CancelPosted) return 0;
    try { asio::post(Socket->Network->Io, WebSocketGuard(Socket, [Socket] { FinishWebSocket(Socket, "[LUI:WebSocket] Canceled"); })); }
    catch (const std::bad_alloc&) { return Raise(State, "[LUI:WebSocket] ResourceExhausted"); }
    Socket->CancelPosted = true;
    return 0;
}
static int WebSocketIndex(lua_State* State) {
    auto Socket = CheckedWebSocket(State); const char* Key = luaL_checkstring(State, 2);
    if (!std::strcmp(Key, "State")) lua_pushstring(State, Socket->Phase == 0 ? "Connecting" : Socket->Phase == 3 ? "Closed" : Socket->Phase == 2 || Socket->OwnerCloseRequested || Socket->CancelPosted ? "Closing" : "Open");
    else if (!std::strcmp(Key, "Subprotocol")) lua_pushlstring(State, Socket->Subprotocol.data(), Socket->Subprotocol.size());
    else if (!std::strcmp(Key, "CloseCode")) { std::lock_guard<std::mutex> Lock(Socket->SnapshotMutex); if (Socket->CloseCode) lua_pushinteger(State, Socket->CloseCode); else lua_pushnil(State); }
    else if (!std::strcmp(Key, "CloseReason")) { std::lock_guard<std::mutex> Lock(Socket->SnapshotMutex); lua_pushlstring(State, Socket->CloseReason.data(), Socket->CloseReason.size()); }
    else if (!std::strcmp(Key, "ReceiveAsync")) lua_pushcfunction(State, ReceiveWebSocket, "WebSocket.ReceiveAsync");
    else if (!std::strcmp(Key, "SendAsync")) lua_pushcfunction(State, SendWebSocket, "WebSocket.SendAsync");
    else if (!std::strcmp(Key, "Close")) lua_pushcfunction(State, CloseWebSocket, "WebSocket.Close");
    else if (!std::strcmp(Key, "Cancel")) lua_pushcfunction(State, CancelWebSocket, "WebSocket.Cancel");
    else lua_pushnil(State); return 1;
}
struct WebSocketHandle { int Id; LuiRuntime* Runtime; };
static constexpr int WebSocketTag = 5;
static void DestroyWebSocket(lua_State*, void* Pointer) {
    const auto* Handle = static_cast<WebSocketHandle*>(Pointer); auto* Runtime = Handle->Runtime;
    if (!Runtime || Runtime->ShuttingDown || !Runtime->Network) return; auto* Network = Runtime->Network;
    auto At = Network->WebSockets.find(Handle->Id); if (At == Network->WebSockets.end()) return;
    auto Socket = At->second;
    // Posting can fail under memory exhaustion. In that case keep the bounded
    // owner root until whole-runtime teardown; never free a live worker socket.
    try { asio::post(Network->Io, WebSocketGuard(Socket, [Socket] { FinishWebSocket(Socket, "[LUI:WebSocket] Canceled"); })); Network->WebSockets.erase(At); }
    catch (...) {}
}
static int WebSocketText(lua_State* State) { CheckedWebSocket(State); lua_pushliteral(State, "WebSocket"); return 1; }
static void PushWebSocket(lua_State* State, const std::shared_ptr<WebSocketConnection>& Socket) {
    if (WebSocketFault(Socket->Reservation->State, 6)) luaL_error(State, "[LUI:WebSocket] ResourceExhausted");
    auto* Handle = static_cast<WebSocketHandle*>(lua_newuserdatatagged(State, sizeof(WebSocketHandle), WebSocketTag));
    Handle->Id = Socket->Id; Handle->Runtime = GetRuntime(State);
    lua_getfield(State, LUA_REGISTRYINDEX, "LuiWebSocketMeta"); lua_setmetatable(State, -2);
    Socket->OwnerPublished = true;
}
static void RegisterWebSocketTypes(lua_State* State) {
    lua_setuserdatadtor(State, WebSocketTag, DestroyWebSocket);
    luaL_newmetatable(State, "LuiWebSocketMeta");
    lua_pushcfunction(State, WebSocketIndex, "WebSocket.__index"); lua_setfield(State, -2, "__index");
    lua_pushcfunction(State, ReadOnly, "WebSocket.__newindex"); lua_setfield(State, -2, "__newindex");
    lua_pushcfunction(State, WebSocketText, "WebSocket.__tostring"); lua_setfield(State, -2, "__tostring");
    lua_pushliteral(State, "WebSocket"); lua_setfield(State, -2, "__metatable"); lua_setreadonly(State, -1, true); lua_pop(State, 1);
}
void PushWebSocketService(lua_State* State) {
    lua_newtable(State); lua_pushcfunction(State, ConnectWebSocket, "WebSocketService.ConnectAsync"); lua_setfield(State, -2, "ConnectAsync");
    lua_setreadonly(State, -1, true);
}
