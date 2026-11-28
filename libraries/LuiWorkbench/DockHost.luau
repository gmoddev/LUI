--!nonstrict
-- Optional desktop presenter. Content belongs to the caller, including while hidden.
local DockHost={}
export type Panel = {Title: string, Content: any, Focus: any?, MinWidth: number?, MinHeight: number?}
export type Options = {Window: any, Root: any, Parking: any?, CanInteract: (() -> boolean)?,
    OnChanged: (() -> ())?, OnGeometry: ((string,any,any) -> ())?, OnAttachment: ((string,any?,boolean) -> ())?,
    OnWindow: ((string,any,boolean) -> ())?, WindowMetrics: any?, Theme: any?, TrackResize: boolean?}
export type Host = {
    Register: (any,string,Panel) -> boolean, Unregister: (any,string) -> boolean, SetTitle: (any,string,string) -> boolean,
    Show: (any,string,string?) -> (boolean,string?), Hide: (any,string) -> (boolean,string?), Activate: (any,string) -> (boolean,string?),
    Move: (any,string,string,string?,string,number?) -> (boolean,string?), Float: (any,string) -> (boolean,string?),
    Redock: (any,string) -> (boolean,string?), IsVisible: (any,string) -> boolean, GetLocation: (any,string) -> (string?,string?,number?),
    Snapshot: (any) -> any, Restore: (any,any) -> (boolean,string?), IsInteracting: (any) -> boolean,
    Cancel: (any) -> (), RefreshGeometry: (any) -> (), Dispose: (any) -> ()}

local Cells={{"Tab","•",0,0},{"Left","←",-1,0},{"Right","→",1,0},{"Top","↑",0,-1},{"Bottom","↓",0,1}}
local function Inside(X,Y,R)
    return X>=R.X and Y>=R.Y and X<R.X+R.Width and Y<R.Y+R.Height
end
local function Cell(Rect,Step,Outer)
    local Radius=if Outer then 12 else 14
    local X=if Outer then Rect.Width/2-Radius else 30
    local Y=if Outer then Rect.Height/2-Radius else 30
    return {X=Rect.X+Rect.Width/2+Step[3]*X-Radius,Y=Rect.Y+Rect.Height/2+Step[4]*Y-Radius,Width=Radius*2,Height=Radius*2}
end
local function Walk(N,F)
    if N then F(N); if N.Axis then Walk(N.First,F); Walk(N.Second,F) end end
end
local function Pixels(Control,R)
    Control.Position=UDim2.fromOffset(R.X,R.Y)
    Control.Size=UDim2.fromOffset(R.Width,R.Height)
end
local function Extent(Control)
    local S=Control.AbsoluteSize
    if S.X>0 and S.Y>0 then return S.X,S.Y end
    -- A hidden or not-yet-shown window has no arranged rectangles. Use its
    -- authored extent until the backend reports the first actual client size.
    local Chain={}
    local Parent=Control
    while Parent and Parent.ClassName~="Window" do table.insert(Chain,Parent); Parent=Parent.Parent end
    local W,H=if Parent then Parent.Size.X.Offset else 0,if Parent then Parent.Size.Y.Offset else 0
    for I=#Chain,1,-1 do
        local Size=Chain[I].Size
        W=W*Size.X.Scale+Size.X.Offset; H=H*Size.Y.Scale+Size.Y.Offset
    end
    return math.max(1,W),math.max(1,H)
end
local function RootTree(Layout,Id)
    if Id=="Main" then return Layout.Main end
    for _,F in Layout.Floats do if F.Id==Id then return F.Tree end end
end

