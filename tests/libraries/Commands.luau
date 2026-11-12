--!strict
local Commands = require('@LuiWorkbench/Commands')
local Registry = Commands.New()
local Calls, Available = 0, false
local Definition: Commands.Command = {Id = 'File.Save', Label = 'Save document', Category = 'File',
    Shortcut = {Key = 'S', Control = true}, Enabled = function() return Available end,
    Execute = function() Calls += 1 end}
local Remove = Registry:Register(Definition)
local Mutable: any = Definition
Mutable.Label = 'Mutated'; Mutable.Shortcut.Key = 'X'
assert(Registry:List()[1].Label == 'Save document')
local Ok, Reason = Registry:Execute('File.Save')
assert(not Ok and Reason == 'Disabled' and Calls == 0)
Available = true
assert(Registry:List('file document')[1].Enabled)
assert(#Registry:List('missing') == 0 and #Registry:List('', 0) == 0)
assert(Registry:Execute('File.Save') and Calls == 1)
local Input: any = {Device = 'Keyboard', Key = 'S', Control = true, Shift = false, Alt = false, IsRepeat = false, TextEditing = true}
assert(not Registry:Dispatch(Input) and Calls == 1)
Input.TextEditing = false; Input.IsRepeat = true
assert(not Registry:Dispatch(Input) and Calls == 1)
Input.IsRepeat = false
assert(Registry:Dispatch(Input) and Calls == 2)
assert(not pcall(function() Registry:Register(Definition) end))
assert(not pcall(function() Registry:Register({Id = 'Other', Label = 'Other', Shortcut = {Key = 'S', Control = true}, Execute = function() end}) end))
Remove(); Remove()
local MetatableBinding: any = setmetatable({}, {__index = function(_, Key) return if Key == 'Key' then 'S' else nil end})
assert(not pcall(function() Registry:Register({Id = 'Proxy', Label = 'Proxy', Shortcut = MetatableBinding, Execute = function() end}) end))
local ExtraBinding: any = {Key = 'X', Extra = {Unbounded = 'must not be retained'}}
local ExtraRemove = Registry:Register({Id = 'Extra', Label = 'Extra', Shortcut = ExtraBinding, Execute = function() end})
local Copied: any = Registry:List('Extra')[1].Shortcut
assert(Copied.Extra == nil)
ExtraBinding.Key = 'S'; assert(Copied.Key == 'X'); ExtraRemove()
assert(not Registry:Execute('File.Save'))
local NewRemove = Registry:Register({Id = 'File.Save', Label = 'Replacement', Execute = function() Calls += 10 end})
Remove(); assert(Registry:Execute('File.Save') and Calls == 12)
NewRemove()
Registry:Register({Id = 'Fail', Label = 'Failure', Execute = function() error('Synthetic private detail') end})
Ok, Reason = Registry:Execute('Fail'); assert(not Ok and Reason == 'Failed')
Registry:Register({Id = 'Reentry', Label = 'Reentry', Execute = function()
    assert(#Registry:List() > 0)
    local Ran, Why = Registry:Execute('Fail'); assert(not Ran and Why == 'Busy')
end})
assert(Registry:Execute('Reentry'))
Registry:Register({Id = 'PredicateFailure', Label = 'Predicate failure', Enabled = function() error('Synthetic') end, Execute = function() error('Must not run') end})
Ok, Reason = Registry:Execute('PredicateFailure'); assert(not Ok and Reason == 'Disabled')
assert(not pcall(function() Registry:List('', math.huge) end))
Registry:Dispose(); Registry:Dispose()
Ok, Reason = Registry:Execute('Fail'); assert(not Ok and Reason == 'Retired')
assert(not pcall(function() Registry:List() end))
local Retiring = Commands.New()
Retiring:Register({Id = 'Retire', Label = 'Retire', Enabled = function() Retiring:Dispose(); return true end,
    Execute = function() error('Must not execute after predicate disposal') end})
Ok, Reason = Retiring:Execute('Retire'); assert(not Ok and Reason == 'Retired')
local Bounded = Commands.New()
for Index = 1, 128 do Bounded:Register({Id = tostring(Index), Label = 'Synthetic', Execute = function() end}) end
assert(not pcall(function() Bounded:Register({Id = 'Overflow', Label = 'Overflow', Execute = function() end}) end))
for _ = 1, 100 do assert(#Bounded:List('synthetic', 12) == 12) end
Bounded:Dispose()
local Interleaved = Commands.New()
Interleaved:Register({Id = 'Async', Label = 'Async', Execute = function() coroutine.yield('handler') end})
Interleaved:Register({Id = 'Predicate', Label = 'Predicate', Enabled = function() coroutine.yield('predicate'); return true end,
    Execute = function() end})
Interleaved:Register({Id = 'Safe', Label = 'Safe', Execute = function() end})
local Invocation = coroutine.create(function() assert(Interleaved:Execute('Async')) end)
local Listing = coroutine.create(function() Interleaved:List() end)
assert(coroutine.resume(Invocation)); assert(coroutine.resume(Listing)); assert(coroutine.resume(Invocation))
assert(not pcall(function() Interleaved:Register({Id = 'DuringListing', Label = 'Must refuse', Execute = function() end}) end))
local Ran, Why = Interleaved:Execute('Safe'); assert(not Ran and Why == 'Busy')
assert(coroutine.resume(Listing)); assert(Interleaved:Execute('Safe'))
Interleaved:Dispose()
print('[LUI:LibraryTest] Commands: isolation, conflicts, precedence, retirement, reentry and bounds passed')
