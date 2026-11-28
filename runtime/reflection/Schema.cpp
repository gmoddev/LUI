#include "Schema.h"

#include <cstddef>
#include <sstream>

namespace LuiSchema {

static constexpr MethodDefinition Globals[] = {
    {"", "require", "(Request: string) -> unknown"},
    {"", "Color3", "{ read new: (R: number, G: number, B: number) -> Color3, read fromRGB: (R: number, G: number, B: number) -> Color3 }"},
};

static constexpr ClassDefinition Classes[] = {
    {"Instance", "", false, false, false, "container"},
    {"Window", "Instance", true, true, true, "none"},
    {"GuiObject", "Instance", false, false, false, ""},
    {"Frame", "GuiObject", true, true, true, ""},
    {"TextLabel", "GuiObject", true, true, false, ""},
    {"GuiButton", "GuiObject", false, false, false, ""},
    {"TextButton", "GuiButton", true, true, false, ""},
    {"TextBox", "GuiObject", true, true, false, ""},
    {"CodeEditor", "TextBox", true, true, false, ""},
    {"CheckBox", "GuiObject", true, true, false, ""},
    {"Slider", "GuiObject", true, true, false, ""},
    {"ProgressBar", "GuiObject", true, true, false, ""},
    {"ImageLabel", "GuiObject", true, true, false, ""},
    {"FrameSurface", "GuiObject", true, true, false, ""},
    {"UIComponent", "Instance", false, false, false, ""},
    {"UIPadding", "UIComponent", true, false, false, ""},
    {"UIListLayout", "UIComponent", true, false, false, ""},
    {"UIGridLayout", "UIComponent", true, false, false, ""},
    {"UISizeConstraint", "UIComponent", true, false, false, "visual"},
    {"ScrollingFrame", "Frame", true, true, true, ""},
};

static constexpr MethodDefinition Methods[] = {
    {"Instance", "Destroy", "(Self: Instance) -> ()"},
    {"Instance", "Clone", "(Self: Instance) -> Instance"},
    {"Instance", "GetChildren", "(Self: Instance) -> {Instance}"},
    {"Instance", "GetDescendants", "(Self: Instance) -> {Instance}"},
    {"Instance", "FindFirstChild", "(Self: Instance, Name: string) -> Instance?"},
    {"Instance", "IsA", "(Self: Instance, ClassName: string) -> boolean"},
    {"GuiObject", "Focus", "(Self: GuiObject) -> boolean"},
    {"CodeEditor", "SetStyles", "(Self: CodeEditor, ExpectedRevision: number, Ranges: {{Start: number, End: number, Color: number}}) -> boolean"},
    {"Window", "RequestClose", "(Self: Window) -> (boolean, string?)"},
    {"Window", "PreventClose", "(Self: Window) -> boolean"},
    {"FrameSurface", "SubmitFrame", "(Self: FrameSurface, Pixels: buffer, Width: number, Height: number, RowStride: number, Format: string) -> (boolean, string?)"},
    {"FrameSurface", "ClearFrame", "(Self: FrameSurface) -> ()"},
};

static constexpr SignalDefinition Signals[] = {
    {"Instance", "Changed", "Signal"},
    {"Instance", "Destroying", "Signal"},
    {"Window", "Shortcut", "ShortcutSignal"},
    {"Window", "CloseRequested", "Signal"},
    {"Window", "Closed", "Signal"},
    {"GuiObject", "Focused", "Signal"},
    {"GuiObject", "FocusLost", "Signal"},
    {"GuiObject", "MouseEnter", "Signal"},
    {"GuiObject", "MouseLeave", "Signal"},
    {"GuiObject", "InputBegan", "InputSignal"},
    {"GuiObject", "InputChanged", "InputSignal"},
    {"GuiObject", "InputEnded", "InputSignal"},
    {"TextButton", "Activated", "Signal"},
    {"TextBox", "TextChanged", "Signal"},
    {"TextBox", "SelectionChanged", "Signal"},
    {"TextBox", "CompositionStarted", "Signal"},
    {"TextBox", "CompositionChanged", "Signal"},
    {"TextBox", "CompositionEnded", "Signal"},
    {"CheckBox", "Activated", "Signal"},
    {"CheckBox", "CheckedChanged", "Signal"},
    {"Slider", "ValueChanged", "Signal"},
    {"ScrollingFrame", "CanvasChanged", "Signal"},
    {"ImageLabel", "LoadChanged", "Signal"},
};

static constexpr PropertyDefinition Properties[] = {
    {"Instance", "Name", "string", "class name", false},
    {"Instance", "Sensitive", "boolean", "false", false},
    {"Instance", "EffectiveSensitive", "boolean", "derived sticky classification", true},
    {"Instance", "Parent", "Instance?", "nil", false},
    {"Instance", "ClassName", "string", "class name", true},
    {"Window", "Title", "string", "", false},
    {"Window", "Visible", "boolean", "false", false},
    {"Window", "IsClosed", "boolean", "false; terminal native lifetime", true},
    {"Window", "Size", "UDim2", "800x600", false},
    {"Window", "AbsolutePosition", "Vector2", "derived", true},
    {"Window", "AbsoluteSize", "Vector2", "derived", true},
    {"GuiObject", "Visible", "boolean", "true", false},
    {"GuiObject", "AccessibilityLabel", "string", "", false},
    {"GuiObject", "AccessibilityDescription", "string", "", false},
    {"GuiObject", "Size", "UDim2", "100% of parent", false},
    {"GuiObject", "Position", "UDim2", "0,0", false},
    {"GuiObject", "AnchorPoint", "Vector2", "0,0", false},
    {"GuiObject", "LayoutOrder", "number", "0", false},
    {"GuiObject", "BackgroundColor3", "Color3?", "nil (native/theme)", false},
    {"GuiObject", "BackgroundTransparency", "number", "0", false},
    {"GuiObject", "BorderColor3", "Color3?", "nil (native/theme)", false},
    {"GuiObject", "BorderSizePixel", "number", "0", false},
    {"GuiObject", "CornerRadius", "number", "0", false},
    {"GuiObject", "Opacity", "number", "1", false},
    {"GuiObject", "TextColor3", "Color3?", "nil (inherited/theme)", false},
    {"GuiObject", "TextSize", "number?", "nil (inherit; default 14 logical units)", false},
    {"GuiObject", "FontFamily", "string?", "nil (inherit; default System)", false},
    {"GuiObject", "FontWeight", "string?", "nil (inherit; Normal or Bold)", false},
    {"GuiObject", "TextWrapped", "boolean", "true", false},
    {"GuiObject", "TextXAlignment", "string", "Left", false},
    {"GuiObject", "TextYAlignment", "string", "Top", false},
    {"GuiObject", "ZIndex", "number", "0", false},
    {"GuiObject", "ClipsDescendants", "boolean", "false; ScrollingFrame always clips", false},
    {"ScrollingFrame", "CanvasSize", "UDim2", "0,0; effective canvas at least viewport", false},
    {"ScrollingFrame", "CanvasPosition", "Vector2", "0,0; requested offset", false},
    {"ScrollingFrame", "AbsoluteCanvasSize", "Vector2", "derived", true},
    {"ScrollingFrame", "AbsoluteCanvasPosition", "Vector2", "derived clamped offset", true},
    {"GuiObject", "IsFocused", "boolean", "false", true},
    {"GuiObject", "AbsolutePosition", "Vector2", "derived", true},
    {"GuiObject", "AbsoluteSize", "Vector2", "derived", true},
    {"TextLabel", "Text", "string", "", false},
    {"TextButton", "Text", "string", "", false},
    {"TextButton", "Enabled", "boolean", "true", false},
    {"TextBox", "Text", "string", "", false},
    {"TextBox", "Enabled", "boolean", "true", false},
    {"TextBox", "Multiline", "boolean", "false", false},
    {"TextBox", "Secret", "boolean", "false; implies sticky sensitivity", false},
    {"TextBox", "PlaceholderText", "string", "", false},
    {"TextBox", "MaxLength", "number", "65534 Unicode scalars; UTF-8 byte ceiling also 65534", false},
    {"TextBox", "SelectionStart", "number", "0; Unicode scalar offset", false},
    {"TextBox", "SelectionLength", "number", "0; Unicode scalar count", false},
    {"TextBox", "IsComposing", "boolean", "false", true},
    {"TextBox", "CompositionText", "string", "", true},
    {"CodeEditor", "Multiline", "boolean", "true; immutable", true},
    {"CodeEditor", "Secret", "boolean", "false; immutable", true},
    {"CodeEditor", "MaxLength", "number", "262144 Unicode scalars; UTF-8 byte ceiling also 262144", false},
    {"CodeEditor", "TextRevision", "number", "1; advances when committed text changes", true},
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
    {"ImageLabel", "Source", "string", "", false},
    {"ImageLabel", "ImageResource", "ImageResource?", "nil", false},
    {"ImageLabel", "SourceRectOffset", "Vector2", "0,0; top-left source pixel", false},
    {"ImageLabel", "SourceRectSize", "Vector2", "0,0; display the full image", false},
    {"ImageLabel", "LoadState", "string", "Idle; Loading, Ready, Failed, Canceled or Unavailable", true},
    {"FrameSurface", "ScaleMode", "string", "Fit; Fill or Stretch", false},
    {"FrameSurface", "FrameState", "string", "Empty; Pending, Ready or Failed (Ready never implies visible presentation)", true},
    {"UIListLayout", "Padding", "UDim", "0", false},
    {"UIListLayout", "FillDirection", "string", "Vertical", false},
    {"UIGridLayout", "CellSize", "UDim2", "100x100", false},
    {"UIGridLayout", "CellPadding", "UDim2", "0x0", false},
    {"UIPadding", "PaddingTop", "UDim", "0", false},
    {"UIPadding", "PaddingBottom", "UDim", "0", false},
    {"UIPadding", "PaddingLeft", "UDim", "0", false},
    {"UIPadding", "PaddingRight", "UDim", "0", false},
    {"UISizeConstraint", "MinSize", "Vector2", "0,0", false},
    {"UISizeConstraint", "MaxSize", "Vector2?", "nil (unbounded)", false},
};

static constexpr ServiceDefinition Services[] = {
    {"StorageService"},
    {"FileService"},
    {"WorkerService"},
    {"NotificationService"},
    {"TrayService"},
    {"PerformanceService"},
    {"WindowService"},
    {"PlatformService"},
    {"ThemeService"},
    {"ClipboardService"},
    {"DialogService"},
    {"AssetService"},
    {"NetworkService"},
    {"HttpService"},
    {"WebSocketService"},
    {"HttpServerService"},
};

static constexpr ServiceMethodDefinition ServiceMethods[] = {
    {"StorageService", "Open", "(Self: StorageService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"WorkerService", "Start", "(Self: WorkerService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"WorkerService", "StartTrusted", "(Self: WorkerService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"WorkerService", "Send", "(Self: WorkerService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "OpenFile", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "SaveFile", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "OpenFolder", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "Read", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "Write", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "List", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "Open", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "ReadRange", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "ByteLength", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "BeginWriter", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "WriteAt", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "CommitWriter", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "OpenOutput", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "CreateDirectory", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "RemoveDirectory", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "RemoveFile", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"FileService", "Rename", "(Self: FileService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"StorageService", "Read", "(Self: StorageService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"StorageService", "Write", "(Self: StorageService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"StorageService", "Remove", "(Self: StorageService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"NotificationService", "Show", "(Self: NotificationService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"TrayService", "Create", "(Self: TrayService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"TrayService", "Watch", "(Self: TrayService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest"},
    {"PerformanceService", "GetStats", "(Self: PerformanceService) -> PerformanceStats"},
    {"PerformanceService", "GetTime", "(Self: PerformanceService) -> number"},
    {"WindowService", "GetWindows", "(Self: WindowService) -> {Window}"},
    {"PlatformService", "Supports", "(Self: PlatformService, Capability: string) -> boolean"},
    {"ClipboardService", "WriteText", "(Self: ClipboardService, Text: string) -> boolean"},
    {"ClipboardService", "ReadText", "(Self: ClipboardService, Callback: (Text: string?, Error: string?) -> ()) -> ()"},
    {"DialogService", "OpenFile", "(Self: DialogService, Callback: (Path: string?, Error: string?) -> ()) -> ()"},
    {"AssetService", "Has", "(Self: AssetService, Name: string) -> boolean"},
    {"AssetService", "FromEncoded", "(Self: AssetService, Data: buffer) -> (ImageResource?, string?)"},
    {"AssetService", "FromPixels", "(Self: AssetService, Data: buffer, Width: number, Height: number, Stride: number, Format: string) -> (ImageResource?, string?)"},
    {"NetworkService", "ListenTcp", "(Self: NetworkService, Options: NetworkListenOptions) -> TcpListener"},
    {"NetworkService", "ConnectTcp", "(Self: NetworkService, Options: NetworkConnectOptions) -> TcpConnection"},
    {"NetworkService", "BindUdp", "(Self: NetworkService, Options: UdpBindOptions) -> UdpSocket"},
    {"HttpService", "RequestAsync", "(Self: HttpService, Options: HttpRequestOptions) -> HttpResponse"},
    {"HttpService", "GetAsync", "(Self: HttpService, Url: string) -> HttpResponse"},
    {"HttpService", "CancelAll", "(Self: HttpService) -> ()"},
    {"HttpService", "CreateCancellationToken", "(Self: HttpService) -> HttpCancellationToken"},
    {"WebSocketService", "ConnectAsync", "(Self: WebSocketService, Url: string, Options: WebSocketOptions?) -> WebSocket"},
    {"HttpServerService", "CreateServer", "(Self: HttpServerService, Options: HttpServerOptions) -> HttpServer"},
};

static constexpr ServicePropertyDefinition ServiceProperties[] = {
    {"PlatformService", "BackendName", "string"},
    {"PlatformService", "OperatingSystem", "string"},
    {"PlatformService", "Architecture", "string"},
    {"ThemeService", "CurrentTheme", "string"},
};

static constexpr ServiceSignalDefinition ServiceSignals[] = {
    {"ThemeService", "ThemeChanged", "ThemeSignal"},
};

static constexpr ServiceDefinition Objects[] = {
    {"ImageResource"},
    {"NativeHandle"},
    {"NativeRequest"},
    {"PerformanceStats"},
    {"Color3"},
    {"NetworkEndpoint"},
    {"NetworkListenOptions"},
    {"NetworkConnectOptions"},
    {"TcpListener"},
    {"TcpConnection"},
    {"UdpBindOptions"},
    {"UdpSocket"},
    {"UdpDatagram"},
    {"HttpHeader"},
    {"HttpCancellationToken"},
    {"HttpRequestOptions"},
    {"HttpResponse"},
    {"HttpServerOptions"},
    {"HttpReplyOptions"},
    {"HttpRequest"},
    {"HttpServer"},
    {"WebSocketOptions"},
    {"WebSocketServerOptions"},
    {"WebSocket"},
    {"WebSocketMessage"},
};

static constexpr ServicePropertyDefinition ObjectProperties[] = {
    {"ImageResource", "Width", "number"},
    {"ImageResource", "Height", "number"},
    {"ImageResource", "IsClosed", "boolean"},
    {"WebSocketOptions", "ConnectTimeoutMs", "number?"},
    {"WebSocketOptions", "CloseTimeoutMs", "number?"},
    {"WebSocketOptions", "IdleTimeoutMs", "number?"},
    {"WebSocketOptions", "PingIntervalMs", "number?"},
    {"WebSocketOptions", "PongTimeoutMs", "number?"},
    {"WebSocketOptions", "MaxMessageBytes", "number?"},
    {"WebSocketServerOptions", "MaxMessageBytes", "number?"},
    {"WebSocketServerOptions", "MaxFrameBytes", "number?"},
    {"WebSocketServerOptions", "MaxQueuedBytes", "number?"},
    {"WebSocketServerOptions", "MaxQueuedMessages", "number?"},
    {"WebSocketServerOptions", "CloseTimeoutMs", "number?"},
    {"WebSocketServerOptions", "IdleTimeoutMs", "number?"},
    {"WebSocketServerOptions", "PingIntervalMs", "number?"},
    {"WebSocketServerOptions", "PongTimeoutMs", "number?"},
    {"WebSocketServerOptions", "Protocols", "{string}?"},
    {"WebSocketServerOptions", "Origins", "{string}?"},
    {"WebSocketServerOptions", "Authorize", "((Request: HttpRequest) -> boolean)?"},
    {"WebSocketOptions", "MaxFrameBytes", "number?"},
    {"WebSocketOptions", "MaxQueuedBytes", "number?"},
    {"WebSocketOptions", "MaxQueuedMessages", "number?"},
    {"WebSocketOptions", "Origin", "string?"},
    {"WebSocketOptions", "Protocols", "{string}?"},
    {"WebSocketOptions", "Headers", "{HttpHeader}?"},
    {"WebSocketOptions", "CancellationToken", "HttpCancellationToken?"},
    {"WebSocket", "State", "\"Connecting\" | \"Open\" | \"Closing\" | \"Closed\""},
    {"WebSocket", "Subprotocol", "string"},
    {"WebSocket", "CloseCode", "number?"},
    {"WebSocket", "CloseReason", "string"},
    {"WebSocketMessage", "Data", "string"},
    {"WebSocketMessage", "Binary", "boolean"},
    {"PerformanceStats", "LiveInstances", "number"},
    {"PerformanceStats", "RetainedInstances", "number"},
    {"PerformanceStats", "Connections", "number"},
    {"PerformanceStats", "LuaMemoryBytes", "number"},
    {"PerformanceStats", "QueuedEvents", "number"},
    {"PerformanceStats", "QueuedBytes", "number"},
    {"PerformanceStats", "LayoutPasses", "number"},
    {"PerformanceStats", "PresentationCalls", "number"},
    {"PerformanceStats", "LayoutSamples", "number"},
    {"PerformanceStats", "LayoutP95Ms", "number"},
    {"PerformanceStats", "LastLayoutMs", "number"},
    {"PerformanceStats", "IsHeadless", "boolean"},
    {"PerformanceStats", "InputSamples", "number"},
    {"PerformanceStats", "InputP95Ms", "number"},
    {"PerformanceStats", "InputP99Ms", "number"},
    {"PerformanceStats", "RenderSamples", "number"},
    {"PerformanceStats", "RenderP95Ms", "number"},
    {"PerformanceStats", "RenderP99Ms", "number"},
    {"Color3", "R", "number"},
    {"Color3", "G", "number"},
    {"Color3", "B", "number"},
    {"HttpServerOptions", "Address", "string?"},
    {"HttpServerOptions", "Family", "(\"IPv4\" | \"IPv6\" | \"DualStack\")?"},
    {"HttpServerOptions", "Port", "number"},
    {"HttpServerOptions", "TLS", "boolean?"},
    {"HttpServerOptions", "TimeoutMs", "number?"},
    {"HttpServerOptions", "MaxConnections", "number?"},
    {"HttpServerOptions", "MaxRequestBytes", "number?"},
    {"HttpServerOptions", "MaxResponseBytes", "number?"},
    {"HttpReplyOptions", "StatusCode", "number?"},
    {"HttpReplyOptions", "StatusMessage", "string?"},
    {"HttpReplyOptions", "Headers", "{HttpHeader}?"},
    {"HttpReplyOptions", "Body", "(string | buffer)?"},
    {"HttpRequest", "Method", "string"},
    {"HttpRequest", "Path", "string"},
    {"HttpRequest", "RawTarget", "string"},
    {"HttpRequest", "HttpVersion", "string"},
    {"HttpRequest", "Headers", "{HttpHeader}"},
    {"HttpRequest", "Trailers", "{HttpHeader}"},
    {"HttpRequest", "Body", "string"},
    {"HttpRequest", "LocalEndpoint", "NetworkEndpoint"},
    {"HttpRequest", "RemoteEndpoint", "NetworkEndpoint"},
    {"HttpServer", "Port", "number"},
    {"HttpServer", "IsListening", "boolean"},
    {"HttpServer", "BoundEndpoints", "{NetworkEndpoint}"},
    {"HttpHeader", "Name", "string"},
    {"HttpHeader", "Value", "string"},
    {"HttpRequestOptions", "Url", "string"},
    {"HttpRequestOptions", "Method", "string?"},
    {"HttpRequestOptions", "Headers", "{HttpHeader}?"},
    {"HttpRequestOptions", "Body", "(string | buffer)?"},
    {"HttpRequestOptions", "TimeoutMs", "number?"},
    {"HttpRequestOptions", "MaxResponseBytes", "number?"},
    {"HttpRequestOptions", "CancellationToken", "HttpCancellationToken?"},
    {"HttpCancellationToken", "IsCanceled", "boolean"},
    {"HttpResponse", "StatusCode", "number"},
    {"HttpResponse", "StatusMessage", "string"},
    {"HttpResponse", "Success", "boolean"},
    {"HttpResponse", "Body", "string"},
    {"HttpResponse", "Headers", "{HttpHeader}"},
    {"HttpResponse", "Trailers", "{HttpHeader}"},
    {"NetworkEndpoint", "Address", "string"},
    {"NetworkEndpoint", "Port", "number"},
    {"NetworkListenOptions", "Address", "string?"},
    {"NetworkListenOptions", "Family", "(\"IPv4\" | \"IPv6\" | \"DualStack\")?"},
    {"NetworkListenOptions", "Port", "number"},
    {"NetworkConnectOptions", "Address", "string?"},
    {"NetworkConnectOptions", "Port", "number"},
    {"TcpListener", "IsListening", "boolean"},
    {"TcpListener", "Port", "number"},
    {"TcpListener", "BoundEndpoints", "{NetworkEndpoint}"},
    {"TcpConnection", "IsOpen", "boolean"},
    {"TcpConnection", "LocalEndpoint", "NetworkEndpoint"},
    {"TcpConnection", "RemoteEndpoint", "NetworkEndpoint"},
    {"UdpBindOptions", "Address", "string?"},
    {"UdpBindOptions", "Family", "(\"IPv4\" | \"IPv6\")?"},
    {"UdpBindOptions", "Port", "number"},
    {"UdpBindOptions", "MaxDatagramBytes", "number?"},
    {"UdpSocket", "IsOpen", "boolean"},
    {"UdpSocket", "LocalEndpoint", "NetworkEndpoint"},
    {"UdpDatagram", "Data", "buffer"},
    {"UdpDatagram", "RemoteEndpoint", "NetworkEndpoint"},
};

static constexpr ServiceMethodDefinition ObjectMethods[] = {
    {"ImageResource", "Close", "(Self: ImageResource) -> ()"},
    {"HttpServer", "RouteWebSocket", "(Self: HttpServer, Path: string, Handler: (Socket: WebSocket, Request: HttpRequest) -> (), Options: WebSocketServerOptions?) -> ()"},
    {"WebSocket", "SendAsync", "(Self: WebSocket, Data: string | buffer, Binary: boolean?) -> ()"},
    {"WebSocket", "ReceiveAsync", "(Self: WebSocket) -> WebSocketMessage?"},
    {"WebSocket", "Close", "(Self: WebSocket, Code: number?, Reason: string?) -> ()"},
    {"WebSocket", "Cancel", "(Self: WebSocket) -> ()"},
    {"NativeHandle", "Close", "(Self: NativeHandle) -> boolean"},
    {"NativeRequest", "Cancel", "(Self: NativeRequest) -> (boolean, string)"},
    {"HttpServer", "Route", "(Self: HttpServer, Method: string, Path: string, Handler: (Request: HttpRequest) -> HttpReplyOptions) -> ()"},
    {"HttpServer", "Start", "(Self: HttpServer) -> ()"},
    {"HttpServer", "Close", "(Self: HttpServer) -> ()"},
    {"HttpCancellationToken", "Cancel", "(Self: HttpCancellationToken) -> ()"},
    {"TcpListener", "AcceptAsync", "(Self: TcpListener) -> TcpConnection"},
    {"TcpListener", "Close", "(Self: TcpListener) -> ()"},
    {"TcpConnection", "ReadAsync", "(Self: TcpConnection, MaxBytes: number?) -> buffer?"},
    {"TcpConnection", "ReadExactAsync", "(Self: TcpConnection, Bytes: number) -> buffer"},
    {"TcpConnection", "WriteAsync", "(Self: TcpConnection, Data: string | buffer) -> ()"},
    {"TcpConnection", "Shutdown", "(Self: TcpConnection, Direction: \"Read\" | \"Write\" | \"Both\") -> ()"},
    {"TcpConnection", "Close", "(Self: TcpConnection) -> ()"},
    {"UdpSocket", "ReceiveFromAsync", "(Self: UdpSocket) -> UdpDatagram"},
    {"UdpSocket", "SendToAsync", "(Self: UdpSocket, Endpoint: NetworkEndpoint, Data: string | buffer) -> ()"},
    {"UdpSocket", "Close", "(Self: UdpSocket) -> ()"},
};

static constexpr ServiceSignalDefinition ObjectSignals[] = {
    {"TcpConnection", "Closed", "Signal"},
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

const MethodDefinition* FindMethod(const std::string& ClassName, const std::string& Name) {
    const ClassDefinition* Class = FindClass(ClassName);
    while (Class) {
        for (const auto& Method : Methods)
            if (Name == Method.Name && std::string(Class->Name) == Method.Owner) return &Method;
        Class = *Class->Base ? FindClass(Class->Base) : nullptr;
    }
    return nullptr;
}

const SignalDefinition* FindSignal(const std::string& ClassName, const std::string& Name) {
    const ClassDefinition* Class = FindClass(ClassName);
    while (Class) {
        for (const auto& Signal : Signals)
            if (Name == Signal.Name && std::string(Class->Name) == Signal.Owner) return &Signal;
        Class = *Class->Base ? FindClass(Class->Base) : nullptr;
    }
    return nullptr;
}

const char* GetParentRule(const std::string& ClassName) {
    const ClassDefinition* Class = FindClass(ClassName);
    while (Class) {
        if (*Class->ParentRule) return Class->ParentRule;
        Class = *Class->Base ? FindClass(Class->Base) : nullptr;
    }
    return "none";
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

template<typename Definition, std::size_t Count>
static void AppendDefinitions(std::ostringstream& Output, const Definition (&Entries)[Count], const char* Owner) {
    Output << '[';
    bool First = true;
    for (const auto& Entry : Entries) {
        if (std::string(Entry.Owner) != Owner) continue;
        if (!First) Output << ',';
        First = false;
        Output << "{\"name\":"; AppendQuoted(Output, Entry.Name);
        Output << ",\"type\":"; AppendQuoted(Output, Entry.Type);
        Output << '}';
    }
    Output << ']';
}

const std::string& GetJson() {
    static const std::string Json = [] {
        std::ostringstream Output;
        Output << "{\"schemaVersion\":2,\"classes\":[";
        bool FirstClass = true;
        for (const auto& Class : Classes) {
            if (!FirstClass) Output << ',';
            FirstClass = false;
            Output << "{\"name\":"; AppendQuoted(Output, Class.Name);
            Output << ",\"base\":"; AppendQuoted(Output, Class.Base);
            Output << ",\"creatable\":" << (Class.Creatable ? "true" : "false");
            Output << ",\"native\":" << (Class.Native ? "true" : "false");
            Output << ",\"acceptsChildren\":" << (Class.AcceptsChildren ? "true" : "false");
            Output << ",\"parentRule\":"; AppendQuoted(Output, GetParentRule(Class.Name));
            Output << ",\"methods\":"; AppendDefinitions(Output, Methods, Class.Name);
            Output << ",\"signals\":"; AppendDefinitions(Output, Signals, Class.Name);
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
            Output << ",\"methods\":"; AppendDefinitions(Output, ServiceMethods, Service.Name);
            Output << ",\"properties\":"; AppendDefinitions(Output, ServiceProperties, Service.Name);
            Output << ",\"signals\":"; AppendDefinitions(Output, ServiceSignals, Service.Name);
            Output << '}';
        }
        Output << "],\"objects\":[";
        bool FirstObject = true;
        for (const auto& Object : Objects) {
            if (!FirstObject) Output << ',';
            FirstObject = false;
            Output << "{\"name\":"; AppendQuoted(Output, Object.Name);
            Output << ",\"methods\":"; AppendDefinitions(Output, ObjectMethods, Object.Name);
            Output << ",\"properties\":"; AppendDefinitions(Output, ObjectProperties, Object.Name);
            Output << ",\"signals\":"; AppendDefinitions(Output, ObjectSignals, Object.Name);
            Output << '}';
        }
        Output << "],\"globals\":"; AppendDefinitions(Output, Globals, ""); Output << '}';
        return Output.str();
    }();
    return Json;
}

}
