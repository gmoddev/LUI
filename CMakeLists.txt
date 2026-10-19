cmake_minimum_required(VERSION 3.24)
project(LUI VERSION 0.0.1 LANGUAGES C CXX)
option(LUI_BUILD_GTK4 "Build the Linux GTK4 host and native qualification tests" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(DEFINED LUAU_SOURCE_DIR)
    add_subdirectory("${LUAU_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/luau")
    set(LUI_LUAU_LICENSE "${LUAU_SOURCE_DIR}/LICENSE.txt")
else()
    include(FetchContent)
    FetchContent_Declare(Luau
        GIT_REPOSITORY https://github.com/luau-lang/luau.git
        GIT_TAG c0e346edd89066b44dca174c9f54ce84c746a540
        GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(Luau)
    set(LUI_LUAU_LICENSE "${luau_SOURCE_DIR}/LICENSE.txt")
endif()

include(FetchContent)
FetchContent_Declare(Asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG 231cb29bab30f82712fcd54faaea42424cc6e710
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(Asio)

include(cmake/OpenSsl.cmake)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(LuiJson
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(LuiJson)
add_library(LuiReflection STATIC runtime/reflection/Schema.cpp)
target_include_directories(LuiReflection PUBLIC runtime/reflection)
add_library(LuiProject STATIC runtime/project/Project.cpp runtime/project/Files.cpp runtime/project/Compile.cpp runtime/project/Imports.cpp runtime/project/Package.cpp runtime/project/Declarations.cpp)
target_include_directories(LuiProject PUBLIC runtime/project)
target_link_libraries(LuiProject PRIVATE nlohmann_json::nlohmann_json Luau.Compiler PUBLIC LuiOpenSsl LuiReflection)
target_compile_definitions(LuiProject PRIVATE LUI_PROJECT_LUAU_REVISION="c0e346edd89066b44dca174c9f54ce84c746a540")

add_library(LuiTls STATIC runtime/network/tls/Provider.cpp)
target_include_directories(LuiTls PRIVATE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(LuiTls PUBLIC ASIO_STANDALONE)
target_link_libraries(LuiTls PUBLIC LuiOpenSsl)
if(WIN32)
    target_compile_definitions(LuiTls PRIVATE _WIN32_WINNT=0x0A00)
endif()

add_library(LuiRuntime SHARED runtime/LuiRuntime.cpp runtime/diagnostics/Diagnostics.cpp runtime/project/Host.cpp runtime/project/Modules.cpp runtime/preview/Snapshot.cpp runtime/input/Input.cpp runtime/extensions/Extensions.cpp runtime/platform/Services.cpp runtime/network/Network.cpp runtime/assets/Images.cpp ui/layout/Layout.cpp ui/style/Style.cpp)
target_include_directories(LuiRuntime PUBLIC native/abi)
target_include_directories(LuiRuntime PRIVATE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(LuiRuntime PRIVATE ASIO_STANDALONE)
target_link_libraries(LuiRuntime PRIVATE Luau.Compiler Luau.VM LuiTls LuiProject nlohmann_json::nlohmann_json)
if(WIN32)
    target_compile_definitions(LuiRuntime PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(LuiRuntime PRIVATE ws2_32 mswsock)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(LuiRuntime PRIVATE Threads::Threads)
endif()
target_compile_definitions(LuiRuntime PRIVATE LUI_BUILD)
add_custom_command(TARGET LuiRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LuiRuntime>/THIRD-PARTY-LICENSES"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/Json.txt"
        "$<TARGET_FILE_DIR:LuiRuntime>/THIRD-PARTY-LICENSES/Json.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/OpenSSL.txt"
        "$<TARGET_FILE_DIR:LuiRuntime>/THIRD-PARTY-LICENSES/OpenSSL.txt")
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
add_executable(LuiProjectTests tests/project/Project.cpp)
target_link_libraries(LuiProjectTests PRIVATE LuiProject nlohmann_json::nlohmann_json)
add_custom_command(TARGET LuiProjectTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LuiProjectTests>/THIRD-PARTY-LICENSES"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/Json.txt" "$<TARGET_FILE_DIR:LuiProjectTests>/THIRD-PARTY-LICENSES/Json.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/OpenSSL.txt" "$<TARGET_FILE_DIR:LuiProjectTests>/THIRD-PARTY-LICENSES/OpenSSL.txt")
add_test(NAME LuiProjectTests COMMAND LuiProjectTests)
add_executable(LuiPackageTests tests/project/Package.cpp)
target_link_libraries(LuiPackageTests PRIVATE LuiProject nlohmann_json::nlohmann_json)
add_test(NAME LuiPackageTests COMMAND LuiPackageTests)
set_tests_properties(LuiPackageTests PROPERTIES TIMEOUT 60)
add_executable(LuiProjectTool tools/packaging/ProjectTool.cpp)
target_link_libraries(LuiProjectTool PRIVATE LuiProject nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(LuiProjectTool PRIVATE shell32)
endif()
add_custom_command(TARGET LuiProjectTool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LuiProjectTool>/THIRD-PARTY-LICENSES"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/Json.txt" "$<TARGET_FILE_DIR:LuiProjectTool>/THIRD-PARTY-LICENSES/Json.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/OpenSSL.txt" "$<TARGET_FILE_DIR:LuiProjectTool>/THIRD-PARTY-LICENSES/OpenSSL.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LUI_LUAU_LICENSE}" "$<TARGET_FILE_DIR:LuiProjectTool>/THIRD-PARTY-LICENSES/Luau.txt")
add_executable(LuiCompiledProjectTests tests/project/Compiled.cpp tests/project/Abi.c)
target_link_libraries(LuiCompiledProjectTests PRIVATE LuiProject LuiRuntime nlohmann_json::nlohmann_json)
if(NOT WIN32)
    target_link_libraries(LuiCompiledProjectTests PRIVATE Threads::Threads)
endif()
add_test(NAME LuiCompiledProjectTests COMMAND LuiCompiledProjectTests)
add_executable(LuiModuleTests tests/project/Modules.cpp)
target_link_libraries(LuiModuleTests PRIVATE LuiRuntime LuiProject nlohmann_json::nlohmann_json)
if(NOT WIN32)
    target_link_libraries(LuiModuleTests PRIVATE Threads::Threads)
endif()
add_test(NAME LuiModuleTests COMMAND LuiModuleTests)
set_tests_properties(LuiModuleTests PROPERTIES TIMEOUT 30)
add_test(NAME LuiHeadlessTests COMMAND LuiHeadlessTests)

add_executable(LuiLayoutTests tests/layout/Layout.cpp)
target_link_libraries(LuiLayoutTests PRIVATE LuiRuntime)
add_test(NAME LuiLayoutTests COMMAND LuiLayoutTests)
add_executable(LuiTextTests tests/text/Text.cpp)
target_link_libraries(LuiTextTests PRIVATE LuiRuntime)
add_test(NAME LuiTextTests COMMAND LuiTextTests)
add_executable(LuiCollectionTests tests/collections/Collections.cpp)
target_link_libraries(LuiCollectionTests PRIVATE LuiRuntime LuiProject nlohmann_json::nlohmann_json)
target_compile_definitions(LuiCollectionTests PRIVATE LUI_UI_LIBRARY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples/conversation/Vendor/LuiUI" LUI_COLLECTION_CASE="${CMAKE_CURRENT_SOURCE_DIR}/tests/collections/Checks.luau")
add_test(NAME LuiCollectionTests COMMAND LuiCollectionTests)
set_tests_properties(LuiCollectionTests PROPERTIES TIMEOUT 30)

add_executable(LuiSchemaDump tools/generators/SchemaDump.cpp)
target_link_libraries(LuiSchemaDump PRIVATE LuiRuntime)

add_executable(LuiConformanceTests tests/conformance/Conformance.cpp)
target_link_libraries(LuiConformanceTests PRIVATE LuiRuntime)
add_test(NAME LuiConformanceTests COMMAND LuiConformanceTests)

add_executable(LuiTypeTests tests/types/Definitions.cpp)
target_link_libraries(LuiTypeTests PRIVATE Luau.Analysis)
target_compile_definitions(LuiTypeTests PRIVATE LUI_TYPES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/types/LUI.d.luau")
add_test(NAME LuiTypeTests COMMAND LuiTypeTests)

add_library(LuiProjectAnalysis STATIC tools/typecheck/ProjectCheck.cpp)
target_include_directories(LuiProjectAnalysis PUBLIC tools/typecheck)
target_link_libraries(LuiProjectAnalysis PRIVATE Luau.Analysis nlohmann_json::nlohmann_json PUBLIC LuiProject)
if(NOT WIN32)
    target_link_libraries(LuiProjectAnalysis PRIVATE Threads::Threads)
endif()
add_executable(LuiProjectTypeTests tests/types/Project.cpp)
target_link_libraries(LuiProjectTypeTests PRIVATE LuiProjectAnalysis nlohmann_json::nlohmann_json)
target_compile_definitions(LuiProjectTypeTests PRIVATE LUI_TYPES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/types/LUI.d.luau")
add_test(NAME LuiProjectTypeTests COMMAND LuiProjectTypeTests)
set_tests_properties(LuiProjectTypeTests PROPERTIES TIMEOUT 60)
add_executable(LuiDeclarationTests tests/project/Declarations.cpp)
target_link_libraries(LuiDeclarationTests PRIVATE LuiRuntime LuiProjectAnalysis nlohmann_json::nlohmann_json)
target_compile_definitions(LuiDeclarationTests PRIVATE LUI_TYPES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/types/LUI.d.luau")
add_test(NAME LuiDeclarationTests COMMAND LuiDeclarationTests)
set_tests_properties(LuiDeclarationTests PROPERTIES TIMEOUT 60)
add_executable(LuiNativeBindingTests tests/project/NativeBindings.cpp)
target_link_libraries(LuiNativeBindingTests PRIVATE LuiRuntime LuiProjectAnalysis nlohmann_json::nlohmann_json)
target_compile_definitions(LuiNativeBindingTests PRIVATE LUI_TYPES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/types/LUI.d.luau")
add_test(NAME LuiNativeBindingTests COMMAND LuiNativeBindingTests)
set_tests_properties(LuiNativeBindingTests PROPERTIES TIMEOUT 60)
add_executable(LuiPrivacyTests tests/project/Privacy.cpp)
target_link_libraries(LuiPrivacyTests PRIVATE LuiRuntime nlohmann_json::nlohmann_json)
add_test(NAME LuiPrivacyTests COMMAND LuiPrivacyTests)
set_tests_properties(LuiPrivacyTests PROPERTIES TIMEOUT 60)
add_executable(LuiApplicationTests tests/project/Application.cpp)
target_link_libraries(LuiApplicationTests PRIVATE LuiRuntime LuiProject nlohmann_json::nlohmann_json)
target_compile_definitions(LuiApplicationTests PRIVATE LUI_MODULAR_MANIFEST="${CMAKE_CURRENT_SOURCE_DIR}/examples/modular/lui.json")
add_test(NAME LuiApplicationTests COMMAND LuiApplicationTests)
add_executable(LuiNativeLoaderTests tests/project/NativeLoader.cpp)
target_link_libraries(LuiNativeLoaderTests PRIVATE LuiRuntime LuiProject nlohmann_json::nlohmann_json)
add_test(NAME LuiNativeLoaderTests COMMAND LuiNativeLoaderTests $<TARGET_FILE:LuiSampleExtension>)
add_executable(LuiTypeCheck tools/typecheck/Main.cpp)
target_link_libraries(LuiTypeCheck PRIVATE Luau.Analysis LuiProjectAnalysis nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(LuiTypeCheck PRIVATE shell32)
endif()
add_custom_command(TARGET LuiTypeCheck POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LuiTypeCheck>/THIRD-PARTY-LICENSES"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/Json.txt" "$<TARGET_FILE_DIR:LuiTypeCheck>/THIRD-PARTY-LICENSES/Json.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/OpenSSL.txt" "$<TARGET_FILE_DIR:LuiTypeCheck>/THIRD-PARTY-LICENSES/OpenSSL.txt")
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

add_executable(LuiNetworkTests tests/network/Tcp.cpp tests/network/Udp.cpp)
target_link_libraries(LuiNetworkTests PRIVATE LuiRuntime)
add_test(NAME LuiNetworkTests COMMAND LuiNetworkTests)

add_library(LuiHttp STATIC runtime/network/http/Parser.cpp runtime/network/http/Serializer.cpp)
target_include_directories(LuiHttp PUBLIC runtime/network/http PRIVATE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(LuiHttp PRIVATE ASIO_STANDALONE)
if(WIN32)
    target_compile_definitions(LuiHttp PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(LuiHttp PRIVATE ws2_32)
endif()
add_executable(LuiHttpTests tests/network/Http.cpp tests/network/HttpClient.cpp tests/network/HttpServer.cpp tests/network/Tls.cpp)
target_include_directories(LuiHttpTests PRIVATE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(LuiHttpTests PRIVATE ASIO_STANDALONE)
if(WIN32)
    target_compile_definitions(LuiHttpTests PRIVATE _WIN32_WINNT=0x0A00)
endif()
target_link_libraries(LuiHttpTests PRIVATE LuiHttp LuiRuntime LuiTls)
target_compile_definitions(LuiHttpTests PRIVATE LUI_TLS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/network/tls")
add_test(NAME LuiHttpTests COMMAND LuiHttpTests)
target_link_libraries(LuiRuntime PRIVATE LuiHttp)

add_executable(LuiAssetTests tests/assets/Assets.cpp)
target_link_libraries(LuiAssetTests PRIVATE LuiRuntime)
add_test(NAME LuiAssetTests COMMAND LuiAssetTests)
if(LUI_BUILD_GTK4)
    add_subdirectory(backends/gtk4)
endif()
