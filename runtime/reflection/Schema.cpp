#include "Schema.h"

#include <sstream>

namespace LuiSchema {

static constexpr ClassDefinition Classes[] = {
    {"Instance", "", false, false, "Destroy,Clone,GetChildren,GetDescendants,FindFirstChild,IsA", "Changed,Destroying"},
    {"Window", "Instance", true, true, "", ""},
    {"GuiObject", "Instance", false, false, "", "Focused,FocusLost"},
    {"Frame", "GuiObject", true, true, "", ""},
    {"TextLabel", "GuiObject", true, true, "", ""},
    {"GuiButton", "GuiObject", false, false, "", ""},
    {"TextButton", "GuiButton", true, true, "", "Activated"},
    {"TextBox", "GuiObject", true, true, "", "TextChanged"},
    {"CheckBox", "GuiObject", true, true, "", "Activated,CheckedChanged"},
    {"Slider", "GuiObject", true, true, "", "ValueChanged"},
    {"ProgressBar", "GuiObject", true, true, "", ""},
    {"UIComponent", "Instance", false, false, "", ""},
    {"UIPadding", "UIComponent", true, false, "", ""},
    {"UIListLayout", "UIComponent", true, false, "", ""},
    {"UISizeConstraint", "UIComponent", true, false, "", ""},
};

static constexpr PropertyDefinition Properties[] = {
    {"Instance", "Name", "string", "class name", false},
    {"Instance", "Parent", "Instance?", "nil", false},
    {"Instance", "ClassName", "string", "class name", true},
    {"Window", "Title", "string", "", false},
    {"Window", "Visible", "boolean", "false", false},
    {"Window", "Size", "UDim2", "800x600", false},
    {"Window", "AbsolutePosition", "Vector2", "derived", true},
    {"Window", "AbsoluteSize", "Vector2", "derived", true},
    {"GuiObject", "Visible", "boolean", "true", false},
    {"GuiObject", "Size", "UDim2", "100% of parent", false},
    {"GuiObject", "Position", "UDim2", "0,0", false},
    {"GuiObject", "AnchorPoint", "Vector2", "0,0", false},
    {"GuiObject", "LayoutOrder", "number", "0", false},
    {"GuiObject", "IsFocused", "boolean", "false", true},
    {"GuiObject", "AbsolutePosition", "Vector2", "derived", true},
    {"GuiObject", "AbsoluteSize", "Vector2", "derived", true},
    {"TextLabel", "Text", "string", "", false},
    {"TextButton", "Text", "string", "", false},
    {"TextButton", "Enabled", "boolean", "true", false},
    {"TextBox", "Text", "string", "", false},
    {"TextBox", "Enabled", "boolean", "true", false},
    {"CheckBox", "Text", "string", "", false},
    {"CheckBox", "Checked", "boolean", "false", false},
    {"CheckBox", "Enabled", "boolean", "true", false},
    {"Slider", "Minimum", "number", "0", false},
    {"Slider", "Maximum", "number", "100", false},
    {"Slider", "Value", "number", "0", false},
    {"Slider", "Enabled", "boolean", "true", false},
    {"ProgressBar", "Minimum", "number", "0", false},
    {"ProgressBar", "Maximum", "number", "100", false},
    {"ProgressBar", "Value", "number", "0", false},
    {"UIListLayout", "Padding", "UDim", "0", false},
    {"UIListLayout", "FillDirection", "string", "Vertical", false},
    {"UIPadding", "PaddingTop", "UDim", "0", false},
    {"UIPadding", "PaddingBottom", "UDim", "0", false},
    {"UIPadding", "PaddingLeft", "UDim", "0", false},
    {"UIPadding", "PaddingRight", "UDim", "0", false},
    {"UISizeConstraint", "MinSize", "Vector2", "0,0", false},
    {"UISizeConstraint", "MaxSize", "Vector2?", "nil (unbounded)", false},
};

static constexpr ServiceDefinition Services[] = {
    {"WindowService", "GetWindows"},
    {"PlatformService", "Supports"},
};

const ClassDefinition* FindClass(const std::string& Name) {
    for (const auto& Class : Classes) if (Name == Class.Name) return &Class;
    return nullptr;
}

bool IsA(const std::string& ClassName, const std::string& BaseName) {
    const ClassDefinition* Class = FindClass(ClassName);
    while (Class) {
        if (BaseName == Class->Name) return true;
        Class = *Class->Base ? FindClass(Class->Base) : nullptr;
    }
    return false;
}

const PropertyDefinition* FindProperty(const std::string& ClassName, const std::string& Name) {
    const ClassDefinition* Class = FindClass(ClassName);
    while (Class) {
        for (const auto& Property : Properties) {
            if (Name == Property.Name && std::string(Class->Name) == Property.Owner) return &Property;
        }
        Class = *Class->Base ? FindClass(Class->Base) : nullptr;
    }
    return nullptr;
}

bool IsNative(const std::string& ClassName) {
    const ClassDefinition* Class = FindClass(ClassName);
    return Class && Class->Native;
}

const ServiceDefinition* FindService(const std::string& Name) {
    for (const auto& Service : Services) if (Name == Service.Name) return &Service;
    return nullptr;
}

static void AppendQuoted(std::ostringstream& Output, const char* Text) {
    Output << '"';
    for (const char* Cursor = Text; *Cursor; ++Cursor) {
        if (*Cursor == '"' || *Cursor == '\\') Output << '\\';
        if (*Cursor == '\n') Output << "\\n";
        else Output << *Cursor;
    }
    Output << '"';
}

static void AppendList(std::ostringstream& Output, const char* Text) {
    Output << '[';
    bool First = true;
    const char* Start = Text;
    for (const char* Cursor = Text; ; ++Cursor) {
        if (*Cursor == ',' || *Cursor == '\0') {
            if (Cursor != Start) {
                if (!First) Output << ',';
                const std::string Value(Start, Cursor);
                AppendQuoted(Output, Value.c_str());
                First = false;
            }
            if (*Cursor == '\0') break;
            Start = Cursor + 1;
        }
    }
    Output << ']';
}

const std::string& GetJson() {
    static const std::string Json = [] {
        std::ostringstream Output;
        Output << "{\"schemaVersion\":1,\"classes\":[";
        bool FirstClass = true;
        for (const auto& Class : Classes) {
            if (!FirstClass) Output << ',';
            FirstClass = false;
            Output << "{\"name\":"; AppendQuoted(Output, Class.Name);
            Output << ",\"base\":"; AppendQuoted(Output, Class.Base);
            Output << ",\"creatable\":" << (Class.Creatable ? "true" : "false");
            Output << ",\"native\":" << (Class.Native ? "true" : "false");
            Output << ",\"methods\":"; AppendList(Output, Class.Methods);
            Output << ",\"signals\":"; AppendList(Output, Class.Signals);
            Output << ",\"properties\":[";
            bool FirstProperty = true;
            for (const auto& Property : Properties) {
                if (std::string(Property.Owner) != Class.Name) continue;
                if (!FirstProperty) Output << ',';
                FirstProperty = false;
                Output << "{\"name\":"; AppendQuoted(Output, Property.Name);
                Output << ",\"type\":"; AppendQuoted(Output, Property.Type);
                Output << ",\"default\":"; AppendQuoted(Output, Property.Default);
                Output << ",\"readOnly\":" << (Property.ReadOnly ? "true" : "false") << '}';
            }
            Output << "]}";
        }
        Output << "],\"services\":[";
        bool FirstService = true;
        for (const auto& Service : Services) {
            if (!FirstService) Output << ',';
            FirstService = false;
            Output << "{\"name\":"; AppendQuoted(Output, Service.Name);
            Output << ",\"methods\":"; AppendList(Output, Service.Methods);
            Output << '}';
        }
        Output << "]}";
        return Output.str();
    }();
    return Json;
}

}
