-- A local, Modrinth-inspired launcher dashboard for exercising LUI's current UI surface.
-- All projects and actions are simulated; this example does not contact Modrinth or launch Minecraft.

local Projects = {
    {Name = "Aurora Valley", Kind = "Modpack", Version = "1.21.1", Downloads = "2.4M", Rank = 1, Updated = 4,
        Summary = "A mellow pack for exploring biomes and ruins.", Installed = true, Favorite = true},
    {Name = "Create: Workshop", Kind = "Modpack", Version = "1.20.1", Downloads = "1.8M", Rank = 2, Updated = 6,
        Summary = "Moving machines for your growing factory.", Installed = true, Favorite = false},
    {Name = "Overworld Atlas", Kind = "Mod", Version = "1.21.1", Downloads = "950K", Rank = 3, Updated = 1,
        Summary = "Waypoints and maps for long journeys.", Installed = false, Favorite = false},
    {Name = "Soft Lanterns", Kind = "Resource pack", Version = "1.21.1", Downloads = "740K", Rank = 4, Updated = 2,
        Summary = "Warm textures and lighting for cozy builds.", Installed = false, Favorite = false},
    {Name = "Copper Trails", Kind = "Mod", Version = "1.21.1", Downloads = "620K", Rank = 5, Updated = 3,
        Summary = "Useful copper tools, rails, and decor.", Installed = false, Favorite = false},
    {Name = "Skyward Isles", Kind = "Modpack", Version = "1.20.1", Downloads = "510K", Rank = 6, Updated = 5,
        Summary = "Build a sky island and unlock a new world.", Installed = false, Favorite = false},
}

local Section = "Play"
local Query = ""
local KindFilter = "All"
local SortMode = "Popular"
local PageIndex = 1
local DetailIndex = 1
local MemoryGb = 6
local AutoUpdate = true
local Pending = {}
local CurrentPage = nil
local RenderPending = false
local RenderPage
local Notice

local Window = Instance.new("Window", {
    Name = "DashboardWindow",
    Title = "LUI Playground - Launcher Dashboard",
    Size = UDim2.fromOffset(1180, 760),
})
Instance.new("UISizeConstraint", {MinSize = Vector2.new(1100, 700), Parent = Window})

local Root = Instance.new("Frame", {Name = "DashboardRoot", Size = UDim2.fromScale(1, 1), Parent = Window})
local Sidebar = Instance.new("Frame", {
    Name = "Sidebar", Size = UDim2.new(0, 220, 1, 0), Parent = Root,
})
local Main = Instance.new("Frame", {
    Name = "Main", Position = UDim2.fromOffset(220, 0), Size = UDim2.new(1, -220, 1, 0), Parent = Root,
})

local function AddLabel(Parent, Name, Text, X, Y, Width, Height)
    return Instance.new("TextLabel", {
        Name = Name, Text = Text, Position = UDim2.fromOffset(X, Y),
        Size = UDim2.fromOffset(Width, Height), Parent = Parent,
    })
end

local function AddButton(Parent, Name, Text, X, Y, Width, Height, Action)
    local Button = Instance.new("TextButton", {
        Name = Name, Text = Text, AccessibilityLabel = Text,
        Position = UDim2.fromOffset(X, Y), Size = UDim2.fromOffset(Width, Height), Parent = Parent,
    })
    Button.Activated:Connect(Action)
    return Button
end

local function SetNotice(Message)
    Notice.Text = Message
    print("[LUI:Dashboard] " .. Message)
end

local function QueueRender()
    if RenderPending then return end
    RenderPending = true
    task.defer(function()
        RenderPending = false
        RenderPage()
    end)
end

AddLabel(Sidebar, "Brand", "◆  LUI PLAYGROUND\n     launcher dashboard", 18, 18, 190, 65)
AddLabel(Sidebar, "NavigationHeading", "WORKSPACE", 18, 99, 190, 24)

local Navigation = {}
for Index, Name in {"Play", "Discover", "Downloads", "Settings"} do
    Navigation[Name] = AddButton(Sidebar, "Navigate" .. Name, Name, 18, 132 + (Index - 1) * 50, 184, 42, function()
        Section = Name
        QueueRender()
    end)
end

AddLabel(Sidebar, "SidebarNote", "LOCAL DEMO\nNo game files are changed.\nNo network requests are made.",
    18, 365, 190, 82)
AddLabel(Sidebar, "SidebarFooter", "LUI Foundation 2\nNative WinUI controls", 18, 646, 190, 55)

