const WindowCode = `local Window = Instance.new("Window", {
    Title = "LUI example", Size = UDim2.fromOffset(560, 280),
})
local Status = Instance.new("TextLabel", {
    Parent = Window, Text = "Ready", Position = UDim2.fromOffset(16, 170),
    Size = UDim2.new(1, -32, 0, 72),
})`;
function Ui(Code, Outcome, Options = {}) {
  return { Code: WindowCode + '\n' + Code + '\nWindow.Visible = true\n', Outcome, Capabilities: [], ...Options };
}
const ButtonCode = `local Button = Instance.new("TextButton", {
    Parent = Window, Text = "Try it", Size = UDim2.fromOffset(220, 44),
    Position = UDim2.fromOffset(16, 16),
})`;

export const Examples = {
  Window: { Code: `local Window = Instance.new("Window", {
    Title = "My app", Size = UDim2.fromOffset(560, 280),
})
Window.Visible = true
`, Outcome: 'A window titled My app opens. Visible starts false, so the last line matters.', Capabilities: [] },
  Instance: Ui(`local Panel = Instance.new("Frame", {Parent = Window, Name = "Content", Size = UDim2.fromOffset(260, 80)})
Instance.new("TextLabel", {Parent = Panel, Name = "Greeting", Text = "Hello", Size = UDim2.fromOffset(200, 40)})
local Greeting = Panel:FindFirstChild("Greeting")
if Greeting and Greeting:IsA("TextLabel") then
    Status.Text = "Found " .. Greeting.Name
end
local Copy = Panel:Clone()
Copy:Destroy() -- The original panel is still alive.`, 'The original greeting remains, and the status says Found Greeting. Destroying the copy does not destroy the original.'),
  GuiObject: Ui(ButtonCode + `
Button.AnchorPoint = Vector2.new(0.5, 0)
Button.Position = UDim2.new(0.5, 0, 0, 16)
Button.BackgroundColor3 = Color3.fromRGB(40, 100, 180)
Button.AccessibilityLabel = "Continue to the next screen"
Button.Activated:Connect(function() Status.Text = "Activated" end)`, 'A centered button appears. It inherits geometry, color and accessible naming from GuiObject. GuiObject itself cannot be created.'),
  Frame: Ui(`local Content = Instance.new("Frame", {
    Parent = Window, Position = UDim2.fromOffset(16, 16),
    Size = UDim2.new(1, -32, 0, 120),
})
Instance.new("TextLabel", {Parent = Content, Text = "Inside the frame", Size = UDim2.fromOffset(260, 44)})`, 'The label belongs to a frame with a 16-unit outer margin.'),
  TextLabel: Ui(`Status.Text = "Saved"
Status.TextColor3 = Color3.fromRGB(30, 140, 70)`, 'The status label displays Saved. Text is writable by your code even though the user cannot edit the label.'),
  TextButton: Ui(ButtonCode + `
Button.Text = "Continue"
Button.Activated:Connect(function()
    Button.Text = "Ready"
    Status.Text = "The button was activated"
end)`, 'Activate Continue. Its label changes to Ready and the status confirms the action.'),
  TextBox: Ui(`local Input = Instance.new("TextBox", {
    Parent = Window, PlaceholderText = "Enter a name", MaxLength = 80,
    Position = UDim2.fromOffset(16, 16), Size = UDim2.fromOffset(320, 44),
    AccessibilityLabel = "Name",
})
Input.TextChanged:Connect(function()
    Status.Text = "Name: " .. Input.Text
end)`, 'Typing updates the status from committed text. The callback reads Input.Text; it does not receive the new string as an argument.'),
  CodeEditor: Ui(`local Editor = Instance.new("CodeEditor", {
    Parent = Window, Text = 'print("Hello")', Size = UDim2.fromOffset(520, 130),
    Position = UDim2.fromOffset(16, 16),
})
Editor.TextChanged:Connect(function()
    Status.Text = "Text revision: " .. Editor.TextRevision
end)
local Styled = Editor:SetStyles(Editor.TextRevision, {{Start = 0, End = 5, Color = 0x3366CC}})
assert(Styled == false) -- Public styling is not admitted in this SDK.`, 'A plain multiline editor appears. Editing advances TextRevision. The example deliberately shows the current refusal of public styling.', {Limits: 'Experimental editor. This demonstrates plain editing, not syntax highlighting or execution of the typed code.'}),
  CheckBox: Ui(`local Choice = Instance.new("CheckBox", {
    Parent = Window, Text = "Include completed items", Checked = false,
    Position = UDim2.fromOffset(16, 16), Size = UDim2.fromOffset(340, 44),
})
Choice.CheckedChanged:Connect(function()
    Status.Text = if Choice.Checked then "Completed items included" else "Completed items hidden"
end)`, 'Changing the checkbox updates the status. Read Checked inside the callback.'),
  Slider: Ui(`local Volume = Instance.new("Slider", {
    Parent = Window, Minimum = 0, Maximum = 100, Value = 50,
    Position = UDim2.fromOffset(16, 16), Size = UDim2.fromOffset(320, 44),
    AccessibilityLabel = "Volume",
})
Volume.ValueChanged:Connect(function()
    Status.Text = "Volume: " .. math.floor(Volume.Value)
end)`, 'Moving the slider updates the displayed volume. This API has no Step property.'),
  ProgressBar: Ui(`local Progress = Instance.new("ProgressBar", {
    Parent = Window, Minimum = 0, Maximum = 10, Value = 0,
    Position = UDim2.fromOffset(16, 16), Size = UDim2.fromOffset(320, 32),
})
task.delay(1, function()
    Progress.Value = 5
    Status.Text = "5 of 10 complete"
end)`, 'After one second the bar is halfway through its range. The delayed callback simulates progress; it does not perform a background job.'),
  ImageLabel: Ui(`local Image = Instance.new("ImageLabel", {
    Parent = Window, Source = "Pixel.png", Size = UDim2.fromOffset(120, 120),
    Position = UDim2.fromOffset(16, 16),
})
Image.LoadChanged:Connect(function() Status.Text = "Image: " .. Image.LoadState end)`, 'The declared image loads asynchronously. The complete image recipe supplies Pixel.png and its Assets declaration.', {Assets: ['Pixel.png'], Recipe: 'Recipe-Images'}),
  FrameSurface: Ui(`local Surface = Instance.new("FrameSurface", {Parent = Window, Size = UDim2.fromOffset(128, 128)})
local Pixels = buffer.create(4) -- One BGRA pixel, four bytes.
buffer.writeu8(Pixels, 2, 255) -- Red.
buffer.writeu8(Pixels, 3, 255) -- Fully opaque.
local Accepted = Surface:SubmitFrame(Pixels, 1, 1, 4, "BGRA8Premultiplied")
Status.Text = if Accepted then "Frame accepted; presentation is host-dependent" else "Frame submission refused"`, 'The status reports whether the host accepted the pixel buffer. It does not claim that visible presentation is qualified.', {Limits: 'Advanced experimental pixel submission. Check PlatformService:Supports("FrameSurface") before depending on this feature; prefer ImageLabel for ordinary images.'}),
  UIPadding: Ui(`local Content = Instance.new("Frame", {Parent = Window, Size = UDim2.fromOffset(320, 120)})
Instance.new("UIPadding", {
    Parent = Content, PaddingLeft = UDim.new(0, 16), PaddingTop = UDim.new(0, 12),
    PaddingRight = UDim.new(0, 16), PaddingBottom = UDim.new(0, 12),
})
Instance.new("TextLabel", {Parent = Content, Text = "Padded content", Size = UDim2.fromOffset(260, 44)})`, 'The label is inset from its container. Padding is an Instance parented to the container it affects.'),
  UIListLayout: Ui(`local Content = Instance.new("Frame", {Parent = Window, Size = UDim2.fromOffset(320, 140)})
Instance.new("UIListLayout", {Parent = Content, FillDirection = "Vertical", Padding = UDim.new(0, 8)})
for Index = 1, 3 do
    Instance.new("TextButton", {Parent = Content, Text = "Action " .. Index,
        Size = UDim2.fromOffset(220, 36), LayoutOrder = Index})
end`, 'Three buttons stack vertically with eight-unit gaps. The layout arranges their positions.'),
  UIGridLayout: Ui(`local Content = Instance.new("Frame", {Parent = Window, Size = UDim2.fromOffset(420, 140)})
Instance.new("UIGridLayout", {Parent = Content, CellSize = UDim2.fromOffset(120, 44), CellPadding = UDim2.fromOffset(8, 8)})
for Index = 1, 6 do
    Instance.new("TextButton", {Parent = Content, Text = "Item " .. Index, LayoutOrder = Index})
end`, 'Six buttons occupy fixed-size cells. CellPadding controls gaps between cells.'),
  UISizeConstraint: Ui(`local Content = Instance.new("Frame", {Parent = Window, Size = UDim2.fromScale(1, 0.5)})
Instance.new("UISizeConstraint", {Parent = Content, MinSize = Vector2.new(220, 80), MaxSize = Vector2.new(420, 140)})
Instance.new("TextLabel", {Parent = Content, Text = "Resize the window"})`, 'Resizing the window changes the frame within the requested minimum and maximum sizes.'),
  ScrollingFrame: Ui(`local List = Instance.new("ScrollingFrame", {
    Parent = Window, Size = UDim2.fromOffset(320, 140), CanvasSize = UDim2.fromOffset(320, 480),
})
Instance.new("UIListLayout", {Parent = List, Padding = UDim.new(0, 8)})
for Index = 1, 10 do
    Instance.new("TextLabel", {Parent = List, Text = "Row " .. Index,
        Size = UDim2.fromOffset(280, 36), LayoutOrder = Index})
end
List.CanvasPosition = Vector2.new(0, 80)`, 'The short viewport clips a taller list. CanvasPosition requests an initial scroll offset.'),
  StorageService: Ui(`local Storage = app:GetService("StorageService")
Storage:Open(buffer.fromstring("example_note"), nil, function(_, Handle, Failure)
    if Failure or not Handle then Status.Text = "Storage unavailable"; return end
    Storage:Read(buffer.create(0), Handle, function(Bytes, _, ReadFailure)
        if ReadFailure then
            Status.Text = "Read failed"
        elseif Bytes then
            Status.Text = buffer.tostring(Bytes)
        else
            Status.Text = "No saved note yet"
        end
        Handle:Close()
    end)
end)`, 'Reads one app-private key and closes its handle. No saved key is distinct from an empty saved buffer.', {Capabilities: ['Storage'], Storage: true, Limits: 'Low-level public host protocol. In preview.2, add Storage.ApplicationId as shown below. Preview.1 needs a custom embedding host that installs the storage provider; its standard SDK cannot provision this scope.'}),
  NotificationService: Ui(ButtonCode + `
Button.Text = "Send notification"
Button.Activated:Connect(function()
    local Notifications = app:GetService("NotificationService")
    local Payload = buffer.fromstring('{"Title":"LUI example","Body":"Your action finished"}')
    Notifications:Show(Payload, nil, function(_, _, Failure)
        Status.Text = if Failure then "Notification unavailable" else "Submitted to the OS"
    end)
end)`, 'On a supported Windows host, pressing the button submits a notification. Successful submission does not guarantee display.', {Capabilities: ['Notifications'], Limits: 'Advanced Windows host protocol; GTK reports unsupported. Title is 1–63 UTF-8 bytes, Body at most 255, and the JSON buffer at most 1,024.'}),
  TrayService: Ui(ButtonCode + `
Button.Text = "Create tray icon"
Button.Activated:Connect(function()
    local Tray = app:GetService("TrayService")
    Tray:Create(buffer.fromstring("LUI example"), nil, function(_, Handle, Failure)
        if Failure or not Handle then Status.Text = "Tray unavailable"; return end
        Status.Text = "Icon created for five seconds"
        task.delay(5, function() Handle:Close() end)
    end)
end)`, 'On the supported Windows shell, pressing the button creates a temporary tray icon and closes its handle five seconds later.', {Capabilities: ['Tray'], Limits: 'Advanced Windows host protocol; GTK reports unsupported. A tooltip is 1–127 UTF-8 bytes. A watch is separate from icon creation.'}),
  PerformanceService: Ui(`local Performance = app:GetService("PerformanceService")
local Start = Performance:GetTime()
task.delay(0.1, function()
    local Stats = Performance:GetStats()
    Status.Text = "Elapsed ms: " .. math.floor(Performance:GetTime() - Start)
        .. "; live Instances: " .. Stats.LiveInstances
end)`, 'Displays elapsed milliseconds and a runtime counter. The delay is scheduled work, not a precise real-time timer.'),
  WindowService: Ui(`local Windows = app:GetService("WindowService"):GetWindows()
Status.Text = "This app owns " .. #Windows .. " window(s)"`, 'Counts this application’s windows, including the example window.'),
  PlatformService: Ui(`local Platform = app:GetService("PlatformService")
Status.Text = Platform.BackendName .. "; clipboard available: " .. tostring(Platform:Supports("Clipboard"))`, 'Reports the active host and whether clipboard support was installed/granted. It does not request permission or open a dialog.'),
  ThemeService: Ui(`local Theme = app:GetService("ThemeService")
Status.Text = "Theme: " .. Theme.CurrentTheme
local Connection = Theme.ThemeChanged:Connect(function(NewTheme)
    Status.Text = "Theme: " .. NewTheme
end)
Window.Destroying:Connect(function() Connection:Disconnect() end)`, 'Displays the current theme and updates it when the host reports a change. The service connection is disconnected when the window is destroyed.'),
  ClipboardService: Ui(ButtonCode + `
Button.Text = "Copy greeting"
Button.Activated:Connect(function()
    local Clipboard = app:GetService("ClipboardService")
    if not Clipboard:WriteText("Hello from LUI") then Status.Text = "Copy failed"; return end
    Clipboard:ReadText(function(Text, Failure)
        Status.Text = if Failure then "Read failed" else "Clipboard: " .. (Text or "")
    end)
end)`, 'Pressing the button copies a greeting and reads it back through a callback.', {Capabilities: ['Clipboard']}),
  DialogService: Ui(ButtonCode + `
Button.Text = "Choose a file"
Button.Activated:Connect(function()
    app:GetService("DialogService"):OpenFile(function(Path, Failure)
        Status.Text = if Path and not Failure then "A file was selected" else "No file selected"
    end)
end)`, 'Pressing the button opens a supported native file picker. This only selects a path; it does not read file contents.', {Capabilities: ['Dialogs']}),
  AssetService: Ui(`local Assets = app:GetService("AssetService")
Status.Text = if Assets:Has("Pixel.png") then "Image declared" else "Image not declared"`, 'Reports whether Pixel.png is declared, without pretending that declaration means decoding is complete.', {Assets: ['Pixel.png'], Recipe: 'Recipe-Images'}),
  HttpService: Ui(ButtonCode + `
Button.Text = "Request local status"
Button.Activated:Connect(function()
    task.spawn(function()
        local Ok, Response = pcall(function()
            return app:GetService("HttpService"):RequestAsync({
                Url = "http://127.0.0.1:8080/status", TimeoutMs = 3000,
                MaxResponseBytes = 4096,
            })
        end)
        if not Ok then Status.Text = "Request could not complete"; return end
        Status.Text = "HTTP " .. Response.StatusCode .. ": " .. Response.Body
    end)
end)`, 'With a local server on port 8080, pressing the button displays its status/body. Without one, it reports request failure without a dialog.', {Capabilities: ['network.client'], Network: true, Recipe: 'Recipe-Http', Limits: 'This client fragment needs a running endpoint. The complete local HTTP recipe starts its own server and avoids a fixed port.'}),
  HttpServerService: Ui(`local Server = app:GetService("HttpServerService"):CreateServer({Address = "loopback", Port = 0})
Server:Route("GET", "/status", function(Request)
    return {StatusCode = 200, Headers = {{Name = "Content-Type", Value = "text/plain"}},
        Body = "Ready: " .. Request.Path}
end)
Server:Start()
Status.Text = "Local URL: http://127.0.0.1:" .. Server.Port .. "/status"
Window.Destroying:Connect(function() Server:Close() end)`, 'Starts an HTTP endpoint on a free local-only port. Open the displayed URL on this machine to see Ready: /status.', {Capabilities: ['network.server'], Network: true, Recipe: 'Recipe-Http'}),
  NetworkService: Ui(`local Network = app:GetService("NetworkService")
local Listener = Network:ListenTcp({Address = "loopback", Port = 0})
task.spawn(function()
    local Peer = Listener:AcceptAsync()
    local Bytes = Peer:ReadExactAsync(4)
    Peer:WriteAsync(Bytes)
    Peer:Close()
    Listener:Close()
end)
task.spawn(function()
    local Peer = Network:ConnectTcp({Address = "127.0.0.1", Port = Listener.Port})
    Peer:WriteAsync("ping")
    local Reply = Peer:ReadExactAsync(4)
    Status.Text = buffer.tostring(Reply)
    Peer:Close()
end)
Window.Destroying:Connect(function() Listener:Close() end)`, 'A local TCP client sends four bytes and receives the echo. The fixed four-byte length is this example’s framing rule, not a TCP message boundary.', {Capabilities: ['network.raw','network.client','network.server'], Network: true, Limits: 'Advanced raw networking. Calls that wait for I/O belong in task.spawn/task.defer; normal HTTP apps can use HttpService instead.'}),
  UdpSocket: Ui(`local Network = app:GetService("NetworkService")
local Receiver = Network:BindUdp({Address = "loopback", Port = 0, MaxDatagramBytes = 1024})
local Sender = Network:BindUdp({Address = "loopback", Port = 0, MaxDatagramBytes = 1024})
task.spawn(function()
    local Packet = Receiver:ReceiveFromAsync()
    Status.Text = buffer.tostring(Packet.Data) .. " from " .. Packet.RemoteEndpoint.Address
    Receiver:Close()
end)
task.spawn(function()
    Sender:SendToAsync(Receiver.LocalEndpoint, "Hello UDP")
    Sender:Close()
end)
Window.Destroying:Connect(function() Receiver:Close(); Sender:Close() end)`, 'Sends one local datagram and displays its bytes and sender. UDP delivery is not guaranteed outside this demonstration.', {Capabilities: ['network.raw','network.client','network.server'], Network: true}),
  Color3: Ui(`Status.TextColor3 = Color3.fromRGB(30, 120, 200)
local Tint = Color3.new(0.1, 0.5, 0.8)
Status.BackgroundColor3 = Tint
Status.Text = "RGB values: " .. Tint.R .. ", " .. Tint.G .. ", " .. Tint.B`, 'Uses 0–255 input with fromRGB and normalized 0–1 components with new.'),
};
Examples.GuiButton = { ...Examples.TextButton, Outcome: 'GuiButton is a non-creatable base. Create TextButton to use the shared button behavior shown here.' };
Examples.UIComponent = { ...Examples.UIListLayout, Outcome: 'UIComponent is a non-creatable base. Create UIListLayout, UIPadding, UIGridLayout or UISizeConstraint and parent it to the container it affects.' };
for (const Name of ['PerformanceStats']) Examples[Name] = { ...Examples.PerformanceService };
for (const Name of ['NativeHandle','NativeRequest']) Examples[Name] = { ...Examples.StorageService,
  Code: Examples.StorageService.Code.replace('Storage:Open(', 'local Request = Storage:Open('),
  Outcome: 'Open returns a NativeRequest immediately; its callback supplies a NativeHandle on success. Close the handle after use. Request:Cancel() can cancel a pending operation, but does not undo completed work.' };
