--!strict
local Documents = require('@LuiWorkbench/Documents')
local Workspace = require('@LuiWorkbench/Workspace')
local Split = require('@LuiWorkbench/Split')
local Store, Model, Divider = Documents.New(), Workspace.New(), Split.New()
local Pointer: any = {Device = 'Mouse', Button = 'Left', PointerId = 1, IsCanceled = false}
-- 640 distinct documents pass through the admitted 32-resident working set.
-- Capacity remains a contract; exceeding it must fail rather than grow silently.
for Batch = 1, 20 do
    for Index = 1, 32 do
        local Id = tostring(Batch) .. '-' .. tostring(Index)
        Store:Open(Id, Id .. '.luau', string.rep('synthetic line\n', 200))
        Model:Add(Id, if Index % 2 == 0 then 'Right' else 'Left')
    end
    assert(not pcall(function() Store:Open('overflow', 'overflow', '') end))
    for Index = 1, 32 do
        local Id = tostring(Batch) .. '-' .. tostring(Index)
        Store:SetText(Id, 'edited ' .. Id)
        local Save = Store:BeginSave(Id)
        assert(not Store:Close(Id, true))
        Store:SetText(Id, 'later ' .. Id)
        if Index % 2 == 0 then assert(Store:CancelSave(Save)) else assert(Store:CompleteSave(Save, true)) end
        assert((Store:Get(Id) :: any).Dirty)
        Model:Dock(Id, 'Left'); Model:Dock(Id, 'Right')
        local Snapshot = Model:Encode(); assert(Model:Decode(Snapshot))
        assert(Divider:Begin(Pointer, 100, 800, Model:Snapshot().Ratio))
        local Ratio = Divider:Move(Pointer, 100 + Index * 10, 800)
        assert(Ratio); Model:SetRatio(Ratio :: number); assert(Divider:Cancel())
        Model:Layout(if Index % 2 == 0 then 400 else 1200)
        assert(Store:Close(Id, true) and Model:Remove(Id))
    end
    assert(#Store:List() == 0 and #Model:Snapshot().Left == 0 and #Model:Snapshot().Right == 0)
end
Store:Open('pending', 'pending', 'shutdown'); Store:BeginSave('pending')
Store:Dispose(); Model:Dispose(); Divider:Dispose()
print('[LUI:LibraryStress] 640 documents, bounded resident set, saves/cancel, redocking, resizing and pending disposal passed')
