# LUI API: implemented classes

Generated from the runtime reflection table. This lists the implemented surface, not the full planned specification.

## Instance

Base: `none`. Creatable: `false`.

| Property | Type | Default | Read only |
| --- | --- | --- | --- |
| `Name` | `string` | class name | No |
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
| `Size` | `UDim2` | 800x600 | No |
| `AbsolutePosition` | `Vector2` | derived | Yes |
| `AbsoluteSize` | `Vector2` | derived | Yes |

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
| `IsFocused` | `boolean` | false | Yes |
| `AbsolutePosition` | `Vector2` | derived | Yes |
| `AbsoluteSize` | `Vector2` | derived | Yes |

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

| Signal | Type |
| --- | --- |
| `TextChanged` | `Signal` |

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

## Services

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
