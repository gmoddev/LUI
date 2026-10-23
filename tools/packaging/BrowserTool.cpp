#include "../../runtime/project/Package.h"
#include "../../runtime/project/Files.h"
#include "../../native/abi/LuiRuntime.h"
#include <nlohmann/json.hpp>
#include <iostream>

// Offline construction only. CapturePackage owns validation, resolution and compilation.
int main(int Count,char** Args) {
    using namespace Lui::Project;
    using Json=nlohmann::json;
    try {
        if(Count!=4) throw std::runtime_error("Arguments");
        std::string Error;
        auto Package=CapturePackage(std::filesystem::u8path(Args[1]),{},Error);
        if(!Package) throw std::runtime_error("Capture");
        const auto& Prepared=Package->GetPrepared();
        const auto Data=Json::parse(Prepared.GetSources().GetDescriptor());
        const auto& Manifest=Data.at("Manifest");
        if(!Manifest.at("Capabilities").empty() || !Manifest.at("Extensions").empty() ||
            !Manifest.at("ExtensionDeclarations").empty()) throw std::runtime_error("Unsupported");
        std::map<std::string,std::string> Files;
        for(const auto& Pair:Package->GetFiles()) Files.emplace(Pair.first,Pair.second.Bytes);
        for(const auto& Pair:Prepared.GetSources().GetSources()) Files.emplace(Pair.second.StagedPath,Pair.second.Bytes);
        if(Files.size()>256) throw std::runtime_error("Limit");
        Json Content=Json::array(), Runtime=Json::array(); size_t Total=0;
        Internal::PathIndex Paths;
        for(const auto& Pair:Files) {
            Paths.Add(Pair.first);
            if(Pair.second.size()>8*1024*1024 || Pair.second.size()>32*1024*1024-Total) throw std::runtime_error("Limit");
            Total+=Pair.second.size();
            Content.push_back({{"Path",Pair.first},{"Bytes",Pair.second.size()},{"Sha256",Hash(Pair.second)}});
        }
        // Revalidate exactly the bytes being shipped, not their original paths.
        auto Sources=PrepareCaptured(Files,"lui.json",{},Error);
        auto Compiled=Sources?Compile(Sources,{},Error):nullptr;
        if(!Compiled || Compiled->GetDigest()!=Prepared.GetDigest()) throw std::runtime_error("Parity");
        Internal::Anchor Build(std::filesystem::u8path(Args[2]));
        std::map<std::string,std::string> RuntimeBytes;
        for(const char* Name:{"Worker.mjs","Host.mjs","Protocol.mjs","Deployment.mjs","Application.mjs","LuiBrowser.mjs","LuiBrowser.wasm","Probe.css"}) {
            auto Bytes=Build.Read(Name,64*1024*1024).Bytes;
            Runtime.push_back({{"Path",Name},{"Bytes",Bytes.size()},{"Sha256",Hash(Bytes)}});
            RuntimeBytes.emplace(Name,std::move(Bytes));
        }
        Json Descriptor{{"FormatVersion",1},{"Profile","BrowserHostV1"},{"FrameworkVersion","0.0.1"},
            {"SchemaSha256",Hash(Lui_GetSchemaJson())},{"LuauRevision",Data.at("LuauRevision")},
            {"SourceDigest",Sources->GetDigest()},{"CompiledDigest",Compiled->GetDigest()}, {"Content",Content},{"Runtime",Runtime}};
        const auto Index=Descriptor.dump(); if(Index.size()>192*1024) throw std::runtime_error("Limit");
        const auto Output=std::filesystem::u8path(Args[3]);
        if(!std::filesystem::create_directory(Output)) throw std::runtime_error("Exists");
        Internal::Anchor Target(Output);
        for(const auto& Pair:Files) Target.Write("Content/"+Pair.first,Pair.second);
        for(const auto& Pair:RuntimeBytes) Target.Write("Runtime/"+Pair.first,Pair.second);
        Target.Write("LICENSE",Build.Read("LICENSE",1024*1024).Bytes);
        for(const char* Name:{"Luau.txt","Json.txt","PicoSHA2.txt"}) {
            const auto Path=std::string("THIRD-PARTY-LICENSES/")+Name;
            Target.Write(Path,Build.Read(Path,1024*1024).Bytes);
        }
        Target.Write("index.html","<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>LUI application</title><link rel=\"stylesheet\" href=\"./Runtime/Probe.css\"><main id=\"Application\" aria-label=\"LUI application\"></main><script type=\"module\" src=\"./Runtime/Application.mjs\"></script></html>");
        // Descriptor is the last commit marker. Existing output is never replaced.
        Target.Write("lui.browser.json",Index);
        std::cout<<"[LUI:BrowserPackage] "<<Hash(Index)<<'\n'; return 0;
    } catch(...) {std::cerr<<"[LUI:BrowserPackage] PreparationFailed\n";return 1;}
}
