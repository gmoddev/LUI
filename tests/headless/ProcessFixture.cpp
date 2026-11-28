#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif
namespace {
int Run(const std::vector<std::string>& Args) {
    if (Args.size()<2) return 10;
    if (Args[1]=="nohandle" && Args.size()==4) {
#if defined(_WIN32)
        wchar_t Path[32768];
        auto Inherited=reinterpret_cast<HANDLE>(static_cast<uintptr_t>(std::stoull(Args[2])));
        const DWORD Count=GetFinalPathNameByHandleW(Inherited,Path,32768,FILE_NAME_NORMALIZED);
        if(Count && Count<32768 && std::filesystem::path(Path).filename().u8string()==Args[3]) return 15;
#else
        char Buffer[4096];
        auto Link="/proc/self/fd/"+Args[2]; const auto Length=readlink(Link.c_str(),Buffer,sizeof(Buffer));
        if(Length>0 && std::filesystem::path(std::string(Buffer,Length)).filename()==Args[3]) return 15;
#endif
        return 0;
    }
    if (Args[1]=="inspect") {
        std::printf("%s|%s|%s\n",std::filesystem::current_path().filename().u8string().c_str(),
            std::getenv("LUI_LABEL")?std::getenv("LUI_LABEL"):"unset",std::getenv("LUI_PRIVATE_TEST")?"visible":"absent");
        for (size_t I=2;I<Args.size();++I) std::printf("%zu:%s\n",Args[I].size(),Args[I].c_str());
        char Data[4096]; const auto Size=std::fread(Data,1,sizeof(Data),stdin);std::fwrite(Data,1,Size,stdout);
        std::fwrite("stderr\0bytes",1,12,stderr); return 7;
    }
    if (Args[1]=="flood") {std::string Block(4096,'x');for(unsigned I=0;I<128;++I){std::fwrite(Block.data(),1,Block.size(),stdout);std::fwrite(Block.data(),1,Block.size(),stderr);}return 0;}
    if (Args[1]=="late") {std::this_thread::sleep_for(std::chrono::seconds(2));std::FILE* F=std::fopen("late.txt","wb");if(F)std::fclose(F);return 0;}
    if (Args[1]=="tree" || Args[1]=="treewait") {
#if defined(_WIN32)
        wchar_t Path[32768]; const auto Len=GetModuleFileNameW(nullptr,Path,32768); if (!Len || Len>=32768) return 12;
        auto Cmd=L"\""+std::wstring(Path)+L"\" late";STARTUPINFOW Startup{sizeof(Startup)};PROCESS_INFORMATION Child{};
        if (!CreateProcessW(Path,Cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&Startup,&Child)) return 13;
        CloseHandle(Child.hThread);CloseHandle(Child.hProcess);
#else
        const auto Pid=fork();if (Pid<0) return 13;
        if(!Pid){execl("/proc/self/exe","fixture","late",static_cast<char*>(nullptr));_exit(14);}
#endif
        if(Args[1]=="treewait") {
            auto* Ready=std::fopen("ready.txt","wb");
            if(!Ready) return 17;
#if defined(_WIN32)
            std::fprintf(Ready,"%lu",GetCurrentProcessId());
#else
            std::fprintf(Ready,"%ld",static_cast<long>(getpid()));
#endif
            std::fclose(Ready);
        }
        std::puts("started"); std::fflush(stdout);
        if(Args[1]=="tree") return 0;
    }
    if (Args[1]=="wait" || Args[1]=="treewait") {std::this_thread::sleep_for(std::chrono::seconds(10));return 0;}
    if (Args[1]=="input-rights") {
#if defined(_WIN32)
        DWORD N=0;return WriteFile(GetStdHandle(STD_INPUT_HANDLE),"x",1,&N,nullptr)?16:0;
#else
        return write(0,"x",1)<0?0:16;
#endif
    }
    return 11;
}
}
#if defined(_WIN32)
int wmain(int Count,wchar_t** Argv) {
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);_setmode(_fileno(stderr),_O_BINARY);
    std::vector<std::string> Args;
    for(int I=0;I<Count;++I) Args.push_back(std::filesystem::path(Argv[I]).u8string());
    return Run(Args);
}
#else
int main(int Count,char** Argv) {return Run(std::vector<std::string>(Argv,Argv+Count));}
#endif
