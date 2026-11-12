--!strict
local Scope = require('./Scope')
local State = require('./State')
local NumericInput = {Version = '0.1.0'}

export type Options = {
    Value: number?, Minimum: number?, Maximum: number?, Step: number?,
    Label: string?, OnChanged: ((number) -> ())?,
}
export type Controller = {
    read View: Frame, read Input: TextBox, read Decrease: TextButton, read Increase: TextButton,
    GetValue: (Self: Controller) -> number,
    SetValue: (Self: Controller, Next: number) -> (),
    Commit: (Self: Controller) -> boolean,
    Dispose: (Self: Controller) -> (),
}

local function Finite(Value: any): boolean
    return type(Value) == 'number' and Value == Value and math.abs(Value) < math.huge
end

function NumericInput.New(Parent: Instance, Options: Options?): Controller
    assert(Options == nil or (type(Options) == 'table' and getmetatable(Options) == nil), '[LUI:NumericInput] PlainOptionsRequired')
    local Settings: any = if Options == nil then {} else Options
    for Key in Settings do
        assert(Key == 'Value' or Key == 'Minimum' or Key == 'Maximum' or Key == 'Step' or Key == 'Label' or Key == 'OnChanged', '[LUI:NumericInput] UnknownOption')
    end
    local Minimum: number = if Settings.Minimum == nil then 0 else Settings.Minimum
    local Maximum: number = if Settings.Maximum == nil then 100 else Settings.Maximum
    local Step: number = if Settings.Step == nil then 1 else Settings.Step
    local Initial: number = if Settings.Value == nil then Minimum else Settings.Value
    local Label: string = if Settings.Label == nil then 'Value' else Settings.Label
    local OnChanged: ((number) -> ())? = Settings.OnChanged
    assert(Finite(Minimum) and Finite(Maximum) and Minimum >= -1e12 and Maximum <= 1e12 and Minimum <= Maximum, '[LUI:NumericInput] Range')
    assert(Finite(Step) and Step > 0 and Step <= 2e12 and Finite(Initial) and Initial >= Minimum and Initial <= Maximum, '[LUI:NumericInput] ValueOrStep')
    assert(type(Label) == 'string' and #Label <= 512 and utf8.len(Label) ~= nil and not string.find(Label, '\0', 1, true), '[LUI:NumericInput] Label')
    assert(OnChanged == nil or type(OnChanged) == 'function', '[LUI:NumericInput] Callback')
    local Owner = Scope.New()
    local Store = State.New(Initial)
    local Alive = true
    local function Retire()
        if not Alive then return end
        Alive = false
        local Ok = pcall(function() Owner:Dispose() end)
        Store:Dispose()
        if not Ok then error('[LUI:NumericInput] CleanupFailed', 0) end
    end
    local function Build(): Controller
        local View = Instance.new('Frame', {Parent = Parent, Size = UDim2.new(1, 0, 0, 36)}) :: Frame
        Owner:Own(View)
        local Input = Instance.new('TextBox', {Parent = View, Text = tostring(Initial), MaxLength = 64,
            AccessibilityLabel = Label, Size = UDim2.new(1, -80, 1, 0)}) :: TextBox
        Owner:Own(Input)
        local Decrease = Instance.new('TextButton', {Parent = View, Text = '−', AccessibilityLabel = 'Decrease ' .. Label,
            Position = UDim2.new(1, -76, 0, 0), Size = UDim2.fromOffset(36, 36), Enabled = Initial > Minimum}) :: TextButton
        Owner:Own(Decrease)
        local Increase = Instance.new('TextButton', {Parent = View, Text = '+', AccessibilityLabel = 'Increase ' .. Label,
            Position = UDim2.new(1, -36, 0, 0), Size = UDim2.fromOffset(36, 36), Enabled = Initial < Maximum}) :: TextButton
        Owner:Own(Increase)
        local Value: any = {View = View, Input = Input, Decrease = Decrease, Increase = Increase}
        local function Render(Next: number): boolean
            local Text = tostring(Next)
            if Input.Text ~= Text then Input.Text = Text end
            if not Alive or Store:Get() ~= Next then return false end
            if Decrease.Enabled ~= (Next > Minimum) then Decrease.Enabled = Next > Minimum end
            if not Alive or Store:Get() ~= Next then return false end
            if Increase.Enabled ~= (Next < Maximum) then Increase.Enabled = Next < Maximum end
            return Alive and Store:Get() == Next
        end
        function Value.GetValue(_Self: any): number return Store:Get() end
        function Value.SetValue(_Self: any, Next: number)
            assert(Alive and Finite(Next) and Next >= Minimum and Next <= Maximum, '[LUI:NumericInput] ValueOrRetired')
            if Store:Get() == Next then Render(Next) else Store:Set(Next) end
        end
        function Value.Commit(_Self: any): boolean
            assert(Alive, '[LUI:NumericInput] Retired')
            if Input.IsComposing then return false end
            local Parsed = tonumber(Input.Text)
            if not Finite(Parsed) then Render(Store:Get()); return false end
            Value:SetValue(math.clamp(Parsed :: number, Minimum, Maximum))
            return true
        end
        function Value.Dispose(_Self: any)
            Retire()
        end
        for _, Object in {View, Input, Decrease, Increase} do
            Owner:Connect(Object.Destroying, Retire)
        end
        Owner:Connect(Input.FocusLost, function() Value:Commit() end)
        Owner:Connect(Input.InputBegan, function(Event: InputEvent)
            if Event.Device == 'Keyboard' and Event.Key == 'Enter' then Value:Commit() end
        end)
        Owner:Connect(Decrease.Activated, function()
            if not Input.IsComposing then Value:SetValue(math.max(Minimum, Store:Get() - Step)) end
        end)
        Owner:Connect(Increase.Activated, function()
            if not Input.IsComposing then Value:SetValue(math.min(Maximum, Store:Get() + Step)) end
        end)
        local Changed = Store:Subscribe(function(Next: any)
            if Render(Next) and OnChanged then OnChanged(Next) end
        end)
        Owner:Add(function() Changed:Disconnect() end)
        return table.freeze(Value) :: Controller
    end
    local Ok, Result = pcall(Build)
    if not Ok then
        pcall(Retire)
        error('[LUI:NumericInput] ConstructionFailed', 0)
    end
    return Result :: Controller
end

return table.freeze(NumericInput)
