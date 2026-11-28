-- Prepare.py supplies Case, Count, WithFrames and GateFrames.
local Window = Instance.new("Window", {Visible = true, Title = "LUI Image Qualification", Size = UDim2.fromOffset(960, 720)})
local Root = Instance.new("Frame", {Parent = Window, Size = UDim2.fromScale(1, 1)})
local Platform = app:GetService("PlatformService")
if Case == "Atlas" and not Platform:Supports("ImageRegion") then
    print("[LUI:ImageBench] Skipped Atlas ImageRegion unavailable")
    Window:Destroy()
    return
end
local Generated = {}
if Case == "RuntimeThumbnails" or Case == "Combined" then
    assert(Platform:Supports("RuntimeImages"))
    local Assets = app:GetService("AssetService")
    local Pixels = buffer.create(24 * 24 * 4)
    buffer.fill(Pixels, 0, 255)
    for Index = 1, 400 do
        buffer.writeu8(Pixels, 2, Index % 256)
        Generated[Index] = assert(Assets:FromPixels(Pixels, 24, 24, 96, "BGRA8Premultiplied"))
    end
end
local Frames = if WithFrames and Platform:Supports("FrameSurface") then Instance.new("FrameSurface", {
    Parent = Root, Position = UDim2.fromOffset(350, 240), Size = UDim2.fromOffset(608, 456),
}) else nil
local FrameCount, FrameRejected = 0, 0
local WorkerActor, WorkerResult, WorkerFailure, WorkerStarted
local WorkerTimings = {}
local WorkbenchTimes, WorkerCompletionTimes = {}, {}
local Inspector, Editor, Search
local Running = true
local FramePixels = if Frames then buffer.create(FrameWidth * FrameHeight * 4) else nil
local FrameMarker = if FramePixels and MeasureFramePacing then buffer.create(64 * 4) else nil
local FrameStarted, PreviousFrame = os.clock(), nil
local FrameIntervals = {}
local FrameClock = if MeasureFramePacing then app:GetService("PerformanceService") else nil
local FrameAdmissionTimes, FrameAdmissionCosts = {}, {}
local NextFrame = FrameStarted
local MissedSlots = 0
if (GateFrames or FrameMarker) and FramePixels then buffer.fill(FramePixels, 0, 255) end
local function Produce()
    if not Running or not Frames or not FramePixels then return end
    if GateFrames and Frames.FrameState == "Pending" then
        task.delay(1 / 120, Produce)
        return
    end
    local Now = os.clock()
    if PreviousFrame then table.insert(FrameIntervals, (Now - PreviousFrame) * 1000) end
    PreviousFrame = Now
    if FrameMarker then
        local Color = 0xff000000 + ((FrameCount + FrameRejected) % 256) * 0x10000
        if ObserveDesktopMarker then
            local Sequence = FrameCount + 1
            Color = 0xff000052 + (Sequence % 256) * 0x10000 + math.floor(Sequence / 256) * 0x100
        end
        for Offset = 0, buffer.len(FrameMarker) - 4, 4 do
            buffer.writeu32(FrameMarker, Offset, Color)
        end
        for Row = 0, 63 do
            buffer.copy(FramePixels, Row * FrameWidth * 4, FrameMarker)
        end
    else
        buffer.writeu8(FramePixels, 0, FrameCount % 256)
    end
    local SubmittedAt = if FrameClock then FrameClock:GetTime() else 0
    local Accepted = Frames:SubmitFrame(FramePixels, FrameWidth, FrameHeight, FrameWidth * 4, "BGRA8Premultiplied")
    if Accepted then
        FrameCount += 1
        if FrameClock and FrameCount <= 4096 then
            FrameAdmissionTimes[FrameCount] = SubmittedAt
            FrameAdmissionCosts[FrameCount] = FrameClock:GetTime() - SubmittedAt
        end
    else
        FrameRejected += 1
    end
    if MeasureFramePacing then
        local Interval = 1 / FrameHz
        NextFrame += Interval
        local Finished = os.clock()
        if NextFrame <= Finished then
            local Skipped = math.floor((Finished - NextFrame) / Interval) + 1
            MissedSlots += Skipped
            NextFrame += Skipped * Interval
        end
        task.delay(NextFrame - Finished, Produce)
    else
        task.delay(1 / FrameHz, Produce)
    end
