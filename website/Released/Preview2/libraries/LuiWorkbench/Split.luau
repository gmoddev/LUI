--!strict
-- Pure mouse divider gesture. The view supplies window-relative logical X.
local Split = {}
export type Gesture = {
    Begin: (Self: Gesture, Input: InputEvent, X: number, Width: number, Ratio: number) -> boolean,
    Move: (Self: Gesture, Input: InputEvent, X: number, Width: number) -> number?,
    Finish: (Self: Gesture, Input: InputEvent) -> number?,
    Cancel: (Self: Gesture) -> number?,
    Dispose: (Self: Gesture) -> (),
}
local function Finite(Value: any): boolean
    return type(Value) == 'number' and Value == Value and math.abs(Value) < math.huge
end
function Split.New(): Gesture
    local Drag: any = nil
    local Alive = true
    local Value: any = {}
    function Value.Begin(_Self: any, Input: any, X: number, Width: number, Ratio: number): boolean
        if not Alive or Drag or Input.Device ~= 'Mouse' or Input.Button ~= 'Left' or Input.IsCanceled
            or not Finite(Input.PointerId) or Input.PointerId % 1 ~= 0 or Input.PointerId < 0
            or not Finite(X) or not Finite(Width) or Width < 488
            or not Finite(Ratio) or Ratio < 0.1 or Ratio > 0.9 then return false end
        local Minimum = math.max(0.1, 240 / (Width - 8))
        Drag = {Id = Input.PointerId, X = X, Width = Width, Before = Ratio, Current = Ratio,
            Effective = math.clamp(Ratio, Minimum, 1 - Minimum), Minimum = Minimum}
        return true
    end
    function Value.Cancel(_Self: any): number?
        if not Drag then return nil end
        local Before = Drag.Before; Drag = nil; return Before
    end
    function Value.Move(Self: any, Input: any, X: number, Width: number): number?
        if not Drag or Input.Device ~= 'Mouse' or Input.PointerId ~= Drag.Id then return nil end
        if Input.IsCanceled or Width ~= Drag.Width or not Finite(X) then return Self:Cancel() end
        Drag.Current = math.clamp(Drag.Effective + (X - Drag.X) / (Width - 8), Drag.Minimum, 1 - Drag.Minimum)
        return Drag.Current
    end
    function Value.Finish(Self: any, Input: any): number?
        if not Drag or Input.Device ~= 'Mouse' or Input.Button ~= 'Left' or Input.PointerId ~= Drag.Id then return nil end
        if Input.IsCanceled then return Self:Cancel() end
        local Current = Drag.Current; Drag = nil; return Current
    end
    function Value.Dispose(_Self: any) Alive = false; Drag = nil end
    return table.freeze(Value) :: Gesture
end
return table.freeze(Split)
