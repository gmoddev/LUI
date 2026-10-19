#include "../internal/Images.h"
#include "../internal/Dispatch.h"
#include "../internal/Scheduler.h"
#include <limits>
struct BackendGuard {
    LuiRuntime* Runtime;
    explicit BackendGuard(LuiRuntime* Value):Runtime(Value) {++Runtime->BackendDepth;}
    ~BackendGuard() {--Runtime->BackendDepth;}
};

void CancelImage(LuiRuntime* Runtime, Node* Value) {
    if (!Value->ImageToken) return;
    const auto Token = Value->ImageToken; Value->ImageToken = 0;
    if (Runtime->PendingImages) --Runtime->PendingImages;
    Value->LoadState = "Canceled";
    Value->ImageStatePending = true;
    if (Runtime->Images.Cancel) {BackendGuard Guard(Runtime); Runtime->Images.Cancel(Runtime->Images.Context, Token);}
}
void StartImage(LuiRuntime* Runtime, Node* Value) {
    CancelImage(Runtime, Value);
    Value->ImageStatePending = true;
    if (Value->Source.empty()) {Value->LoadState = "Idle"; return;}
    if (!Runtime->Images.Load) {Value->LoadState = "Unavailable"; return;}
    if (Runtime->PendingImages >= 32 || Runtime->NextImageToken == std::numeric_limits<uint64_t>::max()) {
        Value->LoadState = "Failed"; return;
    }
    Value->ImageToken = Runtime->NextImageToken++; ++Runtime->PendingImages;
    Value->LoadState = "Loading";
    BackendGuard Guard(Runtime);
    if (!Runtime->Images.Load(Runtime->Images.Context, Value->Id, Value->ImageToken, Value->Source.c_str())) {
        CancelImage(Runtime, Value); Value->LoadState = "Failed";
    }
}
extern "C" LUI_API int LUI_CALL Lui_SetImageCallbacks(LuiRuntime* Runtime, const LuiImageCallbacksV1* Callbacks) {
    if (!CheckOwner(Runtime) || !Callbacks || Callbacks->StructSize != sizeof(*Callbacks) || Callbacks->AbiVersion != 1 ||
        !Callbacks->Load || !Callbacks->Cancel || Runtime->Images.Load || Runtime->ApplicationStarted || Runtime->NextNodeId != 1 ||
        Runtime->VmDepth || Runtime->BackendDepth || Runtime->ShuttingDown) return 0;
    Runtime->Images = *Callbacks; return 1;
}
extern "C" LUI_API int LUI_CALL Lui_ImageCompleted(LuiRuntime* Runtime, int Id, uint64_t Token, int Success) {
    if (!CheckOwner(Runtime) || Runtime->ShuttingDown || Runtime->VmDepth || Runtime->BackendDepth || Runtime->BackendFailed) return 0;
    auto Found = Runtime->Nodes.find(Id);
    if (Found == Runtime->Nodes.end() || Found->second->Destroyed || !Token || Found->second->ImageToken != Token) return 0;
    Node* Value = Found->second.get(); Value->ImageToken = 0;
    if (Runtime->PendingImages) --Runtime->PendingImages;
    Value->LoadState = Success ? "Ready" : "Failed";
    Value->ImageStatePending = false;
    FireSignal(Runtime, Value, "LoadChanged"); FlushLayout(Runtime); return 1;
}
