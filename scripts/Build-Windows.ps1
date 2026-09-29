param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$LuauSourceDir = '',
    [string]$BuildDirectory = ''
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Build = if ($BuildDirectory) { $BuildDirectory } else { Join-Path $Root 'build/windows-x64' }
$VsWhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $VsWhere)) { throw '[LUI:Build] Visual Studio with C++ tools is required' }
$Vs = & $VsWhere -latest -property installationPath
$Version = & $VsWhere -latest -property installationVersion
if (-not $Vs) { throw '[LUI:Build] Visual Studio installation was not found' }
$Cmake = Join-Path $Vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (-not (Test-Path $Cmake)) { throw '[LUI:Build] Visual Studio CMake component was not found' }
$Major = [int]($Version.Split('.')[0])
$Generator = switch ($Major) {
    17 { 'Visual Studio 17 2022' }
    18 { 'Visual Studio 18 2026' }
    default { throw "[LUI:Build] Unsupported Visual Studio major version: $Major" }
}

$Options = @('-S', $Root, '-B', $Build, '-G', $Generator, '-A', 'x64', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0')
if ($LuauSourceDir) {
    $PinnedRevision = 'c0e346edd89066b44dca174c9f54ce84c746a540'
    $ActualRevision = (& git -C $LuauSourceDir rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $ActualRevision -ne $PinnedRevision) {
        throw '[LUI:Build] LuauSourceDir must contain the pinned Luau revision'
    }
    $Options += "-DLUAU_SOURCE_DIR=$LuauSourceDir"
}

& $Cmake @Options
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] CMake configure failed' }
& $Cmake --build $Build --config $Configuration --target LuiHeadlessTests LuiLayoutTests LuiConformanceTests LuiSchemaDump --parallel 8
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Native build failed' }
& (Join-Path $Build "$Configuration/LuiHeadlessTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Headless tests failed' }
& (Join-Path $Build "$Configuration/LuiLayoutTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Layout tests failed' }
& (Join-Path $Build "$Configuration/LuiConformanceTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Conformance tests failed' }
& (Join-Path $Root 'scripts/Generate-Types.ps1') -SchemaDump (Join-Path $Build "$Configuration/LuiSchemaDump.exe") -Root $Root

$Dll = Join-Path $Build "$Configuration/LuiRuntime.dll"
& dotnet build (Join-Path $Root 'backends/winui3/Lui.WinUI.csproj') --configuration $Configuration -p:Platform=x64 "-p:LuiNativeDll=$Dll"
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] WinUI build failed' }
Write-Host '[LUI:Build] Native and WinUI builds, headless tests, and schema generation passed'
