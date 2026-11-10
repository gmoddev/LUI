const Window = 'local Window = Instance.new("Window", {\n    Title = "Hello, LUI",\n    Size = UDim2.fromOffset(520, 320),\n})\n';
const Button = 'local Button = Instance.new("TextButton", {\n    Parent = Window,\n    Text = "Say hello",\n    Size = UDim2.fromOffset(180, 44),\n    Position = UDim2.fromOffset(24, 24),\n})\n';
const Event = 'Button.Activated:Connect(function()\n    Button.Text = "Hello, world!"\nend)\n';
const Component = `return function(Parent: Window | Frame, Name: string, Text: string, Callback: () -> ()): TextButton
    local Button = Instance.new("TextButton", {
        Parent = Parent,
        Name = Name,
        Text = Text,
        Size = UDim2.fromOffset(190, 44),
    })
    Button.Activated:Connect(Callback)
    return Button
end
`;
const Root = `    local Root = Instance.new("Frame", {
        Parent = Parent,
        Name = "Home",
        Size = UDim2.new(1, -32, 1, -100),
        Position = UDim2.fromOffset(16, 88),
    })
    Instance.new("UIListLayout", { Parent = Root, Padding = UDim.new(0, 12) })
`;
function Main(Screens, State) {
  return `${State ? 'local State = require("./State")\n' : ''}${Screens ? 'local Button = require("./Components/Button")\n' : ''}local Home = require("./Screens/Home")
${Screens ? 'local Library = require("./Screens/Library")\n' : ''}local Window = Instance.new("Window", {
    Name = "ProjectLibrary",
    Title = "LUI project library",
    Size = UDim2.fromOffset(760, 560),
})
${Screens ? `local Navigation = Instance.new("Frame", {
    Parent = Window,
    Name = "Navigation",
    Position = UDim2.fromOffset(16, 16),
    Size = UDim2.new(1, -32, 0, 60),
})
Instance.new("UIListLayout", {
    Parent = Navigation,
    FillDirection = "Horizontal",
    Padding = UDim.new(0, 12),
})
local Current: Frame? = nil
local function Show(Screen: string)
    if Current then Current:Destroy() end
    Current = if Screen == "Home" then Home(Window) else Library(Window)
end
Button(Navigation, "ShowHome", "Home", function() Show("Home") end)
Button(Navigation, "ShowLibrary", "Library", function() Show("Library") end)
Show("Home")` : 'Home(Window)'}
Window.Visible = true
`;
}
function Home(HasComponent, State) {
  return `${State ? 'local State = require("../State")\n' : ''}${HasComponent ? 'local Button = require("../Components/Button")\n' : ''}return function(Parent: Window): Frame
${Root}    Instance.new("TextLabel", {
        Parent = Root,
        Text = "My project library",
        Size = UDim2.fromOffset(550, 48),
    })
${HasComponent ? `    local Status = Instance.new("TextLabel", {
        Parent = Root,
        Name = "CounterStatus",
        Text = ${State ? '"Actions: " .. State.Count' : '"Ready"'},
        Size = UDim2.fromOffset(550, 38),
    })
    Button(Root, "Increment", "Add action", function()
${State ? '        State.Count += 1\n        Status.Text = "Actions: " .. State.Count' : '        Status.Text = "Action added"'}
    end)
` : ''}    return Root
end
`;
}
function Library(State) {
  return `${State ? 'local State = require("../State")\n' : ''}local Button = require("../Components/Button")
return function(Parent: Window): Frame
${Root.replace('Name = "Home"', 'Name = "Library"')}    Instance.new("TextLabel", {
        Parent = Root,
        Text = "Choose a project",
        Size = UDim2.fromOffset(550, 48),
    })
    local Status = Instance.new("TextLabel", {
        Parent = Root,
        Name = "Selection",
        Text = ${State ? 'if State.Selected == "" then "Select a project" else "Selected: " .. State.Selected' : '"Select a project"'},
        Size = UDim2.fromOffset(550, 38),
    })
    for _, Name in { "Aurora tools", "Map viewer", "Workshop" } do
        Button(Root, "Project_" .. Name, Name, function()
${State ? '            State.Selected = Name\n' : ''}            Status.Text = "Selected: " .. Name
        end)
    end
    return Root
end
`;
}
export function Checkpoints() {
  const Final = { 'Main.luau': Main(true, true), 'Components/Button.luau': Component, 'Screens/Home.luau': Home(true, true), 'Screens/Library.luau': Library(true), 'State.luau': 'local State = { Count = 0, Selected = "" }\nreturn State\n' };
  return [
    { Name: 'First-01-Window', Title: 'A window', Files: { 'Main.luau': Window + 'Window.Visible = true\n' } },
    { Name: 'First-02-Button', Title: 'A button', Files: { 'Main.luau': Window + Button + 'Window.Visible = true\n' } },
    { Name: 'First-03-Event', Title: 'Your first complete app', Files: { 'Main.luau': Window + Button + Event + 'Window.Visible = true\n' } },
    { Name: 'Advanced-01-Workspace', Title: 'A workspace', Files: { 'Main.luau': 'local Window = Instance.new("Window", { Title = "LUI project library", Size = UDim2.fromOffset(760, 560) })\nlocal Content = Instance.new("Frame", { Parent = Window, Position = UDim2.fromOffset(16, 88), Size = UDim2.new(1, -32, 1, -100) })\nInstance.new("TextLabel", { Parent = Content, Text = "My project library" })\nWindow.Visible = true\n' } },
    { Name: 'Advanced-02-Modules', Title: 'Declared modules', Files: { 'Main.luau': Main(false, false), 'Screens/Home.luau': Home(false, false) } },
    { Name: 'Advanced-03-Component', Title: 'A reusable button', Files: { 'Main.luau': Main(false, false), 'Screens/Home.luau': Home(true, false), 'Components/Button.luau': Component } },
    { Name: 'Advanced-04-Screens', Title: 'Two screens', Files: { 'Main.luau': Main(true, false), 'Screens/Home.luau': Home(true, false), 'Screens/Library.luau': Library(false), 'Components/Button.luau': Component } },
    { Name: 'Advanced-05-State', Title: 'Shared state', Files: Final },
    { Name: 'Advanced-06-Package', Title: 'The complete project library', Files: { ...Final } },
  ];
}
export function ProjectFiles(Checkpoint, Release) {
  const Manifest = { SchemaVersion: 2, Script: 'Src/Main.luau', Capabilities: [], Extensions: [], Assets: [], ExtensionDeclarations: [], Diagnostics: { Mode: 'Development' }, Modules: { Root: 'Src', Files: Object.keys(Checkpoint.Files).sort(), Requires: [], Dependencies: [] } };
  const Files = new Map(Object.entries(Checkpoint.Files).map(([Name, Text]) => ['Src/' + Name, Text]));
  Files.set('lui.json', JSON.stringify(Manifest, null, 2) + '\n');
  Files.set('LuiToolchain.json', JSON.stringify({ SchemaVersion: 1, SdkVersion: Release.SdkVersion, SdkManifestSha256: Release.SdkManifestSha256 }, null, 2) + '\n');
  Files.set('README.md', '# ' + Checkpoint.Title + '\n\nOpen this folder in VS Code and choose the LUI SDK ' + Release.SdkVersion + '. Use LUI: Check Types, then LUI: Run App. Stop and rerun after edits.\n\nGuide: https://gmoddev.github.io/LUI/docs/sdk-' + Release.SdkVersion + '/learn/' + (Checkpoint.Name.startsWith('First') ? 'first-project/' : 'advanced-project/') + '\n\nThis is a local example. State is session-only. Keep the whole project together.\n');
  return Files;
}
