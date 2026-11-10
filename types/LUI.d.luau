-- Generated from LuiRuntime reflection metadata. Do not edit by hand.
export type Connection = { read Disconnect: (Self: Connection) -> () }
export type Signal = { read Connect: (Self: Signal, Callback: (...any) -> ()) -> Connection }
export type Vector2 = { read X: number, read Y: number }
export type UDim = { read Scale: number, read Offset: number }
export type UDim2 = { read X: UDim, read Y: UDim }
export type PointerInput = { read Device: "Mouse" | "Pen" | "Touch" | "Touchpad", read PointerId: number, read Position: Vector2, read IsCanceled: boolean, read Button: ("Left" | "Right" | "Middle" | "Auxiliary1" | "Auxiliary2")? }
export type KeyboardInput = { read Device: "Keyboard", read Key: string, read IsRepeat: boolean }
export type InputEvent = PointerInput | KeyboardInput
export type InputSignal = { read Connect: (Self: InputSignal, Callback: (Input: InputEvent) -> ()) -> Connection }
export type KeyboardShortcut = { read Device: "Keyboard", read Key: string, read IsRepeat: boolean, read Control: boolean, read Shift: boolean, read Alt: boolean, read TextEditing: boolean }
export type ShortcutSignal = { read Connect: (Self: ShortcutSignal, Callback: (Input: KeyboardShortcut) -> ()) -> Connection }
export type ThemeSignal = { read Connect: (Self: ThemeSignal, Callback: (Theme: string) -> ()) -> Connection }

export type Instance = {
    Name: string,
    Sensitive: boolean,
    read EffectiveSensitive: boolean,
    Parent: Instance?,
    read ClassName: string,
    read Destroy: (Self: Instance) -> (),
    read Clone: (Self: Instance) -> Instance,
    read GetChildren: (Self: Instance) -> {Instance},
    read GetDescendants: (Self: Instance) -> {Instance},
    read FindFirstChild: (Self: Instance, Name: string) -> Instance?,
    read IsA: (Self: Instance, ClassName: string) -> boolean,
    read Changed: Signal,
    read Destroying: Signal,
}

export type Window = Instance & {
    Title: string,
    Visible: boolean,
    read IsClosed: boolean,
    Size: UDim2,
    read AbsolutePosition: Vector2,
    read AbsoluteSize: Vector2,
    read RequestClose: (Self: Window) -> (boolean, string?),
    read PreventClose: (Self: Window) -> boolean,
    read Shortcut: ShortcutSignal,
    read CloseRequested: Signal,
    read Closed: Signal,
}

export type GuiObject = Instance & {
    Visible: boolean,
    AccessibilityLabel: string,
    AccessibilityDescription: string,
    Size: UDim2,
    Position: UDim2,
    AnchorPoint: Vector2,
    LayoutOrder: number,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number,
    BorderColor3: Color3?,
    BorderSizePixel: number,
    CornerRadius: number,
    Opacity: number,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean,
    TextXAlignment: string,
    TextYAlignment: string,
    ZIndex: number,
    ClipsDescendants: boolean,
    read IsFocused: boolean,
    read AbsolutePosition: Vector2,
    read AbsoluteSize: Vector2,
    read Focus: (Self: GuiObject) -> boolean,
    read Focused: Signal,
    read FocusLost: Signal,
    read MouseEnter: Signal,
    read MouseLeave: Signal,
    read InputBegan: InputSignal,
    read InputChanged: InputSignal,
    read InputEnded: InputSignal,
}

export type Frame = GuiObject & {
}

export type TextLabel = GuiObject & {
    Text: string,
}

export type GuiButton = GuiObject & {
}

export type TextButton = GuiButton & {
    Text: string,
    Enabled: boolean,
    read Activated: Signal,
}

export type TextBox = GuiObject & {
    Text: string,
    Enabled: boolean,
    Multiline: boolean,
    Secret: boolean,
    PlaceholderText: string,
    MaxLength: number,
    SelectionStart: number,
    SelectionLength: number,
    read IsComposing: boolean,
    read CompositionText: string,
    read TextChanged: Signal,
    read SelectionChanged: Signal,
    read CompositionStarted: Signal,
    read CompositionChanged: Signal,
    read CompositionEnded: Signal,
}

