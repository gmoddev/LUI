add_executable(LuiHeadlessTests tests/runtime/Headless.cpp)
target_link_libraries(LuiHeadlessTests PRIVATE LuiRuntime)
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.luau")
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_CONTROLS_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/controls.luau")
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_GRID_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/grid.luau")
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_WINUI_FOCUS_CASE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/winui/FocusEdge.luau")
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_WINUI_INPUT_CASE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/winui/InputQualification.luau")
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_WINUI_NATIVE_CASE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/winui/NativeMapping.luau")
enable_testing()
add_executable(LuiWebSocketProtocolTests tests/network/WebSocketProtocol.cpp)
target_link_libraries(LuiWebSocketProtocolTests PRIVATE LuiWebSocket)
add_test(NAME LuiWebSocketProtocolTests COMMAND LuiWebSocketProtocolTests)
set_tests_properties(LuiWebSocketProtocolTests PROPERTIES TIMEOUT 30)
add_executable(LuiWebSocketTests tests/network/WebSocket.cpp tests/network/WebSocketLayout.c)
set_target_properties(LuiWebSocketTests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(LuiWebSocketTests PRIVATE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(LuiWebSocketTests PRIVATE ASIO_STANDALONE LUI_TLS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/network/tls")
if(WIN32)
    target_compile_definitions(LuiWebSocketTests PRIVATE _WIN32_WINNT=0x0A00)
endif()
target_link_libraries(LuiWebSocketTests PRIVATE LuiRuntime LuiTls)
add_test(NAME LuiWebSocketTests COMMAND LuiWebSocketTests)
set_tests_properties(LuiWebSocketTests PROPERTIES TIMEOUT 60)
add_subdirectory(native/standard)
option(LUI_STANDARD_FFI "Build the optional trusted-native FFI provider and its qualification fixture" OFF)
if(LUI_STANDARD_FFI)
    add_subdirectory(native/ffi)
    add_subdirectory(tests/ffi)
    set_target_properties(LuiFfi PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/StandardProviders/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/StandardProviders/$<CONFIG>")
    if(TARGET LuiFfiBroker)
        set_target_properties(LuiFfiBroker PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/StandardProviders/$<CONFIG>")
    endif()
endif()
set(LuiPinArgs)
set(LuiProviderTargets)
foreach(Name Compression Crypto SQLite Regex Toml Ffi)
    if(TARGET Lui${Name})
        list(APPEND LuiPinArgs "-D${Name}=$<TARGET_FILE:Lui${Name}>")
        list(APPEND LuiProviderTargets Lui${Name})
    endif()
endforeach()
add_executable(LuiHeadlessHost tools/headless/Main.cpp tools/headless/Stdio.cpp tools/headless/StandardProviders.cpp
    tools/headless/Environment.cpp tools/headless/ProcessHost.cpp tools/headless/ProcessLinux.cpp tools/headless/ProcessWindows.cpp)
if(LuiProviderTargets)
    set(PinDir "${CMAKE_CURRENT_BINARY_DIR}/StandardPins/$<CONFIG>")
    file(GENERATE OUTPUT "${PinDir}/Selection.txt" CONTENT "${LuiProviderTargets}\n")
    add_custom_command(OUTPUT "${PinDir}/LuiStandardPins.h"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PinDir}"
        COMMAND ${CMAKE_COMMAND} ${LuiPinArgs} "-DOutput=${PinDir}/LuiStandardPins.h" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StandardPins.cmake"
        DEPENDS ${LuiProviderTargets} "${PinDir}/Selection.txt" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StandardPins.cmake" VERBATIM)
    target_sources(LuiHeadlessHost PRIVATE "${PinDir}/LuiStandardPins.h")
    target_compile_definitions(LuiHeadlessHost PRIVATE LUI_STANDARD_PINS)
    target_include_directories(LuiHeadlessHost PRIVATE "${PinDir}")
endif()
target_link_libraries(LuiHeadlessHost PRIVATE LuiRuntime nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(LuiHeadlessHost PRIVATE shell32)
endif()
add_executable(LuiExecutionTests tests/runtime/Execution.cpp)
target_link_libraries(LuiExecutionTests PRIVATE LuiRuntime)
add_executable(LuiHttpPolicyTests tests/network/HttpPolicy.cpp)
target_include_directories(LuiHttpPolicyTests PRIVATE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(LuiHttpPolicyTests PRIVATE ASIO_STANDALONE)
if(WIN32)
    target_compile_definitions(LuiHttpPolicyTests PRIVATE _WIN32_WINNT=0x0A00)
endif()
target_link_libraries(LuiHttpPolicyTests PRIVATE LuiHttp LuiRuntime LuiTls)
if(UNIX)
    target_link_libraries(LuiHeadlessHost PRIVATE Threads::Threads)
    target_link_libraries(LuiExecutionTests PRIVATE Threads::Threads)
    set_target_properties(LuiHeadlessHost PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN/../lib")
    install(TARGETS LuiHeadlessHost RUNTIME DESTINATION bin COMPONENT SdkHeadless)
endif()
add_test(NAME LuiExecutionTests COMMAND LuiExecutionTests)
set_tests_properties(LuiExecutionTests PROPERTIES TIMEOUT 10)
add_test(NAME LuiHttpPolicyTests COMMAND LuiHttpPolicyTests)
set_tests_properties(LuiHttpPolicyTests PROPERTIES TIMEOUT 60)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME LuiWebSocketHeadlessTests COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/headless/WebSocket.py" "$<TARGET_FILE:LuiHeadlessHost>")
    set_tests_properties(LuiWebSocketHeadlessTests PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
    add_test(NAME LuiHeadlessHostTests COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/headless/Qualification.py" "$<TARGET_FILE:LuiHeadlessHost>")
    set_tests_properties(LuiHeadlessHostTests PROPERTIES TIMEOUT 60)
    add_test(NAME LuiLibraryTests COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/libraries/Run.py"
        --runtime "$<TARGET_FILE:LuiRuntime>" --checker "$<TARGET_FILE:LuiTypeCheck>" --suite all)
    set_tests_properties(LuiLibraryTests PROPERTIES TIMEOUT 90)
    add_test(NAME LuiNumericInputTests COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/libraries/Numeric.py"
        --runtime "$<TARGET_FILE:LuiRuntime>" --checker "$<TARGET_FILE:LuiTypeCheck>" --output "${CMAKE_CURRENT_BINARY_DIR}/NumericInput.json")
    set_tests_properties(LuiNumericInputTests PROPERTIES TIMEOUT 30)
    add_test(NAME LuiJsonQuotaTests COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/libraries/Run.py"
        --runtime "$<TARGET_FILE:LuiRuntime>" --suite Json --max-interrupts 1000000 --expect-limit)
    set_tests_properties(LuiJsonQuotaTests PROPERTIES TIMEOUT 30)
    add_test(NAME LuiJsonHttpTests COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/libraries/JsonHttp.py"
        --runtime "$<TARGET_FILE:LuiRuntime>" --checker "$<TARGET_FILE:LuiTypeCheck>"
        --output "${CMAKE_CURRENT_BINARY_DIR}/JsonHttp.json")
    set_tests_properties(LuiJsonHttpTests PROPERTIES TIMEOUT 30)
    add_test(NAME LuiWorkbenchTests COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/libraries/Workbench.py"
        --runtime "$<TARGET_FILE:LuiRuntime>")
    set_tests_properties(LuiWorkbenchTests PROPERTIES TIMEOUT 45)
endif()
if(WIN32)
    add_test(NAME LuiWinUIFrameBitmapTests COMMAND LuiWinUIFrameBitmapTests)
    add_test(NAME LuiWinUIFrameGpuTests COMMAND LuiWinUIFrameGpuTests)
    set_tests_properties(LuiWinUIFrameGpuTests PROPERTIES TIMEOUT 45)
endif()
add_test(NAME LuiFrameSurfaceTests COMMAND LuiFrameSurfaceTests)
add_test(NAME LuiFrameSurfacePacing COMMAND LuiFrameSurfaceTests --paced)
set_tests_properties(LuiFrameSurfaceTests LuiFrameSurfacePacing PROPERTIES TIMEOUT 60)
add_executable(LuiNativeServiceTests tests/services/Domain.cpp tests/services/Layout.c)
set_target_properties(LuiNativeServiceTests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_link_libraries(LuiNativeServiceTests PRIVATE LuiRuntime)
if(NOT WIN32)
    target_link_libraries(LuiNativeServiceTests PRIVATE Threads::Threads)
endif()
add_test(NAME LuiNativeServiceTests COMMAND LuiNativeServiceTests)
set_tests_properties(LuiNativeServiceTests PROPERTIES TIMEOUT 30)
add_executable(LuiNativeRuntimeTests tests/services/Runtime.cpp)
target_link_libraries(LuiNativeRuntimeTests PRIVATE LuiRuntime)
if(NOT WIN32)
    target_link_libraries(LuiNativeRuntimeTests PRIVATE Threads::Threads)
endif()
add_test(NAME LuiNativeRuntimeTests COMMAND LuiNativeRuntimeTests)
set_tests_properties(LuiNativeRuntimeTests PROPERTIES TIMEOUT 30)
add_executable(LuiOwnerWakeTests tests/services/Wake.cpp tests/services/WakeLayout.c)
set_target_properties(LuiOwnerWakeTests PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_link_libraries(LuiOwnerWakeTests PRIVATE LuiRuntime)
if(UNIX)
    target_link_libraries(LuiOwnerWakeTests PRIVATE Threads::Threads)
endif()
add_test(NAME LuiOwnerWakeTests COMMAND LuiOwnerWakeTests)
set_tests_properties(LuiOwnerWakeTests PROPERTIES TIMEOUT 30)
add_executable(LuiStorageTests tests/services/Storage.cpp)
target_link_libraries(LuiStorageTests PRIVATE LuiRuntime)
add_test(NAME LuiStorageTests COMMAND LuiStorageTests)
add_executable(LuiStorageFilesTests tests/services/StorageFiles.cpp runtime/services/StorageFiles.cpp)
add_test(NAME LuiStorageFilesTests COMMAND LuiStorageFilesTests)
if(UNIX)
    add_test(NAME LuiStorageFifoTests COMMAND LuiStorageFilesTests --fifo)
    set_tests_properties(LuiStorageFifoTests PROPERTIES TIMEOUT 5)
endif()
set_tests_properties(LuiStorageTests LuiStorageFilesTests PROPERTIES TIMEOUT 60)
add_executable(LuiDesktopServiceTests tests/services/Desktop.cpp)
target_link_libraries(LuiDesktopServiceTests PRIVATE LuiRuntime)
add_test(NAME LuiDesktopServiceTests COMMAND LuiDesktopServiceTests)
set_tests_properties(LuiDesktopServiceTests PROPERTIES TIMEOUT 30)
if(WIN32)
    add_executable(LuiWindowsDesktopQualification tests/services/WindowsDesktop.cpp)
    target_link_libraries(LuiWindowsDesktopQualification PRIVATE LuiRuntime shell32 user32)
endif()
if(LUI_BUILD_RUST_PROOF)
    find_program(LUI_CARGO_EXECUTABLE cargo HINTS "$ENV{USERPROFILE}/.cargo/bin" REQUIRED)
    find_program(LUI_RUSTC_EXECUTABLE rustc HINTS "$ENV{USERPROFILE}/.cargo/bin" REQUIRED)
    add_custom_target(LuiRustProof
        COMMAND "${LUI_RUSTC_EXECUTABLE}" --version
        COMMAND "${LUI_CARGO_EXECUTABLE}" --version
        COMMAND "${LUI_CARGO_EXECUTABLE}" build --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/tests/services/rust/Cargo.toml"
            --target-dir "${CMAKE_BINARY_DIR}/native-proof" --locked --offline --jobs 2
        COMMAND "${LUI_CARGO_EXECUTABLE}" build --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/tests/services/rust/Cargo.toml"
            --target-dir "${CMAKE_BINARY_DIR}/native-proof" --locked --offline --jobs 2 --release
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM)
    add_dependencies(LuiNativeRuntimeTests LuiRustProof)
    if(WIN32)
        set(LUI_RUST_PROOF_LIBRARY "lui_native_proof.dll")
    else()
        set(LUI_RUST_PROOF_LIBRARY "liblui_native_proof.so")
    endif()
    add_test(NAME LuiRustDebugInterop COMMAND LuiNativeRuntimeTests "${CMAKE_BINARY_DIR}/native-proof/debug/${LUI_RUST_PROOF_LIBRARY}")
    add_test(NAME LuiRustReleaseInterop COMMAND LuiNativeRuntimeTests "${CMAKE_BINARY_DIR}/native-proof/release/${LUI_RUST_PROOF_LIBRARY}")
    add_test(NAME LuiRustQuarantine COMMAND LuiNativeRuntimeTests --quarantine "${CMAKE_BINARY_DIR}/native-proof/release/${LUI_RUST_PROOF_LIBRARY}")
    set_tests_properties(LuiRustDebugInterop LuiRustReleaseInterop LuiRustQuarantine PROPERTIES TIMEOUT 30)
endif()
add_executable(LuiProjectTests tests/project/Project.cpp)
target_link_libraries(LuiProjectTests PRIVATE LuiProject nlohmann_json::nlohmann_json)
add_custom_command(TARGET LuiProjectTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LuiProjectTests>/THIRD-PARTY-LICENSES"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/Json.txt" "$<TARGET_FILE_DIR:LuiProjectTests>/THIRD-PARTY-LICENSES/Json.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/licenses/OpenSSL.txt" "$<TARGET_FILE_DIR:LuiProjectTests>/THIRD-PARTY-LICENSES/OpenSSL.txt")
add_test(NAME LuiProjectTests COMMAND LuiProjectTests)
add_executable(LuiCapturedProjectTests tests/browser/Captured.cpp)
target_link_libraries(LuiCapturedProjectTests PRIVATE LuiProject LuiRuntime nlohmann_json::nlohmann_json)
target_compile_definitions(LuiCapturedProjectTests PRIVATE LUI_BROWSER_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/browser/fixture")
add_test(NAME LuiCapturedProjectTests COMMAND LuiCapturedProjectTests)
add_executable(LuiPackageTests tests/project/Package.cpp)
target_link_libraries(LuiPackageTests PRIVATE LuiProject nlohmann_json::nlohmann_json)
add_test(NAME LuiPackageTests COMMAND LuiPackageTests)
set_tests_properties(LuiPackageTests PROPERTIES TIMEOUT 60)
add_executable(LuiProjectTool tools/packaging/ProjectTool.cpp)
add_executable(LuiBrowserTool tools/packaging/BrowserTool.cpp)
target_link_libraries(LuiBrowserTool PRIVATE LuiProject LuiRuntime nlohmann_json::nlohmann_json)
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
if(WIN32)
    # System Rich Edit proof, independent of the WinRT RichEditBox adapter.
    add_executable(LuiTemporaryTextTests tests/winui/TemporaryText.cpp)
    target_link_libraries(LuiTemporaryTextTests PRIVATE user32 ole32 oleaut32 uuid)
    add_test(NAME LuiTemporaryTextTests COMMAND LuiTemporaryTextTests)
    set_tests_properties(LuiTemporaryTextTests PROPERTIES TIMEOUT 30)
    add_library(LuiWinUIText SHARED backends/winui3/NativeText.cpp)
    target_compile_options(LuiWinUIText PRIVATE /EHsc)
    target_link_libraries(LuiWinUIText PRIVATE user32 gdi32 comctl32 ole32 oleaut32 uuid imm32)
endif()
add_executable(LuiCollectionTests tests/collections/Collections.cpp)
target_link_libraries(LuiCollectionTests PRIVATE LuiRuntime LuiProject nlohmann_json::nlohmann_json)
target_compile_definitions(LuiCollectionTests PRIVATE LUI_UI_LIBRARY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples/conversation/Vendor/LuiUI" LUI_COLLECTION_CASE="${CMAKE_CURRENT_SOURCE_DIR}/tests/collections/Checks.luau")
add_test(NAME LuiCollectionTests COMMAND LuiCollectionTests)
set_tests_properties(LuiCollectionTests PROPERTIES TIMEOUT 30)

add_executable(LuiSchemaDump tools/generators/SchemaDump.cpp)
target_link_libraries(LuiSchemaDump PRIVATE LuiRuntime)

add_executable(LuiConformanceTests tests/conformance/Conformance.cpp)
add_executable(LuiWindowLifecycleTests tests/window/Lifecycle.cpp)
target_link_libraries(LuiWindowLifecycleTests PRIVATE LuiRuntime)
add_test(NAME LuiWindowLifecycleTests COMMAND LuiWindowLifecycleTests)
target_link_libraries(LuiConformanceTests PRIVATE LuiRuntime)
add_test(NAME LuiConformanceTests COMMAND LuiConformanceTests)

add_executable(LuiTypeTests tests/types/Definitions.cpp)
target_link_libraries(LuiTypeTests PRIVATE Luau.Analysis)
target_compile_definitions(LuiTypeTests PRIVATE LUI_TYPES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/types/LUI.d.luau")
add_test(NAME LuiTypeTests COMMAND LuiTypeTests)

add_library(LuiProjectAnalysis STATIC tools/typecheck/ProjectCheck.cpp)
add_executable(LuiSelectedFileTests tests/services/SelectedFiles.cpp runtime/services/SelectedFiles.cpp)
target_include_directories(LuiSelectedFileTests PRIVATE runtime/services)
target_link_libraries(LuiSelectedFileTests PRIVATE LuiProject)
add_test(NAME LuiSelectedFileTests COMMAND LuiSelectedFileTests)
add_executable(LuiFileServiceTests tests/services/Files.cpp)
target_link_libraries(LuiFileServiceTests PRIVATE LuiRuntime)
add_test(NAME LuiFileServiceTests COMMAND LuiFileServiceTests)
set_tests_properties(LuiSelectedFileTests LuiFileServiceTests PROPERTIES TIMEOUT 30)
target_include_directories(LuiProjectAnalysis PUBLIC tools/typecheck)
target_link_libraries(LuiProjectAnalysis PRIVATE Luau.Analysis nlohmann_json::nlohmann_json PUBLIC LuiProject)
if(NOT WIN32)
    target_link_libraries(LuiProjectAnalysis PRIVATE Threads::Threads)
endif()
add_executable(LuiProjectTypeTests tests/types/Project.cpp)
add_executable(LuiOwnedProjectTests tests/services/Project.cpp)
target_link_libraries(LuiOwnedProjectTests PRIVATE LuiRuntime LuiProjectAnalysis nlohmann_json::nlohmann_json)
target_compile_definitions(LuiOwnedProjectTests PRIVATE LUI_TYPES_PATH="${CMAKE_CURRENT_SOURCE_DIR}/types/LUI.d.luau")
add_test(NAME LuiOwnedProjectTests COMMAND LuiOwnedProjectTests)
set_tests_properties(LuiOwnedProjectTests PROPERTIES TIMEOUT 60)
if(LUI_BUILD_RUST_PROOF)
    add_dependencies(LuiOwnedProjectTests LuiRustProof)
    add_test(NAME LuiRustCapturedProject COMMAND LuiOwnedProjectTests "${CMAKE_BINARY_DIR}/native-proof/release/${LUI_RUST_PROOF_LIBRARY}")
    set_tests_properties(LuiRustCapturedProject PROPERTIES TIMEOUT 60)
endif()
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
target_link_libraries(LuiTypeCheck PRIVATE Luau.Analysis Luau.Compiler LuiProjectAnalysis nlohmann_json::nlohmann_json)
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

add_executable(LuiHttpTests tests/network/Http.cpp tests/network/HttpClient.cpp tests/network/HttpServer.cpp tests/network/Tls.cpp)
target_include_directories(LuiHttpTests PRIVATE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(LuiHttpTests PRIVATE ASIO_STANDALONE)
if(WIN32)
    target_compile_definitions(LuiHttpTests PRIVATE _WIN32_WINNT=0x0A00)
endif()
target_link_libraries(LuiHttpTests PRIVATE LuiHttp LuiRuntime LuiTls)
target_compile_definitions(LuiHttpTests PRIVATE LUI_TLS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/network/tls")
add_test(NAME LuiHttpTests COMMAND LuiHttpTests)

add_executable(LuiAssetTests tests/assets/Assets.cpp)
target_link_libraries(LuiAssetTests PRIVATE LuiRuntime)
add_test(NAME LuiAssetTests COMMAND LuiAssetTests)

# Canonical dependency closure for native/tooling qualification on every OS.
add_custom_target(LuiNativeQualification DEPENDS
    LuiSelectedFileTests
    LuiFileServiceTests
    LuiWebSocketProtocolTests
    LuiWebSocketTests
    LuiHeadlessHost
    LuiExecutionTests
    LuiHttpPolicyTests
    LuiHeadlessTests
    LuiNativeServiceTests
    LuiNativeRuntimeTests
    LuiStorageTests
    LuiStorageFilesTests
    LuiDesktopServiceTests
    LuiProjectTests
    LuiCapturedProjectTests
    LuiPackageTests
    LuiProjectTool
    LuiBrowserTool
    LuiCompiledProjectTests
    LuiModuleTests
    LuiLayoutTests
    LuiTextTests
    LuiCollectionTests
    LuiSchemaDump
    LuiConformanceTests
    LuiTypeTests
    LuiProjectAnalysis
    LuiProjectTypeTests
    LuiOwnedProjectTests
    LuiDeclarationTests
    LuiNativeBindingTests
    LuiPrivacyTests
    LuiApplicationTests
    LuiNativeLoaderTests
    LuiTypeCheck
    LuiSampleExtension
    LuiFailedExtension
    LuiBadAbiExtension
    LuiExtraCapabilityExtension
    LuiLegacyExtension
    LuiExtensionTests
    LuiHostTests
    LuiSandboxTests
    LuiThemeTests
    LuiPlatformTests
    LuiNetworkTests
    LuiHttpTests
    LuiAssetTests)
