#include "LuiRuntime.h"
#include "LuiExecution.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

template<class Operation>
static void Sample(const char* Name,int Controls,bool Viewport,Operation Call) {
    using Clock=std::chrono::steady_clock;
    constexpr int Iterations=10000;
    std::vector<double> Values;
    for(int Run=-1;Run<9;++Run) {
        const auto Started=Clock::now();
        for(int Index=0;Index<Iterations;++Index) Call();
        if(Run>=0) Values.push_back(
            std::chrono::duration<double,std::nano>(Clock::now()-Started).count()/Iterations);
    }
    std::sort(Values.begin(),Values.end());
    std::printf("[LUI:PumpCost] %s controls=%d viewport=%d n=%zu iterations=%d minNs=%.2f medianNs=%.2f maxNs=%.2f\n",
        Name,Controls,Viewport,Values.size(),Iterations,Values.front(),Values[4],Values.back());
}

int main() {
    for(int Controls : {0,128,1024,4000}) for(bool Viewport : {false,true}) {
        auto* Runtime=Lui_Create();
        if(!Runtime) return 1;
        std::string Scene="Rows={}; for Index=1,"+std::to_string(Controls)+" do Rows[Index]=Instance.new('Frame') end";
        if(Viewport) Scene+="; Surface=Instance.new('FrameSurface')";
        if(!Lui_RunScript(Runtime,Scene.c_str(),"IdlePump")) {
            std::fprintf(stderr,"[LUI:PumpCost] Fixture failed\n");
            Lui_Destroy(Runtime);
            return 1;
        }
        auto Probe=[&] {if(Lui_HasPendingWorkV1(Runtime)!=0) std::abort();};
        Probe();
        Sample("Pump",Controls,Viewport,[&] {Lui_Pump(Runtime);});
        Sample("PendingProbe",Controls,Viewport,Probe);
        Lui_Destroy(Runtime);
    }
}
