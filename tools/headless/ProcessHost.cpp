#include "ProcessHost.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <stdexcept>

namespace Lui::Headless {
namespace {
LuiValueV1 Number(double Value) {LuiValueV1 V{sizeof(V),LUI_VALUE_NUMBER}; V.Number=Value; return V;}
LuiValueV1 Text(const char* Data,size_t Size) {LuiValueV1 V{sizeof(V),LUI_VALUE_STRING}; V.Text=Data; V.TextLength=Size; return V;}
void Require(bool Good) {if (!Good) throw std::invalid_argument("InvalidProcessOptions");}
}
ProcessHost::ProcessHost(LuiRuntime* Owner,const std::string& Tool,const std::string& Directory,std::set<std::string> Keys,std::function<void()> Notify):
    Runtime(Owner),Grant(Tool,Directory),EnvironmentKeys(std::move(Keys)),Wake(std::move(Notify)) {
    if (!Lui_RegisterHostMethod(Runtime,"Process","Start","(Options: string, Input: string) -> number",Start,this) ||
        !Lui_RegisterHostMethod(Runtime,"Process","Stop","(Id: number) -> boolean",Stop,this) ||
        !Lui_RegisterHostSignal(Runtime,"Process","Output","Signal<number, number, string>") ||
        !Lui_RegisterHostSignal(Runtime,"Process","Finished","Signal<number, string, number>")) throw std::runtime_error("ToolUnavailable");
}
bool ToolFeed::Write(std::string Data) {
    std::lock_guard<std::mutex> Lock(Mutex);
    if(Eof || Stopped || Data.size()>65536-Bytes || Queue.size()>=64) return false;
    if(Data.empty()) return true;
    auto Count=Data.size(); Queue.push_back(std::move(Data));Bytes+=Count;return true;
}
bool ToolFeed::Close() {
    std::lock_guard<std::mutex> Lock(Mutex);
    if(Eof || Stopped) return false;
    Eof=true;return true;
}
bool ToolFeed::Pull(std::string& Data) {
    std::lock_guard<std::mutex> Lock(Mutex);
    if(!Queue.empty()) {Data=std::move(Queue.front());Queue.pop_front();Bytes-=Data.size();}
    return Eof && Queue.empty() && Data.empty();
}
ProcessHost::~ProcessHost() {
    for(auto& Pair:Jobs) Pair.second->Canceled=true;
    for(auto& Pair:Jobs) if(Pair.second->Thread.joinable()) Pair.second->Thread.join();
}
int ProcessHost::Start(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Out) {
    if (Count!=2 || Args[0].Type!=LUI_VALUE_STRING || Args[0].TextLength>16384 || Args[1].Type!=LUI_VALUE_STRING || Args[1].TextLength>4096) return 0;
    auto& S=*static_cast<ProcessHost*>(Context);

    try {
        const auto Options=nlohmann::json::parse(Args[0].Text,Args[0].Text+Args[0].TextLength,
            [](int Depth,nlohmann::json::parse_event_t,nlohmann::json&){Require(Depth<=4);return true;});
        Require(Options.is_object());
        if(Options.contains("Command")) {
            Require(Options.size()==2 && Options.at("Id").is_number_unsigned());
            const auto Id=Options.at("Id").get<uint64_t>();
            auto I=Id<=UINT32_MAX?S.Jobs.find(static_cast<unsigned>(Id)):S.Jobs.end();
            bool Ok=false;
            if(I!=S.Jobs.end() && I->second->Feed && !I->second->Done && !I->second->Canceled) {
                if(Options.at("Command")=="Write") Ok=I->second->Feed->Write(std::string(Args[1].Text?Args[1].Text:"",Args[1].TextLength));
                else if(Options.at("Command")=="Eof") Ok=I->second->Feed->Close();
            }
            *Out=Number(Ok?1:0);return 1;
        }
        const bool Interactive=Options.value("Interactive",false);
        Require(Options.size()==(Interactive?5:4) && S.Id<UINT32_MAX && (Interactive?S.Jobs.size()<2:!S.Pending()));
        ToolSpec Spec;
        const auto& Arguments=Options.at("Arguments"); const auto& Environment=Options.at("Environment");
        Require(Arguments.is_array() && Arguments.size()<=32 && Environment.is_object() && Environment.size()<=16);
        for (const auto& Item:Arguments) {
            auto Arg=Item.get<std::string>(); Require(Arg.size()<=2048 && Arg.find(char(0))==Arg.npos);
            Spec.Arguments.push_back(std::move(Arg));
        }
        for (const auto& Pair:Environment.items()) {
            auto Value=Pair.value().get<std::string>();
            Require(S.EnvironmentKeys.count(Pair.key()) && Value.size()<=2048 && Value.find(char(0))==Value.npos);
            Spec.Environment.emplace(Pair.key(),std::move(Value));
        }
        const auto& Timeout=Options.at("Timeout"); const auto& Limit=Options.at("Limit");
        Require(Timeout.is_number_unsigned() && Timeout<=30000 && Timeout>=1 && Limit.is_number_unsigned() && Limit<=131072);
        Spec.Timeout=Timeout.get<unsigned>(); Spec.Limit=Limit.get<unsigned>();
        Spec.Input.assign(Args[1].Text?Args[1].Text:"",static_cast<size_t>(Args[1].TextLength));
        auto Work=std::make_unique<Job>();
        if(Interactive) {
            Work->Feed=std::make_shared<ToolFeed>();Spec.Feed=Work->Feed;
            Require(Spec.Input.empty() || Work->Feed->Write(Spec.Input));
        }
        const auto Id=++S.Id;auto& J=*Work;
        S.Jobs.emplace(Id,std::move(Work));
        try { J.Thread=std::thread([&S,&J,Spec=std::move(Spec)]{
            ToolResult Result;
            try {
                size_t Total=0;
                Result=S.Grant.Run(Spec,J.Canceled,[&](unsigned Stream,const char* Data,size_t Count) {
                    if (Count>Spec.Limit-Total) return false;
                    Total+=Count;
                    {
                        std::lock_guard<std::mutex> Lock(J.Mutex);
                        // Coalesce tiny writes and limit the number of outstanding signal records.
                        if (!J.Output.empty() && J.Output.back().Stream==Stream && J.Output.back().Data.size()+Count<=8192)
                            J.Output.back().Data.append(Data,Count);
                        else {
                            if (J.Output.size()>=256) return false;
                            J.Output.push_back({Stream,std::string(Data,Count)});
                        }
                    }
                    S.Wake(); return true;
                });
            } catch (...) {}
            if(J.Feed) {std::lock_guard<std::mutex> Lock(J.Feed->Mutex);J.Feed->Stopped=true;}
            J.Result=Result; J.Done=true; S.Wake();
        }); } catch(...) {S.Jobs.erase(Id);throw;}
        *Out=Number(S.Id); return 1;
    } catch (...) { return 0; }
}
int ProcessHost::Stop(void* Context,const LuiValueV1* Args,uint32_t Count,LuiValueV1* Out) {
    if (Count!=1 || Args[0].Type!=LUI_VALUE_NUMBER) return 0;
    auto& S=*static_cast<ProcessHost*>(Context);
    const auto N=Args[0].Number;
    auto I=N>=1 && N<=UINT32_MAX && N==static_cast<unsigned>(N)?S.Jobs.find(static_cast<unsigned>(N)):S.Jobs.end();
    Out->Type=LUI_VALUE_BOOLEAN; Out->Boolean=I!=S.Jobs.end() && !I->second->Done && !I->second->Canceled.exchange(true);
    return 1;
}
bool ProcessHost::Pump() {
    // Only pump sessions present on entry. A callback can start another session.
    const auto Last=Id;
    for(auto I=Jobs.begin();I!=Jobs.end() && I->first<=Last;) {
        const auto JobId=I->first;auto& J=*I->second;
        const bool Finished=J.Done.load();
        std::vector<Chunk> Ready;
        {std::lock_guard<std::mutex> Lock(J.Mutex);Ready.swap(J.Output);}
        for(const auto& Chunk:Ready) {
            LuiValueV1 Args[]={Number(JobId),Number(Chunk.Stream),Text(Chunk.Data.data(),Chunk.Data.size())};
            if(!Lui_EmitHostSignal(Runtime,"Process","Output",Args,3)) return false;
        }
        if(!Finished) {++I;continue;}
        J.Thread.join();
        const char* Status=J.Canceled.load()?"Canceled":J.Result.Status;
        LuiValueV1 Args[]={Number(JobId),Text(Status,std::strlen(Status)),Number(J.Result.Exit)};
        I=Jobs.erase(I);
        if(!Lui_EmitHostSignal(Runtime,"Process","Finished",Args,3)) return false;
    }
    return true;
}
}
