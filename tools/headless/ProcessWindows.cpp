#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _CRT_RAND_S
#include "ProcessHost.h"
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace Lui::Headless {
namespace {
void Check(bool Good){if (!Good) throw std::runtime_error("ToolUnavailable");}
struct Handle {
    HANDLE Value=nullptr;
    Handle()=default;
    explicit Handle(HANDLE V):Value(V) {}
    Handle(const Handle&)=delete;
    Handle(Handle&& Other) noexcept:Value(Other.Value){Other.Value=nullptr;}
    ~Handle(){Close();}
    void Close(){if(Value && Value!=INVALID_HANDLE_VALUE) CloseHandle(Value);Value=nullptr;}
};
std::wstring Wide(const std::string& Value) {
    if (Value.empty()) return {};
    const int Len=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,Value.data(),static_cast<int>(Value.size()),nullptr,0);
    Check(Len>0); std::wstring Out(Len,0);
    Check(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,Value.data(),static_cast<int>(Value.size()),Out.data(),Len)==Len);
    return Out;
}
std::wstring Quote(const std::wstring& Input) {
    std::wstring Out(1,L'"'); unsigned Slashes=0;
    for (auto Ch:Input) {
        if (Ch==L'\\') {++Slashes;continue;}
        if (Ch==L'"') {Out.append(2*Slashes+1,L'\\');Out.push_back(Ch);}
        else {Out.append(Slashes,L'\\');Out.push_back(Ch);}
        Slashes=0;
    }
    Out.append(2*Slashes,L'\\');Out.push_back(L'"');return Out;
}
struct Pipe {
    Handle Read,Write;
    explicit Pipe(SECURITY_ATTRIBUTES& Inherit) {
        Check(CreatePipe(&Read.Value,&Write.Value,&Inherit,65536)!=0);
        Check(SetHandleInformation(Read.Value,HANDLE_FLAG_INHERIT,0)!=0);
    }
};
Handle InputFile(const std::string& Input,SECURITY_ATTRIBUTES& Inherit) {
    wchar_t Temp[MAX_PATH+1]{}; auto Size=GetTempPathW(MAX_PATH,Temp); Check(Size && Size<MAX_PATH);
    unsigned A=0,B=0; Check(rand_s(&A)==0 && rand_s(&B)==0);
    auto Path=std::filesystem::path(Temp)/(L"lui-process-"+std::to_wstring(A)+L"-"+std::to_wstring(B));
    Handle File(CreateFileW(Path.c_str(),GENERIC_READ|GENERIC_WRITE,0,&Inherit,CREATE_NEW,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr));
    Check(File.Value!=INVALID_HANDLE_VALUE);
    DWORD Written=0; Check(WriteFile(File.Value,Input.data(),static_cast<DWORD>(Input.size()),&Written,nullptr) && Written==Input.size());
    LARGE_INTEGER Beginning{};Check(SetFilePointerEx(File.Value,Beginning,nullptr,FILE_BEGIN)!=0);
    Handle ReadOnly;Check(DuplicateHandle(GetCurrentProcess(),File.Value,GetCurrentProcess(),&ReadOnly.Value,FILE_GENERIC_READ,TRUE,0)!=0);
    return ReadOnly;
}
struct InputPipe {
    Handle Read,Write;
    void Open(SECURITY_ATTRIBUTES& Inherit) {
        unsigned A=0,B=0;Check(rand_s(&A)==0 && rand_s(&B)==0);
        const auto Name=std::wstring(LR"(\\.\pipe\lui-input-)")+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(A)+L"-"+std::to_wstring(B);
        Write.Value=CreateNamedPipeW(Name.c_str(),PIPE_ACCESS_OUTBOUND|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_NOWAIT|PIPE_REJECT_REMOTE_CLIENTS,1,16384,4096,0,nullptr);
        Check(Write.Value!=INVALID_HANDLE_VALUE);
        Read.Value=CreateFileW(Name.c_str(),GENERIC_READ,0,&Inherit,OPEN_EXISTING,0,nullptr);
        ULONG Client=0;
        Check(Read.Value!=INVALID_HANDLE_VALUE && GetNamedPipeClientProcessId(Write.Value,&Client) && Client==GetCurrentProcessId());
    }
};
struct JobRetirement {
    HANDLE Job;
    bool Retire() noexcept {
        // Completion acknowledges OS job retirement, including early errors and cancellation.
        if (!TerminateJobObject(Job,1)) return false;
        const auto Deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION Counts{};
        do {
            if (!QueryInformationJobObject(Job,JobObjectBasicAccountingInformation,&Counts,sizeof(Counts),nullptr)) return false;
            if (!Counts.ActiveProcesses) return true;
            Sleep(1);
        } while (std::chrono::steady_clock::now()<Deadline);
        return false;
    }
};
struct Attributes {
    std::vector<unsigned char> Data;
    LPPROC_THREAD_ATTRIBUTE_LIST List=nullptr;
    Attributes() {
        SIZE_T Size=0; InitializeProcThreadAttributeList(nullptr,2,0,&Size); Check(Size!=0); Data.resize(Size);
        auto* P=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(Data.data());
        Check(InitializeProcThreadAttributeList(P,2,0,&Size)!=0); List=P;
    }
    ~Attributes(){if(List) DeleteProcThreadAttributeList(List);}
};
}
struct ToolGrant::State {
    std::filesystem::path Executable,Directory;
    std::vector<Handle> Anchors;
    void Pin(std::filesystem::path P,bool IsTool) {
        Check(P.is_absolute() && P.root_name().wstring().size()==2 && P==P.lexically_normal());
        std::filesystem::path At=P.root_path();
        auto Anchor=[&](bool Directory,bool Final) {
            Handle H(CreateFileW(At.c_str(),Directory?FILE_READ_ATTRIBUTES:GENERIC_READ|FILE_EXECUTE,
                FILE_SHARE_READ|(Directory && !Final?FILE_SHARE_WRITE:0),nullptr,OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT|(Directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr));
            BY_HANDLE_FILE_INFORMATION I{};
            Check(H.Value!=INVALID_HANDLE_VALUE && GetFileType(H.Value)==FILE_TYPE_DISK && GetFileInformationByHandle(H.Value,&I) &&
                !(I.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) && bool(I.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==Directory);
            Anchors.push_back(std::move(H));
        };
        Anchor(true,At==P);
        for(const auto& Part:P.relative_path()) {At/=Part; Anchor(At!=P || !IsTool,At==P);}
        if (IsTool) {
            // Only native images enter this profile. CreateProcess receives the exact absolute name.
            WORD Header=0;DWORD Count=0;
            Check(ReadFile(Anchors.back().Value,&Header,sizeof(Header),&Count,nullptr) && Count==sizeof(Header) && Header==0x5a4d);
        }
    }
};
ToolGrant::ToolGrant(const std::string& Executable,const std::string& Directory):Data(std::make_unique<State>()) {
    Data->Executable=std::filesystem::path(Wide(Executable)); Data->Directory=std::filesystem::path(Wide(Directory));
    Data->Pin(Data->Executable,true); Data->Pin(Data->Directory,false);
}
ToolGrant::~ToolGrant()=default;
ToolResult ToolGrant::Run(const ToolSpec& Spec,const std::atomic<bool>& Canceled,const ToolOutput& Output) {
    using Clock=std::chrono::steady_clock;
    const auto Deadline=Clock::now()+std::chrono::milliseconds(Spec.Timeout);
    if (Canceled) return {"Canceled",-1};
    SECURITY_ATTRIBUTES Inherit{sizeof(Inherit),nullptr,TRUE};
    InputPipe Interactive;
    if(Spec.Feed) Interactive.Open(Inherit);
    Handle Input=Spec.Feed?Handle(std::move(Interactive.Read)):InputFile(Spec.Input,Inherit);
    Pipe Out(Inherit),Err(Inherit);
    Handle Job(CreateJobObjectW(nullptr,nullptr)); Check(Job.Value!=nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits{};
    Limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    Check(SetInformationJobObject(Job.Value,JobObjectExtendedLimitInformation,&Limits,sizeof(Limits))!=0);
    Attributes Attr;
    HANDLE Streams[]={Input.Value,Out.Write.Value,Err.Write.Value};
    Check(UpdateProcThreadAttribute(Attr.List,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,Streams,sizeof(Streams),nullptr,nullptr)!=0);
    Check(UpdateProcThreadAttribute(Attr.List,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&Job.Value,sizeof(Job.Value),nullptr,nullptr)!=0);
    STARTUPINFOEXW Start{}; Start.StartupInfo.cb=sizeof(Start); Start.lpAttributeList=Attr.List;
    Start.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW; Start.StartupInfo.wShowWindow=SW_HIDE;
    Start.StartupInfo.hStdInput=Input.Value; Start.StartupInfo.hStdOutput=Out.Write.Value; Start.StartupInfo.hStdError=Err.Write.Value;
    std::wstring Command=Quote(Data->Executable.wstring());
    for (const auto& Arg:Spec.Arguments) {Command.push_back(L' ');Command+=Quote(Wide(Arg));}
    std::wstring Env;
    for (const auto& Item:Spec.Environment) {Env+=Wide(Item.first+"="+Item.second);Env.push_back(0);}
    Env.push_back(0);Env.push_back(0);
    PROCESS_INFORMATION Info{};
    Check(CreateProcessW(Data->Executable.c_str(),Command.data(),nullptr,nullptr,TRUE,
        EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,Env.data(),Data->Directory.c_str(),&Start.StartupInfo,&Info)!=0);
    Handle Child(Info.hProcess),Thread(Info.hThread);
    JobRetirement Retire{Job.Value};
    Thread.Close();Input.Close();Out.Write.Close();Err.Write.Close();
    auto Supervise=[&]() -> ToolResult {
    bool Finished=false;
    std::string Pending;size_t Sent=0;
    for (;;) {
        if (Canceled) return {"Canceled",-1};
        if (Clock::now()>=Deadline) return {"TimedOut",-1};
        if (!Finished && WaitForSingleObject(Child.Value,0)==WAIT_OBJECT_0) {
            Finished=true;Check(TerminateJobObject(Job.Value,1)!=0);
        }
        if(Spec.Feed && Interactive.Write.Value) {
            if(Pending.empty() && Spec.Feed->Pull(Pending)) Interactive.Write.Close();
            if(!Pending.empty()) {
                DWORD Count=0;
                if(WriteFile(Interactive.Write.Value,Pending.data()+Sent,static_cast<DWORD>(Pending.size()-Sent),&Count,nullptr)) {
                    Sent+=Count;
                    if(Sent==Pending.size()) {Pending.clear();Sent=0;}
                } else {
                    const auto Code=GetLastError();
                    if(Code==ERROR_NO_DATA || Code==ERROR_BROKEN_PIPE || Code==ERROR_PIPE_NOT_CONNECTED) {
                        Interactive.Write.Close();Pending.clear();
                        std::lock_guard<std::mutex> Lock(Spec.Feed->Mutex);Spec.Feed->Stopped=true;
                    } else if(Code!=ERROR_PIPE_BUSY) return {};
                }
            }
        }
        bool ReadAny=false;
        for (auto Pair:{std::make_pair(1u,&Out.Read),std::make_pair(2u,&Err.Read)}) {
            if (!Pair.second->Value) continue;
            DWORD Available=0,Count=0; char Bytes[4096];
            if (!PeekNamedPipe(Pair.second->Value,nullptr,0,nullptr,&Available,nullptr)) {
                if (GetLastError()==ERROR_BROKEN_PIPE){Pair.second->Close();continue;} return {};
            }
            if (Available) {
                if (!ReadFile(Pair.second->Value,Bytes,std::min<DWORD>(sizeof(Bytes),Available),&Count,nullptr)) return {};
                if (Count) {ReadAny=true;if (!Output(Pair.first,Bytes,Count)) return {"OutputLimit",-1};}
            }
        }
        if (Finished && !ReadAny) {
            DWORD Code=0;Check(GetExitCodeProcess(Child.Value,&Code)!=0);return {"Exited",static_cast<double>(Code)};
        }
        if (!ReadAny) WaitForSingleObject(Child.Value,5);
    }
    };
    ToolResult Outcome;
    try {Outcome=Supervise();} catch (...) {}
    if (!Retire.Retire()) return {"RetirementFailed",-1};
    return Outcome;
}
}
#endif
