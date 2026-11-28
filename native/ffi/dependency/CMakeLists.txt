cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(LuiFfiDependency LANGUAGES C)
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|X86_64)$")
    message(FATAL_ERROR "The qualified libffi build requires x86-64")
endif()
set(LUI_FFI_ARCHIVE "" CACHE FILEPATH "Offline copy of the pinned upstream libffi release tarball")
set(LuiFfiUrl "https://github.com/libffi/libffi/releases/download/v3.8.1/libffi-3.8.1.tar.gz")
if(LUI_FFI_ARCHIVE)
    set(LuiFfiUrl "${LUI_FFI_ARCHIVE}")
endif()
include(FetchContent)
FetchContent_Declare(LuiLibffiSource URL "${LuiFfiUrl}"
    URL_HASH SHA256=81e879c094cc5ec3186c48510c6385ac6b3e866192e8bc71a35245703757ee40
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(LuiLibffiSource)
set(Source "${luilibffisource_SOURCE_DIR}")

if(MSVC AND WIN32)
    enable_language(ASM_MASM)
    set(Headers "${CMAKE_CURRENT_BINARY_DIR}/include")
    file(MAKE_DIRECTORY "${Headers}")
    set(VERSION "3.8.1")
    set(FFI_VERSION_STRING "${VERSION}")
    set(FFI_VERSION_NUMBER 30801)
    set(TARGET X86_WIN64)
    set(HAVE_LONG_DOUBLE 0)
    set(FFI_EXEC_TRAMPOLINE_TABLE 0)
    configure_file("${Source}/include/ffi.h.in" "${Headers}/ffi.h" @ONLY)
    configure_file("${Source}/src/x86/ffitarget.h" "${Headers}/ffitarget.h" COPYONLY)
    configure_file(MsvcConfig.h "${Headers}/fficonfig.h" COPYONLY)
    set(Asm "${CMAKE_CURRENT_BINARY_DIR}/win64_intel.asm")
    set(AsmObject "${CMAKE_CURRENT_BINARY_DIR}/win64_intel.obj")
    add_custom_command(OUTPUT "${Asm}"
        COMMAND "${CMAKE_C_COMPILER}" /nologo /TC /P /EP "/Fi${Asm}"
            "/I${Headers}" "/I${Source}/include" "${Source}/src/x86/win64_intel.S"
        DEPENDS "${Source}/src/x86/win64_intel.S" "${Source}/include/ffi_cfi.h"
                "${Source}/src/x86/asmnames.h" "${Headers}/ffi.h" "${Headers}/ffitarget.h" "${Headers}/fficonfig.h"
        VERBATIM)
    add_custom_command(OUTPUT "${AsmObject}" DEPENDS "${Asm}"
        COMMAND "${CMAKE_ASM_MASM_COMPILER}" /nologo /errorReport:none /c "/Fo${AsmObject}" "${Asm}" VERBATIM)
    set_source_files_properties("${AsmObject}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
    add_library(LuiLibffi STATIC "${AsmObject}" "${Source}/src/prep_cif.c" "${Source}/src/types.c"
        "${Source}/src/raw_api.c" "${Source}/src/java_raw_api.c" "${Source}/src/closures.c"
        "${Source}/src/tramp.c" "${Source}/src/x86/ffiw64.c")
    target_include_directories(LuiLibffi PUBLIC "${Headers}" PRIVATE "${Source}/include")
    target_compile_definitions(LuiLibffi PUBLIC FFI_STATIC_BUILD)
    target_compile_features(LuiLibffi PRIVATE c_std_11)
    set_target_properties(LuiLibffi PROPERTIES OUTPUT_NAME ffi)
    add_custom_target(LuiFfiDependencyBuild DEPENDS LuiLibffi)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
    include(ExternalProject)
    find_program(LuiFfiMake NAMES gmake make REQUIRED)
    set(Prefix "${CMAKE_CURRENT_BINARY_DIR}/install")
    file(MAKE_DIRECTORY "${Prefix}/include")
    ExternalProject_Add(LuiFfiDependencyBuild SOURCE_DIR "${Source}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/autotools" INSTALL_DIR "${Prefix}"
        DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env "CC=${CMAKE_C_COMPILER}" "CFLAGS=-O2 -fPIC -fvisibility=hidden"
            "${Source}/configure" "--prefix=<INSTALL_DIR>" "--libdir=<INSTALL_DIR>/lib"
            --enable-static --disable-shared --with-pic --disable-docs --disable-multi-os-directory
        BUILD_COMMAND "${LuiFfiMake}" -j2
        INSTALL_COMMAND "${LuiFfiMake}" install
        BUILD_BYPRODUCTS "${Prefix}/lib/libffi.a")
    add_library(LuiLibffi STATIC IMPORTED GLOBAL)
    set_target_properties(LuiLibffi PROPERTIES IMPORTED_LOCATION "${Prefix}/lib/libffi.a"
        INTERFACE_INCLUDE_DIRECTORIES "${Prefix}/include")
    add_dependencies(LuiLibffi LuiFfiDependencyBuild)
else()
    message(FATAL_ERROR "Select native MSVC Windows x64 or native Linux x64")
endif()
