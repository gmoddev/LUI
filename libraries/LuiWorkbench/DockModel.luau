--!nonstrict
-- An opt-in owner-domain layout. Prepared transactions contain no Instances.
local DockModel = {}

export type Group = {Id: string, Tabs: {string}, Active: string}
export type Split = {Id: string, Axis: string, Ratio: number, First: Node, Second: Node}
export type Node = Group | Split
export type OriginSplit = {Id: string, Axis: string, Ratio: number, First: boolean, Anchor: string, Panel: string, Window: string}
export type Hidden = {Panel: string, Group: string, Window: string, Slot: number, Split: OriginSplit?}
export type Float = {Id: string, Tree: Node, Width: number, Height: number, OriginGroup: string?, OriginSlot: number?, OriginSplit: OriginSplit?, Placement: any?}
export type Snapshot = {Version: number, Main: Node?, Floats: {Float}, Hidden: {Hidden}?}
export type Rectangle = {X: number, Y: number, Width: number, Height: number}
export type Plan = {Layout: Snapshot, Group: string?, Panel: string?}
export type Model = {
    Register: (any, string, number?, number?) -> boolean, Unregister: (any, string) -> boolean,
    Snapshot: (any) -> Snapshot, View: (any) -> Snapshot, Restore: (any, any) -> (boolean, string?),
    GetLocation: (any, string) -> (string?, string?, number?), IsVisible: (any, string) -> boolean,
    PrepareMove: (any, string, string, string?, string, number?) -> (Plan?, string?),
    Commit: (any, Plan) -> (boolean, string?), Move: (any, string, string, string?, string, number?) -> (boolean, string?),
    Show: (any, string, string?) -> (boolean, string?), Hide: (any, string) -> (boolean, string?),
    Activate: (any, string) -> (boolean, string?), Float: (any, string, any?) -> (boolean, string?),
    Redock: (any, string) -> (boolean, string?), SetRatio: (any, string, number) -> (boolean, string?),
    Arrange: (any, Node?, number, number, string?, number?) -> {[string]: Rectangle}, Find: (any, string) -> (any, string?),
    Limits: (any, Node, number) -> (number, number), SetWindowSize: (any,string,number,number,any?) -> (boolean,string?)
}
local function Finite(Value)
    return type(Value)=="number" and Value==Value and math.abs(Value)<math.huge
end
local function Identity(Value)
    return type(Value)=="string" and #Value>0 and #Value<=128 and utf8.len(Value)~=nil and not string.find(Value,"%c")
end
local function Integer(Value,Low,High)
    return Finite(Value) and Value%1==0 and Value>=Low and Value<=High
end
local function Plain(Value)
    if type(Value)~="table" then return false end
    local Meta=getmetatable(Value)
    -- Json.Decode uses frozen empty brand tables to distinguish arrays and objects.
    return Meta==nil or (type(Meta)=="table" and table.isfrozen(Meta) and next(Meta)==nil)
end
local function Array(Value,Maximum)
    if not Plain(Value) or #Value>Maximum then return false end
    local Count=0
    for Key in Value do
        if not Integer(Key,1,Maximum) then return false end
        Count+=1
    end
    return Count==#Value
end
local function Copy(Value)
    if type(Value)~="table" then return Value end
    local Result={}
    for K,V in Value do Result[K]=Copy(V) end
    return Result
end
local function Freeze(Value)
    for _,V in Value do if type(V)=="table" then Freeze(V) end end
    return table.freeze(Value)
end
local function Walk(Node,Test)
    if not Node then return nil end
    if Test(Node) then return Node end
    if Node.Axis then return Walk(Node.First,Test) or Walk(Node.Second,Test) end
    return nil
end
local function Locate(Layout,Test)
    local Node=Walk(Layout.Main,Test)
    if Node then return Node,"Main" end
    for _,Window in Layout.Floats do
        Node=Walk(Window.Tree,Test)
        if Node then return Node,Window.Id end
    end
    return nil,nil
end
local function WithPanel(Layout,Id)
    return Locate(Layout,function(N) return N.Tabs and table.find(N.Tabs,Id) end)
end
local function WithId(Layout,Id)
    return Locate(Layout,function(N) return N.Id==Id end)
end
local function WindowRecord(Layout,Id)
    for Index,Window in Layout.Floats do if Window.Id==Id then return Window,Index end end
end
local function Tree(Layout,Id)
    if Id=="Main" then return Layout.Main end
    local Window=WindowRecord(Layout,Id)
    return Window and Window.Tree
