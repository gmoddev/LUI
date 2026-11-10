#include "LuiRuntime.h"
#include <cstdio>
#include <string>
#include <thread>

struct DeferredEditorBackend {
    LuiRuntime* Runtime=nullptr;
    std::string Payload;
    int Queued=0,Accepted=0;
};
static int LUI_CALL DeferredCreate(void* Context,int Id,const char* ClassName) {
    auto* Backend=static_cast<DeferredEditorBackend*>(Context);
    if(std::string(ClassName)=="CodeEditor") {
        ++Backend->Queued;
        Backend->Accepted+=Lui_TextInput(Backend->Runtime,Id,Backend->Payload.c_str(),
            static_cast<int>(Backend->Payload.size()),0);
    }
    return 1;
}
static int LUI_CALL DeferredProperty(void*,int,const char*,const char*) {return 1;}
static int LUI_CALL DeferredParent(void*,int,int) {return 1;}
static int LUI_CALL DeferredArrange(void*,int,double,double,double,double,int) {return 1;}
static int LUI_CALL DeferredPresent(void*,int,int,double,double,double,double,int,int) {return 1;}
static int LUI_CALL DeferredDestroy(void*,int) {return 1;}

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
    Runtime=Lui_CreateWithPolicy(&Policy);
    Failures+=Check(Lui_RunScript(Runtime,R"(
        Editor=Instance.new('CodeEditor',{Text='A\u{1F600}\nreturn',SelectionStart=2})
        assert(Editor:IsA('TextBox') and Editor.Multiline and not Editor.Secret)
        assert(Editor.TextRevision==2 and Editor.SelectionStart==2)
        assert(not Editor:SetStyles(Editor.TextRevision,{{Start=1,End=3,Color=0x1248AB}}))
        assert(not Editor:SetStyles(Editor.TextRevision,{}))
        assert(not Editor:SetStyles(Editor.TextRevision-1,{}))
        assert(not pcall(function() Editor:SetStyles(Editor.TextRevision,{{Start=0,End=20,Color=1}}) end))
        assert(not pcall(function() Editor:SetStyles(Editor.TextRevision,{{Start=2,End=3,Color=1},{Start=1,End=2,Color=2}}) end))
        assert(not pcall(function() Editor:SetStyles(Editor.TextRevision,{{Start=0,End=1,Color=-1}}) end))
        local SideEffects=0
        local Hidden=setmetatable({},{__index=function() SideEffects+=1; Editor.Text='unexpected'; return 0 end})
        assert(not pcall(function() Editor:SetStyles(Editor.TextRevision,{Hidden}) end))
        assert(SideEffects==0 and Editor.Text=='A\u{1F600}\nreturn' and Editor.TextRevision==2)
        assert(not pcall(function() Editor.Multiline=false end))
        assert(not pcall(function() Editor.Secret=true end))
        assert(not pcall(function() Editor.Text='CR\rLF' end))
        assert(Editor.TextRevision==2)
        Editor.Text='B\u{1F600}\nreturn'
        assert(Editor.TextRevision==3 and not Editor:SetStyles(2,{}))
        Copy=Editor:Clone()
        assert(Copy.ClassName=='CodeEditor' and Copy.Multiline and Copy.Text==Editor.Text)
        assert(not Copy:SetStyles(Copy.TextRevision,{}))
    )","CodeEditorContract"),Lui_GetLastError(Runtime));
    uint64_t CodeRevision=0;
    Failures+=Check(Lui_GetCodeEditorTextRevision(Runtime,1,&CodeRevision) && CodeRevision==3,
        "CodeEditor revision query failed");
    Failures+=Check(!Lui_TextInput(Runtime,1,"CR\rLF",0,0),"CodeEditor accepted CR native input");
    Failures+=Check(!Lui_TextChanged(Runtime,1,"CR\rLF"),"CodeEditor accepted CR legacy input");
    Failures+=Check(Lui_TextInput(Runtime,1,"B\xF0\x9F\x98\x80\nreturn!",3,0),"CodeEditor rejected native edit");
    Failures+=Check(Lui_GetCodeEditorTextRevision(Runtime,1,&CodeRevision) && CodeRevision==4,
        "CodeEditor revision did not advance");
    Failures+=Check(Lui_RunScript(Runtime,R"(
        assert(Editor.Text=='B\u{1F600}\nreturn!' and Editor.TextRevision==4)
        assert(not Editor:SetStyles(3,{}))
        assert(not Editor:SetStyles(4,{{Start=2,End=3,Color=0xFF00FF}}))
        Editor.MaxLength=20
        assert(Editor.MaxLength==20)
    )","CodeEditorLimitBeforeUndo"),Lui_GetLastError(Runtime));
    Failures+=Check(Lui_TextInput(Runtime,1,"01234567890123456789",20,0) &&
        Lui_TextInput(Runtime,1,"0123456789",10,0),"CodeEditor native edit history rejected");
    Failures+=Check(Lui_RunScript(Runtime,R"(
        assert(not pcall(function() Editor.MaxLength=10 end))
        assert(Editor.MaxLength==20 and Editor.Text=='0123456789')
    )","CodeEditorLimitRetainsUndoAdmission"),Lui_GetLastError(Runtime));
    Failures+=Check(Lui_TextInput(Runtime,1,"01234567890123456789",20,0),
        "CodeEditor native Undo restore rejected");
    Failures+=Check(!Lui_TextInput(Runtime,1,"012345678901234567890",21,0),
        "CodeEditor over-limit native input accepted");
    Failures+=Check(Lui_RunScript(Runtime,R"(
        assert(Editor.Text=='01234567890123456789' and Editor.MaxLength==20)
        Editor.MaxLength=20
        assert(Editor.MaxLength==20)
        Editor:Destroy()
        assert(not pcall(function() Editor:SetStyles(4,{}) end))
    )","CodeEditorRetirement"),Lui_GetLastError(Runtime));
    Lui_Destroy(Runtime);
    Runtime=Lui_CreateWithPolicy(&Policy);
    Failures+=Check(Lui_RunScript(Runtime,R"(
        Editor=Instance.new('CodeEditor')
        assert(Editor.MaxLength==262144)
        Editor.Text=string.rep('a',262144)
        assert(#Editor.Text==262144 and Editor.SelectionStart==0)
        Editor.SelectionStart=262144
        assert(Editor.SelectionStart==262144)
        assert(not Editor:SetStyles(Editor.TextRevision,{{Start=262143,End=262144,Color=0x123456}}))
        assert(not pcall(function() Editor.Text=Editor.Text..'x' end))
        assert(not pcall(function() Editor.MaxLength=262143 end))
        Copy=Editor:Clone()
        assert(Copy.MaxLength==262144 and Copy.Text==Editor.Text)
        Plain=Instance.new('TextBox',{Text=string.rep('b',65534)})
        assert(Plain.MaxLength==65534 and not pcall(function() Plain.Text=Plain.Text..'x' end))
    )","CodeEditorSourceEnvelope"),Lui_GetLastError(Runtime));
    std::string Full(262144,'c'), Over=Full+"x";
    Failures+=Check(Lui_TextInput(Runtime,1,Full.c_str(),262144,0) &&
        !Lui_TextInput(Runtime,1,Over.c_str(),0,0) &&
        !Lui_TextChanged(Runtime,1,Over.c_str()),"CodeEditor full native ingress bound failed");
    Failures+=Check(Lui_TextInput(Runtime,1,"short",5,0),"CodeEditor shortening edit failed");
    Failures+=Check(Lui_RunScript(Runtime,R"(
        assert(Editor.Text=='short' and Editor.MaxLength==262144)
        assert(not pcall(function() Editor.MaxLength=5 end))
        Editor.Text=string.rep('\u{1F600}',65536)
        assert(Editor.SelectionStart==5 and Editor.MaxLength==262144)
        assert(not Editor:SetStyles(Editor.TextRevision,{{Start=65535,End=65536,Color=0xABCDEF}}))
        assert(not pcall(function() Editor.Text=Editor.Text..'x' end))
    )","CodeEditorUtf8Envelope"),Lui_GetLastError(Runtime));
    Failures+=Check(Lui_TextInput(Runtime,1,Full.c_str(),262144,0),
        "CodeEditor native high-water restore failed");
    Failures+=Check(Lui_RunScript(Runtime,"assert(#Editor.Text==262144 and Editor.MaxLength==262144)",
        "CodeEditorEnvelopeRestored"),Lui_GetLastError(Runtime));
    Lui_Destroy(Runtime);
    Runtime=Lui_CreateWithPolicy(&Policy);
    DeferredEditorBackend Deferred{Runtime,std::string(262144,'q')};
    Lui_SetBackendName(Runtime,"winui3");
    LuiPresentationCallbacksV1 Presentation{sizeof(Presentation),1,&Deferred,DeferredPresent};
    Failures+=Check(Lui_SetPresentationCallbacks(Runtime,&Presentation),
        "CodeEditor deferred presentation registration failed");
    Lui_SetBackend(Runtime,{&Deferred,DeferredCreate,DeferredProperty,
        DeferredParent,DeferredArrange,DeferredDestroy});
    Failures+=Check(Lui_RunScript(Runtime,"Editor=Instance.new('CodeEditor')",
        "CodeEditorDeferredCreate"),Lui_GetLastError(Runtime));
    Failures+=Check(Lui_RunScript(Runtime,R"(
        assert(Editor.Text==string.rep('q',262144) and Editor.MaxLength==262144)
        Editor.Text='small'
        assert(not pcall(function() Editor.MaxLength=5 end))
        Stale=Instance.new('CodeEditor',{Text='fresh'})
        assert(Stale.Text=='fresh' and Stale.MaxLength==262144)
        Stale.MaxLength=5
        assert(Stale.MaxLength==5)
    )","CodeEditorDeferredEnvelope"),Lui_GetLastError(Runtime));
    Failures+=Check(Deferred.Queued==2 && Deferred.Accepted==2,
        "CodeEditor full deferred ingress did not queue");
    Failures+=Check(Lui_RunScript(Runtime,
        "assert(Editor.Text=='small' and Stale.Text=='fresh' and Stale.MaxLength==5)",
        "CodeEditorStaleDeferredRetired"),Lui_GetLastError(Runtime));
    Lui_Destroy(Runtime);
    if(!Failures) std::puts("[LUI:TextTest] Unicode scalars, atomic selection, bounded composition and sticky secret policy passed");
    return Failures?1:0;
}