local Heading = AddLabel(Main, "Heading", "Play", 30, 17, 820, 35)
local Subheading = AddLabel(Main, "Subheading", "Your local launcher workspace", 30, 51, 820, 25)
local Search = Instance.new("TextBox", {
    Name = "ProjectSearch", AccessibilityLabel = "Search projects",
    AccessibilityDescription = "Type to filter the mock project catalog",
    Position = UDim2.fromOffset(30, 87), Size = UDim2.fromOffset(330, 38), Parent = Main,
})
local FilterButtons = {}
for Index, Kind in {"All", "Modpacks", "Mods", "Packs"} do
    FilterButtons[Kind] = AddButton(Main, "Filter" .. Kind, Kind,
        370 + (Index - 1) * 90, 87, 84, 38, function()
            KindFilter = Kind
            PageIndex = 1
            Section = "Discover"
            QueueRender()
        end)
end
local SortButton = AddButton(Main, "SortProjects", "Sort: Popular", 738, 87, 158, 38, function()
    SortMode = SortMode == "Popular" and "Recently updated" or SortMode == "Recently updated" and "Name" or "Popular"
    PageIndex = 1
    QueueRender()
end)

local Body = Instance.new("Frame", {
    Name = "PageBody", Position = UDim2.fromOffset(30, 143),
    Size = UDim2.new(1, -60, 1, -205), Parent = Main,
})
Notice = Instance.new("TextLabel", {
    Name = "Status", Text = "Demo data • explore the controls",
    Position = UDim2.new(0, 30, 1, -42), Size = UDim2.fromOffset(840, 30), Parent = Main,
})

Search.TextChanged:Connect(function()
    Query = Search.Text
    PageIndex = 1
    Section = "Discover"
    QueueRender()
end)

local function GetMatches()
    local Matches = {}
    local LowerQuery = string.lower(Query)
    for Index, Project in Projects do
        local MatchesKind = KindFilter == "All" or
            (KindFilter == "Modpacks" and Project.Kind == "Modpack") or
            (KindFilter == "Mods" and Project.Kind == "Mod") or
            (KindFilter == "Packs" and Project.Kind == "Resource pack")
        local SearchText = string.lower(Project.Name .. " " .. Project.Kind .. " " .. Project.Summary)
        if MatchesKind and string.find(SearchText, LowerQuery, 1, true) then
            table.insert(Matches, Index)
        end
    end
    table.sort(Matches, function(Left, Right)
        local A = Projects[Left]
        local B = Projects[Right]
        if SortMode == "Name" then return A.Name < B.Name end
        if SortMode == "Recently updated" then return A.Updated < B.Updated end
        return A.Rank < B.Rank
    end)
    return Matches
end

local function BeginInstall(Index)
    if Pending[Index] then return end
    local Project = Projects[Index]
    if Project.Installed then
        Project.Installed = false
        SetNotice(Project.Name .. " removed from the demo library")
        QueueRender()
        return
    end
    Pending[Index] = 0
    Section = "Downloads"
    SetNotice("Installing " .. Project.Name .. " (simulated)")
    QueueRender()
    local function Advance()
        if Pending[Index] == nil then return end
        Pending[Index] += 20
        if Pending[Index] >= 100 then
            Pending[Index] = nil
            Project.Installed = true
            SetNotice(Project.Name .. " is ready in your demo library")
        else
            task.delay(0.35, Advance)
        end
        QueueRender()
    end
    task.delay(0.35, Advance)
end

local function RenderPlay(Page)
    AddLabel(Page, "PlayHeading", "READY TO PLAY", 0, 0, 500, 28)
    AddButton(Page, "CreateInstance", "＋  Create an instance\nBrowse a starter pack or build your own profile",
        0, 40, 820, 92, function()
            Section = "Discover"
            SetNotice("Choose a project to start a new demo instance")
            QueueRender()
        end)
    AddLabel(Page, "InstancesHeading", "YOUR INSTANCES", 0, 155, 500, 28)
    local Slot = 0
    for Index, Project in Projects do
        if Project.Installed and Slot < 2 then
            local CardIndex = Index
            AddButton(Page, "Instance" .. Index,
                "◆  " .. Project.Name .. "\n" .. Project.Kind .. "  •  Minecraft " .. Project.Version ..
                "\nPLAY  →", Slot * 420, 195, 400, 142, function()
                    SetNotice("Launching " .. Projects[CardIndex].Name .. " (demo only)")
                end)
            Slot += 1
        end
    end
    if Slot == 0 then
        AddLabel(Page, "EmptyLibrary", "Your demo library is empty. Open Discover to install a project.",
            0, 195, 800, 100)
    end
    AddLabel(Page, "QuickActions", "QUICK ACTIONS", 0, 365, 500, 28)
    AddButton(Page, "BrowseProjects", "Discover projects", 0, 405, 258, 58, function()
        Section = "Discover"
        QueueRender()
    end)
    AddButton(Page, "ViewDownloads", "View downloads", 280, 405, 258, 58, function()
        Section = "Downloads"
        QueueRender()
    end)
    AddButton(Page, "OpenSettings", "Launcher settings", 560, 405, 258, 58, function()
        Section = "Settings"
        QueueRender()
    end)
