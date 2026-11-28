#pragma once
#include "LuiRuntime.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace Lui::Headless {
struct ToolFeed {
    std::mutex Mutex;
    std::deque<std::string> Queue;
    size_t Bytes=0;
    bool Eof=false, Stopped=false;
    bool Write(std::string Data);
    bool Close();
    bool Pull(std::string& Data);
};
struct ToolSpec {
    std::vector<std::string> Arguments;
    std::map<std::string,std::string> Environment;
    std::string Input;
    unsigned Timeout=5000, Limit=65536;
    std::shared_ptr<ToolFeed> Feed;
};
struct ToolResult {const char* Status="Failed"; double Exit=-1;};
using ToolOutput=std::function<bool(unsigned,const char*,size_t)>;
class ToolGrant {
public:
    ToolGrant(const std::string& Executable,const std::string& Directory);
    ~ToolGrant();
    ToolResult Run(const ToolSpec&,const std::atomic<bool>&,const ToolOutput&);
private:
    struct State;
    std::unique_ptr<State> Data;
};
class ProcessHost {
public:
    ProcessHost(LuiRuntime*,const std::string&,const std::string&,std::set<std::string>,std::function<void()>);
    ~ProcessHost();
    bool Pump();
    bool Pending() const {return !Jobs.empty();}
private:
    struct Chunk {unsigned Stream; std::string Data;};
    LuiRuntime* Runtime;
    ToolGrant Grant;
    std::set<std::string> EnvironmentKeys;
    std::function<void()> Wake;
    struct Job {
        std::mutex Mutex;
        std::vector<Chunk> Output;
        std::shared_ptr<ToolFeed> Feed;
        std::atomic<bool> Canceled{false},Done{false};
        ToolResult Result;
        std::thread Thread;
    };
    std::map<unsigned,std::unique_ptr<Job>> Jobs;
    unsigned Id=0;
    static int LUI_EXTENSION_CALL Start(void*,const LuiValueV1*,uint32_t,LuiValueV1*);
    static int LUI_EXTENSION_CALL Stop(void*,const LuiValueV1*,uint32_t,LuiValueV1*);
};
}
