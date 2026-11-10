#include "Host.h"
#include "Encoding.h"
#include "Storage.h"
#include "../../native/abi/LuiApplicationServices.h"
#include "../../runtime/internal/NativeServices.h"
#include "../../runtime/internal/State.h"
#include "../../runtime/internal/Scheduler.h"
#include "../../runtime/project/Project.h"
#include "../../runtime/project/Host.h"
#include "../../runtime/project/Json.h"
#include "../../native/abi/LuiProject.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <cstring>
#include <set>
#include <array>

namespace Lui::Mobile {
namespace {
using Json = nlohmann::json;
constexpr size_t RecordBytes = 256 * 1024;
Lui::Project::Options Settings() {
    Lui::Project::Options Result;
    Result.Bounds.FileBytes = 1024 * 1024;
    Result.Bounds.SourceBytes = 8 * 1024 * 1024;
    return Result;
}
auto Compile(const std::map<std::string, std::string>& Files) {
    std::string Error;
    auto Sources = Lui::Project::PrepareCaptured(Files, "lui.json", Settings(), Error);
    if (!Sources) throw std::runtime_error("Capture");
    const auto Manifest = Json::parse(Sources->GetDescriptor()).at("Manifest");
    for (const auto& Capability : Manifest.at("Capabilities"))
        if (Capability != "Storage") throw std::runtime_error("Profile");
    for (const char* Field : {"Extensions", "ExtensionDeclarations"})
        if (!Manifest.at(Field).empty()) throw std::runtime_error("Profile");
    auto Result = Lui::Project::Compile(Sources, Settings(), Error);
    if (!Result) throw std::runtime_error("Compile");
    return Result;
}
std::string CapturedDigest(const std::map<std::string, std::string>& Files) {
    Json Index = Json::array();
    for (const auto& Pair : Files) Index.push_back({Pair.first, Lui::Project::Hash(Pair.second)});
    return Lui::Project::Hash(Index.dump());
}
int Integer(const Json& Data, const char* Name) {
    const auto& Value = Data.at(Name);
    if (!Value.is_number_integer() || Value.get<double>() < std::numeric_limits<int>::lowest() ||
        Value.get<double>() > std::numeric_limits<int>::max()) throw std::runtime_error("Input");
    return Value.get<int>();
}
}
Host::Host(uint32_t Value) : Owner(std::this_thread::get_id()), Generation(Value) {}
Host::~Host() {
    // An uncooperative owner is retained by its host. Never destroy on a UI or
    // watchdog thread. This invariant is also exercised by the native tests.
    if (!IsOwner()) std::terminate();
    // A standalone portable host has no asynchronous retirement coordinator.
    // Android keeps this object in its registry until Retire has drained it.
    while (!Retire()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
bool Host::IsOwner() const { return Owner == std::this_thread::get_id(); }
void Host::RecordOwner(double Milliseconds) {
    if (!IsOwner() || !std::isfinite(Milliseconds) || Milliseconds < 0) return;
    ++OwnerCalls; OwnerOver8Ms += Milliseconds > 8;
    OwnerTotalMs += Milliseconds; OwnerMaxMs = std::max(OwnerMaxMs, Milliseconds);
}
std::string Host::Metrics() const {
    if (!IsOwner()) return {};
    Json Result{{"OwnerCalls", OwnerCalls}, {"OwnerOver8Ms", OwnerOver8Ms},
        {"OwnerTotalMs", OwnerTotalMs}, {"OwnerMaxMs", OwnerMaxMs},
        {"NativeNodes", Nodes.size()}, {"RetainedBytes", RetainedBytes}};
    Result["Runtime"] = Runtime ? Json::parse(Lui_GetPerformanceJson(Runtime)) : Json::object();
    Result["InterruptLimit"] = Runtime ? Runtime->MaxInterrupts : 0;
    return Result.dump();
}
bool Host::Retire() {
    if (!IsOwner()) return false;
    Attached = false;
    if (Runtime) { Lui_Destroy(Runtime); Runtime = nullptr; }
    Nodes.clear(); Assets.clear(); Edits.clear(); ContentEpochs.clear(); InFlight = Applied = 0; Dirty = false;
    NodeBytes.clear(); RetainedBytes = 0;
    Context.clear(); ContextDirty = true;
    if (Domain) {
        Lui_PumpNativeDomainV2(Domain);
        LuiNativeStatsV2 Stats{sizeof(Stats), 2};
        if (Lui_GetNativeStatsV2(Domain, &Stats) || Stats.Sessions || Stats.Leases || Stats.Bytes || Stats.ReservedBytes ||
            Stats.Handles || Stats.Requests || Stats.Events) return false;
        Lui_ReleaseNativeDomainV2(Domain); Domain = nullptr;
    }
    return true;
}
bool Host::Fail() { Failed = true; Retire(); return false; }
bool Host::WithinBounds(int Id) {
    ContextDirty = true;
    const auto Size = Nodes.at(Id).dump().size();
    RetainedBytes -= NodeBytes[Id]; NodeBytes[Id] = Size; RetainedBytes += Size;
    if (RetainedBytes > 192 * 1024) return false;
    Dirty = true;
    return true;
}
std::string Host::PrepareBundle(const std::map<std::string, std::string>& Files) {
    const auto Compiled = Compile(Files);
    Json Encoded = Json::object();
    for (const auto& Pair : Files) Encoded[Pair.first] = Encode(Pair.second);
    return Json{{"Version", 2}, {"Profile", "MobileHostV1"},
        {"LuauRevision", LUI_PROJECT_LUAU_REVISION},
        {"SchemaSha256", Lui::Project::Hash(Lui_GetSchemaJson())},
        {"SourceDigest", Compiled->GetSources().GetDigest()},
        {"CompiledDigest", Compiled->GetDigest()}, {"CapturedDigest", CapturedDigest(Files)}, {"Files", Encoded}}.dump();
}
bool Host::Start(const std::string& Bundle, const std::string& StorageRoot) {
    if (!IsOwner() || Started || !Generation) return false;
    Started = true;
    try {
        if (Bundle.size() > 10 * 1024 * 1024) return Fail();
        const auto Data = Lui::Project::Internal::ParseJson(Bundle);
        if (!Data.is_object() || Data.size() != 8 || Integer(Data, "Version") != 2 ||
            Data.at("Profile") != "MobileHostV1" ||
            Data.at("LuauRevision") != LUI_PROJECT_LUAU_REVISION ||
            Data.at("SchemaSha256") != Lui::Project::Hash(Lui_GetSchemaJson())) return Fail();
        std::map<std::string, std::string> Files;
        if (!Data.at("Files").is_object() || Data.at("Files").size() > 256) return Fail();
        size_t CapturedBytes = 0;
        for (auto Item = Data.at("Files").begin(); Item != Data.at("Files").end(); ++Item) {
            auto Bytes = Decode(Item.value().get<std::string>());
            if (Bytes.size() > 8 * 1024 * 1024 - CapturedBytes) return Fail();
            CapturedBytes += Bytes.size(); Files.emplace(Item.key(), std::move(Bytes));
        }
        if (Data.at("CapturedDigest") != CapturedDigest(Files)) return Fail();
        auto Compiled = Compile(Files);
        SourceDigest = Compiled->GetSources().GetDigest(); CompiledDigest = Compiled->GetDigest();
        if (Data.at("SourceDigest") != SourceDigest || Data.at("CompiledDigest") != CompiledDigest) return Fail();
        LuiDiagnosticPolicyV1 Policy{sizeof(Policy), 1, LUI_DIAGNOSTICS_RESTRICTED, 0};
        Runtime = Lui_CreateWithPolicy(&Policy);
        if (!Runtime) return Fail();
        Lui_SetBackendName(Runtime, "mobile-feasibility");
        const auto Manifest = Json::parse(Compiled->GetSources().GetDescriptor()).at("Manifest");
        const bool Storage = !Manifest.at("Capabilities").empty();
        if (Storage && (StorageRoot.empty() || !RecoverStorage(StorageRoot))) return Fail();
        LuiCapabilityDeclarationV1 Grants{sizeof(Grants), 1, Storage ? LUI_CAPABILITY_STORAGE : 0};
        LuiSandboxLimitsV1 Limits{sizeof(Limits), 1, 32 * 1024 * 1024, 1000000};
        LuiPresentationCallbacksV1 Presentation{sizeof(Presentation), 1, this, Present};
        if (!Lui_DeclareCapabilities(Runtime, &Grants) || !Lui_ConfigureSandbox(Runtime, &Limits) ||
            !Lui_SetPresentationCallbacks(Runtime, &Presentation) || !ConfigureBoundedTasks(Runtime, 4096)) return Fail();
        if (Storage) {
            Domain = Lui_CreateNativeDomainV2();
            if (!Domain || Lui_SetNativeDomainV2(Runtime, Domain) || Lui_InstallStorageV1(Runtime, StorageRoot.c_str())) return Fail();
        }
        Lui_SetBackend(Runtime, {this, Create, Property, Parent, Arrange, Destroy});
        LuiImageCallbacksV1 Images{sizeof(Images), 1, this, LoadImage, CancelImage};
        if (!Lui_SetImageCallbacks(Runtime, &Images)) return Fail();
        for (const auto& Asset : Manifest.at("Assets")) {
            const auto Name = Asset.get<std::string>();
            const auto& Bytes = Files.at(Name);
            // PNG only; decode bounds are checked again before native allocation.
            if (Bytes.size() < 24 || Bytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8)) return Fail();
            Assets.emplace(Name, Bytes);
            if (!Lui_RegisterAsset(Runtime, Name.c_str())) return Fail();
        }
        auto* Project = WrapProject(std::move(Compiled));
        if (!Project) return Fail();
        LuiProjectRunOptionsV1 Options{sizeof(Options), 1, 0, 0, 0};
        const int Success = Lui_RunProject(Runtime, Project, &Options);
        Lui_ProjectRelease(Project);
        if (!Success || !Window) return Fail();
        return true;
    } catch (...) { return Fail(); }
}
bool Host::Resize() {
    return Window && Lui_WindowResized(Runtime, Window, Width, Height);
}
void Host::CancelInput() {
    if (!Runtime) return;
    std::vector<std::pair<int, unsigned>> Pointers;
    std::vector<int> Focused;
    std::vector<int> Composing;
    for (const auto& Pair : Runtime->Nodes) if (!Pair.second->Destroyed) {
        for (const auto& Pointer : Pair.second->ActivePointers) Pointers.emplace_back(Pair.first, Pointer.second.PointerId);
        if (Pair.second->IsFocused || Pair.second->IsComposing) Focused.push_back(Pair.first);
        if (Pair.second->IsComposing) Composing.push_back(Pair.first);
    }
    std::sort(Pointers.begin(), Pointers.end());
    for (const auto& Pointer : Pointers) Lui_PointerInput(Runtime, Pointer.first, 3, 2, Pointer.second, 0, 0);
    for (int Id : Composing) Lui_TextComposition(Runtime, Id, 2, "");
    std::sort(Focused.begin(), Focused.end());
    for (int Id : Focused) Lui_FocusChanged(Runtime, Id, 0);
}
Host::Json Host::Properties(int Id) const {
    auto Result = Nodes.at(Id).at("Properties");
    const auto& Node = *Runtime->Nodes.at(Id);
    // Native input mutates canonical fields without echoing a backend property.
    if (Result.contains("Text")) Result["Text"] = Node.Text;
    if (Node.ClassName == "CheckBox") Result["Checked"] = Node.Checked ? "true" : "false";
    if (Node.ClassName == "Slider" || Node.ClassName == "ProgressBar") Result["Value"] = Json(Node.Value).dump();
    return Result;
}
std::string Host::Content(int Id) const {
    return Lui::Project::Hash(Json{ContentEpochs.at(Id), Nodes.at(Id).at("Properties")}.dump());
}
void Host::PrepareContext() const {
    if (!ContextDirty) return;
    Context.clear(); Context.reserve(Nodes.size());
    for (const auto& Pair : Nodes) {
        const auto& Node = *Runtime->Nodes.at(Pair.first);
        const auto& Bounds = Node.Bounds;
        auto Presentation = Pair.second.value("Presentation", Json::object());
        const int Rank = Presentation.value("Rank", 0); Presentation.erase("Rank");
        const auto Rect = Presentation.value("Rect", Json::array({0, 0, Bounds.Width, Bounds.Height}));
        const double Left = std::max(Bounds.X, Bounds.X + Rect[0].get<double>());
        const double Top = std::max(Bounds.Y, Bounds.Y + Rect[1].get<double>());
        const double Right = std::min(Bounds.X + Bounds.Width, Bounds.X + Rect[0].get<double>() + Rect[2].get<double>());
        const double Bottom = std::min(Bounds.Y + Bounds.Height, Bounds.Y + Rect[1].get<double>() + Rect[3].get<double>());
        Context.push_back({Pair.first, Node.ParentId, Rank, Presentation.value("Input", 0) != 0, Node.ClassName,
            {Left, Top, std::max(0.0, Right - Left), std::max(0.0, Bottom - Top)},
            {Pair.first, Node.ClassName, Node.ParentId, Node.ZIndex, Node.Visible, Node.Enabled,
             {Bounds.X, Bounds.Y, Bounds.Width, Bounds.Height}, std::move(Presentation)}});
    }
    ContextDirty = false;
}
std::string Host::Observation(int Id) const {
    PrepareContext();
    Json Geometry = Json::array();
    const auto& Target = *Runtime->Nodes.at(Id);
    const auto& TargetContext = *std::find_if(Context.begin(), Context.end(), [&](const auto& Value) { return Value.Id == Id; });
    const auto& TargetRect = TargetContext.Rect;
    const bool Scroller = Target.ClassName == "ScrollingFrame";
    std::set<int> Ancestors;
    for (int Parent = Target.ParentId; Parent; Parent = Runtime->Nodes.at(Parent)->ParentId) Ancestors.insert(Parent);
    for (const auto& Node : Context) {
        bool Descendant = false;
        if (Scroller && Node.Id != Id) for (int Parent = Node.Parent; Parent; Parent = Runtime->Nodes.at(Parent)->ParentId)
            if (Parent == Id) { Descendant = true; break; }
        if (Descendant) continue;
        const auto& Rect = Node.Rect;
        const bool Overlaps = Rect[2] > 0 && Rect[3] > 0 && Rect[0] < TargetRect[0] + TargetRect[2] &&
            TargetRect[0] < Rect[0] + Rect[2] && Rect[1] < TargetRect[1] + TargetRect[3] && TargetRect[1] < Rect[1] + Rect[3];
        if (Node.Id != Id && !Ancestors.count(Node.Id) && (!Overlaps || !Node.Input)) continue;
        // Absolute ranks shift as unrelated rows are realized. Only a control's
        // position relative to this target can change its occlusion context.
        auto Record = Node.Record; Record.push_back(Node.Rank > TargetContext.Rank);
        Geometry.push_back(std::move(Record));
    }
    return Lui::Project::Hash(Json{Id, Geometry}.dump());
}
bool Host::Attach(uint32_t Value, double X, double Y) {
    if (!IsOwner() || !Runtime || Failed || !Value || Value <= Epoch || !std::isfinite(X) ||
        !std::isfinite(Y) || X <= 0 || Y <= 0 || X > 16384 || Y > 16384) return false;
    try {
        CancelInput(); Edits.clear();
        Epoch = Value; Width = X; Height = Y; Attached = true; InFlight = Applied = 0; Sequence = 0; Dirty = true;
        return Resize() || Fail();
    } catch (...) { return Fail(); }
}
bool Host::Detach(uint32_t Value) {
    if (!IsOwner() || Value != Epoch) return false;
    try {
        CancelInput();
    } catch (...) { return Fail(); }
    Attached = false; InFlight = Applied = 0; return true;
}
std::string Host::Take() {
    if (!IsOwner()) return {};
    try {
        if (Failed) return "{\"Failed\":true}";
        if (!Runtime || !Attached || !Dirty || InFlight) return {};
        if (Revision == std::numeric_limits<int32_t>::max()) { Fail(); return "{\"Failed\":true}"; }
        Json Values = Json::array();
        std::function<void(int, bool, double)> Visit = [&](int Id, bool Shown, double Opacity) {
            const auto& Node = *Runtime->Nodes.at(Id);
            if (Node.Destroyed) return;
            Shown = Shown && Node.Visible;
            if (!Node.StyleProjection.empty()) Opacity *= Json::parse(Node.StyleProjection).at("Opacity").get<double>();
            if (Nodes.count(Id)) {
                auto Value = Nodes.at(Id);
                Value["Properties"] = Properties(Id);
                Value["Observation"] = Observation(Id);
                Value["Content"] = Content(Id);
                Value["Focused"] = Node.IsFocused;
                Value["Name"] = Node.Name; Value["Shown"] = Shown; Value["Opacity"] = Opacity;
                Value["LoadState"] = Node.LoadState;
                if (Node.ClassName == "TextBox") Value["TextState"] = {{"Text", Node.Text},
                    {"Start", Node.Secret ? 0 : Node.SelectionStart}, {"Length", Node.Secret ? 0 : Node.SelectionLength},
                    {"InputRevision", std::to_string(Node.InputRevision)}, {"Multiline", Node.Multiline},
                    {"Secret", Node.Secret}, {"MaxLength", Node.MaxLength}, {"Placeholder", Node.PlaceholderText},
                    {"EditId", Edits[Id]}, {"Observation", Value.at("Observation")},
                    {"IsComposing", !Node.Secret && Node.IsComposing}, {"CompositionText", Node.Secret ? "" : Node.CompositionText}};
                Values.push_back(std::move(Value));
            }
            std::vector<int> Children = Node.Children;
            std::stable_sort(Children.begin(), Children.end(), [&](int Left, int Right) {
                return Runtime->Nodes.at(Left)->ZIndex < Runtime->Nodes.at(Right)->ZIndex;
            });
            for (int Child : Children) Visit(Child, Shown, Opacity);
        };
        Visit(Window, true, 1);
        const auto Output = Json{{"Version", 1}, {"Generation", Generation}, {"SurfaceEpoch", Epoch},
            {"Revision", ++Revision}, {"Nodes", std::move(Values)},
            {"SourceDigest", SourceDigest}, {"CompiledDigest", CompiledDigest}}.dump();
        if (Output.size() > RecordBytes) { Fail(); return "{\"Failed\":true}"; }
        InFlight = Revision; Dirty = false; return Output;
    } catch (...) { Fail(); return "{\"Failed\":true}"; }
}
bool Host::Acknowledge(uint32_t Value, uint32_t Frame, bool Success) {
    if (!IsOwner() || !Runtime || !Attached || Value != Epoch || !Frame || Frame != InFlight) return false;
    if (!Success) return Fail();
    Applied = Frame; InFlight = 0; return true;
}
bool Host::Admitted(uint32_t Value, uint32_t Frame, uint32_t Event, int Id) {
    if (!IsOwner() || !Runtime || Failed || !Attached || Value != Epoch || !Frame || Frame != Applied ||
        InFlight || Dirty || !Event || Event != Sequence + 1 || !Nodes.count(Id)) return false;
    return true;
}
bool Host::Activate(uint32_t Value, uint32_t Frame, uint32_t Event, int Id) {
    if (!Admitted(Value, Frame, Event, Id)) return false;
    return this->Event(Value, Frame, Event, Id, "{\"Kind\":\"Activate\"}") == 1;
}
int Host::Event(uint32_t Value, uint32_t Frame, uint32_t Sequence, int Id, const std::string& Input) {
    if (!IsOwner() || Input.size() > 128 * 1024) return -1;
    try {
        const auto Data = Lui::Project::Internal::ParseJson(Input);
        const auto Kind = Data.at("Kind").get<std::string>();
        static const std::map<std::string, std::set<std::string>> Fields{
            {"Activate", {"Kind"}}, {"Checked", {"Kind", "Value"}}, {"Value", {"Kind", "Value"}},
            {"Scroll", {"Kind", "X", "Y"}}, {"Focus", {"Kind", "Value"}},
            {"Text", {"Kind", "InputRevision", "Text", "Start", "Length"}},
            {"Composition", {"Kind", "InputRevision", "Text", "Phase"}}, {"Image", {"Kind", "Token", "Success"}},
            {"Pointer", {"Kind", "Phase", "Device", "PointerId", "X", "Y"}}, {"Key", {"Kind", "Phase", "Key"}}};
        if (!Fields.count(Kind) || !Data.is_object()) { Fail(); return -1; }
        for (const auto& Field : Fields.at(Kind)) if (!Data.contains(Field)) { Fail(); return -1; }
        for (auto Field = Data.begin(); Field != Data.end(); ++Field) if (!Fields.at(Kind).count(Field.key())) {
            if (Kind == "Image") { Fail(); return -1; }
            if (Field.key() == "EditId" && (Kind == "Text" || Kind == "Composition")) {
                if (Integer(Data, "EditId") <= 0) { Fail(); return -1; }
            } else if (Field.key() == "Observation" || Field.key() == "Content") {
                const auto Text = Field.value().get<std::string>();
                if (Text.size() != 64 || Text.find_first_not_of("0123456789abcdef") != std::string::npos) { Fail(); return -1; }
            } else { Fail(); return -1; }
        }
        if (Kind == "Image") {
            if (!Runtime || Failed || !Attached || !Nodes.count(Id)) return 0;
            const auto Text = Data.at("Token").get<std::string>();
            if (Text.empty() || Text.size() > 20 || Text.find_first_not_of("0123456789") != std::string::npos) { Fail(); return -1; }
            const auto Token = std::stoull(Text);
            if (!Token || std::to_string(Token) != Text) { Fail(); return -1; }
            const int Result = Lui_ImageCompleted(Runtime, Id, Token, Data.at("Success").get<bool>());
            // LoadState is canonical metadata, not a geometry/property callback.
            // Publish its terminal transition even when layout stays unchanged.
            if (Result) Dirty = true;
            return Result ? 1 : 0;
        }
        if (!Runtime || Failed || !Attached || Value != Epoch) return 0;
        if (!Sequence || Sequence != this->Sequence + 1) { Fail(); return -1; }
        this->Sequence = Sequence;
        if (!Nodes.count(Id)) { Dirty = true; return 0; }
        if (Data.contains("EditId")) Edits[Id] = std::max(Edits[Id], Integer(Data, "EditId"));
        const bool Terminal = (Kind == "Focus" && !Data.at("Value").get<bool>()) ||
            (Kind == "Pointer" && Integer(Data, "Phase") == 3) || (Kind == "Key" && Integer(Data, "Phase") == 2) ||
            (Kind == "Composition" && Integer(Data, "Phase") == 2);
        const bool Observed = Frame && Frame <= Applied && Data.contains("Observation") && Data.at("Observation") == Observation(Id);
        const bool ContentMatches = Data.contains("Content") && Data.at("Content") == Content(Id);
        const bool ContentInput = Kind == "Activate" || Kind == "Checked" || Kind == "Value";
        const bool Current = Frame && Frame == Applied && !InFlight && !Dirty &&
            (!Data.contains("Observation") || Observed) && (!ContentInput || !Data.contains("Content") || ContentMatches);
        const bool Continued = !InFlight && Observed &&
            (!ContentInput || ContentMatches);
        const bool Release = Kind == "Pointer" && Integer(Data, "Phase") == 2;
        if (!Terminal && !Release && !Current && !Continued) { Dirty = true; return 0; }
        int Result = 0;
        if (Kind == "Activate") Result = Lui_Activate(Runtime, Id);
        else if (Kind == "Checked") {
            Result = Lui_CheckedChanged(Runtime, Id, Data.at("Value").get<bool>());
            // One native checkbox action observes its value and activation at
            // one admitted frame; it cannot rebase half the action after apply.
            if (Result) Lui_Activate(Runtime, Id);
        }
        else if (Kind == "Value") Result = Lui_ValueChanged(Runtime, Id, Data.at("Value").get<double>());
        else if (Kind == "Scroll") Result = Lui_ScrollInput(Runtime, Id, Data.at("X").get<double>(), Data.at("Y").get<double>());
        else if (Kind == "Focus") Result = Lui_FocusChanged(Runtime, Id, Data.at("Value").get<bool>());
        else if (Kind == "Pointer") {
            int Phase = Integer(Data, "Phase");
            const int Pointer = Integer(Data, "PointerId"), Device = Integer(Data, "Device");
            if (Pointer < 0 || Pointer > 31 || Device < 0 || Device > 3 || Phase < 0 || Phase > 3) { Fail(); return -1; }
            const double X = Data.at("X").get<double>(), Y = Data.at("Y").get<double>();
            if (Release && !Current && !Continued) Phase = 3;
            if (Phase == 0) {
                size_t Active = 0;
                for (const auto& Pair : Runtime->Nodes) {
                    Active += Pair.second->ActivePointers.size();
                    if (Pair.first != Id && Pair.second->ActivePointers.count({static_cast<unsigned>(Pointer), 0})) { Dirty = true; return 0; }
                }
                if (Active >= 32) { Dirty = true; return 0; }
                const auto& Node = *Runtime->Nodes.at(Id);
                int Hit = 0;
                if (!Lui_HitTest(Runtime, Window, Node.Bounds.X + X, Node.Bounds.Y + Y, &Hit)) { Dirty = true; return 0; }
                if (Node.ClassName == "ScrollingFrame") while (Hit && Hit != Id) Hit = Runtime->Nodes.at(Hit)->ParentId;
                if (Hit != Id) { Dirty = true; return 0; }
            }
            Result = Lui_PointerInput(Runtime, Id, Phase, Device, Pointer, X, Y);
        } else if (Kind == "Key") {
            const auto Key = Data.at("Key").get<std::string>();
            if (Key.size() > 32 || Key.find('\0') != std::string::npos) { Fail(); return -1; }
            if (Integer(Data, "Phase") == 0 && Runtime->Nodes.at(Id)->ActiveKeys.size() >= 16) { Dirty = true; return 0; }
            Result = Lui_KeyInput(Runtime, Id, Integer(Data, "Phase"), Key.c_str());
        }
        else if (Kind == "Text" || Kind == "Composition") {
            uint64_t Current = 0;
            if (!Lui_GetTextInputRevision(Runtime, Id, &Current) || std::to_string(Current) != Data.at("InputRevision")) {
                Dirty = true; return 0;
            }
            const auto Text = Data.at("Text").get<std::string>();
            if (Text.find('\0') != std::string::npos) { Fail(); return -1; }
            if (Kind == "Text") Result = Lui_TextInput(Runtime, Id, Text.c_str(), Integer(Data, "Start"), Integer(Data, "Length"));
            else Result = Lui_TextComposition(Runtime, Id, Integer(Data, "Phase"), Text.c_str());
        } else { Fail(); return -1; }
        // Even a refusal reprojects canonical state so the native widget can
        // undo an optimistic toggle/edit. It never turns stale input into success.
        if (!Resize()) { Fail(); return -1; }
        Dirty = true; return Result ? 1 : 0;
    } catch (...) { Fail(); return -1; }
}
bool Host::Pump() {
    if (!IsOwner() || !Runtime || Failed) return false;
    if (!Attached || InFlight) return true;
    try {
        const auto Deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
        // One bounded native batch, then the normal bounded task slice. A
        // completion can schedule tasks; nested tasks still enter the next round.
        DrainNativeServices(Runtime, 8, Deadline);
        if (!PumpBoundedTasks(Runtime, 32, Deadline) || !Resize()) return Fail();
        return true;
    } catch (...) { return Fail(); }
}
bool Host::Theme(const std::string& Value) {
    return IsOwner() && Runtime && Lui_SystemThemeChanged(Runtime, Value.c_str());
}
std::string Host::Asset(const std::string& Path) const {
    if (!IsOwner() || !Runtime || !Assets.count(Path)) return {};
    return Assets.at(Path);
}
int Host::Create(void* Context, int Id, const char* Class) {
    auto& Self = *static_cast<Host*>(Context);
    try {
        const std::string Name = Class ? Class : "";
        static const std::set<std::string> Classes{"Window", "Frame", "ScrollingFrame", "TextLabel", "TextButton", "CheckBox", "Slider", "ProgressBar", "TextBox", "ImageLabel"};
        if (!Classes.count(Name) || Self.Nodes.size() >= 128 || Self.Nodes.count(Id) ||
            (Name == "Window" && Self.Window)) return 0;
        Self.Nodes.emplace(Id, Json{{"Id", Id}, {"Class", Name}, {"Parent", 0}, {"Properties", Json::object()}});
        Self.ContentEpochs[Id] = 0;
        if (Name == "Window") Self.Window = Id;
        return Self.WithinBounds(Id);
    } catch (...) { return 0; }
}
int Host::Property(void* Context, int Id, const char* Name, const char* Value) {
    auto& Self = *static_cast<Host*>(Context);
    try {
        if (!Name || !Value || !Self.Nodes.count(Id)) return 0;
        if (Self.ContentEpochs.at(Id) == std::numeric_limits<uint64_t>::max()) return 0;
        ++Self.ContentEpochs.at(Id);
        Self.Nodes.at(Id)["Properties"][Name] = Value;
        return Self.WithinBounds(Id);
    } catch (...) { return 0; }
}
int Host::Parent(void* Context, int Id, int ParentId) {
    auto& Self = *static_cast<Host*>(Context);
    try {
        if (!Self.Nodes.count(Id) || (ParentId && !Self.Nodes.count(ParentId))) return 0;
        Self.Nodes.at(Id)["Parent"] = ParentId; return Self.WithinBounds(Id);
    } catch (...) { return 0; }
}
int Host::Arrange(void* Context, int Id, double X, double Y, double W, double H, int) {
    auto& Self = *static_cast<Host*>(Context);
    try {
        if (!Self.Nodes.count(Id) || !std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(W) || !std::isfinite(H)) return 0;
        Self.Nodes.at(Id)["Bounds"] = {X, Y, W, H}; return Self.WithinBounds(Id);
    } catch (...) { return 0; }
}
int Host::Destroy(void* Context, int Id) {
    auto& Self = *static_cast<Host*>(Context);
    Self.RetainedBytes -= Self.NodeBytes[Id]; Self.NodeBytes.erase(Id);
    Self.ContextDirty = true;
    Self.Nodes.erase(Id); Self.Edits.erase(Id); Self.ContentEpochs.erase(Id); Self.Dirty = true; return 1;
}
int Host::LoadImage(void* Context, int Id, uint64_t Token, const char* Source) {
    auto& Self = *static_cast<Host*>(Context);
    try {
        if (!Source || !Self.Assets.count(Source) || !Self.Nodes.count(Id)) return 0;
        Self.Nodes.at(Id)["Image"] = {{"Source", Source}, {"Token", std::to_string(Token)}};
        return Self.WithinBounds(Id);
    } catch (...) { return 0; }
}
void Host::CancelImage(void* Context, uint64_t Token) {
    auto& Self = *static_cast<Host*>(Context);
    for (auto& Pair : Self.Nodes) if (Pair.second.contains("Image") &&
        Pair.second["Image"]["Token"] == std::to_string(Token)) {
        Pair.second.erase("Image"); Self.WithinBounds(Pair.first);
    }
    Self.Dirty = true;
}
int Host::Present(void* Context, int Id, int Clip, double X, double Y, double W, double H, int Rank, int Input) {
    auto& Self = *static_cast<Host*>(Context);
    try {
        if (!Self.Nodes.count(Id)) return 0;
        Self.Nodes.at(Id)["Presentation"] = {{"Clip", Clip}, {"Rect", {X, Y, W, H}}, {"Rank", Rank}, {"Input", Input}};
        return Self.WithinBounds(Id);
    } catch (...) { return 0; }
}
}
