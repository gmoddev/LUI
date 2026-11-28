--!strict
task.spawn(function()
    local Token = app:GetService("HttpService"):CreateCancellationToken()
    local Socket = app:GetService("WebSocketService"):ConnectAsync("ws://127.0.0.1:18081/echo", {
        ConnectTimeoutMs = 2000,
        CloseTimeoutMs = 1000,
        PingIntervalMs = 30000,
        PongTimeoutMs = 5000,
        MaxMessageBytes = 65536,
        MaxFrameBytes = 65536,
        MaxQueuedBytes = 262144,
        MaxQueuedMessages = 8,
        Protocols = {"lui.echo.v1"},
        CancellationToken = Token,
    })
    assert(Socket.State == "Open" and Socket.Subprotocol == "lui.echo.v1")
    local Bytes = buffer.fromstring("Binary\000payload\255")
    Socket:SendAsync(Bytes)
    local Message = Socket:ReceiveAsync()
    assert(Message and Message.Binary and Message.Data == "Binary\000payload\255")
    Socket:Close(1000, "Finished")
    assert(Socket:ReceiveAsync() == nil)
    assert(Socket.State == "Closed" and Socket.CloseCode == 1000)
    print("[LUI:Example] Advanced WebSocket passed")
end)
