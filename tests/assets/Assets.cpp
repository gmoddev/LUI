#include "LuiRuntime.h"

#include <cstdio>
#include <string>
#include <vector>
#include <utility>

struct ImageProvider {std::vector<std::pair<int,uint64_t>> Requests; int Canceled=0;};
static int LUI_CALL Load(void* Context,int Id,uint64_t Token,const char*) {
    static_cast<ImageProvider*>(Context)->Requests.emplace_back(Id,Token); return 1;
}
static void LUI_CALL Cancel(void* Context,uint64_t) {++static_cast<ImageProvider*>(Context)->Canceled;}

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:AssetTest] %s\n", Message);
    return Condition ? 0 : 1;
}

int main() {
    LuiRuntime* Runtime = Lui_Create();
    if (!Runtime) return 1;
    int Failures = 0;
    Failures += Check(Lui_RegisterAsset(Runtime, "logo.png") == 1, "asset registration failed");
    Failures += Check(Lui_RegisterAsset(Runtime, "logo.png") == 0, "duplicate asset accepted");
    Failures += Check(Lui_RegisterAsset(Runtime, "../outside.png") == 0, "asset path escaped package");
    Failures += Check(Lui_RunScript(Runtime,
        "local Assets = app:GetService('AssetService')\n"
        "assert(Assets:Has('logo.png') and not Assets:Has('missing.png'))\n"
        "local Image = Instance.new('ImageLabel', {Source = 'logo.png'})\n"
        "assert(Image.Source == 'logo.png')\n"
        "local Copy = Image:Clone()\n"
        "assert(Copy.Source == 'logo.png')\n"
        "local Ok, Error = pcall(function() Image.Source = 'missing.png' end)\n"
        "assert(not Ok and string.find(Error, 'not declared'))\n"
        "Copy:Destroy(); Image:Destroy()", "AssetSemantics") == 1,
        Lui_GetLastError(Runtime));
    Failures += Check(Lui_RegisterAsset(Runtime, "late.png") == 0, "late asset registration accepted");
    Lui_Destroy(Runtime);
    Runtime=Lui_Create(); ImageProvider Provider;
    LuiImageCallbacksV1 Images{sizeof(Images),1,&Provider,Load,Cancel};
    auto Invalid=Images; Invalid.AbiVersion=2;
    Failures+=Check(!Lui_SetImageCallbacks(Runtime,&Invalid) && Lui_SetImageCallbacks(Runtime,&Images) && !Lui_SetImageCallbacks(Runtime,&Images),"image bridge validation failed");
    Failures+=Check(Lui_RegisterAsset(Runtime,"logo.png") && Lui_RegisterAsset(Runtime,"other.png"),"image registration failed");
    Failures+=Check(Lui_RunScript(Runtime,"Image=Instance.new('ImageLabel',{Source='logo.png'}); Changes=0; Image.LoadChanged:Connect(function() Changes+=1 end)","ImageLoad"),Lui_GetLastError(Runtime));
    Failures+=Check(Provider.Requests.size()==1,"load was not admitted");
    const auto First=Provider.Requests.back();
    Failures+=Check(Lui_RunScript(Runtime,"assert(Image.LoadState=='Loading'); Image.Source='other.png'","CancelLoad"),Lui_GetLastError(Runtime));
    Failures+=Check(Provider.Canceled==1 && !Lui_ImageCompleted(Runtime,First.first,First.second,1),"canceled completion was accepted");
    const auto Second=Provider.Requests.back();
    Failures+=Check(Lui_ImageCompleted(Runtime,Second.first,Second.second,1) && !Lui_ImageCompleted(Runtime,Second.first,Second.second,1),"completion was not exactly once");
    Failures+=Check(Lui_RunScript(Runtime,"assert(Image.LoadState=='Ready'); Image.Source='logo.png'; Image:Destroy(); Image:Destroy()","DestroyLoad"),Lui_GetLastError(Runtime));
    const auto Retired=Provider.Requests.back();
    Failures+=Check(!Lui_ImageCompleted(Runtime,Retired.first,Retired.second,1),"destroyed image received completion");
    Failures+=Check(Lui_RunScript(Runtime,R"(
        Images={}
        for Index=1,33 do Images[Index]=Instance.new('ImageLabel',{Source='logo.png'}) end
    )","ImageAdmission"),Lui_GetLastError(Runtime));
    Failures+=Check(Lui_RunScript(Runtime,"assert(Images[33].LoadState=='Failed'); for _,Image in Images do Image:Destroy() end","ImageLimit"),Lui_GetLastError(Runtime));
    Lui_Destroy(Runtime);
    if (!Failures) std::puts("[LUI:AssetTest] Declared asset and ImageLabel semantics passed");
    return Failures ? 1 : 0;
}
