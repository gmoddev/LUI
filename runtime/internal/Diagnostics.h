#pragma once
#include <string>
struct LuiRuntime;
struct Node;
void FailBackend(LuiRuntime* Runtime,const char* Operation,int Id);
std::string ProjectError(const LuiRuntime* Runtime,const std::string& Message);
void DeliverDiagnostic(LuiRuntime* Runtime,const char* Level,const std::string& Message,bool Explicit=false);
void ClassifyNode(LuiRuntime* Runtime,Node* Value);
struct DiagnosticText : std::string {
    LuiRuntime* Owner=nullptr;
    DiagnosticText& operator=(const std::string& Value) {std::string::operator=(ProjectError(Owner,Value)); return *this;}
    DiagnosticText& operator=(std::string&& Value) {return operator=(static_cast<const std::string&>(Value));}
    DiagnosticText& operator=(const char* Value) {return operator=(std::string(Value?Value:""));}
    DiagnosticText& operator+=(const std::string& Value) {return operator=(static_cast<const std::string&>(*this)+Value);}
};
