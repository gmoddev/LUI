#if defined(__linux__)
#include "ProcessHost.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/close_range.h>
#include <linux/memfd.h>

namespace Lui::Headless {
namespace {
struct Fd {
    int Value=-1;
    Fd()=default;
    explicit Fd(int V):Value(V) {}
    Fd(const Fd&)=delete;
    ~Fd(){Close();}
    void Close(){if(Value>=0){close(Value);Value=-1;}}
};
void Check(bool Good){if (!Good) throw std::runtime_error("ToolUnavailable");}
struct Pipe {
    Fd Read,Write;
    Pipe(){int P[2];Check(pipe2(P,O_CLOEXEC)==0);Read.Value=P[0];Write.Value=P[1];}
};
struct Child {
    pid_t Id;
    Fd& Lifetime;
    ~Child(){
        if (Id<1) return;
        Lifetime.Close();
        while (waitpid(Id,nullptr,0)<0 && errno==EINTR) {}
    }
};
}
struct ToolGrant::State {Fd Exe,Directory; std::string Name;};
ToolGrant::ToolGrant(const std::string& Executable,const std::string& Directory):Data(std::make_unique<State>()) {
    // Keep standard slots reserved when a service manager starts the host with closed streams.
    for (int I=0;I<3;++I) if (fcntl(I,F_GETFD)<0) {
        Check(errno==EBADF && open("/dev/null",O_RDWR|O_CLOEXEC)==I);
    }
    struct sigaction ChildExit{};ChildExit.sa_handler=SIG_DFL;sigemptyset(&ChildExit.sa_mask);
    Check(sigaction(SIGCHLD,&ChildExit,nullptr)==0);
    Check(std::filesystem::path(Executable).is_absolute() && std::filesystem::path(Directory).is_absolute());
    Data->Exe.Value=open(Executable.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    Data->Directory.Value=open(Directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    struct stat Info{}; unsigned char Header[4]{};
    Check(Data->Exe.Value>=3 && Data->Directory.Value>=3 && fstat(Data->Exe.Value,&Info)==0 && S_ISREG(Info.st_mode) &&
        !(Info.st_mode&(S_ISUID|S_ISGID)) && pread(Data->Exe.Value,Header,4,0)==4 && !std::memcmp(Header,"\177ELF",4));
    Data->Name=std::filesystem::path(Executable).filename().string();
}
ToolGrant::~ToolGrant()=default;
ToolResult ToolGrant::Run(const ToolSpec& Spec,const std::atomic<bool>& Canceled,const ToolOutput& Output) {
    using Clock=std::chrono::steady_clock;
    const auto Deadline=Clock::now()+std::chrono::milliseconds(Spec.Timeout);
    if (Canceled) return {"Canceled",-1};
    Fd Input(static_cast<int>(syscall(SYS_memfd_create,"lui-input",MFD_CLOEXEC|MFD_ALLOW_SEALING)));
    Check(Input.Value>=3);
    size_t At=0;
    while (At<Spec.Input.size()){
        const auto N=write(Input.Value,Spec.Input.data()+At,Spec.Input.size()-At);
        if (N<0 && errno==EINTR) continue;
        Check(N>0); At+=static_cast<size_t>(N);
    }
    Check(fcntl(Input.Value,F_ADD_SEALS,F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL)==0 && lseek(Input.Value,0,SEEK_SET)==0);
    Pipe Out,Err,Startup,Interactive,Lifetime;
    if(Spec.Feed) Check(fcntl(Interactive.Write.Value,F_SETFL,O_NONBLOCK)==0);
    sigset_t BlockPipe;sigemptyset(&BlockPipe);sigaddset(&BlockPipe,SIGPIPE);
    Check(pthread_sigmask(SIG_BLOCK,&BlockPipe,nullptr)==0);
    for (int F:{Out.Read.Value,Err.Read.Value,Startup.Read.Value}) Check(F>=3 && fcntl(F,F_SETFL,O_NONBLOCK)==0);
    std::vector<char*> Args{Data->Name.data()},Env;
    std::vector<std::string> Variables;
    for (const auto& Arg:Spec.Arguments) Args.push_back(const_cast<char*>(Arg.c_str()));
    for (const auto& Item:Spec.Environment) Variables.push_back(Item.first+"="+Item.second);
    for (auto& V:Variables) Env.push_back(V.data());
    Args.push_back(nullptr); Env.push_back(nullptr);

    char** Argv=Args.data(); char** Envp=Env.data();
    const int Exe=Data->Exe.Value,Cwd=Data->Directory.Value;
    const pid_t Pid=fork();
    if (Pid==0) {
        // Only direct syscalls and async-signal-safe operations run in the fork child.
        sigset_t Mask; sigemptyset(&Mask); sigprocmask(SIG_SETMASK,&Mask,nullptr);
        struct sigaction Default{}; Default.sa_handler=SIG_DFL; sigemptyset(&Default.sa_mask);
        sigaction(SIGPIPE,&Default,nullptr);
        auto FailLaunch=[&]() {
            const char Failed=1;
            ssize_t Written;
            do {Written=write(Startup.Write.Value,&Failed,1);} while(Written<0 && errno==EINTR);
            _exit(126);
        };
        if(setsid()<0) FailLaunch();
        const auto Guardian=getpid();
        const auto Tool=fork();
        if(Tool<0) FailLaunch();
        if(Tool>0) {
            // Keep the leader waitable until its process group is retired. The
            // private control pipe reaches EOF even if the LUI host is killed.
            (void)setpgid(Tool,Tool);
            const int Control=Lifetime.Read.Value;
            (void)syscall(SYS_close_range,0u,static_cast<unsigned>(Control-1),0u);
            (void)syscall(SYS_close_range,static_cast<unsigned>(Control+1),~0u,0u);
            siginfo_t Info{};
            for(;;) {
                if(waitid(P_PID,static_cast<id_t>(Tool),&Info,WEXITED|WNOHANG|WNOWAIT)==0 && Info.si_pid==Tool) break;
                pollfd Watched{Control,POLLIN,0};
                const auto Seen=poll(&Watched,1,5);
                if(Seen>0 || (Seen<0 && errno!=EINTR)) break;
            }
            kill(-Tool,SIGKILL);kill(Tool,SIGKILL);
            int Status=0;while(waitpid(Tool,&Status,0)<0 && errno==EINTR) {}
            _exit(WIFEXITED(Status)?WEXITSTATUS(Status):128+WTERMSIG(Status));
        }
        if (!setpgid(0,0) && !prctl(PR_SET_PDEATHSIG,SIGKILL) && getppid()==Guardian &&
            !prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0) && !fchdir(Cwd) &&
            dup2(Spec.Feed?Interactive.Read.Value:Input.Value,0)==0 && dup2(Out.Write.Value,1)==1 && dup2(Err.Write.Value,2)==2 &&
            syscall(SYS_close_range,3u,~0u,CLOSE_RANGE_CLOEXEC)==0)
            syscall(SYS_execveat,Exe,"",Argv,Envp,AT_EMPTY_PATH);
        FailLaunch();
    }
    Check(Pid>0);
    Child Guard{Pid,Lifetime.Write};
    Lifetime.Read.Close();Interactive.Read.Close();if(!Spec.Feed) Interactive.Write.Close();
    std::string PendingInput;size_t Sent=0;
    Out.Write.Close(); Err.Write.Close(); Startup.Write.Close(); Input.Close();
    bool LeaderDone=false;
    siginfo_t Exit{};
    for (;;) {
        if (Canceled) return {"Canceled",-1};
        if (Clock::now()>=Deadline) return {"TimedOut",-1};
        if (!LeaderDone) {
            const int Status=waitid(P_PID,static_cast<id_t>(Pid),&Exit,WEXITED|WNOHANG|WNOWAIT);
            if (Status<0 && errno==ECHILD) {Guard.Id=-1;return {};}
            if (Status<0 && errno!=EINTR) return {};
            if (Status==0 && Exit.si_pid==Pid) {LeaderDone=true;}
        }
        if(Spec.Feed && Interactive.Write.Value>=0) {
            if(PendingInput.empty() && Spec.Feed->Pull(PendingInput)) Interactive.Write.Close();
            if(!PendingInput.empty()) {
                const auto N=write(Interactive.Write.Value,PendingInput.data()+Sent,PendingInput.size()-Sent);
                if(N>0) {
                    Sent+=static_cast<size_t>(N);
                    if(Sent==PendingInput.size()) {PendingInput.clear();Sent=0;}
                } else if(N<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) {
                    Interactive.Write.Close();PendingInput.clear();
                    std::lock_guard<std::mutex> Lock(Spec.Feed->Mutex);Spec.Feed->Stopped=true;
                }
            }
        }
        bool ReadAny=false;
        for (auto Pair:{std::make_pair(1u,&Out.Read),std::make_pair(2u,&Err.Read)}) {
            if (Pair.second->Value<0) continue;
            char Bytes[4096]; const auto N=read(Pair.second->Value,Bytes,sizeof(Bytes));
            if (N>0) {ReadAny=true;if (!Output(Pair.first,Bytes,static_cast<size_t>(N))) return {"OutputLimit",-1};}
            else if (!N) Pair.second->Close();
            else if (errno!=EINTR && errno!=EAGAIN && errno!=EWOULDBLOCK) return {};
        }
        if (LeaderDone && !ReadAny) {
            char Error; if (read(Startup.Read.Value,&Error,1)!=0) return {};
            return {"Exited",static_cast<double>(Exit.si_code==CLD_EXITED?Exit.si_status:128+Exit.si_status)};
        }
        if (!ReadAny) {
            pollfd Fds[2]={{Out.Read.Value,POLLIN,0},{Err.Read.Value,POLLIN,0}};
            (void)poll(Fds,2,5);
        }
    }
}
}
#endif
