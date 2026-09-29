cmake_minimum_required(VERSION 3.24)
project(LUI VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
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

add_library(LuiRuntime SHARED runtime/LuiRuntime.cpp)
target_include_directories(LuiRuntime PUBLIC native/abi)
target_link_libraries(LuiRuntime PRIVATE Luau.Compiler Luau.VM)
target_compile_definitions(LuiRuntime PRIVATE LUI_BUILD)

add_executable(LuiHeadlessTests tests/runtime/Headless.cpp)
target_link_libraries(LuiHeadlessTests PRIVATE LuiRuntime)
target_compile_definitions(LuiHeadlessTests PRIVATE LUI_EXAMPLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.luau")
enable_testing()
add_test(NAME LuiHeadlessTests COMMAND LuiHeadlessTests)