end
if Frames then task.defer(Produce) end

if Case == "Combined" then
    local Toolbar = Instance.new("Frame", {Parent = Root, Position = UDim2.fromOffset(0, 80), Size = UDim2.fromOffset(960, 42)})
    for Slot = 1, 6 do
        Instance.new("ImageLabel", {
            Parent = Toolbar, Source = "Atlas.png", Size = UDim2.fromOffset(24, 24),
            Position = UDim2.fromOffset(Slot * 32, 8), SourceRectOffset = Vector2.new(Slot * 24, 0),
            SourceRectSize = Vector2.new(24, 24),
        })
    end
    Inspector = Instance.new("TextLabel", {Parent = Root, Position = UDim2.fromOffset(0, 130), Size = UDim2.fromOffset(310, 100)})
    Search = Instance.new("TextBox", {Parent = Root, Position = UDim2.fromOffset(0, 240), Size = UDim2.fromOffset(310, 42)})
    Editor = Instance.new("CodeEditor", {Parent = Root, Position = UDim2.fromOffset(0, 296), Size = UDim2.fromOffset(320, 350)})
    WorkerStarted = os.clock()
    Workers.Start("./Compute", {}, function(Handle, Failure)
        WorkerActor, WorkerFailure = Handle, Failure
        WorkerStarted = (os.clock() - WorkerStarted) * 1000
    end)
end

local function Bind(Image, Index, Thumbnail)
    if Case == "RuntimeThumbnails" or (Case == "Combined" and Thumbnail) then
        if Image.ImageResource ~= Generated[Index] then Image.ImageResource = Generated[Index] end
        return
    end
    local Asset = if Thumbnail then "Thumb" .. Index .. ".png" else "Icon" .. Index .. ".png"
    if Case == "ClippedAtlas" then
        Image.Position = UDim2.fromOffset(-((Index - 1) % 8) * 24, -math.floor((Index - 1) / 8) * 24)
        Asset = "Atlas.png"
    elseif Case == "Atlas" then
        Image.SourceRectOffset = Vector2.new(((Index - 1) % 8) * 24, math.floor((Index - 1) / 8) * 24)
        Image.SourceRectSize = Vector2.new(24, 24)
        Asset = "Atlas.png"
    end
    if Image.Source ~= Asset or Image.LoadState == "Failed" then Image.Source = Asset end
end

local function Add(Parent, Slot)
    local X, Y = ((Slot - 1) % 24) * 32, math.floor((Slot - 1) / 24) * 32
    local Position, Size = UDim2.fromOffset(X, Y), UDim2.fromOffset(24, 24)
    if Case == "ClippedAtlas" then
        Parent = Instance.new("Frame", {Parent = Parent, ClipsDescendants = true,
            Position = Position, Size = Size, BackgroundTransparency = 1})
        Position, Size = UDim2.fromOffset(0, 0), UDim2.fromOffset(192, 192)
    end
    return Instance.new("ImageLabel", {Parent = Parent, Position = Position, Size = Size, BackgroundTransparency = 1})
end

local function Group(Parent, Indices, Thumbnail, Finished, First, Rows, Selection)
    local Started = os.clock()
    local Images = {}
    for Slot, Index in Indices do
        local Image = Rows[Slot] or Add(Parent, (First or 0) + Slot)
        Images[Slot] = Image
        Bind(Image, Index, Thumbnail)
        if Selection then Image.Opacity = if Slot == Selection then 0.7 else 1 end
    end
    local Submitted = (os.clock() - Started) * 1000
    local function Check()
        local Ready, Failed = 0, 0
        for _, Image in Images do
            if Image.LoadState == "Loading" then
                assert(os.clock() - Started < 8, "Image settlement deadline")
                task.delay(0.001, Check)
                return
            end
            if Image.LoadState == "Ready" then Ready += 1 else Failed += 1 end
        end
        Finished(Ready, Failed, (os.clock() - Started) * 1000, Submitted, Images)
    end
    task.defer(Check)
end

