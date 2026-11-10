#pragma once

#include "LuiRuntime.h"
#include "LuiNativeServices.h"
#include "LuiApplicationServices.h"
#include "Diagnostics.h"

#include <chrono>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lua_State;
struct ModuleContext;
namespace Lui::Tls { class Settings; }
namespace Lui::Native { struct Domain; }
struct NativeServiceBinding;
struct DesktopServiceState;
struct FrameDomain;
struct FrameSurfaceSlot;

struct Dimension {
    double Scale = 0;
    double Offset = 0;
};

struct SizeValue {
    Dimension X;
    Dimension Y;
};

struct VectorValue {
    double X = 0;
    double Y = 0;
};

struct BoundsValue {
    double X = 0;
    double Y = 0;
    double Width = 0;
    double Height = 0;
};

struct PointerInputValue {
    unsigned int PointerId = 0;
    std::string Device;
    VectorValue Position;
    bool IsCanceled = false;
    std::string Button;
    bool IsV2 = false;
};

struct SourceLocation {
    std::string Source;
    int Line = 0;
};

struct KeyboardInputValue {
    std::string Key;
    bool IsRepeat = false;
    bool IsShortcut = false;
    unsigned int Modifiers = 0;
    bool TextEditing = false;
};

// Disposable facts normalized from validated local style properties. Keep the
// authoritative values in StyleProperties; ancestor composition stays in layout.
struct StyleGeometryValue {
    enum Fact : unsigned char {
        Background = 1u, NoTransparency = 2u, SquareCorners = 4u,
        NoBorder = 8u, VisibleOpacity = 16u, UnitOpacity = 32u,
    };
    unsigned char Facts = NoTransparency | SquareCorners | NoBorder | VisibleOpacity | UnitOpacity;
    void Set(Fact Flag, bool Enabled) {
        Facts = static_cast<unsigned char>(Enabled ? Facts | Flag : Facts & ~Flag);
    }
    bool Has(Fact Flag) const {return (Facts & Flag) != 0;}
    bool FillsOpaqueRectangle() const {
        constexpr unsigned char Mask = Background | NoTransparency | SquareCorners | NoBorder | UnitOpacity;
        return (Facts & Mask) == Mask;
    }
};
static_assert(sizeof(StyleGeometryValue) == 1, "Style geometry facts must stay compact");

struct NodeHandle;
struct Node {
    NodeHandle* Handle = nullptr;
    bool Sensitive=false, EffectiveSensitive=false;
    bool NativeCreated = false;
    int Id = 0;
    int Reference = 0;
    int ParentId = 0;
    SourceLocation CreatedAt;
    SourceLocation LastChangedAt;
    std::string LastChangedProperty;
    bool Destroyed = false;
    bool DestroyingInProgress = false;
    bool Visible = true;
    bool Enabled = true;
    bool Checked = false;
    bool IsFocused = false;
    bool IsHovered = false;
    bool WindowClosed = false, WindowClosePending = false;
    uint64_t PresentationRevision = 0;
    bool NativeWindowClosedPending = false;
    bool WindowCloseRequestQueued = false;
    bool WindowCloseDispatch = false, WindowClosePrevented = false;
    std::map<std::pair<unsigned int, int>, PointerInputValue> ActivePointers;
    std::unordered_map<std::string, KeyboardInputValue> ActiveKeys;
    std::string ClassName;
    std::string Name;
    std::string Title;
    std::string Text;
    bool Multiline = false, Secret = false, IsComposing = false;
    int SelectionStart = 0, SelectionLength = 0, MaxLength = 65534;
    uint64_t InputRevision = 1;
    uint64_t TextRevision = 1;
    int CodeEditorUndoMaxLength = 0;
    std::string PlaceholderText, CompositionText;
    std::string Source;
    uint64_t ImageToken = 0;
    std::string LoadState = "Idle";
    bool ImageStatePending = false;
    std::string ScaleMode = "Fit", FrameState = "Empty";
    uint64_t FrameEpoch = 1, FrameSequence = 0;
    std::shared_ptr<FrameSurfaceSlot> FrameSlot;
    LuiFrame* PendingFrame = nullptr;
    LuiFrame* InFlightFrame = nullptr;
    LuiFrame* DisplayedFrame = nullptr;
    std::string AccessibilityLabel;
    std::string AccessibilityDescription;
    double Minimum = 0;
    double Maximum = 100;
    double Value = 0;
    SizeValue Size;
    VectorValue ViewportSize;
    bool HasViewportSize = false;
    SizeValue Position;
    SizeValue CanvasSize;
    VectorValue CanvasPosition;
    VectorValue AbsoluteCanvasSize;
    VectorValue AbsoluteCanvasPosition;
    bool ClipsDescendants = false;
    StyleGeometryValue StyleGeometry;
    int ZIndex = 0;
    std::unordered_map<std::string, std::string> StyleProperties;
    std::string StyleProjection;
    SizeValue CellSize{{0, 100}, {0, 100}};
    SizeValue CellPadding;
    VectorValue AnchorPoint;
    VectorValue MinSize;
    VectorValue MaxSize;
    bool HasMaxSize = false;
    BoundsValue Bounds;
    int LayoutOrder = 0;
    Dimension Padding;
    Dimension PaddingTop;
    Dimension PaddingBottom;
    Dimension PaddingLeft;
    Dimension PaddingRight;
    std::string FillDirection = "Vertical";
    std::vector<int> Children;
    std::vector<int> Listeners;
};

