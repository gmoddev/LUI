# LUI — Luau UI Framework

## 1. Purpose

**LUI (Luau UI)** is a cross-platform native desktop application framework that gives application developers Roblox-like Luau semantics while hiding operating-system UI complexity.

LUI is intended to make native desktop applications feel closer to:

```lua
local window = Instance.new("Window")
window.Title = "My App"
window.Size = Vector2.new(800, 600)

local button = Instance.new("TextButton")
button.Text = "Launch"
button.Parent = window

button.Activated:Connect(function()
    Launcher:Launch()
end)
```

than to:

- Win32 message loops;
- HWND ownership;
- XAML;
- COM event plumbing;
- GTK signal registration;
- Wayland or X11;
- manual UI-thread dispatch;
- platform-specific layout systems.

The guiding principle is:

> **LUI exposes a small, consistent Luau API and absorbs platform, lifecycle, scheduling, accessibility, and native integration complexity internally.**

LUI is not intended to merely bind Win32 or GTK into Luau.

LUI owns the application-facing semantic model.

---

# 2. Goals

LUI should provide:

- a small Roblox-familiar Luau UI API;
- native desktop controls and platform behavior;
- Windows and Linux support from one application model;
- future macOS support without redesigning the public API;
- native-first and Luau-first application models;
- first-class C/C++ integration;
- a stable native extension ABI;
- safe asynchronous work;
- hot reload and rapid iteration;
- generated Luau typing;
- a VS Code live preview and inspector;
- native packaging into normal standalone applications;
- strong accessibility and input behavior by leveraging platform toolkits;
- deliberate escape hatches for applications that genuinely need OS-specific functionality.

The normal application developer should not need to understand the operating system's UI stack.

---

# 3. Non-goals

Initial LUI versions should **not** attempt to provide:

- a custom cross-platform GPU-rendered widget toolkit;
- pixel-identical controls across operating systems;
- direct Win32 bindings as the standard API;
- direct GTK bindings as the standard API;
- a React-like framework as the fundamental object model;
- arbitrary FFI from Luau;
- arbitrary native pointer manipulation;
- browser/Electron-based application rendering;
- state-preserving hot reload in the first implementation;
- a complete visual drag-and-drop designer in the first release;
- every platform-specific capability behind a fake portable abstraction.

The priority is:

```text
simple API
+
native behavior
+
strong architecture
```

rather than maximum feature count.

---

# 4. High-level architecture

```text
                       Application Luau
                              │
                              ▼
                  ┌──────────────────────┐
                  │      LUI Runtime     │
                  │                      │
                  │ Luau VM              │
                  │ scheduler            │
                  │ Instance model       │
                  │ signals              │
                  │ reflection           │
                  │ lifecycle            │
                  │ input/focus          │
                  │ layout               │
                  │ theme                │
                  └──────────┬───────────┘
                             │
                     Backend Contract
                             │
          ┌──────────────────┼──────────────────┐
          ▼                  ▼                  ▼
      Windows             Linux           Preview/Test
          │                  │                  │
       WinUI 3             GTK4          Preview backend
          │                  │
 Windows App SDK         GDK
          │                  │
     Win32 interop      Wayland / X11

                         Future
                           │
                           ▼
                         macOS
                           │
                     native Apple UI
```

The public API does not correspond directly to any backend.

LUI defines its own semantics and translates them to each platform.

---

# 5. Core architectural rule

The most important contract in LUI is:

> **Normal LUI application code never depends on WinUI, Win32, GTK, Wayland, X11, AppKit, HWNDs, COM objects, native pointers, or other backend implementation details.**

Platform-specific access may exist through explicit interop modules.

Using such an API is an intentional portability boundary.

Example:

```lua
local Windows = require("@lui/platform/windows")

local handle = Windows:GetNativeWindowHandle(window)
```

That code is explicitly Windows-specific.

The existence of platform escape hatches must never influence the design of normal portable APIs.

---

# 6. Core object model

LUI uses an Instance hierarchy similar to Roblox.

## 6.1 Base Instance

```text
Instance
├─ Name
├─ Parent
├─ ClassName [readonly]
├─ Changed
├─ Destroying
│
├─ Destroy()
├─ Clone()
├─ GetChildren()
├─ GetDescendants()
├─ FindFirstChild()
└─ IsA()
```

Parenting defines ownership and hierarchy.

Destroying a parent destroys owned descendant UI objects unless explicitly documented otherwise.

Native objects are implementation details corresponding to LUI Instances rather than the authoritative application object model.

---

# 7. Initial class model

A practical first class set is:

```text
Instance
├─ Window
├─ GuiObject
│  ├─ Frame
│  ├─ TextLabel
│  ├─ GuiButton
│  │  ├─ TextButton
│  │  └─ ImageButton
│  ├─ TextBox
│  ├─ ImageLabel
│  ├─ CheckBox
│  ├─ Slider
│  ├─ ProgressBar
│  └─ ScrollingFrame
│
└─ UIComponent
   ├─ UIListLayout
   ├─ UIGridLayout
   ├─ UIPadding
   ├─ UISizeConstraint
   ├─ UIAspectRatioConstraint
   ├─ UICorner
   └─ UIStroke
```

Classes should be added only when their semantics can be defined independently of a single backend.

---

# 8. Core datatypes

LUI should include a small standard set of immutable value types:

```text
Vector2
Vector3 if later required
Color3
UDim
UDim2
Rect
Font
```

Roblox-compatible semantics should be retained where they are already appropriate.

---

# 9. Layout

## 9.1 UDim and UDim2

LUI should preserve the scale-plus-offset model:

```lua
panel.Size = UDim2.new(
    0.5, -8,
    1.0, -16
)
```

Meaning:

```text
width  = parent width  * 0.5 - 8
height = parent height * 1.0 - 16
```

Offsets are expressed in logical device-independent units.

Applications should not reason about raw physical pixels for ordinary layout.

---

## 9.2 Layout ownership

LUI owns high-level layout semantics.

Backends do not independently reinterpret `UIListLayout`, `UDim2`, padding, constraints, or anchoring.

The pipeline should resemble:

```text
Instance tree
    ↓
style resolution
    ↓
measurement
    ↓
LUI layout
    ↓
arrangement
    ↓
backend geometry
```

Backends may provide intrinsic/native control measurements.

For example:

```text
How tall does this native TextBox naturally want to be?

How wide is this native text under the current font?

What minimum size does this platform control require?
```

The final layout model remains defined by LUI.

This guarantees substantially more consistent behavior between:

- Windows;
- Linux;
- preview;
- headless tests.

---

## 9.3 Size constraints

`UISizeConstraint` is a nonvisual child of a `Window` or GUI object. At most one may be attached to each visual object. `MinSize` is a nonnegative logical `Vector2` and defaults to `(0, 0)`. `MaxSize` is an optional nonnegative logical `Vector2`; `nil` means no upper bound. A specified maximum must be at least the minimum on both axes. Invalid assignments leave the prior values intact.

LUI resolves `Size` against the parent content box, then clamps each axis before applying `AnchorPoint` or advancing a list layout's cursor. Constraints may make a child larger than its parent content box. The resulting `AbsoluteSize` and backend arrangement use the same clamped geometry. A window is clamped before its children are laid out. When a native window is resized, its current content size temporarily determines layout and `AbsoluteSize`; its requested `Size` remains unchanged. A later script `Size` assignment or window size-constraint change requests a new native size. Foundation 1 does not yet enforce window size constraints against manual native resizing.

