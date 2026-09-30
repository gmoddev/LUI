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
| `InputBegan` | `PointerInputSignal` |
| `InputChanged` | `PointerInputSignal` |
| `InputEnded` | `PointerInputSignal` |

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
