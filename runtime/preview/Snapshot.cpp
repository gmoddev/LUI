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
        if (Output.size() > MaxSnapshotBytes) {
            Output.clear();
            Runtime->LastError = "[LUI:Preview] Tree snapshot exceeds 4 MiB";
            return nullptr;
        }
        if (Output.size() > 1) Output += ',';
        Output += "{\"id\":" + std::to_string(Id) + ",\"parentId\":" + std::to_string(Value.ParentId);
        auto StringField = [&Output](const char* Name, const std::string& Text) {
            Output += ",\""; Output += Name; Output += "\":"; AppendString(Output, Text);
        };
        StringField("className", Value.ClassName);
        StringField("name", Value.Name);
        StringField("title", Value.Title);
        StringField("text", Value.Text);
        StringField("source", Value.Source);
        StringField("accessibilityLabel", Value.AccessibilityLabel);
        StringField("accessibilityDescription", Value.AccessibilityDescription);
        Output += ",\"createdAt\":"; AppendLocation(Output, Value.CreatedAt);
        Output += ",\"lastChangedAt\":";
        AppendLocation(Output, Value.LastChangedAt, Value.LastChangedProperty);
        Output += ",\"visible\":"; Output += Value.Visible ? "true" : "false";
        Output += ",\"enabled\":"; Output += Value.Enabled ? "true" : "false";
        Output += ",\"checked\":"; Output += Value.Checked ? "true" : "false";
        Output += ",\"isFocused\":"; Output += Value.IsFocused ? "true" : "false";
        Output += ",\"minimum\":"; AppendNumber(Output, Value.Minimum);
        Output += ",\"maximum\":"; AppendNumber(Output, Value.Maximum);
        Output += ",\"value\":"; AppendNumber(Output, Value.Value);
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