---

## 9.4 Grid layout

`UIGridLayout` is a nonvisual child of a `Window` or `Frame`. A container may have one `UIPadding` and either one `UIListLayout` or one `UIGridLayout`. Grid items are sorted by `LayoutOrder`, preserving insertion order for ties, and placed left to right in rows. A grid overrides each item's `Size`, `Position`, and `AnchorPoint` for arrangement; the stored properties are unchanged.

`CellSize` defaults to `UDim2.fromOffset(100, 100)` and resolves against the padded content box. Negative resolved dimensions become zero. `CellPadding` defaults to zero and requires nonnegative scale and offset components on both axes. The grid starts with the resolved cell size, applies each item's `UISizeConstraint`, and uses the largest resulting width and height as a uniform slot. Each item retains its own constrained size at the slot's top-left corner. The number of columns is the largest that fit in the padded content width using the uniform slot width and horizontal gap, with at least one column. Vertical spacing uses the uniform slot height and vertical gap. Items may overflow a container that is narrower than one slot.

---

# 10. Signals

Events use Roblox-like Signals:

```lua
button.Activated:Connect(function()
    print("clicked")
end)
```

Connections expose:

```lua
connection:Disconnect()
```

Signals are LUI objects rather than direct backend event handles.

For example:

```text
WinUI Button.Click
        ↓
backend translation
        ↓
LUI Activate event
        ↓
TextButton.Activated
```

and:

```text
GtkButton::clicked
        ↓
backend translation
        ↓
LUI Activate event
        ↓
TextButton.Activated
```

The Luau application therefore sees identical semantics.

---

# 11. Application services

Operating-system functionality that does not logically belong to an Instance should be exposed through services.

Example:

```lua
local DialogService = app:GetService("DialogService")
local ClipboardService = app:GetService("ClipboardService")
local ThemeService = app:GetService("ThemeService")
```

Initial candidates:

```text
ApplicationService
WindowService
DialogService
ClipboardService
InputService
ThemeService
PlatformService
AssetService
```

Later:

```text
NotificationService
TrayService
ShellService
DragDropService
AccessibilityService
```

The root environment should use `app` rather than `game`.

Foundation 2 implements `WindowService`, `PlatformService`, `ThemeService`, `AssetService`, and capability-gated `ClipboardService` and `DialogService`. Clipboard reads and file picking currently use callbacks so the WinUI thread stays responsive:

```lua
app:GetService("ClipboardService"):ReadText(function(text, error)
    -- text is nil when the clipboard has no text
end)

app:GetService("DialogService"):OpenFile(function(path, error)
    -- path is nil when the picker is cancelled
end)
```

The synchronous file-picker form in the later example is a design illustration, not an implemented Foundation 2 call signature.

Example:

```lua
local ThemeService = app:GetService("ThemeService")
```

---

# 12. Object creation

Basic Roblox-like syntax remains valid:

```lua
local button = Instance.new("TextButton")
button.Text = "Save"
button.Parent = root
```

LUI should additionally provide property initialization:

```lua
local button = Instance.new("TextButton", {
    Name = "SaveButton",
    Text = "Save",
    Size = UDim2.new(1, 0, 0, 36),
    LayoutOrder = 3,
    Parent = root,
})
```

`Parent` should be applied after all other initialization properties.

This allows concise application construction without introducing a separate component system.

---

# 13. Optional declarative layer

A declarative helper may be shipped as an ordinary LUI library:

```lua
local UI = require("@lui/ui")

local window = UI.create("Window", {
    Title = "Settings",
}, {
    UI.create("TextLabel", {
        Text = "General",
    }),

    UI.create("TextButton", {
        Text = "Save",
        Activated = saveSettings,
    }),
})
```

This library must compile down to ordinary LUI Instances.

It must **not** become the authoritative semantic model.

The canonical abstraction remains:

```text
Instances
+
properties
+
signals
+
parenting
```

---

# 14. Scheduling and execution model

LUI should expose familiar scheduling primitives:

```lua
task.spawn()
task.defer()
task.delay()
task.wait()
```

The VM and UI object graph should have a single authoritative execution thread.

Conceptually:

```text
UI thread
├─ Luau VM
├─ signal callbacks
├─ layout
├─ UI object mutation
└─ native backend interaction
```

Background threads may perform:

- HTTP;
- filesystem operations;
- database activity;
- decompression;
- native computation;
- process management.

They may **never directly enter the Luau VM**.

Completion must be marshaled to the LUI scheduler.

```text
worker
   ↓
completion queue
   ↓
LUI scheduler
   ↓
resume Luau coroutine
```

This is both a correctness and security invariant.

---

# 15. Property batching

Property updates should not synchronously trigger backend work individually.

Example:

```lua
button.Text = "Loading..."
button.Enabled = false
panel.Visible = true
label.Text = ""
```

Internally:

```text
Luau callback
   ↓
property mutations
   ↓
dirty flags
   ↓
callback completes
   ↓
single style/layout/update transaction
   ↓
backend
```

This avoids unnecessary layout churn and prevents backend behavior from leaking into application semantics.

---

# 16. Windows backend

The primary Windows backend should use:

```text
LUI
 ↓
WinUI 3
 ↓
Windows App SDK
 ↓
Win32 where necessary
```

WinUI is the implementation backend rather than the application model.

LUI may use Win32 where required for:

- HWND integration;
- advanced window management;
- tray support;
- shell integration;
- APIs not exposed through WinUI;
- existing native-host interoperability.

LUI applications should not need to know when this occurs.

---

# 17. Optional lightweight Win32 backend

A direct Win32 backend may eventually be useful for:

- tiny utility applications;
- minimal deployment size;
- compatibility scenarios;
- existing HWND hosts;
- environments where WinUI deployment is undesirable.

The API should remain:

```lua
Instance.new("TextButton")
```

rather than:

```lua
Win32.CreateWindowEx(...)
```

Potential Windows targets would therefore become:

```text
winui3
win32
```

without changing application semantics.

The WinUI backend should be implemented first.

---

# 18. Linux backend

Linux should initially use:

```text
LUI
 ↓
GTK4
 ↓
GDK
 ↓
Wayland / X11
```

LUI should not implement separate Wayland and X11 application UI systems.

GTK handles platform display-system integration.

Example mappings may include:

```text
Window       → GtkApplicationWindow
TextButton   → GtkButton
TextBox      → GtkEntry
TextLabel    → GtkLabel
CheckBox     → GtkCheckButton
Slider       → GtkScale
```

These mappings remain backend implementation details.

---

# 19. macOS

macOS should be considered in the backend contract from the beginning but not implemented until Windows and Linux semantics have stabilized.

Possible native implementation choices can be evaluated later.

No initial public API should assume that:

- WinUI exists;
- GTK exists;
- windows have HWND-like handles;
- menus behave like Windows menus;
- every platform has identical windowing concepts.

---

# 20. Styling and theming

LUI should be **native-first**, not pixel-identical.

The same application may therefore naturally look different on:

- Windows;
- GNOME;
- KDE through GTK theming;
- future macOS.

Default properties should resolve to platform-native appearance where possible.

Applications may progressively override styling.

Example:

```lua
button.BackgroundColor3 = Color3.fromRGB(25, 25, 25)
button.TextColor3 = Color3.new(1, 1, 1)
```

