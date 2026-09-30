#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConfigResolver.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/TypeInfer.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>

struct ScriptResolver : Luau::FileResolver {
    std::unordered_map<std::string, std::string> Scripts;

    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& Name) override {
        auto Found = Scripts.find(Name);
        if (Found == Scripts.end()) return std::nullopt;
        return Luau::SourceCode{Found->second, Luau::SourceCode::Script};
    }
};

static int Check(bool Condition, const char* Message) {
    if (!Condition) std::fprintf(stderr, "[LUI:Types] %s\n", Message);
    return Condition ? 0 : 1;
}

int main() {
    std::ifstream File(LUI_TYPES_PATH);
    if (!File) return Check(false, "generated definition file is missing");
    const std::string Definitions{std::istreambuf_iterator<char>{File}, std::istreambuf_iterator<char>{}};

    ScriptResolver Resolver;
    Resolver.Scripts.emplace("valid", R"(
        --!strict
        local Window = Instance.new("Window", {Title = "Typed", Size = UDim2.fromOffset(480, 320)})
        local Root = Instance.new("Frame", {Parent = Window, Size = UDim2.fromScale(1, 1)})
        local EmptyFrame = Instance.new("Frame")
        local Input = Instance.new("TextBox", {Parent = Root, Text = "Name"})
        local SizeProps: UISizeConstraintInit = {Parent = Input, MinSize = Vector2.new(100, 30)}
        Instance.new("UISizeConstraint", SizeProps)
        local GridProps: UIGridLayoutInit = {Parent = Root, CellSize = UDim2.fromOffset(50, 40)}
        Instance.new("UIGridLayout", GridProps)
        local Slider = Instance.new("Slider", {Parent = Root, Minimum = 0, Maximum = 10, Value = 5})
        local Windows = app:GetService("WindowService"):GetWindows()
        local BackendName: string = app:GetService("PlatformService").BackendName
        Input.TextChanged:Connect(function() Slider.Value = 6 end)
        Input.InputBegan:Connect(function(Event)
            if Event.Device == "Keyboard" then
                local Key: string = Event.Key
                assert(not Event.IsRepeat and #Key > 0)
            else
                local Position: Vector2 = Event.Position
                local PointerId: number = Event.PointerId
                local Canceled: boolean = Event.IsCanceled
                assert(Position.X >= 0 and PointerId >= 0 and not Canceled)
            end
        end)
        Window.Visible = #Windows > 0 and BackendName ~= ""
        local Width: number = Root.AbsoluteSize.X
        assert(Width >= 0)
        EmptyFrame:Destroy()
    )");
    Resolver.Scripts.emplace("readOnly", R"(
        --!strict
        local Root = Instance.new("Frame", {})
        Root.AbsoluteSize = Vector2.new(1, 1)
    )");
    Resolver.Scripts.emplace("inputReadOnly", R"(
        --!strict
        local Input = Instance.new("TextBox", {})
        Input.InputBegan:Connect(function(Event)
            if Event.Device ~= "Keyboard" then Event.PointerId = 5 end
        end)
    )");
    Resolver.Scripts.emplace("keyboardReadOnly", R"(
        --!strict
        local Input = Instance.new("TextBox", {})
        Input.InputBegan:Connect(function(Event)
            if Event.Device == "Keyboard" then Event.Key = "A" end
        end)
    )");
    Resolver.Scripts.emplace("unknownClass", R"(
        --!strict
        Instance.new("NotAClass", {})
    )");
    Resolver.Scripts.emplace("init", R"(
        --!strict
        local Props: WindowInit = {Title = "Typed", Size = UDim2.fromOffset(480, 320)}
    )");
    Resolver.Scripts.emplace("invalidInit", R"(
        --!strict
        local Props: WindowInit = {Title = 42}
    )");
    Resolver.Scripts.emplace("invalidSizeInit", R"(
        --!strict
        local Props: UISizeConstraintInit = {MinSize = 42}
    )");
    Resolver.Scripts.emplace("invalidGridInit", R"(
        --!strict
        local Props: UIGridLayoutInit = {CellSize = Vector2.new(5, 5)}
    )");

    Luau::NullConfigResolver Config;
    Config.defaultConfig.mode = Luau::Mode::Strict;
    Luau::Frontend Frontend(Luau::SolverMode::New, &Resolver, &Config);
    Luau::unfreeze(Frontend.globals.globalTypes);
    Luau::registerBuiltinGlobals(Frontend, Frontend.globals);
    Luau::LoadDefinitionFileResult Loaded = Frontend.loadDefinitionFile(
        Frontend.globals, Frontend.globals.globalScope, Definitions, "LUI", false);
    Luau::freeze(Frontend.globals.globalTypes);
    for (const Luau::ParseError& Error : Loaded.parseResult.errors)
        std::fprintf(stderr, "[LUI:Types] definition line %u: %s\n",
            Error.getLocation().begin.line + 1, Error.getMessage().c_str());
    if (Loaded.module) {
        for (const Luau::TypeError& Error : Loaded.module->errors)
            std::fprintf(stderr, "[LUI:Types] definition line %u: %s\n",
                Error.location.begin.line + 1, Luau::toString(Error).c_str());
    }
    int Failures = Check(Loaded.success, "generated definitions failed Luau validation");
    if (Failures) return 1;

    Luau::CheckResult Init = Frontend.check("init");
    for (const Luau::TypeError& Error : Init.errors)
        std::fprintf(stderr, "[LUI:Types] init: %s\n", Luau::toString(Error).c_str());
    Failures += Check(Init.errors.empty(), "valid typed constructor properties did not typecheck");

    Luau::CheckResult Valid = Frontend.check("valid");
    for (const Luau::TypeError& Error : Valid.errors)
        std::fprintf(stderr, "[LUI:Types] valid script line %u: %s\n",
            Error.location.begin.line + 1, Luau::toString(Error).c_str());
    Failures += Check(Valid.errors.empty(), "valid application did not typecheck");
    Luau::CheckResult ReadOnly = Frontend.check("readOnly");
    bool FoundReadOnlyError = false;
    for (const Luau::TypeError& Error : ReadOnly.errors)
        FoundReadOnlyError |= Luau::toString(Error).find("read-only") != std::string::npos;
    Failures += Check(FoundReadOnlyError, "read-only property assignment was not rejected as read-only");
    Failures += Check(!Frontend.check("inputReadOnly").errors.empty(), "read-only input payload assignment typechecked");
    Failures += Check(!Frontend.check("keyboardReadOnly").errors.empty(), "read-only keyboard payload assignment typechecked");
    Luau::CheckResult UnknownClass = Frontend.check("unknownClass");
    Failures += Check(!UnknownClass.errors.empty(), "unknown class typechecked");
    Failures += Check(!Frontend.check("invalidInit").errors.empty(), "invalid typed constructor properties typechecked");
    Failures += Check(!Frontend.check("invalidSizeInit").errors.empty(), "invalid size constraint properties typechecked");
    Failures += Check(!Frontend.check("invalidGridInit").errors.empty(), "invalid grid layout properties typechecked");
    if (!Failures) std::puts("[LUI:Types] Generated definitions passed Luau type checks");
    return Failures ? 1 : 0;
}