export type CodeEditor = TextBox & {
    read Multiline: boolean,
    read Secret: boolean,
    MaxLength: number,
    read TextRevision: number,
    read SetStyles: (Self: CodeEditor, ExpectedRevision: number, Ranges: {{Start: number, End: number, Color: number}}) -> boolean,
}

export type CheckBox = GuiObject & {
    Text: string,
    Checked: boolean,
    Enabled: boolean,
    read Activated: Signal,
    read CheckedChanged: Signal,
}

export type Slider = GuiObject & {
    Minimum: number,
    Maximum: number,
    Value: number,
    Enabled: boolean,
    read ValueChanged: Signal,
}

export type ProgressBar = GuiObject & {
    Minimum: number,
    Maximum: number,
    Value: number,
}

export type ImageLabel = GuiObject & {
    Source: string,
    read LoadState: string,
    read LoadChanged: Signal,
}

export type FrameSurface = GuiObject & {
    ScaleMode: string,
    read FrameState: string,
    read SubmitFrame: (Self: FrameSurface, Pixels: buffer, Width: number, Height: number, RowStride: number, Format: string) -> (boolean, string?),
    read ClearFrame: (Self: FrameSurface) -> (),
}

export type UIComponent = Instance & {
}

export type UIPadding = UIComponent & {
    PaddingTop: UDim,
    PaddingBottom: UDim,
    PaddingLeft: UDim,
    PaddingRight: UDim,
}

export type UIListLayout = UIComponent & {
    Padding: UDim,
    FillDirection: string,
}

export type UIGridLayout = UIComponent & {
    CellSize: UDim2,
    CellPadding: UDim2,
}

export type UISizeConstraint = UIComponent & {
    MinSize: Vector2,
    MaxSize: Vector2?,
}

export type ScrollingFrame = Frame & {
    CanvasSize: UDim2,
    CanvasPosition: Vector2,
    read AbsoluteCanvasSize: Vector2,
    read AbsoluteCanvasPosition: Vector2,
    read CanvasChanged: Signal,
}

