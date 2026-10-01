#include "LuiRuntime.h"

#include <cstdio>
#include <string>

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:ThemeTest] %s\n", Message);
    return Condition ? 0 : 1;
}

int main() {
    LuiRuntime* Runtime = Lui_Create();
    if (!Runtime) return 1;
    int Failures = 0;
    Failures += Check(Lui_SystemThemeChanged(Runtime, "Dark") == 1, "initial theme rejected");
    Failures += Check(Lui_RunScript(Runtime,
        "Theme = app:GetService('ThemeService')\n"
        "assert(Theme.CurrentTheme == 'Dark')\n"
        "Seen = {}\n"
        "Connection = Theme.ThemeChanged:Connect(function(Value) table.insert(Seen, Value) end)\n"
        "local Ok = pcall(function() Theme.CurrentTheme = 'Light' end)\n"
        "assert(not Ok)", "ThemeStart") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_SystemThemeChanged(Runtime, "Invalid") == 0, "invalid theme accepted");
    Failures += Check(Lui_SystemThemeChanged(Runtime, "HighContrast") == 1, "high contrast rejected");
    Lui_Pump(Runtime);
    Failures += Check(Lui_RunScript(Runtime,
        "assert(Theme.CurrentTheme == 'HighContrast' and Seen[1] == 'HighContrast')\n"
        "Connection:Disconnect()", "ThemeChanged") == 1, Lui_GetLastError(Runtime));
    Failures += Check(Lui_SystemThemeChanged(Runtime, "Light") == 1, "light theme rejected");
    Lui_Pump(Runtime);
    Failures += Check(Lui_RunScript(Runtime,
        "assert(Theme.CurrentTheme == 'Light' and #Seen == 1)", "ThemeDisconnected") == 1,
        Lui_GetLastError(Runtime));
    Lui_Destroy(Runtime);
    if (!Failures) std::puts("[LUI:ThemeTest] System theme changes and listeners passed");
    return Failures ? 1 : 0;
}
