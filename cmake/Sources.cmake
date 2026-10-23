# Shared semantics compile unchanged into desktop and browser profiles.
set(LuiRuntimeSources
    runtime/LuiRuntime.cpp runtime/diagnostics/Diagnostics.cpp
    runtime/project/Host.cpp runtime/project/Modules.cpp runtime/preview/Snapshot.cpp
    runtime/input/Input.cpp runtime/platform/Services.cpp runtime/assets/Images.cpp
    ui/layout/Layout.cpp ui/style/Style.cpp)
set(LuiProjectSources
    runtime/project/Project.cpp runtime/project/Paths.cpp runtime/project/Captured.cpp
    runtime/project/Compile.cpp runtime/project/Imports.cpp runtime/project/Declarations.cpp)
set(LuiDesktopSources
    runtime/extensions/Extensions.cpp runtime/services/Domain.cpp runtime/services/Runtime.cpp
    runtime/services/Storage.cpp runtime/services/StorageFiles.cpp runtime/services/Desktop.cpp
    runtime/services/WindowsDesktop.cpp runtime/network/Network.cpp)
