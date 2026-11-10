#include "../../backends/mobile/Host.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <cstdlib>
#include <algorithm>

namespace Fs = std::filesystem;
using Json = nlohmann::json;
void CheckAt(bool Value, int Line) { if (!Value) { std::cerr << "[LUI:MobileWorkloadTest] Assertion " << Line << '\n'; throw std::runtime_error("Assertion"); } }
#define Check(Value) CheckAt((Value), __LINE__)
std::string Read(const Fs::path& Path) { std::ifstream Input(Path, std::ios::binary); Check(bool(Input)); return {std::istreambuf_iterator<char>(Input), {}}; }
std::map<std::string, std::string> Files(const Fs::path& Root) {
    std::map<std::string, std::string> Result;
    for (const auto& File : Fs::recursive_directory_iterator(Root)) if (File.is_regular_file()) Result.emplace(Fs::relative(File.path(), Root).generic_string(), Read(File.path()));
    return Result;
}
struct Driver {
    Lui::Mobile::Host Host;
    Json Frame;
    uint32_t Sequence = 0;
    explicit Driver(uint32_t Generation, const std::string& Bundle) : Host(Generation) {
        Check(Host.Start(Bundle) && Host.Attach(1, 412, 820)); Take();
    }
    void Take() { const auto Bytes = Host.Take(); if (Bytes.empty()) return; Frame = Json::parse(Bytes); Check(Host.Acknowledge(1, Frame.at("Revision"), true)); }
    Json Find(const std::string& Name) const { for (const auto& Node : Frame.at("Nodes")) if (Node.at("Name") == Name) return Node; throw std::runtime_error("Node"); }
    int Send(const Json& Node, Json Event, uint32_t Revision = 0) {
        Event["Observation"] = Node.at("Observation");
        if (Event.at("Kind") == "Activate") Event["Content"] = Node.at("Content");
        const int Result = Host.Event(1, Revision ? Revision : Frame.at("Revision").get<uint32_t>(), ++Sequence, Node.at("Id"), Event.dump());
        Check(Result >= 0); Take(); return Result;
    }
    void Click(const std::string& Name) { Check(Send(Find(Name), {{"Kind", "Activate"}}) == 1); }
};
int main(int Count, char** Args) {
    try {
        const char* Capture = std::getenv("LUI_MOBILE_CAPTURES"); Check(Capture);
        std::map<std::string, std::string> Bundles;
        for (const char* Name : {"Conversation", "Modular"}) Bundles[Name] = Lui::Mobile::Host::PrepareBundle(Files(Fs::path(Capture) / Name));
        if (Count == 3 && std::string(Args[1]) == "--bundle") {
            for (const auto& Pair : Bundles) { std::ofstream Output(Fs::path(Args[2]) / (Pair.first + ".json"), std::ios::binary); Output << Pair.second; Check(bool(Output)); }
            return 0;
        }
        Driver Conversation(1, Bundles.at("Conversation"));
        for (int Turn = 0; Turn < 40; ++Turn) { Check(Conversation.Host.Pump()); Conversation.Take(); }
        Check(Conversation.Find("WorkloadStatus").at("Properties").at("Text").get<std::string>().find("10000 items") == 0);
        const auto Composer = Conversation.Find("Composer"); const uint32_t Seen = Conversation.Frame.at("Revision");
        Check(Conversation.Send(Composer, {{"Kind", "Focus"}, {"Value", true}}) == 1);
        Check(Conversation.Send(Composer, {{"Kind", "Composition"}, {"Phase", 0}, {"Text", "e\xcc\x81"}, {"InputRevision", Composer.at("TextState").at("InputRevision")}}) == 1);
        for (int Index = 0; Index < 200; ++Index) {
            Conversation.Click(Index % 2 ? "Edit" : "Insert");
            Check(Conversation.Find("Composer").at("Observation") == Composer.at("Observation"));
            Check(Conversation.Find("Composer").at("TextState").at("IsComposing") == true);
            const auto Metrics = Json::parse(Conversation.Host.Metrics());
            Check(Metrics.at("NativeNodes") <= 128 && Metrics.at("RetainedBytes") <= 192 * 1024);
            Check(Metrics.at("Runtime").at("LuaMemoryBytes") <= 32 * 1024 * 1024);
        }
        Check(Conversation.Send(Composer, {{"Kind", "Composition"}, {"Phase", 2}, {"Text", ""}, {"InputRevision", Composer.at("TextState").at("InputRevision")}}, Seen) == 1);
        const auto Image = *std::find_if(Conversation.Frame.at("Nodes").begin(), Conversation.Frame.at("Nodes").end(),
            [](const auto& Value) { return Value.at("Class") == "ImageLabel"; });
        const auto BeforeImage = Conversation.Frame.at("Revision");
        const Json ImageEvent{{"Kind", "Image"}, {"Token", Image.at("Image").at("Token")}, {"Success", true}};
        Check(Conversation.Host.Event(1, 0, 0, Image.at("Id"), ImageEvent.dump()) == 1);
        Conversation.Take(); Check(Conversation.Frame.at("Revision") != BeforeImage);
        for (const auto& Value : Conversation.Frame.at("Nodes")) if (Value.at("Id") == Image.at("Id")) Check(Value.at("LoadState") == "Ready");
        Check(Conversation.Host.Event(1, 0, 0, Image.at("Id"), ImageEvent.dump()) == 0);
        Check(Conversation.Host.Take().empty());
        const auto Row = Conversation.Find("Message-1");
        Conversation.Send(Row, {{"Kind", "Focus"}, {"Value", true}});
        Conversation.Click("Insert");
        Check(Conversation.Find("Message-1").at("Id") == Row.at("Id"));
        Check(Conversation.Find("Message-1").at("Focused") == true);
        Conversation.Click("Bottom");
        Check(Conversation.Host.Event(1, Conversation.Frame.at("Revision"), ++Conversation.Sequence, Row.at("Id"), "{\"Kind\":\"Activate\"}") == 0);
        Conversation.Take();
        Conversation.Click("Top"); Conversation.Click("SettingsPageButton"); Conversation.Click("HistoryPage");
        Check(Conversation.Host.Retire());
        const auto Retired = Json::parse(Conversation.Host.Metrics()); Check(Retired.at("NativeNodes") == 0 && Retired.at("RetainedBytes") == 0);
        Driver Modular(2, Bundles.at("Modular"));
        Modular.Click("Increment"); Check(Modular.Find("CounterStatus").at("Properties").at("Text") == "Actions: 1");
        const auto Old = Modular.Find("Increment"); Modular.Click("ShowLibrary"); Modular.Click("Project_Workshop");
        Check(Modular.Find("Selection").at("Properties").at("Text") == "Selected: Workshop");
        Modular.Click("ShowHome"); Check(Modular.Find("CounterStatus").at("Properties").at("Text") == "Actions: 1");
        Check(Modular.Host.Event(1, Modular.Frame.at("Revision"), ++Modular.Sequence, Old.at("Id"), "{\"Kind\":\"Activate\"}") == 0);
        std::cout << "[LUI:MobileWorkloadTest] 10000-item pinned collection, 200 mixed transitions, composition/anchoring, retired callbacks, shared modular navigation and bounds passed\n";
        return 0;
    } catch (...) { std::cerr << "[LUI:MobileWorkloadTest] Failed\n"; return 1; }
}
