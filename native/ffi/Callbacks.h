#pragma once
#include "Core.h"
#include "Scoped.h"
#include <deque>
#include <mutex>

namespace Lui::Ffi {
class Callbacks {
public:
    struct Scope {
        Engine& Service;
        LuiFfiComputeV1 Compute;
        void* Context;
        bool Invoking = false;
        Scope(Engine&, LuiFfiComputeV1, void*);
        ~Scope();
        Scope(const Scope&) = delete;
    private:
        Scope* Previous;
    };
    struct Event { unsigned Id; std::vector<std::vector<unsigned char>> Values; };
    struct Mailbox {
        std::mutex Mutex;
        std::deque<Event> Pending;
        uint64_t Dropped=0;
    };
    Callbacks():Mail(std::make_shared<Mailbox>()){}
    ~Callbacks();
    Json Create(Engine&,const Json&);
    Json Poll(Engine&);
    bool Retire(unsigned);
private:
    struct Entry;
    std::map<unsigned,Entry*> Entries;
    std::shared_ptr<Mailbox> Mail;
};
}
