#include "../../backends/mobile/Host.h"
#include "../../backends/mobile/Storage.h"
#include "../../runtime/services/StorageFiles.h"
#include "../../runtime/internal/NativeServices.h"
#include "../../runtime/internal/Scheduler.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#if !defined(_WIN32)
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace Fs = std::filesystem;
using Json = nlohmann::json;
void CheckAt(bool Value, int Line) { if (!Value) { std::cerr << "[LUI:MobilePersistenceTest] Assertion " << Line << '\n'; throw std::runtime_error("Assertion"); } }
#define Check(Value) CheckAt((Value), __LINE__)
std::string Read(const Fs::path& Path) {
    std::ifstream File(Path, std::ios::binary); Check(bool(File));
    return {std::istreambuf_iterator<char>(File), {}};
}
int main() {
    const auto Root = Fs::temp_directory_path() / ("LuiMobileStorage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        Fs::create_directory(Root);
        // Recovery removes exactly the provider's unpublished temporary while
        // holding its lock. It neither promotes bytes nor bypasses unsafe files.
        { Lui::Native::StorageFiles Store(Root); Store.Write("checkpoint", "previous", [] { return false; });
          Check(!Lui::Mobile::RecoverStorage(Root.string())); }
        { std::ofstream Temporary(Root / ".lui-write"); Temporary << "partial"; }
        Check(Lui::Mobile::RecoverStorage(Root.string()) && !Fs::exists(Root / ".lui-write"));
        { Lui::Native::StorageFiles Store(Root); std::string Bytes; Check(Store.Read("checkpoint", Bytes) && Bytes == "previous"); Store.Remove("checkpoint"); }
#if !defined(_WIN32)
        { Lui::Native::StorageFiles Store(Root); Store.Write("checkpoint", "committed", [] { return false; }); }
        int Barrier[2]; Check(pipe(Barrier) == 0);
        const auto Child = fork(); Check(Child >= 0);
        if (Child == 0) {
            close(Barrier[0]);
            Lui::Native::StorageFiles Store(Root);
            int Checks = 0;
            Store.Write("checkpoint", std::string(131072, 'x'), [&] {
                if (++Checks < 3) return false;
                const char Ready = 'R'; if (write(Barrier[1], &Ready, 1) != 1) _exit(2);
                for (;;) pause();
                return false;
            });
            _exit(3);
        }
        close(Barrier[1]); char Ready = 0; Check(read(Barrier[0], &Ready, 1) == 1 && Ready == 'R'); close(Barrier[0]);
        Check(kill(Child, SIGKILL) == 0); int Status = 0; Check(waitpid(Child, &Status, 0) == Child && WIFSIGNALED(Status));
        Check(Fs::exists(Root / ".lui-write") && Lui::Mobile::RecoverStorage(Root.string()));
        { Lui::Native::StorageFiles Store(Root); std::string Bytes; Check(Store.Read("checkpoint", Bytes) && Bytes == "committed"); Store.Remove("checkpoint"); }
#endif
        Fs::create_symlink(Root / "K-checkpoint.bin", Root / ".lui-write");
        Check(!Lui::Mobile::RecoverStorage(Root.string())); Fs::remove(Root / ".lui-write");
        Fs::create_directory(Root / ".lui-write");
        Check(!Lui::Mobile::RecoverStorage(Root.string())); Fs::remove(Root / ".lui-write");

        auto* Domain = Lui_CreateNativeDomainV2(); auto* Runtime = Lui_Create(); Check(Runtime && Domain);
        LuiCapabilityDeclarationV1 Grants{sizeof(Grants), 1, LUI_CAPABILITY_STORAGE};
        Check(Lui_DeclareCapabilities(Runtime, &Grants) && ConfigureBoundedTasks(Runtime, 32));
        Check(!Lui_SetNativeDomainV2(Runtime, Domain) && !Lui_InstallStorageV1(Runtime, Root.string().c_str()));
        Check(Lui_RunScript(Runtime,
            "N=0; local S=app:GetService('StorageService'); for I=1,12 do "
            "S:Open(buffer.fromstring('key'..I),nil,function(_,H,E) assert(H and not E); H:Close(); N+=1; "
            "task.defer(function() N+=100 end) end) end", "CompletionBudget"));
        auto Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        LuiNativeStatsV2 Stats{sizeof(Stats), 2};
        // Terminal outcomes live on requests; Events counts progress only.
        // Every completed Open owns a handle, and worker leases drain only after
        // publishing its terminal result. The binding retains one base lease.
        do { Check(!Lui_GetNativeStatsV2(Domain, &Stats)); if (Stats.Handles == 12 && Stats.Leases == 1) break; std::this_thread::yield(); } while (std::chrono::steady_clock::now() < Until);
        Check(Stats.Handles == 12 && Stats.Leases == 1);
        Check(DrainNativeServices(Runtime, 8, std::chrono::steady_clock::now()) == 0);
        Check(DrainNativeServices(Runtime, 8) == 8);
        Check(Lui_RunScript(Runtime, "assert(N==8)", "EightOnly"));
        Check(DrainNativeServices(Runtime, 8) == 4);
        Check(Lui_RunScript(Runtime, "assert(N==12)", "ExactlyOnce"));
        Check(PumpBoundedTasks(Runtime, 32, std::chrono::steady_clock::now() + std::chrono::seconds(1)));
        Check(Lui_RunScript(Runtime, "assert(N==1212)", "DeferredOrder"));
        Lui_Destroy(Runtime);
        Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do { Lui_PumpNativeDomainV2(Domain); Lui_GetNativeStatsV2(Domain, &Stats); if (!Stats.Sessions) break; std::this_thread::yield(); } while (std::chrono::steady_clock::now() < Until);
        Check(!Stats.Sessions && !Stats.Leases && !Stats.Bytes && !Stats.ReservedBytes && !Stats.Handles && !Stats.Requests);
        Lui_ReleaseNativeDomainV2(Domain);

        const Fs::path Fixture = LUI_MOBILE_CONTROLS;
        Json Manifest = Json::parse(Read(Fixture / "lui.json"));
        Manifest["Capabilities"] = {"Storage"}; Manifest["Assets"] = Json::array();
        Manifest["Modules"]["Files"] = {"Main.luau", "Offline.luau"};
        std::map<std::string, std::string> Files{{"lui.json", Manifest.dump()},
            {"Src/Main.luau", "local W=Instance.new('Window',{Visible=true}); require('./Offline').Mount(W)"},
            {"Src/Offline.luau", Read(Fixture / "Src/Offline.luau")}};
        const auto Bundle = Lui::Mobile::Host::PrepareBundle(Files);
        for (int Cycle = 0; Cycle < 5; ++Cycle) {
            if (Cycle == 2) { Lui::Native::StorageFiles Store(Root); Store.Write("checkpoint", "LUI-CHECKPOINT\n1\n7\n", [] { return false; }); }
            if (Cycle == 3) { Lui::Native::StorageFiles Store(Root); Store.Write("checkpoint", "LUI-CHECKPOINT\n99\n7\n", [] { return false; }); }
            if (Cycle == 4) { Lui::Native::StorageFiles Store(Root); Store.Write("checkpoint", "LUI-CHECKPOINT\n2\n0007\n", [] { return false; }); }
            Lui::Mobile::Host Host(Cycle + 1); Check(Host.Start(Bundle, Root.string()) && Host.Attach(1, 400, 1000));
            Json Frame; std::string Text;
            const auto Expected = Cycle < 2 ? "Committed 0" : Cycle == 2 ? "Committed 7" : "Checkpoint refused";
            Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do {
                const auto Bytes = Host.Take();
                if (!Bytes.empty()) {
                    Frame = Json::parse(Bytes); Check(Host.Acknowledge(1, Frame.at("Revision"), true));
                    for (const auto& Node : Frame.at("Nodes")) if (Node.at("Name") == "CheckpointState") Text = Node.at("Properties").at("Text");
                }
                if (Text == Expected) break;
                Check(Host.Pump()); std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (std::chrono::steady_clock::now() < Until);
            Check(Text == Expected);
            Until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!Host.Retire()) { Check(std::chrono::steady_clock::now() < Until); std::this_thread::yield(); }
        }
        Fs::remove_all(Root);
        std::cout << "[LUI:MobilePersistenceTest] interrupted recovery, safe refusal, bounded completion/order, clean retirement, checkpoint migration/corruption passed\n";
        return 0;
    } catch (...) { std::cerr << "[LUI:MobilePersistenceTest] Failed\n"; return 1; }
}
