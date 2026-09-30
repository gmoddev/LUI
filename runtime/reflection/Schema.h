#pragma once

#include <string>

namespace LuiSchema {

struct ClassDefinition {
    const char* Name;
    const char* Base;
    bool Creatable;
    bool Native;
    bool AcceptsChildren;
    const char* ParentRule;
};

struct MethodDefinition {
    const char* Owner;
    const char* Name;
    const char* Type;
};

struct SignalDefinition {
    const char* Owner;
    const char* Name;
    const char* Type;
};

struct PropertyDefinition {
    const char* Owner;
    const char* Name;
    const char* Type;
    const char* Default;
    bool ReadOnly;
};

struct ServiceDefinition {
    const char* Name;
};

struct ServiceMethodDefinition {
    const char* Owner;
    const char* Name;
    const char* Type;
};

struct ServicePropertyDefinition {
    const char* Owner;
    const char* Name;
    const char* Type;
};

const ClassDefinition* FindClass(const std::string& Name);
const PropertyDefinition* FindProperty(const std::string& ClassName, const std::string& Name);
const MethodDefinition* FindMethod(const std::string& ClassName, const std::string& Name);
const SignalDefinition* FindSignal(const std::string& ClassName, const std::string& Name);
const char* GetParentRule(const std::string& ClassName);
bool IsA(const std::string& ClassName, const std::string& BaseName);
bool IsNative(const std::string& ClassName);
const ServiceDefinition* FindService(const std::string& Name);
const std::string& GetJson();

}
