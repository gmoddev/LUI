include_guard(GLOBAL)
file(READ "${CMAKE_CURRENT_LIST_DIR}/Dependencies.json" LuiDependencyJson)
foreach(Name LuauRevision AsioRevision JsonUrl JsonSha256 OpenSslVersion OpenSslSha256)
    string(JSON Lui${Name} GET "${LuiDependencyJson}" "${Name}")
endforeach()
include(FetchContent)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_Declare(LuiJson
    URL "${LuiJsonUrl}"
    URL_HASH "SHA256=${LuiJsonSha256}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(LuiJson)