end

local function RenderDiscover(Page)
    local Matches = GetMatches()
    local TotalPages = math.max(1, math.ceil(#Matches / 4))
    if PageIndex > TotalPages then PageIndex = TotalPages end
    AddLabel(Page, "Results", tostring(#Matches) .. (#Matches == 1 and " project" or " projects") ..
        "  •  " .. KindFilter .. "  •  " .. SortMode,
        0, 0, 780, 28)
    if #Matches == 0 then
        AddLabel(Page, "NoResults", "No matching projects. Try another search or category.",
            0, 75, 810, 100)
    end
    for Slot = 1, 4 do
        local Index = Matches[(PageIndex - 1) * 4 + Slot]
        if Index then
            local Project = Projects[Index]
            local X = ((Slot - 1) % 2) * 420
            local Y = math.floor((Slot - 1) / 2) * 174 + 38
            local Badge = Project.Installed and "INSTALLED" or Pending[Index] and "INSTALLING" or "VIEW PROJECT"
            AddButton(Page, "Project" .. Index,
                "◆  " .. Project.Name .. "\n" .. Project.Kind .. "  •  MC " .. Project.Version ..
                "  •  " .. Project.Downloads .. " downloads\n" .. Project.Summary .. "\n" .. Badge,
                X, Y, 400, 160, function()
                    DetailIndex = Index
                    Section = "Detail"
                    QueueRender()
                end)
        end
    end
    AddButton(Page, "PreviousPage", "← Previous", 0, 400, 150, 42, function()
        PageIndex = math.max(1, PageIndex - 1)
        QueueRender()
    end)
    AddLabel(Page, "PageNumber", "Page " .. PageIndex .. " of " .. TotalPages, 355, 409, 150, 30)
    AddButton(Page, "NextPage", "Next →", 670, 400, 150, 42, function()
        PageIndex = math.min(TotalPages, PageIndex + 1)
        QueueRender()
    end)
end

local function RenderDetail(Page)
    local Project = Projects[DetailIndex]
    AddButton(Page, "BackToDiscover", "←  Back to Discover", 0, 0, 195, 42, function()
        Section = "Discover"
        QueueRender()
    end)
    AddLabel(Page, "ProjectTitle", "◆  " .. Project.Name, 0, 65, 810, 40)
    AddLabel(Page, "ProjectMeta", Project.Kind .. "  •  Minecraft " .. Project.Version ..
        "  •  " .. Project.Downloads .. " downloads", 0, 110, 810, 30)
    AddLabel(Page, "ProjectDescription", Project.Summary ..
        "\n\nThis is sample catalog data. Installing only runs a local progress animation.",
        0, 170, 810, 120)
    AddButton(Page, "InstallProject", Project.Installed and "Remove from library" or
        Pending[DetailIndex] and "Installation in progress" or "Install to library",
        0, 310, 320, 54, function()
            BeginInstall(DetailIndex)
        end)
    AddButton(Page, "FavoriteProject", Project.Favorite and "★  Saved project" or "☆  Save project",
        345, 310, 260, 54, function()
            Project.Favorite = not Project.Favorite
            SetNotice(Project.Name .. (Project.Favorite and " saved" or " removed from saved projects"))
            QueueRender()
        end)
    AddButton(Page, "ShowDownloads", "View downloads", 0, 390, 260, 50, function()
        Section = "Downloads"
        QueueRender()
    end)
end

local function RenderDownloads(Page)
    AddLabel(Page, "DownloadsHeading", "DOWNLOAD QUEUE", 0, 0, 810, 32)
    AddLabel(Page, "DownloadsNote", "Local simulation • no files are downloaded", 0, 36, 810, 28)
    local Slot = 0
    for Index, Project in Projects do
        if Pending[Index] then
            local Y = 82 + Slot * 100
            AddLabel(Page, "DownloadName" .. Index, Project.Name .. "  •  " .. Pending[Index] .. "%", 0, Y, 780, 30)
            Instance.new("ProgressBar", {
                Name = "DownloadProgress" .. Index, Minimum = 0, Maximum = 100, Value = Pending[Index],
                AccessibilityLabel = Project.Name .. " installation progress",
                Position = UDim2.fromOffset(0, Y + 35), Size = UDim2.fromOffset(820, 24), Parent = Page,
            })
            Slot += 1
        end
    end
    if Slot == 0 then
        AddLabel(Page, "NoDownloads", "Everything is up to date. Open Discover to try an installation.",
            0, 102, 810, 90)
        AddButton(Page, "DiscoverFromDownloads", "Discover projects", 0, 215, 260, 52, function()
            Section = "Discover"
            QueueRender()
        end)
    end
end

local function RenderSettings(Page)
    AddLabel(Page, "SettingsHeading", "LAUNCHER SETTINGS", 0, 0, 810, 35)
    AddLabel(Page, "SettingsNote", "Change these demo preferences to test native controls.", 0, 38, 810, 28)
    local MemoryLabel = AddLabel(Page, "MemoryLabel", "Allocated memory: " .. MemoryGb .. " GB",
        0, 93, 810, 30)
    local Memory = Instance.new("Slider", {
        Name = "MemorySlider",
        AccessibilityLabel = "Allocated memory in gigabytes",
        Position = UDim2.fromOffset(0, 126), Size = UDim2.fromOffset(530, 42), Parent = Page,
    })
    -- Set range limits before the value; a property table does not promise application order.
    Memory.Maximum = 16
    Memory.Minimum = 2
    Memory.Value = MemoryGb
    Memory.ValueChanged:Connect(function()
        MemoryGb = math.floor(Memory.Value + 0.5)
        MemoryLabel.Text = "Allocated memory: " .. MemoryGb .. " GB"
        SetNotice("Memory set to " .. MemoryGb .. " GB (demo only)")
    end)
    local Updates = Instance.new("CheckBox", {
        Name = "AutoUpdate", Text = "Automatically check for updates", Checked = AutoUpdate,
        Position = UDim2.fromOffset(0, 208), Size = UDim2.fromOffset(530, 42), Parent = Page,
    })
    Updates.CheckedChanged:Connect(function()
        AutoUpdate = Updates.Checked
        SetNotice("Automatic updates " .. (AutoUpdate and "enabled" or "disabled") .. " (demo only)")
    end)
    AddLabel(Page, "ThemeState", "System theme: " .. app:GetService("ThemeService").CurrentTheme,
        0, 286, 810, 32)
    AddLabel(Page, "SettingsFooter", "These values live in memory for this session. No settings files are written.",
        0, 348, 810, 62)
end

RenderPage = function()
    if CurrentPage then CurrentPage:Destroy() end
    CurrentPage = Instance.new("Frame", {
        Name = Section .. "Page", Size = UDim2.fromScale(1, 1), Parent = Body,
    })
    Heading.Text = Section == "Detail" and Projects[DetailIndex].Name or Section
    Subheading.Text = Section == "Play" and "Your local launcher workspace" or
        Section == "Discover" and "Find something new to play" or
        Section == "Downloads" and "See local demo install progress" or
        Section == "Settings" and "Tune your launcher preferences" or "Project details"
    for Name, Button in Navigation do
        Button.Text = (Name == Section or Section == "Detail" and Name == "Discover") and
            "◆  " .. Name or "     " .. Name
    end
    local DiscoverVisible = Section == "Discover"
    Search.Visible = DiscoverVisible
    SortButton.Visible = DiscoverVisible
    SortButton.Text = "Sort: " .. SortMode
    for Name, Button in FilterButtons do
        Button.Visible = DiscoverVisible
        Button.Text = Name == KindFilter and "◆ " .. Name or Name
    end
    if Section == "Play" then RenderPlay(CurrentPage)
    elseif Section == "Discover" then RenderDiscover(CurrentPage)
    elseif Section == "Detail" then RenderDetail(CurrentPage)
    elseif Section == "Downloads" then RenderDownloads(CurrentPage)
    else RenderSettings(CurrentPage) end
end

app:GetService("ThemeService").ThemeChanged:Connect(function(NewTheme)
    SetNotice("System theme changed to " .. NewTheme)
    if Section == "Settings" then QueueRender() end
end)

RenderPage()
Window.Visible = true
print("[LUI:Dashboard] Ready")
