--!strict
-- HTTP, a lazy optional library and WebSocket share one runtime and authority.
task.spawn(function()
    local Response = app:GetService("HttpService"):GetAsync("http://127.0.0.1:18081/config")
    assert(Response.Success)
    local Json = require("@Json/Json")
    local Config: any = Json.Decode(Response.Body)
    assert(Config.Protocol == "lui.echo.v1")
    local Socket = app:GetService("WebSocketService"):ConnectAsync("ws://127.0.0.1:18081/echo", {
        Protocols = {Config.Protocol},
    })
    Socket:SendAsync(Json.Encode({Message = "Hello", Ready = true}))
    local Message = Socket:ReceiveAsync()
    assert(Message and not Message.Binary)
    local Reply: any = Json.Decode(Message.Data)
    assert(Reply.Message == "Hello" and Reply.Ready == true)
    Socket:Close()
    assert(Socket:ReceiveAsync() == nil)
    print("[LUI:Example] Headless HTTP Json and WebSocket passed")
end)
