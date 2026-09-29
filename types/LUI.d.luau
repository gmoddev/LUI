-- Generated from LuiRuntime reflection metadata. Do not edit by hand.
export type Connection = { read Disconnect: (Self: Connection) -> () }
export type Signal = { read Connect: (Self: Signal, Callback: (...any) -> ()) -> Connection }
export type Vector2 = { read X: number, read Y: number }
export type UDim = { read Scale: number, read Offset: number }
export type UDim2 = { read X: UDim, read Y: UDim }

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
    Size: UDim2,
    Position: UDim2,
    AnchorPoint: Vector2,
    LayoutOrder: number,
    read IsFocused: boolean,
    read AbsolutePosition: Vector2,
    read AbsoluteSize: Vector2,
    read Focused: Signal,
    read FocusLost: Signal,
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

export type WindowService = {
    read GetWindows: (Self: WindowService) -> {Window},
}

export type PlatformService = {
    read BackendName: string,
    read Supports: (Self: PlatformService, Capability: string) -> boolean,
}

export type App = { read GetService: ((Self: App, Name: "WindowService") -> WindowService) & ((Self: App, Name: "PlatformService") -> PlatformService) }

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
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
}

export type TextLabelInit = {
    Name: string?,
    Parent: (Window | Frame)?,
    Visible: boolean?,
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
    Size: UDim2?,
    Position: UDim2?,
    AnchorPoint: Vector2?,
    LayoutOrder: number?,
    Minimum: number?,
    Maximum: number?,
    Value: number?,
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

declare Instance: { read new: (((((ClassName: "Window", Properties: any) -> Window) & ((ClassName: "Frame", Properties: any) -> Frame)) & (((ClassName: "TextLabel", Properties: any) -> TextLabel) & ((ClassName: "TextButton", Properties: any) -> TextButton))) & ((((ClassName: "TextBox", Properties: any) -> TextBox) & ((ClassName: "CheckBox", Properties: any) -> CheckBox)) & (((ClassName: "Slider", Properties: any) -> Slider) & ((ClassName: "ProgressBar" | "UIPadding" | "UIListLayout", Properties: any) -> (ProgressBar | UIPadding | UIListLayout))))) }
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
