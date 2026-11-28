--!strict
local Path = {Version = "0.1.0", RequiredCapabilities = table.freeze({})}
local MaxBytes = 4096

local function Split(Text: string): (string, string)
    if type(Text) ~= "string" or #Text > MaxBytes then error("[LUI:Path] InvalidLength", 0) end
    if string.find(Text, "\0", 1, true) or not utf8.len(Text) then error("[LUI:Path] InvalidText", 0) end
    local Value = string.gsub(Text, "\\", "/")
    if string.sub(Value, 1, 2) == "//" then
        local Server, Share, Tail = string.match(Value, "^//([^/]+)/+([^/]+)(.*)$")
        if not Server or not Share or Server == "." or Server == ".." or Server == "?" or Share == "." or Share == ".." then
            error("[LUI:Path] InvalidRoot", 0)
        end
        return "//" .. Server .. "/" .. Share .. "/", Tail or ""
    end
    if string.match(Value, "^[A-Za-z]:") then
        if string.sub(Value, 3, 3) ~= "/" then error("[LUI:Path] InvalidRoot", 0) end
        return string.upper(string.sub(Value, 1, 1)) .. ":/", string.sub(Value, 4)
    end
    if string.sub(Text, 1, 1) == "\\" then error("[LUI:Path] InvalidRoot", 0) end
    if string.sub(Value, 1, 1) == "/" then return "/", string.sub(Value, 2) end
    return "", Value
end

local function Parts(Text: string): (string, {string})
    local Root, Tail = Split(Text)
    local Segments: {string} = {}
    for Part in string.gmatch(Tail, "[^/]+") do
        if Part == ".." and #Segments > 0 and Segments[#Segments] ~= ".." then
            table.remove(Segments)
        elseif Part ~= "." and (Part ~= ".." or Root == "") then
            table.insert(Segments, Part)
        end
    end
    -- Removing a prefix must not turn a relative spelling into a drive path.
    if Root == "" and Segments[1] and string.match(Segments[1], "^[A-Za-z]:") then
        error("[LUI:Path] InvalidRoot", 0)
    end
    return Root, Segments
end

local function Render(Root: string, Segments: {string}): string
    local Text = Root .. table.concat(Segments, "/")
    if #Text > MaxBytes then error("[LUI:Path] InvalidLength", 0) end
    return if Text == "" then "." else Text
end

function Path.Normalize(Text: string): string
    return Render(Parts(Text))
end

function Path.Join(Base: string, Relative: string): string
    local Root, Segments = Parts(Base)
    local OtherRoot, Tail = Split(Relative)
    if OtherRoot ~= "" then error("[LUI:Path] RelativeRequired", 0) end
    local Prefix = Render(Root, Segments)
    if Tail == "" then return Prefix end
    if Prefix == "." then return Path.Normalize(Tail) end
    local Separator = if string.sub(Prefix, -1) == "/" then "" else "/"
    return Path.Normalize(Prefix .. Separator .. Tail)
end

function Path.Resolve(Base: string, Text: string): string
    local BaseRoot = Split(Base)
    if BaseRoot == "" then error("[LUI:Path] AbsoluteBaseRequired", 0) end
    local Root = Split(Text)
    return if Root == "" then Path.Join(Base, Text) else Path.Normalize(Text)
end

function Path.Parent(Text: string): string
    local Root, Segments = Parts(Text)
    if #Segments > 0 then table.remove(Segments) end
    return Render(Root, Segments)
end

function Path.Name(Text: string): string
    local _, Segments = Parts(Text)
    return Segments[#Segments] or ""
end

function Path.Extension(Text: string): string
    local Name = Path.Name(Text)
    local FromEnd = string.find(string.reverse(Name), ".", 1, true)
    return if FromEnd and FromEnd > 1 and FromEnd < #Name then string.sub(Name, -FromEnd) else ""
end

return table.freeze(Path)