Unspecified properties remain platform-native.

---

# 21. Theme service

Theme should not be represented as a scattered collection of platform-specific checks.

Example:

```lua
local Theme = app:GetService("ThemeService")

print(Theme.CurrentTheme)

Theme.ThemeChanged:Connect(function(theme)
    updateTheme(theme)
end)
```

Potential common semantics:

```text
System
Light
Dark
HighContrast
```

Applications may define custom themes above this layer.

The implemented `CurrentTheme` values are `Light`, `Dark`, and `HighContrast`. `System` is a possible future theme selection mode, not a current value. `ThemeChanged` is delivered by the scheduler pump.

---

# 22. Platform-specific visual capabilities

LUI must not pretend every backend supports every platform effect.

Example:

```lua
window.Backdrop = Enum.WindowBackdrop.Mica
```

may only work on Windows.

Applications may check:

```lua
local Platform = app:GetService("PlatformService")

if Platform:Supports("WindowBackdrop.Mica") then
    window.Backdrop = Enum.WindowBackdrop.Mica
end
```

Unsupported features must:

1. have an explicitly documented fallback;
2. emit a development diagnostic where useful;
3. never silently provide misleading semantics.

Example:

```text
LUI2017:
WindowBackdrop.Mica is unsupported by backend gtk4.
Falling back to WindowBackdrop.Auto.
```

---

# 23. Accessibility

Accessibility is part of the core contract, not a later optional feature.

LUI should rely on native controls wherever practical so that standard accessibility behavior is inherited from the platform.

Custom LUI controls must expose meaningful semantic information to the backend.

Foundation 1 implements `AccessibilityLabel` and `AccessibilityDescription` as strings on `GuiObject`. Both default to an empty string. A nonempty label supplies the native accessible name; a description supplies additional help text. Empty values let native controls use their standard platform semantics. The WinUI backend maps these properties to UI Automation name and help text:

```lua
button.AccessibilityLabel = "Delete account"
button.AccessibilityDescription =
    "Permanently deletes the current account"
```

Accessibility semantics should describe intent rather than platform APIs.

---

# 24. Input and focus

LUI owns cross-platform input semantics.

Potential events:

```text
InputBegan
InputChanged
InputEnded
MouseEnter
MouseLeave
Activated
Focused
FocusLost
```

The backend converts platform events into canonical LUI events.

In the current Foundation 1 implementation, `InputBegan`, `InputChanged`, and `InputEnded` carry a read-only `InputEvent` union. Pointer events provide a portable device name (`Mouse`, `Pen`, `Touch`, or `Touchpad`), a pointer ID, a position in the target object's logical coordinates, and `IsCanceled`. Movement may occur without a press. The runtime pairs accepted presses with one end event; cancellation, hiding, disabling, or reparenting an active target under a hidden ancestor ends its presses with `IsCanceled = true`.

Keyboard events have `Device = "Keyboard"`, a portable `Key`, and `IsRepeat`. First press, repeat, and paired release map to `InputBegan`, `InputChanged`, and `InputEnded`. Only the focused eligible object accepts key beginnings and repeats. Focus loss ends active keys before a new object's focus gain. The initial portable key set covers letters, digits, F1–F12, navigation keys, editing keys, and common modifiers; see [keyboard input decision](decisions/0014-keyboard-input.md). Native controls continue to own text composition and system shortcuts; `TextBox.TextChanged` reports edited text.

Focus navigation must not depend on backend-specific application code.

The Foundation 1 runtime permits one focused `GuiObject` at a time. A new accepted focus clears the previous object's `IsFocused` state and fires `FocusLost`, then ends active keys, before firing `Focused` on the new object. Hiding, disabling, or moving a focused object under a hidden ancestor clears focus, hover, and active input in that order. Repeated native focus notifications do not repeat signals. Destroying an object ends its focus without an additional `FocusLost` callback.

Keyboard, pointer, touch, and accessibility activation should converge on common semantic events such as `Activated` whenever appropriate.

---

# 25. Native interoperability

LUI should support native code in two distinct ways:

1. **host application bindings**;
2. **native extensions**.

These should share infrastructure but have different ergonomics.

---

# 26. Host applications

Native applications should be able to use LUI purely as their UI layer.

Example C++:

```cpp
lui::Application app;

app.service("Launcher")
    .function("Launch", &Launch)
    .function("Stop", &Stop)
    .function("IsRunning", &IsRunning);

app.run("ui/main.luau");
```

Luau:

```lua
local Launcher = app:GetService("Launcher")

button.Activated:Connect(function()
    Launcher:Launch()
end)
```

This should require very little registration boilerplate.

---

# 27. Native type conversion

The native SDK should automatically convert ordinary values.

Example:

```text
C++                         Luau

bool                        boolean
int                         number
float                       number
double                      number
std::string                 string
std::optional<T>            T?
std::vector<T>              {T}
enum                        Enum-like value
registered object           userdata/object proxy
```

Unsupported conversion should fail explicitly during registration or invocation.

It should never silently reinterpret memory.

---

# 28. Native objects

Host applications should be able to expose:

- functions;
- methods;
- properties;
- objects;
- enums;
- signals;
- async operations.

Example:

```cpp
app.service("Launcher")
    .signal<Process>("ProcessStarted")
    .function("Launch", &Launch);
```

Then:

```lua
Launcher.ProcessStarted:Connect(function(process)
    status.Text = process.Name .. " started"
end)
```

Signals emitted from worker threads must be marshaled onto the LUI scheduler before reaching Luau.

---

# 29. Native extension ABI

Extensions should use a stable C ABI.

Conceptually:

```c
typedef struct lui_host_api_v1 lui_host_api_v1;

typedef struct
{
    uint32_t struct_size;
    uint32_t abi_version;
    const char* name;
    uint32_t extension_version;
} lui_extension_info_v1;

LUI_EXPORT int
lui_extension_init(
    const lui_host_api_v1* host,
    lui_extension_info_v1* extension
);
```

This enables extensions written in:

- C;
- C++;
- Rust;
- Zig;
- other languages capable of implementing a C ABI.

Foundation 2's initial implementation uses [LuiExtension.h](../native/abi/LuiExtension.h) with version 1 query, init, and shutdown exports. The query reports required capabilities before initialization. A host must explicitly grant `NativeExtensions` before loading a DLL; normal Luau code cannot grant it. The current implementation registers primitive service methods and reflects them per runtime. Object handles and additional registration kinds remain design targets.

The version 1 host table now also appends service-signal registration and owner-thread emission entries. Old binaries can use the original table prefix; extensions that use appended entries check `StructSize`. Signals and methods appear in runtime-specific reflection. Version 1 is supported for its documented primitive method and signal subset; object handles, properties, enums, and datatypes require a later versioned design.

---

# 30. Extension registration

The ABI should expose capabilities similar to:

```text
register_service
register_class
register_property
register_method
register_signal
register_enum
register_datatype

create_object
retain_object
release_object

schedule_ui
schedule_worker

raise_error
log
```

The C ABI should be versioned explicitly.

Internal C++ types must never cross the stable ABI boundary.

The version 1 implemented host table offers method and signal registration, signal emission, structured logging, error reporting, and bounded owner-thread completion scheduling. The broader registration list above is future work. Failed initialization rolls back registered methods and signals and unloads the library; compatible extensions load before the first application script.

