--!strict
local Json = require("@Json/Json")
local Stdio = require("@Stdio/Stdio")
local Storage = app:GetService("StorageService")
local Args = Stdio.Arguments()
local Port = tonumber(Args[1])
Stdio.SetExitStatus(1)
if not Port or Port % 1 ~= 0 or Port < 1024 or Port > 65535 then
    Stdio.SetExitStatus(2)
    Stdio.WriteError("[TicketService:Input] Supply a loopback port from 1024 to 65535.\n", function() end)
    return
end
Storage:Open(buffer.fromstring("tickets"), nil, function(_, Record, OpenFailure)
    if not Record or OpenFailure then return end
    Storage:Read(buffer.create(0), Record, function(Bytes, _, ReadFailure)
        if ReadFailure then Record:Close(); return end
        local Tickets: {string} = {}
        if Bytes and buffer.len(Bytes) > 0 then
            local Valid, Result = pcall(function() return Json.Decode(buffer.tostring(Bytes)) end)
            if not Valid or type(Result) ~= "table" or #Result > 64 then Record:Close(); return end
            for _, Ticket in Result do
                if type(Ticket) ~= "string" or #Ticket > 128 then Record:Close(); return end
                table.insert(Tickets, Ticket)
            end
        end
        local Server = app:GetService("HttpServerService"):CreateServer({Port = Port})
        local Pending = false
        local Headers: {HttpHeader} = {{Name = "Content-Type", Value = "application/json"}}
        Server:Route("GET", "/tickets", function()
            return {Headers = Headers, Body = Json.Encode(Json.Array(Tickets))}
        end)
        Server:Route("POST", "/tickets", function(Request)
            if Pending then return {StatusCode = 409, Body = "An update is already pending."} end
            if #Request.Body > 1024 then return {StatusCode = 413, Body = "Send at most 1 KiB."} end
            local Valid, Body = pcall(function() return Json.Decode(Request.Body) end)
            local Title = if Valid and type(Body) == "table" then Body.Title else nil
            if type(Title) ~= "string" or #Title < 1 or #Title > 128 then
                return {StatusCode = 400, Body = "Provide a title of 1 to 128 bytes."}
            end
            if #Tickets >= 64 then return {StatusCode = 409, Body = "The ticket collection is full."} end
            Pending = true
            local Next = table.clone(Tickets)
            table.insert(Next, Title)
            Storage:Write(buffer.fromstring(Json.Encode(Next)), Record, function(_, _, Failure)
                Pending = false
                if not Failure then Tickets = Next end
            end)
            return {StatusCode = 202, Headers = Headers, Body = Json.Encode({Accepted = true})}
        end)
        Server:Start()
        Stdio.Write("Ready " .. tostring(Port) .. "\n", function() end)
        Stdio.ReadLine(16, function()
            Server:Close()
            Record:Close()
            Stdio.SetExitStatus(0)
        end)
    end)
end)
