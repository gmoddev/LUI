--!strict
local Config = require("@Config/Config")
local Encoding = require("@Encoding/Encoding")
local Json = require("@Json/Json")
local Path = require("@Path/Path")
local Utc = require("@Utc/Utc")
local Inputs = require("./Inputs")

local Settings = Config.Decode(Inputs.Settings, {Base = "/assets"})
local Base = Settings.Base
assert(type(Base) == "string")
local Rows = {}
for _, Item in Inputs.Assets do
    local Name = Path.Name(Path.Resolve(Base, Item.File))
    table.insert(Rows, Json.Object({
        Name = Name, Extension = Path.Extension(Name),
        Link = "/download/" .. Encoding.UrlEncode(Name),
        Modified = Utc.Format(Utc.Parse(Item.Modified)),
        Tag = Encoding.Base64Encode(Encoding.HexDecode(Item.TagHex)),
    }))
end
print("[LUI:AssetReport] " .. Json.Encode(Json.Array(Rows)))