---

# 31. Extension tiers

Two native integration levels should exist.

## Tier 1 — Stable LUI ABI

Preferred.

Extensions interact with:

```text
lui_value
lui_object_handle
lui_call_context
```

They do not receive `lua_State*`.

This is the compatibility-oriented extension model.

## Tier 2 — Raw Luau integration

Advanced and explicitly unstable.

An extension may request direct access to:

```cpp
lua_State*
```

but becomes tied to:

- the pinned Luau version;
- LUI VM invariants;
- LUI scheduler rules;
- exact runtime compatibility requirements.

Raw Luau integration must never be necessary for ordinary native extensions.

---

# 32. Arbitrary FFI

LUI should not expose:

```lua
ffi.load(...)
ffi.cast(...)
```

or arbitrary native memory access to normal Luau applications.

Native capabilities are provided through:

- host bindings;
- declared extensions;
- explicit platform interoperability APIs.

This prevents the runtime abstraction from degenerating into arbitrary pointer manipulation.

---

# 33. Platform escape hatches

Explicit native interoperability should exist.

For example:

```lua
local Windows = require("@lui/platform/windows")

local handle = Windows:GetNativeWindowHandle(window)
```

The returned value should normally be an opaque typed handle:

```text
NativeWindowHandle
```

rather than an integer pointer.

The user must deliberately cross the portability boundary.

Foundation 2 provides explicit, capability-gated clipboard and file-picking services rather than a raw native window handle. The handle example above remains a design target for a later typed object ABI with lifetime rules; it is not an implemented Luau API.

---

# 34. Reflection

LUI should have one authoritative reflection database.

For every class it defines:

```text
Class
Base class
Properties
Methods
Signals
Enums
Documentation
Capabilities
Serialization rules
Editor metadata
```

Example:

```text
Class:
    TextButton

Base:
    GuiButton

Properties:
    Text: string
    TextColor3: Color3
    TextSize: number
    Enabled: boolean

Signals:
    Activated()
```

Reflection should drive:

- runtime validation;
- generated Luau declarations;
- autocomplete;
- documentation;
- VS Code property inspection;
- serialization;
- backend conformance tests;
- API documentation generation.

This metadata must not be independently reimplemented across these systems.

---

# 35. Generated Luau typing

LUI should generate an environment definition from reflection.

Example:

```lua
declare class TextButton extends GuiButton
    Text: string
    TextColor3: Color3
    TextSize: number
    Enabled: boolean

    Activated: Signal<>
end
```

This definition is consumed by Luau tooling/LSP support.

The editor API must therefore derive from the same schema as the runtime API.

Runtime and editor APIs are not allowed to drift independently.

---

# 36. Development CLI

Initial CLI:

```text
lui new
lui run
lui check
lui preview
lui build
```

Potential later commands:

```text
lui test
lui doctor
lui pack
lui docs
```

The Foundation 3 CLI implements `new`, `check`, `run`, `build`, and `preview`. `check` validates the version 1 manifest and compiles the entry script without executing it; type diagnostics are not yet included. `run` launches the existing WinUI host on Windows. `build` stages a self-contained folder from an already published Windows host. `preview` launches a separate headless host with a version 1 JSON-lines protocol and the actual runtime tree and resolved layout.

---

# 37. Project manifest

Example:

```toml
[app]
name = "Example"
entry = "src/main.luau"

[target.windows]
backend = "winui3"

[target.linux]
backend = "gtk4"
```

Native extensions may additionally be declared:

```toml
[extensions]
terminal = "extensions/terminal"
database = "extensions/database"
```

The current WinUI host uses a version 1 JSON manifest while the project-wide TOML format above remains a design target. It declares a script, capability names, DLL filenames, packaged asset filenames, and optional sandbox VM limits. The host validates local paths before loading; ordinary Luau code cannot alter the grant mask.

The first CLI uses this same version 1 JSON manifest, conventionally named `lui.json`, as its project manifest. The project and host therefore share path and capability validation. A later TOML format needs an explicit schema version and migration rather than an implicit reinterpretation of version 1 JSON.

---

# 38. Application packaging

Development:

```text
lui run
```

Release:

```text
lui build --release
```

Conceptually:

```text
Luau source
    ↓
parse/typecheck
    ↓
Luau bytecode
    ↓
optimize
    ↓
resolve assets/extensions
    ↓
application package
    ↓
embed/bundle into native host
    ↓
MyApp.exe / myapp
```

The resulting application should not require users to install Luau or LUI separately.

Bytecode is an implementation detail and must not become part of the stable application API.

Future AOT/native compilation could be introduced without changing normal LUI source.

Foundation 2 can publish a self-contained Windows folder with the WinUI host and only the manifest's declared application script, native DLLs, and assets. It does not yet produce a signed installer or use precompiled Luau bytecode.

---

# 39. VS Code integration

The implemented initial type diagnostic tool statically checks the saved manifest entry script in a separate pinned Luau analyzer against reflection-generated LUI definitions. `lui check` defaults to types; `--mode syntax` explicitly requests compiler-only checking. The version 1 result carries manifest-relative source identity, syntax/type messages, bounded truncation, and zero-based UTF-16 ranges. The editor requires workspace trust, checks on open/save or command, clears outdated errors on edit, cancels the owned checker process tree, and keeps type Problems separate from preview runtime errors. Application code is never executed by static checking. Module graphs, unsaved-buffer analysis, dynamic extension types, and language-server features remain planned. See [decision 0026](decisions/0026-isolated-luau-type-diagnostics.md) for protocol and limits.

The LUI extension should provide:

- Luau diagnostics;
- LUI type information;
- autocomplete;
- API documentation;
- live preview;
- Instance Explorer;
- Properties panel;
- runtime console;
- source navigation;
- native preview launcher.

Luau language tooling should be reused rather than replaced where practical.

The implemented VS Code slice provides the Instance Explorer, a resolved-bounds layout map, read-only reflection-backed properties, activation for supported buttons, viewport resizing, a runtime Output channel, Luau creation/change locations, source navigation, source-linked runtime diagnostics, and reload on entry-script save. On Windows it can explicitly launch the existing WinUI host in a separate process for exact native rendering. That native process is independent of the isolated Explorer preview and does not automatically reload on save. Type diagnostics and richer input remain planned.

---

# 40. Preview architecture

Application Luau must not execute inside the VS Code extension host.

Instead:

```text
VS Code
   │
   │ IPC
   ▼
lui-preview-host
   │
   ├─ Luau VM
   ├─ LUI runtime
   ├─ Instance tree
   ├─ scheduler
   ├─ layout
   └─ preview backend
```

The editor receives a representation of the LUI object tree and its resolved layout.

The preview is therefore based on actual LUI semantics rather than a separately implemented approximation.

---

# 41. Preview protocol

Runtime → editor messages may include:

```text
hello
fullTree
instanceCreated
instanceDestroyed
propertyChanged
layoutChanged
selectionChanged
focusChanged
diagnostic
consoleMessage
runtimeError
sourceLocation
```

Editor → runtime:

```text
reload
selectInstance
activate
pointerMove
pointerDown
pointerUp
keyDown
keyUp
resizeViewport
setTheme
```

