#include "Wire.h"
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

// Each selected child accepts a bounded request with one fixed signature.
// Handles and callback addresses never cross this transport.
int main(int Count,char** Args){
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    using namespace Lui::Ffi;
    if(Count!=2 || std::string(Args[1])!="--call")return 2;
    try{
        std::string Line;char C;
        while(std::cin.get(C) && C!='\n'){Require(Line.size()<4096,"PacketLimit");Line+=C;}
        auto R=Json::parse(Line,[](int Depth,Json::parse_event_t,Json&){Require(Depth<=12,"PacketDepth");return true;});
        const auto& Params=R.at("Params");const auto Result=R.at("Result").get<std::string>();
        Require(Params.is_array() && Result!="pointer","BrokerPointerUnsupported");
        for(const auto& P:Params)Require(P.is_string() && P!="pointer","BrokerPointerUnsupported");
        const Json* Rows=nullptr;
        if(R.contains("Rows")){
            Rows=&R.at("Rows");
            Require(!R.contains("Args")&&Rows->is_array()&&!Rows->empty()&&Rows->size()<=32,"BatchLimit");
            for(const auto& Row:*Rows)
                Require(Row.is_array()&&Row.size()==Params.size(),"ArgumentCount");
        }
        Engine E;
        auto Selection=R.at("Library");Selection["Op"]="Open";
        auto Lib=E.Dispatch(Selection);
        auto F=E.Dispatch({{"Op","Bind"},{"Library",Lib.at("Ref")},{"Params",Params},{"Result",Result},{"Symbol",R.at("Symbol")}});
        Json Call={{"Op","Call"},{"Function",F.at("Ref")}};
        if(Rows){
            std::string Output=R"({"Ok":true,"Result":[)";
            bool First=true;
            for(const auto& Row:*Rows){
                Call["Args"]=Row;
                if(!First)Output+=',';
                First=false;
                auto Item=E.Dispatch(Call).dump();
                Require(Output.size()+Item.size()+3<=65536,"ResultLimit");
                Output+=Item;
            }
            std::cout<<Output<<"]}\n";return 0;
        }
        Call["Args"]=R.at("Args");
        auto Reply=E.Dispatch(Call);
        std::cout<<Json{{"Ok",true},{"Result",Reply}}.dump()<<'\n';return 0;
    }catch(const Failure& Why){std::cout<<Json{{"Ok",false},{"Error",Why.what()}}.dump()<<'\n';return 1;}
    catch(...){std::cout<<R"({"Ok":false,"Error":"InvalidNativeRequest"})"<<'\n';return 1;}
}