end
local function SetTree(Layout,Id,Node)
    if Id=="Main" then Layout.Main=Node else WindowRecord(Layout,Id).Tree=Node end
end
local function Replace(Node,Id,New)
    if not Node or Node.Id==Id then return New end
    if Node.Axis then
        Node.First=Replace(Node.First,Id,New)
        Node.Second=Replace(Node.Second,Id,New)
        if not Node.First then return Node.Second end
        if not Node.Second then return Node.First end
    end
    return Node
end
local function Detach(Layout,Id)
    local Group,Window=WithPanel(Layout,Id)
    if not Group then return end
    local Slot=table.find(Group.Tabs,Id)
    table.remove(Group.Tabs,Slot)
    if #Group.Tabs==0 then
        SetTree(Layout,Window,Replace(Tree(Layout,Window),Group.Id,nil))
    elseif Group.Active==Id then Group.Active=Group.Tabs[math.min(Slot,#Group.Tabs)] end
    return Group.Id,Window,Slot
end
local function First(Node)
    while Node and Node.Axis do Node=Node.First end
    return Node
end
local function Clean(Layout)
    for I=#Layout.Floats,1,-1 do if not Layout.Floats[I].Tree then table.remove(Layout.Floats,I) end end
end
local function Forget(Layout,Id)
    for I,H in Layout.Hidden or {} do
        if H.Panel==Id then
            table.remove(Layout.Hidden,I)
            if #Layout.Hidden==0 then Layout.Hidden=nil end
            return H
        end
    end
end
local function CaptureEdge(Layout,Source,W)
    if not Source or #Source.Tabs~=1 then return nil end
    local P=Walk(Tree(Layout,W),function(N) return N.Axis and (N.First==Source or N.Second==Source) end)
    if not P then return nil end
    local A=if P.First==Source then P.Second else P.First
    return {Id=P.Id,Axis=P.Axis,Ratio=P.Ratio,First=P.First==Source,Anchor=A.Id,Panel=First(A).Active,Window=W}
end
local function ReadEdge(S)
    if S==nil then return nil end
    assert(Plain(S) and Identity(S.Id) and S.Id~="Main" and Identity(S.Anchor) and Identity(S.Panel) and Identity(S.Window))
    assert((S.Axis=="X" or S.Axis=="Y") and Finite(S.Ratio) and S.Ratio>0 and S.Ratio<1 and type(S.First)=="boolean")
    return {Id=S.Id,Axis=S.Axis,Ratio=S.Ratio,First=S.First,Anchor=S.Anchor,Panel=S.Panel,Window=S.Window}
end
local function ReadPlacement(P)
    if P==nil then return nil end
    assert(Plain(P))
    assert(Integer(P.X,-1000000,1000000) and Integer(P.Y,-1000000,1000000))
    assert(Integer(P.Width,1,65536) and Integer(P.Height,1,65536))
    return {X=P.X,Y=P.Y,Width=P.Width,Height=P.Height}
end

function DockModel.New(): Model
    local Registry: {[string]:{number}}={}
    local Count,Serial=0,0
    local Current=Freeze({Version=1,Floats={}})
    local Plans=setmetatable({},{__mode="k"})
    local Value:any={}
    local function Unique(Layout,Prefix)
        repeat Serial+=1 until not WithId(Layout,Prefix..Serial) and not WindowRecord(Layout,Prefix..Serial)
        return Prefix..Serial
    end
    local function Valid(Input)
        assert(Plain(Input) and Input.Version==1 and Array(Input.Floats,32))
        local Names,Panels={},{}
        local function Name(Id)
            assert(Identity(Id) and Id~="Main" and not Names[Id])
            Names[Id]=true
            return Id
        end
        local function Node(N,Depth)
            assert(Plain(N) and Depth<=32)
            local Id=Name(N.Id)
            if N.Axis~=nil then
                assert((N.Axis=="X" or N.Axis=="Y") and Finite(N.Ratio) and N.Ratio>0 and N.Ratio<1 and N.Tabs==nil)
                return {Id=Id,Axis=N.Axis,Ratio=N.Ratio,First=Node(N.First,Depth+1),Second=Node(N.Second,Depth+1)}
            end
            assert(Array(N.Tabs,256) and #N.Tabs>0 and N.First==nil and N.Second==nil)
            local Tabs={}
            for _,Panel in N.Tabs do
                assert(Identity(Panel) and Registry[Panel] and not Panels[Panel])
                Panels[Panel]=true; table.insert(Tabs,Panel)
            end
            assert(table.find(Tabs,N.Active))
            return {Id=Id,Tabs=Tabs,Active=N.Active}
        end
        local Result={Version=1,Floats={}}
        if Input.Main~=nil then Result.Main=Node(Input.Main,1) end
        for _,F in Input.Floats do
            assert(Plain(F) and Finite(F.Width) and Finite(F.Height))
            assert(F.Width>=1 and F.Width<=32768 and F.Height>=1 and F.Height<=32768)
            assert(F.OriginGroup==nil or Identity(F.OriginGroup))
            assert(F.OriginSlot==nil or Integer(F.OriginSlot,1,257))
            table.insert(Result.Floats,{Id=Name(F.Id),Tree=Node(F.Tree,1),Width=F.Width,Height=F.Height,
                OriginGroup=F.OriginGroup,OriginSlot=F.OriginSlot,OriginSplit=ReadEdge(F.OriginSplit),Placement=ReadPlacement(F.Placement)})
        end
        if Input.Hidden~=nil then
            assert(Array(Input.Hidden,256))
            if #Input.Hidden>0 then Result.Hidden={} end
            for _,H in Input.Hidden do
                assert(Plain(H) and Identity(H.Panel) and Registry[H.Panel] and not Panels[H.Panel])
                assert(Identity(H.Group) and H.Group~="Main" and Identity(H.Window) and Integer(H.Slot,1,256))
                Panels[H.Panel]=true
                table.insert(Result.Hidden,{Panel=H.Panel,Group=H.Group,Window=H.Window,Slot=H.Slot,Split=ReadEdge(H.Split)})
            end
        end
        return Freeze(Result)
    end
    local function Accept(Layout)
        local Ok,Next=pcall(Valid,Layout)
        if not Ok then return false,"InvalidLayout" end
        Current=Next
        return true
    end
    function Value:Register(Id,W,H)
        if not Identity(Id) or Registry[Id] or Count>=256 then return false end
        W,H=W or 64,H or 32
        if not Finite(W) or not Finite(H) or W<0 or H<0 or W>32768 or H>32768 then return false end
        Registry[Id]={W,H}; Count+=1
        return true
    end
    function Value:View() return Current end
    function Value:Snapshot() return Copy(Current) end
    function Value:Restore(Input) return Accept(Input) end
    function Value:Find(Id) return WithId(Current,Id) end
    function Value:GetLocation(Id)
        local G,W=WithPanel(Current,Id)
        if G then return G.Id,W,table.find(G.Tabs,Id) end
        return nil,nil,nil
    end
    function Value:IsVisible(Id)
        local G=WithPanel(Current,Id)
        return G~=nil and G.Active==Id
    end
    local function NewGroup(Layout,Id) return {Id=Unique(Layout,"Group"),Tabs={Id},Active=Id} end
    local function RestoreEdge(Layout,Incoming,Group,S)
        if not S then return false end
        local Anchor,W=WithId(Layout,S.Anchor)
        if not Anchor or W~=S.Window then Anchor,W=WithPanel(Layout,S.Panel) end
        if not Anchor or W~=S.Window then return false end
        if Incoming.Tabs and Group and Group~="Main" and not WithId(Layout,Group) and not WindowRecord(Layout,Group) then Incoming.Id=Group end
        local Id=if WithId(Layout,S.Id) or WindowRecord(Layout,S.Id) or Walk(Incoming,function(N) return N.Id==S.Id end)
            then Unique(Layout,"Split") else S.Id
        local Restored={Id=Id,Axis=S.Axis,Ratio=S.Ratio,First=if S.First then Incoming else Anchor,
            Second=if S.First then Anchor else Incoming}
        SetTree(Layout,W,Replace(Tree(Layout,W),Anchor.Id,Restored))
        return true
    end
    function Value:PrepareMove(Id,Window,NodeId,Side,Index)
        if not Registry[Id] or (Window~="Main" and not WindowRecord(Current,Window)) then return nil,"UnknownPanelOrWindow" end
        if Side~="Tab" and Side~="Left" and Side~="Right" and Side~="Top" and Side~="Bottom" then return nil,"InvalidSide" end
        local Layout=Copy(Current)
        Forget(Layout,Id)
        local Target,TargetWindow=WithId(Layout,NodeId)
        if NodeId and (not Target or TargetWindow~=Window) then return nil,"UnknownTarget" end
        if not Target then Target=First(Tree(Layout,Window)) end
        if Side=="Tab" and Target and Target.Axis then return nil,"ExpectedGroup" end
        if Index and (Side~="Tab" or not Integer(Index,1,(Target and #Target.Tabs or 0)+1)) then return nil,"InvalidIndex" end
        local Origin,Source=WithPanel(Layout,Id)
        local Group
        if Target==Origin and Origin then
            if Side=="Tab" then
                if Index then
                    local Old=table.find(Origin.Tabs,Id)
                    table.remove(Origin.Tabs,Old)
                    table.insert(Origin.Tabs,if Index>Old then Index-1 else Index,Id)
                end
                Origin.Active=Id; Group=Origin
            elseif #Origin.Tabs==1 then return nil,"OnlyTab" end
        end
        if not Group then
            local WasRoot=Target~=nil and Target==Tree(Layout,Window)
            Detach(Layout,Id)
            if WasRoot then Target=Tree(Layout,Window) end
            if Target and not WithId(Layout,Target.Id) then return nil,"RetiredTarget" end
            if not Target then
                Group=NewGroup(Layout,Id); SetTree(Layout,Window,Group)
            elseif Side=="Tab" then
                Group=Target; table.insert(Group.Tabs,Index or #Group.Tabs+1,Id); Group.Active=Id
            else
                Group=NewGroup(Layout,Id)
                local Before=Side=="Left" or Side=="Top"
                local Split={Id=Unique(Layout,"Split"),Axis=if Side=="Left" or Side=="Right" then "X" else "Y",
                    Ratio=0.5,First=if Before then Group else Target,Second=if Before then Target else Group}
                SetTree(Layout,Window,Replace(Tree(Layout,Window),Target.Id,Split))
            end
        end
        Clean(Layout)
        local Ok,Validated=pcall(Valid,Layout)
        if not Ok then return nil,"LayoutLimit" end
        local Plan=table.freeze({Layout=Validated,Group=Group.Id,Panel=Id})
        Plans[Plan]=Current
        return Plan
    end
    function Value:Commit(Plan)
        if not Plan or Plans[Plan]~=Current then return false,"StalePlan" end
        Current=Plan.Layout; Plans[Plan]=nil
        return true
    end
    function Value:Move(Id,W,N,S,I)
        local Plan,Why=self:PrepareMove(Id,W,N,S,I)
        if not Plan then return false,Why end
        return self:Commit(Plan)
    end
    function Value:Activate(Id)
        local G,W=WithPanel(Current,Id)
        if not G then return false,"Hidden" end
        if G.Active==Id then return true end
        return self:Move(Id,W,G.Id,"Tab")
    end
    function Value:Show(Id,GroupId)
        if WithPanel(Current,Id) then return self:Activate(Id) end
        if not GroupId and Current.Hidden then
            local Layout=Copy(Current)
            local H=Forget(Layout,Id)
            if H then
                local Group,W=WithId(Layout,H.Group)
                if Group and Group.Tabs then return self:Move(Id,W,Group.Id,"Tab",math.min(H.Slot,#Group.Tabs+1)) end
                if RestoreEdge(Layout,NewGroup(Layout,Id),H.Group,H.Split) then return Accept(Layout) end
                if H.Window=="Main" and not Layout.Main and not WithId(Layout,H.Group) and not WindowRecord(Layout,H.Group) then
                    Layout.Main={Id=H.Group,Tabs={Id},Active=Id}
                    return Accept(Layout)
                end
            end
        end
        local _,W=WithId(Current,GroupId)
        return self:Move(Id,W or "Main",GroupId,"Tab")
    end
    function Value:Hide(Id)
        if not Registry[Id] then return false,"UnknownPanel" end
        if not WithPanel(Current,Id) then return true end
        local Layout=Copy(Current)
        local Group,W=WithPanel(Layout,Id)
        local Edge=CaptureEdge(Layout,Group,W)
        local GroupId,Window,Slot=Detach(Layout,Id)
        Layout.Hidden=Layout.Hidden or {}
        table.insert(Layout.Hidden,{Panel=Id,Group=GroupId,Window=Window,Slot=Slot,Split=Edge})
        Clean(Layout)
        return Accept(Layout)
    end
    function Value:Unregister(Id)
        if not Registry[Id] then return false end
        local Layout=Copy(Current)
        Detach(Layout,Id); Forget(Layout,Id); Clean(Layout)
        if not Accept(Layout) then return false end
        Registry[Id]=nil; Count-=1
        -- A prepared insertion for a previously hidden panel is now invalid.
        Plans=setmetatable({},{__mode="k"})
        return true
    end
    function Value:Float(Id,Placement)
        if not Registry[Id] or #Current.Floats>=32 then return false,"UnknownPanelOrLimit" end
        local Layout=Copy(Current)
        Forget(Layout,Id)
        local Source,SourceWindow=WithPanel(Layout,Id)
        local OriginSplit=CaptureEdge(Layout,Source,SourceWindow)
        local G,_,Slot=Detach(Layout,Id)
        Clean(Layout)
        table.insert(Layout.Floats,{Id=Unique(Layout,"Window"),Tree=NewGroup(Layout,Id),Width=560,Height=400,
            OriginGroup=G,OriginSlot=Slot,OriginSplit=OriginSplit,Placement=Placement})
        return Accept(Layout)
    end
    function Value:Redock(Window)
        local Layout=Copy(Current)
        local F,At=WindowRecord(Layout,Window)
        if not F then return false,"UnknownWindow" end
        table.remove(Layout.Floats,At)
        local G=WithId(Layout,F.OriginGroup)
        if G and not G.Tabs then G=nil end
        if not G and RestoreEdge(Layout,F.Tree,F.OriginGroup,F.OriginSplit) then return Accept(Layout) end
        G=G or First(Layout.Main)
        if not G then Layout.Main=F.Tree; return Accept(Layout) end
        local Slot=if G.Id==F.OriginGroup then math.min(F.OriginSlot or #G.Tabs+1,#G.Tabs+1) else #G.Tabs+1
        local Active=First(F.Tree).Active
        Walk(F.Tree,function(N)
            if N.Tabs then for _,Id in N.Tabs do table.insert(G.Tabs,Slot,Id); Slot+=1 end end
            return false
        end)
        G.Active=Active
        return Accept(Layout)
    end
    function Value:SetRatio(Id,Ratio)
        local Layout=Copy(Current)
        local Node=WithId(Layout,Id)
        if not Node or not Node.Axis or not Finite(Ratio) or Ratio<=0 or Ratio>=1 then return false,"InvalidRatio" end
        Node.Ratio=Ratio
        return Accept(Layout)
    end
    function Value:SetWindowSize(Id,W,H,Placement)
        local Layout=Copy(Current)
        local F=WindowRecord(Layout,Id)
        if not F then return false,"UnknownWindow" end
        F.Width,F.Height,F.Placement=W,H,Placement
        return Accept(Layout)
    end
    local function Minimum(N)
        if N.Tabs then
            local W,H=1,1
            for _,Id in N.Tabs do local M=Registry[Id]; W=math.max(W,M[1]); H=math.max(H,M[2]) end
            return W,H+32
        end
        local A,B=Minimum(N.First); local C,D=Minimum(N.Second)
        return if N.Axis=="X" then A+C+6 else math.max(A,C),if N.Axis=="Y" then B+D+6 else math.max(B,D)
    end
    function Value:Limits(N,Extent)
        local A,B=Minimum(N.First); local C,D=Minimum(N.Second)
        local Low,High=if N.Axis=="X" then A else B,if N.Axis=="X" then C else D
        if Extent<=Low+High+6 then local R=Low/(Low+High); return R,R end
        return (Low+3)/Extent,1-(High+3)/Extent
    end
    function Value:Arrange(Root,W,H,Override,Ratio)
        local Out={}
        local function Visit(N,X,Y,Width,Height)
            Out[N.Id]={X=X,Y=Y,Width=Width,Height=Height}
            if not N.Axis then return end
            local Extent=if N.Axis=="X" then Width else Height
            local Low,High=self:Limits(N,Extent)
            local Half=math.min(3,Extent/2)
            local Effective=if N.Id==Override and Ratio then Ratio else N.Ratio
            local Pivot=math.clamp(Extent*math.clamp(Effective,Low,High),Half,Extent-Half)
            if N.Axis=="X" then
                Visit(N.First,X,Y,Pivot-Half,Height); Visit(N.Second,X+Pivot+Half,Y,Width-Pivot-Half,Height)
            else
                Visit(N.First,X,Y,Width,Pivot-Half); Visit(N.Second,X,Y+Pivot+Half,Width,Height-Pivot-Half)
            end
        end
        if Root then Visit(Root,0,0,math.max(0,W),math.max(0,H)) end
        return Out
    end
    return Value
end
return table.freeze(DockModel)
