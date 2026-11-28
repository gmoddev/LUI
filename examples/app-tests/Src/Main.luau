--!strict
local Spec = require("@Spec/Spec")
local Encoding = require("@Encoding/Encoding")
local Binary = require("@Binary/Binary")
local Scratch: {string} = {}
local Results = Spec.Run({
    {Name = "binary receipt", Run = function(Defer)
        Defer(function() table.clear(Scratch) end)
        local Header = Binary.Writer(2)
        Header.U8(0)
        Header.U8(255)
        table.insert(Scratch, Encoding.Base64Encode(Header.Finish()))
        assert(Scratch[1] == "AP8=")
    end},
    {Name = "fixture retired", Run = function() assert(#Scratch == 0) end},
})
for _, Result in Results do
    assert(Result.Passed, "[LUI:AppTests] " .. Result.Name)
end
print("[LUI:AppTests] Passed " .. tostring(#Results) .. " cases")
