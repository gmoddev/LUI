#include "Application.hpp"

#include <cstdio>
#include <string>

int main() {
    try {
        lui::Application App;
        int Calls = 0;
        App.Service("Launcher")
            .Function("Launch", [&Calls](std::string Name, int Count) {
                ++Calls;
                return Name + ":" + std::to_string(Count);
            })
            .Function("Ready", [] { return true; });
        App.Service("Launcher").Signal<std::string>("Started");
        const std::string Schema = Lui_GetExtensionSchemaJson(App.NativeRuntime());
        if (Schema.find("Launcher") == std::string::npos ||
            Schema.find("Ready") == std::string::npos) return 1;
        App.RunSource(
            "local L = app:GetService('Launcher')\n"
            "assert(L:Ready() and L:Launch('demo', 2) == 'demo:2')\n"
            "local Ok, Error = pcall(function() L:Launch(false, 2) end)\n"
            "assert(not Ok and string.find(Error, 'expected string'))\n"
            "Ok, Error = pcall(function() L:Launch('demo') end)\n"
            "assert(not Ok and string.find(Error, 'wrong number'))\n"
            "Ok, Error = pcall(function() L:Launch('demo', 2.5) end)\n"
            "assert(not Ok and string.find(Error, 'outside native range'))\n"
            "StartedName = ''\n"
            "StartedConnection = L.Started:Connect(function(Name) StartedName = Name end)\n", "HostBinding");
        App.Emit("Launcher", "Started", std::string("demo"));
        App.RunSource("assert(StartedName == 'demo'); StartedConnection:Disconnect()", "HostSignal");
        if (Calls != 1) return 1;
        try {
            App.Service("Late").Function("Value", [] { return 1; });
            return 1;
        } catch (const std::runtime_error&) {}
        App.RunSource("assert(app:GetService('Launcher'):Launch('again', 3) == 'again:3')", "HostStillUsable");
        if (Calls != 2) return 1;
        std::puts("[LUI:HostTest] C++ host service and reflection passed");
        return 0;
    } catch (const std::exception& Error) {
        std::fprintf(stderr, "[LUI:HostTest] %s\n", Error.what());
        return 1;
    }
}
