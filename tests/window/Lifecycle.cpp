#include "LuiRuntime.h"

#include <cstdio>
#include <cstring>
#include <thread>

static int Check(bool Value, const char* Label) {
    if (!Value) std::fprintf(stderr, "[LUI:WindowTest] %s\n", Label);
    return Value ? 0 : 1;
}
static int Run(LuiRuntime* Runtime, const char* Source) {
    const auto Result = Lui_RunScript(Runtime, Source, "WindowLifecycle");
    if (!Result) std::fprintf(stderr, "[LUI:WindowTest] %s\n", Lui_GetLastError(Runtime));
    return Result ? 0 : 1;
}
struct Bridge {
    LuiRuntime* Runtime = nullptr;
    int Calls = 0;
    bool Accept = true;
    bool Saturate = false;
    bool DeferAck = false;
    static int LUI_CALL Create(void*, int, const char*) { return 1; }
    static int LUI_CALL Close(void* Context, int Id) {
        auto& Self = *static_cast<Bridge*>(Context);
        ++Self.Calls;
        if (Self.Saturate)
            for (int Index=0; Index<4096; ++Index) Lui_WindowResized(Self.Runtime,Id,320+Index,200);
        return Self.Accept ? (Self.DeferAck ? 1 : Lui_WindowClosed(Self.Runtime, Id)) : 0;
    }
};

