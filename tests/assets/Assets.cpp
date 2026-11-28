#include "LuiRuntime.h"

#include <cstdio>
#include <string>
#include <vector>
#include <utility>

struct ImageProvider {
    std::vector<std::pair<int,uint64_t>> Requests;
    std::vector<LuiImageSource*> Sources;
    int Canceled=0;
};
static int LUI_CALL Load(void* Context,int Id,uint64_t Token,const char*) {
    static_cast<ImageProvider*>(Context)->Requests.emplace_back(Id,Token); return 1;
}
static void LUI_CALL Cancel(void* Context,uint64_t) {++static_cast<ImageProvider*>(Context)->Canceled;}
static int LUI_CALL LoadSource(void* Context,int Id,uint64_t Token,LuiImageSource* Source) {
    auto& Provider=*static_cast<ImageProvider*>(Context);
    Provider.Sources.push_back(Source);
    Lui_RetainImageSourceV1(Source);
    return Load(Context,Id,Token,nullptr);
}

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
        "assert(not app:GetService('PlatformService'):Supports('ImageRegion'))\n"
        "assert(Image.SourceRectSize.X == 0 and Image.SourceRectSize.Y == 0)\n"
        "Image.SourceRectOffset = Vector2.new(12, 24); Image.SourceRectSize = Vector2.new(16, 32)\n"
        "for _, Bad in {Vector2.new(0, 1), Vector2.new(-1, 2), Vector2.new(2.5, 3), Vector2.new(4097, 1)} do\n"
        "    assert(not pcall(function() Image.SourceRectSize = Bad end))\n"
        "end\n"
        "local Copy = Image:Clone()\n"
        "assert(Copy.Source == 'logo.png')\n"
        "assert(Copy.SourceRectOffset.X == 12 and Copy.SourceRectOffset.Y == 24)\n"
        "assert(Copy.SourceRectSize.X == 16 and Copy.SourceRectSize.Y == 32)\n"
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
    Runtime=Lui_Create();
    Provider={};
    LuiImageSourceCallbacksV1 Sources{sizeof(Sources),1,&Provider,LoadSource};
    auto WrongContext=Sources; WrongContext.Context=nullptr;
    Failures+=Check(!Lui_SetImageSourceCallbacksV1(Runtime,&Sources) &&
        Lui_SetImageCallbacks(Runtime,&Images) && !Lui_SetImageSourceCallbacksV1(Runtime,&WrongContext) &&
        Lui_SetImageSourceCallbacksV1(Runtime,&Sources) &&
        !Lui_SetImageSourceCallbacksV1(Runtime,&Sources),"source bridge validation failed");
    Failures+=Check(Lui_RunScript(Runtime,R"(
        local Assets=app:GetService('AssetService')
        assert(app:GetService('PlatformService'):Supports('RuntimeImages'))
        local Bytes=buffer.create(24)
        buffer.writeu32(Bytes,0,0xff332211)
        buffer.writeu32(Bytes,12,0xff665544)
        local Resource,Failure=Assets:FromPixels(Bytes,2,2,12,'BGRA8Premultiplied')
        assert(Resource and not Failure and Resource.Width==2 and Resource.Height==2)
        buffer.fill(Bytes,0,0)
        assert(select(2,Assets:FromEncoded(buffer.create(24)))=='InvalidImage')
        assert(select(2,Assets:FromPixels(Bytes,2,2,6,'BGRA8Premultiplied'))=='InvalidArgument')
        assert(select(2,Assets:FromPixels(Bytes,2,2,12,'RGBA8'))=='Unsupported')
        assert(not pcall(function() Instance.new('ImageLabel',{Source='', ImageResource=Resource}) end))
        A=Instance.new('ImageLabel',{ImageResource=Resource})
        B=A:Clone()
        assert(A.ImageResource==B.ImageResource and A.ImageResource==Resource and A.Source=='')
        Resource:Close(); Resource:Close()
        assert(Resource.IsClosed and not pcall(function() A.ImageResource=Resource end))
        assert(A.ImageResource==Resource)
    )","ImageSourceOwnership"),Lui_GetLastError(Runtime));
    Failures+=Check(Provider.Sources.size()==2 && Provider.Sources[0]==Provider.Sources[1],"clone did not share source");
    for(const auto& Request:Provider.Requests)
        Failures+=Check(Lui_ImageCompleted(Runtime,Request.first,Request.second,1),"runtime image completion rejected");
    Failures+=Check(Lui_RunScript(Runtime,"assert(A.LoadState=='Ready' and B.LoadState=='Ready'); A.ImageResource=nil; local _=A.AbsoluteSize; assert(A.LoadState=='Idle'); B:Destroy(); A:Destroy()","ImageSourceDetach"),Lui_GetLastError(Runtime));
    Lui_Destroy(Runtime);
    for(auto* Source:Provider.Sources) {
        LuiImageSourceViewV1 View{sizeof(View),1};
        const bool Valid=Lui_ImageSourceViewV1(Source,&View) && View.Kind==2 && View.RowStride==8 && View.ByteCount==16;
        Failures+=Check(Valid && static_cast<const unsigned char*>(View.Data)[0]==0x11 &&
            static_cast<const unsigned char*>(View.Data)[8]==0x44,"immutable packed pixels were lost during retirement");
        Lui_ReleaseImageSourceV1(Source);
    }
    if (!Failures) std::puts("[LUI:AssetTest] Declared asset and ImageLabel semantics passed");
    return Failures ? 1 : 0;
}
