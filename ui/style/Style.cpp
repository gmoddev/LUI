#include "Style.h"
#include "../../runtime/internal/State.h"
#include "../../runtime/internal/Diagnostics.h"
#include "../../runtime/reflection/Schema.h"
#include <lua.h>
#include <lualib.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstring>

namespace LuiStyle {
using Json = nlohmann::json;
static const Json Defaults = {
    {"BackgroundColor3", nullptr}, {"BackgroundTransparency", 0.0}, {"BorderColor3", nullptr},
    {"BorderSizePixel", 0.0}, {"CornerRadius", 0.0}, {"Opacity", 1.0}, {"TextColor3", nullptr},
    {"TextSize", nullptr}, {"FontFamily", nullptr}, {"FontWeight", nullptr},
    {"TextWrapped", true}, {"TextXAlignment", "Left"}, {"TextYAlignment", "Top"},
};
bool IsProperty(const std::string& Name) {return Defaults.contains(Name);}
static void PushColor(lua_State* State, const Json& Color) {
    lua_createtable(State, 0, 3);
    for (int Index = 0; Index < 3; ++Index) {
        lua_pushnumber(State, Color[Index].get<double>()); lua_setfield(State, -2, Index == 0 ? "R" : Index == 1 ? "G" : "B");
    }
    lua_setreadonly(State, -1, true);
}
static int Color(lua_State* State, double Divisor) {
    Json Channels = Json::array();
    for (int Index = 1; Index <= 3; ++Index) {
        const double Channel = luaL_checknumber(State, Index);
        if (!std::isfinite(Channel) || Channel < 0 || Channel > Divisor) luaL_error(State, "[LUI:Style] Color channel out of range");
        Channels.push_back(Channel / Divisor);
    }
    PushColor(State, Channels); return 1;
}
static int ColorNew(lua_State* State) {return Color(State, 1);}
static int ColorRgb(lua_State* State) {return Color(State, 255);}
void Register(lua_State* State) {
    lua_newtable(State); lua_pushcfunction(State, ColorNew, "Color3.new"); lua_setfield(State, -2, "new");
    lua_pushcfunction(State, ColorRgb, "Color3.fromRGB"); lua_setfield(State, -2, "fromRGB");
    lua_setreadonly(State, -1, true); lua_setglobal(State, "Color3");
}
static Json Read(lua_State* State, const std::string& Name, int Index) {
    if (Name == "BackgroundColor3" || Name == "BorderColor3" || Name == "TextColor3") {
        if (lua_isnil(State, Index)) return nullptr;
        if (lua_type(State, Index) != LUA_TTABLE) luaL_error(State, "[LUI:Style] Expected Color3");
        Json Result = Json::array();
        for (const char* Field : {"R", "G", "B"}) {
            lua_getfield(State, Index, Field);
            if (lua_type(State, -1) != LUA_TNUMBER) luaL_error(State, "[LUI:Style] Expected Color3");
            const double Channel = lua_tonumber(State, -1); lua_pop(State, 1);
            if (!std::isfinite(Channel) || Channel < 0 || Channel > 1) luaL_error(State, "[LUI:Style] Color channel out of range");
            Result.push_back(Channel);
        }
        return Result;
    }
    if (Name == "TextSize" || Name == "Opacity" || Name == "BackgroundTransparency" || Name == "BorderSizePixel" || Name == "CornerRadius") {
        if (Name == "TextSize" && lua_isnil(State, Index)) return nullptr;
        if (lua_type(State, Index) != LUA_TNUMBER) luaL_error(State, "[LUI:Style] Expected number");
        const double Number = lua_tonumber(State, Index);
        const double Minimum = Name == "TextSize" ? 1 : 0;
        const double Maximum = Name == "TextSize" ? 256 : Name == "CornerRadius" ? 256 : Name == "BorderSizePixel" ? 64 : 1;
        if (!std::isfinite(Number) || Number < Minimum || Number > Maximum) luaL_error(State, "[LUI:Style] Number out of range");
        return Number;
    }
    if (Name == "TextWrapped") {
        if (lua_type(State, Index) != LUA_TBOOLEAN) luaL_error(State, "[LUI:Style] Expected boolean");
        return lua_toboolean(State, Index) != 0;
    }
    if ((Name == "FontFamily" || Name == "FontWeight") && lua_isnil(State, Index)) return nullptr;
    if (lua_type(State, Index) != LUA_TSTRING) luaL_error(State, "[LUI:Style] Expected string");
    size_t Bytes = 0; const char* Text = lua_tolstring(State, Index, &Bytes);
    const std::string Result(Text, Bytes);
    if (Name == "FontFamily") {
        if (Bytes == 0 || Bytes > 128) luaL_error(State, "[LUI:Style] Invalid font family");
        for (unsigned char Character : Result)
            if (!((Character >= 'A' && Character <= 'Z') || (Character >= 'a' && Character <= 'z') ||
                (Character >= '0' && Character <= '9') || Character == ' ' || Character == '-' || Character == '_'))
                luaL_error(State, "[LUI:Style] Invalid font family");
    } else if ((Name == "FontWeight" && Result != "Normal" && Result != "Bold") ||
        (Name == "TextXAlignment" && Result != "Left" && Result != "Center" && Result != "Right") ||
        (Name == "TextYAlignment" && Result != "Top" && Result != "Center" && Result != "Bottom"))
        luaL_error(State, "[LUI:Style] Invalid enum value");
    return Result;
}
bool Set(lua_State* State, Node* Value, const std::string& Name, int Index) {
    if (!Defaults.contains(Name)) return false;
    Value->StyleProperties[Name] = Read(State, Name, Index).dump(); return true;
}
bool Push(lua_State* State, const Node* Value, const std::string& Name) {
    if (!Defaults.contains(Name)) return false;
    const auto Found = Value->StyleProperties.find(Name);
    const Json Property = Found == Value->StyleProperties.end() ? Defaults[Name] : Json::parse(Found->second);
    if (Property.is_null()) lua_pushnil(State);
    else if (Property.is_array()) PushColor(State, Property);
    else if (Property.is_boolean()) lua_pushboolean(State, Property.get<bool>());
    else if (Property.is_number()) lua_pushnumber(State, Property.get<double>());
    else lua_pushstring(State, Property.get_ref<const std::string&>().c_str());
    return true;
}
void Project(LuiRuntime* Runtime, Node* Value) {
    if (Runtime->BackendFailed || !LuiSchema::IsA(Value->ClassName, "GuiObject")) return;
    Json Style = Defaults;
    for (const auto& Property : Value->StyleProperties) Style[Property.first] = Json::parse(Property.second);
    for (const char* Name : {"TextColor3", "TextSize", "FontFamily", "FontWeight"}) {
        for (const Node* Ancestor = Value; Ancestor; Ancestor = Ancestor->ParentId ? Runtime->Nodes.at(Ancestor->ParentId).get() : nullptr) {
            const auto Found = Ancestor->StyleProperties.find(Name);
            if (Found != Ancestor->StyleProperties.end()) {
                const Json Candidate = Json::parse(Found->second);
                if (!Candidate.is_null()) {Style[Name] = Candidate; break;}
            }
        }
    }
    if (Style["TextSize"].is_null()) Style["TextSize"] = 14;
    if (Style["FontFamily"].is_null()) Style["FontFamily"] = "System";
    if (Style["FontWeight"].is_null()) Style["FontWeight"] = "Normal";
    Style["HighContrast"] = Runtime->CurrentTheme == "HighContrast";
    const std::string Projection = Style.dump();
    if (Projection == Value->StyleProjection) return;
    Value->StyleProjection = Projection;
    if (Runtime->Presentation.Present && Runtime->Backend.Property) {
        Runtime->BackendError.clear();
        if (!Runtime->Backend.Property(Runtime->Backend.Context, Value->Id, "__Style", Projection.c_str())) FailBackend(Runtime, "Style", Value->Id);
    }
}
}