The protocol should be explicitly versioned. The implemented initial subset uses one UTF-8 JSON object per line, with `version: 1`, `type`, and `generation`. The host emits `hello` with the runtime reflection schema, `fullTree`, `diagnostic`, `consoleMessage`, and `runtimeError`. It accepts `reload`, `snapshot`, `activate`, `resizeViewport`, and `shutdown`; other message names above remain planned. A full tree contains live Instance IDs, parent IDs, class and display properties, state, logical-unit bounds, and optional creation/last-change Luau source locations. Source-linked errors include an optional `location` with script and one-based line. Reload destroys the previous Luau VM and tree before incrementing the generation and rerunning the entry script. Commands against older generations are rejected. Protocol input and tree structure are bounded and validated on both sides. The initial preview requires a manifest without privileged capabilities or native extensions and is not a native visual renderer.

---

# 42. Source provenance

Development-created Instances should retain source provenance.

Conceptually:

```text
TextButton #481

created at:
    src/settings.luau:74

last mutated at:
    src/settings.luau:91
```

This enables:

- click control → jump to code;
- runtime diagnostics pointing to creation sites;
- property history during debugging;
- useful hot-reload diagnostics.

Source provenance is development metadata. The preview host enables it before running a script; other hosts leave it disabled by default. Luau frame information provides one-based lines but not columns. Initial Instance property tables point to their `Instance.new` call site when no closer Luau frame is available. Host-only/native changes have no new Luau location. The editor only opens a location when its source resolves to the manifest's validated entry script.

---

# 43. Designer

The first designer is an inspector and preview system, not a complete visual authoring system.

Initial view:

```text
┌────────────────────────────────────────────┐
│ Explorer        Preview       Properties  │
│                                            │
│ MainWindow      ┌─────────┐    TextButton │
│ └─ Root         │         │    Text: Save │
│    ├─ Title     │  Save   │    Visible: ✓ │
│    └─ Save      │         │    Size: ...  │
│                 └─────────┘                │
└────────────────────────────────────────────┘
```

Later visual editing may support:

- resize;
- positioning;
- property editing;
- hierarchy manipulation;
- layout insertion;
- asset assignment.

Direct manipulation must emit or modify deterministic application representation rather than maintaining hidden designer-only state.

---

# 44. Native preview

The editor preview is optimized for iteration.

A separate command should launch the actual platform backend:

```text
LUI: Open Native Preview
```

Native preview is authoritative for:

- exact native controls;
- font rendering;
- window decorations;
- IME;
- accessibility;
- platform input;
- platform-specific effects.

The webview preview is authoritative for LUI layout semantics, not exact OS rendering.

---

# 45. Hot reload

Initial hot reload may simply:

```text
source changed
    ↓
terminate preview generation
    ↓
create new generation
    ↓
re-run entrypoint
    ↓
replace preview tree
```

Later versions may preserve state through explicit mechanisms.

State-preserving object reconciliation must not be required for the initial architecture.

---

# 46. Security model

LUI should distinguish between:

## Trusted desktop applications

May receive capabilities for:

- filesystem;
- networking;
- clipboard;
- native extensions;
- process launching;
- shell integration.

## Sandboxed modules/applications

May be restricted through:

- capability-based services;
- memory limits;
- execution limits;
- restricted filesystem roots;
- restricted networking;
- native extension denial;
- shell/process denial.

Capabilities must be explicit.

Sandbox guarantees must never depend solely on Luau code convention.

The current sandbox enforces a Luau VM heap limit and per-dispatch interrupt budget from host configuration. It denies native extension loading and gates host bindings on a `HostServices` grant. Clipboard and file-picking services have separate grants. There is no ordinary Luau filesystem, network, process, or shell service yet; any such future service must define its own grant and boundary checks. The VM heap limit is not a process-wide memory ceiling, and a trusted native host is outside the sandbox boundary.

---

# 47. Error model

Programmer errors should fail loudly and deterministically.

Examples:

```text
Property 'Texxt' does not exist on TextButton.
Did you mean 'Text'?
```

```text
Frame cannot be parented to TextLabel.
```

```text
WindowBackdrop.Mica is unsupported on backend gtk4.
```

Native errors must be translated into LUI errors before reaching Luau.

Platform-specific error codes may be available as diagnostic metadata but should not normally become the public semantic contract.

---

# 48. Backend contract

Conceptually:

```cpp
class IUIBackend
{
public:
    virtual NativeObject create(
        const InstanceDescriptor&
    ) = 0;

    virtual void destroy(
        NativeObject
    ) = 0;

    virtual void setProperty(
        NativeObject,
        PropertyId,
        const Value&
    ) = 0;

    virtual MeasureResult measure(
        NativeObject,
        MeasureConstraints
    ) = 0;

    virtual void arrange(
        NativeObject,
        Rect
    ) = 0;

    virtual void setParent(
        NativeObject,
        NativeObject parent
    ) = 0;
};
```

Production APIs do not need to resemble this exact interface.

The critical separation is:

```text
LUI semantics
      ↓
backend-neutral state/change model
      ↓
platform adapter
```

---

# 49. Backend qualification

Every backend must pass a shared semantic conformance suite.

Tests should cover:

- parenting;
- destruction;
- signal ordering;
- property defaults;
- property validation;
- size calculations;
- layout;
- visibility;
- focus;
- activation;
- disabled state;
- lifecycle;
- scheduler integration;
- accessibility mappings where testable.

Platform-specific extensions have separate qualification.

Adding GTK should not require changing the meaning of existing LUI classes.

---

# 50. Canonical invariants

The following invariants are architectural requirements rather than implementation suggestions.

---

## 50.1 Public API invariants

1. **LUI owns public semantics.**  
   Public behavior is defined by LUI rather than inherited accidentally from WinUI, GTK, Win32, or another backend.

2. **Simple operations remain simple.**  
   Creating a window, label, button, text box, or layout must not require lifecycle, thread, renderer, or platform boilerplate.

3. **Platform details stay below the normal API.**  
   Ordinary application code must never require native handles or toolkit objects.

4. **Roblox familiarity is intentional.**  
   Where Roblox semantics translate cleanly to desktop UI, LUI should prefer familiar names and behavior rather than inventing unnecessary concepts.

5. **No framework is required above Instances.**  
   Declarative/component systems are libraries, not mandatory runtime semantics.

6. **Portable code is platform-neutral by default.**  
   Platform-specific behavior requires deliberate use of platform APIs.

7. **Unsupported behavior is explicit.**  
   Backend limitations must not be silently misrepresented as supported semantics.

---

## 50.2 Backend invariants

1. A backend implements LUI semantics; it does not redefine them.

2. Backend-specific UI objects remain private implementation details.

3. Native events are normalized before reaching Luau.

4. Backend object lifetime follows LUI Instance lifetime.

5. Platform-specific features must be capability-queryable.

6. A new backend must not require changes to ordinary application code.

7. Backends must pass the shared conformance suite.

8. LUI layout semantics cannot depend on whichever layout algorithm the native toolkit happens to use.

9. Backend operation failures must reach the runtime. A failed creation is rolled back; other failures stop dispatch before the runtime presents divergent state as healthy.

---

## 50.3 Layout invariants

1. All ordinary layout uses logical device-independent units.

2. `UDim` and `UDim2` have identical semantics across backends.

3. LUI is authoritative for parent-relative layout.

4. Backend measurement may provide intrinsic size data but may not redefine LUI layout rules.

5. Property mutation during one Luau dispatch is batched before expensive backend/layout work wherever practical.