local function Panel()
    return Instance.new("Frame", {Parent = Root, Size = UDim2.fromScale(1, 1), BackgroundTransparency = 1})
end

local function Series(Name, Steps, Hold, Build, Done)
    local Step, Ready, Failed = 0, 0, 0
    local Latencies, Submissions = {}, {}
    local View = Panel()
    local Virtualized = Case == "Explorer" or Case == "Atlas" or Case == "ClippedAtlas"
        or Case == "RuntimeThumbnails" or Case == "Combined"
    local Rows = {}
    local function Advance()
        if Step == Steps then
            View:Destroy()
            table.sort(Latencies)
            table.sort(Submissions)
            print(string.format("[LUI:ImageBench] %s/%s groups=%d ready=%d failed=%d settleP95Ms=%.3f submitP95Ms=%.3f",
                Case, Name, Steps, Ready, Failed, Latencies[math.ceil(Steps * .95)], Submissions[math.ceil(Steps * .95)]))
            Done()
            return
        end
        local Reconstruct = if Hold then Step % (Hold / 16) == 0 else not Virtualized or Step % 30 == 0
        if Step > 0 and Reconstruct then
            View:Destroy()
            View = Panel()
            Rows = {}
        end
        Step += 1
        local StepStarted = if MeasureMatchedCpu and Case == "Combined" then os.clock() else nil
        local Indices, Thumbnail = Build(Step)
        Group(View, Indices, Thumbnail, function(Good, Bad, Settled, Submitted, Images)
            Ready += Good
            Failed += Bad
            Latencies[Step], Submissions[Step] = Settled, Submitted
            if Virtualized then Rows = Images end
            if StepStarted then
                table.insert(WorkbenchTimes, FrameClock:GetTime())
                task.delay(math.max(0, StepStarted + 1 / 20 - os.clock()), Advance)
            else
                task.defer(Advance)
            end
        end, if Hold then ((Step - 1) * 16) % Hold else 0, Rows, if Virtualized then Step % 20 + 1 else nil)
    end
    task.defer(Advance)
end

