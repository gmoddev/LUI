--!strict
local SQLite = require("@SQLite/SQLite")
local Storage = app:GetService("StorageService")
local Window = Instance.new("Window", {Title = "Inventory Desk", Size = UDim2.fromOffset(650, 460)})
local Root = Instance.new("Frame", {Parent = Window, Size = UDim2.fromScale(1, 1)})
local Name = Instance.new("TextBox", {Parent = Root, Name = "Item", PlaceholderText = "Item name",
    AccessibilityLabel = "Item name", Position = UDim2.fromOffset(18, 18), Size = UDim2.new(1, -148, 0, 36), MaxLength = 80})
local Add = Instance.new("TextButton", {Parent = Root, Text = "Save item",
    Position = UDim2.new(1, -112, 0, 18), Size = UDim2.fromOffset(94, 36)})
local Search = Instance.new("TextBox", {Parent = Root, Name = "Search", PlaceholderText = "Search inventory",
    AccessibilityLabel = "Search inventory", Position = UDim2.fromOffset(18, 67), Size = UDim2.new(1, -36, 0, 36), MaxLength = 80})
local List = Instance.new("TextLabel", {Parent = Root, Name = "Results", Text = "", TextWrapped = true,
    TextXAlignment = "Left", TextYAlignment = "Top", Position = UDim2.fromOffset(18, 118), Size = UDim2.new(1, -36, 1, -169)})
local Status = Instance.new("TextLabel", {Parent = Root, Name = "Status", Text = "Opening inventory…",
    TextXAlignment = "Left", Position = UDim2.new(0, 18, 1, -48), Size = UDim2.new(1, -36, 0, 32)})
local Alive, Busy = true, true
local Database: SQLite.Connection? = nil
local File: NativeHandle? = nil
local function Refresh()
    local Db = Database
    if Busy or not Db then return end
    Busy = true
    local Query = string.lower(Search.Text)
    Db:Execute("SELECT name FROM items ORDER BY name LIMIT 256", {}, function(Data, Failure)
        if not Alive then return end
        Busy = false
        if not Data or Failure then Status.Text = "Inventory query failed."; return end
        if string.lower(Search.Text) ~= Query then Refresh(); return end
        local Entries: {string} = {}
        for _, Item in Data[1].Rows do
            local Value = tostring(Item[1])
            if string.find(string.lower(Value), Query, 1, true) then table.insert(Entries, Value) end
            if #Entries >= 12 then break end
        end
        List.Text = if #Entries == 0 then "No matching items." else table.concat(Entries, "\n")
        Status.Text = "Inventory ready"
    end)
end
Add.Activated:Connect(function()
    local Db, Handle = Database, File
    if Busy or not Db or not Handle then return end
    local Input = string.match(Name.Text, "^%s*(.-)%s*$")
    if not Input or #Input == 0 then Status.Text = "Enter an item name."; return end
    Busy = true
    Db:Execute("INSERT OR IGNORE INTO items(name) VALUES (?)", {Input}, function(Data, Failure)
        if not Alive then return end
        if not Data or Failure then Busy = false; Status.Text = "The item could not be saved."; return end
        Storage:Write(Db:Snapshot(), Handle, function(_, _, Why)
            if not Alive then return end
            Busy = false
            if Why then Status.Text = "The database could not be written."; return end
            Name.Text = ""
            Refresh()
        end)
    end)
end)
Search.TextChanged:Connect(Refresh)
Window.Destroying:Connect(function()
    Alive = false
    if Database then Database:Close() end
    if File then File:Close() end
end)
Window.Visible = true
Storage:Open(buffer.fromstring("inventory"), nil, function(_, Handle, Error)
    if not Alive then if Handle then Handle:Close() end; return end
    if not Handle or Error then Status.Text = "Inventory storage is unavailable."; return end
    File = Handle
    Storage:Read(buffer.create(0), Handle, function(Image, _, Failure)
        if not Alive then return end
        if Failure then Status.Text = "Inventory could not be read."; return end
        local Db = SQLite.Open(Image)
        Database = Db
        Db:Execute("CREATE TABLE IF NOT EXISTS items(name TEXT NOT NULL UNIQUE)", {}, function(Results)
            if not Alive then return end
            Busy = false
            if Results then Refresh() else Status.Text = "Inventory could not be opened." end
        end)
    end)
end)
