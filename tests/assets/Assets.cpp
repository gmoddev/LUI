#include "LuiRuntime.h"

#include <cstdio>
#include <string>

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:AssetTest] %s\n", Message);
    return Condition ? 0 : 1;
}

int main() {
    LuiRuntime* Runtime = Lui_Create();
    if (!Runtime) return 1;
    int Failures = 0;
    Failures += Check(Lui_RegisterAsset(Runtime, "logo.png") == 1, "asset registration failed");
    Failures += Check(Lui_RegisterAsset(Runtime, "logo.png") == 0, "duplicate asset accepted");
    Failures += Check(Lui_RegisterAsset(Runtime, "../outside.png") == 0, "asset path escaped package");
    Failures += Check(Lui_RunScript(Runtime,
        "local Assets = app:GetService('AssetService')\n"
        "assert(Assets:Has('logo.png') and not Assets:Has('missing.png'))\n"
        "local Image = Instance.new('ImageLabel', {Source = 'logo.png'})\n"
        "assert(Image.Source == 'logo.png')\n"
        "local Copy = Image:Clone()\n"
        "assert(Copy.Source == 'logo.png')\n"
        "local Ok, Error = pcall(function() Image.Source = 'missing.png' end)\n"
        "assert(not Ok and string.find(Error, 'not declared'))\n"
        "Copy:Destroy(); Image:Destroy()", "AssetSemantics") == 1,
        Lui_GetLastError(Runtime));
    Failures += Check(Lui_RegisterAsset(Runtime, "late.png") == 0, "late asset registration accepted");
    Lui_Destroy(Runtime);
    if (!Failures) std::puts("[LUI:AssetTest] Declared asset and ImageLabel semantics passed");
    return Failures ? 1 : 0;
}
