#include "../../native/abi/LuiRuntime.h"
#include "../../native/abi/LuiProject.h"
#include "../../runtime/project/Project.h"
#include "../../runtime/project/Host.h"
#include "../../runtime/project/Json.h"
#include "../../runtime/internal/Diagnostics.h"
#include "../../runtime/internal/State.h"
#include "../../runtime/project/Files.h"
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <cstring>
#include <cmath>
#include <limits>
#include <set>
#include <emscripten/heap.h>

namespace {
using Json = nlohmann::json;
struct Projection {
    LuiRuntime* Runtime = nullptr;
    std::map<int, std::string> Nodes;
    std::map<int, std::string> TextStates;
    std::map<std::string, std::string> Files;
    std::map<std::string, size_t> Expected;
    std::map<std::string, std::string> Hashes;
    std::set<std::string> Complete;
    std::string SourceDigest, CompiledDigest;
    size_t CaptureBytes = 0;
    bool Capturing = false;
    Json Staged = Json::array();
    size_t Bytes = 0;
    uint32_t Revision = 0, InFlight = 0, Applied = 0;
    bool Failed = false;
    std::string Output;
    bool Queue(Json Operation) {
        if (Failed) return false;
        const auto Size = Operation.dump().size();
        if (Staged.size() >= 4096 || Size > 1024 * 1024 - Bytes) { Failed = true; return false; }
        Bytes += Size; Staged.push_back(std::move(Operation)); return true;
    }
} Host;
int Create(void*, int Id, const char* Class) {
    static const std::set<std::string> Classes{"Window","Frame","ScrollingFrame","TextLabel","TextButton","CheckBox","Slider","ProgressBar","TextBox","ImageLabel"};
    if (!Class || !Classes.count(Class) || Host.Nodes.size() >= 4096 || Host.Nodes.count(Id)) return 0;
    if (!std::strcmp(Class,"Window")) for (const auto& Pair : Host.Nodes) if (Pair.second == "Window") return 0;
    if (!Host.Queue({{"Kind","Create"},{"Id",Id},{"Class",Class}})) return 0;
    Host.Nodes.emplace(Id,Class); return 1;
}
int Property(void*, int Id, const char* Name, const char* Value) {
    return Host.Nodes.count(Id) && Name && Value &&
        Host.Queue({{"Kind","Property"},{"Id",Id},{"Name",Name},{"Value",Value}});
}
int Parent(void*, int Id, int ParentId) {
    return Host.Nodes.count(Id) && (!ParentId || Host.Nodes.count(ParentId)) &&
        Host.Queue({{"Kind","Parent"},{"Id",Id},{"Parent",ParentId}});
}
int Arrange(void*, int Id, double X, double Y, double Width, double Height, int) {
    return Host.Nodes.count(Id) && Host.Queue({{"Kind","Arrange"},{"Id",Id},{"X",X},{"Y",Y},{"Width",Width},{"Height",Height}});
}
int Destroy(void*, int Id) {
    Host.TextStates.erase(Id);
    if (!Host.Nodes.erase(Id)) return 1;
    return Host.Queue({{"Kind","Destroy"},{"Id",Id}});
}
int Present(void*, int Id, int Clip, double X, double Y, double Width, double Height, int Rank, int Input) {
    return Host.Nodes.count(Id) && Host.Queue({{"Kind","Present"},{"Id",Id},{"Clip",Clip},
        {"X",X},{"Y",Y},{"Width",Width},{"Height",Height},{"Rank",Rank},{"Input",Input}});
}
void Retire() {
    if (Host.Runtime) { Lui_Destroy(Host.Runtime); Host.Runtime = nullptr; }
    Host.Nodes.clear(); Host.Staged = Json::array(); Host.Bytes = 0; Host.InFlight = 0;
    Host.TextStates.clear(); Host.Files.clear(); Host.Expected.clear(); Host.Hashes.clear(); Host.Complete.clear();
    Host.CaptureBytes = 0; Host.Capturing = false;
}
void Fail() {
    if (Host.Runtime) FailBackend(Host.Runtime, "BrowserPresentation", 0);
    Retire(); Host.Failed = true;
}
Json Words(uint64_t Value) { return Json::array({static_cast<uint32_t>(Value),static_cast<uint32_t>(Value>>32)}); }
uint64_t WordValue(const Json& Value) {
    if (!Value.is_array() || Value.size()!=2) throw std::runtime_error("U64");
    for (const auto& Word:Value) if (!Word.is_number_unsigned() || Word.get<uint64_t>()>0xffffffff) throw std::runtime_error("U64");
    return Value[0].get<uint64_t>() | (Value[1].get<uint64_t>()<<32);
}
int LoadImage(void*, int Id, uint64_t Token, const char* Source) {
    return Source && Host.Files.count(Source) && Host.Queue({{"Kind","Image"},{"Id",Id},{"Token",Words(Token)},{"Source",Source}});
}
void CancelImage(void*, uint64_t Token) { Host.Queue({{"Kind","CancelImage"},{"Id",1},{"Token",Words(Token)}}); }
bool Admitted(int Id, uint32_t Revision) {
    return Host.Runtime && !Host.Failed && !Host.InFlight && Host.Staged.empty() && Revision && Revision==Host.Applied && Host.Nodes.count(Id);
}
}

