#include "Declarations.h"
#include "Project.h"
#include "Json.h"
#include "../reflection/Schema.h"
#include "../../native/abi/LuiExtension.h"
#include "../../native/abi/LuiApplicationServices.h"
#include <algorithm>
#include <set>

namespace Lui::Project {
using Json = nlohmann::json;
using namespace Internal;
namespace {
bool Identifier(std::string_view Value) {
    if (Value.empty() || Value.size() > 64 || !((Value[0] >= 'A' && Value[0] <= 'Z') ||
        (Value[0] >= 'a' && Value[0] <= 'z') || Value[0] == '_')) return false;
    for (char Byte : Value) if (!((Byte >= 'A' && Byte <= 'Z') || (Byte >= 'a' && Byte <= 'z') ||
        (Byte >= '0' && Byte <= '9') || Byte == '_')) return false;
    const std::set<std::string_view> Keywords{"and","break","do","else","elseif","end","false","for","function","if","in","local","nil","not","or","repeat","return","then","true","until","while"};
    return !Keywords.count(Value);
}
std::string Name(const Json& Value) {
    if (!Value.is_string()) Fail("InvalidDeclaration");
    auto Text = Value.get<std::string>(); if (!Identifier(Text)) Fail("InvalidDeclaration"); return Text;
}
bool Primitive(std::string_view Value) { return Value == "nil" || Value == "boolean" || Value == "number" || Value == "string"; }
bool Hex(std::string_view Value,size_t Length) {
    return Value.size()==Length && std::all_of(Value.begin(),Value.end(),[](char Byte) {return (Byte>='0' && Byte<='9') || (Byte>='a' && Byte<='f');});
}
uint64_t Capabilities(const Json& Names) {
    if(!Names.is_array() || Names.empty() || Names.size()>10) Fail("InvalidDeclaration");
    const std::map<std::string,uint64_t> Known{{"NativeExtensions",LUI_CAPABILITY_NATIVE_EXTENSIONS},{"HostServices",LUI_CAPABILITY_HOST_SERVICES},
        {"Clipboard",LUI_CAPABILITY_CLIPBOARD},{"Dialogs",LUI_CAPABILITY_DIALOGS},{"network.client",LUI_CAPABILITY_NETWORK_CLIENT},
        {"network.server",LUI_CAPABILITY_NETWORK_SERVER},{"network.raw",LUI_CAPABILITY_NETWORK_RAW},
        {"Storage",LUI_CAPABILITY_STORAGE},{"Notifications",LUI_CAPABILITY_NOTIFICATIONS},{"Tray",LUI_CAPABILITY_TRAY}};
    uint64_t Result=0;
    for(const auto& Item:Names) {
        if(!Item.is_string() || !Known.count(Item.get<std::string>())) Fail("InvalidDeclaration");
        const auto Bit=Known.at(Item.get<std::string>()); if(Result & Bit) Fail("InvalidDeclaration"); Result|=Bit;
    }
    if(!(Result & LUI_CAPABILITY_NATIVE_EXTENSIONS)) Fail("InvalidDeclaration"); return Result;
}
std::string Type(const Json& Value) {
    if (!Value.is_string()) Fail("InvalidDeclaration");
    auto Text = Value.get<std::string>(); if (!Primitive(Text)) Fail("InvalidDeclaration"); return Text;
}
std::vector<std::string> Arguments(const Json& Value) {
    if (!Value.is_array()) Fail("InvalidDeclaration");
    if (Value.size() > 32) Fail("LimitExceeded");
    std::vector<std::string> Result; for (const auto& Item : Value) Result.push_back(Type(Item)); return Result;
}
void Shape(const Json& Object, std::initializer_list<const char*> Names) {
    if (!Object.is_object() || Object.size() != Names.size()) Fail("InvalidDeclaration");
    for (const auto* Name : Names) if (!Object.contains(Name)) Fail("InvalidDeclaration");
}
bool Reserved(const std::string& Name) {
    if (LuiSchema::FindService(Name) || LuiSchema::FindClass(Name)) return true;
    const auto Schema = Json::parse(LuiSchema::GetJson());
    for (const auto* Group : {"objects","objectProperties","objectMethods","objectSignals"})
        if (Schema.contains(Group)) for (const auto& Item : Schema.at(Group))
            if ((Item.contains("name") && Item.at("name") == Name) || (Item.contains("owner") && Item.at("owner") == Name)) return true;
    return Name == "app" || Name == "App" || Name == "Host" || Name == "Signal" || Name == "Connection" ||
        Name == "Vector2" || Name == "UDim" || Name == "UDim2" || Name.substr(0, 11) == "LuiDeclared";
}
std::string_view Trim(std::string_view Text) {
    while (!Text.empty() && (Text.front() == ' ' || Text.front() == '\t' || Text.front() == '\r' || Text.front() == '\n')) Text.remove_prefix(1);
    while (!Text.empty() && (Text.back() == ' ' || Text.back() == '\t' || Text.back() == '\r' || Text.back() == '\n')) Text.remove_suffix(1);
    return Text;
}
std::vector<std::string_view> Split(std::string_view Text) {
    std::vector<std::string_view> Parts; Text = Trim(Text); if (Text.empty()) return Parts;
    while (true) {
        const auto Comma = Text.find(','); auto Part = Trim(Text.substr(0, Comma));
        if (Part.empty() || Parts.size() == 33) Fail("InvalidDeclaration");
        Parts.push_back(Part); if (Comma == std::string_view::npos) break; Text.remove_prefix(Comma + 1);
    }
    return Parts;
}
std::string Parameters(const std::vector<std::string>& Types) {
    std::string Result;
    for (size_t Index = 0; Index < Types.size(); ++Index) {
        if (Index) Result += ", "; Result += "Arg" + std::to_string(Index + 1) + ": " + Types[Index];
    }
    return Result;
}
}
void AddDeclaration(DeclarationSet& Set, const std::string& Path, std::string Bytes) {
    if (Set.Files.size() >= 16 || Bytes.size() > 256 * 1024) Fail("LimitExceeded");
    size_t Total = Bytes.size(); for (const auto& Pair : Set.Files) Total += Pair.second.Bytes.size();
    if (Total > 1024 * 1024) Fail("LimitExceeded");
    auto Input = ParseJson(Bytes);
    if(!Input.is_object() || !Input.contains("Provider") || !Input.contains("SchemaVersion") || !Input.contains("AbiVersion")) Fail("InvalidDeclaration");
    if(!Input.at("Provider").is_string()) Fail("InvalidDeclaration");
    const bool Native=Input.at("Provider")=="Native";
    if(Native && Input.at("SchemaVersion")==1) Fail("UnsupportedFeature"); // Never reinterpret the earlier unsupported profile.
    if(!Native && Input.at("Provider")!="Host") Fail("UnsupportedFeature");
    if(Native) Shape(Input,{"SchemaVersion","AbiVersion","Provider","Extension","Services"});
    else Shape(Input, {"SchemaVersion","AbiVersion","Provider","Services"});
    const bool Owned = Input.at("SchemaVersion") == 3 && Input.at("AbiVersion") == 2;
    if ((!Owned && (Input.at("SchemaVersion") != (Native?2:1) || Input.at("AbiVersion") != 1)) || !Input.at("SchemaVersion").is_number_integer() ||
        !Input.at("AbiVersion").is_number_integer()) Fail("InvalidDeclaration");
    std::string Provider="Host";
    if(Native) {
        const auto& Extension=Input.at("Extension"); Shape(Extension,{"Name","Version","RequiredCapabilities","Bindings","Provenance"});
        Provider=Name(Extension.at("Name"));
        if(Reserved(Provider) || Set.NativeProviders.count(Provider)) Fail("InvalidDeclaration");
        for(const auto& Pair:Set.NativeProviders) if(Fold(Pair.first)==Fold(Provider)) Fail("DuplicateIdentity");
        if(Set.NativeProviders.size()>=16) Fail("LimitExceeded");
        NativeDeclaration Value;
        Value.AbiVersion = Owned ? 2 : 1;
        if(!Extension.at("Version").is_number_unsigned() || Extension.at("Version").get<uint64_t>()==0 ||
            Extension.at("Version").get<uint64_t>()>UINT32_MAX) Fail("InvalidDeclaration");
        Value.Version=Extension.at("Version").get<uint32_t>(); Value.RequiredCapabilities=Capabilities(Extension.at("RequiredCapabilities"));
        if (Owned && !(Value.RequiredCapabilities & LUI_CAPABILITY_HOST_SERVICES)) Fail("InvalidDeclaration");
        const auto& Provenance=Extension.at("Provenance"); Shape(Provenance,{"Kind","Id"});
        if(!Provenance.at("Kind").is_string() || !Provenance.at("Id").is_string()) Fail("InvalidDeclaration");
        Value.ProvenanceKind=Provenance.at("Kind").get<std::string>(); Value.ProvenanceId=Provenance.at("Id").get<std::string>();
        if((Value.ProvenanceKind!="LocalVendor" && Value.ProvenanceKind!="Git") || Value.ProvenanceId.empty() || Value.ProvenanceId.size()>128) Fail("InvalidDeclaration");
        for(unsigned char Byte:Value.ProvenanceId) if(Byte<32 || Byte>126) Fail("InvalidDeclaration");
        if(Value.ProvenanceKind=="Git" && !Hex(Value.ProvenanceId,40) && !Hex(Value.ProvenanceId,64)) Fail("InvalidDeclaration");
        const auto& Bindings=Extension.at("Bindings"); if(!Bindings.is_array() || Bindings.empty() || Bindings.size()>2) Fail("InvalidDeclaration");
        for(const auto& Binding:Bindings) {
            Shape(Binding,{"Platform","Path","Sha256"});
            if(!Binding.at("Platform").is_string() || !Binding.at("Path").is_string() || !Binding.at("Sha256").is_string()) Fail("InvalidDeclaration");
            const auto Platform=Binding.at("Platform").get<std::string>(), File=Binding.at("Path").get<std::string>(), Hash=Binding.at("Sha256").get<std::string>();
            if(Platform!="WindowsX64" && Platform!="LinuxX64") Fail("UnsupportedPlatform");
            const auto Suffix=Platform=="WindowsX64"?std::string(".dll"):std::string(".so");
            if(!PathValid(File) || File.size()<=Suffix.size() || File.substr(File.size()-Suffix.size())!=Suffix) Fail("InvalidPath");
            if(!Hex(Hash,64) || !Value.Bindings.emplace(Platform,BinaryBinding{File,Hash}).second) Fail("InvalidDeclaration");
        }
        Set.NativeProviders.emplace(Provider,std::move(Value));
    }
    const auto& Services = Input.at("Services");
    if (!Services.is_array() || Services.empty()) Fail("InvalidDeclaration");
    if (Services.size() > 64) Fail("LimitExceeded");
    for (const auto& Item : Services) {
        if (Owned) Shape(Item, {"Name","Methods","ResourceKindCount"});
        else Shape(Item, {"Name","Methods","Signals"});
        auto ServiceName = Name(Item.at("Name"));
        if (Reserved(ServiceName) || Set.Services.count(ServiceName)) Fail("InvalidDeclaration");
        if (Set.Services.size() >= 64) Fail("LimitExceeded");
        ServiceDeclaration Service; std::set<std::string> Members;
        if (Owned) {
            if (Native && (Services.size() != 1 || ServiceName != Provider)) Fail("InvalidDeclaration");
            const auto Number = [](const Json& Value, uint32_t Maximum) -> uint32_t {
                if (!Value.is_number_unsigned() || Value.get<uint64_t>() > Maximum) Fail("InvalidDeclaration");
                return Value.get<uint32_t>();
            };
            Service.ResourceKindCount = Number(Item.at("ResourceKindCount"), 64);
            const auto& Methods = Item.at("Methods");
            if (!Methods.is_array() || Methods.empty() || Methods.size() > 32) Fail("InvalidDeclaration");
            for (const auto& Method : Methods) {
                Shape(Method, {"Name","Profile","MaxInputBytes","MaxResultBytes","InputKind","ResultKind","DeadlineMilliseconds"});
                auto Member = Name(Method.at("Name"));
                if (Method.at("Profile") != "OwnedAsync" || !Members.insert(Member).second) Fail("InvalidDeclaration");
                OwnedMethodDeclaration Value{Number(Method.at("MaxInputBytes"), 8*1024*1024), Number(Method.at("MaxResultBytes"), 8*1024*1024),
                    Number(Method.at("InputKind"), Service.ResourceKindCount), Number(Method.at("ResultKind"), Service.ResourceKindCount),
                    Number(Method.at("DeadlineMilliseconds"), 300000)};
                if (!Value.DeadlineMilliseconds) Fail("InvalidDeclaration");
                Service.OwnedMethods.emplace(Member, Value);
            }
            Set.ServiceProviders.emplace(ServiceName, Provider); Set.Services.emplace(ServiceName, std::move(Service));
            continue;
        }
        const auto& Methods = Item.at("Methods"); const auto& Signals = Item.at("Signals");
        if (!Methods.is_array() || !Signals.is_array() || (Methods.empty() && Signals.empty())) Fail("InvalidDeclaration");
        if (Methods.size() + Signals.size() > 256) Fail("LimitExceeded");
        for (const auto& Method : Methods) {
            Shape(Method, {"Name","Arguments","Returns"}); auto Member = Name(Method.at("Name"));
            if (!Members.insert(Member).second) Fail("InvalidDeclaration");
            Service.Methods.emplace(Member, MethodDeclaration{Arguments(Method.at("Arguments")),Type(Method.at("Returns"))});
        }
        for (const auto& Signal : Signals) {
            Shape(Signal, {"Name","Arguments"}); auto Member = Name(Signal.at("Name"));
            if (!Members.insert(Member).second) Fail("InvalidDeclaration");
            Service.Signals.emplace(Member, Arguments(Signal.at("Arguments")));
        }
        Set.ServiceProviders.emplace(ServiceName,Provider);
        Set.Services.emplace(std::move(ServiceName), std::move(Service));
    }
    Set.Files.emplace(Path, DeclarationFile{Bytes,Hash(Bytes)});
}
void FinishDeclarations(DeclarationSet& Set) {
    if (Set.Services.size() > 64) Fail("LimitExceeded");
    size_t Members = 0; Json Services = Json::array(); bool Owned = false;
    for (const auto& Pair : Set.Services) {
        Json Methods = Json::array(), Signals = Json::array();
        if (!Pair.second.OwnedMethods.empty()) {
            if (!Pair.second.Methods.empty() || !Pair.second.Signals.empty()) Fail("InvalidDeclaration");
            for (const auto& Method : Pair.second.OwnedMethods) {
                const auto& Value = Method.second;
                Methods.push_back({{"Name",Method.first},{"Profile","OwnedAsync"},{"MaxInputBytes",Value.MaxInputBytes},
                    {"MaxResultBytes",Value.MaxResultBytes},{"InputKind",Value.InputKind},{"ResultKind",Value.ResultKind},
                    {"DeadlineMilliseconds",Value.DeadlineMilliseconds}});
            }
            Members += Methods.size(); if (Members > 256) Fail("LimitExceeded");
            Services.push_back({{"Name",Pair.first},{"Methods",Methods},{"ResourceKindCount",Pair.second.ResourceKindCount}});
            Owned = true; continue;
        }
        for (const auto& Method : Pair.second.Methods)
            Methods.push_back({{"Name",Method.first},{"Arguments",Method.second.Arguments},{"Returns",Method.second.Returns}});
        for (const auto& Signal : Pair.second.Signals) Signals.push_back({{"Name",Signal.first},{"Arguments",Signal.second}});
        Members += Methods.size() + Signals.size(); if (Members > 256) Fail("LimitExceeded");
        Services.push_back({{"Name",Pair.first},{"Methods",Methods},{"Signals",Signals}});
    }
    if(Set.NativeProviders.empty()) Set.Descriptor = Json{{"SchemaVersion",Owned?3:1},{"AbiVersion",Owned?2:1},{"Provider","Host"},{"Services",Services}}.dump();
    else {
        Json Providers=Json::array(), Host=Json::array();
        for(const auto& Service:Services) if(Set.ServiceProviders.at(Service.at("Name").get<std::string>())=="Host") Host.push_back(Service);
        if(!Host.empty()) {
            bool HostOwned = false;
            for (const auto& Service : Host) if (Service.contains("ResourceKindCount")) HostOwned = true;
            Providers.push_back({{"Provider","Host"},{"AbiVersion",HostOwned?2:1},{"Services",Host}});
        }
        for(const auto& Pair:Set.NativeProviders) {
            Json Owned=Json::array();
            for(const auto& Service:Services) if(Set.ServiceProviders.at(Service.at("Name").get<std::string>())==Pair.first) Owned.push_back(Service);
            Providers.push_back({{"Provider","Native"},{"Name",Pair.first},{"AbiVersion",Pair.second.AbiVersion},{"Version",Pair.second.Version},
                {"RequiredCapabilities",Pair.second.RequiredCapabilities},{"Services",Owned}});
        }
        Set.Descriptor=Json{{"SchemaVersion",Owned?3:2},{"Providers",Providers}}.dump();
    }
    Set.Digest = Hash(Set.Descriptor);
}
bool MatchNative(const DeclarationSet& Set,const NativeObservation& Observed,std::string& Error) {
    Error.clear(); auto Failed=[&](const char* Code) {Error=std::string("[LUI:Project] ")+Code; return false;};
    try {
        auto Found=Set.NativeProviders.find(Observed.Name);
        if(Found==Set.NativeProviders.end() || Observed.AbiVersion!=Found->second.AbiVersion || Observed.Version!=Found->second.Version) return Failed("IdentityMismatch");
        if(Observed.RequiredCapabilities!=Found->second.RequiredCapabilities) return Failed("CapabilityMismatch");
        auto Binding=Found->second.Bindings.find(Observed.Platform);
        if(Binding==Found->second.Bindings.end()) return Failed("UnsupportedPlatform");
        if(Observed.BinaryHash!=Binding->second.Hash) return Failed("BinaryMismatch");
        if(Observed.Services.size()>64) return Failed("RegistrationMismatch");
        size_t Members=0;
        for(const auto& Pair:Observed.Services) {
            if (!Pair.second.OwnedMethods.empty()) {
                if (Observed.AbiVersion != 2 || !Identifier(Pair.first) || Pair.second.OwnedMethods.size() > 32 ||
                    !Pair.second.Methods.empty() || !Pair.second.Signals.empty()) return Failed("RegistrationMismatch");
                Members += Pair.second.OwnedMethods.size(); if (Members > 256) return Failed("RegistrationMismatch");
                for (const auto& Method : Pair.second.OwnedMethods) if (!Identifier(Method.first)) return Failed("RegistrationMismatch");
                continue;
            }
            if(!Identifier(Pair.first) || Pair.second.Methods.size()>256 || Pair.second.Signals.size()>256) return Failed("RegistrationMismatch");
            Members+=Pair.second.Methods.size()+Pair.second.Signals.size(); if(Members>256) return Failed("RegistrationMismatch");
            for(const auto& Method:Pair.second.Methods) {
                if(!Identifier(Method.first) || Method.second.Arguments.size()>32 || !Primitive(Method.second.Returns)) return Failed("RegistrationMismatch");
                for(const auto& Type:Method.second.Arguments) if(!Primitive(Type)) return Failed("RegistrationMismatch");
            }
            for(const auto& Signal:Pair.second.Signals) {
                if(!Identifier(Signal.first) || Signal.second.size()>32) return Failed("RegistrationMismatch");
                for(const auto& Type:Signal.second) if(!Primitive(Type)) return Failed("RegistrationMismatch");
            }
        }
        DeclarationSet Expected, Actual; Actual.Services=Observed.Services;
        for(const auto& Pair:Set.Services) if(Set.ServiceProviders.at(Pair.first)==Observed.Name) Expected.Services.emplace(Pair);
        FinishDeclarations(Expected); FinishDeclarations(Actual);
        if(Expected.Descriptor!=Actual.Descriptor) return Failed("RegistrationMismatch"); return true;
    } catch(...) {return Failed("RegistrationMismatch");}
}
MethodDeclaration ParseMethod(const std::string& Service, std::string_view Text) {
    if (Text.size() > 256) Fail("InvalidDeclaration"); Text = Trim(Text);
    if (Text.empty() || Text.front() != '(') Fail("InvalidDeclaration");
    const auto End = Text.find(')'); if (End == std::string_view::npos) Fail("InvalidDeclaration");
    const auto Parts = Split(Text.substr(1, End - 1)); MethodDeclaration Result;
    for (size_t Index = 0; Index < Parts.size(); ++Index) {
        auto Value = Parts[Index]; const auto Colon = Value.find(':');
        if (Colon != std::string_view::npos) {
            const auto Label = Trim(Value.substr(0, Colon)); Value = Trim(Value.substr(Colon + 1));
            if (!Identifier(Label)) Fail("InvalidDeclaration");
            if (Label == "Self" && Index == 0 && Value == Service) continue;
        }
        if (!Primitive(Value) || Result.Arguments.size() >= 32) Fail("InvalidDeclaration"); Result.Arguments.emplace_back(Value);
    }
    Text = Trim(Text.substr(End + 1)); if (Text.substr(0, 2) != "->") Fail("InvalidDeclaration");
    Text = Trim(Text.substr(2)); if (Text == "()") Text = "nil";
    if (!Primitive(Text)) Fail("InvalidDeclaration"); Result.Returns = std::string(Text); return Result;
}
std::vector<std::string> ParseSignal(std::string_view Text) {
    if (Text.size() > 256) Fail("InvalidDeclaration"); Text = Trim(Text);
    if (Text == "Signal") return {};
    if (Text.substr(0, 7) != "Signal<" || Text.back() != '>') Fail("InvalidDeclaration");
    const auto Parts = Split(Text.substr(7, Text.size() - 8));
    if (Parts.empty() || Parts.size() > 32) Fail("InvalidDeclaration");
    std::vector<std::string> Result;
    for (auto Part : Parts) {if (!Primitive(Part)) Fail("InvalidDeclaration"); Result.emplace_back(Part);} return Result;
}
std::string MergeDefinitions(const DeclarationSet& Set, const std::string& Definitions) {
    if (Set.Files.empty()) return Definitions;
    std::string Output = Definitions; const std::string Marker = "export type App = ";
    const auto Offset = Output.find(Marker);
    if (Offset == std::string::npos || (Offset && Output[Offset - 1] != '\n') ||
        Output.find(Marker, Offset + Marker.size()) != std::string::npos) Fail("DefinitionsInvalid");
    const auto End = Output.find('\n',Offset);
    if (End == std::string::npos || Output.find("declare app: App") == std::string::npos) Fail("DefinitionsInvalid");
    std::vector<std::string> Overloads;
    const auto Schema = Json::parse(LuiSchema::GetJson());
    for (const auto& Service : Schema.at("services")) {
        const auto Name = Service.at("name").get<std::string>();
        Overloads.push_back("{ read GetService: (Self: App, Name: \"" + Name + "\") -> " + Name + " }");
    }
    size_t Index = 0, SignalIndex = 0;
    for (const auto& Pair : Set.Services) {
        const auto Alias = "LuiDeclaredService" + std::to_string(++Index);
        std::string Fields;
        for (const auto& Method : Pair.second.OwnedMethods)
            Fields += "    read " + Method.first + ": " + OwnedSignature(Alias) + ",\n";
        for (const auto& Method : Pair.second.Methods) {
            Fields += "    read " + Method.first + ": (Self: " + Alias;
            const auto Params = Parameters(Method.second.Arguments); if (!Params.empty()) Fields += ", " + Params;
            Fields += ") -> " + Method.second.Returns + ",\n";
        }
        for (const auto& Signal : Pair.second.Signals) {
            const auto SignalAlias = "LuiDeclaredSignal" + std::to_string(++SignalIndex);
            Output += "\nexport type " + SignalAlias + " = { read Connect: (Self: " + SignalAlias + ", Callback: (" +
                Parameters(Signal.second) + ") -> ()) -> Connection }\n";
            Fields += "    read " + Signal.first + ": " + SignalAlias + ",\n";
        }
        Output += "\nexport type " + Alias + " = {\n" + Fields + "}\n";
        Overloads.push_back("{ read GetService: (Self: App, Name: \"" + Pair.first + "\") -> " + Alias + " }");
    }
    // Balance intersections just as the core generator does, and use the same
    // App self type for built-in and declared overloads to avoid recursive
    // subtype mismatches caused by intersecting an already recursive App.
    while (Overloads.size() > 1) {
        std::vector<std::string> Next;
        for (size_t Index = 0; Index < Overloads.size(); Index += 2)
            Next.push_back(Index + 1 == Overloads.size() ? Overloads[Index] : "(" + Overloads[Index] + " & " + Overloads[Index + 1] + ")");
        Overloads = std::move(Next);
    }
    Output.replace(Offset,End - Offset,Marker + Overloads.at(0));
    if (Output.size() > 8 * 1024 * 1024) Fail("LimitExceeded"); return Output;
}
std::string OwnedSignature(const std::string& Self) {
    return "(Self: " + Self + ", Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest";
}
}
