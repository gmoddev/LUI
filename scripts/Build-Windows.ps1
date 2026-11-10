param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$LuauSourceDir = '',
    [string]$BuildDirectory = '',
    [switch]$NativeServiceProof,
    [ValidateRange(1, 16)]
    [int]$BuildJobs = 2
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Build = if ($BuildDirectory) { $BuildDirectory } else { Join-Path $Root 'build/windows-x64' }
$ToolchainOptions = @('--node')
if ($NativeServiceProof) { $ToolchainOptions += '--rust' }
& python (Join-Path $Root 'scripts/Check-Toolchains.py') @ToolchainOptions
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Toolchain qualification failed' }
& (Join-Path $Root 'scripts/Generate-WinUIFrameProjection.ps1') -BuildDirectory $Build
$FrameProjection=Get-Content -Raw (Join-Path $Build 'WinUIFrameProjection.json') | ConvertFrom-Json
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

$Options = @('-S', $Root, '-B', $Build, '-G', $Generator, '-A', 'x64', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0', '-DLUI_BUILD_WINUI_PRESENTER=ON', "-DLUI_WINUI_PROJECTION_DIR=$($FrameProjection.Projection)", "-DLUI_WINUI_INCLUDE_DIR=$($FrameProjection.Include)")
if ($LuauSourceDir) {
    $PinnedRevision = (Get-Content (Join-Path $Root 'cmake/Dependencies.json') -Raw | ConvertFrom-Json).LuauRevision
    $ActualRevision = (& git -C $LuauSourceDir rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $ActualRevision -ne $PinnedRevision) {
        throw '[LUI:Build] LuauSourceDir must contain the pinned Luau revision'
    }
    $Options += "-DLUAU_SOURCE_DIR=$LuauSourceDir"
}
$Options += if ($NativeServiceProof) { '-DLUI_BUILD_RUST_PROOF=ON' } else { '-DLUI_BUILD_RUST_PROOF=OFF' }

& $Cmake @Options
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] CMake configure failed' }
& $Cmake --build $Build --config $Configuration --target LuiNativeQualification --parallel $BuildJobs
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Native build failed' }
& (Join-Path (Split-Path $Cmake) 'ctest.exe') --test-dir $Build -C $Configuration --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Native qualification failed' }
& (Join-Path $Root 'scripts/Generate-Types.ps1') -SchemaDump (Join-Path $Build "$Configuration/LuiSchemaDump.exe") -Root $Root
& (Join-Path $Build "$Configuration/LuiTypeTests.exe")
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Generated type validation failed' }
& dotnet run --project (Join-Path $Root 'tests/manifest/ManifestTests.csproj') --configuration $Configuration -- $Root
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Application manifest validation failed' }
& dotnet run --project (Join-Path $Root 'tests/packaging/PackagingTests.csproj') --configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Package staging tests failed' }
& dotnet run --project (Join-Path $Root 'tests/diagnostics/DiagnosticsTests.csproj') --configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Policy disk diagnostics tests failed' }
$Dll = Join-Path $Build "$Configuration/LuiRuntime.dll"
$TypeChecker = Join-Path $Build "$Configuration/LuiTypeCheck.exe"
& dotnet run --project (Join-Path $Root 'tests/cli/CliTests.csproj') --configuration $Configuration -- $Dll $TypeChecker
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] CLI tests failed' }
& dotnet build (Join-Path $Root 'tools/cli/Lui.Cli.csproj') --configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] CLI build failed' }
& dotnet (Join-Path $Root "tools/cli/bin/$Configuration/net10.0/lui.dll") check (Join-Path $Root 'examples/dashboard/dashboard.manifest.json') --checker $TypeChecker
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Dashboard type check failed' }
& dotnet (Join-Path $Root "tools/cli/bin/$Configuration/net10.0/lui.dll") check (Join-Path $Root 'examples/native-integration/lui.json') --checker $TypeChecker --mode project --diagnostics development
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Native integration type check failed' }
& dotnet build (Join-Path $Root 'tools/preview-host/Lui.PreviewHost.csproj') --configuration $Configuration
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Preview host build failed' }
$PreviewHost = Join-Path $Root "tools/preview-host/bin/$Configuration/net10.0/lui-preview-host.dll"
& dotnet run --project (Join-Path $Root 'tests/preview/PreviewTests.csproj') --configuration $Configuration -- $PreviewHost $Dll
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] Preview protocol tests failed' }
$env:LUI_TEST_PREVIEW_HOST = $PreviewHost
$env:LUI_TEST_RUNTIME = $Dll
$env:LUI_TEST_CLI = Join-Path $Root "tools/cli/bin/$Configuration/net10.0/lui.dll"
$env:LUI_TEST_TYPECHECK = $TypeChecker
& node --test (Join-Path $Root 'tests/editor/EditorTests.js') (Join-Path $Root 'tests/editor/TypeTests.js') (Join-Path $Root 'tests/editor/ProjectTests.js') (Join-Path $Root 'tests/editor/SdkTests.js')
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] VS Code preview client tests failed' }

& dotnet build (Join-Path $Root 'backends/winui3/Lui.WinUI.csproj') --configuration $Configuration -p:Platform=x64 "-p:LuiNativeDll=$Dll"
if ($LASTEXITCODE -ne 0) { throw '[LUI:Build] WinUI build failed' }
Write-Host '[LUI:Build] Native, WinUI, CLI, preview, VS Code client, runtime, manifest, packaging, and type tests passed'
