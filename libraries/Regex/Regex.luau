--!strict
local Regex = {}
export type Span = {First: number, Last: number}
export type Match = {First: number, Last: number, Captures: {[number]: Span}}
export type Pattern = {
    read Find: (Self: Pattern, Text: string, Start: number?) -> Match?,
    read Matches: (Self: Pattern, Text: string, Start: number?) -> {Match},
    read Replace: (Self: Pattern, Text: string, Replacement: string) -> string,
    read Split: (Self: Pattern, Text: string) -> {string},
    read Close: (Self: Pattern) -> (),
}
function Regex.Compile(Expression: string): Pattern
    local Service = app:GetService("Regex")
    local Id: number? = Service:Compile(Expression)
    local function Search(Text: string, Start: number?, All: boolean): {Match}
        local Packet = buffer.fromstring(Service:Search(assert(Id, "[LUI:Regex] Closed"), Text, Start or 1, All))
        local Groups = buffer.readu8(Packet, 0)
        local List: {Match} = {}
        for Offset = 1, buffer.len(Packet) - 1, Groups * 4 do
            local Captures: {[number]: Span} = {}
            for Group = 1, Groups - 1 do
                local At = Offset + 4 * Group
                local First = buffer.readu16(Packet, At)
                if First ~= 65535 then
                    Captures[Group] = {First = First + 1, Last = buffer.readu16(Packet, At + 2)}
                end
            end
            table.insert(List, {First = buffer.readu16(Packet, Offset) + 1, Last = buffer.readu16(Packet, Offset + 2), Captures = Captures})
        end
        return List
    end
    return table.freeze({
        Find = function(_Self: Pattern, Text: string, Start: number?): Match? return Search(Text, Start, false)[1] end,
        Matches = function(_Self: Pattern, Text: string, Start: number?): {Match} return Search(Text, Start, true) end,
        Replace = function(_Self: Pattern, Text: string, Replacement: string): string
            assert(#Replacement <= 65536, "[LUI:Regex] OutputLimit")
            local Parts: {string} = {}
            local At, Size = 1, 0
            for _, Hit in Search(Text, 1, true) do
                Size += Hit.First - At + #Replacement
                assert(Size <= 65536, "[LUI:Regex] OutputLimit")
                table.insert(Parts, string.sub(Text, At, Hit.First - 1))
                table.insert(Parts, Replacement)
                At = Hit.Last + 1
            end
            assert(Size + #Text - At + 1 <= 65536, "[LUI:Regex] OutputLimit")
            table.insert(Parts, string.sub(Text, At))
            return table.concat(Parts)
        end,
        Split = function(_Self: Pattern, Text: string): {string}
            local Parts: {string} = {}
            local At = 1
            for _, Hit in Search(Text, 1, true) do
                table.insert(Parts, string.sub(Text, At, Hit.First - 1))
                At = Hit.Last + 1
            end
            table.insert(Parts, string.sub(Text, At))
            return Parts
        end,
        Close = function(_Self: Pattern)
            if Id then Service:Release(Id); Id = nil end
        end,
    }) :: Pattern
end
return table.freeze(Regex)
