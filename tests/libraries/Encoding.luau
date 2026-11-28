--!strict
local Encoding = require("@Encoding/Encoding")
local Vectors: {[string]: string} = {
    [""] = "", f = "Zg==", fo = "Zm8=", foo = "Zm9v", foob = "Zm9vYg==",
    fooba = "Zm9vYmE=", foobar = "Zm9vYmFy", ["\0\255"] = "AP8=",
}
for Bytes, Text in Vectors do
    assert(Encoding.Base64Encode(Bytes) == Text and Encoding.Base64Decode(Text) == Bytes)
end
local Bytes = {}
for Byte = 0, 255 do table.insert(Bytes, string.char(Byte)) end
local All = table.concat(Bytes)
assert(Encoding.HexDecode(Encoding.HexEncode(All)) == All)
assert(Encoding.UrlDecode(Encoding.UrlEncode(All)) == All)
assert(Encoding.Base64Decode(Encoding.Base64Encode(All)) == All)
assert(Encoding.HexEncode("\255\0") == "FF00" and Encoding.HexDecode("Ff00") == "\255\0")
assert(Encoding.UrlEncode("~ a+b/") == "~%20a%2Bb%2F" and Encoding.UrlDecode("+%2b") == "++")
for _, Bad in {"a", "====", "A===", "AB==", "AAB=", "Zm=8", "Zm9v\n", "__8=", "Zg==AAAA", " Zg="} do
    assert(not pcall(Encoding.Base64Decode, Bad))
end
assert(not pcall(Encoding.HexDecode, "0") and not pcall(Encoding.HexDecode, "gg"))
assert(not pcall(Encoding.UrlDecode, "%") and not pcall(Encoding.UrlDecode, "%AZ"))
local Full = string.rep("a", 65536)
assert(Encoding.Base64Decode(Encoding.Base64Encode(Full)) == Full)
assert(not pcall(Encoding.HexEncode, Full .. "x"))
assert(not pcall(Encoding.Base64Decode, string.rep("A", 87384)))
assert(not pcall(Encoding.UrlDecode, Full .. "a"))
assert(table.isfrozen(Encoding))
