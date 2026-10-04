-- Generated from LuiRuntime reflection metadata. Do not edit by hand.
export type Connection = { read Disconnect: (Self: Connection) -> () }
export type Signal = { read Connect: (Self: Signal, Callback: (...any) -> ()) -> Connection }
export type Vector2 = { read X: number, read Y: number }
export type UDim = { read Scale: number, read Offset: number }
export type UDim2 = { read X: UDim, read Y: UDim }
export type PointerInput = { read Device: "Mouse" | "Pen" | "Touch" | "Touchpad", read PointerId: number, read Position: Vector2, read IsCanceled: boolean }
export type KeyboardInput = { read Device: "Keyboard", read Key: string, read IsRepeat: boolean }
export type InputEvent = PointerInput | KeyboardInput
export type InputSignal = { read Connect: (Self: InputSignal, Callback: (Input: InputEvent) -> ()) -> Connection }
export type ThemeSignal = { read Connect: (Self: ThemeSignal, Callback: (Theme: string) -> ()) -> Connection }

export type Instance = {
    Name: string,
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
    Size: UDim2,
    read AbsolutePosition: Vector2,
    read AbsoluteSize: Vector2,
}

export type GuiObject = Instance & {
    Visible: boolean,
    AccessibilityLabel: string,
    AccessibilityDescription: string,
    Size: UDim2,
    Position: UDim2,
    AnchorPoint: Vector2,
    LayoutOrder: number,
    read IsFocused: boolean,
    read AbsolutePosition: Vector2,
    read AbsoluteSize: Vector2,
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
    read TextChanged: Signal,
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
}

export type App = { read GetService: ((Self: App, Name: "WindowService") -> WindowService) & ((Self: App, Name: "PlatformService") -> PlatformService) & ((Self: App, Name: "ThemeService") -> ThemeService) & ((Self: App, Name: "ClipboardService") -> ClipboardService) & ((Self: App, Name: "DialogService") -> DialogService) & ((Self: App, Name: "AssetService") -> AssetService) & ((Self: App, Name: "NetworkService") -> NetworkService) }

export type WindowInit = {
    Name: string?,
    Title: string?,
    Visible: boolean?,
    Size: UDim2?,
}

export type FrameInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
}

export type TextLabelInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    Text: string?,
}

export type TextButtonInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    Text: string?,
    Enabled: boolean?,
}

export type TextBoxInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    Text: string?,
    Enabled: boolean?,
}

export type CheckBoxInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    Text: string?,
    Checked: boolean?,
    Enabled: boolean?,
}

export type SliderInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    Minimum: number?,
    Maximum: number?,
    Value: number?,
    Enabled: boolean?,
}

export type ProgressBarInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    Minimum: number?,
    Maximum: number?,
    Value: number?,
}

export type ImageLabelInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    Visible: boolean?,
    AccessibilityLabel: string?,
    AccessibilityDescription: string?,
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    Source: string?,
}

export type UIPaddingInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    PaddingTop: UDim?,
    PaddingBottom: UDim?,
    PaddingLeft: UDim?,
    PaddingRight: UDim?,
}

export type UIListLayoutInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    Padding: UDim?,
    FillDirection: string?,
}

export type UIGridLayoutInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    CellSize: UDim2?,
    CellPadding: UDim2?,
}

export type UISizeConstraintInit = {
    Name: string?,
    Parent: (Window | Frame | GuiObject)?,
    MinSize: Vector2?,
    MaxSize: Vector2?,
}

declare Instance: { read new: (((((ClassName: "Window", Properties: any) -> Window) & ((ClassName: "Frame", Properties: any) -> Frame)) & (((ClassName: "TextLabel", Properties: any) -> TextLabel) & ((ClassName: "TextButton", Properties: any) -> TextButton))) & ((((ClassName: "TextBox", Properties: any) -> TextBox) & ((ClassName: "CheckBox", Properties: any) -> CheckBox)) & (((ClassName: "Slider", Properties: any) -> Slider) & ((ClassName: "ProgressBar" | "ImageLabel" | "UIPadding" | "UIListLayout" | "UIGridLayout" | "UISizeConstraint", Properties: any) -> (ProgressBar | ImageLabel | UIPadding | UIListLayout | UIGridLayout | UISizeConstraint))))) }
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