export type StorageService = {
    read Open: (Self: StorageService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest,
    read Read: (Self: StorageService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest,
    read Write: (Self: StorageService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest,
    read Remove: (Self: StorageService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest,
}

export type NotificationService = {
    read Show: (Self: NotificationService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest,
}

export type TrayService = {
    read Create: (Self: TrayService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest,
    read Watch: (Self: TrayService, Input: buffer, Handle: NativeHandle?, Complete: (buffer?, NativeHandle?, string?) -> (), Progress: ((buffer) -> ())?) -> NativeRequest,
}

export type PerformanceService = {
    read GetStats: (Self: PerformanceService) -> PerformanceStats,
    read GetTime: (Self: PerformanceService) -> number,
}

export type WindowService = {
    read GetWindows: (Self: WindowService) -> {Window},
}

export type PlatformService = {
    read BackendName: string,
    read Supports: (Self: PlatformService, Capability: string) -> boolean,
}

export type ThemeService = {
    read CurrentTheme: string,
    read ThemeChanged: ThemeSignal,
}

export type ClipboardService = {
    read WriteText: (Self: ClipboardService, Text: string) -> boolean,
    read ReadText: (Self: ClipboardService, Callback: (Text: string?, Error: string?) -> ()) -> (),
}

export type DialogService = {
    read OpenFile: (Self: DialogService, Callback: (Path: string?, Error: string?) -> ()) -> (),
}

export type AssetService = {
    read Has: (Self: AssetService, Name: string) -> boolean,
}

export type NetworkService = {
    read ListenTcp: (Self: NetworkService, Options: NetworkListenOptions) -> TcpListener,
    read ConnectTcp: (Self: NetworkService, Options: NetworkConnectOptions) -> TcpConnection,
    read BindUdp: (Self: NetworkService, Options: UdpBindOptions) -> UdpSocket,
}

export type HttpService = {
    read RequestAsync: (Self: HttpService, Options: HttpRequestOptions) -> HttpResponse,
    read GetAsync: (Self: HttpService, Url: string) -> HttpResponse,
    read CancelAll: (Self: HttpService) -> (),
}

export type HttpServerService = {
    read CreateServer: (Self: HttpServerService, Options: HttpServerOptions) -> HttpServer,
}

export type NativeHandle = {
    read Close: (Self: NativeHandle) -> boolean,
}

export type NativeRequest = {
    read Cancel: (Self: NativeRequest) -> (boolean, string),
}

export type PerformanceStats = {
    read LiveInstances: number,
    read RetainedInstances: number,
    read Connections: number,
    read LuaMemoryBytes: number,
    read QueuedEvents: number,
    read QueuedBytes: number,
    read LayoutPasses: number,
    read PresentationCalls: number,
    read LayoutSamples: number,
    read LayoutP95Ms: number,
    read LastLayoutMs: number,
    read IsHeadless: boolean,
    read InputSamples: number,
    read InputP95Ms: number,
    read InputP99Ms: number,
    read RenderSamples: number,
    read RenderP95Ms: number,
    read RenderP99Ms: number,
}

export type Color3 = {
    read R: number,
    read G: number,
    read B: number,
}

export type NetworkEndpoint = {
    read Address: string,
    read Port: number,
}

export type NetworkListenOptions = {
    Address: string?,
    Family: ("IPv4" | "IPv6" | "DualStack")?,
    Port: number,
}

export type NetworkConnectOptions = {
    Address: string?,
    Port: number,
}

export type TcpListener = {
    read IsListening: boolean,
    read Port: number,
    read BoundEndpoints: {NetworkEndpoint},
    read AcceptAsync: (Self: TcpListener) -> TcpConnection,
    read Close: (Self: TcpListener) -> (),
}

export type TcpConnection = {
    read IsOpen: boolean,
    read LocalEndpoint: NetworkEndpoint,
    read RemoteEndpoint: NetworkEndpoint,
    read ReadAsync: (Self: TcpConnection, MaxBytes: number?) -> buffer?,
    read ReadExactAsync: (Self: TcpConnection, Bytes: number) -> buffer,
    read WriteAsync: (Self: TcpConnection, Data: string | buffer) -> (),
    read Shutdown: (Self: TcpConnection, Direction: "Read" | "Write" | "Both") -> (),
    read Close: (Self: TcpConnection) -> (),
    read Closed: Signal,
}

export type UdpBindOptions = {
    Address: string?,
    Family: ("IPv4" | "IPv6")?,
    Port: number,
    MaxDatagramBytes: number?,
}

export type UdpSocket = {
    read IsOpen: boolean,
    read LocalEndpoint: NetworkEndpoint,
    read ReceiveFromAsync: (Self: UdpSocket) -> UdpDatagram,
    read SendToAsync: (Self: UdpSocket, Endpoint: NetworkEndpoint, Data: string | buffer) -> (),
    read Close: (Self: UdpSocket) -> (),
}

export type UdpDatagram = {
    read Data: buffer,
    read RemoteEndpoint: NetworkEndpoint,
}

export type HttpHeader = {
    read Name: string,
    read Value: string,
}

export type HttpRequestOptions = {
    Url: string,
    Method: string?,
    Headers: {HttpHeader}?,
    Body: (string | buffer)?,
    TimeoutMs: number?,
    MaxResponseBytes: number?,
}

export type HttpResponse = {
    read StatusCode: number,
    read StatusMessage: string,
    read Success: boolean,
    read Body: string,
    read Headers: {HttpHeader},
    read Trailers: {HttpHeader},
}

export type HttpServerOptions = {
    Address: string?,
    Family: ("IPv4" | "IPv6" | "DualStack")?,
    Port: number,
    TLS: boolean?,
    TimeoutMs: number?,
    MaxConnections: number?,
    MaxRequestBytes: number?,
    MaxResponseBytes: number?,
}

export type HttpReplyOptions = {
    StatusCode: number?,
    StatusMessage: string?,
    Headers: {HttpHeader}?,
    Body: (string | buffer)?,
}

export type HttpRequest = {
    read Method: string,
    read Path: string,
    read RawTarget: string,
    read HttpVersion: string,
    read Headers: {HttpHeader},
    read Trailers: {HttpHeader},
    read Body: string,
    read LocalEndpoint: NetworkEndpoint,
    read RemoteEndpoint: NetworkEndpoint,
}

export type HttpServer = {
    read Port: number,
    read IsListening: boolean,
    read BoundEndpoints: {NetworkEndpoint},
    read Route: (Self: HttpServer, Method: string, Path: string, Handler: (Request: HttpRequest) -> HttpReplyOptions) -> (),
    read Start: (Self: HttpServer) -> (),
    read Close: (Self: HttpServer) -> (),
}

export type App = (((({ read GetService: (Self: App, Name: "StorageService") -> StorageService } & { read GetService: (Self: App, Name: "NotificationService") -> NotificationService }) & ({ read GetService: (Self: App, Name: "TrayService") -> TrayService } & { read GetService: (Self: App, Name: "PerformanceService") -> PerformanceService })) & (({ read GetService: (Self: App, Name: "WindowService") -> WindowService } & { read GetService: (Self: App, Name: "PlatformService") -> PlatformService }) & ({ read GetService: (Self: App, Name: "ThemeService") -> ThemeService } & { read GetService: (Self: App, Name: "ClipboardService") -> ClipboardService }))) & ((({ read GetService: (Self: App, Name: "DialogService") -> DialogService } & { read GetService: (Self: App, Name: "AssetService") -> AssetService }) & ({ read GetService: (Self: App, Name: "NetworkService") -> NetworkService } & { read GetService: (Self: App, Name: "HttpService") -> HttpService })) & { read GetService: (Self: App, Name: "HttpServerService") -> HttpServerService }))

export type WindowInit = {
    Name: string?,
    Sensitive: boolean?,
    Title: string?,
    Visible: boolean?,
    Size: UDim2?,
}

export type FrameInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number?,
    BorderColor3: Color3?,
    BorderSizePixel: number?,
    CornerRadius: number?,
    Opacity: number?,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean?,
    TextXAlignment: string?,
    TextYAlignment: string?,
    ZIndex: number?,
    ClipsDescendants: boolean?,
}

export type TextLabelInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number?,
    BorderColor3: Color3?,
    BorderSizePixel: number?,
    CornerRadius: number?,
    Opacity: number?,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean?,
    TextXAlignment: string?,
    TextYAlignment: string?,
    ZIndex: number?,
    ClipsDescendants: boolean?,
    Text: string?,
}

export type TextButtonInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number?,
    BorderColor3: Color3?,
    BorderSizePixel: number?,
    CornerRadius: number?,
    Opacity: number?,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean?,
    TextXAlignment: string?,
    TextYAlignment: string?,
    ZIndex: number?,
    ClipsDescendants: boolean?,
    Text: string?,
    Enabled: boolean?,
}

export type TextBoxInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number?,
    BorderColor3: Color3?,
    BorderSizePixel: number?,
    CornerRadius: number?,
    Opacity: number?,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean?,
    TextXAlignment: string?,
    TextYAlignment: string?,
    ZIndex: number?,
    ClipsDescendants: boolean?,
    Text: string?,
    Enabled: boolean?,
    Multiline: boolean?,
    Secret: boolean?,
    PlaceholderText: string?,
    MaxLength: number?,
    SelectionStart: number?,
    SelectionLength: number?,
}

export type CodeEditorInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number?,
    BorderColor3: Color3?,
    BorderSizePixel: number?,
    CornerRadius: number?,
    Opacity: number?,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean?,
    TextXAlignment: string?,
    TextYAlignment: string?,
    ZIndex: number?,
    ClipsDescendants: boolean?,
    Text: string?,
    Enabled: boolean?,
    PlaceholderText: string?,
    SelectionStart: number?,
    SelectionLength: number?,
    MaxLength: number?,
}

export type CheckBoxInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number?,
    BorderColor3: Color3?,
    BorderSizePixel: number?,
    CornerRadius: number?,
    Opacity: number?,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean?,
    TextXAlignment: string?,
    TextYAlignment: string?,
    ZIndex: number?,
    ClipsDescendants: boolean?,
    Text: string?,
    Checked: boolean?,
    Enabled: boolean?,
}

