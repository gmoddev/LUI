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
& $Cmake --build $Build --config $Configuration --target LuiHeadlessTests LuiLayoutTests LuiConformanceTests LuiSchemaDump LuiTypeTests LuiExtensionTests LuiHostTests LuiSandboxTests LuiThemeTests LuiPlatformTests LuiAssetTests LuiSampleExtension LuiFailedExtension LuiBadAbiExtension LuiExtraCapabilityExtension LuiLegacyExtension --parallel 8
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Native build failed' }
& (Join-Path $Build "$Configuration/LuiHeadlessTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Headless tests failed' }
& (Join-Path $Build "$Configuration/LuiLayoutTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Layout tests failed' }
& (Join-Path $Build "$Configuration/LuiConformanceTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Conformance tests failed' }
& (Join-Path $Build "$Configuration/LuiExtensionTests.exe") `
    (Join-Path $Build "$Configuration/LuiSampleExtension.dll") `
    (Join-Path $Build "$Configuration/LuiFailedExtension.dll") `
    (Join-Path $Build "$Configuration/LuiBadAbiExtension.dll") `
    (Join-Path $Build "$Configuration/LuiExtraCapabilityExtension.dll") `
    (Join-Path $Build "$Configuration/LuiLegacyExtension.dll")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Extension ABI tests failed' }
& (Join-Path $Build "$Configuration/LuiHostTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] C++ host tests failed' }
& (Join-Path $Build "$Configuration/LuiSandboxTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Sandbox tests failed' }
& (Join-Path $Build "$Configuration/LuiThemeTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Theme tests failed' }
& (Join-Path $Build "$Configuration/LuiPlatformTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Platform service tests failed' }
& (Join-Path $Build "$Configuration/LuiAssetTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Asset tests failed' }
& (Join-Path $Root 'scripts/Generate-Types.ps1') -SchemaDump (Join-Path $Build "$Configuration/LuiSchemaDump.exe") -Root $Root
& (Join-Path $Build "$Configuration/LuiTypeTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Generated type validation failed' }
& dotnet run --project (Join-Path $Root 'tests/manifest/ManifestTests.csproj') --configuration $Configuration -- $Root
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Application manifest validation failed' }
& dotnet run --project (Join-Path $Root 'tests/packaging/PackagingTests.csproj') --configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Package staging tests failed' }
$Dll = Join-Path $Build "$Configuration/LuiRuntime.dll"
& dotnet run --project (Join-Path $Root 'tests/cli/CliTests.csproj') --configuration $Configuration -- $Dll
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] CLI tests failed' }
& dotnet build (Join-Path $Root 'tools/cli/Lui.Cli.csproj') --configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] CLI build failed' }
& dotnet build (Join-Path $Root 'tools/preview-host/Lui.PreviewHost.csproj') --configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Preview host build failed' }
$PreviewHost = Join-Path $Root "tools/preview-host/bin/$Configuration/net9.0/lui-preview-host.dll"
& dotnet run --project (Join-Path $Root 'tests/preview/PreviewTests.csproj') --configuration $Configuration -- $PreviewHost $Dll
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Preview protocol tests failed' }

& dotnet build (Join-Path $Root 'backends/winui3/Lui.WinUI.csproj') --configuration $Configuration -p:Platform=x64 "-p:LuiNativeDll=$Dll"
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] WinUI build failed' }
Write-Host '[LUI:Build] Native, WinUI, CLI, preview, runtime, manifest, packaging, and type tests passed'
