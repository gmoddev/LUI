#include "LuiRuntime.h"
#include <cstdio>
#include <string>
#include <thread>

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr,"[LUI:TextTest] %s\n",Message); return Condition?0:1;
}
int main() {
    LuiDiagnosticPolicyV1 Policy{sizeof(Policy),1,LUI_DIAGNOSTICS_DEVELOPMENT,0};
    auto* Runtime=Lui_CreateWithPolicy(&Policy);
    int Failures=Check(Lui_RunScript(Runtime,R"(
        Input=Instance.new('TextBox',{Text='A\u{1F642}B',Multiline=true,MaxLength=8,SelectionStart=1,SelectionLength=1})
        assert(Input.SelectionStart==1 and Input.SelectionLength==1)
        assert(not pcall(function() Input.Text=string.char(0xC0,0xAF) end))
        assert(not pcall(function() Input.Text='nul'..string.char(0) end))
        assert(not pcall(function() Input.MaxLength=2 end))
        assert(not pcall(function() Input.Secret=true end))
        TextCalls,SelectionCalls,Starts,Changes,Ends=0,0,0,0,0
        Input.TextChanged:Connect(function() TextCalls+=1; assert(Input.SelectionStart==2) end)
        Input.SelectionChanged:Connect(function() SelectionCalls+=1 end)
        Input.CompositionStarted:Connect(function() Starts+=1; assert(Input.IsComposing) end)
        Input.CompositionChanged:Connect(function() Changes+=1; assert(Input.IsComposing and Input.CompositionText~='') end)
        Input.CompositionEnded:Connect(function() Ends+=1; assert(not Input.IsComposing and Input.CompositionText=='') end)
        Password=Instance.new('TextBox',{Secret=true,Text='SECRET_TEXT_CANARY'})
        assert(Password.EffectiveSensitive)
        assert(not pcall(function() Password.SelectionLength=1 end))
        Copy=Password:Clone(); assert(Copy.Secret and Copy.Text==Password.Text and Copy.EffectiveSensitive)
    )","TextContract"),Lui_GetLastError(Runtime));
    const char* Commit="A\xF0\x9F\x99\x82\xE6\x96\x87";
    uint64_t Revision=0;
    Failures+=Check(Lui_GetTextInputRevision(Runtime,1,&Revision) && Revision!=0,"owner input revision query failed");
    uint64_t InvalidRevision=99;
    Failures+=Check(!Lui_GetTextInputRevision(Runtime,999,&InvalidRevision) && InvalidRevision==0 &&
        !Lui_GetTextInputRevision(Runtime,1,nullptr),"invalid input revision query accepted");
    std::thread Foreign([&]{InvalidRevision=99; if(Lui_GetTextInputRevision(Runtime,1,&InvalidRevision) || InvalidRevision!=0) ++Failures;}); Foreign.join();
    Failures+=Check(Lui_RunScript(Runtime,"Input.PlaceholderText='Synthetic placeholder'","RevisionRetirement"),"script revision fixture failed");
    uint64_t CurrentRevision=0;
    Failures+=Check(Lui_GetTextInputRevision(Runtime,1,&CurrentRevision) && CurrentRevision>Revision,"script configuration did not retire queued text revision");
    Failures+=Check(!Lui_TextComposition(Runtime,1,1,"x"),"preedit change without start accepted");
    Failures+=Check(Lui_TextComposition(Runtime,1,0,"") && Lui_TextComposition(Runtime,1,1,"\xE6\x96\x87") &&
        Lui_TextInput(Runtime,1,Commit,2,1) && Lui_TextComposition(Runtime,1,2,""),"composition/commit failed");
    Failures+=Check(Lui_RunScript(Runtime,"assert(TextCalls==1 and SelectionCalls==1 and Starts==1 and Changes==1 and Ends==1)","AtomicText"),Lui_GetLastError(Runtime));
    Failures+=Check(!Lui_TextInput(Runtime,1,Commit,4,0) && !Lui_TextInput(Runtime,1,"\xED\xA0\x80",0,0) &&
        !Lui_TextInput(Runtime,2,"valid",1,0),"invalid UTF-8/selection or secret selection accepted");
    Failures+=Check(!Lui_TextComposition(Runtime,2,0,"SECRET_PREEDIT_CANARY"),"secret preedit was exposed");
    const char* Snapshot=Lui_GetPreviewTreeJson(Runtime);
    Failures+=Check(Snapshot && std::string(Snapshot).find("CANARY")==std::string::npos,"secret text leaked into snapshot");
    Failures+=Check(Lui_RunScript(Runtime,"Password.Secret=false; assert(Password.EffectiveSensitive); Password.Enabled=false; assert(not Password.IsComposing)","SecretRetirement"),Lui_GetLastError(Runtime));
    Failures+=Check(!Lui_TextComposition(Runtime,2,1,"LATE_PREEDIT_CANARY"),"disabled composition accepted");
    Failures+=Check(Lui_FocusChanged(Runtime,1,1) && Lui_TextComposition(Runtime,1,0,"preedit") &&
        Lui_FocusChanged(Runtime,1,0),"focus/composition retirement failed");
    Failures+=Check(Lui_RunScript(Runtime,"assert(not Input.IsComposing and Ends==2); Input:Destroy(); Input:Destroy()","TextDestroy"),Lui_GetLastError(Runtime));
    Failures+=Check(!Lui_TextInput(Runtime,1,"late",0,0),"destroyed text input accepted");
    Failures+=Check(Lui_RunScript(Runtime,"Held=Instance.new('Frame',{Name='Retired metadata'}); Held:Destroy()","RetiredMetadata"),Lui_GetLastError(Runtime));
    Failures+=Check(Lui_RunScript(Runtime,"assert(Held.Name=='Retired metadata' and Held.ClassName=='Frame'); Held:Destroy(); Held=nil; local Stats=app:GetService('PerformanceService'):GetStats(); assert(Stats.LiveInstances==2)","RetiredUserdata"),Lui_GetLastError(Runtime));
    Lui_Destroy(Runtime);
    if(!Failures) std::puts("[LUI:TextTest] Unicode scalars, atomic selection, bounded composition and sticky secret policy passed");
    return Failures?1:0;
}
