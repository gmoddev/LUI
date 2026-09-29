#pragma once

#include <string>

namespace LuiSchema {

struct ClassDefinition {
    const char* Name;
    const char* Base;
    bool Creatable;
    bool Native;
    const char* Methods;
    const char* Signals;
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
    const char* Methods;
};

const ClassDefinition* FindClass(const std::string& Name);
const PropertyDefinition* FindProperty(const std::string& ClassName, const std::string& Name);
bool IsA(const std::string& ClassName, const std::string& BaseName);
bool IsNative(const std::string& ClassName);
const ServiceDefinition* FindService(const std::string& Name);
const std::string& GetJson();

}