local function Finish()
    Running = false
    if WorkerActor then WorkerActor:Close(); WorkerActor = nil end
    table.sort(FrameIntervals)
    local FrameP95 = FrameIntervals[math.ceil(#FrameIntervals * .95)] or 0
    print(string.format("[LUI:ImageBench] Done %s frameAdmitted=%d frameRejected=%d frameTurnP95Ms=%.3f elapsedMs=%.3f",
        Case, FrameCount, FrameRejected, FrameP95, (os.clock() - FrameStarted) * 1000))
    if MeasureFramePacing then
        local function ReportTimes(Name, Timestamps)
            for First = 1, #Timestamps, 48 do
                local Values = {}
                for Index = First, math.min(First + 47, #Timestamps) do
                    Values[#Values + 1] = string.format("%.6f", Timestamps[Index])
                end
                print(string.format("[LUI:ImageBench] %s first=%d ms=%s", Name, First, table.concat(Values, ",")))
            end
        end
        local function Gap(Quantile)
            return FrameIntervals[math.ceil(#FrameIntervals * Quantile)] or 0
        end
        print(string.format("[LUI:ImageBench] Producer width=%d height=%d requestedHz=%d missedSlots=%d gapP50Ms=%.3f gapP95Ms=%.3f gapP99Ms=%.3f gapMaxMs=%.3f",
            FrameWidth, FrameHeight, FrameHz, MissedSlots, Gap(.5), Gap(.95), Gap(.99), Gap(1)))
        if #FrameAdmissionCosts > 0 then
            table.sort(FrameAdmissionCosts)
            local function Cost(Fraction)
                return FrameAdmissionCosts[math.ceil(#FrameAdmissionCosts * Fraction)]
            end
            print(string.format("[LUI:ImageBench] AdmissionCosts n=%d p50Ms=%.3f p95Ms=%.3f p99Ms=%.3f",
                #FrameAdmissionCosts, Cost(.5), Cost(.95), Cost(.99)))
        end
        ReportTimes("AdmissionTimes", FrameAdmissionTimes)
        if MeasureMatchedCpu and Case == "Combined" then
            ReportTimes("WorkbenchTimes", WorkbenchTimes)
            ReportTimes("WorkerCompletionTimes", WorkerCompletionTimes)
        end
    end
    Window:Destroy()
end

local function Explorer(Step)
    local Document = math.floor((Step - 1) / 30) % 3
    local Offset = (Step * 41 + Document * 339) % 9980
    if Step % 22 >= 15 then Offset = 9980 - Offset end
    local Filter = if Step % 30 >= 25 then 13 elseif Step % 19 >= 16 then 4 else 1
    local Indices = {}
    for Slot = 1, 20 do
        local Row = (Offset + (Slot - 1) * Filter) % 10000
        Indices[Slot] = if Row % 5 == 0 then 25 + ((Row * 13 + Document) % 40) else 1 + ((Row * 7 + Document) % 24)
    end
    return Indices, false
end

if Case == "Combined" then
    local Busy = false
    local function BuildWorkbenchStep(Step)
        local Document = math.floor((Step - 1) / 30) + 1
        local First = (Step * 131) % 10000
        if Step % 30 > 20 then First = 9999 - First end
        local Thumbnail = Step % 4 >= 2
        local Indices = {}
        local Filter = if Step % 19 > 10 then 13 else 1
        for Slot = 1, 20 do
            local Row = (First + Slot * Filter) % 10000
            Indices[Slot] = Row % (if Thumbnail then #Generated else Count) + 1
        end
        Inspector.Text = string.format("Document %d, selection %d", Document, First)
        if Step % 4 == 0 then Search.Text = "Asset " .. First end
        if Step % 8 == 0 then
            Editor.Text = string.rep("-- synthetic document " .. Document .. "\nlocal Row = " .. First .. "\n", 80)
        end
        if WorkerActor and not Busy and Step % 3 == 0 then
            Busy = true
            local Sent = os.clock()
            WorkerActor:Send(30000, function(Value, Failure)
                WorkerFailure = Failure
                WorkerResult = Value
                table.insert(WorkerTimings, (os.clock() - Sent)*1000)
                if MeasureMatchedCpu then table.insert(WorkerCompletionTimes, FrameClock:GetTime()) end
                Busy = false
            end)
        end
        return Indices, Thumbnail
    end
    local function FinishWorkbenchPass()
        if MeasureFramePacing and os.clock() - FrameStarted < FrameSeconds then
            Series("mixed-workbench", 120, nil, BuildWorkbenchStep, FinishWorkbenchPass)
            return
        end
        local function Complete()
            if Busy then task.delay(.001, Complete); return end
            local WorkerPassed = WorkerFailure == nil and WorkerActor ~= nil and #WorkerTimings > 0
                and WorkerResult == (30000 * 30001 // 2 * 17) % 65521
            table.sort(WorkerTimings)
            print(string.format("[LUI:ImageBench] Workers passed=%s failure=%s completed=%d coldMs=%.3f turnP95Ms=%.3f maxOutstanding=1",
                tostring(WorkerPassed), WorkerFailure or "none", #WorkerTimings, WorkerStarted or 0,
                WorkerTimings[math.ceil(#WorkerTimings*.95)] or 0))
            for _, Image in Generated do Image:Close() end
            Finish()
            assert(WorkerPassed, "Worker qualification failed")
        end
        Complete()
    end
    Series("mixed-workbench", 120, nil, BuildWorkbenchStep, FinishWorkbenchPass)
elseif Case == "RuntimeSources" then
    local Assets = app:GetService("AssetService")
    assert(Platform:Supports("RuntimeImages"))
    local PngInput = buffer.fromstring(PngBytes)
    local Png = assert(Assets:FromEncoded(PngInput))
    local Jpeg = assert(Assets:FromEncoded(buffer.fromstring(JpegBytes)))
    local Incomplete = assert(Assets:FromEncoded(buffer.fromstring(string.sub(PngBytes, 1, 24))))
    assert(Png.Width == 4 and Png.Height == 4 and Jpeg.Width == 2 and Jpeg.Height == 2)
    buffer.fill(PngInput, 0, 0)
    local First, Second, Third, Invalid = Add(Root, 1), Add(Root, 2), Add(Root, 3), Add(Root, 4)
    First.ImageResource, Second.ImageResource, Third.ImageResource = Png, Png, Jpeg
    Invalid.ImageResource = Incomplete
    if Platform:Supports("ImageRegion") then
        Second.SourceRectOffset = Vector2.new(2, 2)
        Second.SourceRectSize = Vector2.new(2, 2)
    end
    Png:Close()
    Jpeg:Close()
    Incomplete:Close()
    local function Check()
        for _, Image in {First, Second, Third, Invalid} do
            if Image.LoadState == "Loading" then
                assert(os.clock() - FrameStarted < 8, "Runtime sources settlement deadline")
                task.delay(.001, Check)
                return
            end
        end
        assert(First.LoadState == "Ready" and Second.LoadState == "Ready" and Third.LoadState == "Ready")
        assert(Invalid.LoadState == "Failed")
        print("[LUI:ImageBench] PNG and JPEG ready, shared PNG, caller mutation isolated, truncated PNG rejected")
        Finish()
    end
    task.defer(Check)
elseif Case == "RuntimeThumbnails" then
    Series("virtualized-runtime-sources", 80, nil, function(Step)
        local Indices = {}
        local Offset = (if Step <= 40 then Step else 81 - Step) * 17
        for Slot = 1, 20 do Indices[Slot] = (Offset + Slot - 1) % #Generated + 1 end
        return Indices, true
    end, function()
        for _, Image in Generated do Image:Close() end
        Finish()
    end)
elseif Case == "Lifecycle" then
    local Valid, Malformed, Excessive, Superseded = Add(Root, 1), Add(Root, 2), Add(Root, 3), Add(Root, 4)
    Valid.Source, Malformed.Source, Excessive.Source = "Icon1.png", "Malformed.png", "Excessive.png"
    Superseded.Source = "Icon2.png"
    local _ = Superseded.AbsoluteSize
    Superseded.Source = "Icon3.png"
    local function Check()
        if Valid.LoadState == "Loading" or Malformed.LoadState == "Loading" or
            Excessive.LoadState == "Loading" or Superseded.LoadState == "Loading" then
            assert(os.clock() - FrameStarted < 8, "Lifecycle settlement deadline")
            task.delay(.001, Check)
            return
        end
        assert(Valid.LoadState == "Ready" and Superseded.LoadState == "Ready" and Superseded.Source == "Icon3.png")
        assert(Malformed.LoadState == "Failed" and Excessive.LoadState == "Failed")
        if Platform:Supports("ImageRegion") then
            Valid.SourceRectOffset, Valid.SourceRectSize = Vector2.new(4, 4), Vector2.new(8, 8)
            assert(Valid.LoadState == "Ready")
            Valid.SourceRectOffset = Vector2.new(4096, 4096)
            assert(Valid.LoadState == "Ready")
            Valid.SourceRectSize = Vector2.new(0, 0)
        end
        local Late = Add(Root, 5)
        Late.Source = "Icon4.png"
        local _ = Late.AbsoluteSize
        print("[LUI:ImageBench] Lifecycle valid, malformed, oversized, superseded and pending shutdown")
        Finish()
    end
    task.defer(Check)
elseif Case == "FrameOnly" then
    task.delay(FrameSeconds, Finish)
elseif Case == "Static" then
    local function Icons(Step)
        local Indices = {}
        for Slot = 1, 16 do Indices[Slot] = (Step - 1) * 16 + Slot end
        return Indices, false
    end
    local function Pass(Name, Done)
        Series(Name, Count / 16, 64, Icons, Done)
    end
    Pass("cold", function() Pass("warm", Finish) end)
elseif Case == "Thumbnails" then
    local function Thumbnails(Step)
        local Indices = {}
        local First = if Step <= 24 then Step else 49 - Step
        for Slot = 1, 16 do Indices[Slot] = ((First - 1) * 11 + Slot - 1) % 189 + 1 end
        return Indices, true
    end
    Series("icons-before", 30, nil, Explorer, function()
        Series("thumbnail-navigation", 48, nil, Thumbnails, function()
            Series("icons-after", 30, nil, Explorer, Finish)
        end)
    end)
else
    Series("scroll-reverse-expand-filter-switch", 120, nil, Explorer, Finish)
end