export type SliderInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number?,
    BorderColor3: Color3?,
    BorderSizePixel: number?,
    CornerRadius: number?,
    Opacity: number?,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean?,
    TextXAlignment: string?,
    TextYAlignment: string?,
    ZIndex: number?,
    ClipsDescendants: boolean?,
    Minimum: number?,
    Maximum: number?,
    Value: number?,
    Enabled: boolean?,
}

export type ProgressBarInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number?,
    BorderColor3: Color3?,
    BorderSizePixel: number?,
    CornerRadius: number?,
    Opacity: number?,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean?,
    TextXAlignment: string?,
    TextYAlignment: string?,
    ZIndex: number?,
    ClipsDescendants: boolean?,
    Minimum: number?,
    Maximum: number?,
    Value: number?,
}

export type ImageLabelInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number?,
    BorderColor3: Color3?,
    BorderSizePixel: number?,
    CornerRadius: number?,
    Opacity: number?,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean?,
    TextXAlignment: string?,
    TextYAlignment: string?,
    ZIndex: number?,
    ClipsDescendants: boolean?,
    Source: string?,
}

export type FrameSurfaceInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number?,
    BorderColor3: Color3?,
    BorderSizePixel: number?,
    CornerRadius: number?,
    Opacity: number?,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean?,
    TextXAlignment: string?,
    TextYAlignment: string?,
    ZIndex: number?,
    ClipsDescendants: boolean?,
    ScaleMode: string?,
}

