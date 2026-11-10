-- Synthetic application checkpoint, not VM serialization. Only this numeric
-- preference is persistable; editor/password/preedit state is never captured.
local Offline = {}
function Offline.Mount(Window)
    if not app:GetService("PlatformService"):Supports("Storage") then return end
    local Storage = app:GetService("StorageService")
    local Count = 0
    local Key = nil
    local ExistingStatus = Window:FindFirstChild("TaskStatus")
    if ExistingStatus then ExistingStatus.Size = UDim2.fromOffset(180, 30) end
    local State = Instance.new("TextLabel", {
        Name = "CheckpointState", Text = "Loading checkpoint", Position = UDim2.fromOffset(212, 72),
        Size = UDim2.fromOffset(88, 32), TextSize = 12, Parent = Window,
    })
    local Save = Instance.new("TextButton", {
        Name = "CheckpointButton", Text = "Persist next", Enabled = false,
        Position = UDim2.fromOffset(304, 56), Size = UDim2.fromOffset(52, 44), Parent = Window,
    })
    local Timer = Instance.new("TextButton", {
        Name = "LifecycleTimer", Text = "Timer idle", Position = UDim2.fromOffset(304, 108),
        Size = UDim2.fromOffset(52, 44), Parent = Window,
    })
    Timer.Activated:Connect(function()
        Timer.Text = "Timer pending"
        task.delay(1, function() Timer.Text = "Timer fired" end)
    end)
    local function Refuse() State.Text = "Checkpoint refused"; Save.Enabled = false end
    local function Committed(Value)
        Count = Value; State.Text = "Committed " .. Value; Save.Enabled = Value < 1000000
    end
    local function Write(Value)
        Save.Enabled = false; State.Text = "Saving checkpoint"
        Storage:Write(buffer.fromstring("LUI-CHECKPOINT\n2\n" .. Value .. "\n"), Key, function(_, _, Failure)
            if Failure then Refuse() else Committed(Value) end
        end)
    end
    Storage:Open(buffer.fromstring("checkpoint"), nil, function(_, Handle, Failure)
        if Failure or not Handle then Refuse(); return end
        Key = Handle
        Storage:Read(buffer.create(0), Key, function(Bytes, _, ReadFailure)
            if ReadFailure then Refuse(); return end
            if not Bytes then Committed(0); return end
            if buffer.len(Bytes) > 64 then Refuse(); return end
            local Version, Number = string.match(buffer.tostring(Bytes), "^LUI%-CHECKPOINT\n([12])\n(%d+)\n$")
            local Value = Number and tonumber(Number)
            if not Value or Value > 1000000 or tostring(Value) ~= Number then Refuse(); return end
            if Version == "1" then Write(Value) else Committed(Value) end
        end)
    end)
    Save.Activated:Connect(function() if Key and Save.Enabled then Write(Count + 1) end end)
end
return Offline
