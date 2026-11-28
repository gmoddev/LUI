#include "../internal/Images.h"
#include "../internal/Dispatch.h"
#include "../internal/Modules.h"
#include "ImageLimits.h"
#include <lua.h>
#include <lualib.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

struct ImageSourceDomain {
    std::atomic<size_t> Bytes{0}, Count{0};
};
struct LuiImageSource {
    std::shared_ptr<ImageSourceDomain> Domain;
    std::atomic<size_t> References{1};
    std::unique_ptr<unsigned char[]> Data;
    size_t Length;
    uint64_t Identity;
    uint32_t Kind, Width, Height, Stride;
    ~LuiImageSource() {
        Data.reset();
        Domain->Bytes.fetch_sub(Length);
        Domain->Count.fetch_sub(1);
    }
};
struct ImageResourceHandle {
    uint32_t Width, Height;
    std::shared_ptr<LuiImageSource> Source;
};

namespace {
constexpr char ImageMeta[] = "LuiImageResource";
constexpr size_t MaxSourceBytes = 64*1024*1024, MaxSourceCount = 1024;
using HandlePtr = std::shared_ptr<ImageResourceHandle>;

HandlePtr* FindImage(lua_State* State, int Index) {
    if (lua_type(State, Index) != LUA_TUSERDATA || !lua_getmetatable(State, Index)) return nullptr;
    lua_getfield(State, LUA_REGISTRYINDEX, ImageMeta);
    const bool Matches = lua_rawequal(State, -1, -2);
    lua_pop(State, 2);
    return Matches ? static_cast<HandlePtr*>(lua_touserdata(State, Index)) : nullptr;
}
void PushHandle(lua_State* State, const HandlePtr& Handle) {
    if (!Handle) {lua_pushnil(State); return;}
    auto* Storage = lua_newuserdatadtor(State, sizeof(HandlePtr), +[](lua_State*, void* Data) {
        static_cast<HandlePtr*>(Data)->~HandlePtr();
    });
    new (Storage) HandlePtr(Handle);
    lua_getfield(State, LUA_REGISTRYINDEX, ImageMeta);
    lua_setmetatable(State, -2);
}
int ImageClose(lua_State* State) {
    CheckModuleEffect(State);
    auto* Box = FindImage(State, 1);
    if (!Box) luaL_error(State, "[LUI:Image] ExpectedResource");
    (*Box)->Source.reset();
    return 0;
}
int ImageIndex(lua_State* State) {
    const auto* Box = FindImage(State, 1);
    if (!Box) luaL_error(State, "[LUI:Image] ExpectedResource");
    const auto& Handle = **Box;
    std::string_view Key = luaL_checkstring(State, 2);
    if (Key=="Width") lua_pushinteger(State, Handle.Width);
    else if (Key=="Height") lua_pushinteger(State, Handle.Height);
    else if (Key=="IsClosed") lua_pushboolean(State, !Handle.Source);
    else if (Key=="Close") lua_pushcfunction(State, ImageClose, "ImageResource.Close");
    else lua_pushnil(State);
    return 1;
}
int ImageEqual(lua_State* State) {
    const auto* A = FindImage(State, 1);
    const auto* B = FindImage(State, 2);
    lua_pushboolean(State, A && B && *A==*B);
    return 1;
}
int Reject(lua_State* State, const char* Code) {
    lua_pushnil(State);
    lua_pushstring(State, Code);
    return 2;
}
bool Integer(double Value, uint32_t Maximum) {
    return Value >= 1 && Value <= Maximum && std::floor(Value)==Value;
}
int CreateImage(lua_State* State) {
    CheckModuleEffect(State);
    auto* Runtime = static_cast<LuiRuntime*>(lua_touserdata(State, lua_upvalueindex(1)));
    const bool Raw = lua_toboolean(State, lua_upvalueindex(2));
    size_t Length = 0;
    const auto* Input = static_cast<const unsigned char*>(lua_tobuffer(State, 2, &Length));
    if (!Input || Length==0) return Reject(State, "InvalidArgument");
    if (Runtime->ShuttingDown) return Reject(State, "Closed");
    uint32_t Width=0, Height=0, InputStride=0, Stride=0;
    size_t Bytes = Length;
    if (Raw) {
        const double W=luaL_checknumber(State, 3), H=luaL_checknumber(State, 4), Pitch=luaL_checknumber(State, 5);
        size_t FormatLength=0;
        const char* Format=luaL_checklstring(State, 6, &FormatLength);
        if (std::string_view(Format, FormatLength)!="BGRA8Premultiplied") return Reject(State, "Unsupported");
        if (!Integer(W, 4096) || !Integer(H, 4096) || !Integer(Pitch, 8*1024*1024))
            return Reject(State, "InvalidArgument");
        Width=static_cast<uint32_t>(W); Height=static_cast<uint32_t>(H); InputStride=static_cast<uint32_t>(Pitch);
        Stride=Width*4;
        if (InputStride < Stride || InputStride%4 || uint64_t(InputStride)*Height!=Length)
            return Reject(State, "InvalidArgument");
        if (uint64_t(Width)*Height>LuiImages::MaxPixels || Length>8*1024*1024)
            return Reject(State, "LimitExceeded");
        Bytes=size_t(Stride)*Height;
    } else {
        if (Length>LuiImages::MaxEncoded) return Reject(State, "LimitExceeded");
        if (!LuiImages::Dimensions({reinterpret_cast<const char*>(Input), Length}, Width, Height))
            return Reject(State, "InvalidImage");
    }
    try {
        if (!Runtime->ImageAssets) {
            static thread_local std::weak_ptr<ImageSourceDomain> OwnerDomain;
            Runtime->ImageAssets=OwnerDomain.lock();
            if (!Runtime->ImageAssets) {
                Runtime->ImageAssets=std::make_shared<ImageSourceDomain>();
                OwnerDomain=Runtime->ImageAssets;
            }
        }
        auto Domain = Runtime->ImageAssets;
        // Only the owner increases the counters. Host leases may release them
        // from another thread after the VM has closed.
        if (Bytes > MaxSourceBytes - Domain->Bytes.load() || Domain->Count.load()>=MaxSourceCount ||
            Runtime->NextImageIdentity==std::numeric_limits<uint64_t>::max()) return Reject(State, "LimitExceeded");
        auto Handle = std::make_shared<ImageResourceHandle>();
        std::unique_ptr<unsigned char[]> Data(new unsigned char[Bytes]);
        if (!Raw || InputStride==Stride) std::memcpy(Data.get(), Input, Bytes);
        else for (uint32_t Row=0; Row<Height; ++Row)
            std::memcpy(Data.get()+size_t(Row)*Stride, Input+size_t(Row)*InputStride, Stride);
        auto* Source = new LuiImageSource{Domain, 1, std::move(Data), Bytes, Runtime->NextImageIdentity++,
            Raw?2u:1u, Width, Height, Stride};
        Domain->Bytes.fetch_add(Bytes);
        Domain->Count.fetch_add(1);
        Handle->Width=Width; Handle->Height=Height;
        Handle->Source=std::shared_ptr<LuiImageSource>(Source, Lui_ReleaseImageSourceV1);
        PushHandle(State, Handle);
        lua_pushnil(State);
        return 2;
    } catch (const std::bad_alloc&) {
        return Reject(State, "LimitExceeded");
    }
}
}