extern "C" int Lui_BrowserBegin(const char* Descriptor) {
    Retire(); Host=Projection{};
    try {
        if (!Descriptor || std::strlen(Descriptor)>192*1024) return 0;
        auto Data=Lui::Project::Internal::ParseJson(Descriptor);
        const std::set<std::string> Fields{"FormatVersion","Profile","FrameworkVersion","SchemaSha256","LuauRevision","SourceDigest","CompiledDigest","Content","Runtime"};
        if (!Data.is_object() || Data.size()!=Fields.size()) return 0;
        for(const auto& Field:Fields) if(!Data.contains(Field)) return 0;
        if(Data.at("FormatVersion")!=1 || Data.at("Profile")!="BrowserHostV1" || Data.at("FrameworkVersion")!="0.0.1" ||
            Data.at("LuauRevision")!="c0e346edd89066b44dca174c9f54ce84c746a540" ||
            Data.at("SchemaSha256")!=Lui::Project::Hash(Lui_GetSchemaJson())) return 0;
        const auto& Content=Data.at("Content");
        if(!Content.is_array() || Content.empty() || Content.size()>256) return 0;
        Lui::Project::Internal::PathIndex Paths;
        size_t Total=0;
        for(const auto& File:Content) {
            if(!File.is_object() || File.size()!=3 || !File.contains("Path") || !File.contains("Bytes") || !File.contains("Sha256")) return 0;
            const auto Path=File.at("Path").get<std::string>(); Paths.Add(Path);
            if(!File.at("Bytes").is_number_unsigned()) return 0;
            const auto Size=File.at("Bytes").get<uint64_t>();
            const auto Hash=File.at("Sha256").get<std::string>();
            if(Size>8*1024*1024 || Size>32*1024*1024-Total || Hash.size()!=64 || Hash.find_first_not_of("0123456789abcdef")!=std::string::npos) return 0;
            Total+=static_cast<size_t>(Size); Host.Expected.emplace(Path,Size); Host.Hashes.emplace(Path,Hash);
        }
        Host.SourceDigest=Data.at("SourceDigest").get<std::string>(); Host.CompiledDigest=Data.at("CompiledDigest").get<std::string>();
        for(const auto* Digest:{&Host.SourceDigest,&Host.CompiledDigest})
            if(Digest->size()!=64 || Digest->find_first_not_of("0123456789abcdef")!=std::string::npos) return 0;
        Host.Capturing=true; return 1;
    } catch(...) {Fail();return 0;}
}
extern "C" int Lui_BrowserCapture(const char* Path, const char* Bytes, uint32_t Size, int Final) {
    try {
        if(!Host.Capturing || !Path || !Host.Expected.count(Path) || Host.Complete.count(Path) || Size>65536 || (!Bytes && Size)) return 0;
        auto& Target=Host.Files[Path]; const auto Maximum=Host.Expected.at(Path);
        if(Target.size()>Maximum || Size>Maximum-Target.size() || Size>32*1024*1024-Host.CaptureBytes) return 0;
        Target.append(Bytes,Size); Host.CaptureBytes+=Size;
        if(Final) {
            if(Target.size()!=Maximum || Lui::Project::Hash(Target)!=Host.Hashes.at(Path)) return 0;
            Host.Complete.insert(Path);
        }
        return 1;
    } catch(...) {Fail();return 0;}
}
extern "C" int Lui_BrowserStart(const char* Input) {
    try {
        if (Host.Runtime) return 0;
        if(Input && *Input) {
            Retire(); Host = Projection{};
            if (std::strlen(Input) > 1024 * 1024) return 0;
            const auto Data = Lui::Project::Internal::ParseJson(Input);
            if (!Data.is_object() || Data.size()!=1 || !Data.contains("Files") || !Data["Files"].is_object()) return 0;
            for (auto It=Data["Files"].begin();It!=Data["Files"].end();++It) {
                if (!It.value().is_string()) return 0;
                Host.Files.emplace(It.key(),It.value().get<std::string>());
            }
        } else {
            if(!Host.Capturing || Host.Complete.size()!=Host.Expected.size()) return 0;
            Host.Capturing=false;
        }
        std::string Error;
        auto Sources = Lui::Project::PrepareCaptured(Host.Files,"lui.json",{},Error);
        if (!Sources) return 0;
        auto Manifest=Json::parse(Sources->GetDescriptor()).at("Manifest");
        if (!Manifest.at("Capabilities").empty() || !Manifest.at("Extensions").empty() ||
            !Manifest.at("ExtensionDeclarations").empty()) return 0;
        auto Compiled=Lui::Project::Compile(Sources,{},Error);
        if (!Compiled) return 0;
        if(!Host.SourceDigest.empty() && (Sources->GetDigest()!=Host.SourceDigest || Compiled->GetDigest()!=Host.CompiledDigest)) return 0;
        LuiDiagnosticPolicyV1 Policy{sizeof(Policy),1,LUI_DIAGNOSTICS_RESTRICTED,0};
        Host.Runtime=Lui_CreateWithPolicy(&Policy);
        if (!Host.Runtime) return 0;
        Lui_SetBackendName(Host.Runtime,"browser");
        LuiPresentationCallbacksV1 Presentation{sizeof(Presentation),1,nullptr,Present};
        LuiCapabilityDeclarationV1 Capabilities{sizeof(Capabilities),1,0};
        LuiSandboxLimitsV1 Limits{sizeof(Limits),1,32*1024*1024,1000000};
        if (!Lui_SetPresentationCallbacks(Host.Runtime,&Presentation) || !Lui_DeclareCapabilities(Host.Runtime,&Capabilities) ||
            !Lui_ConfigureSandbox(Host.Runtime,&Limits)) {Fail();return 0;}
        Lui_SetBackend(Host.Runtime,{nullptr,Create,Property,Parent,Arrange,Destroy});
        LuiImageCallbacksV1 Images{sizeof(Images),1,nullptr,LoadImage,CancelImage};
        if(!Lui_SetImageCallbacks(Host.Runtime,&Images)) {Fail();return 0;}
        for(const auto& Asset:Manifest.at("Assets")) if(!Lui_RegisterAsset(Host.Runtime,Asset.get_ref<const std::string&>().c_str())) {Fail();return 0;}
        auto* Project=WrapProject(std::move(Compiled));
        LuiProjectRunOptionsV1 Options{sizeof(Options),1,0,0,0};
        const int Result=Lui_RunProject(Host.Runtime,Project,&Options);
        Lui_ProjectRelease(Project);
        if (!Result || Host.Failed) {Fail();return 0;}
        return 1;
    } catch (...) {Fail();return 0;}
}
extern "C" const char* Lui_BrowserTake() {
    try {
        if(Host.Runtime && !Host.Failed) for(const auto& Pair:Host.Runtime->Nodes) {
            const auto& Node=*Pair.second;
            if(Node.Destroyed || Node.ClassName!="TextBox") continue;
            Json State{{"Kind","TextState"},{"Id",Node.Id},{"Text",Node.Text},{"Start",Node.Secret?0:Node.SelectionStart},
                {"Length",Node.Secret?0:Node.SelectionLength},{"InputRevision",Words(Node.InputRevision)},{"Multiline",Node.Multiline},
                {"Secret",Node.Secret},{"MaxLength",Node.MaxLength},{"Placeholder",Node.PlaceholderText}};
            const auto Serialized=State.dump();
            if(Host.TextStates[Node.Id]!=Serialized) {Host.Queue(std::move(State)); Host.TextStates[Node.Id]=Serialized;}
        }
        Json Output{{"Failed",Host.Failed},{"Revision",Host.InFlight},{"Ops",Json::array()},{"Nodes",Host.Nodes.size()}};
        if (!Host.Failed && !Host.InFlight && !Host.Staged.empty()) {
            if (Host.Revision==std::numeric_limits<uint32_t>::max()) {Fail();return "{\"Failed\":true}";}
            Host.InFlight=++Host.Revision;
            Output["Revision"]=Host.InFlight;
            size_t Taken=0, Count=0;
            for(const auto& Op:Host.Staged) {
                const auto Size=Op.dump().size();
                if(Size>192*1024 || (Count && Size>192*1024-Taken)) break;
                Output["Ops"].push_back(Op); Taken+=Size; ++Count;
            }
            if(!Count) {Fail();return "{\"Failed\":true}";}
            Host.Staged.erase(Host.Staged.begin(),Host.Staged.begin()+static_cast<Json::difference_type>(Count)); Host.Bytes-=Taken;
        }
        Host.Output=Output.dump(); return Host.Output.c_str();
    } catch (...) {Fail();return "{\"Failed\":true}";}
}
extern "C" int Lui_BrowserAck(uint32_t Revision, int Success) {
    if (!Host.Runtime || !Revision || Revision!=Host.InFlight) return 0;
    if (!Success) {Fail();return 1;}
    Host.Applied=Revision; Host.InFlight=0; return 1;
}
extern "C" int Lui_BrowserActivate(int Id, uint32_t Revision) {
    try {
        if (!Admitted(Id,Revision) || Host.Nodes.at(Id)!="TextButton") return 0;
        return Lui_Activate(Host.Runtime,Id);
    } catch (...) {Fail();return 0;}
}
extern "C" int Lui_BrowserEvent(int Id, uint32_t Revision, const char* Input) {
    try {
        if(!Host.Runtime || !Input || std::strlen(Input)>128*1024 || !Host.Nodes.count(Id)) return 0;
        auto Event=Lui::Project::Internal::ParseJson(Input);
        const auto Kind=Event.at("Kind").get<std::string>();
        // Image completion is terminal work and is not gated on page input frames.
        if(Kind=="Image") return Lui_ImageCompleted(Host.Runtime,Id,WordValue(Event.at("Token")),Event.at("Success").get<bool>());
        // Release/cancel notifications can retire existing pressed state while a
        // newer frame is in flight. They cannot start an interaction or edit.
        const bool Terminal=(Kind=="Focus" && !Event.at("Value").get<bool>()) ||
            (Kind=="Key" && Event.at("Phase")==2) || (Kind=="Pointer" && Event.at("Phase")==3);
        if(!Admitted(Id,Revision) && !Terminal) {if(Host.Nodes.at(Id)=="TextBox") Host.TextStates.erase(Id); return 0;}
        if(Kind=="Text" || Kind=="Composition") {
            uint64_t Current=0;
            if(!Lui_GetTextInputRevision(Host.Runtime,Id,&Current) || Current!=WordValue(Event.at("InputRevision"))) {Host.TextStates.erase(Id);return 0;}
            const auto Text=Event.at("Text").get<std::string>();
            if(Text.find('\0')!=std::string::npos || !Lui::Project::Internal::Utf8(Text)) return 0;
            if(Kind=="Text") {
                const int Result=Lui_TextInput(Host.Runtime,Id,Text.c_str(),Event.at("Start").get<int>(),Event.at("Length").get<int>());
                Host.TextStates.erase(Id); return Result;
            }
            return Lui_TextComposition(Host.Runtime,Id,Event.at("Phase").get<int>(),Text.c_str());
        }
        if(Kind=="Focus") return Lui_FocusChanged(Host.Runtime,Id,Event.at("Value").get<bool>());
        if(Kind=="Hover") return Lui_HoverChanged(Host.Runtime,Id,Event.at("Value").get<bool>());
        if(Kind=="Checked") return Lui_CheckedChanged(Host.Runtime,Id,Event.at("Value").get<bool>());
        if(Kind=="Value") return Lui_ValueChanged(Host.Runtime,Id,Event.at("Value").get<double>());
        if(Kind=="Scroll") return Lui_ScrollInput(Host.Runtime,Id,Event.at("X").get<double>(),Event.at("Y").get<double>());
        if(Kind=="Key") return Lui_KeyInput(Host.Runtime,Id,Event.at("Phase").get<int>(),Event.at("Key").get_ref<const std::string&>().c_str());
        if(Kind=="Pointer") {
            const int Phase=Event.at("Phase").get<int>();
            if(Phase==0) {
                const Node* Target=Host.Runtime->Nodes.at(Id).get();
                while(Target->ParentId) Target=Host.Runtime->Nodes.at(Target->ParentId).get();
                int Hit=0;
                if(Target->ClassName!="Window" || !Lui_HitTest(Host.Runtime,Target->Id,Event.at("X").get<double>(),Event.at("Y").get<double>(),&Hit) || Hit!=Id) return 0;
            }
            const auto& Bounds=Host.Runtime->Nodes.at(Id)->Bounds;
            return Lui_PointerInput(Host.Runtime,Id,Phase,Event.at("Device").get<int>(),Event.at("PointerId").get<unsigned>(),
                Event.at("X").get<double>()-Bounds.X,Event.at("Y").get<double>()-Bounds.Y);
        }
        return 0;
    } catch(...) {Fail();return 0;}
}
extern "C" int Lui_BrowserViewport(int Id,double Width,double Height) { return Host.Runtime?Lui_WindowResized(Host.Runtime,Id,Width,Height):0; }
extern "C" int Lui_BrowserTheme(const char* Theme) { return Host.Runtime?Lui_SystemThemeChanged(Host.Runtime,Theme):0; }
extern "C" int Lui_BrowserPump() {
    try { if (!Host.Runtime || Host.Failed) return 0; Lui_Pump(Host.Runtime); return !Host.Failed; }
    catch (...) {Fail();return 0;}
}
extern "C" void Lui_BrowserRetire() { Retire(); }
extern "C" uint32_t Lui_BrowserMemory() { return static_cast<uint32_t>(emscripten_get_heap_size()); }
