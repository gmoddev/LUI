#include "ProjectCheck.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace Fs=std::filesystem;
using Json=nlohmann::json;
static void Require(bool Value,const char* Label) { if(!Value) throw std::runtime_error(Label); }
static void Write(const Fs::path& Path,const std::string& Bytes) {
    Fs::create_directories(Path.parent_path()); std::ofstream File(Path,std::ios::binary);
    File.write(Bytes.data(),Bytes.size()); Require(bool(File),"fixture write failed");
}
static std::shared_ptr<const Lui::Project::Snapshot> Capture(const Fs::path& Root,const std::map<std::string,std::string>& Files,
    const std::string& Mode="Development") {
    Json Names=Json::array(); for(const auto& Pair:Files) { Names.push_back(Pair.first); Write(Root/"Src"/Pair.first,Pair.second); }
    Write(Root/"lui.json",Json{{"SchemaVersion",2},{"Script","Src/Main.luau"},{"Capabilities",Json::array()},
        {"Extensions",Json::array()},{"Assets",Json::array()},{"ExtensionDeclarations",Json::array()},{"Diagnostics",{{"Mode",Mode}}},
        {"Modules",{{"Root","Src"},{"Files",Names},{"Requires",Json::array()},{"Dependencies",Json::array()}}}}.dump());
    std::string Error; auto Snapshot=Lui::Project::Prepare(Root/"lui.json",{},Error);
    Require(bool(Snapshot),Error.c_str()); return Snapshot;
}
static auto Check(const Lui::Project::Snapshot& Snapshot,const std::string& Definitions) {
    Lui::TypeCheck::Options Options; Options.DevelopmentDiagnostics=true;
    std::string Error; auto Result=Lui::TypeCheck::Check(Snapshot,Definitions,Options,Error);
    Require(bool(Result),Error.c_str()); return Result;
}
static void Cases(const Fs::path& Root,const std::string& Definitions) {
    const std::map<std::string,std::string> Files{
        {"Main.luau","local Data=require('./Data'); local N:number=Data.Count; local F=require('./Sub/Factory'); local W:Window=F(); assert(W); local Types=require('./Types'); local Value:Types.Record={Count=9}"},
        {"Data.luau","return {Count=17, Name='Typed'}"},
        {"Sub/Factory.luau","return function():Window local Data=require('../Data'); local N:number=Data.Count; return Instance.new('Window') end"},
        {"Unused.luau","error('DO_NOT_EXECUTE_PRIVATE_CANARY'); return false"}
        ,{"Types.luau","export type Record={Count:number}; return {}"}
    };
    auto Snapshot=Capture(Root/"Good",Files); auto Result=Check(*Snapshot,Definitions);
    for(const auto& Issue:Result->Diagnostics) std::fprintf(stderr,"[LUI:ProjectTypes] %s: %s\n",Issue.Source.c_str(),Issue.Message.c_str());
    Require(Result->Diagnostics.empty(),"literal imports did not infer table/factory export types");
    auto Output=Json::parse(Lui::TypeCheck::Serialize(*Snapshot,*Result,12));
    Require(Output["version"]==2 && Output["generation"]==12 && Output["sources"].size()==5 &&
        Output["entry"]=="app:Main.luau" && Output["redacted"]==false,"project protocol metadata absent");
    Require(Result->SourceDigest==Snapshot->GetDigest() && Result->Digest!=Result->SourceDigest,"analysis profile digest missing");
    Write(Root/"Good/Src/Data.luau","return 'CHANGED_DISK' ");
    auto Frozen=Check(*Snapshot,Definitions); Require(Frozen->Digest==Result->Digest && Frozen->Diagnostics.empty(),"analysis reread captured files");
    auto Bad=Files; Bad["Main.luau"]="local Data=require('./Data'); local Text:string=Data.Count";
    auto Errors=Check(*Capture(Root/"Bad",Bad),Definitions);
    Require(!Errors->Diagnostics.empty() && Errors->Diagnostics[0].Source=="app:Main.luau","wrong imported field type passed");
    auto Unused=Files; Unused["Unused.luau"]="local N:number='unused failure'; return false";
    auto UnusedResult=Check(*Capture(Root/"Unused",Unused),Definitions);
    Require(!UnusedResult->Diagnostics.empty() && UnusedResult->Diagnostics[0].Source=="app:Unused.luau","unused module was not analyzed");
    auto Dynamic=Files; Dynamic["Main.luau"]="local Path:string='./Data'; local Data=require(Path); local N:number=Data.Count";
    auto DynamicResult=Check(*Capture(Root/"Dynamic",Dynamic),Definitions);
    Require(!DynamicResult->Diagnostics.empty(),"dynamic import did not remain unknown");
    Dynamic["Main.luau"]="local Path:string='./Data'; local Data=require(Path); if type(Data)=='table' then local Value:unknown=Data end";
    Require(Check(*Capture(Root/"Refined",Dynamic),Definitions)->Diagnostics.empty(),"dynamic unknown refinement failed");
    auto Shadow=Capture(Root/"Shadow",{{"Main.luau","local function F(require:(string)->number) local N:number=require('./Missing'); return N end; assert(F(function() return 7 end)==7)"}});
    Require(Check(*Shadow,Definitions)->Diagnostics.empty(),"lexical parameter shadow interpreted as builtin import");
    auto Local=Capture(Root/"Local",{{"Main.luau","local require=function(Path:string):number return 4 end; local N:number=require('./Missing')"}});
    Require(Check(*Local,Definitions)->Diagnostics.empty(),"local require shadow interpreted as builtin import");
    auto Alias=Capture(Root/"Alias",{{"Main.luau","local Data=require('@Kit/Data'); local N:number=Data.Count"}});
    auto Manifest=Json::parse(std::ifstream(Root/"Alias/lui.json"));
    Manifest["Modules"]["Requires"]={"Kit"}; Manifest["Modules"]["Lock"]="Modules.lock.json";
    Manifest["Modules"]["Dependencies"]=Json::array({{{"Alias","Kit"},{"Root","Vendor/Kit"},
        {"Files",{"Data.luau"}},{"Requires",Json::array()}}});
    const std::string Vendor="return {Count=18}";
    Write(Root/"Alias/Vendor/Kit/Data.luau",Vendor); Write(Root/"Alias/lui.json",Manifest.dump());
    Write(Root/"Alias/Modules.lock.json",Json{{"SchemaVersion",1},{"Packages",Json::array({{{"Alias","Kit"},
        {"Provenance",{{"Kind","LocalVendor"},{"Id","Fixture-1"}}},{"Files",Json::array({{{"Path","Data.luau"},{"Sha256",Lui::Project::Hash(Vendor)}}})}}})}}.dump());
    std::string AliasError; Alias=Lui::Project::Prepare(Root/"Alias/lui.json",{},AliasError);
    Require(bool(Alias) && Check(*Alias,Definitions)->Diagnostics.empty(),"locked alias export did not infer");
    auto Missing=Check(*Capture(Root/"Missing",{{"Main.luau","local Value=require('./Missing')"}}),Definitions);
    bool Found=false; for(const auto& Issue:Missing->Diagnostics) Found|=Issue.Code=="MissingModule" && Issue.Source=="app:Main.luau";
    Require(Found,"missing import lost source identity/code");
    auto Cycle=Check(*Capture(Root/"Cycle",{{"Main.luau","return"},{"A.luau","return function() return require('./B') end"},
        {"B.luau","return function() return require('./A') end"}}),Definitions);
    Require(Cycle->Diagnostics.size()==1 && Cycle->Diagnostics[0].Code=="Cycle","deferred literal cycle was not rejected");
    auto Masked=Check(*Capture(Root/"MaskedCycle",{{"Main.luau","local Missing=require('./Missing')"},
        {"A.luau","local Missing=require('./Missing'); return function() return require('./B') end"},
        {"B.luau","return function() return require('./A') end"}}),Definitions);
    Require(Masked->Diagnostics.size()==1 && Masked->Diagnostics[0].Code=="Cycle","missing import masked a known cycle");
    auto BrokenCycle=Check(*Capture(Root/"BrokenCycle",{{"Main.luau","return"},{"0Broken.luau","local = broken"},
        {"A.luau","return function() return require('./B') end"},{"B.luau","return function() return require('./A') end"}}),Definitions);
    Require(BrokenCycle->Diagnostics.size()==1 && BrokenCycle->Diagnostics[0].Code=="Cycle","syntax error masked a known cycle");
    auto Syntax=Check(*Capture(Root/"Syntax",{{"Main.luau","return"},{"Broken.luau","local = invalid"}}),Definitions);
    Found=false; for(const auto& Issue:Syntax->Diagnostics) Found|=Issue.Kind=="syntax" && Issue.Source=="app:Broken.luau";
    Require(Found,"unused syntax failure lost identity");
    for(const auto& Text:{"return nil","return","return 1,2","return if math.random()>0.5 then 1 else nil"}) {
        auto Invalid=Check(*Capture(Root/(std::string("Result")+std::to_string(std::strlen(Text))),{{"Main.luau","return"},{"Invalid.luau",Text}}),Definitions);
        Found=false; for(const auto& Issue:Invalid->Diagnostics) Found|=Issue.Code=="InvalidResult" || Issue.Kind=="type";
        Require(Found,"invalid module return accepted");
    }
    const std::string Unicode="local Emoji='\xf0\x9f\x98\x80'; local N:number='wrong'";
    auto Utf8=Check(*Capture(Root/"Unicode",{{"Main.luau",Unicode}}),Definitions);
    Require(Utf8->Diagnostics.size()==1 && Utf8->Diagnostics[0].Start.Column==Unicode.find("'wrong'")-2,"UTF-16 project range incorrect");
    auto Restricted=Capture(Root/"Restricted",{{"Main.luau","local PRIVATE_NAME:number='SECRET_PAYLOAD'"}},"Restricted");
    Lui::TypeCheck::Options Options; Options.DevelopmentDiagnostics=true; std::string Error;
    Require(!Lui::TypeCheck::Check(*Restricted,Definitions,Options,Error) && Error=="[LUI:TypeCheck] PolicyNotSupported","restricted source diagnostics emitted");
    Options={}; Require(!Lui::TypeCheck::Check(*Snapshot,Definitions,Options,Error),"missing owner opt-in accepted");
    Options.DevelopmentDiagnostics=true; Options.Canceled=[] { return true; };
    Require(!Lui::TypeCheck::Check(*Snapshot,Definitions,Options,Error) && Error=="[LUI:TypeCheck] Canceled","cancellation ignored");
    Options.Canceled={}; Require(!Lui::TypeCheck::Check(*Snapshot,"declare Broken:",Options,Error),"invalid definitions accepted");
    Options.ModuleSeconds=0.000001;
    Require(!Lui::TypeCheck::Check(*Snapshot,Definitions,Options,Error) && Error=="[LUI:TypeCheck] LimitExceeded","module analysis budget ignored");
    Options.ModuleSeconds=10;
    auto TypeFunction=Capture(Root/"TypeFunction",{{"Main.luau","type function Private() error('TYPE_FUNCTION_MUST_NOT_EXECUTE') end; type X=Private<>"}});
    Require(!Lui::TypeCheck::Check(*TypeFunction,Definitions,Options,Error) && Error=="[LUI:TypeCheck] UnsupportedFeature",
        "checker admitted user type-function execution");
    auto NoCheck=Capture(Root/"NoCheck",{{"Main.luau","--!nocheck\nlocal N:number='ignored'"}});
    Require(!Lui::TypeCheck::Check(*NoCheck,Definitions,Options,Error) && Error=="[LUI:TypeCheck] UnsupportedFeature",
        "nocheck skipped declared source analysis");
    Require(!Lui::TypeCheck::Check(*Snapshot,"export type function Unsafe() error('PRIVATE_DEFINITION') end",Options,Error) &&
        Error=="[LUI:TypeCheck] UnsupportedFeature","definition type-function execution accepted");
    auto Many=Files; Many["Unused.luau"]="";
    for(int Index=0;Index<300;++Index) Many["Unused.luau"]+="local N"+std::to_string(Index)+":number='bad'\n";
    Many["Unused.luau"]+="return false";
    auto Limited=Check(*Capture(Root/"Limits",Many),Definitions);
    Require(Limited->Diagnostics.size()==256 && Limited->Truncated,"project diagnostic ceiling not enforced");
}
int main() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    const auto Root=Fs::temp_directory_path()/("LuiProjectTypes-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fs::create_directory(Root);
    try {
        std::ifstream File(LUI_TYPES_PATH,std::ios::binary); Require(bool(File),"definitions missing");
        const std::string Definitions{std::istreambuf_iterator<char>{File},std::istreambuf_iterator<char>{}};
        Cases(Root,Definitions); Fs::remove_all(Root);
        std::puts("[LUI:ProjectTypes] Literal exports, dynamic unknown, all-source analysis, protocol and policy passed"); return 0;
    } catch(const std::exception& Error) { std::fprintf(stderr,"[LUI:ProjectTypes] %s\n",Error.what()); return 1; }
}
