# LUI API: implemented classes

Generated from the runtime reflection table. This lists the implemented surface, not the full planned specification.

## Global functions

- `require`: `(Request: string) -> unknown`
- `Color3`: `{ read new: (R: number, G: number, B: number) -> Color3, read fromRGB: (R: number, G: number, B: number) -> Color3 }`

## Instance

Base: `none`. Creatable: `false`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Name` | `string` | class name | No |
| `Sensitive` | `boolean` | false | No |
| `EffectiveSensitive` | `boolean` | derived sticky classification | Yes |
| `Parent` | `Instance?` | nil | No |
| `ClassName` | `string` | class name | Yes |

| Method | Type |
| --- | --- |
| `Destroy` | `(Self: Instance) -> ()` |
| `Clone` | `(Self: Instance) -> Instance` |
| `GetChildren` | `(Self: Instance) -> {Instance}` |
| `GetDescendants` | `(Self: Instance) -> {Instance}` |
| `FindFirstChild` | `(Self: Instance, Name: string) -> Instance?` |
| `IsA` | `(Self: Instance, ClassName: string) -> boolean` |

| Signal | Type |
| --- | --- |
| `Changed` | `Signal` |
| `Destroying` | `Signal` |

## Window

Base: `Instance`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Title` | `string` |  | No |
| `Visible` | `boolean` | false | No |
| `IsClosed` | `boolean` | false; terminal native lifetime | Yes |
| `Size` | `UDim2` | 800x600 | No |
| `AbsolutePosition` | `Vector2` | derived | Yes |
| `AbsoluteSize` | `Vector2` | derived | Yes |

| Method | Type |
| --- | --- |
| `RequestClose` | `(Self: Window) -> (boolean, string?)` |
| `PreventClose` | `(Self: Window) -> boolean` |

| Signal | Type |
| --- | --- |
| `Shortcut` | `ShortcutSignal` |
| `CloseRequested` | `Signal` |
| `Closed` | `Signal` |

## GuiObject

Base: `Instance`. Creatable: `false`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Visible` | `boolean` | true | No |
| `AccessibilityLabel` | `string` |  | No |
| `AccessibilityDescription` | `string` |  | No |
| `Size` | `UDim2` | 100% of parent | No |
| `Position` | `UDim2` | 0,0 | No |
| `AnchorPoint` | `Vector2` | 0,0 | No |
| `LayoutOrder` | `number` | 0 | No |
| `BackgroundColor3` | `Color3?` | nil (native/theme) | No |
| `BackgroundTransparency` | `number` | 0 | No |
| `BorderColor3` | `Color3?` | nil (native/theme) | No |
| `BorderSizePixel` | `number` | 0 | No |
| `CornerRadius` | `number` | 0 | No |
| `Opacity` | `number` | 1 | No |
| `TextColor3` | `Color3?` | nil (inherited/theme) | No |
| `TextSize` | `number?` | nil (inherit; default 14 logical units) | No |
| `FontFamily` | `string?` | nil (inherit; default System) | No |
| `FontWeight` | `string?` | nil (inherit; Normal or Bold) | No |
| `TextWrapped` | `boolean` | true | No |
| `TextXAlignment` | `string` | Left | No |
| `TextYAlignment` | `string` | Top | No |
| `ZIndex` | `number` | 0 | No |
| `ClipsDescendants` | `boolean` | false; ScrollingFrame always clips | No |
| `IsFocused` | `boolean` | false | Yes |
| `AbsolutePosition` | `Vector2` | derived | Yes |
| `AbsoluteSize` | `Vector2` | derived | Yes |

| Method | Type |
| --- | --- |
| `Focus` | `(Self: GuiObject) -> boolean` |

| Signal | Type |
| --- | --- |
| `Focused` | `Signal` |
| `FocusLost` | `Signal` |
| `MouseEnter` | `Signal` |
| `MouseLeave` | `Signal` |
| `InputBegan` | `InputSignal` |
| `InputChanged` | `InputSignal` |
| `InputEnded` | `InputSignal` |

## Frame

Base: `GuiObject`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |

## TextLabel

Base: `GuiObject`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Text` | `string` |  | No |

## GuiButton

Base: `GuiObject`. Creatable: `false`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |

## TextButton

Base: `GuiButton`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Text` | `string` |  | No |
| `Enabled` | `boolean` | true | No |

| Signal | Type |
| --- | --- |
| `Activated` | `Signal` |

## TextBox

Base: `GuiObject`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Text` | `string` |  | No |
| `Enabled` | `boolean` | true | No |
| `Multiline` | `boolean` | false | No |
| `Secret` | `boolean` | false; implies sticky sensitivity | No |
| `PlaceholderText` | `string` |  | No |
| `MaxLength` | `number` | 65534 Unicode scalars; UTF-8 byte ceiling also 65534 | No |
| `SelectionStart` | `number` | 0; Unicode scalar offset | No |
| `SelectionLength` | `number` | 0; Unicode scalar count | No |
| `IsComposing` | `boolean` | false | Yes |
| `CompositionText` | `string` |  | Yes |

