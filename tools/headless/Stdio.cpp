#include "Stdio.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <pthread.h>
#include <unistd.h>
#endif

namespace Lui::Headless {
namespace {
constexpr unsigned Maximum = 65536;
bool Integer(const LuiValueV1& Value, unsigned Minimum, unsigned Limit) {
    return Value.Type == LUI_VALUE_NUMBER && Value.Number >= Minimum && Value.Number <= Limit &&
        std::floor(Value.Number) == Value.Number;
}
LuiValueV1 Number(unsigned Value) {
    LuiValueV1 Result{sizeof(Result),LUI_VALUE_NUMBER};
    Result.Number = Value;
    return Result;
}
LuiValueV1 Text(const char* Data, size_t Size) {
    LuiValueV1 Result{sizeof(Result),LUI_VALUE_STRING};
    Result.Text = Data;
    Result.TextLength = Size;
    return Result;
}
#if defined(_WIN32)
HANDLE StreamHandle(unsigned Index) {
    return GetStdHandle(Index == 0 ? STD_INPUT_HANDLE : Index == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
}
#endif
int Transfer(unsigned Stream, char* Data, unsigned Size) {
#if defined(_WIN32)
    DWORD Count = 0;
    const BOOL Ok = Stream == 0 ? ReadFile(StreamHandle(Stream), Data, Size, &Count, nullptr) :
        WriteFile(StreamHandle(Stream), Data, Size, &Count, nullptr);
    if (!Ok && Stream == 0 && (GetLastError() == ERROR_BROKEN_PIPE || GetLastError() == ERROR_HANDLE_EOF)) return 0;
    return Ok ? static_cast<int>(Count) : -1;
#else
    return static_cast<int>(Stream == 0 ? read(0, Data, Size) : write(static_cast<int>(Stream), Data, Size));
#endif
}
}
Stdio::Stdio(LuiRuntime* Owner, const std::vector<std::string>& Args, std::function<void()> Notify)
    : Runtime(Owner), Arguments(Args), Wake(std::move(Notify)) {
#if !defined(_WIN32)
    // Only this standalone process installs these handlers. A repeated targeted
    // interrupt covers cancellation between a worker's check and its system call.
    struct sigaction Action{};
    sigemptyset(&Action.sa_mask);
    Action.sa_handler = +[](int) {};
    if (sigaction(SIGUSR1,&Action,nullptr)) throw std::runtime_error("StdioUnavailable");
    Action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE,&Action,nullptr)) throw std::runtime_error("StdioUnavailable");
#endif
    struct Method { const char* Name; const char* Type; LuiExtensionMethodV1 Call; };
    const Method Methods[] = {
        {"Read","(Limit: number, Line: boolean) -> number",Read},
        {"Write","(Stream: number, Bytes: string) -> number",Write},
        {"Cancel","(Id: number) -> boolean",Cancel},
        {"IsTerminal","(Stream: number) -> boolean",Terminal},
        {"Argument","(Index: number) -> string",Argument},
        {"ArgumentCount","() -> number",Count},
        {"SetExitStatus","(Code: number) -> nil",Exit},
    };
    for (const auto& Item : Methods)
        if (!Lui_RegisterHostMethod(Runtime,"Stdio",Item.Name,Item.Type,Item.Call,this))
            throw std::runtime_error("StdioUnavailable");
    if (!Lui_RegisterHostSignal(Runtime,"Stdio","Completed","Signal<number, string, string>"))
        throw std::runtime_error("StdioUnavailable");
}
void Stdio::Interrupt(Job& Work) {
    Work.Cancel = true;
    if (Work.Done || !Work.Worker.joinable()) return;
#if defined(_WIN32)
    CancelSynchronousIo(Work.Worker.native_handle());
#else
    pthread_kill(Work.Worker.native_handle(), SIGUSR1);
#endif
}
Stdio::~Stdio() {
    const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (auto& Work : Jobs) if (Work) Interrupt(*Work);
    for (auto& Work : Jobs) {
        if (!Work) continue;
        while (!Work->Done) {
            // Some device drivers ignore cancellation. Retire the dedicated
            // owner process instead of detaching a worker with live references.
            if (std::chrono::steady_clock::now() >= Deadline) std::_Exit(ShutdownStatus);
            Interrupt(*Work);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Work->Worker.join();
    }
}
void Stdio::Input(Job& Work, size_t Limit, bool Line) {
    char Block[4096];
    while (!Work.Cancel) {
        if (!Buffered.empty()) {
            size_t Take = std::min(Limit - Work.Data.size(), Buffered.size());
            const size_t End = Line ? Buffered.find('\n') : std::string::npos;
            if (End < Take) Take = End + 1;
            Work.Data.append(Buffered, 0, Take);
            Buffered.erase(0, Take);
            if (!Line || Work.Data.back() == '\n') return;
            if (Work.Data.size() == Limit) { Work.Status = "Limit"; return; }
        }
        const int Count = Transfer(0, Block, static_cast<unsigned>(sizeof(Block)));
        if (Count < 0) {
#if !defined(_WIN32)
            if (errno == EINTR) continue;
#endif
            Work.Status = "IoError"; return;
        }
        if (!Count) { if (Work.Data.empty()) Work.Status = "Eof"; return; }
        Buffered.assign(Block, static_cast<size_t>(Count));
    }
}
void Stdio::Output(Job& Work, unsigned Stream) {
    size_t Offset = 0;
    while (Offset < Work.Data.size() && !Work.Cancel) {
        const int Count = Transfer(Stream, Work.Data.data() + Offset, static_cast<unsigned>(Work.Data.size() - Offset));
        if (Count <= 0) {
#if !defined(_WIN32)
            if (Count < 0 && errno == EINTR) continue;
#endif
            Work.Status = "IoError"; break;
        }
        Offset += static_cast<size_t>(Count);
    }
    Work.Data.clear();
}
unsigned Stdio::Start(unsigned Stream, size_t Limit, bool Line, std::string Data) {
    if (Jobs[Stream] || Next == 0xffffffffu) return 0;
    auto Work = std::make_unique<Job>();
    Work->Id = ++Next;
    Work->Data = std::move(Data);
    Work->Worker = std::thread([this, Entry = Work.get(), Stream, Limit, Line] {
        try {
#if !defined(_WIN32)
            sigset_t Mask;
            sigemptyset(&Mask); sigaddset(&Mask,SIGUSR1);
            pthread_sigmask(SIG_UNBLOCK,&Mask,nullptr);
#endif
            if (Stream == 0) Input(*Entry,Limit,Line); else Output(*Entry,Stream);
        } catch (...) { Entry->Status = "IoError"; }
        Entry->Done = true;
        Wake();
    });
    Jobs[Stream] = std::move(Work);
    return Next;
}
bool Stdio::Pending() const {
    return std::any_of(Jobs.begin(), Jobs.end(), [](const auto& Work){ return bool(Work); });
}
bool Stdio::Pump() {
    for (auto& Slot : Jobs) {
        if (!Slot) continue;
        if (!Slot->Done) { if (Slot->Cancel) Interrupt(*Slot); continue; }
        auto Work = std::move(Slot);
        Work->Worker.join();
        const char* Status = Work->Cancel ? "Canceled" : Work->Status;
        const LuiValueV1 Args[] = {Number(Work->Id), Text(Status,std::strlen(Status)), Text(Work->Data.data(),Work->Data.size())};
        if (!Lui_EmitHostSignal(Runtime,"Stdio","Completed",Args,3)) return false;
    }
    return true;
}
int Stdio::Read(void* Context,const LuiValueV1* Args,uint32_t Size,LuiValueV1* Result) {
    if (Size != 2 || !Integer(Args[0],1,Maximum) || Args[1].Type != LUI_VALUE_BOOLEAN) return 0;
    *Result = Number(static_cast<Stdio*>(Context)->Start(0,static_cast<unsigned>(Args[0].Number),Args[1].Boolean != 0,{})); return 1;
}
int Stdio::Write(void* Context,const LuiValueV1* Args,uint32_t Size,LuiValueV1* Result) {
    if (Size != 2 || !Integer(Args[0],1,2) || Args[1].Type != LUI_VALUE_STRING || Args[1].TextLength > Maximum) return 0;
    *Result = Number(static_cast<Stdio*>(Context)->Start(static_cast<unsigned>(Args[0].Number),0,false,
        std::string(Args[1].Text,static_cast<size_t>(Args[1].TextLength)))); return 1;
}
int Stdio::Cancel(void* Context,const LuiValueV1* Args,uint32_t Size,LuiValueV1* Result) {
    if (Size != 1 || !Integer(Args[0],1,0xffffffffu)) return 0;
    Result->Type = LUI_VALUE_BOOLEAN;
    for (auto& Work : static_cast<Stdio*>(Context)->Jobs)
        if (Work && Work->Id == Args[0].Number) { Interrupt(*Work); Result->Boolean = 1; }
    return 1;
}
int Stdio::Terminal(void*,const LuiValueV1* Args,uint32_t Size,LuiValueV1* Result) {
    if (Size != 1 || !Integer(Args[0],0,2)) return 0;
    const auto Index = static_cast<unsigned>(Args[0].Number);
    Result->Type = LUI_VALUE_BOOLEAN;
#if defined(_WIN32)
    DWORD Mode;
    Result->Boolean = GetConsoleMode(StreamHandle(Index),&Mode) != 0;
#else
    Result->Boolean = isatty(static_cast<int>(Index));
#endif
    return 1;
}
int Stdio::Argument(void* Context,const LuiValueV1* Args,uint32_t Size,LuiValueV1* Result) {
    const auto& Values = static_cast<Stdio*>(Context)->Arguments;
    if (Size != 1 || !Integer(Args[0],1,static_cast<unsigned>(Values.size()))) return 0;
    const auto& Value = Values[static_cast<size_t>(Args[0].Number)-1];
    *Result = Text(Value.data(), Value.size()); return 1;
}
int Stdio::Count(void* Context,const LuiValueV1*,uint32_t Size,LuiValueV1* Result) {
    if (Size) return 0;
    *Result = Number(static_cast<unsigned>(static_cast<Stdio*>(Context)->Arguments.size())); return 1;
}
int Stdio::Exit(void* Context,const LuiValueV1* Args,uint32_t Size,LuiValueV1* Result) {
    if (Size != 1 || !Integer(Args[0],0,125)) return 0;
    static_cast<Stdio*>(Context)->ExitStatus = static_cast<int>(Args[0].Number);
    Result->Type = LUI_VALUE_NIL; return 1;
}
}
