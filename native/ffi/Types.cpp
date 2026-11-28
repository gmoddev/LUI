#include "Core.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Lui::Ffi {
namespace {
unsigned Digit(char C) {
    if (C >= '0' && C <= '9') return C - '0';
    if (C >= 'a' && C <= 'f') return C - 'a' + 10;
    if (C >= 'A' && C <= 'F') return C - 'A' + 10;
    throw Failure("InvalidHex");
}
template<class T> void Put(void* Dest, T Value) { std::memcpy(Dest, &Value, sizeof(T)); }
template<class T> T Read(const void* Source) { T Value; std::memcpy(&Value, Source, sizeof(T)); return Value; }
size_t Align(size_t Offset,size_t Alignment) {
    return (Offset+Alignment-1)/Alignment*Alignment;
}
template<class T> void Integer(void* Dest, double Number) {
    Require(std::isfinite(Number) && std::trunc(Number) == Number &&
        Number >= std::numeric_limits<T>::lowest() && Number <= std::numeric_limits<T>::max(), "IntegerRange");
    Put(Dest, static_cast<T>(Number));
}
template<class T> void Integer(void* Dest, const Json& V) {
    Require(V.is_number(), "ExpectedInteger");
    Integer<T>(Dest,V.get<double>());
}
std::map<std::string, NativeType> BuildTypes() {
    std::map<std::string, NativeType> Types;
    auto Add = [&](const char* Name, ffi_type* Abi) {
        auto T = std::make_shared<Type>(); T->Name = Name; T->Abi = Abi;
        T->HasPointers = Abi == &ffi_type_pointer;
        T->Boolean = T->Name=="bool8" || T->Name=="bool32";
        T->PointerResults = T->Name=="pointer" ? 1 : 0;
        Types.emplace(Name, T);
    };
    Add("void", &ffi_type_void); Add("bool8", &ffi_type_uint8); Add("bool32", &ffi_type_sint32);
    Add("i8", &ffi_type_sint8); Add("u8", &ffi_type_uint8); Add("i16", &ffi_type_sint16); Add("u16", &ffi_type_uint16);
    Add("i32", &ffi_type_sint32); Add("u32", &ffi_type_uint32); Add("i64", &ffi_type_sint64); Add("u64", &ffi_type_uint64);
    Add("f32", &ffi_type_float); Add("f64", &ffi_type_double); Add("pointer", &ffi_type_pointer); Add("utf8", &ffi_type_pointer);
    Types["long"] = Types[sizeof(long)==8 ? "i64" : "i32"]; Types["ulong"] = Types[sizeof(long)==8 ? "u64" : "u32"];
    Types["intptr"] = Types["i64"]; Types["uintptr"] = Types["u64"]; Types["size"] = Types["u64"];
    static_assert(sizeof(void*)==8 && sizeof(ffi_arg)==8, "x64 ABI required");
    return Types;
}
}
void EncodeNumeric(const Type& T,double Number,bool Boolean,void* Dest) {
    const auto Kind=T.Abi->type;
    if(T.Boolean){
        Require(Boolean,"ExpectedBoolean");
    }else{
        Require(!Boolean,Kind==FFI_TYPE_FLOAT||Kind==FFI_TYPE_DOUBLE?"ExpectedNumber":"ExpectedInteger");
    }
    switch(Kind){
    case FFI_TYPE_SINT8: Integer<int8_t>(Dest,Number); return;
    case FFI_TYPE_UINT8: Integer<uint8_t>(Dest,Number); return;
    case FFI_TYPE_SINT16: Integer<int16_t>(Dest,Number); return;
    case FFI_TYPE_UINT16: Integer<uint16_t>(Dest,Number); return;
    case FFI_TYPE_SINT32: Integer<int32_t>(Dest,Number); return;
    case FFI_TYPE_UINT32: Integer<uint32_t>(Dest,Number); return;
    case FFI_TYPE_FLOAT:
        Require(std::isfinite(Number)&&std::abs(Number)<=std::numeric_limits<float>::max(),"FloatRange");
        Put(Dest,static_cast<float>(Number));return;
    case FFI_TYPE_DOUBLE:
        Require(std::isfinite(Number),"FloatRange");
        Put(Dest,Number);return;
    default: throw Failure("UnsupportedType");
    }
}
double DecodeNumeric(const Type& T,const void* Source) {
    switch(T.Abi->type){
    case FFI_TYPE_SINT8: return Read<int8_t>(Source);
    case FFI_TYPE_UINT8: return Read<uint8_t>(Source);
    case FFI_TYPE_SINT16: return Read<int16_t>(Source);
    case FFI_TYPE_UINT16: return Read<uint16_t>(Source);
    case FFI_TYPE_SINT32: return Read<int32_t>(Source);
    case FFI_TYPE_UINT32: return Read<uint32_t>(Source);
    case FFI_TYPE_FLOAT:
    case FFI_TYPE_DOUBLE: {
        const double Number=T.Abi->type==FFI_TYPE_FLOAT?Read<float>(Source):Read<double>(Source);
        Require(std::isfinite(Number),"NonFiniteResult");return Number;
    }
    default: throw Failure("UnsupportedType");
    }
}
std::string HexBytes(const unsigned char* Data, size_t Size) {
    constexpr char Digits[] = "0123456789abcdef";
    std::string Out(Size * 2, '0');
    for (size_t I=0; I<Size; ++I) { Out[2*I]=Digits[Data[I]>>4]; Out[2*I+1]=Digits[Data[I]&15]; }
    return Out;
}
std::vector<unsigned char> ParseBytes(const std::string& Text, size_t Maximum) {
    Require(Text.size()%2==0 && Text.size()/2<=Maximum, "InvalidBytes");
    std::vector<unsigned char> Out(Text.size()/2);
    for (size_t I=0; I<Out.size(); ++I) Out[I] = static_cast<unsigned char>(Digit(Text[2*I])*16+Digit(Text[2*I+1]));
    return Out;
}
uint32_t Engine::Count(const Json& V, uint32_t Max) {
    Require(V.is_number(), "InvalidCount");
    const double N = V.get<double>();
    Require(N >= 0 && N <= Max && std::trunc(N)==N, "InvalidCount");
    return static_cast<uint32_t>(N);
}
NativeType Engine::Resolve(const Json& Spec) const {
    if (!Spec.is_string()) return Get<Type>(Spec);
    static const auto Types = BuildTypes();
    auto It = Types.find(Spec.get_ref<const std::string&>());
    Require(It != Types.end(), "UnknownType");
    return It->second;
}
std::shared_ptr<Signature> Engine::Declare(const Json& Params, const Json& Result, unsigned Fixed) const {
    Require(Params.is_array() && Params.size()<=MaxArguments, "InvalidSignature");
    auto Sig = std::make_shared<Signature>();
    Sig->Result = Resolve(Result);
    Require(!Sig->Result->Array, "ArrayRequiresPointer");
    Require(Sig->Result->Abi, "MemoryLayoutRequiresPointer");
    for (const auto& P : Params) {
        auto T = Resolve(P); Require(T->Name!="void", "VoidArgument");
        Require(!T->Array, "ArrayRequiresPointer");
        Require(T->Abi, "MemoryLayoutRequiresPointer");
        Sig->Parameters.push_back(T); Sig->Abi.push_back(T->Abi);
    }
    const auto Count=static_cast<unsigned>(Params.size());
    if(Fixed) {
        Require(Fixed<=Count,"InvalidFixedCount");
        for(unsigned Index=Fixed;Index<Count;++Index) {
            const auto* Type=Sig->Abi[Index];
            // Natural C aggregates are not subject to default promotions.
            // The supported x64 backends classify their fixed ABI layout.
            if(Type->type==FFI_TYPE_STRUCT && !Sig->Parameters[Index]->PackedAbi)continue;
            Require(Type==&ffi_type_sint32||Type==&ffi_type_uint32||Type==&ffi_type_sint64||
                Type==&ffi_type_uint64||Type==&ffi_type_double||Type==&ffi_type_pointer,"VariadicPromotionRequired");
        }
    }
    const auto Status=Fixed?ffi_prep_cif_var(&Sig->Cif,FFI_DEFAULT_ABI,Fixed,Count,Sig->Result->Abi,Sig->Abi.data()):
        ffi_prep_cif(&Sig->Cif,FFI_DEFAULT_ABI,Count,Sig->Result->Abi,Sig->Abi.data());
    Require(Status==FFI_OK,"UnsupportedAbi");
    size_t Bytes=0;
    for(size_t Index=0;Index<Sig->Abi.size();++Index) {
        const auto* Type=Sig->Abi[Index];
        Require(Type->alignment!=0 && Type->alignment<=alignof(std::max_align_t),"UnsupportedAbi");
        // Win64 libffi reads small by-value aggregates through integer slots.
        Sig->ArgumentOffsets[Index]=Align(Bytes,std::max<size_t>(Type->alignment,alignof(ffi_arg)));
        Bytes=Sig->ArgumentOffsets[Index]+Type->size;
    }
    Require(Sig->Result->Abi->alignment<=alignof(std::max_align_t),"UnsupportedAbi");
    Sig->ReturnOffset=Align(Bytes,alignof(std::max_align_t));
    Sig->ScratchBytes=Sig->ReturnOffset+std::max<size_t>(Sig->Result->Abi->size,sizeof(ffi_arg));
    const auto Scalar=[](const NativeType& T) {
        return T->Children.empty() && !T->HasPointers && T->Abi->size<=sizeof(double) &&
            T->Abi!=&ffi_type_sint64 && T->Abi!=&ffi_type_uint64;
    };
    Sig->Scalar=Scalar(Sig->Result) && std::all_of(Sig->Parameters.begin(),Sig->Parameters.end(),Scalar);
    if(!Sig->Scalar) {
        // Fixed-size leaves fit the prepared packet. UTF-8 still needs the
        // structured path and its checked, call-local string storage.
        const auto Flatten=[&](const auto& Recurse,const NativeType& T,size_t Offset,
            std::vector<Signature::CompactField>& Fields)->bool {
            if(T->Children.empty()){
                if(Fields.size()==128)return false;
                auto Kind=Signature::CompactKind::Number;
                if(T->Name=="pointer")Kind=Signature::CompactKind::Handle;
                else if(T->Abi==&ffi_type_sint64 || T->Abi==&ffi_type_uint64)Kind=Signature::CompactKind::Word;
                else if(!Scalar(T))return false;
                if(T->Name!="void")Fields.push_back({T,Offset,Kind});
                Sig->CompactHandles|=Kind!=Signature::CompactKind::Number;
                return true;
            }
            for(size_t I=0;I<T->Children.size();++I)
                if(!Recurse(Recurse,T->Children[I],Offset+T->Offsets[I],Fields))return false;
            return true;
        };
        std::vector<Signature::CompactField> Arguments, Returned;
        Sig->Compact=Flatten(Flatten,Sig->Result,Sig->ReturnOffset,Returned);
        for(size_t Index=0;Sig->Compact && Index<Sig->Parameters.size();++Index)
            Sig->Compact=Flatten(Flatten,Sig->Parameters[Index],Sig->ArgumentOffsets[Index],Arguments);
        if(Sig->Compact){
            Sig->CompactArguments=std::move(Arguments);
            Sig->CompactResult=std::move(Returned);
        }
        Sig->CompactHandles &= Sig->Compact;
    }
    return Sig;
}
Json Engine::Layout(const Json& Req) {
    auto T = std::make_shared<Type>(); T->Array = Req.contains("Element"); T->Name = T->Array ? "array" : "struct";
    T->Leaves = 0;
    size_t Pack=0;
    if(Req.contains("Pack")){
        Pack=Count(Req.at("Pack"),16);
        Require(!T->Array && Pack && (Pack&(Pack-1))==0,"InvalidPacking");
    }
    bool MemoryOnly=false;
    auto Append = [&](const std::string& Name, const Json& Child) {
        auto C = Resolve(Child);
        Require(C->Name!="void" && C->Depth<8 && T->Leaves + C->Leaves<=128, "LayoutLimit");
        T->Depth = std::max(T->Depth, C->Depth+1); T->Leaves+=C->Leaves; T->HasPointers |= C->HasPointers;
        T->PackedAbi |= C->PackedAbi;
        T->PointerResults+=C->PointerResults;
        // A cap that preserves each field's natural alignment leaves the
        // complete aggregate layout and libffi classification unchanged.
        MemoryOnly=MemoryOnly || !C->Abi || (Pack && Pack<C->Alignment());
        T->Children.push_back(C); T->Fields.push_back(Name); T->Elements.push_back(C->Abi);
    };
    if (T->Array) {
        const auto N=Count(Req.at("Count"),128); Require(N>0,"InvalidLayout");
        for (unsigned I=0; I<N; ++I) Append("", Req.at("Element"));
    } else {
        const auto& Fields=Req.at("Fields"); Require(Fields.is_array() && !Fields.empty() && Fields.size()<=32,"InvalidLayout");
        for (const auto& F:Fields) {
            const auto Name=F.at("Name").get<std::string>();
            Require(!Name.empty() && Name.size()<=48 && std::find(T->Fields.begin(),T->Fields.end(),Name)==T->Fields.end(),"InvalidField");
            Append(Name,F.at("Type"));
        }
    }
    T->Offsets.resize(T->Children.size());
    if(MemoryOnly){
        T->PackedAbi=true;
        T->MemoryAlignment=1;
        size_t End=0;
        for(size_t I=0;I<T->Children.size();++I){
            const auto& Child=T->Children[I];
            const auto Alignment=Pack ? std::min(Pack,Child->Alignment()) : Child->Alignment();
            T->MemoryAlignment=std::max(T->MemoryAlignment,Alignment);
            T->Offsets[I]=Align(End,Alignment);
            End=T->Offsets[I]+Child->Size();
        }
        T->MemorySize=Align(End,T->MemoryAlignment);
#if defined(_WIN32)
        // Microsoft x64 transfers POD aggregates according to their size.
        // libffi's Win64 backend uses the supplied size to select an integer
        // slot or an indirect copy. Offsets remain the declared packed ones.
        T->Elements.push_back(nullptr);
        T->Aggregate.size=T->MemorySize;
        T->Aggregate.alignment=static_cast<unsigned short>(T->MemoryAlignment);
        T->Aggregate.type=FFI_TYPE_STRUCT;
        T->Aggregate.elements=T->Elements.data();
        T->Abi=&T->Aggregate;
#endif
    } else {
        T->Elements.push_back(nullptr);
        T->Aggregate.type=FFI_TYPE_STRUCT;T->Aggregate.elements=T->Elements.data();T->Abi=&T->Aggregate;
        Require(ffi_get_struct_offsets(FFI_DEFAULT_ABI,T->Abi,T->Offsets.data())==FFI_OK,"UnsupportedLayout");
    }
    Require(T->Size()<=MaxValueBytes && T->Alignment()<=alignof(Slot),"UnsupportedLayout");
    return {{"Size",T->Size()},{"Alignment",T->Alignment()},{"Offsets",T->Offsets},{"Ref",Add(T)}};
}
void Engine::Encode(const NativeType& T, const Json& V, void* Dest, Held& Pins) {
    auto* Bytes=static_cast<unsigned char*>(Dest);
    if (!T->Children.empty()) {
        Require((T->Array ? V.is_array() : V.is_object()) && V.size()==T->Children.size(),"InvalidAggregate");
        for(size_t I=0; I<T->Children.size(); ++I) Encode(T->Children[I], T->Array ? V.at(I) : V.at(T->Fields[I]), Bytes+T->Offsets[I], Pins);
        return;
    }
    const auto& N=T->Name;
    if (N=="i8") Integer<int8_t>(Dest,V); else if (N=="u8") Integer<uint8_t>(Dest,V);
    else if(N=="i16") Integer<int16_t>(Dest,V); else if(N=="u16") Integer<uint16_t>(Dest,V);
    else if(N=="i32") Integer<int32_t>(Dest,V); else if(N=="u32") Integer<uint32_t>(Dest,V);
    else if(N=="i64" || N=="u64") {
        Require(V.is_string() && V.get_ref<const std::string&>().size()==16,"ExpectedWord64");
        uint64_t Bits=0; for(char C:V.get_ref<const std::string&>()) Bits=(Bits<<4)|Digit(C); Put(Dest,Bits);
    } else if(N=="bool8" || N=="bool32") {
        Require(V.is_boolean(),"ExpectedBoolean");
        if(N=="bool8") Put(Dest,static_cast<uint8_t>(V.get<bool>())); else Put(Dest,static_cast<int32_t>(V.get<bool>()));
    } else if(N=="f32" || N=="f64") {
        Require(V.is_number(),"ExpectedNumber"); const double D=V.get<double>();
        EncodeNumeric(*T,D,false,Dest);
    } else if(N=="utf8") {
        if (V.is_null()) { Put(Dest, static_cast<const char*>(nullptr)); return; }
        Require(V.is_string(),"ExpectedString");
        auto S=std::make_shared<std::string>(V.get<std::string>());
        Require(S->size()<=MaxChunk && S->find('\0')==S->npos,"InvalidCString");
        Put(Dest,S->c_str()); Pins.push_back(ManagedPointer(S->data(),S->size()+1,S));
    } else if(N=="pointer") {
        if(V.is_null()) { Put(Dest,static_cast<void*>(nullptr)); return; }
        auto P=Get<Pointer>(V.at("Ref")); Put(Dest,P->Address); Pins.push_back(P);
    } else throw Failure("UnsupportedType");
}
Json Engine::Decode(const NativeType& T, const void* Source, const Held& Pins) {
    if (!T->Children.empty()) {
        Json V=T->Array ? Json::array() : Json::object();
        for(size_t I=0; I<T->Children.size(); ++I) {
            auto Child=Decode(T->Children[I],static_cast<const unsigned char*>(Source)+T->Offsets[I],Pins);
            if(T->Array) V.push_back(std::move(Child)); else V[T->Fields[I]]=std::move(Child);
        }
        return V;
    }
    const auto& N=T->Name;
    if(N=="void") return nullptr;
    if(N=="bool8") return Read<uint8_t>(Source)!=0;
    if(N=="bool32") return Read<int32_t>(Source)!=0;
    if(N=="i8") return Read<int8_t>(Source); if(N=="u8") return Read<uint8_t>(Source);
    if(N=="i16") return Read<int16_t>(Source); if(N=="u16") return Read<uint16_t>(Source);
    if(N=="i32") return Read<int32_t>(Source); if(N=="u32") return Read<uint32_t>(Source);
    if(N=="i64" || N=="u64") {
        const auto V=Read<uint64_t>(Source); unsigned char Big[8];
        for(unsigned I=0; I<8; ++I) Big[I]=static_cast<unsigned char>(V>>(56-8*I));
        return HexBytes(Big,8);
    }
    if(N=="f32" || N=="f64") return DecodeNumeric(*T,Source);
    if(N=="utf8") {
        auto P=Read<const char*>(Source); if (!P) return nullptr;
        size_t Size=0; while(Size<MaxChunk && P[Size]) ++Size; Require(Size<MaxChunk,"StringLimit");
        return std::string(P,Size);
    }
    auto Address=Read<void*>(Source); if(!Address) return nullptr;
    return {{"Ref",Add(ReturnedPointer(Address,Pins))}};
}
}
