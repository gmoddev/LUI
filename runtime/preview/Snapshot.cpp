#include "../internal/Dispatch.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
constexpr size_t MaxSnapshotBytes = 4 * 1024 * 1024;

void AppendString(std::string& Output, const std::string& Value) {
    Output += '"';
    for (size_t Index = 0; Index < Value.size(); ++Index) {
        const unsigned char Byte = static_cast<unsigned char>(Value[Index]);
        if (Byte == '"' || Byte == '\\') { Output += '\\'; Output += static_cast<char>(Byte); }
        else if (Byte < 0x20) {
            char Buffer[7];
            std::snprintf(Buffer, sizeof(Buffer), "\\u%04x", Byte);
            Output += Buffer;
        } else if (Byte < 0x80) Output += static_cast<char>(Byte);
        else {
            // Keep the protocol valid even when Luau strings contain arbitrary bytes.
            size_t Length = Byte >= 0xC2 && Byte <= 0xDF ? 2 :
                Byte >= 0xE0 && Byte <= 0xEF ? 3 : Byte >= 0xF0 && Byte <= 0xF4 ? 4 : 0;
            bool Valid = Length && Index + Length <= Value.size();
            if (Valid) for (size_t Offset = 1; Offset < Length; ++Offset)
                Valid &= (static_cast<unsigned char>(Value[Index + Offset]) & 0xC0) == 0x80;
            if (Valid && Length == 3) {
                const auto Next = static_cast<unsigned char>(Value[Index + 1]);
                Valid = !(Byte == 0xE0 && Next < 0xA0) && !(Byte == 0xED && Next >= 0xA0);
            }
            if (Valid && Length == 4) {
                const auto Next = static_cast<unsigned char>(Value[Index + 1]);
                Valid = !(Byte == 0xF0 && Next < 0x90) && !(Byte == 0xF4 && Next >= 0x90);
            }
            if (!Valid) Output += "\\ufffd";
            else { Output.append(Value, Index, Length); Index += Length - 1; }
        }
    }
    Output += '"';
}

void AppendNumber(std::string& Output, double Value) {
    if (!std::isfinite(Value)) { Output += "null"; return; }
    char Buffer[48];
    std::snprintf(Buffer, sizeof(Buffer), "%.17g", Value);
    Output += Buffer;
}

void AppendLocation(std::string& Output, const SourceLocation& Location, const std::string& Property = {}) {
    if (Location.Line <= 0 || Location.Source.empty()) { Output += "null"; return; }
    Output += "{\"source\":";
    AppendString(Output, Location.Source);
    Output += ",\"line\":" + std::to_string(Location.Line);
    if (!Property.empty()) {
        Output += ",\"property\":";
        AppendString(Output, Property);
    }
    Output += '}';
}

}