6. Absolute geometry properties are read-only derived values.

7. Layout behavior must be testable without requiring physical display hardware.

---

## 50.4 Lifecycle invariants

1. An Instance has one authoritative LUI identity regardless of backend object representation.

2. Destroy is explicit and idempotent from the application perspective.

3. Destroyed objects may not continue generating application-visible events.

4. Native callbacks may never reference an already-destroyed LUI object.

5. Parenting defines UI ownership unless a class explicitly documents another lifetime.

6. Application shutdown has deterministic ordering.

7. Backend destruction cannot synchronously reenter arbitrary Luau code unless explicitly defined by the LUI lifecycle contract.

---

## 50.5 Threading invariants

1. The Luau VM has one authoritative scheduler thread.

2. UI Instances are mutated only through that scheduler context.

3. Native worker threads never directly enter Luau.

4. Worker completion is marshaled through LUI.

5. Native backend APIs with UI-thread affinity are only called from their valid thread.

6. Application authors do not manually lock UI objects.

7. Application authors do not manually pump OS event loops.

8. Native backend notifications never synchronously reenter an already executing Luau VM. The scheduler dispatches them after the active VM and backend calls finish.

---

## 50.6 Native ABI invariants

1. The stable extension boundary is a C ABI.

2. C++ standard-library types never cross the stable ABI.

3. Internal pointers are not stable handles.

4. Every ABI structure is explicitly versioned or size-versioned.

5. Extensions receive only declared host capabilities.

6. Native object ownership is explicit.

7. Native callbacks cannot directly reenter Luau from arbitrary threads.

8. Raw `lua_State*` integration belongs to an explicitly unstable advanced tier.

9. Native ABI changes cannot silently reinterpret old structures.

10. Extension load failure must fail safely without corrupting the runtime.

---

## 50.7 Security invariants

1. No arbitrary FFI is exposed to ordinary Luau code.

2. Native pointers are opaque unless explicitly crossing a platform-interop boundary.

3. Native extension loading is explicit.

4. Capability access is declared rather than inferred.

5. Sandboxed execution cannot gain native-code execution through normal API surfaces.

6. Sandboxed execution cannot escape filesystem restrictions through path normalization bugs or native helper APIs.

7. Execution and memory limits are enforceable by the host.

8. Untrusted code cannot directly control the UI thread's native message pump.

9. IPC from the editor to preview runtime is treated as untrusted input.

10. Preview execution must never run application Luau inside the VS Code extension process.

11. Development metadata must not accidentally grant application capabilities.

12. Native handles exposed through interop APIs are typed and scope-aware wherever practical.

13. Serialization/deserialization of preview, build, or extension metadata is fail-closed.

---

## 50.8 Theming invariants

1. Default controls use native/system appearance wherever practical.

2. System light/dark/high-contrast changes propagate through the LUI theme system.

3. Explicit application styling overrides appropriate system defaults.

4. Platform-specific visual effects are optional capabilities.

5. Unsupported visual effects have documented fallback behavior.

6. Themes never change the semantic meaning of controls.

7. Accessibility modes take precedence over purely decorative styling when required for correct platform behavior.

8. Applications do not need platform conditionals for ordinary light/dark theming.

---

## 50.9 Accessibility invariants

1. Accessibility is part of class semantics.

2. Native controls retain their native accessibility support.

3. Custom controls must define their accessible role and state.

4. Keyboard-only operation must remain possible for standard interactive controls.

5. Focus state must remain exposed consistently.

6. Application styling must not silently disable required accessibility behavior.

7. Preview tooling may approximate accessibility presentation, but native backend qualification is authoritative.

---

## 50.10 Input invariants

1. LUI exposes semantic input events rather than raw platform message IDs.

2. Standard controls expose high-level actions such as `Activated`.

3. Keyboard, mouse, touch, accessibility activation, and pen input may map to the same high-level action where semantically equivalent.

4. Raw platform input remains behind explicit interop APIs.

5. Input callbacks execute through the LUI scheduler.

6. Focus behavior must remain deterministic from the application's perspective.

---

## 50.11 Reflection invariants

1. Runtime metadata has one authoritative definition.

2. Editor types are generated from runtime metadata.

3. Documentation is generated or validated against the same metadata.

4. Property Inspector schemas derive from the same metadata.

5. Backend conformance data derives from the same semantic definition.

6. A property cannot exist only in runtime code while being absent from reflection.

7. A reflected API cannot claim functionality the runtime does not implement.

---

## 50.12 Tooling invariants

1. Tooling consumes LUI semantics rather than reimplementing them.

2. Preview uses the same object/layout engine as production.

3. Native preview is authoritative where web preview cannot reproduce platform behavior.

4. VS Code never executes arbitrary application code in the extension host.

5. Source provenance must identify the Luau origin of development-mode Instances where available.

6. Preview/editor protocols are versioned.

7. Hot reload must not leave callbacks or native objects belonging to a dead runtime generation active.

---

## 50.13 Build invariants

1. Release applications are self-contained from the user's perspective.

2. Application developers do not need to redistribute a separate Luau installation.

3. Build artifacts contain only required capabilities and extensions.

4. Build mode does not change public LUI semantics.

5. Bytecode format is not a public compatibility contract.

6. Native extension compatibility is checked before application execution.

7. Release builds may strip development metadata without altering application behavior.

---

## 50.14 Compatibility invariants

1. Public semantic behavior is versioned separately from implementation details.

2. Backend changes cannot silently change existing property meaning.

3. Deprecation precedes removal of normal public APIs.

4. Internal native architecture may evolve without requiring ordinary Luau application changes.

5. Stable extension ABI compatibility is explicitly defined rather than assumed.

6. Platform-specific APIs may evolve independently while remaining outside the portable contract.

---

# 51. Recommended repository structure

```text
lui/
│
├── runtime/
│   ├── vm/
│   ├── scheduler/
│   ├── object/
│   ├── reflection/
│   ├── signals/
│   ├── lifecycle/
│   └── services/
│
├── ui/
│   ├── layout/
│   ├── input/
│   ├── focus/
│   ├── style/
│   ├── theme/
│   └── accessibility/
│
├── backends/
│   ├── winui3/
│   ├── gtk4/
│   └── preview/
│
├── native/
│   ├── abi/
│   ├── cpp/
│   └── extensions/
│
├── tools/
│   ├── cli/
│   ├── preview-host/
│   ├── packager/
│   └── generators/
│
├── vscode/
│
├── types/
│
├── tests/
│   ├── runtime/
│   ├── layout/
│   ├── conformance/
│   └── integration/
│
└── docs/
```

---

# 52. Short roadmap

The roadmap should deliberately avoid splitting the project into dozens of micro-phases.

## Foundation 0 — Runtime and Windows proof

Build:

- embedded Luau runtime;
- Instance/object model;
- properties;
- signals;
- scheduler;
- `Window`;
- `Frame`;
- `TextLabel`;
- `TextButton`;
- minimal WinUI 3 backend.

Acceptance example:

```lua
local window = Instance.new("Window", {
    Title = "Hello"
})

local button = Instance.new("TextButton", {
    Text = "Click",
    Parent = window,
})

button.Activated:Connect(function()
    print("Hello")
end)

window.Visible = true
```

The user must not write backend or event-loop code.

---

## Foundation 1 — Canonical UI semantics

Stabilize:

- Instance lifecycle;
- `GuiObject`;
- `UDim` / `UDim2`;
- layout engine;
- input;
- focus;
- common controls;
- basic services;
- reflection;
- generated Luau types;
- backend conformance tests.

At the end of this foundation, the public semantic model becomes the canonical base for future backends.

---

## Foundation 2 — Native integration and production Windows

Add:

- host C++ API;
- stable C ABI;
- extension registration;
- native async marshaling;
- dialogs;
- clipboard;
- assets;
- accessibility validation;
- DPI behavior;
- Windows packaging;
- native/platform escape hatches.

The result should be capable of real Windows desktop applications.

---

## Foundation 3 — Developer experience

Add:

- CLI;
- project manifests;
- generated typing;
- VS Code extension;
- preview host;
- Explorer;
- Properties inspector;
- source provenance;
- hot reload;
- native preview.

This turns LUI from a runtime into an application-development environment.

---

## Foundation 4 — Linux backend

Implement GTK4 against the already-canonical LUI backend contract.

Qualify using the same semantic conformance suite.

Do not redesign the public API around GTK behavior.

---

## Later work

Only after the above is stable:

- macOS backend;
- richer declarative libraries;
- visual editing;
- tray/notification APIs;
- drag and drop;
- advanced text;
- custom rendering surfaces;
- WebView;
- terminal;
- graphics/3D extension packages;
- optional direct Win32 lightweight backend;
- AOT/native Luau execution if justified.

---

# 53. First implementation target

The first implementation should intentionally be small.

Required:

```text
Luau VM
Instance
Signal
Connection
Window
Frame
TextLabel
TextButton
UDim
UDim2
basic parenting
basic layout
WinUI backend
UI scheduler
```

Not required:

```text
GTK
custom renderer
hot reload
designer
native plugins
complex theming
state reconciliation
drag and drop
rich text
macOS
```

The first milestone exists to prove one question:

> Can a native Windows application be authored with the simplicity and predictability of a Roblox Luau UI tree while retaining real native controls underneath?

If the answer is yes, the remaining architecture can grow around that proven semantic core.

---

# 54. Example target application

A normal LUI application should eventually look approximately like:

```lua
local DialogService = app:GetService("DialogService")

local window = Instance.new("Window", {
    Name = "MainWindow",
    Title = "Photo Manager",
    Size = UDim2.fromOffset(900, 600),
})

local root = Instance.new("Frame", {
    Size = UDim2.fromScale(1, 1),
    BackgroundTransparency = 1,
    Parent = window,
})

Instance.new("UIPadding", {
    PaddingTop = UDim.new(0, 16),
    PaddingBottom = UDim.new(0, 16),
    PaddingLeft = UDim.new(0, 16),
    PaddingRight = UDim.new(0, 16),
    Parent = root,
})

Instance.new("UIListLayout", {
    Padding = UDim.new(0, 8),
    Parent = root,
})

local title = Instance.new("TextLabel", {
    Text = "Photos",
    AutomaticSize = Enum.AutomaticSize.Y,
    Parent = root,
})

local browse = Instance.new("TextButton", {
    Text = "Browse...",
    Size = UDim2.new(1, 0, 0, 36),
    Parent = root,
})

browse.Activated:Connect(function()
    local file = DialogService:OpenFile({
        Filters = {
            {
                Name = "Images",
                Extensions = { "png", "jpg", "webp" },
            },
        },
    })

    if file then
        title.Text = file.Name
    end
end)

window.Visible = true
```

No application-side message loop exists.

No HWND is required.

No GTK callback is required.

No XAML exists.

No platform conditionals are required.

That is the standard LUI should be measured against.

---

# 55. Final architectural position

LUI should not be thought of as:

> Luau bindings for Windows UI.

It should be thought of as:

> **A native desktop application runtime built around Luau, a Roblox-like object model, native platform controls, a stable native integration layer, and first-class development tooling.**

The architecture is therefore:

```text
                      APPLICATION
                          │
                          ▼
                        Luau
                          │
                          ▼
              LUI semantic object model
                          │
          ┌───────────────┼────────────────┐
          │               │                │
       Runtime          Layout         Services
          │               │                │
          └───────────────┼────────────────┘
                          │
                  Backend Contract
                          │
          ┌───────────────┼────────────────┐
          ▼               ▼                ▼
       WinUI 3           GTK4           Preview
          │               │
       Windows           Linux

                   Native Integration
                          │
                  stable LUI C ABI
                          │
             ┌────────────┼────────────┐
             ▼            ▼            ▼
            C++          Rust         Zig
```

The canonical design rule remains:

> **Simple Luau surface. Rigorous native machinery underneath.**

LUI succeeds when an application developer can build ordinary desktop software without needing to learn the underlying desktop UI stack, while still having deliberate routes into native code when the abstraction genuinely needs to be crossed.

---

# 53. Networking and hosted endpoints (planned)

LUI should support ordinary desktop networking without requiring an application native extension. Foundation A implements `app:GetService("NetworkService")` for TCP; Foundation B implements outbound `HttpService` and hosted `HttpServerService` for plain HTTP; Foundation C implements bounded UDP, with TLS still pending. The [networking proposal](proposals/NETWORKING-ARCHITECTURE.md) is design input; [decision 0022](decisions/0022-networking-scope.md) records scope, and [decisions 0023–0025](decisions/0025-paced-dialing-and-closed-signal.md) record TCP implementation and limits.

The implemented TCP service has `ListenTcp` and `ConnectTcp`; a listener has `Port`, `BoundEndpoints`, `IsListening`, `AcceptAsync`, and `Close`; a connection has `LocalEndpoint`, `RemoteEndpoint`, `IsOpen`, `ReadAsync`, `ReadExactAsync`, `WriteAsync`, `Shutdown`, `Close`, and a `Closed` signal. Async methods may be called from a `task.spawn` or `task.defer` coroutine and resume on the owner scheduler thread. Network errors are thrown as strings with stable `[LUI:Network] Code` prefixes. Paired dual-family listeners and versioned host address/port policy are implemented as recorded in [decision 0024](decisions/0024-dual-family-and-network-policy.md). Paced dialing and `Closed` semantics are recorded in [decision 0025](decisions/0025-paced-dialing-and-closed-signal.md). [Foundation A notes](NETWORKING-FOUNDATION-A.md) state the exact shipped behavior and limitations.

## Placement and async boundary

Networking belongs to the portable runtime and platform-services layer. WinUI, GTK, and the preview renderer must not define network semantics. The pinned standalone Asio transport uses platform I/O mechanisms internally; no native socket handle, file descriptor, completion port, or transport-library object reaches ordinary Luau. LUI owns the public address, error, resource, and lifecycle contracts.

Async operations suspend only the calling Luau task. A network worker may resolve DNS or perform I/O, but may not enter Luau, mutate an Instance, or synchronously reenter the VM. It posts a bounded result to the authoritative scheduler, which resumes the task. Each operation has one terminal completion, failure, or cancellation, plus teardown on runtime destruction.

## Access and binding

The host declares network grants before scripts. Current TCP requires `network.client` for outbound connections, `network.server` for listeners, and `network.raw` in addition to the relevant direction grant. The default grant set remains empty. The version 1 host policy can restrict client destination and server listener ports and require loopback client destinations or server binds. Under loopback-only client policy, only numeric loopback addresses and `localhost` reach resolution; resolved endpoints are filtered before dialing. The headless preview must deny networking unless an explicitly designed preview policy grants it. Neither a Luau script nor a native backend may grant itself more access.

