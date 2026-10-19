#pragma once
#include "Project.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <stdexcept>
namespace LuiTests {
struct ConversationFixture {
    std::filesystem::path Root;
    static std::string Read(const std::filesystem::path& Path) {
        std::ifstream File(Path,std::ios::binary); if(!File) throw std::runtime_error("conversation source missing");
        return {std::istreambuf_iterator<char>(File),std::istreambuf_iterator<char>()};
    }
    void Write(const std::filesystem::path& Path,const std::string& Bytes) {
        std::filesystem::create_directories(Path.parent_path()); std::ofstream File(Path,std::ios::binary); File<<Bytes;
        if(!File) throw std::runtime_error("conversation fixture write failed");
    }
    ConversationFixture(const std::filesystem::path& Source,const std::string& Pixel) {
        using Json=nlohmann::json; using Fs=std::filesystem::path;
        Root=std::filesystem::temp_directory_path()/("lui-conversation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if(!Root.is_absolute() || !std::filesystem::equivalent(Root.parent_path(),std::filesystem::temp_directory_path())) throw std::runtime_error("fixture path escaped root");
        Write(Root/"Src/Main.luau",Read(Source/"Src/Main.luau"));
        Json Pins=Json::array();
        for(const char* Name:{"Scope.luau","State.luau","VirtualList.luau"}) {
            const auto Bytes=Read(Source/"Vendor/LuiUI"/Name); Write(Root/"Vendor/LuiUI"/Name,Bytes);
            Pins.push_back({{"Path",Name},{"Sha256",Lui::Project::Hash(Bytes)}});
        }
        Write(Root/"Modules.lock.json",Json{{"SchemaVersion",1},{"Packages",Json::array({{{"Alias","LuiUI"},{"Provenance",{{"Kind","LocalVendor"},{"Id","LuiUI-0.1.0"}}},{"Files",Pins}}})}}.dump());
        Write(Root/"Avatar.png",Pixel); Write(Root/"lui.json",Read(Source/"lui.json"));
    }
    ~ConversationFixture() {std::error_code Error; std::filesystem::remove_all(Root,Error);}
};
}
