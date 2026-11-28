#include "Callbacks.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <new>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace Lui::Ffi {
Engine::Engine()=default;
Engine::~Engine()=default;
Ownership::~Ownership(){
    if(Release){
        void* Address=reinterpret_cast<void*>(Base);
        void* Args[]={&Address};
        ffi_call(&Release->Declaration->Cif,FFI_FN(Release->Address),nullptr,Args);
    }
}
namespace {
std::shared_ptr<Ownership> RootOwner(std::shared_ptr<Ownership> Owner){
    while(Owner->Parent)Owner=Owner->Parent;
    return Owner;
}
}
std::shared_ptr<Ownership> Engine::FindOwner(void* Address){
    const auto Base=reinterpret_cast<uintptr_t>(Address);
    for(auto It=Owners.begin();It!=Owners.end();){
        auto Owner=It->second.lock();
        if(!Owner){It=Owners.erase(It);continue;}
        if(Base==Owner->Base || (Base>Owner->Base && Base-Owner->Base<Owner->Extent))
            return RootOwner(Owner);
        ++It;
    }
    auto Owner=std::make_shared<Ownership>();
    Owner->Base=Base;
    Owners[Base]=Owner;
    return Owner;
}
void Engine::ExtendOwner(const std::shared_ptr<Ownership>& Owner,size_t Size){
    Require(Size>=Owner->Extent && Size<=UINTPTR_MAX-Owner->Base,"InvalidOwnership");
    if(Size==Owner->Extent)return;
    // Interior addresses may have escaped before the native extent was declared.
    // Validate every overlap before attaching their existing handles to this lifetime.
    std::vector<std::shared_ptr<Ownership>> Interior;
    for(auto It=Owners.lower_bound(Owner->Base);It!=Owners.end() && It->first-Owner->Base<Size;++It){
        auto Other=It->second.lock();
        if(!Other || Other==Owner || Other->Parent)continue;
        Require(!Other->Locked && Other->Extent<=Size-(Other->Base-Owner->Base),"InvalidOwnership");
        Interior.push_back(Other);
    }
    for(const auto& Other:Interior)Other->Parent=Owner;
    Owner->Extent=Size;
}
std::shared_ptr<Pointer> Engine::ManagedPointer(void* Address,size_t Size,std::shared_ptr<void> Storage){
    auto P=std::make_shared<Pointer>();
    auto Owner=FindOwner(Address);
    Require(!Owner->Locked && Owner->Base==reinterpret_cast<uintptr_t>(Address),"InvalidOwnership");
    ExtendOwner(Owner,Size);
    Owner->Storage=std::move(Storage);Owner->Locked=true;
    P->Address=Address;P->Bounded=true;P->Size=Size;P->Owner=std::move(Owner);
    return P;
}
std::shared_ptr<Pointer> Engine::ReturnedPointer(void* Address,const Held& Pins){
    auto P=std::make_shared<Pointer>();
    P->Address=Address;P->Pins=Pins;
    const auto Base=reinterpret_cast<uintptr_t>(Address);
    for(const auto& Pin:Pins){
        auto Source=std::dynamic_pointer_cast<Pointer>(Pin);
        if(!Source || !Source->Bounded)continue;
        const auto Start=reinterpret_cast<uintptr_t>(Source->Address);
        if(Base>=Start && Base-Start<=Source->Size){
            const auto Remaining=Source->Size-(Base-Start);
            if(!P->Bounded || Remaining<P->Size){
                P->Bounded=true;P->Size=Remaining;P->Owner=RootOwner(Source->Owner);
            }
        }
    }
    if(!P->Owner)P->Owner=FindOwner(Address);
    auto& Owner=*P->Owner;
    if(!P->Bounded && (Owner.Locked || Base!=Owner.Base)){
        P->Bounded=true;P->Size=Owner.Extent-(Base-Owner.Base);
    }
    P->LibraryOwned=!P->Bounded && !Owner.Locked;
    return P;
}
void Engine::RequireHandleCapacity(size_t Count) const{
    Require(Count<=1024-Resources.size() && (!Count || (Next && Count-1<=UINT32_MAX-Next)),"HandleLimit");
}
void Engine::RollbackHandles(unsigned First){
    if(!First)return;
    auto It=Resources.lower_bound(First);
    while(It!=Resources.end()){
        if(Notices) Notices->Retire(It->first);
        It=Resources.erase(It);
    }
}
unsigned Engine::Add(const std::shared_ptr<Resource>& Value){
    RequireHandleCapacity(1);
    const unsigned Id=Next++;Resources.emplace(Id,Value);return Id;
}
namespace {
unsigned char* Range(const std::shared_ptr<Pointer>& P,size_t Offset,size_t Size){
    Require(P->Bounded && Offset<=P->Size && Size<=P->Size-Offset,"OutOfBounds");
    return static_cast<unsigned char*>(P->Address)+Offset;
}
struct NativeErrors {int32_t Errno;uint32_t LastError;};
NativeErrors CallNative(const Function& F,void* Result,void** Arguments){
    errno=0;
#ifdef _WIN32
    SetLastError(0);
#endif
    ffi_call(&F.Declaration->Cif,FFI_FN(F.Address),Result,Arguments);
#ifdef _WIN32
    const auto SystemError=GetLastError();
#else
    const uint32_t SystemError=0;
#endif
    return {errno,SystemError};
}
template<class T> T ReadBits(std::string_view Bytes,size_t Offset){
    T Value;
    std::memcpy(&Value,Bytes.data()+Offset,sizeof(Value));
    return Value;
}
template<class T> void AppendBits(std::string& Bytes,T Value){
    Bytes.append(reinterpret_cast<const char*>(&Value),sizeof(Value));
}
struct CallStorage {
    alignas(std::max_align_t) unsigned char Inline[256];
    std::unique_ptr<std::max_align_t[]> Heap;
    unsigned char* Bytes=Inline;
    explicit CallStorage(size_t Size) {
        if(Size>sizeof(Inline)){
            const size_t Words=(Size+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t);
            Heap.reset(new std::max_align_t[Words]);
            Bytes=reinterpret_cast<unsigned char*>(Heap.get());
        }
        std::memset(Bytes,0,Size);
    }
};
}
Json Engine::Invoke(const std::shared_ptr<Function>& F,const Json& Arguments){
    const auto& Sig=*F->Declaration;
    Require(Arguments.is_array() && Arguments.size()==Sig.Parameters.size(),"ArgumentCount");
    RequireHandleCapacity(Sig.Result->PointerResults);
    CallStorage Storage(Sig.ScratchBytes);
    std::array<void*,MaxArguments> Pointers;
    Held Pins;
    if(Sig.Result->PointerResults) Pins.push_back(F);
    for(size_t I=0;I<Arguments.size();++I){
        Pointers[I]=Storage.Bytes+Sig.ArgumentOffsets[I];
        Encode(Sig.Parameters[I],Arguments[I],Pointers[I],Pins);
    }
    auto* Returned=Storage.Bytes+Sig.ReturnOffset;
    const auto Errors=CallNative(*F,Returned,Pointers.data());
    return {{"Value",Decode(Sig.Result,Returned,Pins)},{"Errno",Errors.Errno},{"LastError",Errors.LastError}};
}
std::string Engine::InvokeCompact(const std::shared_ptr<Function>& F,std::string_view Packet) {
    const auto& Sig=*F->Declaration;
    const bool Extended=Packet[0]=='H'||Packet[0]=='J';
    Require(Sig.Compact && Sig.CompactHandles==Extended,"CompactSignatureRequired");
    Require(Packet.size()==5+sizeof(double)*Sig.CompactArguments.size(),"ArgumentCount");
    RequireHandleCapacity(Sig.Result->PointerResults);
    CallStorage Storage(Sig.ScratchBytes);
    std::array<void*,MaxArguments> Arguments;
    Held Pins;
    if(Sig.Result->PointerResults) Pins.push_back(F);
    for(size_t I=0;I<Sig.Parameters.size();++I)
        Arguments[I]=Storage.Bytes+Sig.ArgumentOffsets[I];
    size_t Offset=5;
    for(const auto& Field:Sig.CompactArguments){
        auto* Target=Storage.Bytes+Field.Offset;
        if(Field.Kind==Signature::CompactKind::Handle){
            const auto Ref=ReadBits<uint64_t>(Packet,Offset);
            Require(Ref<=UINT32_MAX,"InvalidBinaryArgument");
            if(Ref){
                auto P=Get<Pointer>(static_cast<unsigned>(Ref));
                std::memcpy(Target,&P->Address,sizeof(P->Address));
                Pins.push_back(std::move(P));
            }
        }else if(Field.Kind==Signature::CompactKind::Word){
            std::memcpy(Target,Packet.data()+Offset,sizeof(uint64_t));
        }else{
            const auto Number=ReadBits<double>(Packet,Offset);
            if(Field.Value->Boolean)Require(Number==0 || Number==1,"InvalidBinaryArgument");
            EncodeNumeric(*Field.Value,Number,Field.Value->Boolean,Target);
        }
        Offset+=sizeof(double);
    }
    const bool WithError=Packet[0]=='D'||Packet[0]=='J';
    std::string Reply(1,'\0');
    Reply.reserve(1+(WithError?8:0)+sizeof(double)*Sig.CompactResult.size());
    const auto Errors=CallNative(*F,Storage.Bytes+Sig.ReturnOffset,Arguments.data());
    if(WithError){AppendBits(Reply,Errors.Errno);AppendBits(Reply,Errors.LastError);}
    const auto Checkpoint=HandleCheckpoint();
    try {
        for(const auto& Field:Sig.CompactResult){
            const auto* Source=Storage.Bytes+Field.Offset;
            if(Field.Kind==Signature::CompactKind::Handle){
                void* Address=nullptr;
                std::memcpy(&Address,Source,sizeof(Address));
                AppendBits(Reply,static_cast<uint64_t>(Address?Add(ReturnedPointer(Address,Pins)):0));
            }else if(Field.Kind==Signature::CompactKind::Word){
                Reply.append(reinterpret_cast<const char*>(Source),sizeof(uint64_t));
            }else{
                const auto Value=DecodeNumeric(*Field.Value,Source);
                AppendBits(Reply,Field.Value->Boolean?(Value!=0?1.0:0.0):Value);
            }
        }
    }catch(...){
        RollbackHandles(Checkpoint);
        throw;
    }
    return Reply;
}
std::string Engine::DispatchBinary(std::string_view Packet){
    // The optional provider builds exclusively for little-endian x64.
    // The prefix has already been consumed and offsets are checked before use.
    Require(Packet.size()>=5,"InvalidBinaryPacket");
    const auto Id=ReadBits<uint32_t>(Packet,1);
    std::string Reply(1,'\0');
    if(Packet[0]=='C'||Packet[0]=='E'){
        const auto F=Get<Function>(Id);
        const auto& Sig=*F->Declaration;
        Require(Sig.Scalar,"ScalarSignatureRequired");
        Require(Packet.size()==5+9*Sig.Parameters.size(),"ArgumentCount");
        alignas(std::max_align_t) unsigned char Storage[256]{};
        Require(Sig.ScratchBytes<=sizeof(Storage),"UnsupportedAbi");
        std::array<void*,MaxArguments> Arguments;
        for(size_t I=0;I<Sig.Parameters.size();++I){
            const size_t At=5+9*I;
            const auto Value=ReadBits<double>(Packet,At+1);
            Arguments[I]=Storage+Sig.ArgumentOffsets[I];
            if(Packet[At]=='N') EncodeNumeric(*Sig.Parameters[I],Value,false,Arguments[I]);
            else {
                Require(Packet[At]=='B' && (Value==0 || Value==1),"InvalidBinaryArgument");
                EncodeNumeric(*Sig.Parameters[I],Value,true,Arguments[I]);
            }
        }
        auto* Returned=Storage+Sig.ReturnOffset;
        const auto Errors=CallNative(*F,Returned,Arguments.data());
        if(Packet[0]=='E'){
            AppendBits(Reply,Errors.Errno);AppendBits(Reply,Errors.LastError);
        }
        if(Sig.Result->Abi==&ffi_type_void) Reply+='V';
        else {
            const auto Value=DecodeNumeric(*Sig.Result,Returned);
            if(Sig.Result->Boolean) Reply+=Value!=0?'T':'F';
            else {Reply+='N';AppendBits(Reply,Value);}
        }
        return Reply;
    }
    if(Packet[0]=='A'||Packet[0]=='D'||Packet[0]=='H'||Packet[0]=='J')return InvokeCompact(Get<Function>(Id),Packet);
    Require(Packet.size()>=9,"InvalidBinaryPacket");
    const auto Offset=ReadBits<uint32_t>(Packet,5);
    Require(Offset<=1<<20,"InvalidCount");
    if(Packet[0]=='R'){
        Require(Packet.size()==13,"InvalidBinaryPacket");
        const auto Size=ReadBits<uint32_t>(Packet,9);
        Require(Size<=MaxChunk,"InvalidCount");
        const auto* Bytes=Range(Get<Pointer>(Id),Offset,Size);
        Reply.append(reinterpret_cast<const char*>(Bytes),Size);
    } else if(Packet[0]=='W'){
        const auto Bytes=Packet.substr(9);
        Require(Bytes.size()<=MaxChunk,"InvalidBytes");
        auto* Dest=Range(Get<Pointer>(Id),Offset,Bytes.size());
        if(!Bytes.empty())std::memcpy(Dest,Bytes.data(),Bytes.size());
    } else throw Failure("UnknownBinaryOperation");
    return Reply;
}
Json Engine::Dispatch(const Json& R){
    Require(R.is_object(),"InvalidRequest");
    const auto& Op=R.at("Op").get_ref<const std::string&>();
    if(Op=="Platform"){
#ifdef _WIN32
        const char* Os="Windows";
#else
        const char* Os="Linux";
#endif
        return {{"OS",Os},{"PointerBytes",sizeof(void*)},{"LongBytes",sizeof(long)},{"Abi","C-x64"},
            {"Binary",true},{"PackedLayouts",true}};
    }
    if(Op=="Open")return {{"Ref",Add(OpenImage(R))}};
    if(Op=="Layout")return Layout(R);
    if(Op=="Describe"){
        auto T=Resolve(R.at("Type"));Require(T->Name!="void","VoidHasNoLayout");
        return {{"Size",T->Size()},{"Alignment",T->Alignment()},{"Offsets",T->Offsets}};
    }
    if(Op=="Bind"||Op=="BindVariadic"){
        const auto Fixed=Op=="BindVariadic"?Count(R.at("Fixed"),MaxArguments):0;
        Require(Op=="Bind"||Fixed,"InvalidFixedCount");
        auto Image=Get<Lui::Ffi::Image>(R.at("Library"));
        auto F=std::make_shared<Function>(); F->Library=Image;
        F->Declaration=Declare(R.at("Params"),R.at("Result"),Fixed);
        F->Address=Image->Symbol(R.at("Symbol").get<std::string>());
        return {{"Ref",Add(F)},{"Scalar",F->Declaration->Scalar},
            {"Compact",F->Declaration->Compact&&!F->Declaration->CompactHandles},
            {"CompactHandles",F->Declaration->CompactHandles}};
    }
    if(Op=="Call")return Invoke(Get<Function>(R.at("Function")),R.at("Args"));
    if(Op=="Close"){
        const unsigned Id=Count(R.at("Ref"),UINT32_MAX);
        if(Notices) Notices->Retire(Id);
        return Resources.erase(Id)!=0;
    }
    if(Op=="Callback"){
        if(!Notices) Notices=std::make_unique<Callbacks>();
        return Notices->Create(*this,R);
    }
    if(Op=="Poll")return Notices?Notices->Poll(*this):Json{{"Events",Json::array()},{"Dropped",0}};
    if(Op=="Stats")return {{"Live",Resources.size()},{"AllocatedBytes",Allocated?*Allocated:0}};
    if(Op=="Allocate"){
        RequireHandleCapacity(1);
        const auto Size=Count(R.at("Size"),1<<20), Alignment=Count(R.value("Alignment",16),64);
        const auto Used=Allocated?*Allocated:0;
        Require(Size && Alignment && (Alignment&(Alignment-1))==0 && Used+Size<=8*1024*1024,"AllocationLimit");
        if(!Allocated) Allocated=std::make_shared<size_t>(0);
        const size_t Actual=std::max<size_t>(Alignment,alignof(std::max_align_t));
        auto* Raw=::operator new(Size,std::align_val_t(Actual)); std::memset(Raw,0,Size);
        auto Counter=Allocated;*Counter+=Size;
        std::shared_ptr<void> Storage(Raw,[Counter,Size,Actual](void* P){::operator delete(P,std::align_val_t(Actual));*Counter-=Size;});
        auto P=ManagedPointer(Raw,Size,std::move(Storage));
        return {{"Ref",Add(P)}};
    }
    // A borrowed view always retains its source. Only TrustBorrow can attach a user-asserted bound to an external address.
    auto P=Get<Pointer>(R.at("Ref"));
    if(Op=="Slice" || Op=="TrustBorrow"){
        RequireHandleCapacity(1);
        const auto Size=Count(R.at("Size"),1<<20),Offset=Count(R.value("Offset",0),1<<20);
        auto View=std::make_shared<Pointer>();
        if(Op=="Slice" || P->Bounded)View->Address=Range(P,Offset,Size);
        else {
            Require(P->LibraryOwned && Offset==0,"BorrowRequiresNativePointer");
            auto Owner=RootOwner(P->Owner);
            const auto Relative=reinterpret_cast<uintptr_t>(P->Address)-Owner->Base;
            if(Owner->Locked || Relative)
                Require(Relative<=Owner->Extent && Size<=Owner->Extent-Relative,"OutOfBounds");
            else if(Size>Owner->Extent)ExtendOwner(Owner,Size);
            View->Address=P->Address;
        }
        View->Size=Size;View->Bounded=true;View->Owner=P->Owner;View->Pins.push_back(P);return {{"Ref",Add(View)}};
    }
    if(Op=="Adopt"){
        auto Drop=Get<Function>(R.at("Release"));auto& Sig=*Drop->Declaration;
        auto Owner=RootOwner(P->Owner);
        Require(P->LibraryOwned && !P->Bounded && !Owner->Locked && Owner->Base==reinterpret_cast<uintptr_t>(P->Address) &&
            Sig.Parameters.size()==1 && Sig.Parameters[0]->Name=="pointer" &&
            Sig.Result->Name=="void","InvalidOwnership");
        const auto Size=Count(R.at("Size"),1<<20);
        // Commit only after validating the entire transfer. Existing views retain this owner.
        ExtendOwner(Owner,Size);
        Owner->Release=std::move(Drop);Owner->Locked=true;
        P->LibraryOwned=false; P->Bounded=true; P->Size=Size;
        return true;
    }
    const auto Offset=Count(R.value("Offset",0),1<<20);
    if(Op=="Read"){
        const auto N=Count(R.at("Size"),MaxChunk);return HexBytes(Range(P,Offset,N),N);
    }
    if(Op=="Write"){
        auto Bytes=ParseBytes(R.at("Bytes").get<std::string>(),MaxChunk);
        auto* Dest=Range(P,Offset,Bytes.size());if(!Bytes.empty())std::memcpy(Dest,Bytes.data(),Bytes.size());return true;
    }
    auto T=Resolve(R.at("Type"));Require(T->Name!="void","VoidHasNoLayout");
    auto* Memory=Range(P,Offset,T->Size());
    if(Op=="Get")return Decode(T,Memory,{P});
    if(Op=="Set"){
        Require(!T->HasPointers,"PointerStorageRequiresBinding");
        Slot Value;Held Pins;Encode(T,R.at("Value"),Value.Data.data(),Pins);std::memcpy(Memory,Value.Data.data(),T->Size());return true;
    }
    throw Failure("UnknownOperation");
}
}