struct Listener {
    int Id = 0;
    int NodeId = 0;
    int Reference = 0;
    std::string Signal;
    bool Active = true;
};

struct ScheduledCall {
    int Reference = 0;
    std::chrono::steady_clock::time_point Due;
    int ArgumentCount = 0;
};

struct NetworkContext;
struct NetworkConnection;
struct HostedSession;
namespace Lui::Http { struct Message; }

struct NetworkCompletion {
    enum class Kind { None, Bytes, EndOfStream, Connection, ClosedSignal, HttpResponse, HostedRequest, Datagram } Type = Kind::None;
    int Reference = 0;
    std::string Bytes;
    std::string Error;
    std::shared_ptr<NetworkConnection> Connection;
    std::shared_ptr<Lui::Http::Message> HttpResponse;
    std::shared_ptr<HostedSession> Session;
    std::string RemoteAddress;
    unsigned short RemotePort = 0;
};

struct BackendChange {
    enum class Kind { Property, Parent } Type;
    int Id = 0;
    std::string Name;
    std::string Value;
    int ParentId = 0;
};

struct BackendEvent {
    enum class Kind { Activate, TextChanged, CheckedChanged, ValueChanged, WindowResized, WindowCloseRequested, WindowClosed, FocusChanged, HoverChanged, PointerInput, KeyInput, WindowShortcut, ScrollInput, TextInput, Composition } Type;
    int Id = 0;
    int Value = 0;
    int Phase = 0;
    int Device = 0;
    int Button = 0;
    bool PointerV2 = false;
    unsigned int PointerId = 0;
    double Number = 0;
    double X = 0;
    double Y = 0;
    std::string Text;
    uint64_t InputRevision = 0;
};

struct ExtensionMethod {
    std::string ExtensionName;
    std::string ServiceName;
    std::string Name;
    std::string Type;
    LuiExtensionMethodV1 Callback = nullptr;
    void* Context = nullptr;
};

struct ExtensionSignalListener {
    int Id = 0;
    int Reference = 0;
    bool Active = true;
};

struct ExtensionSignal {
    std::string ExtensionName;
    std::string ServiceName;
    std::string Name;
    std::string Type;
    std::vector<ExtensionSignalListener> Listeners;
};

struct LoadedExtension {
    std::string Name;
    void* Library = nullptr;
    LuiExtensionShutdownV1 Shutdown = nullptr;
    void* Context = nullptr;
    uint32_t Version=0;
    uint64_t RequiredCapabilities=0;
    std::string BinaryHash;
    std::shared_ptr<void> ImagePin;
};

struct PendingUiCompletion {
    LuiUiCompletionV1 Callback = nullptr;
    void* Context = nullptr;
};

struct PlatformRequestCompletion {
    uint64_t Id = 0;
    std::string Result;
    std::string Error;
    bool HasResult = false;
};

