// Separates the trusted provider's packet, typed dispatch and libffi costs.
#include "../../native/ffi/Wire.h"
#include "Fixture.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

using namespace Lui::Ffi;

template<class Operation>
static void Measure(const char* Name,Operation Call) {
    using Clock=std::chrono::steady_clock;
    constexpr int Repetitions=20000;
    std::vector<double> Samples;
    volatile uint64_t Checksum=0;
    for(int Sample=-1;Sample<9;++Sample) {
        const auto Begin=Clock::now();
        for(int Index=0;Index<Repetitions;++Index) Checksum+=Call();
        if(Sample>=0) Samples.push_back(
            std::chrono::duration<double,std::nano>(Clock::now()-Begin).count()/Repetitions);
    }
    Require(Checksum!=0,"ProbeChecksum");
    std::sort(Samples.begin(),Samples.end());
    std::printf("[LUI:FfiCost] %s n=%zu iterations=%d minNs=%.2f medianNs=%.2f maxNs=%.2f\n",
        Name,Samples.size(),Repetitions,Samples.front(),Samples[4],Samples.back());
}

int main() {
    Engine Service;
    auto Scalar=std::make_shared<Function>();
    Scalar->Declaration=Service.Declare(Json::array({"u32"}),"u32");
    Scalar->Address=reinterpret_cast<void*>(&LuiFfiEchoU32);
    const auto Handle=Service.Add(Scalar);
    const Json Call{{"Op","Call"},{"Function",Handle},{"Args",Json::array({42})}};
    const std::string Packet=Call.dump();
    const auto Receipt=Json::parse(Exchange(Service,Packet));
    Require(Receipt.at("Ok")==true && Receipt.at("Result").at("Value")==42,"ProbeScalar");
    std::string Binary="LFB1C";
    Binary.append(reinterpret_cast<const char*>(&Handle),sizeof(Handle));
    Binary+='N';
    const double Number=42;
    Binary.append(reinterpret_cast<const char*>(&Number),sizeof(Number));
    const auto BinaryReply=Exchange(Service,Binary);
    Require(BinaryReply.size()==10 && BinaryReply[0]=='\0' && BinaryReply[1]=='N',"ProbeBinaryScalar");
    double Answer;
    std::memcpy(&Answer,BinaryReply.data()+2,sizeof(Answer));
    Require(Answer==Number,"ProbeBinaryValue");

    uint32_t Argument=42;
    ffi_arg Return=0;
    void* Inputs[]{&Argument};
    Measure("DirectScalar",[&]{return LuiFfiEchoU32(Argument);});
    Measure("LibffiScalar",[&]{
        ffi_call(&Scalar->Declaration->Cif,FFI_FN(Scalar->Address),&Return,Inputs);
        return Return;
    });
    Measure("TypedScalar",[&]{return Service.Dispatch(Call).at("Value").get<unsigned>();});
    Measure("PacketScalar",[&]{return Exchange(Service,Packet).size();});
    Measure("BinaryScalar",[&]{return Exchange(Service,Binary).size();});
    Binary[4]='E';
    Measure("BinaryScalarWithError",[&]{return Exchange(Service,Binary).size();});
    Measure("HandleLookup",[&]{return Service.Get<Function>(Handle)==Scalar;});
    const auto ParameterTypes=Json::array({"u32"});
    const Json ReturnType="u32";
    Measure("PrepareScalar",[&]{return Service.Declare(ParameterTypes,ReturnType)->ScratchBytes;});

    auto Mixed=std::make_shared<Function>();
    Mixed->Declaration=Service.Declare(Json::array({"i8","f64","u16","f32","i32","f64","u32","f32"}),"f64");
    Mixed->Address=reinterpret_cast<void*>(&LuiFfiMix);
    const Json MixedCall{{"Op","Call"},{"Function",Service.Add(Mixed)},{"Args",Json::array({1,2.0,3,4.0,5,6.0,7,8.0})}};
    const std::string MixedPacket=MixedCall.dump();
    Require(Service.Dispatch(MixedCall).at("Value")==LuiFfiMix(1,2,3,4,5,6,7,8),"ProbeMixed");
    Measure("TypedMixed",[&]{return static_cast<unsigned>(Service.Dispatch(MixedCall).at("Value").get<double>());});
    Measure("PacketMixed",[&]{return Exchange(Service,MixedPacket).size();});

    const auto Pair=Service.Dispatch({{"Op","Layout"},{"Fields",Json::array({
        {{"Name","Left"},{"Type","i32"}},{{"Name","Right"},{"Type","f64"}}})}}).at("Ref");
    const auto Adjust=std::make_shared<Function>();
    Adjust->Declaration=Service.Declare(Json::array({Pair,"i32"}),Pair);
    Adjust->Address=reinterpret_cast<void*>(&LuiFfiAdjustPair);
    const auto AdjustId=Service.Add(Adjust);
    const Json AdjustCall{{"Op","Call"},{"Function",AdjustId},
        {"Args",Json::array({{{"Left",7},{"Right",2.5}},3})}};
    const std::string AdjustPacket=AdjustCall.dump();
    std::string Compact="LFB1A";
    Compact.append(reinterpret_cast<const char*>(&AdjustId),sizeof(AdjustId));
    const double Fields[]{7,2.5,3};
    Compact.append(reinterpret_cast<const char*>(Fields),sizeof(Fields));
    LuiFfiPair Input{7,2.5}, Output{};
    int32_t Delta=3;
    void* AdjustArguments[]{&Input,&Delta};
    const auto Expected=LuiFfiAdjustPair(Input,Delta);
    Require(Service.Dispatch(AdjustCall).at("Value").at("Right")==Expected.Right,"ProbePair");
    Measure("DirectPair",[&]{return static_cast<unsigned>(LuiFfiAdjustPair(Input,Delta).Right);});
    Measure("LibffiPair",[&]{
        ffi_call(&Adjust->Declaration->Cif,FFI_FN(Adjust->Address),&Output,AdjustArguments);
        return static_cast<unsigned>(Output.Right);
    });
    Measure("TypedPair",[&]{return static_cast<unsigned>(Service.Dispatch(AdjustCall).at("Value").at("Right").get<double>());});
    Measure("PacketPair",[&]{return Exchange(Service,AdjustPacket).size();});
    const auto Packed=Exchange(Service,Compact);
    if(Packed[0]=='\0'){
        double Right;
        Require(Packed.size()==17,"ProbeCompactPairLength");
        std::memcpy(&Right,Packed.data()+9,sizeof(Right));
        Require(Right==Expected.Right,"ProbeCompactPairResult");
        Measure("CompactPair",[&]{return Exchange(Service,Compact).size();});
    }

    const auto MeasureFixed=[&](const char* StructuredName,const char* CompactName,void* Address,
        const char* Parameter,const char* Result,const Json& Value,uint64_t Bits,uint64_t Expected) {
        auto FunctionValue=std::make_shared<Function>();
        FunctionValue->Declaration=Service.Declare(Json::array({Parameter}),Result);
        FunctionValue->Address=Address;
        const auto Ref=Service.Add(FunctionValue);
        const std::string Structured=Json{{"Op","Call"},{"Function",Ref},{"Args",Json::array({Value})}}.dump();
        std::string Fixed="LFB1H";
        Fixed.append(reinterpret_cast<const char*>(&Ref),sizeof(Ref));
        Fixed.append(reinterpret_cast<const char*>(&Bits),sizeof(Bits));
        const auto Reply=Exchange(Service,Fixed);
        Require(Reply.size()==9 && Reply[0]=='\0',"ProbeFixedReply");
        uint64_t Observed;
        std::memcpy(&Observed,Reply.data()+1,sizeof(Observed));
        Require(Observed==Expected,"ProbeFixedValue");
        Require(Json::parse(Exchange(Service,Structured)).at("Ok")==true,"ProbeStructuredFixedValue");
        Measure(StructuredName,[&]{return Exchange(Service,Structured).size();});
        Measure(CompactName,[&]{return Exchange(Service,Fixed).size();});
    };
    const auto PairRef=Service.Add(Service.ManagedPointer(&Input,sizeof(Input),{}));
    const double Sum=LuiFfiSumPair(&Input);
    uint64_t SumBits;
    std::memcpy(&SumBits,&Sum,sizeof(SumBits));
    MeasureFixed("PacketPointer","CompactPointer",reinterpret_cast<void*>(&LuiFfiSumPair),
        "pointer","f64",Json{{"Ref",PairRef}},PairRef,SumBits);
    constexpr uint64_t Word=UINT64_C(0xfedcba9876543210);
    MeasureFixed("PacketWord","CompactWord",reinterpret_cast<void*>(&LuiFfiEchoU64),
        "u64","u64","fedcba9876543210",Word,LuiFfiEchoU64(Word));
}
