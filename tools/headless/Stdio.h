#pragma once
#include "LuiRuntime.h"
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace Lui::Headless {
class Stdio {
public:
    Stdio(LuiRuntime* Runtime, const std::vector<std::string>& Arguments, std::function<void()> Wake);
    ~Stdio();
    bool Pump();
    bool Pending() const;
    int ExitStatus = 0;
    int ShutdownStatus = 1;
private:
    struct Job {
        unsigned Id;
        std::thread Worker;
        std::atomic<bool> Cancel{false}, Done{false};
        std::string Data;
        const char* Status = "Ok";
    };
    LuiRuntime* Runtime;
    std::vector<std::string> Arguments;
    std::function<void()> Wake;
    std::array<std::unique_ptr<Job>,3> Jobs;
    std::string Buffered;
    unsigned Next = 0;
    static int LUI_EXTENSION_CALL Read(void*, const LuiValueV1*, uint32_t, LuiValueV1*);
    static int LUI_EXTENSION_CALL Write(void*, const LuiValueV1*, uint32_t, LuiValueV1*);
    static int LUI_EXTENSION_CALL Cancel(void*, const LuiValueV1*, uint32_t, LuiValueV1*);
    static int LUI_EXTENSION_CALL Terminal(void*, const LuiValueV1*, uint32_t, LuiValueV1*);
    static int LUI_EXTENSION_CALL Argument(void*, const LuiValueV1*, uint32_t, LuiValueV1*);
    static int LUI_EXTENSION_CALL Count(void*, const LuiValueV1*, uint32_t, LuiValueV1*);
    static int LUI_EXTENSION_CALL Exit(void*, const LuiValueV1*, uint32_t, LuiValueV1*);
    unsigned Start(unsigned Stream, size_t Limit, bool Line, std::string Data);
    void Input(Job& Work, size_t Limit, bool Line);
    void Output(Job& Work, unsigned Stream);
    static void Interrupt(Job& Work);
};
}
