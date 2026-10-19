#include "Package.h"
#include "Files.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <iostream>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif
int main(int Count, char** Arguments) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    try {
        std::vector<std::string> Args;
#ifdef _WIN32
        int WideCount=0; wchar_t** Wide=CommandLineToArgvW(GetCommandLineW(),&WideCount);
        if (!Wide) throw std::runtime_error("InvalidInput");
        for (int Index=0;Index<WideCount;++Index) {
            const int Bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,Wide[Index],-1,nullptr,0,nullptr,nullptr);
            if (!Bytes) {LocalFree(Wide); throw std::runtime_error("InvalidInput");}
            std::string Value(Bytes,'\0');
            WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,Wide[Index],-1,Value.data(),Bytes,nullptr,nullptr);
            Value.pop_back(); Args.push_back(std::move(Value));
        }
        LocalFree(Wide);
#else
        for (int Index=0;Index<Count;++Index) Args.emplace_back(Arguments[Index]);
#endif
        using namespace Lui::Project;
        std::shared_ptr<const PackageSnapshot> Package; std::string Error;
        for (const auto& Arg:Args) if (Arg.size()>4096 || !Internal::Utf8(Arg)) throw std::runtime_error("InvalidInput");
        if (Args.size()==4 && Args[1]=="--stage") {
            Package=CapturePackage(std::filesystem::u8path(Args[2]),{},Error);
            if (Package && !StagePackage(*Package,std::filesystem::u8path(Args[3]),{},Error)) Package.reset();
        } else if (Args.size()==3 && Args[1]=="--verify") Package=VerifyPackage(std::filesystem::u8path(Args[2]),{},Error);
        else throw std::runtime_error("InvalidInput");
        if (!Package) {std::cerr<<Error<<'\n'; return 1;}
        const auto Index=nlohmann::json::parse(Package->GetIndex());
        std::cout<<nlohmann::json{{"SchemaVersion",Index.at("SchemaVersion")},{"Profile",Index.at("Profile")},{"PackageDigest",Package->GetDigest()},
            {"SourceDigest",Package->GetPrepared().GetSources().GetDigest()},{"CompiledDigest",Package->GetPrepared().GetDigest()},
            {"SourceCount",Index.at("Sources").size()},{"AssetCount",Index.at("Assets").size()}}.dump()<<'\n';
        return 0;
    } catch (const std::bad_alloc&) {std::fputs("[LUI:Project] LimitExceeded\n",stderr);}
    catch (...) {std::fputs("[LUI:Project] InvalidInput\n",stderr);}
    return 1;
}