extern "C" LUI_API const char* LUI_CALL Lui_GetPreviewTreeJson(LuiRuntime* Runtime) {
    if (!CheckOwner(Runtime)) return nullptr;
    if (Runtime->VmDepth || Runtime->BackendDepth || Runtime->DrainingBackendEvents) {
        Runtime->LastError = "[LUI:Preview] Cannot snapshot during active dispatch";
        return nullptr;
    }
    FlushLayout(Runtime);
    std::vector<int> Ids;
    Ids.reserve(Runtime->Nodes.size());
    for (const auto& Pair : Runtime->Nodes) if (!Pair.second->Destroyed) Ids.push_back(Pair.first);
    std::sort(Ids.begin(), Ids.end());
    std::string& Output = Runtime->PreviewSnapshotJson;
    Output = "[";
    for (int Id : Ids) {
        const Node& Value = *Runtime->Nodes.at(Id);
        const bool Redacted=!Runtime->Inspecting && (Value.EffectiveSensitive || Runtime->DiagnosticMode==LUI_DIAGNOSTICS_RESTRICTED);
        if (Output.size() > MaxSnapshotBytes) {
            Output.clear();
            Runtime->LastError = "[LUI:Preview] Tree snapshot exceeds 4 MiB";
            return nullptr;
        }
        if (Output.size() > 1) Output += ',';
        Output += "{\"id\":" + std::to_string(Id) + ",\"parentId\":" + std::to_string(Value.ParentId);
        auto StringField = [&Output,Redacted](const char* Name, const std::string& Text) {
            Output += ",\""; Output += Name; Output += "\":"; AppendString(Output, Redacted && std::string(Name)!="className" ? "[Redacted]" : Text);
        };
        StringField("className", Value.ClassName);
        StringField("name", Value.Name);
        StringField("title", Value.Title);
        StringField("text", Value.Text);
        StringField("source", Value.Source);
        StringField("accessibilityLabel", Value.AccessibilityLabel);
        StringField("accessibilityDescription", Value.AccessibilityDescription);
        const bool Locations=(!Runtime->DiagnosticMode || Runtime->Modules) && !Redacted;
        Output += ",\"redacted\":"; Output+=Redacted?"true":"false";
        Output += ",\"sensitive\":"; Output+=Value.Sensitive?"true":"false";
        Output += ",\"effectiveSensitive\":"; Output+=Value.EffectiveSensitive?"true":"false";
        Output += ",\"createdAt\":"; if(Locations) AppendLocation(Output, Value.CreatedAt); else Output+="null";
        Output += ",\"lastChangedAt\":";
        if(Locations) AppendLocation(Output, Value.LastChangedAt, Value.LastChangedProperty); else Output+="null";
        Output += ",\"visible\":"; Output += Value.Visible ? "true" : "false";
        Output += ",\"enabled\":"; Output += Value.Enabled ? "true" : "false";
        Output += ",\"checked\":"; Output += Redacted ? "null" : Value.Checked ? "true" : "false";
        Output += ",\"isFocused\":"; Output += Value.IsFocused ? "true" : "false";
        Output += ",\"minimum\":"; if(Redacted) Output+="null"; else AppendNumber(Output, Value.Minimum);
        Output += ",\"maximum\":"; if(Redacted) Output+="null"; else AppendNumber(Output, Value.Maximum);
        Output += ",\"value\":"; if(Redacted) Output+="null"; else AppendNumber(Output, Value.Value);
        Output += ",\"bounds\":{\"x\":"; AppendNumber(Output, Value.Bounds.X);
        Output += ",\"y\":"; AppendNumber(Output, Value.Bounds.Y);
        Output += ",\"width\":"; AppendNumber(Output, Value.Bounds.Width);
        Output += ",\"height\":"; AppendNumber(Output, Value.Bounds.Height);
        Output += "}}";
    }
    Output += ']';
    if (Output.size() > MaxSnapshotBytes) {
        Output.clear();
        Runtime->LastError = "[LUI:Preview] Tree snapshot exceeds 4 MiB";
        return nullptr;
    }
    Runtime->LastError.clear();
    return Output.c_str();
}
extern "C" LUI_API int LUI_CALL Lui_BeginInspection(LuiRuntime* Runtime) {
    if(!CheckOwner(Runtime) || Runtime->DiagnosticMode!=LUI_DIAGNOSTICS_DEVELOPMENT || Runtime->ShuttingDown || Runtime->VmDepth || Runtime->BackendDepth) return 0;
    Runtime->InspectionGranted=true; return 1;
}
extern "C" LUI_API const char* LUI_CALL Lui_GetInspectionTreeJson(LuiRuntime* Runtime) {
    if(!CheckOwner(Runtime) || !Runtime->InspectionGranted || Runtime->DiagnosticMode!=LUI_DIAGNOSTICS_DEVELOPMENT) return nullptr;
    Runtime->Inspecting=true;
    struct InspectionGuard {
        LuiRuntime* Runtime;
        ~InspectionGuard() {Runtime->PreviewSnapshotJson.clear(); Runtime->Inspecting=false;}
    } Guard{Runtime};
    try {
    const auto* Result=Lui_GetPreviewTreeJson(Runtime);
    const bool Admitted=Result && Runtime->PreviewSnapshotJson.size()<=512*1024;
    if(Admitted) Runtime->InspectionSnapshot=Result; else Runtime->InspectionSnapshot.clear();
    Runtime->PreviewSnapshotJson.clear(); Runtime->Inspecting=false;
    return Admitted?Runtime->InspectionSnapshot.c_str():nullptr;
    } catch(...) {Runtime->InspectionSnapshot.clear(); return nullptr;}
}
