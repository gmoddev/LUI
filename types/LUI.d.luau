-- Generated from LuiRuntime reflection metadata. Do not edit by hand.
export type Connection = { Disconnect: (Self: Connection) -> () }
export type Signal = { Connect: (Self: Signal, Callback: (...any) -> ()) -> Connection }
export type Vector2 = { X: number, Y: number }
export type UDim = { Scale: number, Offset: number }
export type UDim2 = { X: UDim, Y: UDim }

export type Instance = {
    Name: string,
    Parent: Instance?,
    ClassName: string, -- read-only at runtime
    Destroy: (Self: Instance) -> (),
    Clone: (Self: Instance) -> Instance,
    GetChildren: (Self: Instance) -> {Instance},
    GetDescendants: (Self: Instance) -> {Instance},
    FindFirstChild: (Self: Instance, Name: string) -> Instance?,
    IsA: (Self: Instance, ClassName: string) -> boolean,
    Changed: Signal,
    Destroying: Signal,
}

export type Window = Instance & {
    Title: string,
    Visible: boolean,
    Size: UDim2,
    AbsolutePosition: Vector2, -- read-only at runtime
    AbsoluteSize: Vector2, -- read-only at runtime
}

export type GuiObject = Instance & {
    Visible: boolean,
    Size: UDim2,
    Position: UDim2,
    AnchorPoint: Vector2,
    LayoutOrder: number,
    IsFocused: boolean, -- read-only at runtime
    AbsolutePosition: Vector2, -- read-only at runtime
    AbsoluteSize: Vector2, -- read-only at runtime
    Focused: Signal,
    FocusLost: Signal,
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
    Activated: Signal,
}

export type TextBox = GuiObject & {
    Text: string,
    Enabled: boolean,
    TextChanged: Signal,
}

export type CheckBox = GuiObject & {
    Text: string,
    Checked: boolean,
    Enabled: boolean,
    Activated: Signal,
    CheckedChanged: Signal,
}

export type Slider = GuiObject & {
    Minimum: number,
    Maximum: number,
    Value: number,
    Enabled: boolean,
    ValueChanged: Signal,
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
    GetWindows: (Self: WindowService) -> {Window},
}

export type PlatformService = {
    BackendName: string, -- read-only at runtime
    Supports: (Self: PlatformService, Capability: string) -> boolean,
}

export type App = { GetService: (Self: App, Name: string) -> any }
