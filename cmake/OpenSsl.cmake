# Windows uses a pinned static provider built inside the CMake tree. Linux uses
# its distribution-maintained OpenSSL 3 package and system certificate paths.
add_library(LuiOpenSsl INTERFACE)
if(WIN32)
    if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "[LUI:Tls] Bundled OpenSSL currently requires Windows MSVC x64")
    endif()
    find_program(LuiPerl NAMES perl REQUIRED)
    if(NOT CMAKE_GENERATOR_INSTANCE)
        message(FATAL_ERROR "[LUI:Tls] Bundled OpenSSL requires the Visual Studio CMake generator")
    endif()
    include(ExternalProject)
    set(LuiSslRoot "${CMAKE_BINARY_DIR}/_deps/lui-openssl")
    file(MAKE_DIRECTORY "${LuiSslRoot}/install/include")
    # OpenSSL's static C libraries use /Zl and do not bind a CRT. Consumers use
    # their own configured CRT. This cache is shared by Debug/Release consumers.
    file(GENERATE OUTPUT "${LuiSslRoot}/Configure.cmd" CONTENT "@echo off\r\ncall \"${CMAKE_GENERATOR_INSTANCE}/VC/Auxiliary/Build/vcvars64.bat\" >nul\r\nif errorlevel 1 exit /b 1\r\n\"${LuiPerl}\" Configure VC-WIN64A no-shared no-module no-asm no-tests no-docs no-comp no-legacy --prefix=\"${LuiSslRoot}/install\" --libdir=lib\r\nexit /b %errorlevel%\r\n")
    file(GENERATE OUTPUT "${LuiSslRoot}/Build.cmd" CONTENT "@echo off\r\ncall \"${CMAKE_GENERATOR_INSTANCE}/VC/Auxiliary/Build/vcvars64.bat\" >nul\r\nif errorlevel 1 exit /b 1\r\nnmake /nologo build_libs\r\nexit /b %errorlevel%\r\n")
    file(GENERATE OUTPUT "${LuiSslRoot}/Install.cmd" CONTENT "@echo off\r\ncall \"${CMAKE_GENERATOR_INSTANCE}/VC/Auxiliary/Build/vcvars64.bat\" >nul\r\nif errorlevel 1 exit /b 1\r\nnmake /nologo install_dev\r\nexit /b %errorlevel%\r\n")
    ExternalProject_Add(LuiOpenSslBuild
        URL https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz
        URL_HASH SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PREFIX "${LuiSslRoot}"
        BUILD_IN_SOURCE TRUE
        CONFIGURE_COMMAND cmd /c "${LuiSslRoot}/Configure.cmd"
        BUILD_COMMAND cmd /c "${LuiSslRoot}/Build.cmd"
        INSTALL_COMMAND cmd /c "${LuiSslRoot}/Install.cmd"
        BUILD_BYPRODUCTS "${LuiSslRoot}/install/lib/libssl.lib" "${LuiSslRoot}/install/lib/libcrypto.lib"
        LOG_CONFIGURE TRUE LOG_BUILD TRUE LOG_INSTALL TRUE)
    ExternalProject_Add_StepDependencies(LuiOpenSslBuild configure "${LuiSslRoot}/Configure.cmd")
    ExternalProject_Add_StepDependencies(LuiOpenSslBuild build "${LuiSslRoot}/Build.cmd")
    ExternalProject_Add_StepDependencies(LuiOpenSslBuild install "${LuiSslRoot}/Install.cmd")
    target_include_directories(LuiOpenSsl INTERFACE "${LuiSslRoot}/install/include")
    target_link_libraries(LuiOpenSsl INTERFACE
        "${LuiSslRoot}/install/lib/libssl.lib" "${LuiSslRoot}/install/lib/libcrypto.lib"
        ws2_32 crypt32 bcrypt advapi32 user32 gdi32)
    add_dependencies(LuiOpenSsl LuiOpenSslBuild)
else()
    find_package(OpenSSL 3.0 REQUIRED)
    target_link_libraries(LuiOpenSsl INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()
