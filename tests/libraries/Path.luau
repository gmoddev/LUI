--!strict
local Path = require("@Path/Path")
local Cases: {[string]: string} = {
    [""] = ".", ["a/b/../../.."] = "..", ["../.././x"] = "../../x",
    ["/../../x//"] = "/x", ["c:\\tools\\..\\bin"] = "C:/bin",
    ["\\\\node\\share\\..\\x"] = "//node/share/x", ["//node/share"] = "//node/share/",
    ["folder/é"] = "folder/é", ["//node//share///file"] = "//node/share/file",
}
for Input, Expected in Cases do
    assert(Path.Normalize(Input) == Expected)
    assert(Path.Normalize(Expected) == Expected)
end
assert(Path.Join("a/b", "../c") == "a/c" and Path.Join("D:/", "x") == "D:/x")
assert(Path.Resolve("/a", "b/../c") == "/a/c" and Path.Resolve("/a", "c:/b") == "C:/b")
assert(Path.Name("//s/t") == "" and Path.Parent("//s/t") == "//s/t/")
assert(Path.Parent("file") == "." and Path.Parent("x/y") == "x")
assert(Path.Extension(".config") == "" and Path.Extension("x.") == "")
assert(Path.Extension("a.tar.gz") == ".gz" and Path.Extension(".local.json") == ".json")
assert(Path.Normalize(string.rep("x", 4096)) == string.rep("x", 4096))
for _, Invalid in {"//", "//host", "//?/C:/x", "//./pipe/x", "\\single", "D:x", "x/../D:y", "\0", "\255", string.rep("x", 4097)} do
    assert(not pcall(Path.Normalize, Invalid))
end
assert(not pcall(Path.Join, "/a", "/b") and not pcall(Path.Resolve, "a", "b"))
assert(table.isfrozen(Path) and #Path.RequiredCapabilities == 0)
