local Window = Instance.new('Window', {Title = 'Browser controls', Size = UDim2.fromOffset(640, 600)})
local Output = Instance.new('TextLabel', {Parent = Window, Text = 'Ready', Position = UDim2.fromOffset(8, 8), Size = UDim2.fromOffset(600, 32), AccessibilityLabel = 'Output'})
local Button = Instance.new('TextButton', {Parent = Window, Text = 'Activate', Position = UDim2.fromOffset(8, 48), Size = UDim2.fromOffset(120, 36)})
local Count = 0
Button.Activated:Connect(function() Count += 1; Output.Text = 'Activated ' .. Count end)
local Check = Instance.new('CheckBox', {Parent = Window, Text = 'Option', Position = UDim2.fromOffset(8, 92), Size = UDim2.fromOffset(180, 36)})
Check.CheckedChanged:Connect(function() Output.Text = if Check.Checked then 'Checked' else 'Unchecked' end)
local Slider = Instance.new('Slider', {Parent = Window, AccessibilityLabel = 'Range', Position = UDim2.fromOffset(8, 136), Size = UDim2.fromOffset(180, 36), Minimum = 10, Maximum = 20, Value = 15})
Slider.ValueChanged:Connect(function() Output.Text = 'Value ' .. Slider.Value end)
local Progress = Instance.new('ProgressBar', {Parent = Window, AccessibilityLabel = 'Progress', Position = UDim2.fromOffset(208, 136), Size = UDim2.fromOffset(180, 36), Minimum = 10, Maximum = 20, Value = 15})
local Text = Instance.new('TextBox', {Parent = Window, AccessibilityLabel = 'Editor', Position = UDim2.fromOffset(8, 180), Size = UDim2.fromOffset(400, 40), MaxLength = 64})
Text.TextChanged:Connect(function() Output.Text = Text.Text end)
local Multiline = Instance.new('TextBox', {Parent = Window, AccessibilityLabel = 'Multiline', Multiline = true, Position = UDim2.fromOffset(8, 228), Size = UDim2.fromOffset(400, 80)})
local Secret = Instance.new('TextBox', {Parent = Window, AccessibilityLabel = 'Secret', Secret = true, Position = UDim2.fromOffset(8, 316), Size = UDim2.fromOffset(400, 40)})
local Scroller = Instance.new('ScrollingFrame', {Parent = Window, AccessibilityLabel = 'Viewport', Position = UDim2.fromOffset(8, 364), Size = UDim2.fromOffset(400, 160), CanvasSize = UDim2.fromOffset(400, 1600)})
Instance.new('TextLabel', {Parent = Scroller, Text = 'Top row', Size = UDim2.fromOffset(200, 40), BackgroundColor3 = Color3.fromRGB(12, 34, 56), TextColor3 = Color3.fromRGB(230, 240, 250), CornerRadius = 8})
Instance.new('TextLabel', {Parent = Scroller, Text = 'Bottom row', Position = UDim2.fromOffset(0, 1500), Size = UDim2.fromOffset(200, 40)})
local Platform = app:GetService('PlatformService')
for _, Name in {'network.client', 'network.server', 'network.raw', 'Clipboard', 'Dialogs', 'Storage', 'Notifications', 'Tray', 'NativeExtensions', 'HostServices'} do
    assert(not Platform:Supports(Name), 'Unsupported capability became available')
end
for _, Name in {'NetworkService', 'HttpService', 'HttpServerService', 'ClipboardService', 'DialogService', 'StorageService', 'NotificationService', 'TrayService'} do
    local Success = pcall(function() app:GetService(Name) end)
    assert(not Success, 'Unsupported service became available')
end
Window.Visible = true
task.defer(function()
    Instance.new('TextLabel', {Parent = Window, Text = 'Deferred task ran', Position = UDim2.fromOffset(440, 48), Size = UDim2.fromOffset(180, 40)})
end)