| Signal | Type |
| --- | --- |
| `TextChanged` | `Signal` |
| `SelectionChanged` | `Signal` |
| `CompositionStarted` | `Signal` |
| `CompositionChanged` | `Signal` |
| `CompositionEnded` | `Signal` |

## CodeEditor

Base: `TextBox`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Multiline` | `boolean` | true; immutable | Yes |
| `Secret` | `boolean` | false; immutable | Yes |
| `MaxLength` | `number` | 262144 Unicode scalars; UTF-8 byte ceiling also 262144 | No |
| `TextRevision` | `number` | 1; advances when committed text changes | Yes |

| Method | Type |
| --- | --- |
| `SetStyles` | `(Self: CodeEditor, ExpectedRevision: number, Ranges: {{Start: number, End: number, Color: number}}) -> boolean` |

## CheckBox

Base: `GuiObject`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Text` | `string` |  | No |
| `Checked` | `boolean` | false | No |
| `Enabled` | `boolean` | true | No |

| Signal | Type |
| --- | --- |
| `Activated` | `Signal` |
| `CheckedChanged` | `Signal` |

## Slider

Base: `GuiObject`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Minimum` | `number` | 0 | No |
| `Maximum` | `number` | 100 | No |
| `Value` | `number` | 0 | No |
| `Enabled` | `boolean` | true | No |

| Signal | Type |
| --- | --- |
| `ValueChanged` | `Signal` |

## ProgressBar

Base: `GuiObject`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Minimum` | `number` | 0 | No |
| `Maximum` | `number` | 100 | No |
| `Value` | `number` | 0 | No |

## ImageLabel

Base: `GuiObject`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Source` | `string` |  | No |
| `LoadState` | `string` | Idle; Loading, Ready, Failed, Canceled or Unavailable | Yes |

| Signal | Type |
| --- | --- |
| `LoadChanged` | `Signal` |

## FrameSurface

Base: `GuiObject`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `ScaleMode` | `string` | Fit; Fill or Stretch | No |
| `FrameState` | `string` | Empty; Pending, Ready or Failed (Ready never implies visible presentation) | Yes |

| Method | Type |
| --- | --- |
| `SubmitFrame` | `(Self: FrameSurface, Pixels: buffer, Width: number, Height: number, RowStride: number, Format: string) -> (boolean, string?)` |
| `ClearFrame` | `(Self: FrameSurface) -> ()` |

## UIComponent

Base: `Instance`. Creatable: `false`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |

## UIPadding

Base: `UIComponent`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `PaddingTop` | `UDim` | 0 | No |
| `PaddingBottom` | `UDim` | 0 | No |
| `PaddingLeft` | `UDim` | 0 | No |
| `PaddingRight` | `UDim` | 0 | No |

## UIListLayout

Base: `UIComponent`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Padding` | `UDim` | 0 | No |
| `FillDirection` | `string` | Vertical | No |

## UIGridLayout

Base: `UIComponent`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `CellSize` | `UDim2` | 100x100 | No |
| `CellPadding` | `UDim2` | 0x0 | No |

## UISizeConstraint

Base: `UIComponent`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `MinSize` | `Vector2` | 0,0 | No |
| `MaxSize` | `Vector2?` | nil (unbounded) | No |

## ScrollingFrame

Base: `Frame`. Creatable: `true`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `CanvasSize` | `UDim2` | 0,0; effective canvas at least viewport | No |
| `CanvasPosition` | `Vector2` | 0,0; requested offset | No |
| `AbsoluteCanvasSize` | `Vector2` | derived | Yes |
| `AbsoluteCanvasPosition` | `Vector2` | derived clamped offset | Yes |

| Signal | Type |
| --- | --- |
| `CanvasChanged` | `Signal` |

## Services

- `StorageService`: Open, Read, Write, Remove
- `NotificationService`: Show
- `TrayService`: Create, Watch
- `PerformanceService`: GetStats, GetTime
- `WindowService`: GetWindows
- `PlatformService`: Supports
- `ThemeService`: ThemeChanged
- `ClipboardService`: WriteText, ReadText
- `DialogService`: OpenFile
- `AssetService`: Has
- `NetworkService`: ListenTcp, ConnectTcp, BindUdp
- `HttpService`: RequestAsync, GetAsync, CancelAll
- `HttpServerService`: CreateServer

## Networking value and resource types

### NativeHandle

- `Close`: `(Self: NativeHandle) -> boolean`