export type UIPaddingInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    PaddingTop: UDim?,
    PaddingBottom: UDim?,
    PaddingLeft: UDim?,
    PaddingRight: UDim?,
}

export type UIListLayoutInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Padding: UDim?,
    FillDirection: string?,
}

export type UIGridLayoutInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    CellSize: UDim2?,
    CellPadding: UDim2?,
}

export type UISizeConstraintInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame | GuiObject)?,
    MinSize: Vector2?,
    MaxSize: Vector2?,
}

export type ScrollingFrameInit = {
    Name: string?,
    Sensitive: boolean?,
    Parent: (Window | Frame | ScrollingFrame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    BackgroundColor3: Color3?,
    BackgroundTransparency: number?,
    BorderColor3: Color3?,
    BorderSizePixel: number?,
    CornerRadius: number?,
    Opacity: number?,
    TextColor3: Color3?,
    TextSize: number?,
    FontFamily: string?,
    FontWeight: string?,
    TextWrapped: boolean?,
    TextXAlignment: string?,
    TextYAlignment: string?,
    ZIndex: number?,
    ClipsDescendants: boolean?,
    CanvasSize: UDim2?,
    CanvasPosition: Vector2?,
}

type LUIInstanceClassMap = {
    Window: Window,
    Frame: Frame,
    TextLabel: TextLabel,
    TextButton: TextButton,
    TextBox: TextBox,
    CodeEditor: CodeEditor,
    CheckBox: CheckBox,
    Slider: Slider,
    ProgressBar: ProgressBar,
    ImageLabel: ImageLabel,
    FrameSurface: FrameSurface,
    UIPadding: UIPadding,
    UIListLayout: UIListLayout,
    UIGridLayout: UIGridLayout,
    UISizeConstraint: UISizeConstraint,
    ScrollingFrame: ScrollingFrame,
}
declare Instance: { read new: <Name>(ClassName: Name & keyof<LUIInstanceClassMap>, Properties: any) -> index<LUIInstanceClassMap, Name> }
declare Vector2: { read new: (X: number, Y: number) -> Vector2 }
declare UDim: { read new: (Scale: number, Offset: number) -> UDim }
declare UDim2: {
    read new: (XScale: number, XOffset: number, YScale: number, YOffset: number) -> UDim2,
    read fromOffset: (X: number, Y: number) -> UDim2,
    read fromScale: (X: number, Y: number) -> UDim2,
}
declare app: App
declare task: {
    read defer: (Callback: () -> ()) -> (),
    read spawn: (Callback: () -> ()) -> (),
    read delay: (Seconds: number, Callback: () -> ()) -> (),
}
declare require: (Request: string) -> unknown
declare Color3: { read new: (R: number, G: number, B: number) -> Color3, read fromRGB: (R: number, G: number, B: number) -> Color3 }
