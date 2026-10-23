cmake_minimum_required(VERSION 3.24)
project(LUI VERSION 0.0.1 LANGUAGES C CXX)
include(cmake/Dependencies.cmake)
include(cmake/Sources.cmake)
option(LUI_BUILD_GTK4 "Build the Linux GTK4 host and native qualification tests" OFF)
option(LUI_BUILD_RUST_PROOF "Build the independent native ABI v2 Rust qualification fixture" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(EMSCRIPTEN)
    add_compile_options(-fexceptions)
    # Optimize the shared runtime and Luau together for the qualified browser
    # build. Debug remains available for diagnosis; native profiles are unchanged.
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

if(DEFINED LUAU_SOURCE_DIR)
    add_subdirectory("${LUAU_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/luau")
    set(LUI_LUAU_LICENSE "${LUAU_SOURCE_DIR}/LICENSE.txt")
else()
    include(FetchContent)
    FetchContent_Declare(Luau
        GIT_REPOSITORY https://github.com/luau-lang/luau.git
        GIT_TAG ${LuiLuauRevision}
        GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(Luau)
    set(LUI_LUAU_LICENSE "${luau_SOURCE_DIR}/LICENSE.txt")
endif()

if(EMSCRIPTEN)
    include(cmake/Browser.cmake)
    return()
endif()

include(FetchContent)
FetchContent_Declare(Asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG ${LuiAsioRevision}
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(Asio)

include(cmake/OpenSsl.cmake)

add_library(LuiReflection STATIC runtime/reflection/Schema.cpp)
target_include_directories(LuiReflection PUBLIC runtime/reflection)
add_library(LuiProject STATIC ${LuiProjectSources} runtime/project/Files.cpp runtime/project/HashOpenSsl.cpp runtime/project/Package.cpp)
target_include_directories(LuiProject PUBLIC runtime/project)
target_link_libraries(LuiProject PRIVATE nlohmann_json::nlohmann_json Luau.Compiler PUBLIC LuiOpenSsl LuiReflection)
target_compile_definitions(LuiProject PRIVATE LUI_PROJECT_LUAU_REVISION="${LuiLuauRevision}")

add_library(LuiTls STATIC runtime/network/tls/Provider.cpp)
target_include_directories(LuiTls PRIVATE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(LuiTls PUBLIC ASIO_STANDALONE)
target_link_libraries(LuiTls PUBLIC LuiOpenSsl)
if(WIN32)
    target_compile_definitions(LuiTls PRIVATE _WIN32_WINNT=0x0A00)
endif()

add_library(LuiRuntime SHARED ${LuiRuntimeSources} ${LuiDesktopSources})
target_include_directories(LuiRuntime PUBLIC native/abi)
target_include_directories(LuiRuntime PRIVATE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(LuiRuntime PRIVATE ASIO_STANDALONE)
target_link_libraries(LuiRuntime PRIVATE Luau.Compiler Luau.VM LuiTls LuiProject nlohmann_json::nlohmann_json)
if(WIN32)
    target_compile_definitions(LuiRuntime PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(LuiRuntime PRIVATE ws2_32 mswsock shell32 user32)
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

add_library(LuiHttp STATIC runtime/network/http/Parser.cpp runtime/network/http/Serializer.cpp)
target_include_directories(LuiHttp PUBLIC runtime/network/http PRIVATE "${asio_SOURCE_DIR}/asio/include")
target_compile_definitions(LuiHttp PRIVATE ASIO_STANDALONE)
if(WIN32)
    target_compile_definitions(LuiHttp PRIVATE _WIN32_WINNT=0x0A00)
    target_link_libraries(LuiHttp PRIVATE ws2_32)
endif()
target_link_libraries(LuiRuntime PRIVATE LuiHttp)

include(cmake/NativeTests.cmake)
if(LUI_BUILD_GTK4)
    add_subdirectory(backends/gtk4)
endif()
