#include "Wire.h"
#include "Callbacks.h"
#include "LuiExtension.h"
namespace {
struct Session { Lui::Ffi::Engine Service; std::string Reply; bool Computing=false; };
int Exchange(Session& S,std::string_view Packet,LuiValueV1* Result) {
    S.Reply=Lui::Ffi::Exchange(S.Service,Packet);
    Result->Type=LUI_VALUE_STRING;Result->Text=S.Reply.data();Result->TextLength=S.Reply.size();return 1;
}
int LUI_EXTENSION_CALL Dispatch(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Result) {
    if(!Context || !Args || !Result || Count!=1 || Args[0].Type!=LUI_VALUE_STRING || !Args[0].Text || Args[0].TextLength>131072)return 0;
    try{
        auto& S=*static_cast<Session*>(Context);
        if(S.Computing)return 0;
        return Exchange(S,{Args[0].Text,static_cast<size_t>(Args[0].TextLength)},Result);
    } catch(...){return 0;}
}
}
extern "C" LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiFfiDispatchScopedV1(void* Context,
    const char* Packet,size_t Size,LuiFfiComputeV1 Compute,void* Script,LuiValueV1* Result){
    if(!Context || !Packet || Size>131072 || !Compute || !Script || !Result)return 0;
    auto& S=*static_cast<Session*>(Context);
    if(S.Computing)return 0;
    S.Computing=true;
    int Status=0;
    try{
        Lui::Ffi::Callbacks::Scope Scope(S.Service,Compute,Script);
        Status=Exchange(S,{Packet,Size},Result);
    }catch(...){}
    S.Computing=false;
    return Status;
}
extern "C" LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionQuery(LuiExtensionInfoV1* Info){
    if(!Info || Info->StructSize<sizeof(*Info))return 0;
    *Info={sizeof(*Info),1,"Ffi",1,LUI_CAPABILITY_NATIVE_EXTENSIONS|LUI_CAPABILITY_HOST_SERVICES};return 1;
}
extern "C" LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionInit(const LuiHostApiV1* Host,void** Context){
    if(!Host || Host->AbiVersion!=1 || !Context || Host->StructSize<sizeof(*Host))return 0;
    try{
        auto Value=std::make_unique<Session>();
        if(!Host->RegisterMethod(Host->HostContext,"Ffi","Dispatch","(Packet: string) -> string",Dispatch,Value.get()))return 0;
        *Context=Value.release();return 1;
    }catch(...){return 0;}
}
extern "C" LUI_EXTENSION_EXPORT void LUI_EXTENSION_CALL LuiExtensionShutdown(void* Context){delete static_cast<Session*>(Context);}