Examples.NativeRequest = Ui(`local Storage = app:GetService("StorageService")
local Request = Storage:Open(buffer.fromstring("example_note"), nil, function(_, Handle, Failure)
    if Handle then Handle:Close() end -- Close a handle if completion won the race.
end)
local Won, Code = Request:Cancel()
Status.Text = "Cancellation won: " .. tostring(Won) .. "; status: " .. Code`, 'Shows the result of canceling a native request. A completed operation cannot be rolled back by canceling its request.', {Capabilities:['Storage'],Storage:true,Limits:Examples.StorageService.Limits});
for (const Name of ['NetworkEndpoint','NetworkListenOptions','NetworkConnectOptions','TcpListener','TcpConnection']) Examples[Name] = { ...Examples.NetworkService };
for (const Name of ['UdpBindOptions','UdpDatagram']) Examples[Name] = { ...Examples.UdpSocket };
for (const Name of ['HttpHeader','HttpRequestOptions','HttpResponse']) Examples[Name] = { ...Examples.HttpService };
for (const Name of ['HttpServerOptions','HttpReplyOptions','HttpRequest','HttpServer']) Examples[Name] = { ...Examples.HttpServerService };

export const Globals = {
  Instance: Examples.TextButton,
  Vector2: Ui(`local Point = Vector2.new(24, 48)
Status.Text = "X=" .. Point.X .. ", Y=" .. Point.Y`, 'Constructs a two-component value. Vector2 is a value, not an Instance with a Parent.'),
  UDim: Ui(`local Gap = UDim.new(0, 12)
Status.Text = "Scale=" .. Gap.Scale .. ", Offset=" .. Gap.Offset`, 'Creates a fixed twelve-unit dimension. A scale of 0 means none of the parent’s size is added.'),
  UDim2: Ui(`Status.Position = UDim2.new(0, 16, 0.5, 0)
Status.Size = UDim2.new(1, -32, 0, 44)`, 'Places a label halfway down its parent and makes it the parent width minus 32 logical units.'),
  Color3: Examples.Color3,
  app: Examples.PlatformService,
  task: Ui(`task.defer(function() Status.Text = "Deferred callback ran" end)
task.delay(1, function() Status.Text = "Delayed callback ran" end)`, 'The status changes after scheduled callbacks run. These tasks run through LUI’s scheduler, not unrestricted native threads.'),
  require: {Code:`-- Src/Main.luau; declare both source files in lui.json.
local Greeting = require("./Greeting")
local Window = Instance.new("Window", {Title = Greeting.Text, Size = UDim2.fromOffset(560, 280)})
Window.Visible = true
`, Files: {'Greeting.luau':'return {Text = "Hello from a module"}\n'}, Capabilities: [], Outcome:'A window uses the value returned by Greeting.luau. The module is loaded from declared source; require does not install a package.'},
};