int main() {
    int Failed = 0;
    auto* Runtime = Lui_Create();
    Failed += Run(Runtime, R"(
        assert(app:GetService('PlatformService'):Supports('WindowLifecycle'))
        W = Instance.new('Window', {Visible=true})
        Input = Instance.new('TextBox', {Text='retained', Parent=W})
        Other = Instance.new('Window', {Visible=true})
        Requested, Closed = 0, 0
        Block = true
        Handler = W.CloseRequested:Connect(function()
            Requested += 1
            local Ok, Reason = W:RequestClose()
            assert(not Ok and Reason == 'InProgress')
            if Block then assert(W:PreventClose()) end
        end)
        W.Closed:Connect(function()
            assert(W.IsClosed and not W.Visible and Input.Text=='retained')
            Closed += 1
        end)
        assert(not W.IsClosed and not W:PreventClose())
        local Ok, Reason = W:RequestClose()
        assert(not Ok and Reason=='Prevented' and Requested==1 and Closed==0 and not W.IsClosed)
        assert(not pcall(function() W.IsClosed=true end))
        assert(not pcall(function() W.RequestClose(Input) end))
        assert(not pcall(function() W.PreventClose(Input) end))
    )");
    Failed += Check(Lui_FocusChanged(Runtime, 2, 1) == 1, "focus setup");
    Failed += Check(Lui_WindowCloseRequested(Runtime, 1) == 1, "prevented native intent admission");
    Failed += Run(Runtime, R"(
        assert(Requested==2 and not W.IsClosed and Input.IsFocused)
        Block=false
        assert(W:RequestClose())
        assert(W.IsClosed and Closed==1 and not Input.IsFocused and Input.Parent==W)
        assert(#app:GetService('WindowService'):GetWindows()==1)
        assert(not W:RequestClose() and not W:PreventClose())
        assert(not pcall(function() W.Visible=true end))
        assert(not pcall(function() W.Size=UDim2.fromOffset(100,100) end))
        Input.Parent=Other
        assert(Input.Text=='retained' and Input.Parent==Other)
    )");
    Failed += Check(Lui_WindowClosed(Runtime, 1) == 0 && Lui_WindowCloseRequested(Runtime, 1) == 0,
        "duplicate terminal notifications accepted");
    Failed += Check(Lui_FocusChanged(Runtime, 2, 1) == 1, "retained child cannot refocus after redocking");
    Failed += Run(Runtime, R"(
        W:Destroy()
        assert(Input.Text=='retained' and Input.Parent==Other)
        Fail = Instance.new('Window', {Visible=true})
        ErrorHandler=Fail.CloseRequested:Connect(function() error('synthetic close failure') end)
        local Ok, Reason=Fail:RequestClose()
        assert(not Ok and Reason=='CallbackFailed' and not Fail.IsClosed)
        ErrorHandler:Disconnect()
        assert(Fail:RequestClose() and Fail.IsClosed)
        Disposable=Instance.new('Window')
        Disposable.CloseRequested:Connect(function() Disposable:Destroy() end)
        assert(not Disposable:RequestClose())
        Unexpected=Instance.new('Window',{Visible=true})
        Unexpected.Closed:Connect(function() UnexpectedClosed=(UnexpectedClosed or 0)+1 end)
    )");
    Failed += Check(Lui_WindowClosed(Runtime, 6) == 1 && Lui_WindowClosed(Runtime, 6) == 0,
        "unexpected native closure acknowledgement");
    Failed += Run(Runtime, "assert(Unexpected.IsClosed and UnexpectedClosed==1)");
    Failed += Run(Runtime, "NativeDestroyed=Instance.new('Window');NativeDestroyed.CloseRequested:Connect(function() NativeDestroyed:Destroy() end)");
    Failed += Check(Lui_WindowCloseRequested(Runtime,7)==1,"native close handler destruction was not admitted");
    Failed += Check(Lui_WindowClosed(Runtime, 2) == 0 && Lui_WindowCloseRequested(Runtime, 999999) == 0,
        "wrong class or stale identity admitted");
    Lui_Destroy(Runtime);

    Runtime = Lui_Create();
    Bridge State{Runtime};
    Lui_SetBackend(Runtime, {&State, Bridge::Create, nullptr, nullptr, nullptr, nullptr});
    Failed += Run(Runtime, "W=Instance.new('Window'); local Ok,Reason=W:RequestClose(); assert(not Ok and Reason=='Unsupported' and not W.IsClosed)");
    Lui_Destroy(Runtime);

    Runtime = Lui_Create(); State.Runtime = Runtime;
    Lui_SetBackend(Runtime, {&State, Bridge::Create, nullptr, nullptr, nullptr, nullptr});
    LuiWindowCallbacksV1 Callbacks{sizeof(Callbacks),1,&State,Bridge::Close,0};
    auto Bad=Callbacks; Bad.AbiVersion=2;
    Failed += Check(!Lui_SetWindowCallbacks(Runtime,&Bad), "bad version accepted");
    Bad=Callbacks; Bad.Reserved=1;
    Failed += Check(!Lui_SetWindowCallbacks(Runtime,&Bad), "reserved flags accepted");
    Bad=Callbacks; Bad.StructSize-=1;
    Failed += Check(!Lui_SetWindowCallbacks(Runtime,&Bad), "truncated callbacks accepted");
    int WorkerAccepted=1;
    std::thread Worker([&] { WorkerAccepted=Lui_SetWindowCallbacks(Runtime,&Callbacks); }); Worker.join();
    Failed += Check(!WorkerAccepted && Lui_SetWindowCallbacks(Runtime,&Callbacks)==1 &&
        !Lui_SetWindowCallbacks(Runtime,&Callbacks), "registration ownership/one-shot");
    State.Accept=false;
    Failed += Run(Runtime, "W=Instance.new('Window');local Ok,Reason=W:RequestClose();assert(not Ok and Reason=='BackendRejected' and not W.IsClosed)");
    State.Accept=true;
    Failed += Run(Runtime, "assert(W:RequestClose()); assert(not W:RequestClose())");
    Failed += Run(Runtime, "assert(W.IsClosed)");
    Failed += Check(State.Calls==2 && !Lui_SetWindowCallbacks(Runtime,&Callbacks), "backend acknowledgement/retry counts");
    State.Saturate=true;
    Failed += Run(Runtime, "S=Instance.new('Window');S.Closed:Connect(function() SaturatedClosed=(SaturatedClosed or 0)+1 end);assert(S:RequestClose())");
    Failed += Run(Runtime, "assert(S.IsClosed and SaturatedClosed==1 and app:GetService('PerformanceService'):GetStats().QueuedEvents==0)");
    Failed += Check(State.Calls==3,"saturated acknowledgement did not use one native close");
    State.Saturate=false; State.DeferAck=true;
    Failed += Run(Runtime,R"(
        P=Instance.new('Window')
        Retained=Instance.new('Frame',{Parent=P})
        Candidate=Instance.new('Frame')
        LateClosed=0;P.Closed:Connect(function() LateClosed+=1 end)
        assert(P:RequestClose() and not P.IsClosed)
        local Ok,Reason=P:RequestClose();assert(not Ok and Reason=='InProgress')
        assert(not pcall(function() P.Visible=true end))
        assert(not pcall(function() Candidate.Parent=P end))
        assert(Candidate.Parent==nil and Retained.Parent==P and not P.IsClosed)
    )");
    Failed += Check(State.Calls==4 && Lui_WindowClosed(Runtime,3)==1 && Lui_WindowClosed(Runtime,3)==0,
        "delayed acknowledgement/duplicate native close");
    Failed += Run(Runtime,"assert(P.IsClosed and LateClosed==1 and Retained.Parent==P)");
    Lui_Destroy(Runtime);
    if (!Failed) std::puts("[LUI:WindowTest] Lifecycle, prevention, terminal ownership and bridge bounds passed");
    return Failed ? 1 : 0;
}