void RegisterImageResource(lua_State* State) {
    lua_newtable(State);
    lua_pushcfunction(State, ImageIndex, "ImageResource"); lua_setfield(State, -2, "__index");
    lua_pushcfunction(State, ImageEqual, "ImageResource.__eq"); lua_setfield(State, -2, "__eq");
    lua_pushliteral(State, "ImageResource"); lua_setfield(State, -2, "__metatable");
    lua_setfield(State, LUA_REGISTRYINDEX, ImageMeta);
}
void AddImageAssetMethods(lua_State* State, LuiRuntime* Runtime) {
    for (const auto* Name : {"FromEncoded", "FromPixels"}) {
        lua_pushlightuserdata(State, Runtime);
        lua_pushboolean(State, std::string_view(Name)=="FromPixels");
        lua_pushcclosure(State, CreateImage, Name, 2);
        lua_setfield(State, -2, Name);
    }
}
void SetImageResource(lua_State* State, Node* Value, int Index) {
    const bool Clear = lua_isnil(State, Index);
    auto* Handle = Clear ? nullptr : FindImage(State, Index);
    if (!Clear && !Handle) luaL_error(State, "[LUI:Image] ExpectedResource");
    if (Handle && !(*Handle)->Source) luaL_error(State, "[LUI:Image] Closed");
    Value->ImageHandle = Handle?*Handle:HandlePtr{};
    Value->RuntimeImage = Handle?(*Handle)->Source:nullptr;
}
void PushImageResource(lua_State* State, const Node* Value) {PushHandle(State, Value->ImageHandle);}
size_t RuntimeImageBytes(const LuiRuntime* Runtime) {
    return Runtime->ImageAssets ? Runtime->ImageAssets->Bytes.load() : 0;
}
size_t RuntimeImageCount(const LuiRuntime* Runtime) {
    return Runtime->ImageAssets ? Runtime->ImageAssets->Count.load() : 0;
}

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
    Runtime->ImageSignalsPending = true;
    if (Runtime->Images.Cancel) {BackendGuard Guard(Runtime); Runtime->Images.Cancel(Runtime->Images.Context, Token);}
}
void StartImage(LuiRuntime* Runtime, Node* Value) {
    CancelImage(Runtime, Value);
    Value->ImageStatePending = true;
    Runtime->ImageSignalsPending = true;
    const bool Resource = bool(Value->RuntimeImage);
    if (Value->Source.empty() && !Resource) {Value->LoadState = "Idle"; return;}
    if (Resource ? !Runtime->ImageSources.Load : !Runtime->Images.Load) {Value->LoadState = "Unavailable"; return;}
    if (Runtime->PendingImages >= 32 || Runtime->NextImageToken == std::numeric_limits<uint64_t>::max()) {
        Value->LoadState = "Failed"; return;
    }
    Value->ImageToken = Runtime->NextImageToken++; ++Runtime->PendingImages;
    Value->LoadState = "Loading";
    BackendGuard Guard(Runtime);
    const int Accepted = Resource
        ? Runtime->ImageSources.Load(Runtime->ImageSources.Context, Value->Id, Value->ImageToken, Value->RuntimeImage.get())
        : Runtime->Images.Load(Runtime->Images.Context, Value->Id, Value->ImageToken, Value->Source.c_str());
    if (!Accepted) {
        CancelImage(Runtime, Value); Value->LoadState = "Failed";
    }
}
extern "C" LUI_API int LUI_CALL Lui_SetImageSourceCallbacksV1(LuiRuntime* Runtime, const LuiImageSourceCallbacksV1* Callbacks) {
    if (!CheckOwner(Runtime) || !Callbacks || Callbacks->StructSize!=sizeof(*Callbacks) || Callbacks->AbiVersion!=1 ||
        !Callbacks->Load || !Runtime->Images.Cancel || Callbacks->Context!=Runtime->Images.Context ||
        Runtime->ImageSources.Load || Runtime->ApplicationStarted ||
        Runtime->NextNodeId!=1 || Runtime->VmDepth || Runtime->BackendDepth || Runtime->ShuttingDown) return 0;
    Runtime->ImageSources=*Callbacks;
    return 1;
}
extern "C" LUI_API int LUI_CALL Lui_ImageSourceViewV1(LuiImageSource* Source, LuiImageSourceViewV1* View) {
    if (!Source || !View || View->StructSize!=sizeof(*View) || View->AbiVersion!=1) return 0;
    *View = {sizeof(*View), 1, Source->Kind, Source->Width, Source->Height, Source->Stride,
        Source->Identity, Source->Length, Source->Data.get()};
    return 1;
}
extern "C" LUI_API void LUI_CALL Lui_RetainImageSourceV1(LuiImageSource* Source) {
    if (Source) Source->References.fetch_add(1, std::memory_order_relaxed);
}
extern "C" LUI_API void LUI_CALL Lui_ReleaseImageSourceV1(LuiImageSource* Source) {
    if (Source && Source->References.fetch_sub(1, std::memory_order_acq_rel)==1) delete Source;
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