### NativeRequest

- `Cancel`: `(Self: NativeRequest) -> (boolean, string)`

### PerformanceStats

- `LiveInstances`: `number`
- `RetainedInstances`: `number`
- `Connections`: `number`
- `LuaMemoryBytes`: `number`
- `QueuedEvents`: `number`
- `QueuedBytes`: `number`
- `LayoutPasses`: `number`
- `PresentationCalls`: `number`
- `LayoutSamples`: `number`
- `LayoutP95Ms`: `number`
- `LastLayoutMs`: `number`
- `IsHeadless`: `boolean`
- `InputSamples`: `number`
- `InputP95Ms`: `number`
- `InputP99Ms`: `number`
- `RenderSamples`: `number`
- `RenderP95Ms`: `number`
- `RenderP99Ms`: `number`

### Color3

- `R`: `number`
- `G`: `number`
- `B`: `number`

### NetworkEndpoint

- `Address`: `string`
- `Port`: `number`

### NetworkListenOptions

- `Address`: `string?`
- `Family`: `("IPv4" | "IPv6" | "DualStack")?`
- `Port`: `number`

### NetworkConnectOptions

- `Address`: `string?`
- `Port`: `number`

### TcpListener

- `IsListening`: `boolean`
- `Port`: `number`
- `BoundEndpoints`: `{NetworkEndpoint}`
- `AcceptAsync`: `(Self: TcpListener) -> TcpConnection`
- `Close`: `(Self: TcpListener) -> ()`

### TcpConnection

- `IsOpen`: `boolean`
- `LocalEndpoint`: `NetworkEndpoint`
- `RemoteEndpoint`: `NetworkEndpoint`
- `ReadAsync`: `(Self: TcpConnection, MaxBytes: number?) -> buffer?`
- `ReadExactAsync`: `(Self: TcpConnection, Bytes: number) -> buffer`
- `WriteAsync`: `(Self: TcpConnection, Data: string | buffer) -> ()`
- `Shutdown`: `(Self: TcpConnection, Direction: "Read" | "Write" | "Both") -> ()`
- `Close`: `(Self: TcpConnection) -> ()`
- `Closed`: `Signal`

### UdpBindOptions

- `Address`: `string?`
- `Family`: `("IPv4" | "IPv6")?`
- `Port`: `number`
- `MaxDatagramBytes`: `number?`

### UdpSocket

- `IsOpen`: `boolean`
- `LocalEndpoint`: `NetworkEndpoint`
- `ReceiveFromAsync`: `(Self: UdpSocket) -> UdpDatagram`
- `SendToAsync`: `(Self: UdpSocket, Endpoint: NetworkEndpoint, Data: string | buffer) -> ()`
- `Close`: `(Self: UdpSocket) -> ()`

### UdpDatagram

- `Data`: `buffer`
- `RemoteEndpoint`: `NetworkEndpoint`

### HttpHeader

- `Name`: `string`
- `Value`: `string`

### HttpRequestOptions

- `Url`: `string`
- `Method`: `string?`
- `Headers`: `{HttpHeader}?`
- `Body`: `(string | buffer)?`
- `TimeoutMs`: `number?`
- `MaxResponseBytes`: `number?`

### HttpResponse

- `StatusCode`: `number`
- `StatusMessage`: `string`
- `Success`: `boolean`
- `Body`: `string`
- `Headers`: `{HttpHeader}`
- `Trailers`: `{HttpHeader}`

### HttpServerOptions

- `Address`: `string?`
- `Family`: `("IPv4" | "IPv6" | "DualStack")?`
- `Port`: `number`
- `TLS`: `boolean?`
- `TimeoutMs`: `number?`
- `MaxConnections`: `number?`
- `MaxRequestBytes`: `number?`
- `MaxResponseBytes`: `number?`

### HttpReplyOptions

- `StatusCode`: `number?`
- `StatusMessage`: `string?`
- `Headers`: `{HttpHeader}?`
- `Body`: `(string | buffer)?`

### HttpRequest

- `Method`: `string`
- `Path`: `string`
- `RawTarget`: `string`
- `HttpVersion`: `string`
- `Headers`: `{HttpHeader}`
- `Trailers`: `{HttpHeader}`
- `Body`: `string`
- `LocalEndpoint`: `NetworkEndpoint`
- `RemoteEndpoint`: `NetworkEndpoint`

### HttpServer

- `Port`: `number`
- `IsListening`: `boolean`
- `BoundEndpoints`: `{NetworkEndpoint}`
- `Route`: `(Self: HttpServer, Method: string, Path: string, Handler: (Request: HttpRequest) -> HttpReplyOptions) -> ()`
- `Start`: `(Self: HttpServer) -> ()`
- `Close`: `(Self: HttpServer) -> ()`
