--!strict
local Args = {}
export type Options = {[string]: "Flag" | "Value"}
export type Parsed = {Options: {[string]: string | boolean}, Positionals: {string}}
function Args.Parse(Words: {string}, Schema: Options): Parsed
    assert(type(Words) == "table" and getmetatable(Words) == nil and type(Schema) == "table" and getmetatable(Schema) == nil, "[LUI:Args] InvalidInput")
    local Count, Size = 0, 0
    for Key, Word in next, Words do
        Count += 1
        assert(type(Key) == "number" and Key % 1 == 0 and Key >= 1 and Key <= 64 and type(Word) == "string" and #Word <= 4096, "[LUI:Args] InvalidArguments")
        Size += #Word
        assert(Size <= 32768, "[LUI:Args] LimitExceeded")
    end
    local Declared = 0
    for Name, Kind in next, Schema do
        Declared += 1
        assert(Declared <= 64 and type(Name) == "string" and #Name <= 48 and string.match(Name, "^[A-Za-z][A-Za-z0-9%-]*$") and (Kind == "Flag" or Kind == "Value"), "[LUI:Args] InvalidSchema")
    end
    local Values: {[string]: string | boolean} = {}
    local Positionals: {string} = {}
    local Literal, Index = false, 1
    while Index <= Count do
        local Word = assert(rawget(Words, Index), "[LUI:Args] SparseArguments")
        if not Literal and Word == "--" then Literal = true
        elseif not Literal and string.sub(Word, 1, 2) == "--" then
            local Equals = string.find(Word, "=", 3, true)
            local Name = string.sub(Word, 3, if Equals then Equals - 1 else -1)
            local Kind = rawget(Schema, Name)
            assert(Kind and Values[Name] == nil, "[LUI:Args] UnknownOrRepeatedOption")
            if Kind == "Flag" then assert(not Equals, "[LUI:Args] UnexpectedValue"); Values[Name] = true
            elseif Equals then Values[Name] = string.sub(Word, Equals + 1)
            else
                Index += 1
                local Next = rawget(Words, Index)
                assert(Next and string.sub(Next, 1, 2) ~= "--", "[LUI:Args] MissingValue")
                Values[Name] = Next
            end
        else table.insert(Positionals, Word) end
        Index += 1
    end
    return {Options=Values, Positionals=Positionals}
end
return table.freeze(Args)