The listener address defaults to `loopback`. `any` and a specific non-loopback numeric local IP are explicit bind choices. Listener binding accepts semantic tokens or numeric local addresses, not DNS hostnames. Outbound connection APIs may resolve hostnames. No bind failure silently falls back to another interface. Port zero asks the OS for an ephemeral port, exposed through the resulting listener's read-only `Port` and `BoundEndpoints` values. `IPv4`, `IPv6`, and `DualStack` are supported family choices; numeric literals imply their family and reject a contradictory choice. Dual-family binding sets IPv6-only explicitly, binds IPv6 and IPv4 on one port, retries an ephemeral second-bind collision up to 16 times, and rolls back both on failure. If one family is unavailable, the whole bind fails. LUI never modifies firewall rules on its own.

## Raw transport semantics

`NetworkService` exposes `ListenTcp`, `ConnectTcp`, and `BindUdp`. A TCP listener offers `AcceptAsync` and an idempotent `Close`. A TCP connection offers `ReadAsync`, `ReadExactAsync`, ordered `WriteAsync`, `Shutdown`, idempotent `Close`, and a one-shot `Closed` signal. TCP is a byte stream: reads return 1..N bytes up to a bounded request size, or `nil` after clean EOF; they do not preserve writes as messages. A reset or other failure uses a portable `NetworkError` code. One connection rejects concurrent reads and bounds queued write bytes. A local close cancels pending operations and resumes waiting tasks once. Writes complete the supplied byte sequence or fail; completion does not promise peer application consumption. Binary reads use Luau `buffer`, while writes may accept `string` or `buffer`.

UDP retains datagram boundaries with `ReceiveFromAsync` and `SendToAsync`. Bind requires `network.server` and `network.raw`; send additionally requires `network.client`. Bind uses numeric or semantic addresses with explicit IPv4/IPv6 (IPv6-only) sockets, while sends accept numeric same-family destinations only. UDP does not yet support paired dual-family binding, DNS destinations, multicast/broadcast configuration, or IPv4-mapped IPv6 addresses. `LocalEndpoint` exposes the actual bound port; `IsOpen` and idempotent `Close` define lifecycle. Receive returns a read-only record with an owned mutable `Data: buffer` and read-only `RemoteEndpoint`, including an empty buffer for an empty datagram. Per-socket `MaxDatagramBytes` defaults to 65507 and accepts 1–65507. Oversized sends and oversized/truncated receives fail with `MessageTooLarge`; partial payloads never reach Luau, and recoverable receive errors leave the socket open. Sixteen retained handles per runtime, one receive waiter per socket, 32 queued/active sends and 256 KiB queued/active send payload per socket, and the shared 128 outstanding await/result ceiling bound resources. Send copies buffer content before yielding and completes local acceptance without guaranteeing remote delivery. Close cancels unsettled operations; already queued terminal outcomes remain settled. Receive has no automatic deadline. [Decision 0030](decisions/0030-bounded-udp-datagrams.md) and [Foundation C notes](NETWORKING-FOUNDATION-C.md) define the shipped UDP contract.

The address-family and path MTU can reduce the actual sendable payload below any policy ceiling. Outbound TCP selection now races at most two interleaved IPv4/IPv6 candidates with a 250 ms attempt delay and a ten-second overall deadline. It waits for the combined resolver result before starting, so full Happy Eyeballs v2 DNS query timing remains a possible later improvement.

Every transport resource has hard limits on connections, pending accepts, reads, writes, completions, and memory. Slow peers apply backpressure. Exceeding a limit yields a portable error, never an unbounded queue. Worker and scheduler errors must not surface as modal UI or remote stack traces.

## HTTP and TLS layering

Foundation B implements incremental request/response framing in `runtime/network/http`. It bounds lines, fields, body bytes, and chunk overhead, rejects ambiguous framing, and exposes only completed messages. It preserves the exact consumed boundary for persistent input and keeps admitted metadata trailers separate from headers. Its initial profile supports HTTP/1.1 origin-form requests and `OPTIONS *`, fixed/chunked bodies, body-free response exceptions, and EOF-delimited responses; proxy forms, upgrades, CONNECT, expectations, and other versions/codings are rejected. [Decision 0027](decisions/0027-bounded-http-framing.md) records the exact contract and limits.

`HttpService:RequestAsync` and `GetAsync` perform bounded plain HTTP exchanges using the shared worker/dialer and owner-thread completion path; `CancelAll` cancels pending HTTP work at an ordered worker barrier. Only `network.client` is required, with the existing host client address/port policy. Request bodies are capped at 1 MiB; responses default to 1 MiB with an 8 MiB configurable ceiling. Up to 32 calls/results and eight informational replies per call are admitted. A 1–60000 ms worker deadline (default 10000) covers DNS through final response. LUI owns framing and uses one socket per exchange. Binary response strings, duplicate-preserving header arrays, and metadata trailers are immutable. Non-2xx status codes are responses; protocol/transport failures are catchable `[LUI:Http] Code` errors. HTTPS, automatic retries/redirects, cookies, compression decoding, JSON helpers, and pooling remain pending. [Decision 0028](decisions/0028-outbound-http-service.md) and [Foundation B notes](NETWORKING-FOUNDATION-B.md) document the complete shipped contract and internal response serializer.

`HttpServerService` now defaults to loopback, uses only `network.server`, and shares TCP's bind/policy/worker path. CreateServer binds immediately; Route registers exact case-sensitive method/encoded path handlers before Start. Complete bounded parsing precedes scheduler dispatch. Requests carry immutable binary string content, ordered field pairs, metadata trailers, and portable endpoints. Each connection runs one yielding scheduler handler and writes one validated buffered response before handling the next, retaining at most a 16 KiB read suffix and admitting at most 100 requests. Unknown routes return 404; malformed/unsupported requests return 400, parser bounds return 413, and invalid/failed handlers produce generic 500 plus a local structured diagnostic. Every failed framing/handler path closes without resynchronization. Four retained server handles and 32 hosted session/handler slots per runtime, default 16 per server, bound admission. Request/response bodies default to 1 MiB, configurable to 8 MiB. A 1–60000 ms worker deadline (default 10000) covers idle input through response writing. Close/expiry prevents queued and suspended handler resumes; executing Luau still uses VM execution quotas. [Decision 0029](decisions/0029-hosted-http-service.md) states the exact implemented contract, reference-retention cancellation behavior, and limitations.

TLS is a later provider boundary for both client and server use. Server credentials are opaque LUI objects or host configuration; ordinary Luau never receives a native credential handle. Certificate validation is on by default and cannot be silently disabled. WebSocket, HTTP/2, HTTP/3, streaming bodies, multipart, proxy support, and local IPC are later extensions.

## Delivery order and qualification

Networking Foundation A is a Foundation 2 follow-on: first the scheduler's yielding async operation, then TCP and its capability checks, then the same headless conformance on Windows and Linux. Foundation B adds outbound and hosted HTTP plus malformed-request and request-smuggling regressions. Foundation C adds UDP and TLS. Existing Foundation 2 Windows exit criteria remain complete; this new track does not imply those audits tested networking. Only implemented services enter reflection metadata, generated Luau definitions, API documentation, and the editor schema.
