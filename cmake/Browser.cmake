# Experimental browser feasibility target. Desktop providers are deliberately absent.
add_library(LuiBrowserCore STATIC ${LuiRuntimeSources} ${LuiProjectSources}
    runtime/project/HashPortable.cpp runtime/reflection/Schema.cpp backends/browser/Unavailable.cpp)
target_sources(LuiBrowserCore PRIVATE runtime/presentation/Frames.cpp)
target_include_directories(LuiBrowserCore PUBLIC native/abi)
target_link_libraries(LuiBrowserCore PRIVATE Luau.Compiler Luau.VM nlohmann_json::nlohmann_json)
target_compile_definitions(LuiBrowserCore PRIVATE LUI_BUILD LUI_CAPTURED_ONLY
    LUI_PROJECT_LUAU_REVISION="${LuiLuauRevision}")
add_executable(LuiBrowserProbe backends/browser/Probe.cpp)
target_link_libraries(LuiBrowserProbe PRIVATE LuiBrowserCore nlohmann_json::nlohmann_json)
set_target_properties(LuiBrowserProbe PROPERTIES OUTPUT_NAME LuiBrowser SUFFIX .mjs)
target_link_options(LuiBrowserProbe PRIVATE --no-entry -fexceptions
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=worker -sDYNAMIC_EXECUTION=0
    -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=268435456 -sINITIAL_MEMORY=33554432
    -sSTACK_SIZE=1048576 -sASSERTIONS=1
    "-sEXPORTED_FUNCTIONS=['_Lui_BrowserStart','_Lui_BrowserBegin','_Lui_BrowserCapture','_Lui_BrowserTake','_Lui_BrowserAck','_Lui_BrowserActivate','_Lui_BrowserEvent','_Lui_BrowserViewport','_Lui_BrowserTheme','_Lui_BrowserPump','_Lui_BrowserRetire','_Lui_BrowserMemory']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall']")
foreach(Name Worker.mjs Host.mjs Protocol.mjs Deployment.mjs Application.mjs Entry.mjs Probe.html Probe.css)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/backends/browser/${Name}" "${CMAKE_BINARY_DIR}/${Name}" COPYONLY)
endforeach()
configure_file(LICENSE "${CMAKE_BINARY_DIR}/LICENSE" COPYONLY)
configure_file("${LUI_LUAU_LICENSE}" "${CMAKE_BINARY_DIR}/THIRD-PARTY-LICENSES/Luau.txt" COPYONLY)
foreach(Name Json PicoSHA2)
    configure_file("third_party/licenses/${Name}.txt" "${CMAKE_BINARY_DIR}/THIRD-PARTY-LICENSES/${Name}.txt" COPYONLY)
endforeach()
