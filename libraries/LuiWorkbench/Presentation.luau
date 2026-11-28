--!strict
-- A new ticket fences queued callbacks when the producer, window, or pixel extent changes.
local Presentation = {}

export type Ticket = {read Width: number, read Height: number}
export type Attachment = {
    Attach: (Self: Attachment, Surface: FrameSurface, Width: number, Height: number) -> Ticket,
    Submit: (Self: Attachment, Ticket: Ticket, Sequence: number, Pixels: buffer, Stride: number, Format: string) -> (boolean, string?),
    Detach: (Self: Attachment, Ticket: Ticket) -> boolean,
    Destroy: (Self: Attachment) -> (),
}

function Presentation.New(): Attachment
    local Target: FrameSurface? = nil
    local Current: Ticket? = nil
    local Destroying: Connection? = nil
    local Last = -1
    local Closed = false
    local function Release(Clear: boolean)
        local Surface = Target
        local Connection = Destroying
        Target, Current, Destroying = nil, nil, nil
        if Connection then Connection:Disconnect() end
        if Clear and Surface then Surface:ClearFrame() end
    end
    local Controller = {} :: any
    function Controller:Attach(Surface: FrameSurface, Width: number, Height: number): Ticket
        assert(not Closed and Surface:IsA("FrameSurface"), "[LUI:Presentation] Invalid target")
        assert(Width >= 1 and Width <= 1920 and Height >= 1 and Height <= 1080 and
            Width % 1 == 0 and Height % 1 == 0, "[LUI:Presentation] Invalid pixel extent")
        Release(true)
        local Ticket: Ticket = table.freeze({Width=Width, Height=Height})
        Current, Target, Last = Ticket, Surface, -1
        Destroying = Surface.Destroying:Connect(function()
            if Current == Ticket then Release(false) end
        end)
        return Ticket
    end
    function Controller:Submit(Ticket: Ticket, Sequence: number, Pixels: buffer, Stride: number, Format: string): (boolean,string?)
        local Surface = Target
        if Current ~= Ticket or not Surface then return false, "RetiredAttachment" end
        if Sequence < 0 or Sequence % 1 ~= 0 or Sequence > 9007199254740991 or Sequence <= Last then
            return false, "StaleSequence"
        end
        local Accepted, Reason = Surface:SubmitFrame(Pixels, Ticket.Width, Ticket.Height, Stride, Format)
        if Accepted then Last = Sequence end
        return Accepted, Reason
    end
    function Controller:Detach(Ticket: Ticket): boolean
        if Current ~= Ticket or not Target then return false end
        Release(true)
        return true
    end
    function Controller:Destroy()
        Closed = true
        Release(true)
    end
    return table.freeze(Controller)
end

return table.freeze(Presentation)
