local Platform = app:GetService("PlatformService")
local HasNotifications = Platform:Supports("Notifications")
local HasTray = Platform:Supports("Tray")
local Window = Instance.new("Window", {Title = "LUI Native Integration", Size = UDim2.fromOffset(520, 310)})
local Root = Instance.new("Frame", {Parent = Window, Size = UDim2.fromScale(1, 1)})
Instance.new("UIPadding", {Parent = Root, PaddingLeft = UDim.new(0, 20), PaddingTop = UDim.new(0, 20)})
Instance.new("UIListLayout", {Parent = Root, Padding = UDim.new(0, 12)})
local Status = Instance.new("TextLabel", {Parent = Root, Size = UDim2.fromOffset(470, 48), Text = "Native integration ready"})
local function Button(Text: string, Enabled: boolean, Action: () -> ())
    local Value = Instance.new("TextButton", {Parent = Root, Size = UDim2.fromOffset(470, 44), Text = Text, Enabled = Enabled})
    Value.Activated:Connect(Action)
end
local Icon: NativeHandle? = nil
local Watch: NativeRequest? = nil
local Generation = 0
local function CloseIcon()
    Generation += 1
    if Watch then Watch:Cancel(); Watch = nil end
    if Icon then Icon:Close(); Icon = nil end
end
Button("Send a sample notification", HasNotifications, function()
    if not HasNotifications then return end
    app:GetService("NotificationService"):Show(buffer.fromstring('{"Title":"LUI example","Body":"A synthetic desktop notification."}'), nil,
        function(_, _, Error)
            Status.Text = Error and "Notification: " .. Error or "Notification submitted"
        end)
end)
Button("Add a tray icon", HasTray, function()
    if not HasTray then return end
    CloseIcon()
    local Current = Generation
    local Tray = app:GetService("TrayService")
    Tray:Create(buffer.fromstring("LUI Native Integration"), nil, function(_, Handle, Error)
        if Current ~= Generation then if Handle then Handle:Close() end; return end
        if Error or not Handle then Status.Text = "Tray: " .. (Error or "Closed"); return end
        Icon = Handle
        Status.Text = "Tray icon added; activate it to test delivery"
        Watch = Tray:Watch(buffer.create(0), Handle, function(_, _, Failure)
            if Failure == "DeadlineExceeded" then Status.Text = "Tray watch expired; add the icon again to rearm" end
        end, function(_)
            Status.Text = "Tray activation delivered"
        end)
    end)
end)
Button("Remove the tray icon", HasTray, function() CloseIcon(); Status.Text = "Tray icon removed" end)
Window.Destroying:Connect(CloseIcon)
if not HasNotifications and not HasTray then Status.Text = "Desktop notifications and tray aren't available in this host" end
