#include "LuiExtension.h"
#include "CryptoAlgorithms.h"
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace {
struct State {
    std::array<unsigned char,65536+29> Result{};
    size_t Used=0;
    ~State() { Wipe(); }
    void Wipe() {
        volatile unsigned char* Bytes = Result.data();
        for (size_t Index=0;Index<Used;++Index) Bytes[Index]=0;
        Used=0;
    }
    void Prepare(size_t Count) { Wipe(); Used=Count; }
};
bool Bytes(const LuiValueV1& Value,size_t Limit) {
    return Value.Type==LUI_VALUE_STRING && Value.TextLength<=Limit && (Value.Text || !Value.TextLength);
}
Lui::Standard::ByteSpan Span(const LuiValueV1& Value) {
    return {reinterpret_cast<const unsigned char*>(Value.Text?Value.Text:""),static_cast<size_t>(Value.TextLength)};
}
bool RandomFill(unsigned char* Out,unsigned Size) {
#if defined(_WIN32)
    return BCryptGenRandom(nullptr,Out,Size,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0;
#else
    return RAND_bytes(Out,static_cast<int>(Size))==1;
#endif
}
int Return(State& Owner,size_t Size,LuiValueV1* Result) {
    Result->Type=LUI_VALUE_STRING;
    Result->Text=reinterpret_cast<const char*>(Owner.Result.data());
    Result->TextLength=Size;
    return 1;
}
bool Digest(bool Keyed,const char* Key,size_t KeySize,const char* Input,size_t Size,unsigned char* Out) {
#if defined(_WIN32)
    const auto Algorithm=Keyed?BCRYPT_HMAC_SHA256_ALG_HANDLE:BCRYPT_SHA256_ALG_HANDLE;
    return BCryptHash(Algorithm,reinterpret_cast<PUCHAR>(const_cast<char*>(Key)),static_cast<ULONG>(KeySize),
        reinterpret_cast<PUCHAR>(const_cast<char*>(Input)),static_cast<ULONG>(Size),Out,32)>=0;
#else
    size_t Written=0;
    if (Keyed) return EVP_Q_mac(nullptr,"HMAC",nullptr,"SHA256",nullptr,Key,KeySize,
        reinterpret_cast<const unsigned char*>(Input),Size,Out,32,&Written) && Written==32;
    return EVP_Q_digest(nullptr,"SHA256",nullptr,Input,Size,Out,&Written)==1 && Written==32;
#endif
}
int LUI_EXTENSION_CALL Hash(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Result) {
    if (Count!=1 || !Bytes(Args[0],65536)) return 0;
    auto& Owner=*static_cast<State*>(Context); Owner.Prepare(32);
    return Digest(false,nullptr,0,Args[0].Text,static_cast<size_t>(Args[0].TextLength),Owner.Result.data()) ? Return(Owner,32,Result) : 0;
}
int LUI_EXTENSION_CALL Mac(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Result) {
    if (Count!=2 || !Bytes(Args[0],4096) || !Args[0].TextLength || !Bytes(Args[1],65536)) return 0;
    auto& Owner=*static_cast<State*>(Context); Owner.Prepare(32);
    return Digest(true,Args[0].Text,static_cast<size_t>(Args[0].TextLength),Args[1].Text,
        static_cast<size_t>(Args[1].TextLength),Owner.Result.data()) ? Return(Owner,32,Result) : 0;
}
int LUI_EXTENSION_CALL Random(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Result) {
    if (Count!=1 || Args[0].Type!=LUI_VALUE_NUMBER || !std::isfinite(Args[0].Number) ||
        Args[0].Number<1 || Args[0].Number>4096 || std::floor(Args[0].Number)!=Args[0].Number) return 0;
    const auto Size=static_cast<unsigned>(Args[0].Number);
    auto& Owner=*static_cast<State*>(Context); Owner.Prepare(Size);
    if (!RandomFill(Owner.Result.data(),Size)) { Owner.Wipe(); return 0; }
    return Return(Owner,Size,Result);
}
int LUI_EXTENSION_CALL Derive(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Result) {
    if (Count!=4 || !Bytes(Args[0],2048) || Args[0].TextLength<16 || !Bytes(Args[1],1024) || !Bytes(Args[2],1024) ||
        Args[3].Type!=LUI_VALUE_NUMBER || !(Args[3].Number>=1 && Args[3].Number<=64) || std::floor(Args[3].Number)!=Args[3].Number) return 0;
    auto& Owner=*static_cast<State*>(Context); Owner.Prepare(static_cast<size_t>(Args[3].Number));
    const size_t Size=static_cast<size_t>(Args[3].Number);
    if (!Lui::Standard::DeriveSha256(Span(Args[0]),Span(Args[1]),Span(Args[2]),Owner.Result.data(),Size)) {Owner.Wipe();return 0;}
    return Return(Owner,Size,Result);
}
int LUI_EXTENSION_CALL Seal(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Result) {
    if (Count!=3 || !Bytes(Args[0],32) || Args[0].TextLength!=32 || !Bytes(Args[1],65536) || !Bytes(Args[2],4096)) return 0;
    auto& Owner=*static_cast<State*>(Context); Owner.Prepare(29+static_cast<size_t>(Args[1].TextLength));
    auto* Out=Owner.Result.data();
    Out[0]=1;
    auto Input=Span(Args[1]);
    if (!RandomFill(Out+1,12) || !Lui::Standard::Aes256Gcm(true,Span(Args[0]),{Out+1,12},Span(Args[2]),Input,Out+13,Out+13+Input.Size)) {
        Owner.Wipe(); return 0;
    }
    return Return(Owner,29+Input.Size,Result);
}
int LUI_EXTENSION_CALL Open(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Result) {
    if (Count!=3 || !Bytes(Args[0],32) || Args[0].TextLength!=32 || !Bytes(Args[1],65536+29) ||
        Args[1].TextLength<29 || Args[1].Text[0]!=1 || !Bytes(Args[2],4096)) return 0;
    auto& Owner=*static_cast<State*>(Context); Owner.Prepare(static_cast<size_t>(Args[1].TextLength)-29);
    const auto Input=Span(Args[1]); const auto Length=Input.Size-29;
    unsigned char Tag[16];
    std::memcpy(Tag,Input.Data+13+Length,sizeof(Tag));
    if (!Lui::Standard::Aes256Gcm(false,Span(Args[0]),{Input.Data+1,12},Span(Args[2]),{Input.Data+13,Length},Owner.Result.data(),Tag)) {
        Owner.Wipe(); return 0;
    }
    return Return(Owner,Length,Result);
}
}
extern "C" LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionQuery(LuiExtensionInfoV1* Info) {
    if (!Info || Info->StructSize<sizeof(*Info)) return 0;
    *Info={sizeof(*Info),1,"Crypto",1,LUI_CAPABILITY_NATIVE_EXTENSIONS | LUI_CAPABILITY_HOST_SERVICES}; return 1;
}
extern "C" LUI_EXTENSION_EXPORT int LUI_EXTENSION_CALL LuiExtensionInit(const LuiHostApiV1* Host,void** Context) {
    if (!Host || Host->AbiVersion!=1 || Host->StructSize<sizeof(*Host) || !Context) return 0;
    try {
        auto Owner=std::make_unique<State>();
        if (!Host->RegisterMethod(Host->HostContext,"Crypto","Sha256","(Bytes: string) -> string",Hash,Owner.get()) ||
            !Host->RegisterMethod(Host->HostContext,"Crypto","HmacSha256","(Key: string, Bytes: string) -> string",Mac,Owner.get()) ||
            !Host->RegisterMethod(Host->HostContext,"Crypto","RandomBytes","(Count: number) -> string",Random,Owner.get()) ||
            !Host->RegisterMethod(Host->HostContext,"Crypto","HkdfSha256","(Secret: string, Salt: string, Info: string, Count: number) -> string",Derive,Owner.get()) ||
            !Host->RegisterMethod(Host->HostContext,"Crypto","Seal","(Key: string, Plaintext: string, Aad: string) -> string",Seal,Owner.get()) ||
            !Host->RegisterMethod(Host->HostContext,"Crypto","Open","(Key: string, Envelope: string, Aad: string) -> string",Open,Owner.get())) return 0;
        *Context=Owner.release(); return 1;
    } catch (...) { return 0; }
}
extern "C" LUI_EXTENSION_EXPORT void LUI_EXTENSION_CALL LuiExtensionShutdown(void* Context) { delete static_cast<State*>(Context); }
