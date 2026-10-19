#include "../internal/State.h"
#include "../internal/Diagnostics.h"
#include "../internal/Dispatch.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <set>
namespace {
std::string Bounded(std::string Value) {
    if(Value.size()>4096) {size_t End=4080; while(End && (static_cast<unsigned char>(Value[End])&0xc0)==0x80) --End; Value.resize(End); Value+="[Truncated]";}
    return Value;
}
}
std::string ProjectError(const LuiRuntime* Runtime,const std::string& Message) {
    if(!Runtime || !Runtime->DiagnosticMode || Message.empty()) return Message;
    static const std::set<std::string> Codes{"InvalidInput","InvalidOptions","InvalidJson","InvalidManifest","InvalidDeclaration","InvalidPath","InvalidLock",
        "InvalidSchema","UnsupportedFeature","PolicyNotSupported","CapabilityDenied","RegistrationMismatch","BinaryMismatch","IdentityMismatch",
        "CapabilityMismatch","UnsupportedPlatform","Canceled","LimitExceeded","CompileFailed","MissingFile","MissingModule","ForbiddenImport",
        "EntryImportDenied","Cycle","InvalidResult","LoadFailed","InitializationEffect","YieldNotAllowed","ProjectNotConfigured","AssetNotRegistered",
        "PolicyNotConfigured","OutputExists","PackageMismatch","FileChanged","DuplicateIdentity","LinkedPath","CaseMismatch","NotRegularFile","IoError"};
    for(const auto* System:{"Modules","Project","Sandbox","Network","Tls","Platform","Scheduler","Backend","Preview","Extension"})
        for(const auto& Code:Codes) if(Message==std::string("[LUI:")+System+"] "+Code) return Message;
    return "[LUI:Diagnostics] OperationFailed";
}
void DeliverDiagnostic(LuiRuntime* Runtime,const char* Level,const std::string& Message,bool Explicit) {
    try {
        std::string Safe=Message;
        if(Runtime->DiagnosticMode) {
            Safe=Explicit ? (Runtime->DiagnosticMode==LUI_DIAGNOSTICS_RESTRICTED ? "[LUI:Diagnostics] ApplicationMessageSuppressed" : Bounded(Message)) :
                (std::string(Level)=="Error" ? ProjectError(Runtime,Message) : "[LUI:Diagnostics] FrameworkEvent");
            while(!Runtime->DiagnosticRecords.empty() && (Runtime->DiagnosticRecords.size()>=256 || Runtime->DiagnosticBytes+Safe.size()>512*1024)) {
                Runtime->DiagnosticBytes-=Runtime->DiagnosticRecords.front().size(); Runtime->DiagnosticRecords.pop_front(); ++Runtime->DroppedDiagnostics;
            }
            Runtime->DiagnosticRecords.push_back(Safe); Runtime->DiagnosticBytes+=Safe.size();
        }
        if(Runtime->LogCallback) Runtime->LogCallback(Runtime->LogContext,Level,Safe.c_str());
        else std::fprintf(std::string(Level)=="Error"?stderr:stdout,"[LUI:%s] %s\n",Level,Safe.c_str());
    } catch(...) {}
}
void ClassifyNode(LuiRuntime* Runtime,Node* Value) {
    if(!Value || Value->Destroyed) return;
    Runtime->PreviewSnapshotJson.clear(); Runtime->DiagnosticSnapshot.clear(); Runtime->DiagnosticRecords.clear(); Runtime->DiagnosticBytes=0;
    Runtime->InspectionSnapshot.clear();
    std::vector<Node*> Pending{Value};
    while(!Pending.empty()) {
        auto* Current=Pending.back(); Pending.pop_back(); Current->EffectiveSensitive=true;
        for(int Id:Current->Children) {auto Found=Runtime->Nodes.find(Id); if(Found!=Runtime->Nodes.end() && !Found->second->Destroyed) Pending.push_back(Found->second.get());}
    }
}
extern "C" LUI_API const char* LUI_CALL Lui_GetDiagnosticsJson(LuiRuntime* Runtime) {
    if(!CheckOwner(Runtime) || !Runtime->DiagnosticMode) return nullptr;
    nlohmann::json Records=nlohmann::json::array(); for(const auto& Value:Runtime->DiagnosticRecords) Records.push_back(Value);
    Runtime->DiagnosticSnapshot=nlohmann::json{{"version",1},{"mode",Runtime->DiagnosticMode},{"inspectionGranted",Runtime->InspectionGranted},
        {"dropped",Runtime->DroppedDiagnostics},{"records",Records}}.dump(-1,' ',false,nlohmann::json::error_handler_t::replace);
    return Runtime->DiagnosticSnapshot.c_str();
}
