#pragma once

#include "LuiRuntime.h"

#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct lua_State;

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
};

struct Node {
    int Id = 0;
    int Reference = 0;
    int ParentId = 0;
    bool Destroyed = false;
    bool DestroyingInProgress = false;
    bool Visible = true;
    bool Enabled = true;
    bool Checked = false;
    bool IsFocused = false;
    bool IsHovered = false;
    std::unordered_map<unsigned int, PointerInputValue> ActivePointers;
    std::string ClassName;
    std::string Name;
    std::string Title;
    std::string Text;
    double Minimum = 0;
    double Maximum = 100;
    double Value = 0;
    SizeValue Size;
    SizeValue Position;
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
};

struct BackendChange {
    enum class Kind { Property, Parent } Type;
    int Id = 0;
    std::string Name;
    std::string Value;
    int ParentId = 0;
};

struct BackendEvent {
    enum class Kind { Activate, TextChanged, CheckedChanged, ValueChanged, FocusChanged, HoverChanged, PointerInput } Type;
    int Id = 0;
    int Value = 0;
    int Phase = 0;
    int Device = 0;
    unsigned int PointerId = 0;
    double Number = 0;
    double X = 0;
    double Y = 0;
    std::string Text;
};

struct LuiRuntime {
    lua_State* State = nullptr;
    std::thread::id Owner;
    LuiBackendCallbacks Backend{};
    void* LogContext = nullptr;
    LuiLogCallback LogCallback = nullptr;
    std::string BackendName = "headless";
    std::unordered_map<std::string, int> ServiceRefs;
    std::unordered_map<int, std::unique_ptr<Node>> Nodes;
    std::unordered_map<int, Listener> Listeners;
    std::vector<ScheduledCall> Tasks;
    std::vector<BackendChange> PendingChanges;
    std::deque<BackendEvent> PendingBackendEvents;
    std::string LastError;
    std::string BackendError;
    int NextNodeId = 1;
    int NextListenerId = 1;
    bool LayoutDirty = false;
    int VmDepth = 0;
    int BackendDepth = 0;
    bool DrainingBackendEvents = false;
    bool BackendFailed = false;
};

struct SignalValue {
    int NodeId;
    const char* Name;
};
