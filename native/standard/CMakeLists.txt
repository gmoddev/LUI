cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(LuiStandardProviders LANGUAGES C CXX)
endif()
option(LUI_STANDARD_COMPRESSION "Build the optional gzip, zlib and raw DEFLATE provider" OFF)
option(LUI_STANDARD_CRYPTO "Build the optional SHA-256, HMAC and secure random provider" OFF)
option(LUI_STANDARD_SQLITE "Build the optional SQLite transaction provider" OFF)

option(LUI_STANDARD_REGEX "Build the optional byte-pattern matcher" OFF)
option(LUI_STANDARD_TOML "Build the optional TOML configuration codec" OFF)

function(LuiStandardModule Name)
    add_library(Lui${Name} MODULE ${ARGN})
    target_include_directories(Lui${Name} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../abi")
    target_compile_features(Lui${Name} PRIVATE cxx_std_17)
    target_compile_definitions(Lui${Name} PRIVATE LUI_EXTENSION_BUILD)
    set_target_properties(Lui${Name} PROPERTIES CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/StandardProviders/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/StandardProviders/$<CONFIG>")
endfunction()

if(LUI_STANDARD_COMPRESSION)
    include(FetchContent)
    set(ZLIB_BUILD_SHARED OFF)
    set(ZLIB_BUILD_STATIC ON)
    set(ZLIB_BUILD_TESTING OFF)
    set(ZLIB_INSTALL OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    FetchContent_Declare(LuiStandardZlib URL https://zlib.net/fossils/zlib-1.4.tar.gz
        URL_HASH SHA256=02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(LuiStandardZlib)
    set(ZSTD_BUILD_PROGRAMS OFF)
    set(ZSTD_BUILD_TESTS OFF)
    set(ZSTD_BUILD_CONTRIB OFF)
    set(ZSTD_BUILD_SHARED OFF)
    set(ZSTD_BUILD_STATIC ON)
    set(ZSTD_MULTITHREAD_SUPPORT OFF)
    FetchContent_Declare(LuiStandardZstd URL https://github.com/meta/zstd/releases/download/v1.6.0/zstd-1.6.0.tar.gz
        URL_HASH SHA256=2ba599ae2a257976278e7e71e998433a387bfd18aa7b78678229fd5d84f5e9b8
        SOURCE_SUBDIR build/cmake DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(LuiStandardZstd)
    find_package(Threads REQUIRED)
    LuiStandardModule(Compression ByteWorker.cpp Compression.cpp)
    target_link_libraries(LuiCompression PRIVATE zlibstatic libzstd_static Threads::Threads)
endif()
if(LUI_STANDARD_CRYPTO)
    LuiStandardModule(Crypto Crypto.cpp CryptoAlgorithms.cpp)
    if(WIN32)
        target_link_libraries(LuiCrypto PRIVATE bcrypt)
    else()
        find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
        target_link_libraries(LuiCrypto PRIVATE OpenSSL::Crypto)
    endif()
endif()
if(LUI_STANDARD_SQLITE)
    include(FetchContent)
    FetchContent_Declare(LuiStandardSqlite URL https://www3.sqlite.org/2026/sqlite-amalgamation-3540100.zip
        URL_HASH SHA256=0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(LuiStandardSqlite)
    find_package(Threads REQUIRED)
    LuiStandardModule(SQLite ByteWorker.cpp SQLite.cpp LiveStore.cpp "${luistandardsqlite_SOURCE_DIR}/sqlite3.c")
    target_include_directories(LuiSQLite PRIVATE "${luistandardsqlite_SOURCE_DIR}")
    target_compile_definitions(LuiSQLite PRIVATE LUI_LIVE_SQLITE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_TEMP_STORE=3
        SQLITE_OMIT_SHARED_CACHE SQLITE_DQS=0 SQLITE_THREADSAFE=1
        SQLITE_MAX_SQL_LENGTH=8192 SQLITE_MAX_EXPR_DEPTH=32 SQLITE_MAX_COLUMN=32
        SQLITE_MAX_COMPOUND_SELECT=8 SQLITE_MAX_VARIABLE_NUMBER=32 SQLITE_MAX_WORKER_THREADS=0
        SQLITE_MAX_PAGE_COUNT=32768 SQLITE_MAX_LENGTH=262144 SQLITE_MAX_MEMORY=16777216)
    set_target_properties(LuiSQLite PROPERTIES C_VISIBILITY_PRESET hidden)
    target_link_libraries(LuiSQLite PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()
if(LUI_STANDARD_REGEX)
    include(FetchContent)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_SHARED_LIBS OFF)
    set(PCRE2_BUILD_TESTS OFF)
    set(PCRE2_BUILD_PCRE2GREP OFF)
    set(PCRE2_SUPPORT_JIT OFF)
    set(PCRE2_STATIC_PIC ON)
    FetchContent_Declare(LuiStandardPcre URL https://codeload.github.com/PCRE2Project/pcre2/tar.gz/refs/tags/pcre2-10.49
        URL_HASH SHA256=69d04dcddcf9e038aeae8f58b0c07ee5e08abb835ee8daa2bd6acee433dc112c
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(LuiStandardPcre)
    LuiStandardModule(Regex TextProvider.cpp Regex.cpp)
    target_link_libraries(LuiRegex PRIVATE pcre2-8-static)
    target_compile_definitions(LuiRegex PRIVATE PCRE2_STATIC)
endif()
if(LUI_STANDARD_TOML)
    include(FetchContent)
    FetchContent_Declare(LuiStandardToml URL https://codeload.github.com/marzer/tomlplusplus/tar.gz/refs/tags/v3.4.0
        URL_HASH SHA256=8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155
        SOURCE_SUBDIR lui-header-only DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(LuiStandardToml)
    LuiStandardModule(Toml TextProvider.cpp Toml.cpp)
    # Bound implicit parent creation during parsing, including dotted table headers.
    file(READ "${luistandardtoml_SOURCE_DIR}/toml.hpp" TomlHeader)
    foreach(Anchor "parse_key_buffer key_buffer;" "key_buffer.push_back(key_segment, key_begin, key_end);")
        string(FIND "${TomlHeader}" "${Anchor}" Position)
        if(Position EQUAL -1)
            message(FATAL_ERROR "toml++ parser differs from the qualified source")
        endif()
    endforeach()
    string(REPLACE "parse_key_buffer key_buffer;" "parse_key_buffer key_buffer; size_t LuiDottedSegments = 0;" TomlHeader "${TomlHeader}")
    string(REPLACE "key_buffer.push_back(key_segment, key_begin, key_end);"
        "if (!key_buffer.empty() && (++LuiDottedSegments > 64 || key_buffer.size() >= 16)) { set_error_and_return_default(\"key nesting limit\"sv); } key_buffer.push_back(key_segment, key_begin, key_end);"
        TomlHeader "${TomlHeader}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/LuiTomlInclude/toml.hpp" "${TomlHeader}")
    target_include_directories(LuiToml PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/LuiTomlInclude")
    target_compile_definitions(LuiToml PRIVATE TOML_MAX_NESTED_VALUES=16)
endif()