function DockHost.New(Model:any,Options:Options): Host
    local Value:any={}
    local Windows,Panels,Nodes,Connections={},{},{},{}
    local Drag,Resize,LastPreview
    local Active=true
    local Metrics=Options.WindowMetrics
    local Theme=Options.Theme or {}
    local Background=Theme.Background or Color3.fromRGB(33,37,43)
    local Surface=Theme.Surface or Color3.fromRGB(53,60,70)
    local Accent=Theme.Accent or Color3.fromRGB(63,113,165)
    local Selected=Theme.Selected or Color3.fromRGB(32,77,127)
    local Parking=Instance.new("Frame",{Name="DockParking",Parent=Options.Parking or Options.Root,Visible=false})
    local Apply,InstallWindow,WatchTab,WatchDivider
    local function Allowed() return Active and (not Options.CanInteract or Options.CanInteract()) end
    local function Remember(Signal,Fn) table.insert(Connections,Signal:Connect(Fn)) end
    local function HidePreview()
        if not LastPreview then return end
        local H=LastPreview.Host
        H.Preview.Visible=false
        for _,Guide in H.Guides do Guide.Visible=false end
        LastPreview=nil
    end
    function Value:Cancel()
        Drag=nil
        HidePreview()
        if Resize then Resize=nil; Apply(false) end
    end
    function Value:IsInteracting() return Drag~=nil or Resize~=nil end
    local function Impact(Changed)
        Apply(false)
        if Changed and Options.OnChanged then Options.OnChanged() end
    end
    local function Change(Method,...)
        if not Active then return false,"Disposed" end
        Value:Cancel()
        local Old=Model:View()
        local Ok,Why=Method(Model,...)
        if Ok and Model:View()~=Old then Impact(true) end
        return Ok,Why
    end
    function Value:Restore(Input)
        if not Active then return false,"Disposed" end
        Value:Cancel()
        local Ok,Why=Model:Restore(Input)
        if not Ok then return false,Why end
        for _,F in Model:View().Floats do
            local H=Windows[F.Id]
            if H then
                H.Window.Size=UDim2.fromOffset(F.Width,F.Height)
                if Metrics and Metrics.SetBounds and F.Placement then Metrics.SetBounds(H.Window,F.Placement) end
            end
        end
        Impact(true)
        return true
    end
    function Value:Snapshot()
        local Data=Model:Snapshot()
        for _,F in Data.Floats do
            local H=Windows[F.Id]
            if H then
                local Client=H.Window.AbsoluteSize
                if Client.X>1 and Client.Y>1 then F.Width,F.Height=Client.X,Client.Y
                elseif H.ClientWidth and H.ClientHeight then F.Width,F.Height=H.ClientWidth,H.ClientHeight end
                if Metrics and Metrics.GetBounds then
                    H.Placement=Metrics.GetBounds(H.Window) or H.Placement
                    F.Placement=H.Placement or F.Placement
                end
            end
        end
        return Data
    end
    function Value:GetLocation(Id) return Model:GetLocation(Id) end
    function Value:IsVisible(Id) return Model:IsVisible(Id) end
    local function Focus(Id)
        local P=Panels[Id]
        if P and P.Shown then
            local _,HostId=Model:GetLocation(Id)
            if Windows[HostId] then Windows[HostId].LastActive=Id end
            if not P.Focus or not P.Focus:Focus() then P.Tab:Focus() end
        end
    end
    function Value:Show(Id,Group) return Change(Model.Show,Id,Group) end
    function Value:Hide(Id) return Change(Model.Hide,Id) end
    function Value:Activate(Id)
        local Ok,Why=Change(Model.Activate,Id)
        if Ok then Focus(Id) end
        return Ok,Why
    end
    function Value:Move(Id,W,N,S,I)
        local Ok,Why=Change(Model.Move,Id,W,N,S,I)
        if Ok then Focus(Id) end
        return Ok,Why
    end
    function Value:Float(Id,Placement)
        local Ok,Why=Change(Model.Float,Id,Placement)
        if Ok then Focus(Id) end
        return Ok,Why
    end
    function Value:Redock(Id)
        local H=Windows[Id]
        local Last=H and H.LastActive
        local Ok,Why=Change(Model.Redock,Id)
        if Ok and Last and Model:GetLocation(Last) then Value:Activate(Last) end
        return Ok,Why
    end
    local function MakeButton(Parent,Name,Title)
        return Instance.new("TextButton",{Parent=Parent,Name=Name,Text=Title,AccessibilityLabel=Title,BackgroundColor3=Surface})
    end
    local function WindowRoot(Id,Window,Root)
        local H={Id=Id,Window=Window,Root=Root,Guides={},Rects={}}
        H.Preview=Instance.new("Frame",{Name="DockPreview",Parent=Root,ZIndex=90,Visible=false,
            BackgroundColor3=Accent,BackgroundTransparency=0.55,BorderColor3=Accent,BorderSizePixel=2})
        H.Caption=Instance.new("TextLabel",{Name="DockDestination",Parent=H.Preview,Position=UDim2.fromOffset(4,2),
            Size=UDim2.new(1,-8,0,24),Text="",BackgroundColor3=Background,ZIndex=90})
        for _,Step in Cells do
            for _,Outer in {false,true} do
                if Outer and Step[1]=="Tab" then continue end
                local Key=(if Outer then "Root" else "")..Step[1]
                H.Guides[Key]=Instance.new("TextLabel",{Name="DockGuide"..Key,Parent=Root,Text=Step[2],
                    Size=UDim2.fromOffset(if Outer then 24 else 28,if Outer then 24 else 28),BackgroundColor3=Accent,Visible=false,ZIndex=91})
            end
        end
        Windows[Id]=H
        return H
    end
    WindowRoot("Main",Options.Window,Options.Root)
    InstallWindow=function(F)
        local Window=Instance.new("Window",{Name=F.Id,Title="Floating panels",Size=UDim2.fromOffset(F.Width,F.Height)})
        local Root=Instance.new("Frame",{Name="DockRoot",Parent=Window,Position=UDim2.fromOffset(0,28),
            Size=UDim2.new(1,0,1,-28),BackgroundColor3=Background})
        local H=WindowRoot(F.Id,Window,Root)
        H.Placement=F.Placement
        local Back=MakeButton(Window,"DockReturn","Redock")
        Back.Size=UDim2.fromOffset(128,28)
        Back.Activated:Connect(function() if Allowed() and Windows[F.Id]==H then Value:Redock(F.Id) end end)
        Window.CloseRequested:Connect(function()
            if H.Retiring or not Active then return end
            if not Allowed() then Window:PreventClose(); return end
            H.Closing=true
            local Ok=Value:Redock(F.Id)
            if not Ok then H.Closing=false; Window:PreventClose() end
        end)
        Window.Closed:Connect(function() Window:Destroy() end)
        Window.Shortcut:Connect(function(Input)
            if Input.Key=="Escape" and Value:IsInteracting() then Value:Cancel() end
        end)
        if Options.OnWindow then Options.OnWindow(F.Id,Window,true) end
        Window.Visible=true
        if F.Placement and Metrics and Metrics.SetBounds then Metrics.SetBounds(Window,F.Placement) end
        return H
    end
    local function Arrange(H,N,W,Ht,Rects)
        -- Scale with the real native client between notifications. RefreshGeometry
        -- re-evaluates minimum sizes on resize without rebuilding any controls.
        Walk(N,function(Node)
            local UI=Nodes[Node.Id]
            local R=Rects[Node.Id]
            UI.Frame.Position=UDim2.fromScale(R.X/W,R.Y/Ht)
            UI.Frame.Size=UDim2.fromScale(R.Width/W,R.Height/Ht)
            if UI.Bar then
                local A,B=Rects[Node.First.Id],Rects[Node.Second.Id]
                if Node.Axis=="X" then
                    UI.Bar.Position=UDim2.new(A.Width/math.max(1,R.Width),0,0,0)
                    UI.Bar.Size=UDim2.new(0,math.max(0,B.X-A.X-A.Width),1,0)
                else
                    UI.Bar.Position=UDim2.new(0,0,A.Height/math.max(1,R.Height),0)
                    UI.Bar.Size=UDim2.new(1,0,0,math.max(0,B.Y-A.Y-A.Height))
                end
            end
        end)
        H.Width,H.Height,H.Rects=W,Ht,Rects
    end
    local function Attach(P,Window,Shown)
        if P.Host~=Window or P.Shown~=Shown then
            P.Host,P.Shown=Window,Shown
            if Options.OnAttachment then Options.OnAttachment(P.Id,Window,Shown) end
        end
    end
    Apply=function(GeometryOnly)
        if not Active then return end
        local Layout=Model:View()
        local LiveNodes,LiveWindows,Placed={},{Main=true},{}
        for _,F in Layout.Floats do LiveWindows[F.Id]=true; if not Windows[F.Id] then InstallWindow(F) end end
        for Id,H in Windows do
            if not LiveWindows[Id] then continue end
            local N=RootTree(Layout,Id)
            local W,Ht=Extent(H.Root)
            if GeometryOnly and W==H.Width and Ht==H.Height then continue end
            if Id~="Main" then
                local Client=H.Window.AbsoluteSize
                if Client.X>1 and Client.Y>1 then H.ClientWidth,H.ClientHeight=Client.X,Client.Y end
            end
            Walk(N,function(Node)
                LiveNodes[Node.Id]=true
                local UI=Nodes[Node.Id]
                if not UI then
                    UI={Frame=Instance.new("Frame",{Name="DockNode"..Node.Id,Parent=H.Root,
                        BackgroundTransparency=if Node.Axis then 1 else 0,BackgroundColor3=Background,ClipsDescendants=true,
                        ZIndex=if Node.Axis then 1 else 2})}
                    Nodes[Node.Id]=UI
                end
                if UI.Frame.Parent~=H.Root then UI.Frame.Parent=H.Root end
                if Node.Axis and not UI.Bar then
                    UI.Bar=MakeButton(UI.Frame,"DockDivider"..Node.Id,"")
                    UI.Bar.AccessibilityLabel="Resize "..Node.Axis.." split"
                    UI.Bar.BackgroundColor3=Surface
                    WatchDivider(Node.Id,UI.Bar)
                elseif not Node.Axis and UI.Bar then UI.Bar:Destroy(); UI.Bar=nil end
                UI.Frame.ZIndex=if Node.Axis then 1 else 2
                UI.Frame.BackgroundTransparency=if Node.Axis then 1 else 0
                if Node.Tabs and not GeometryOnly then
                    for Index,PanelId in Node.Tabs do
                        local P=Panels[PanelId]
                        Placed[PanelId]=true
                        local Shown=PanelId==Node.Active
                        if P.Tab.Parent~=UI.Frame then P.Tab.Parent=UI.Frame end
                        P.Tab.Position=UDim2.fromScale((Index-1)/#Node.Tabs,0)
                        P.Tab.Size=UDim2.new(1/#Node.Tabs,0,0,32)
                        P.Tab.BackgroundColor3=if Shown and #Node.Tabs>1 then Surface else Background
                        P.Tab.BorderSizePixel=if Shown and #Node.Tabs>1 then 1 else 0
                        P.Tab.BorderColor3=Accent
                        if P.Content.Parent~=UI.Frame then P.Content.Parent=UI.Frame end
                        P.Content.Position=UDim2.fromOffset(0,32); P.Content.Size=UDim2.new(1,0,1,-32)
                        P.Content.Visible=Shown
                        Attach(P,H.Window,Shown)
                    end
                end
            end)
            Arrange(H,N,W,Ht,Model:Arrange(N,W,Ht))
            if Options.OnGeometry then
                Walk(N,function(Node)
                    if Node.Tabs then Options.OnGeometry(Node.Active,H.Window,Panels[Node.Active].Content) end
                end)
            end
        end
        if GeometryOnly then return end
        -- Content must leave obsolete native groups before any ancestor is destroyed.
        for Id,P in Panels do
            if not Placed[Id] then
                P.Content.Visible=false
                if P.Content.Parent~=Parking then P.Content.Parent=Parking end
                if P.Tab.Parent~=Parking then P.Tab.Parent=Parking end
                Attach(P,nil,false)
            end
        end
        for Id,UI in Nodes do if not LiveNodes[Id] then UI.Frame:Destroy(); Nodes[Id]=nil end end
        for Id,H in Windows do
            if not LiveWindows[Id] then
                Windows[Id]=nil; H.Retiring=true
                if Options.OnWindow then Options.OnWindow(Id,H.Window,false) end
                if not H.Closing then
                    local Ok=H.Window:RequestClose()
                    if not Ok then H.Window:Destroy() end
                end
            end
        end
    end
    function Value:RefreshGeometry() Apply(true) end
    local function Resolve(Input,Source,H)
        local P=Source.AbsolutePosition
        local X,Y=P.X+Input.Position.X,P.Y+Input.Position.Y
        local ScreenX,ScreenY
        if Metrics and Metrics.ToScreen and Metrics.FromScreen and Metrics.WindowAt then
            ScreenX,ScreenY=Metrics.ToScreen(H.Window,X,Y)
            if ScreenX and ScreenY then
                local Under=Metrics.WindowAt(ScreenX,ScreenY)
                for Id,Other in Windows do
                    if Under==Other.Window or Under==Id then
                        local LocalX,LocalY=Metrics.FromScreen(Other.Window,ScreenX,ScreenY)
                        if LocalX and LocalY then X,Y,H=LocalX,LocalY,Other end
                        break
                    end
                end
                if not Under then return nil,X,Y,ScreenX,ScreenY end
                if Under~=H.Id and Under~=H.Window then return nil,X,Y,ScreenX,ScreenY end
            end
        end
        local Origin=H.Root.AbsolutePosition
        return H,X-Origin.X,Y-Origin.Y,ScreenX,ScreenY
    end
    local function Target(H,X,Y,Moving)
        local Root=RootTree(Model:View(),H.Id)
        local Hover,Node,Side,Slot
        Walk(Root,function(N)
            local R=H.Rects[N.Id]
            if N.Axis or not Inside(X,Y,R) then return end
            Hover=N
            if Y<R.Y+32 then
                Node,Side,Slot=N,"Tab",math.clamp(math.floor((X-R.X)/math.max(1,R.Width)*#N.Tabs+0.5)+1,1,#N.Tabs+1)
            end
            for _,Step in Cells do
                if (Step[1]=="Tab" or #N.Tabs>1 or N.Tabs[1]~=Moving) and Inside(X,Y,Cell(R,Step,false)) then
                    Node,Side,Slot=N,Step[1],nil
                end
            end
            if not Side and (#N.Tabs>1 or N.Tabs[1]~=Moving) then
                local EdgeX=math.min(160,R.Width*.24)
                local EdgeY=math.min(96,math.max(1,R.Height-32)*.24)
                local Left,Right=(X-R.X)/EdgeX,(R.X+R.Width-X)/EdgeX
                local Top,Bottom=(Y-R.Y-32)/EdgeY,(R.Y+R.Height-Y)/EdgeY
                local Nearest=math.min(Left,Right,Top,Bottom)
                Node=N
                Side=if Nearest>1 then "Tab" elseif Nearest==Left then "Left" elseif Nearest==Right then "Right" elseif Nearest==Top then "Top" else "Bottom"
            end
        end)
        if Root and Root.Axis then
            for _,Step in Cells do
                if Step[1]~="Tab" and Inside(X,Y,Cell(H.Rects[Root.Id],Step,true)) then
                    return Hover,Root,Step[1],nil,true
                end
            end
        elseif not Root and X>=0 and Y>=0 and X<H.Width and Y<H.Height then
            return nil,nil,"Tab",nil,true
        end
        return Hover,Node,Side,Slot,false
    end
    local function UpdateDrag(Source,Input)
        if not Input.Position then return end
        local D=Drag
        local P=Source.AbsolutePosition
        if not D.Started and math.max(math.abs(P.X+Input.Position.X-D.X),math.abs(P.Y+Input.Position.Y-D.Y))<6 then return end
        D.Started=true
        local H,X,Y,SX,SY=Resolve(Input,Source,D.Host)
        D.ScreenX,D.ScreenY=SX,SY
        D.InsideDock=H~=nil and X>=0 and Y>=0 and X<H.Width and Y<H.Height
        if not H then HidePreview(); D.Plan=nil; return end
        local W,Ht=Extent(H.Root)
        if W~=H.Width or Ht~=H.Height then Value:RefreshGeometry() end
        local Hover,Node,Side,Slot,Outer=Target(H,X,Y,D.Id)
        local Last=LastPreview
        if Last and Last.Host==H and Last.Hover==Hover and Last.Node==Node and Last.Side==Side and Last.Slot==Slot
            and Last.Outer==Outer and Last.W==W and Last.H==Ht and Last.Layout==Model:View() then return end
        if Last and Last.Host~=H then HidePreview() end
        local Plan=if Side then Model:PrepareMove(D.Id,H.Id,Node and Node.Id,Side,Slot) else nil
        D.Plan=Plan
        LastPreview={Host=H,Hover=Hover,Node=Node,Side=Side,Slot=Slot,Outer=Outer,W=W,H=Ht,Layout=Model:View()}
        local Root=RootTree(Model:View(),H.Id)
        for _,Step in Cells do
            for _,Edge in {false,true} do
                if Edge and Step[1]=="Tab" then continue end
                local Key=(if Edge then "Root" else "")..Step[1]
                local Guide=H.Guides[Key]
                local R=if Edge then Root and Root.Axis and H.Rects[Root.Id] else Hover and H.Rects[Hover.Id]
                local Show=R and (Edge or Step[1]=="Tab" or #Hover.Tabs>1 or Hover.Tabs[1]~=D.Id)
                Guide.Visible=if Show then true else false
                if Show then
                    Pixels(Guide,Cell(R,Step,Edge))
                    Guide.BackgroundColor3=if Edge==Outer and Step[1]==Side then Selected else Accent
                end
            end
        end
        H.Preview.Visible=Plan~=nil
        if not Plan then return end
        H.Caption.Visible=Slot==nil
        local Destination=if Outer then "workspace" elseif Node and Node.Active then Panels[Node.Active].Title else "workspace"
        H.Caption.Text=if Side=="Tab" then "Tab with "..Destination else Destination.." · split "..string.lower(Side)
        local NewRoot=RootTree(Plan.Layout,H.Id)
        local R=Model:Arrange(NewRoot,W,Ht)[Plan.Group]
        if Slot then
            local Existing=H.Rects[Node.Id]
            R={X=Existing.X+math.clamp((Slot-1)*Existing.Width/#Node.Tabs,0,math.max(0,Existing.Width-4)),
                Y=Existing.Y,Width=4,Height=32}
        end
        Pixels(H.Preview,R)
    end
    local function FloatPlacement(D)
        if not D.ScreenX or not Metrics or not Metrics.ToScreen then return nil end
        local X,Y=Metrics.ToScreen(D.Host.Window,0,0)
        local R,B=Metrics.ToScreen(D.Host.Window,560,400)
        if X and R and Y and B then
            return {X=math.round(D.ScreenX-32),Y=math.round(D.ScreenY-16),Width=math.max(1,math.round(R-X)),Height=math.max(1,math.round(B-Y))}
        end
        return nil
    end
    WatchTab=function(P)
        local Source,Id=P.Tab,P.Id
        Source.InputBegan:Connect(function(Input)
            if Input.Device=="Keyboard" then if Input.Key=="Escape" then Value:Cancel() end; return end
            if not Allowed() or Input.Device~="Mouse" or Input.Button~="Left" or Input.IsCanceled or Drag or Resize then return end
            local _,Window=Model:GetLocation(Id)
            if not Window then return end
            local Position=Source.AbsolutePosition
            Drag={Id=Id,Host=Windows[Window],Pointer=Input.PointerId,X=Position.X+Input.Position.X,Y=Position.Y+Input.Position.Y}
            Source:Focus()
        end)
        Source.InputChanged:Connect(function(Input)
            if Drag and Drag.Id==Id and Drag.Pointer==Input.PointerId then
                if Allowed() then UpdateDrag(Source,Input) else Value:Cancel() end
            end
        end)
        Source.InputEnded:Connect(function(Input)
            if not Drag or Drag.Id~=Id or Drag.Pointer~=Input.PointerId then return end
            if Input.IsCanceled or not Allowed() then Value:Cancel(); return end
            if Input.Button~="Left" then return end
            UpdateDrag(Source,Input)
            local D=Drag
            Value:Cancel()
            if not D.Started then Value:Activate(Id); return end
            if D.Plan then
                if Model:Commit(D.Plan) then Impact(true); Focus(Id) end
                return
            end
            if D.InsideDock then return end
            local Position=FloatPlacement(D)
            local N=RootTree(Model:View(),D.Host.Id)
            if D.Host.Id~="Main" and N and N.Tabs and #N.Tabs==1 then
                if Position and Metrics.SetBounds then
                    local Size=Metrics.GetBounds and Metrics.GetBounds(D.Host.Window)
                    if Size then Position.Width,Position.Height=Size.Width,Size.Height end
                    Metrics.SetBounds(D.Host.Window,Position)
                    if Options.OnChanged then Options.OnChanged() end
                end
                return
            end
            Value:Float(Id,Position)
        end)
        Source.Activated:Connect(function() if Allowed() and not Drag and not Resize then Value:Activate(Id) end end)
        Source.FocusLost:Connect(function() if Drag and Drag.Id==Id then Value:Cancel() end end)
        Source.Focused:Connect(function()
            local _,W=Model:GetLocation(Id)
            if W and Windows[W] then Windows[W].LastActive=Id end
        end)
    end
    WatchDivider=function(Id,Bar)
        local function Move(Input)
            local D=Resize
            if not D or D.Id~=Id or not Input.Position or Input.PointerId~=D.Pointer then return end
            local N,W=Model:Find(Id)
            if not N or not N.Axis or not Allowed() then Value:Cancel(); return end
            local H=Windows[W]
            local Width,Height=Extent(H.Root)
            local Layout=RootTree(Model:View(),W)
            local R=H.Rects[Id]
            local Size=if N.Axis=="X" then R.Width else R.Height
            if Size<1 then return end
            local Lo,Hi=Model:Limits(N,Size)
            local Pointer=Bar.AbsolutePosition[N.Axis]+Input.Position[N.Axis]
            D.Ratio=math.clamp(D.StartRatio+(Pointer-D.StartPointer)/Size,Lo,Hi)
            -- Project a transient ratio; never change the committed model during capture.
            local Rects=Model:Arrange(Layout,Width,Height,Id,D.Ratio)
            Arrange(H,Layout,Width,Height,Rects)
            if Options.OnGeometry then Walk(Layout,function(Node)
                if Node.Tabs then Options.OnGeometry(Node.Active,H.Window,Panels[Node.Active].Content) end
            end) end
        end
        Bar.InputBegan:Connect(function(Input)
            if Input.Device=="Keyboard" then if Input.Key=="Escape" then Value:Cancel() end; return end
            if not Allowed() or Input.Device~="Mouse" or Input.Button~="Left" or Input.IsCanceled or Resize or Drag then return end
            local N,W=Model:Find(Id)
            if not N or not N.Axis or not Input.Position then return end
            local R=Windows[W].Rects[Id]
            local Size=if N.Axis=="X" then R.Width else R.Height
            if Size<1 then return end
            local Lo,Hi=Model:Limits(N,Size)
            Resize={Id=Id,Pointer=Input.PointerId,StartRatio=math.clamp(N.Ratio,Lo,Hi),
                StartPointer=Bar.AbsolutePosition[N.Axis]+Input.Position[N.Axis]}
            Bar:Focus()
        end)
        Bar.InputChanged:Connect(Move)
        Bar.InputEnded:Connect(function(Input)
            if not Resize or Resize.Id~=Id or Input.PointerId~=Resize.Pointer then return end
            if Input.IsCanceled or not Allowed() then Value:Cancel(); return end
            if Input.Button~="Left" then return end
            Move(Input)
            local D=Resize; Resize=nil
            if D and D.Ratio and Model:SetRatio(Id,D.Ratio) then Impact(true) end
        end)
        Bar.FocusLost:Connect(function() if Resize and Resize.Id==Id then Value:Cancel() end end)
    end
    function Value:Register(Id,Spec)
        if not Active or type(Spec)~="table" or type(Spec.Title)~="string" or not Spec.Content then return false end
        local Ok,IsControl=pcall(function() return Spec.Content:IsA("GuiObject") end)
        if not Ok or not IsControl then return false end
        for _,P in Panels do if P.Content==Spec.Content then return false end end
        local Ancestor=Parking
        while Ancestor do
            if Ancestor==Spec.Content then return false end
            Ancestor=Ancestor.Parent
        end
        if not Model:Register(Id,Spec.MinWidth,Spec.MinHeight) then return false end
        local C=Spec.Content
        local P={Id=Id,Title=Spec.Title,Content=C,Focus=Spec.Focus,Parent=C.Parent,Position=C.Position,Size=C.Size,Visible=C.Visible,
            Tab=MakeButton(Parking,"DockTab"..Id,Spec.Title)}
        Panels[Id]=P
        C.Parent=Parking; C.Visible=false
        WatchTab(P)
        return true
    end
    function Value:SetTitle(Id,Title)
        local P=Panels[Id]
        if not P or type(Title)~="string" then return false end
        P.Title=Title; P.Tab.Text=Title; P.Tab.AccessibilityLabel=Title
        return true
    end
    local function Release(P)
        P.Content.Parent=P.Parent
        P.Content.Position=P.Position; P.Content.Size=P.Size; P.Content.Visible=P.Visible
        P.Tab:Destroy()
    end
    function Value:Unregister(Id)
        local P=Panels[Id]
        if not P or not Active then return false end
        Value:Cancel()
        Model:Unregister(Id)
        Panels[Id]=nil; Release(P)
        if Options.OnAttachment then Options.OnAttachment(Id,nil,false) end
        Impact(true)
        return true
    end
    function Value:Dispose()
        if not Active then return end
        Value:Cancel(); Active=false
        for _,C in Connections do C:Disconnect() end
        table.clear(Connections)
        for Id,P in Panels do
            Model:Unregister(Id)
            Release(P)
            if Options.OnAttachment then Options.OnAttachment(Id,nil,false) end
        end
        table.clear(Panels)
        for _,UI in Nodes do UI.Frame:Destroy() end
        table.clear(Nodes)
        for Id,H in Windows do
            if Id=="Main" then
                H.Preview:Destroy()
                for _,Guide in H.Guides do Guide:Destroy() end
            else
                H.Retiring=true
                if Options.OnWindow then Options.OnWindow(Id,H.Window,false) end
                H.Window:Destroy()
            end
        end
        table.clear(Windows); Parking:Destroy()
    end
    Remember(Options.Window.Shortcut,function(Input) if Input.Key=="Escape" then Value:Cancel() end end)
    Remember(Options.Window.Changed,function(Property)
        if Property=="Visible" or Property=="Size" then Value:RefreshGeometry() end
    end)
    if Options.TrackResize~=false then
        -- Foundation 1 exposes derived sizes and no geometry-changed signal.
        -- Consumers with an owner resize pump call RefreshGeometry themselves.
        local function Observe()
            if not Active then return end
            Value:RefreshGeometry()
            task.delay(0.12,Observe)
        end
        task.delay(0.12,Observe)
    end
    return Value
end
return table.freeze(DockHost)
