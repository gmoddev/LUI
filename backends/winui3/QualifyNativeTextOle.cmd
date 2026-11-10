@echo off
setlocal
where cl >nul 2>&1
if errorlevel 1 (
  echo [LUI:NativeText] Run from an x64 Visual Studio Developer Command Prompt.
  exit /b 2
)
if "%~1"=="" (
  echo [LUI:NativeText] Usage: QualifyNativeTextOle.cmd OUTPUT_DIRECTORY
  exit /b 2
)
if not exist "%~f1" mkdir "%~f1"
if errorlevel 1 exit /b 2
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MD "%~dp0NativeTextOleQualification.cpp" /Fo:"%~f1\NativeTextOleQualification.obj" /Fe:"%~f1\NativeTextOleQualification.exe" /link user32.lib gdi32.lib comctl32.lib ole32.lib uuid.lib imm32.lib
if errorlevel 1 exit /b 1
"%~f1\NativeTextOleQualification.exe"
exit /b %errorlevel%
