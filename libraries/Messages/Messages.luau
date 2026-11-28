--!strict
local Json = require("@Json/Json")
local Messages = {}
local Limits: Json.Limits = {MaxBytes=65531, MaxNodes=2048, MaxDepth=16}
export type Mailbox = {
    read Send: (Self: Mailbox, Value: any) -> (boolean, string?),
    read Receive: (Self: Mailbox) -> (any, string?),
    read Usage: (Self: Mailbox) -> (number, number),
    read Close: (Self: Mailbox) -> (),
}
function Messages.Encode(Value: any): string
    if typeof(Value) == "buffer" then
        assert(buffer.len(Value) <= 65531, "[LUI:Messages] LimitExceeded")
        return "LUM1B" .. buffer.tostring(Value)
    end
    return "LUM1J" .. Json.Encode(Value, Limits)
end
function Messages.Decode(Packet: string): any
    assert(type(Packet) == "string" and #Packet >= 5 and #Packet <= 65536, "[LUI:Messages] InvalidPacket")
    local Tag = string.sub(Packet, 1, 5)
    if Tag == "LUM1B" then return buffer.fromstring(string.sub(Packet, 6)) end
    assert(Tag == "LUM1J", "[LUI:Messages] InvalidPacket")
    return Json.Decode(string.sub(Packet, 6), Limits)
end
function Messages.Mailbox(MaxMessages: number?, MaxBytes: number?): Mailbox
    local Slots = if MaxMessages == nil then 32 else MaxMessages
    local Budget = if MaxBytes == nil then 262144 else MaxBytes
    assert(type(Slots) == "number" and type(Budget) == "number" and Slots >= 1 and Slots <= 64 and Slots % 1 == 0 and Budget >= 5 and Budget <= 262144 and Budget % 1 == 0, "[LUI:Messages] InvalidLimit")
    local Queue: {[number]: string} = {}
    local Head, Count, Used, Closed = 1, 0, 0, false
    return table.freeze({
        Send = function(_Self: Mailbox, Value: any): (boolean, string?)
            if Closed then return false, "Closed" end
            if Count == Slots then return false, "Full" end
            local Packet = Messages.Encode(Value)
            if #Packet > Budget - Used then return false, "Full" end
            Queue[(Head + Count - 1) % Slots + 1] = Packet
            Count += 1; Used += #Packet
            return true, nil
        end,
        Receive = function(_Self: Mailbox): (any, string?)
            if Count == 0 then return nil, if Closed then "Closed" else "Empty" end
            local Packet = Queue[Head]
            local Value = Messages.Decode(Packet)
            Queue[Head] = nil; Head = Head % Slots + 1
            Count -= 1; Used -= #Packet
            return Value, nil
        end,
        Usage = function(_Self: Mailbox): (number, number) return Count, Used end,
        Close = function(_Self: Mailbox) Closed = true; table.clear(Queue); Count = 0; Used = 0 end,
    }) :: Mailbox
end
return table.freeze(Messages)
