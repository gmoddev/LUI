--!strict
local Toml = require("@Toml/Toml")
local Stdio = require("@Stdio/Stdio")
local Parts: {string} = {}
local Size = 0
Stdio.SetExitStatus(2)
local function Read()
    Stdio.Read(4096, function(Status, Bytes)
        if Status == "Eof" then
            local Settings = Toml.Settings(table.concat(Parts), {Name="LUI", Copies=1})
            local Count = Settings.Copies :: number
            assert(Count == math.floor(Count) and Count >= 0 and Count <= 8)
            local Name = Settings.Name :: string
            assert(#Name <= 128 and not string.find(Name, "[\r\n]"))
            Stdio.Write(string.rep("Hello " .. Name .. "\n", Count), function(Written)
                if Written == "Ok" then Stdio.SetExitStatus(0) end
            end)
        elseif Status == "Ok" then
            Size += #Bytes
            if Size <= 32768 then table.insert(Parts, Bytes); Read() end
        end
    end)
end
Read()
