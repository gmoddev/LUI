if(CMAKE_SCRIPT_MODE_FILE)
    if(NOT DEFINED LuiConfiguration)
        message(FATAL_ERROR "[LUI:Build] Native compiler qualification requires a configuration")
    endif()
    string(TOUPPER "${LuiConfiguration}" LuiNormalizedConfiguration)
    if(LuiNormalizedConfiguration STREQUAL "RELEASE")
        message(FATAL_ERROR
            "[LUI:Build] MSVC 19.37.32822.0 x64 Release is refused: its Luau interpreter unwind metadata corrupts error recovery. Select another toolchain; MSVC 19.44.35228 passed qualification. Debug remains available.")
    endif()
    return()
endif()

# Refuse only the demonstrated failing profile. In a Visual Studio build tree,
# Debug must remain usable even when Release cannot produce an admitted runtime.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows"
    AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
    AND CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64"
    AND CMAKE_CXX_COMPILER_VERSION STREQUAL "19.37.32822.0")
    message(WARNING
        "[LUI:Build] MSVC 19.37.32822.0 x64 failed Release interpreter qualification. Release builds are disabled; Debug remains available. The qualified Windows profile uses MSVC 19.44.35228.")
    add_custom_target(LuiCompilerQualification
        COMMAND "${CMAKE_COMMAND}" "-DLuiConfiguration=$<CONFIG>"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        VERBATIM)
    add_dependencies(Luau.VM LuiCompilerQualification)
endif()