export const InputExample = Ui(ButtonCode + `
Button.InputBegan:Connect(function(Input)
    if Input.Device == "Keyboard" then
        Status.Text = "Key: " .. Input.Key
    else
        Status.Text = "Pointer at " .. Input.Position.X .. ", " .. Input.Position.Y
    end
end)`, 'Checks Device before accessing fields that exist only on a keyboard or pointer record. Use Activated for an ordinary button action.');
export const ConnectionExample = Ui(ButtonCode + `
local Connection = Button.Activated:Connect(function() Status.Text = "Activated" end)
task.delay(5, function()
    Connection:Disconnect()
    Status.Text = "Callback disconnected"
end)`, 'The button works for five seconds, then its explicit connection is disconnected. Destroying the button would also disconnect its own signals.');
export const ShortcutExample = Ui(`Window.Shortcut:Connect(function(Input)
    if Input.Control and Input.Key == "S" and not Input.IsRepeat and not Input.TextEditing then
        Status.Text = "Save shortcut received"
    end
end)`, 'Receives an unhandled Ctrl+S shortcut when the host supplies it. It does not consume global keyboard input.');

export function TypeExample(Name) {
  if (Name.endsWith('Init')) return Examples[Name.slice(0,-4)];
  if (Globals[Name]) return Globals[Name];
  if (Name === 'App') return Globals.app;
  if (['Connection','Signal'].includes(Name)) return ConnectionExample;
  if (['PointerInput','KeyboardInput','InputEvent','InputSignal'].includes(Name)) return InputExample;
  if (['KeyboardShortcut','ShortcutSignal'].includes(Name)) return ShortcutExample;
  if (Name === 'ThemeSignal') return Examples.ThemeService;
  return Examples[Name];
}