struct LuiRuntime {
    lua_State* State = nullptr;
    std::thread::id Owner;
    LuiBackendCallbacks Backend{};
    LuiFocusCallbacksV1 Focus{};
    LuiWindowCallbacksV1 Windows{};
    LuiPresentationCallbacksV1 Presentation{};
    LuiImageCallbacksV1 Images{};
    LuiFrameCallbacksV1 Frames{};
    std::shared_ptr<FrameDomain> FrameResources;
    // Internal pixel allocation seam; null selects malloc. Returned storage must
    // be free-compatible. Tests inject failure without changing host protocols.
    void* (*FrameAllocator)(size_t) = nullptr;
    uint64_t FramesAccepted = 0, FramesReplaced = 0, FramesRejected = 0;
    uint64_t FramesConsumed = 0, FramesStale = 0;
    uint64_t NextImageToken = 1;
    size_t PendingImages = 0;
    std::vector<int> CanvasSignals;
    uint64_t LayoutPasses = 0, PresentationCalls = 0;
    size_t RetiredUserdata = 0;
    std::deque<double> LayoutSamples;
    std::deque<double> InputSamples, RenderSamples;
    std::string PerformanceJson;
    LuiPlatformCallbacksV1 Platform{};
    void* LogContext = nullptr;
    LuiLogCallback LogCallback = nullptr;
    std::string BackendName = "headless";
    uint64_t GrantedCapabilities = 0;
    uint32_t NetworkPolicyFlags = 0;
    uint32_t ClientPortMin = 0;
    uint32_t ClientPortMax = 0;
    uint32_t ServerPortMin = 0;
    uint32_t ServerPortMax = 0;
    bool NetworkPolicyDeclared = false;
    uint64_t VmBytes = 0;
    uint64_t VmLimitBytes = 0;
    uint64_t MaxInterrupts = 0;
    uint64_t InterruptCount = 0;
    bool Sandboxed = false;
    bool SourceProvenanceEnabled = false;
    bool CapabilitiesDeclared = false;
    bool ApplicationStarted = false;
    bool ProjectBootstrap = false;
    int ModuleDepth = 0;
    std::shared_ptr<ModuleContext> Modules;
    std::shared_ptr<Lui::Tls::Settings> TlsSettings;
    bool InitializingExtension = false;
    bool UiCompletionRunning = false;
    std::atomic<bool> ShuttingDown{false};
    std::mutex CompletionMutex;
    std::deque<PendingUiCompletion> PendingUiCompletions;
    std::string InitializingExtensionName;
    DiagnosticText ExtensionError;
    std::string ExtensionSchemaJson;
    std::vector<std::unique_ptr<ExtensionMethod>> ExtensionMethods;
    std::vector<std::unique_ptr<ExtensionSignal>> ExtensionSignals;
    int NextExtensionListenerId = 1;
    std::vector<LoadedExtension> Extensions;
    std::shared_ptr<Lui::Native::Domain> NativeDomain;
    std::vector<std::shared_ptr<NativeServiceBinding>> NativeServices;
    size_t PendingNativeRequests = 0;
    std::shared_ptr<DesktopServiceState> DesktopServices;
    std::shared_ptr<void> DesktopBridgeOwner;
    bool ProjectNativeVerified=false;
    std::string VerifiedProjectDigest;
    std::unordered_map<std::string, int> ServiceRefs;
    std::unordered_set<std::string> Assets;
    std::unordered_map<uint64_t, int> PlatformRequests;
    std::deque<PlatformRequestCompletion> PlatformCompletions;
    uint64_t NextPlatformRequestId = 1;
    std::unordered_map<int, std::unique_ptr<Node>> Nodes;
    std::unordered_map<int, Listener> Listeners;
    std::vector<int> ThemeListeners;
    std::string CurrentTheme = "Light";
    std::string PendingTheme;
    std::vector<ScheduledCall> Tasks;
    size_t TaskLimit = 0, TaskRoundIndex = 0;
    std::vector<ScheduledCall> TaskRound;
    std::chrono::steady_clock::time_point TaskRoundTime;
    bool TaskRoundActive = false;
    int ActiveTaskReference = 0;
    lua_State* ActiveTaskThread = nullptr;
    bool ActiveTaskHasWaiter = false;
    NetworkContext* Network = nullptr;
    std::deque<NetworkCompletion> NetworkCompletions;
    std::unordered_set<int> PendingAsyncReferences;
    std::vector<BackendChange> PendingChanges;
    std::deque<BackendEvent> PendingBackendEvents;
    size_t PendingBackendBytes = 0;
    DiagnosticText LastError;
    uint32_t DiagnosticMode=0;
    std::deque<std::string> DiagnosticRecords;
    size_t DiagnosticBytes=0, DroppedDiagnostics=0;
    std::string DiagnosticSnapshot;
    bool InspectionGranted=false, Inspecting=false;
    std::string InspectionSnapshot;
    std::string PreviewSnapshotJson;
    std::string BackendError;
    int NextNodeId = 1;
    int NextListenerId = 1;
    bool LayoutDirty = false;
    int LayoutDepth = 0, PresentationWindowId = 0;
    int VmDepth = 0;
    int BackendDepth = 0;
    bool DrainingBackendEvents = false;
    bool PendingWindowClosures = false;
    bool BackendFailed = false;
};

struct SignalValue {
    int NodeId;
    const char* Name;
};
