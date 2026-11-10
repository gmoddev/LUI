#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConfigResolver.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/TypeInfer.h"

#include <cstdio>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

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

int main(int Count, char** Arguments) {
    if (Count > 2) return Check(false, "expected at most one generated definition path");
    std::ifstream File(Count == 2 ? Arguments[1] : LUI_TYPES_PATH);
    if (!File) return Check(false, "generated definition file is missing");
    const std::string Definitions{std::istreambuf_iterator<char>{File}, std::istreambuf_iterator<char>{}};

    // Init aliases are emitted only for creatable classes. Exercise all of them
    // so inserting a new class cannot silently degrade a later constructor.
    std::vector<std::string> ConstructorNames;
    std::istringstream DefinitionLines(Definitions);
    for (std::string Line; std::getline(DefinitionLines, Line);) {
        constexpr const char* Prefix = "export type ";
        const size_t End = Line.find("Init = {");
        if (Line.rfind(Prefix, 0) == 0 && End != std::string::npos)
            ConstructorNames.push_back(Line.substr(std::char_traits<char>::length(Prefix), End - std::char_traits<char>::length(Prefix)));
    }
    std::string AllConstructors = "--!strict\n", UnknownMembers = "--!strict\n";
    for (size_t Index = 0; Index < ConstructorNames.size(); ++Index) {
        const std::string& Name = ConstructorNames[Index];
        AllConstructors += "local Exact" + Name + ": " + Name + " = Instance.new(\"" + Name + "\", {})\n";
        UnknownMembers += "local Constructed" + std::to_string(Index) + " = Instance.new(\"" + Name + "\", {})\n";
        UnknownMembers += "Constructed" + std::to_string(Index) + ".LUIUnknownMember = 1\n";
    }

    ScriptResolver Resolver;
    Resolver.Scripts.emplace("allConstructors", AllConstructors);
    Resolver.Scripts.emplace("constructorUnknownMembers", UnknownMembers);
    Resolver.Scripts.emplace("constructorUnion", R"(
        --!strict
        local Name: "ProgressBar" | "ImageLabel" = if app:GetService("PlatformService").BackendName == "" then "ProgressBar" else "ImageLabel"
        local Created: Instance = Instance.new(Name, {})
        local Exact: ProgressBar | ImageLabel = Instance.new(Name, {})
    )");
    Resolver.Scripts.emplace("constructorUnknownDynamic", R"(
        --!strict
        local Name: string = app:GetService("PlatformService").BackendName
        Instance.new(Name, {})
    )");
    Resolver.Scripts.emplace("valid", R"(
        --!strict
        local Window = Instance.new("Window", {Title = "Typed", Size = UDim2.fromOffset(480, 320)})
        Window.CloseRequested:Connect(function()
            local Prevented: boolean = Window:PreventClose()
        end)
        Window.Closed:Connect(function()
            local Closed: boolean = Window.IsClosed
        end)
        local AcceptedClose: boolean, CloseReason: string? = Window:RequestClose()
        Window.Shortcut:Connect(function(Input: KeyboardShortcut)
            local Key: string = Input.Key
            local Modifiers: boolean = Input.Control or Input.Shift or Input.Alt
            local Editable: boolean = Input.TextEditing
        end)
        local Root = Instance.new("Frame", {Parent = Window, Size = UDim2.fromScale(1, 1)})
        local EmptyFrame = Instance.new("Frame")
        local SurfaceProps: FrameSurfaceInit = {Parent = Root, ScaleMode = "Fit"}
        local Surface = Instance.new("FrameSurface", SurfaceProps)
        local Accepted: boolean, Failure: string? = Surface:SubmitFrame(buffer.create(4), 1, 1, 4, "BGRA8Premultiplied")
        local FrameState: string = Surface.FrameState
        Surface:ClearFrame()
        local ScrollProps: ScrollingFrameInit = {Parent = Root, CanvasSize = UDim2.fromOffset(500, 800),
            CanvasPosition = Vector2.new(0, 120), ClipsDescendants = true, ZIndex = 2}
        local Scroll = Instance.new("ScrollingFrame", ScrollProps)
        local Canvas: Vector2 = Scroll.AbsoluteCanvasSize
        local Offset: Vector2 = Scroll.AbsoluteCanvasPosition
        assert(Canvas.Y >= Offset.Y)
        local Input = Instance.new("TextBox", {Parent = Root, Text = "Name"})
        local SizeProps: UISizeConstraintInit = {Parent = Input, MinSize = Vector2.new(100, 30)}
        Instance.new("UISizeConstraint", SizeProps)
        local GridProps: UIGridLayoutInit = {Parent = Root, CellSize = UDim2.fromOffset(50, 40)}
        Instance.new("UIGridLayout", GridProps)
        local Slider = Instance.new("Slider", {Parent = Root, Minimum = 0, Maximum = 10, Value = 5})
        local Windows = app:GetService("WindowService"):GetWindows()
        local BackendName: string = app:GetService("PlatformService").BackendName
        local Http = app:GetService("HttpService")
        local Socket: UdpSocket = app:GetService("NetworkService"):BindUdp({Port = 0, Family = "IPv6", MaxDatagramBytes = 4096})
        task.spawn(function()
            local Datagram: UdpDatagram = Socket:ReceiveFromAsync()
            local Bytes: buffer = Datagram.Data
            local Endpoint: NetworkEndpoint = Datagram.RemoteEndpoint
            Socket:SendToAsync(Endpoint, Bytes)
            Socket:SendToAsync(Endpoint, "reply")
            Socket:Close()
        end)
        local Hosted = app:GetService("HttpServerService"):CreateServer({Port = 0, Family = "IPv4", MaxConnections = 4, TLS = true})
        Hosted:Route("POST", "/typed", function(Request: HttpRequest): HttpReplyOptions
            local Target: string = Request.RawTarget
            local Body: string = Request.Body
            return {StatusCode = 200, Body = Body, Headers = {{Name = "X-Target", Value = Target}}}
        end)
        Hosted:Start()
        local Port: number = Hosted.Port
        assert(Port >= 0)
        Hosted:Close()
        task.spawn(function()
            local Options: HttpRequestOptions = {Url = "http://localhost/", Method = "POST",
                Headers = {{Name = "Content-Type", Value = "application/octet-stream"}},
                Body = buffer.fromstring("data"), TimeoutMs = 5000, MaxResponseBytes = 1024}
            local Response: HttpResponse = Http:RequestAsync(Options)
            local Status: number = Response.StatusCode
            local Bytes: string = Response.Body
            local Headers: {HttpHeader} = Response.Headers
            assert(Status > 0 and #Bytes >= 0 and #Headers >= 0)
            Http:CancelAll()
        end)
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
    Resolver.Scripts.emplace("httpReadOnly", R"(
        --!strict
        local Response = app:GetService("HttpService"):GetAsync("http://localhost/")
        Response.Body = "changed"
        Response.Headers[1].Value = "changed"
    )");
    Resolver.Scripts.emplace("udpReadOnly", R"(
        --!strict
        local Datagram = app:GetService("NetworkService"):BindUdp({Port = 0}):ReceiveFromAsync()
        Datagram.Data = buffer.create(0)
        Datagram.RemoteEndpoint.Port = 42
    )");
    Resolver.Scripts.emplace("udpInvalidOptions", R"(
        --!strict
        app:GetService("NetworkService"):BindUdp({Port = "invalid", Family = "DualStack"})
    )");
    Resolver.Scripts.emplace("tlsInvalidOptions", R"(
        --!strict
        app:GetService("HttpServerService"):CreateServer({Port = 0, TLS = "true"})
    )");
    Resolver.Scripts.emplace("hostedReadOnly", R"(
        --!strict
        local Server = app:GetService("HttpServerService"):CreateServer({Port=0})
        Server:Route("POST", "/", function(Request)
            Request.Body = "changed"
            return {Body="OK"}
        end)
    )");
    Resolver.Scripts.emplace("unknownService", R"(
        --!strict
        app:GetService("MissingService")
    )");
    Resolver.Scripts.emplace("wrongService", R"(
        --!strict
        app:GetService("HttpServerService"):GetAsync("http://localhost/")
    )");
    Resolver.Scripts.emplace("httpInvalidOptions", R"(
        --!strict
        local Options: HttpRequestOptions = {Url = "http://localhost/", TimeoutMs = "forever"}
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
    Resolver.Scripts.emplace("abstractClass", R"(
        --!strict
        Instance.new("GuiObject", {})
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
    Resolver.Scripts.emplace("dynamicImportUnknown", R"(
        --!strict
        local Path: string = './Data'
        local Value: number = require(Path)
    )");
    Resolver.Scripts.emplace("dynamicImportRefined", R"(
        --!strict
        local Path: string = './Data'
        local Export = require(Path)
        if type(Export) == 'number' then local Value: number = Export end
    )");
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

    const auto ConstructorStarted = std::chrono::steady_clock::now();
    Luau::FrontendOptions ConstructorOptions;
    ConstructorOptions.moduleTimeLimitSec = 10.0;
    const auto Constructors = Frontend.check("allConstructors", ConstructorOptions);
    for (const Luau::TypeError& Error : Constructors.errors)
        std::fprintf(stderr, "[LUI:Types] constructor: %s\n", Luau::toString(Error).c_str());
    Failures += Check(!ConstructorNames.empty() && Constructors.errors.empty() && Constructors.timeoutHits.empty(),
        "a literal constructor did not return its exact generated class");
    const auto Members = Frontend.check("constructorUnknownMembers", ConstructorOptions);
    for (size_t Index = 0; Index < ConstructorNames.size(); ++Index) {
        bool Rejected = false;
        for (const auto& Error : Members.errors) Rejected |= Error.location.begin.line == 2 + 2 * Index;
        if (!Rejected) std::fprintf(stderr, "[LUI:Types] constructor accepted unknown member for %s\n", ConstructorNames[Index].c_str());
        Failures += Check(Rejected, "constructor result lost static member validation");
    }
    Failures += Check(Members.timeoutHits.empty(), "constructor member analysis exceeded its time limit");
    const auto ConstructorUnion = Frontend.check("constructorUnion", ConstructorOptions);
    for (const auto& Error : ConstructorUnion.errors)
        std::fprintf(stderr, "[LUI:Types] constructor union: %s\n", Luau::toString(Error).c_str());
    Failures += Check(ConstructorUnion.errors.empty() && ConstructorUnion.timeoutHits.empty(),
        "supported finite class-name union lost its precise constructor result");
    Failures += Check(!Frontend.check("constructorUnknownDynamic", ConstructorOptions).errors.empty(),
        "unconstrained dynamic class name typechecked");
    const double ConstructorMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - ConstructorStarted).count();
    std::printf("[LUI:Types] constructors=%zu checked_ms=%.3f (analysis only)\n", ConstructorNames.size(), ConstructorMilliseconds);

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
    Failures += Check(!Frontend.check("abstractClass").errors.empty(), "abstract class constructor typechecked");
    Failures += Check(!Frontend.check("invalidInit").errors.empty(), "invalid typed constructor properties typechecked");
    Failures += Check(!Frontend.check("invalidSizeInit").errors.empty(), "invalid size constraint properties typechecked");
    Failures += Check(!Frontend.check("invalidGridInit").errors.empty(), "invalid grid layout properties typechecked");
    Failures += Check(!Frontend.check("httpReadOnly").errors.empty(), "HTTP response mutation typechecked");
    Failures += Check(!Frontend.check("udpReadOnly").errors.empty(), "UDP datagram mutation typechecked");
    Failures += Check(!Frontend.check("udpInvalidOptions").errors.empty(), "invalid UDP options typechecked");
    Failures += Check(!Frontend.check("tlsInvalidOptions").errors.empty(), "invalid TLS options typechecked");
    Failures += Check(!Frontend.check("httpInvalidOptions").errors.empty(), "invalid HTTP options typechecked");
    Failures += Check(!Frontend.check("hostedReadOnly").errors.empty(), "hosted request mutation typechecked");
    Failures += Check(!Frontend.check("unknownService").errors.empty(), "unknown service typechecked");
    Failures += Check(!Frontend.check("wrongService").errors.empty(), "service overload returned an unrelated service type");
    Failures += Check(!Frontend.check("dynamicImportUnknown").errors.empty(), "dynamic require result was not unknown");
    Failures += Check(Frontend.check("dynamicImportRefined").errors.empty(), "refined dynamic require did not typecheck");
    if (!Failures) std::puts("[LUI:Types] Generated definitions passed Luau type checks");
    return Failures ? 1 : 0;
}
