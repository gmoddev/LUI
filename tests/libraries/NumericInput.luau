--!strict
local NumericInput = require('@LuiUI/NumericInput')
local State = require('@LuiUI/State')
local Scope = require('@LuiUI/Scope')
local Window = Instance.new('Window', {Visible = false}) :: Window
local Before = #Window:GetChildren()
local Invalid: {any} = {
    false, {Minimum = false}, {Maximum = false}, {Value = false}, {Step = false}, {Label = false},
    {OnChanged = false}, {Unknown = true}, {Value = 0/0}, {Value = math.huge},
    {Minimum = 10, Maximum = 0}, {Step = 0}, {Step = -1}, {Step = math.huge},
    {Minimum = -1e13}, {Maximum = 1e13}, {Value = 101}, {Label = string.rep('x', 513)},
    {Label = string.char(0xC0)}, {Label = 'private\0label'}, setmetatable({}, {}),
}
for _, Options in Invalid do
    assert(not pcall(function() NumericInput.New(Window, Options) end))
    assert(#Window:GetChildren() == Before, 'invalid options allocated controls')
end

local Calls = 0
local Definition: NumericInput.Options = {Value = 5, Minimum = -10, Maximum = 10, Step = 0.5,
    OnChanged = function(_Next) Calls += 1 end}
local Number = NumericInput.New(Window, Definition)
assert(table.isfrozen(Number))
local Mutable: any = Number
assert(not pcall(function() Mutable.View = Window end), 'controller wrapper was mutable')
Definition.OnChanged = function(_Next: number) Calls += 1000 end
local Normalize = Number.Input.TextChanged:Connect(function() Number:SetValue(Number:GetValue()) end)
Number:SetValue(-10)
assert(Number:GetValue() == -10 and Number.Input.Text == '-10' and not Number.Decrease.Enabled and Calls == 1)
Normalize:Disconnect()
Number:SetValue(-10); assert(Calls == 1)
Number.Input.Text = 'not a number'; assert(not Number:Commit() and Number.Input.Text == '-10' and Calls == 1)
Number.Input.Text = '1e300'; assert(Number:Commit() and Number:GetValue() == 10 and not Number.Increase.Enabled and Calls == 2)
Number.Input.Text = '-1e300'; assert(Number:Commit() and Number:GetValue() == -10 and Calls == 3)
Number.Input.Text = '1e999'; assert(not Number:Commit() and Number:GetValue() == -10 and Calls == 3)
Number.Input.Text = ''; assert(not Number:Commit() and Number.Input.Text == '-10')
assert(not pcall(function() Number:SetValue(math.huge) end))
assert(not pcall(function() Number:SetValue(11) end))
Number:Dispose(); Number:Dispose(); assert(Number:GetValue() == -10)
assert(not pcall(function() Number:SetValue(0) end))
assert(not pcall(function() Number:Commit() end))

local Same = NumericInput.New(Window, {Minimum = 2, Maximum = 2})
assert(Same:GetValue() == 2 and not Same.Decrease.Enabled and not Same.Increase.Enabled)
Same.Input.Text = '99'; assert(Same:Commit() and Same:GetValue() == 2); Same:Dispose()

local ShouldFail = true
local Recovering = NumericInput.New(Window, {OnChanged = function(_Next) if ShouldFail then error('PRIVATE_NUMERIC_CALLBACK') end end})
local Failed, Detail = pcall(function() Recovering:SetValue(1) end)
assert(not Failed and Detail == '[LUI:State] DeliveryFailed' and Recovering:GetValue() == 1)
ShouldFail = false; Recovering:SetValue(2); assert(Recovering:GetValue() == 2); Recovering:Dispose()

local Updates: {number} = {}
local Reentrant = NumericInput.New(Window, {Value = 0, OnChanged = function(Next) table.insert(Updates, Next) end})
local Once = true
local Change = Reentrant.Input.TextChanged:Connect(function()
    if Once then Once = false; Reentrant:SetValue(7) end
end)
Reentrant:SetValue(5)
assert(Reentrant:GetValue() == 7 and Reentrant.Input.Text == '7' and #Updates == 1 and Updates[1] == 7, 'stale render escaped')
Change:Disconnect(); Reentrant:Dispose()

local Retiring: NumericInput.Controller? = nil
Retiring = NumericInput.New(Window, {OnChanged = function(_Next) (Retiring :: NumericInput.Controller):Dispose() end})
Retiring:SetValue(1); Retiring:Dispose()
assert(#Window:GetChildren() == Before)
local RenderCalls = 0
local DuringRender = NumericInput.New(Window, {OnChanged = function(_Next) RenderCalls += 1 end})
local Kill = DuringRender.Input.TextChanged:Connect(function() DuringRender:Dispose() end)
DuringRender:SetValue(1); Kill:Disconnect()
assert(RenderCalls == 0 and #Window:GetChildren() == Before)

local Parent = Instance.new('Frame', {Parent = Window}) :: Frame
local Child = NumericInput.New(Parent)
Parent:Destroy(); Child:Dispose(); assert(not pcall(function() Child:SetValue(1) end))
for _, Part in {'Input', 'Decrease', 'Increase'} do
    local Temporary: any = NumericInput.New(Window)
    Temporary[Part]:Destroy()
    assert(#Window:GetChildren() == Before and not pcall(function() Temporary:SetValue(1) end), 'dead component part remained active')
    Temporary:Dispose()
end
local Destination = Instance.new('Frame', {Parent = Window}) :: Frame
local Moved = NumericInput.New(Window); Moved.Input.Parent = Destination
Moved:Dispose(); assert(#Destination:GetChildren() == 0, 'moved owned input leaked'); Destination:Destroy()
for Index = 1, 100 do local Temporary = NumericInput.New(Window, {Value = Index}); Temporary:Dispose() end
assert(#Window:GetChildren() == Before, 'component subtrees retained')

local Cell = State.New(0)
local Seen: {number} = {}
local Subscription = Cell:Subscribe(function(Value)
    table.insert(Seen, Value)
    if Value < 3 then Cell:Set(Value + 1) end
end)
Cell:Set(1); assert(Cell:Get() == 3 and #Seen == 3 and Seen[1] == 1 and Seen[3] == 3)
Subscription:Disconnect(); Subscription:Disconnect(); Cell:Set(4); assert(#Seen == 3)
Cell:Dispose(); Cell:Dispose(); assert(Cell:Get() == 4 and not pcall(function() Cell:Set(5) end))
local Owner = Scope.New(); local Order = ''
local function GetOrder(): string return Order end
Owner:Add(function() Order ..= 'A' end)
Owner:Add(function() Order ..= 'B'; error('PRIVATE_CLEANUP_CANARY') end)
local Ok, Failure = pcall(function() Owner:Dispose() end)
assert(not Ok and Failure == '[LUI:Scope] CleanupFailed' and GetOrder() == 'BA' and not Owner.Alive)
Owner:Dispose(); Owner:Add(function() Order ..= 'C' end); assert(GetOrder() == 'BAC')
Window:Destroy()
