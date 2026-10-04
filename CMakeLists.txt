cmake_minimum_required(VERSION 3.24)
project(LUI VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(DEFINED LUAU_SOURCE_DIR)
    add_subdirectory("${LUAU_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/luau")
else()
    include(FetchContent)
    FetchContent_Declare(Luau
        GIT_REPOSITORY https://github.com/luau-lang/luau.git
        GIT_TAG c0e346edd89066b44dca174c9f54ce84c746a540
        GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(Luau)
endif()

include(FetchContent)
FetchContent_Declare(Asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG 231cb29bab30f82712fcd54faaea42424cc6e710
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(Asio)

add_library(LuiRuntime SHARED runtime/LuiRuntime.cpp runtime/preview/Snapshot.cpp runtime/input/Input.cpp runtime/extensions/Extensions.cpp runtime/platform/Services.cpp runtime/network/Network.cpp runtime/reflection/Schema.cpp ui/layout/Layout.cpp)
target_include_directories(LuiRuntime PUBLIC native/abi)
target_include_directories(LuiRuntime PRIVATE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(LuiRuntime PRIVATE ASIO_STANDALONE)
target_link_libraries(LuiRuntime PRIVATE Luau.Compiler Luau.VM)
if(WIN32)
    target_compile_definitions(LuiRuntime PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(LuiRuntime PRIVATE ws2_32 mswsock)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(LuiRuntime PRIVATE Threads::Threads)
endif()
target_compile_definitions(LuiRuntime PRIVATE LUI_BUILD)
if(UNIX AND NOT APPLE)
    target_link_libraries(LuiRuntime PRIVATE dl)
endif()

add_executable(LuiHeadlessTests tests/runtime/Headless.cpp)
target_link_libraries(LuiHeadlessTests PRIVATE LuiRuntime)
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.luau")
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_CONTROLS_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/controls.luau")
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_GRID_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/grid.luau")
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_WINUI_FOCUS_CASE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/winui/FocusEdge.luau")
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_WINUI_INPUT_CASE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/winui/InputQualification.luau")
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_WINUI_NATIVE_CASE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/winui/NativeMapping.luau")
enable_testing()
add_test(NAME LuiHeadlessTests COMMAND LuiHeadlessTests)

add_executable(LuiLayoutTests tests/layout/Layout.cpp)
target_link_libraries(LuiLayoutTests PRIVATE LuiRuntime)
add_test(NAME LuiLayoutTests COMMAND LuiLayoutTests)

add_executable(LuiSchemaDump tools/generators/SchemaDump.cpp)
target_link_libraries(LuiSchemaDump PRIVATE LuiRuntime)

add_executable(LuiConformanceTests tests/conformance/Conformance.cpp)
target_link_libraries(LuiConformanceTests PRIVATE LuiRuntime)
add_test(NAME LuiConformanceTests COMMAND LuiConformanceTests)

add_executable(LuiTypeTests tests/types/Definitions.cpp)
target_link_libraries(LuiTypeTests PRIVATE Luau.Analysis)
target_compile_definitions(LuiTypeTests PRIVATE LUI_TYPES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/types/LUI.d.luau")
add_test(NAME LuiTypeTests COMMAND LuiTypeTests)

add_executable(LuiTypeCheck tools/typecheck/Main.cpp)
target_link_libraries(LuiTypeCheck PRIVATE Luau.Analysis)
# Keep the reflected definitions beside the checker for CLI/editor discovery.
add_custom_target(LuiTypeDefinitions
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LuiTypeCheck>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/types/LUI.d.luau" "$<TARGET_FILE_DIR:LuiTypeCheck>/LUI.d.luau")
add_dependencies(LuiTypeCheck LuiTypeDefinitions)

add_library(LuiSampleExtension SHARED tests/extensions/Sample.c)
target_include_directories(LuiSampleExtension PRIVATE native/abi)
target_compile_definitions(LuiSampleExtension PRIVATE LUI_EXTENSION_BUILD)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(LuiSampleExtension PRIVATE Threads::Threads)
endif()

add_library(LuiFailedExtension SHARED tests/extensions/Sample.c)
target_include_directories(LuiFailedExtension PRIVATE native/abi)
target_compile_definitions(LuiFailedExtension PRIVATE LUI_EXTENSION_BUILD LUI_SAMPLE_FAIL_INIT)
if(UNIX)
    target_link_libraries(LuiFailedExtension PRIVATE Threads::Threads)
endif()

add_library(LuiBadAbiExtension SHARED tests/extensions/Sample.c)
target_include_directories(LuiBadAbiExtension PRIVATE native/abi)
target_compile_definitions(LuiBadAbiExtension PRIVATE LUI_EXTENSION_BUILD LUI_SAMPLE_BAD_ABI)
if(UNIX)
    target_link_libraries(LuiBadAbiExtension PRIVATE Threads::Threads)
endif()

add_library(LuiExtraCapabilityExtension SHARED tests/extensions/Sample.c)
target_include_directories(LuiExtraCapabilityExtension PRIVATE native/abi)
target_compile_definitions(LuiExtraCapabilityExtension PRIVATE LUI_EXTENSION_BUILD LUI_SAMPLE_EXTRA_CAPABILITY)
if(UNIX)
    target_link_libraries(LuiExtraCapabilityExtension PRIVATE Threads::Threads)
endif()

add_library(LuiLegacyExtension SHARED tests/extensions/Legacy.c)
target_include_directories(LuiLegacyExtension PRIVATE native/abi)
target_compile_definitions(LuiLegacyExtension PRIVATE LUI_EXTENSION_BUILD)

add_executable(LuiExtensionTests tests/extensions/ExtensionTests.cpp)
target_link_libraries(LuiExtensionTests PRIVATE LuiRuntime)
target_compile_definitions(LuiExtensionTests PRIVATE LUI_NATIVE_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/native-service.luau")
add_test(NAME LuiExtensionTests COMMAND LuiExtensionTests
    $<TARGET_FILE:LuiSampleExtension> $<TARGET_FILE:LuiFailedExtension>
    $<TARGET_FILE:LuiBadAbiExtension> $<TARGET_FILE:LuiExtraCapabilityExtension>
    $<TARGET_FILE:LuiLegacyExtension>)

add_executable(LuiHostTests tests/host/Host.cpp)
target_include_directories(LuiHostTests PRIVATE native/host)
target_link_libraries(LuiHostTests PRIVATE LuiRuntime)
add_test(NAME LuiHostTests COMMAND LuiHostTests)

add_executable(LuiSandboxTests tests/sandbox/Sandbox.cpp)
target_include_directories(LuiSandboxTests PRIVATE native/host)
target_link_libraries(LuiSandboxTests PRIVATE LuiRuntime)
add_test(NAME LuiSandboxTests COMMAND LuiSandboxTests)

add_executable(LuiThemeTests tests/theme/Theme.cpp)
target_link_libraries(LuiThemeTests PRIVATE LuiRuntime)
add_test(NAME LuiThemeTests COMMAND LuiThemeTests)

add_executable(LuiPlatformTests tests/platform/Services.cpp)
target_link_libraries(LuiPlatformTests PRIVATE LuiRuntime)
add_test(NAME LuiPlatformTests COMMAND LuiPlatformTests)

add_executable(LuiNetworkTests tests/network/Tcp.cpp)
target_link_libraries(LuiNetworkTests PRIVATE LuiRuntime)
add_test(NAME LuiNetworkTests COMMAND LuiNetworkTests)

add_executable(LuiAssetTests tests/assets/Assets.cpp)
target_link_libraries(LuiAssetTests PRIVATE LuiRuntime)
add_test(NAME LuiAssetTests COMMAND LuiAssetTests)
