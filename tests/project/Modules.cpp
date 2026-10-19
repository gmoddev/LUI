#include "LuiProject.h"
#include "Project.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>
#include <filesystem>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace Fs = std::filesystem;
using Json = nlohmann::json;
static void Check(bool Value, const char* Label) { if (!Value) throw std::runtime_error(Label); }
static void Write(const Fs::path& Path, const std::string& Bytes) {
    Fs::create_directories(Path.parent_path()); std::ofstream File(Path,std::ios::binary);
    File.write(Bytes.data(),Bytes.size()); Check(bool(File),"fixture write failed");
}
struct Recorder {
    std::set<int> Alive;
    int Creates=0, Destroys=0, Properties=0, Arranges=0, FailCreate=0, HostCalls=0;
    bool FailProperty=false;
    std::vector<std::string> Logs;
};
static int LUI_CALL Create(void* Context, int Id, const char*) {
    auto& Value=*static_cast<Recorder*>(Context); ++Value.Creates; Value.Alive.insert(Id);
    return Value.Creates != Value.FailCreate;
}
static int LUI_CALL Property(void* Context,int,const char*,const char*) {
    auto& Value=*static_cast<Recorder*>(Context); ++Value.Properties; return !Value.FailProperty;
}
static int LUI_CALL Parent(void*,int,int) { return 1; }
static int LUI_CALL Arrange(void* Context,int,double,double,double,double,int) { ++static_cast<Recorder*>(Context)->Arranges; return 1; }
static int LUI_CALL Destroy(void* Context,int Id) { auto& Value=*static_cast<Recorder*>(Context); ++Value.Destroys; Value.Alive.erase(Id); return 1; }
static void LUI_CALL Log(void* Context,const char*,const char* Message) { static_cast<Recorder*>(Context)->Logs.emplace_back(Message); }
static int LUI_CALL Clipboard(void* Context,const char*) { ++static_cast<Recorder*>(Context)->HostCalls; return 1; }
static int LUI_CALL ReadClipboard(void* Context,uint64_t) { ++static_cast<Recorder*>(Context)->HostCalls; return 1; }
static int LUI_CALL Probe(void* Context,const LuiValueV1*,uint32_t,LuiValueV1* Output) {
    auto& Value=*static_cast<Recorder*>(Context); ++Value.HostCalls;
    Output->Type=LUI_VALUE_NUMBER; Output->Number=Value.Creates; return 1;
}
static LuiProjectRunOptionsV1 RunOptions() {
    return {sizeof(LuiProjectRunOptionsV1),LUI_PROJECT_RUN_ABI_VERSION,LUI_PROJECT_RUN_DEVELOPMENT_DIAGNOSTICS,0,0};
}
using RuntimePtr=std::unique_ptr<LuiRuntime, decltype(&Lui_Destroy)>;
static RuntimePtr Runtime(Recorder& Value) {
    RuntimePtr Result(Lui_Create(),Lui_Destroy); Check(bool(Result),"runtime creation failed");
    LuiBackendCallbacks Backend{&Value,Create,Property,Parent,Arrange,Destroy}; Lui_SetBackend(Result.get(),Backend);
    Lui_SetLogCallback(Result.get(),&Value,Log);
    LuiCapabilityDeclarationV1 Grants{sizeof(Grants),LUI_EXTENSION_ABI_VERSION,
        LUI_CAPABILITY_HOST_SERVICES|LUI_CAPABILITY_CLIPBOARD|LUI_CAPABILITY_DIALOGS|
        LUI_CAPABILITY_NETWORK_CLIENT|LUI_CAPABILITY_NETWORK_SERVER|LUI_CAPABILITY_NETWORK_RAW};
    Check(Lui_DeclareCapabilities(Result.get(),&Grants)==1,"host grants failed");
    Check(Lui_RegisterHostMethod(Result.get(),"Probe","Creates","(Self: Probe) -> number",Probe,&Value)==1,"host registration failed");
    LuiPlatformCallbacksV1 Platform{}; Platform.StructSize=sizeof(Platform); Platform.AbiVersion=LUI_EXTENSION_ABI_VERSION;
    Platform.Context=&Value; Platform.WriteClipboardText=Clipboard;
    Platform.ReadClipboardText=ReadClipboard;
    Check(Lui_SetPlatformCallbacks(Result.get(),&Platform)==1,"platform registration failed");
    return Result;
}
struct Fixture {
    Fs::path Root;
    Json Manifest;
    Fixture(Fs::path Root,const std::map<std::string,std::string>& Files):Root(std::move(Root)) {
        Json Names=Json::array(); for (const auto& Pair:Files) { Names.push_back(Pair.first); Write(this->Root/"Src"/Pair.first,Pair.second); }
        Manifest={{"SchemaVersion",2},{"Script","Src/Main.luau"},{"Capabilities",{"HostServices","Clipboard"}},
            {"Extensions",Json::array()},{"Assets",Json::array()},{"ExtensionDeclarations",Json::array()},
            {"Diagnostics",{{"Mode","Development"}}},{"Modules",{{"Root","Src"},{"Files",Names},
                {"Requires",Json::array()},{"Dependencies",Json::array()}}}};
    }
    LuiProject* Prepare(std::string* Failure=nullptr) {
        Write(Root/"lui.json",Manifest.dump()); const auto Path=(Root/"lui.json").u8string();
        LuiProjectOptionsV1 Options{}; Options.StructSize=sizeof(Options); Options.AbiVersion=LUI_PROJECT_ABI_VERSION;
        LuiProject* Project=nullptr; char Error[128]{};
        const bool Ok=Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Project,Error,sizeof(Error))!=0;
        if (Failure) *Failure=Error; else Check(Ok,Error);
        return Project;
    }
};
static void Run(Fixture& Input,Recorder& Value,bool Success=true,const char* Code=nullptr,LuiProjectRunOptionsV1 Options=RunOptions()) {
    std::fprintf(stderr,"[LUI:ModuleTest] Case %s\n",Input.Root.filename().string().c_str());
    auto* Project=Input.Prepare(); auto App=Runtime(Value);
    const int Status=Lui_RunProject(App.get(),Project,&Options); Lui_ProjectRelease(Project);
    if (Success) Check(Status==1,Lui_GetLastError(App.get()));
    else {
        Check(Status==0,"project unexpectedly succeeded");
        if (Code) Check(std::string(Lui_GetLastError(App.get()))==std::string("[LUI:Modules] ")+Code,Code);
        Check(std::string(Lui_GetPreviewTreeJson(App.get()))=="[]","failed bootstrap retained tree");
        Check(Lui_Pump(App.get())==0,"failed generation admitted tasks");
        Check(Lui_RunScript(App.get(),"error('OLD_VM')","Late")==0,"failed VM accepted script");
        Check(Value.Alive.empty(),"failed bootstrap retained native handles");
    }
}
static void Cases(const Fs::path& Suite) {
    {
        Recorder Value; auto App=Runtime(Value);
        Check(Lui_RunScript(App.get(),"local Ok, E = pcall(require, './X'); assert(not Ok and E == '[LUI:Modules] ProjectNotConfigured')","Legacy")==1,
            "legacy require did not report unconfigured project");
    }
    Fixture Good(Suite/"Good",{
        {"Main.luau",R"(
            Private = 'Entry'
            local Counter = require('./Counter')
            assert(require('./False') == false and require('./Sub/../False.luau') == false and Counter.False == 1)
            local Data = require('./Data'); assert(Data == require('./Data.luau') and Data.Read() == 'Data' and Private == 'Entry')
            for I=1,2 do local Ok,E=pcall(require,'./Failed'); assert(not Ok and E=='[LUI:Modules] LoadFailed') end
            assert(Counter.Failed == 1)
            for _,Name in {'Nil','Empty','Multiple'} do
                local Ok,E=pcall(require,'./'..Name); assert(not Ok and E=='[LUI:Modules] InvalidResult')
            end
            for _,Request in {'./Missing','../Escape','./Main','C:/No','./Data\0'} do assert(not pcall(require,Request)) end
            local Factory = require('./Sub/Factory')
            local Window,Button = Factory()
            assert(app:GetService('Probe'):Creates() == 0)
            Window.Destroying:Connect(function() print('OLD_DESTROY_CALLBACK'); task.defer(function() print('OLD_TASK') end) end)
            task.defer(function() assert(require('./Data') == Data); print('TASK_READY') end)
            Window.Visible = true
        )"},
        {"Counter.luau","return {False=0, Failed=0}"},
        {"False.luau","local C=require('./Counter'); C.False+=1; return false"},
        {"Failed.luau","local C=require('./Counter'); C.Failed+=1; error('PRIVATE_MODULE_CANARY')"},
        {"Nil.luau","return nil"},{"Empty.luau","local Value=3"},{"Multiple.luau","return 1,2"},
        {"Data.luau","Private='Data'; return {Read=function() return Private end}"},
        {"Sub/Factory.luau",R"(
            local function Read() return require('../Data') end
            return function()
                local Window=Instance.new('Window')
                local Button=Instance.new('TextButton',{Parent=Window})
                Button.Activated:Connect(function() assert(Read().Read()=='Data'); print('SOURCE_BOUND') end)
                return Window,Button
            end
        )"}
    });
    {
        Recorder Value; auto App=Runtime(Value); auto* Prepared=Good.Prepare(); auto Options=RunOptions();
        // Captured bytecode survives source edits and release of the host handle.
        Write(Good.Root/"Src/Data.luau","error('NEW_DISK_BYTES')");
        const int Started=Lui_RunProject(App.get(),Prepared,&Options);
        Check(Started==1,Lui_GetLastError(App.get())); Lui_ProjectRelease(Prepared);
        Check(Value.Creates==2 && Value.Properties>0 && Value.Arranges>0,"native commit did not run");
        Check(Value.Logs.empty(),"scheduled task ran before pump");
        Check(Lui_Pump(App.get())==1 && Value.Logs.back()=="TASK_READY","post-start task failed");
        Check(Lui_Activate(App.get(),2)==1 && Value.Logs.back()=="SOURCE_BOUND","exported source binding lost");
        Check(Lui_RunScript(App.get(),"return","Mixed")==0,"project accepted unrelated chunk");
        App.reset(); Check(Value.Alive.empty() && Value.Logs.size()==2,"shutdown invoked old generation callbacks");
    }
    Write(Good.Root/"Src/Data.luau","Private='Data'; return {Read=function() return Private end}");
    {
        Recorder Value; Value.FailCreate=2; Run(Good,Value,false,"LoadFailed");
        Check(Value.Creates==2 && Value.Destroys==2,"native create failure not rolled back");
    }
    {
        Recorder Value; Value.FailProperty=true; Run(Good,Value,false,"LoadFailed"); Check(Value.Destroys==2,"property commit failed cleanup");
    }
    Fixture BadStart(Suite/"BadStart",{{"Main.luau",R"(
        local Window=Instance.new('Window',{Visible=true})
        Window.Destroying:Connect(function() print('OLD_CALLBACK') end)
        task.spawn(function() print('OLD_TASK') end)
        error('PRIVATE_STARTUP_CANARY')
    )"}});
    {
        Recorder Value; Run(BadStart,Value,false,"LoadFailed");
        Check(Value.Creates==0 && Value.Destroys==0 && Value.Properties==0,"failed entry escaped native presentation buffer");
        for (const auto& Message:Value.Logs) Check(Message.find("PRIVATE")==std::string::npos && Message.find("OLD_")==std::string::npos,"startup payload/callback escaped");
    }
    // New runtimes start with fresh caches, IDs and work. Shared input bytes may
    // be reused, but exports/callbacks are never reused across generations.
    { Recorder First,Second; Run(Good,First); Run(Good,Second); Check(First.Creates==2 && Second.Creates==2,"clean generation recovery failed"); }
    const std::map<std::string,std::string> Guards{
        {"Create","Instance.new('Window')"},{"Defer","task.defer(function() end)"},{"Spawn","task.spawn(function() end)"},
        {"Delay","task.delay(1,function() end)"},{"Service","app:GetService('Probe')"},
        {"Mutate","Shared.Node.Text='bad'"},{"Destroy","Shared.Node:Destroy()"},{"Clone","Shared.Node:Clone()"},
        {"Connect","Shared.Node.Activated:Connect(function() end)"},{"Disconnect","Shared.Connection:Disconnect()"},
        {"Platform","Shared.Clipboard:WriteText('bad')"},{"Host","Shared.Host:Creates()"}
        ,{"ReadClipboard","Shared.Clipboard:ReadText(function() end)"}
        ,{"Tcp","Shared.Network:ListenTcp({})"},{"Udp","Shared.Network:BindUdp({})"}
        ,{"Http","Shared.Http:GetAsync('http://127.0.0.1/')"},{"HttpCancel","Shared.Http:CancelAll()"}
        ,{"Server","Shared.Server:CreateServer({})"}
    };
    std::map<std::string,std::string> GuardFiles{{"Shared.luau","return {}"}};
    std::string Main=R"(
        local Shared=require('./Shared')
        local Window=Instance.new('Window')
        Shared.Node=Instance.new('TextButton',{Parent=Window})
        Shared.Connection=Shared.Node.Activated:Connect(function() print('CONNECTION_ALIVE') end)
        Shared.Clipboard=app:GetService('ClipboardService'); Shared.Host=app:GetService('Probe')
        Shared.Network=app:GetService('NetworkService'); Shared.Http=app:GetService('HttpService')
        Shared.Server=app:GetService('HttpServerService')
    )";
    for (const auto& Pair:Guards) {
        GuardFiles[Pair.first+".luau"]="local Shared=require('./Shared'); "+Pair.second+"; return true";
        Main+="for I=1,2 do local Ok,E=pcall(require,'./"+Pair.first+"'); assert(not Ok and E=='[LUI:Modules] InitializationEffect') end\n";
    }
    GuardFiles["Yield.luau"]="coroutine.yield(); return true";
    Main+="local Ok,E=pcall(require,'./Yield'); assert(not Ok and E=='[LUI:Modules] YieldNotAllowed')\n";
    GuardFiles["Pure.luau"]="return {Size=UDim2.fromOffset(8,9), Vector=Vector2.new(2,3)}";
    Main+="assert(require('./Pure').Size.X.Offset==8 and Shared.Node.Text==''); Window.Visible=true\n";
    GuardFiles["Main.luau"]=Main;
    Fixture GuardCase(Suite/"Guards",GuardFiles);
    GuardCase.Manifest["Capabilities"]={"HostServices","Clipboard","network.client","network.server","network.raw"};
    {
        Recorder Value; auto App=Runtime(Value); auto* Prepared=GuardCase.Prepare(); auto Options=RunOptions();
        const int Started=Lui_RunProject(App.get(),Prepared,&Options);
        Check(Started==1,Lui_GetLastError(App.get())); Lui_ProjectRelease(Prepared);
        Check(Value.HostCalls==0 && Value.Creates==2 && Lui_Pump(App.get())==0,"initializer allocated forbidden work");
        Check(Lui_Activate(App.get(),2)==1 && Value.Logs.back()=="CONNECTION_ALIVE","guard disconnected callback");
    }
    Fixture Dynamic(Suite/"DynamicCycle",{
        {"Main.luau","for _,Name in {'A','B'} do local Ok,E=pcall(require,'./'..Name); assert(not Ok and E=='[LUI:Modules] Cycle') end"},
        {"A.luau","local R='./B'; pcall(require,R); return {Partial=true}"},
        {"B.luau","local R='./A'; pcall(require,R); return {Partial=true}"}
    }); { Recorder Value; Run(Dynamic,Value); }
    Fixture Literal(Suite/"LiteralCycle",{
        {"Main.luau","return"},{"A.luau","return function() return require('./B') end"},{"B.luau","return function() return require('./A') end"}
    });
    { std::string Error; Check(!Literal.Prepare(&Error) && Error=="[LUI:Project] Cycle","deferred literal cycle accepted"); }
    Fixture Shadow(Suite/"Shadow",{{"Main.luau",R"(
        local function F(require) assert(require('./Missing')==7) end
        F(function() return 7 end)
        local require=function() return 9 end; assert(require('./NoFile')==9)
    )"}}); { Recorder Value; Run(Shadow,Value); }
    Fixture Rebind(Suite/"Rebind",{{"Main.luau","require=function() return 5 end; assert(require('./NoFile')==5)"}});
    { Recorder Value; Run(Rebind,Value); }
    Fixture GlobalTable(Suite/"GlobalTable",{{"Main.luau","_G.require=function() return 6 end; assert(require('./NoFile')==6)"}});
    { Recorder Value; Run(GlobalTable,Value); }
    Fixture Alias(Suite/"Alias",{{"Main.luau","local A=require('@Kit/Factory'); assert(A==require('@Kit/Factory.luau')); assert(A.Read().Value==17)"}});
    const std::string Factory="return {Read=function() return require('./State') end}", State="return {Value=17}";
    Write(Alias.Root/"Vendor/Kit/Factory.luau",Factory); Write(Alias.Root/"Vendor/Kit/State.luau",State);
    Alias.Manifest["Modules"]["Requires"]={"Kit"};
    Alias.Manifest["Modules"]["Dependencies"]=Json::array({{{"Alias","Kit"},{"Root","Vendor/Kit"},
        {"Files",{"Factory.luau","State.luau"}},{"Requires",Json::array()}}});
    Alias.Manifest["Modules"]["Lock"]="Modules.lock.json";
    Write(Alias.Root/"Modules.lock.json",Json{{"SchemaVersion",1},{"Packages",Json::array({{{"Alias","Kit"},
        {"Provenance",{{"Kind","LocalVendor"},{"Id","Fixture-1"}}},{"Files",Json::array({
            {{"Path","Factory.luau"},{"Sha256",Lui::Project::Hash(Factory)}},
            {{"Path","State.luau"},{"Sha256",Lui::Project::Hash(State)}}})}}})}}.dump());
    { Recorder Value; Run(Alias,Value); }
    Fixture MissingLiteral(Suite/"MissingLiteral",{{"Main.luau","return"},{"Unused.luau","return require('./Missing')"}});
    { std::string Error; Check(!MissingLiteral.Prepare(&Error) && Error=="[LUI:Project] MissingModule","unused literal import not validated"); }
    std::map<std::string,std::string> Chain{{"Main.luau","local Ok,E=pcall(require,'./M0'); assert(not Ok and E=='[LUI:Modules] LimitExceeded')"}};
    for (int Index=0; Index<65; ++Index) Chain["M"+std::to_string(Index)+".luau"]="local R='./M"+std::to_string(Index+1)+"'; return require(R)";
    Chain["M65.luau"]="return true";
    Fixture Depth(Suite/"Depth",Chain); { Recorder Value; Run(Depth,Value); }
    Fixture Quota(Suite/"Quota",{{"Main.luau","local R='./Loop'; return require(R)"},{"Loop.luau","while true do end"}});
    Quota.Manifest["Sandbox"]={{"MaxMemoryBytes",1024*1024},{"MaxInterrupts",5}};
    { Recorder Value; Run(Quota,Value,false,"LimitExceeded"); }
    // Each initializer fits by itself; nested evaluation must spend one shared
    // execution budget rather than resetting it at every require.
    const std::string Work="local N=0; for I=1,100 do N+=I end; ";
    Fixture SingleQuota(Suite/"SingleQuota",{{"Main.luau","require('./Work')"},{"Work.luau",Work+"return true"}});
    SingleQuota.Manifest["Sandbox"]={{"MaxMemoryBytes",1024*1024},{"MaxInterrupts",500}};
    { Recorder Value; Run(SingleQuota,Value); }
    std::map<std::string,std::string> WorkFiles{{"Main.luau","require('./W0')"}};
    for (int Index=0; Index<10; ++Index) WorkFiles["W"+std::to_string(Index)+".luau"]=Work+
        (Index==9 ? "return true" : "return require('./W"+std::to_string(Index+1)+"')");
    Fixture SharedQuota(Suite/"SharedQuota",WorkFiles); SharedQuota.Manifest["Sandbox"]=SingleQuota.Manifest["Sandbox"];
    { Recorder Value; Run(SharedQuota,Value,false,"LimitExceeded"); }
    Fixture Memory(Suite/"Memory",{{"Main.luau","local Ok,E=pcall(require,'./Large'); assert(not Ok and E=='[LUI:Modules] LimitExceeded'); assert(Instance.new('Window'))"},
        {"Large.luau","return string.rep('x',2*1024*1024)"}});
    Memory.Manifest["Sandbox"]={{"MaxMemoryBytes",1024*1024},{"MaxInterrupts",1000}};
    { Recorder Value; Run(Memory,Value); }
    Fixture Coroutine(Suite/"Coroutine",{{"Main.luau",R"(
        assert(require('./Check'))
        local Thread=coroutine.create(function() coroutine.yield(8); return 9 end)
        local Ok,Value=coroutine.resume(Thread); assert(Ok and Value==8)
        Ok,Value=coroutine.resume(Thread); assert(Ok and Value==9)
    )"},{"Check.luau",R"(
        local Ok,Error=coroutine.resume(coroutine.create(function() coroutine.yield() end))
        assert(not Ok and Error=='[LUI:Modules] YieldNotAllowed'); return true
    )"}}); { Recorder Value; Run(Coroutine,Value); }
    {
        Recorder Value; auto App=Runtime(Value); auto* Prepared=Good.Prepare(); auto Options=RunOptions();
        Options.Flags=0; Check(!Lui_RunProject(App.get(),Prepared,&Options),"missing diagnostic opt-in accepted");
        Options=RunOptions(); ++Options.AbiVersion; Check(!Lui_RunProject(App.get(),Prepared,&Options),"unknown run ABI accepted");
        Options=RunOptions(); --Options.StructSize; Check(!Lui_RunProject(App.get(),Prepared,&Options),"invalid run size accepted");
        Options=RunOptions(); Options.Reserved=1; Check(!Lui_RunProject(App.get(),Prepared,&Options),"reserved run bit accepted");
        Options=RunOptions(); Options.MaxImportDepth=65; Check(!Lui_RunProject(App.get(),Prepared,&Options),"depth ceiling raised");
        Options=RunOptions(); bool Accepted=true;
        std::thread Worker([&] { Accepted=Lui_RunProject(App.get(),Prepared,&Options)!=0; }); Worker.join();
        Check(!Accepted,"worker entered VM"); Lui_ProjectRelease(Prepared);
    }
    Fixture Restricted(Suite/"Restricted",{{"Main.luau","return"}}); Restricted.Manifest["Diagnostics"]["Mode"]="Restricted";
    { Recorder Value; Run(Restricted,Value,false,"PolicyNotSupported"); }
    {
        Recorder Value; auto App=RuntimePtr(Lui_Create(),Lui_Destroy); auto* Prepared=Good.Prepare(); auto Options=RunOptions();
        Check(!Lui_RunProject(App.get(),Prepared,&Options) && std::string(Lui_GetLastError(App.get()))=="[LUI:Modules] CapabilityDenied",
            "manifest granted its own capabilities"); Lui_ProjectRelease(Prepared);
    }
}
int main() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    const auto Suite=Fs::temp_directory_path()/("LuiModuleTests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fs::create_directory(Suite);
    try {
        Cases(Suite); Fs::remove_all(Suite); // Fresh fixture tree, no links or external targets.
        std::puts("[LUI:ModuleTest] Cache, source binding, guards, cycles, quotas and generation cleanup passed"); return 0;
    } catch (const std::exception& Error) { std::fprintf(stderr,"[LUI:ModuleTest] %s\n",Error.what()); return 1; }
}
