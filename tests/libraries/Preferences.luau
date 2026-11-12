--!strict
local Preferences = require('@LuiWorkbench/Preferences')
local Callback: any = nil
local Opens, Reads, Writes, Closes, Cancels, Completions = 0, 0, 0, 0, 0, 0
local Payload = ''
local Handle: any = {Close = function() Closes += 1; return true end}
local Request: any = {Cancel = function() Cancels += 1; return true, '' end}
local Service: any = {}
function Service.Open(_Self: any, _: buffer, _Unused: any, Complete: any): any Opens += 1; Callback = Complete; return Request end
function Service.Read(_Self: any, _: buffer, _Unused: any, Complete: any): any Reads += 1; Callback = Complete; return Request end
function Service.Write(_Self: any, Bytes: buffer, _Unused: any, Complete: any): any Writes += 1; Payload = buffer.tostring(Bytes); Callback = Complete; return Request end
local Store = Preferences.New(Service, 'workspace')
assert(Opens == 0) -- construction is lazy
local Input = buffer.fromstring('layout')
local First = Store:Write(Input, function(_, Failure) assert(Failure == nil); Completions += 1 end)
buffer.writeu8(Input, 0, 88)
assert(not pcall(function() Store:Read(function() end) end))
Callback(nil, Handle, nil)
assert(Writes == 1 and Payload == 'layout')
Callback(nil, nil, nil)
assert(Completions == 1 and not First:Cancel())
local Unexpected = 0
local Second = Store:Read(function() Unexpected += 1 end)
assert(Reads == 1 and Opens == 1)
local Late = Callback
assert(Second:Cancel() and not Second:Cancel() and Cancels == 1 and Closes == 1)
Late(buffer.fromstring('late'), nil, nil)
assert(Unexpected == 0)
local Third = Store:Read(function() Unexpected += 1 end)
local LateOpen = Callback
Store:Dispose(); Store:Dispose()
LateOpen(nil, Handle, nil)
assert(Closes == 2 and Cancels == 2 and not Third:Cancel() and Unexpected == 0)
assert(not pcall(function() Store:Read(function() end) end))
assert(not pcall(function() Preferences.New(Service, '../private') end))
local Immediate: any = {}
function Immediate.Open(_Self: any, _: any, _Unused: any, Complete: any): any Complete(nil, Handle, nil); return Request end
function Immediate.Read(_Self: any, _: any, _Unused: any, Complete: any): any Complete(buffer.create(8193), nil, nil); return Request end
local Sync = Preferences.New(Immediate, 'sync')
local Done = false
local Completed = Sync:Read(function(Bytes, Failure) assert(Bytes == nil and Failure == 'ByteLimit'); Done = true end)
assert(Done and not Completed:Cancel()); Sync:Dispose()
local Denied: any = {Open = function() error('Synthetic permission refusal') end}
local Refused = Preferences.New(Denied, 'denied')
local DeniedResult = false
Refused:Read(function(Bytes, Failure) DeniedResult = Bytes == nil and Failure == 'Unavailable' end)
assert(DeniedResult)
Refused:Dispose()
local Unavailable = Preferences.New(nil, 'optional')
local Missing = false
Unavailable:Read(function(Bytes, Failure) assert(Bytes == nil and Failure == 'Unavailable'); Missing = true end)
assert(Missing); Unavailable:Dispose()
print('[LUI:LibraryTest] Preferences: lazy service use, byte capture, cancel, late handles, immediate completion and bounds passed')
