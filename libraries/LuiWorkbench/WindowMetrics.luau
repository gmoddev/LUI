--!strict
-- Window-local logical coordinates and global physical pixels are kept separate.
local WindowMetrics = {}

export type Rectangle = {X:number,Y:number,Width:number,Height:number}
export type Adapter = {
    read OnWindow: (string,any,boolean) -> (),
    read GetBounds: (any) -> Rectangle?,
    read SetBounds: (any,Rectangle) -> boolean,
    read ToScreen: (any,number,number) -> (number?,number?),
    read FromScreen: (any,number,number) -> (number?,number?),
    read WindowAt: (number,number) -> any?,
}
function WindowMetrics.New(Main: any): Adapter?
    local Ok, Service = pcall(function() return (app :: any):GetService('DesktopWindowMetrics') end)
    if not Ok or not Service then return nil end
    local Native: any = Service
    local Owned: {[any]: boolean} = {[Main] = true}
    local Metrics = {}
    function Metrics.OnWindow(_Id: string, Window: any, Added: boolean)
        Owned[Window] = if Added then true else nil
        if Added and Window ~= Main and Native:Own(Window.Name, Main.Name) ~= 1 then
            task.defer(function() if Owned[Window] then Native:Own(Window.Name, Main.Name) end end)
        end
    end
    function Metrics.GetBounds(Window: any): {X:number,Y:number,Width:number,Height:number}?
        if not Owned[Window] then return nil end
        local W, H = Native:Read(Window.Name, 3), Native:Read(Window.Name, 4)
        if W < 1 or H < 1 then return nil end
        return {X=Native:Read(Window.Name, 1), Y=Native:Read(Window.Name, 2), Width=W, Height=H}
    end
    function Metrics.SetBounds(Window: any, Rect: {X:number,Y:number,Width:number,Height:number})
        if not Owned[Window] then return false end
        local function Place(): boolean
            return Native:Place(Window.Name, Rect.X, Rect.Y, Rect.Width, Rect.Height) == 1
        end
        if Place() then return true end
        -- Native creation is delivered after the owner callback that committed the float.
        task.defer(function() if Owned[Window] then Place() end end)
        return false
    end
    function Metrics.ToScreen(Window: any, X: number, Y: number): (number?,number?)
        if not Owned[Window] then return nil,nil end
        local Scale = Native:Read(Window.Name, 5)
        if Scale <= 0 then return nil,nil end
        return Native:Read(Window.Name,6) + X*Scale, Native:Read(Window.Name,7) + Y*Scale
    end
    function Metrics.FromScreen(Window: any, X: number, Y: number): (number?,number?)
        if not Owned[Window] then return nil,nil end
        local Scale = Native:Read(Window.Name, 5)
        if Scale <= 0 then return nil,nil end
        return (X-Native:Read(Window.Name,6))/Scale, (Y-Native:Read(Window.Name,7))/Scale
    end
    function Metrics.WindowAt(X: number,Y: number): any?
        local Token = Native:At(math.round(X),math.round(Y))
        if Token <= 0 then return nil end
        for Window in Owned do
            if Native:Read(Window.Name,0) == Token then return Window end
        end
        return nil
    end
    return table.freeze(Metrics)
end

return table.freeze(WindowMetrics)
