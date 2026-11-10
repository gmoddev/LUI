#include "../../backends/mobile/Host.h"
#include "../../runtime/project/Project.h"
#include "../../backends/mobile/Encoding.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <algorithm>

using Json = nlohmann::json;
void CheckAt(bool Value, int Line) { if (!Value) { std::cerr << "[LUI:MobileTest] Assertion " << Line << '\n'; throw std::runtime_error("Assertion"); } }
#define Check(Value) CheckAt((Value), __LINE__)
std::map<std::string, std::string> Files() {
    std::map<std::string, std::string> Result;
    for (const char* Name : {"lui.json", "Src/Main.luau"}) {
        std::ifstream Input(std::string(LUI_MOBILE_FIXTURE) + "/" + Name, std::ios::binary);
        Check(bool(Input));
        Result[Name] = {std::istreambuf_iterator<char>(Input), {}};
    }
    return Result;
}
std::string Read(const std::string& Path) {
    std::ifstream Input(Path, std::ios::binary); Check(bool(Input));
    return {std::istreambuf_iterator<char>(Input), {}};
}
std::map<std::string, std::string> Controls() {
    std::map<std::string, std::string> Result;
    const std::string Root = std::string(LUI_MOBILE_FIXTURE) + "/../controls/";
    for (const char* Name : {"lui.json", "Src/Main.luau", "Src/Counter.luau", "Src/Offline.luau"}) Result[Name] = Read(Root + Name);
    auto Pixel = Read(Root + "Pixel.base64");
    Pixel.erase(std::remove_if(Pixel.begin(), Pixel.end(), [](char Value) { return Value == '\r' || Value == '\n'; }), Pixel.end());
    Result["Pixel.png"] = Lui::Mobile::Decode(Pixel);
    return Result;
}
int main(int Count, char** Args) {
    try {
        const auto Input = Files();
        const auto Bundle = Lui::Mobile::Host::PrepareBundle(Input);
        if (Count == 3 && std::string(Args[1]) == "--bundle") {
            auto Android = Controls();
            auto Manifest = Json::parse(Android.at("lui.json")); Manifest["Capabilities"] = {"Storage"};
            Android["lui.json"] = Manifest.dump();
            std::ofstream Output(Args[2], std::ios::binary); Output << Lui::Mobile::Host::PrepareBundle(Android);
            Check(bool(Output)); return 0;
        }
        Check(Lui::Project::Hash("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        for (uint32_t Cycle = 1; Cycle <= 100; ++Cycle) {
            Lui::Mobile::Host Host(Cycle);
            Check(Host.Start(Bundle)); Check(!Host.Start(Bundle));
            bool Rejected = false;
            std::thread Foreign([&] { Rejected = !Host.Attach(1, 360, 640) && !Host.Retire() && Host.Take().empty(); });
            Foreign.join(); Check(Rejected);
            Check(Host.Attach(1, 360, 640));
            auto Frame = Json::parse(Host.Take()); Check(Frame.at("Generation") == Cycle);
            const auto Revision = Frame.at("Revision").get<uint32_t>();
            int Button = 0;
            for (const auto& Node : Frame.at("Nodes")) if (Node.at("Class") == "TextButton") Button = Node.at("Id");
            Check(Button > 0 && Host.Take().empty());
            Check(!Host.Activate(1, Revision, 1, Button));
            Check(!Host.Acknowledge(2, Revision, true));
            Check(Host.Acknowledge(1, Revision, true));
            Check(Host.Activate(1, Revision, 1, Button));
            Check(!Host.Activate(1, Revision, 1, Button));
            Frame = Json::parse(Host.Take());
            for (const auto& Node : Frame.at("Nodes")) if (Node.at("Id") == Button)
                Check(Node.at("Properties").at("Text") == "Count 1");
            Check(Host.Detach(1)); Check(Host.Take().empty());
            Check(Host.Attach(2, 640, 360));
            Check(!Host.Acknowledge(1, Frame.at("Revision"), true));
            Frame = Json::parse(Host.Take());
            Check(Frame.at("SurfaceEpoch") == 2);
            for (const auto& Node : Frame.at("Nodes")) {
                if (Node.at("Id") == Button) Check(Node.at("Properties").at("Text") == "Count 1");
                if (Node.at("Class") == "Window") Check(Node.at("Bounds")[2] == 640 && Node.at("Bounds")[3] == 360);
            }
            Check(Host.Acknowledge(2, Frame.at("Revision"), true));
            Check(Host.Detach(2) && Host.Attach(3, 640, 360));
            Frame = Json::parse(Host.Take()); Check(Frame.at("SurfaceEpoch") == 3);
            Check(!Host.Acknowledge(3, Frame.at("Revision"), false));
            Check(!Host.Attach(4, 360, 640)); Check(Host.Retire() && Host.Retire());
        }
        auto Broken = Json::parse(Bundle); Broken["CompiledDigest"] = std::string(64, '0');
        Lui::Mobile::Host Invalid(101); Check(!Invalid.Start(Broken.dump()));
        Broken = Json::parse(Bundle); Broken["Files"]["Src/Main.luau"] = "while true do end";
        Lui::Mobile::Host Tampered(102); Check(!Tampered.Start(Broken.dump()));
        Broken = Json::parse(Bundle); Broken["Version"] = 2.0;
        Lui::Mobile::Host FractionalVersion(105); Check(!FractionalVersion.Start(Broken.dump()));
        auto Loop = Input; Loop["Src/Main.luau"] = "while true do end";
        Lui::Mobile::Host Quota(103); Check(!Quota.Start(Lui::Mobile::Host::PrepareBundle(Loop)));
        Lui::Mobile::Host ControlHost(104);
        Check(ControlHost.Start(Lui::Mobile::Host::PrepareBundle(Controls())) && ControlHost.Attach(1, 360, 640));
        auto ControlFrame = Json::parse(ControlHost.Take());
        Check(ControlFrame.at("Nodes").size() > 15);
        Check(ControlHost.Acknowledge(1, ControlFrame.at("Revision"), true));
        {
            Lui::Mobile::Host InputHost(107);
            Check(InputHost.Start(Lui::Mobile::Host::PrepareBundle(Controls())) && InputHost.Attach(1, 360, 900));
            auto Frame = Json::parse(InputHost.Take());
            Check(InputHost.Acknowledge(1, Frame.at("Revision"), true));
            uint32_t Sequence = 0;
            auto Find = [&](const char* Name) -> Json {
                for (const auto& Node : Frame.at("Nodes")) if (Node.at("Name") == Name) return Node;
                throw std::runtime_error("Node");
            };
            auto Input = [&](const char* Name, Json Data, uint32_t Seen = 0) {
                const auto Node = Find(Name);
                if (!Data.contains("Observation")) Data["Observation"] = Node.at("Observation");
                if (Data.at("Kind") == "Activate" || Data.at("Kind") == "Checked" || Data.at("Kind") == "Value") Data["Content"] = Node.at("Content");
                const int Result = InputHost.Event(1, Seen ? Seen : Frame.at("Revision").get<uint32_t>(), ++Sequence, Node.at("Id"), Data.dump());
                Frame = Json::parse(InputHost.Take()); Check(InputHost.Acknowledge(1, Frame.at("Revision"), true));
                return Result;
            };
            const auto InitialFrame = Frame.at("Revision").get<uint32_t>();
            Check(Find("CounterButton").at("Observation") != Find("Toggle").at("Observation"));
            Check(Input("CounterButton", {{"Kind", "Pointer"}, {"Phase", 0}, {"Device", 2}, {"PointerId", 7}, {"X", 8}, {"Y", 8}}) == 1);
            Check(Input("Toggle", {{"Kind", "Pointer"}, {"Phase", 0}, {"Device", 2}, {"PointerId", 7}, {"X", 8}, {"Y", 8}}) == 0);
            Check(Input("CounterButton", {{"Kind", "Pointer"}, {"Phase", 0}, {"Device", 2}, {"PointerId", 11}, {"X", 16}, {"Y", 8}}, InitialFrame) == 1);
            Check(Input("CounterButton", {{"Kind", "Pointer"}, {"Phase", 2}, {"Device", 2}, {"PointerId", 7}, {"X", 9}, {"Y", 8}}, InitialFrame) == 1);
            Check(Input("CounterButton", {{"Kind", "Pointer"}, {"Phase", 2}, {"Device", 2}, {"PointerId", 11}, {"X", 16}, {"Y", 8}, {"Observation", std::string(64, '0')}}, InitialFrame) == 1);
            Check(Find("InputCounts").at("Properties").at("Text") == "B2 E2 C1 K0");
            Check(Input("CounterButton", {{"Kind", "Focus"}, {"Value", true}}) == 1);
            Check(Input("CounterButton", {{"Kind", "Key"}, {"Phase", 0}, {"Key", "ArrowLeft"}}) == 1);
            Check(Input("CounterButton", {{"Kind", "Key"}, {"Phase", 2}, {"Key", "ArrowLeft"}}, InitialFrame) == 1);
            const auto Revision = Find("Editor").at("TextState").at("InputRevision");
            Check(Input("Editor", {{"Kind", "Composition"}, {"Phase", 0}, {"Text", "e\xcc\x81"}, {"InputRevision", Revision}, {"EditId", 1}}) == 1);
            Check(Find("Editor").at("TextState").at("IsComposing") == true);
            Check(Find("Editor").at("TextState").at("Text") == "");
            Check(Input("Editor", {{"Kind", "Composition"}, {"Phase", 2}, {"Text", ""}, {"InputRevision", Revision}, {"EditId", 2}}) == 1);
            const auto TextFrame = Frame.at("Revision").get<uint32_t>();
            Check(Input("Editor", {{"Kind", "Text"}, {"Text", "a"}, {"Start", 1}, {"Length", 0}, {"InputRevision", Revision}, {"EditId", 3}}) == 1);
            Check(Input("Editor", {{"Kind", "Text"}, {"Text", "ab"}, {"Start", 2}, {"Length", 0}, {"InputRevision", Revision}, {"EditId", 4}}, TextFrame) == 1);
            Check(Find("Editor").at("TextState").at("EditId") == 4 && Find("Editor").at("TextState").at("Text") == "ab");
            Check(Input("ResetInputButton", {{"Kind", "Activate"}}) == 1);
            Check(Input("Editor", {{"Kind", "Text"}, {"Text", "old"}, {"Start", 3}, {"Length", 0}, {"InputRevision", Revision}, {"EditId", 5}}, TextFrame) == 0);
            Check(Find("Editor").at("TextState").at("Text") == "Reset");
            Check(Input("Toggle", {{"Kind", "Checked"}, {"Value", true}}) == 1);
            Check(Find("Toggle").at("Properties").at("Checked") == "true");
            const auto RangeFrame = Frame.at("Revision").get<uint32_t>();
            Check(Input("Range", {{"Kind", "Value"}, {"Value", 21}}) == 1);
            Check(Input("Range", {{"Kind", "Value"}, {"Value", 30}}, RangeFrame) == 1);
            Check(std::stod(Find("Range").at("Properties").at("Value").get<std::string>()) == 30);
            Check(Input("InnerScroller", {{"Kind", "Scroll"}, {"X", 0}, {"Y", 150}}) == 1);
            Check(Find("ScrollBottom").at("Bounds")[1].get<double>() < 476);
            Check(Input("CounterButton", {{"Kind", "Pointer"}, {"Phase", 0}, {"Device", 2}, {"PointerId", 3}, {"X", 8}, {"Y", 8}}) == 1);
            Check(Input("CounterButton", {{"Kind", "Pointer"}, {"Phase", 0}, {"Device", 2}, {"PointerId", 4}, {"X", 12}, {"Y", 8}}) == 1);
            Check(InputHost.Detach(1) && InputHost.Attach(2, 360, 900));
            Frame = Json::parse(InputHost.Take());
            Check(Find("InputCounts").at("Properties").at("Text") == "B4 E4 C3 K1");
        }
        for (const auto& InvalidNumber : {Json(0.5), Json(uint64_t(1) << 32)}) {
            Lui::Mobile::Host Malformed(106);
            Check(Malformed.Start(Lui::Mobile::Host::PrepareBundle(Controls())) && Malformed.Attach(1, 360, 640));
            const auto Frame = Json::parse(Malformed.Take());
            Check(Malformed.Acknowledge(1, Frame.at("Revision"), true));
            for (const auto& Node : Frame.at("Nodes")) if (Node.at("Name") == "Editor") {
                const auto Event = Json{{"Kind", "Text"}, {"InputRevision", Node.at("TextState").at("InputRevision")},
                    {"Text", ""}, {"Start", InvalidNumber}, {"Length", 0}};
                Check(Malformed.Event(1, Frame.at("Revision"), 1, Node.at("Id"), Event.dump()) == -1);
                Check(Json::parse(Malformed.Take()).at("Failed") == true);
            }
        }
        auto NodeByName = [&](const char* Name) -> Json {
            for (const auto& Node : ControlFrame.at("Nodes")) if (Node.at("Name") == Name) return Node;
            throw std::runtime_error("Node");
        };
        auto Receive = [&] {
            ControlFrame = Json::parse(ControlHost.Take());
            Check(ControlHost.Acknowledge(1, ControlFrame.at("Revision"), true));
        };
        const auto Image = NodeByName("Pixel");
        Check(Image.contains("Image") && !ControlHost.Asset("Pixel.png").empty());
        Check(ControlHost.Event(1, 0, 0, Image.at("Id"), Json{{"Kind", "Image"}, {"Token", Image.at("Image").at("Token")}, {"Success", true}}.dump()) == 1);
        Receive(); Check(NodeByName("Pixel").at("LoadState") == "Ready");
        uint32_t InputSequence = 0;
        auto Send = [&](const char* Name, Json Event) {
            const int Result = ControlHost.Event(1, ControlFrame.at("Revision"), ++InputSequence, NodeByName(Name).at("Id"), Event.dump());
            Receive(); return Result;
        };
        const auto ObservedFrame = ControlFrame.at("Revision").get<uint32_t>();
        Check(Send("Toggle", {{"Kind", "Checked"}, {"Value", true}}) == 1);
        Check(NodeByName("TaskStatus").at("Properties").at("Text") == "Checked");
        Check(NodeByName("Toggle").at("Properties").at("AccessibilityLabel") == "Toggle 1");
        Check(ControlHost.Event(1, ObservedFrame, ++InputSequence, NodeByName("Toggle").at("Id"), "{\"Kind\":\"Activate\"}") == 0);
        Receive();
        Check(NodeByName("Toggle").at("Properties").at("AccessibilityLabel") == "Toggle 1");
        Check(Send("Range", {{"Kind", "Value"}, {"Value", 31}}) == 0);
        Check(Send("Range", {{"Kind", "Value"}, {"Value", 30}}) == 1);
        Check(std::stod(NodeByName("Progress").at("Properties").at("Value").get<std::string>()) == 30);
        Check(Send("Scroller", {{"Kind", "Scroll"}, {"X", 0}, {"Y", 152}}) == 1);
        Check(NodeByName("ScrollBottom").at("Bounds")[1] == 324);
        const auto ClipRect = NodeByName("ClippedButton").at("Presentation").at("Rect");
        Check(ClipRect[0].get<double>() + ClipRect[2].get<double>() == 30);
        Check(NodeByName("OverflowButton").at("Presentation").at("Input") == 1);
        Check(Send("ResizeButton", {{"Kind", "Activate"}}) == 1);
        Check(NodeByName("MobileWindow").at("Bounds")[2] == 360);
        const auto InputRevision = NodeByName("Editor").at("TextState").at("InputRevision");
        Check(Send("Editor", {{"Kind", "Text"}, {"InputRevision", InputRevision}, {"Text", "Hi\xf0\x9f\x98\x80"}, {"Start", 3}, {"Length", 0}}) == 1);
        Check(NodeByName("TaskStatus").at("Properties").at("Text") == "Hi\xf0\x9f\x98\x80");
        const auto Cases = Json::parse(Read(std::string(LUI_MOBILE_FIXTURE) + "/../../browser/Modules.json"));
        for (int Generation = 0; Generation < 2; ++Generation) for (const auto& Case : Cases.at("Cases")) {
            auto Manifest = Json::parse(Input.at("lui.json"));
            Manifest["Modules"]["Files"] = Json::array();
            std::map<std::string, std::string> CaseFiles;
            for (auto Item = Case.at("Files").begin(); Item != Case.at("Files").end(); ++Item) {
                Manifest["Modules"]["Files"].push_back(Item.key());
                auto Text = Item.value().get<std::string>();
                if (Item.key() == "Main.luau" && !Case.contains("CompileError"))
                    Text = Cases.at("Prelude").get<std::string>() + Text + Cases.at("Epilogue").get<std::string>();
                CaseFiles["Src/" + Item.key()] = Text;
            }
            CaseFiles["lui.json"] = Manifest.dump();
            if (Case.contains("CompileError")) {
                bool Refused = false;
                try { Lui::Mobile::Host::PrepareBundle(CaseFiles); } catch (...) { Refused = true; }
                Check(Refused); continue;
            }
            Lui::Mobile::Host Modules(200 + Generation);
            Check(Modules.Start(Lui::Mobile::Host::PrepareBundle(CaseFiles)) && Modules.Attach(1, 640, 360));
            auto Frame = Json::parse(Modules.Take());
            Check(Modules.Acknowledge(1, Frame.at("Revision"), true));
            bool Complete = false;
            for (int Turn = 0; Turn < Case.value("PumpTurns", 1); ++Turn) {
                Check(Modules.Pump());
                const auto Bytes = Modules.Take();
                if (!Bytes.empty()) {
                    Frame = Json::parse(Bytes); Check(Modules.Acknowledge(1, Frame.at("Revision"), true));
                    for (const auto& Node : Frame.at("Nodes")) if (Node.at("Properties").value("Text", "") == "Module accepted") Complete = true;
                }
                if (Turn + 1 < Case.value("PumpTurns", 1)) Check(!Complete);
            }
            Check(Complete);
        }
        Check(ControlHost.Pump());
        Check(ControlHost.Take().empty()); // nested completion is next-round
        Check(ControlHost.Pump());
        ControlFrame = Json::parse(ControlHost.Take());
        for (const auto& Node : ControlFrame.at("Nodes")) if (Node.at("Name") == "TaskStatus") Check(Node.at("Properties").at("Text") == "ABC");
        Check(ControlHost.Acknowledge(1, ControlFrame.at("Revision"), true));
        std::cout << "[LUI:MobileTest] 100 owner/surface/admission/retirement cycles, controls/assets/viewport, module fixtures, capture identity, portable hash and interrupt rejection passed\n";
        return 0;
    } catch (const std::exception& Error) {
        const std::string Code = Error.what();
        if (Code == "Capture" || Code == "Compile" || Code == "Profile" || Code == "Base64" || Code == "Base64Limit" || Code == "Node")
            std::cerr << "[LUI:MobileTest] " << Code << '\n';
        std::cerr << "[LUI:MobileTest] Failed\n"; return 1;
    } catch (...) { std::cerr << "[LUI:MobileTest] Failed\n"; return 1; }
}
