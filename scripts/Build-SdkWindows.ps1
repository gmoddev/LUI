param(
    [Parameter(Mandatory=$true)][string]$NativeDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$CppRuntimeDirectory,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9.-]+)?$')][string]$SdkVersion
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Native = (Resolve-Path -LiteralPath $NativeDirectory).Path
$CppRuntime = (Resolve-Path -LiteralPath $CppRuntimeDirectory).Path
$Output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $Output) { throw '[LUI:SDK] Output exists; choose a new directory' }
$ManagedBuild = Join-Path (Split-Path -Parent $Output) ((Split-Path -Leaf $Output) + '.ManagedBuild')
if (Test-Path -LiteralPath $ManagedBuild) { throw '[LUI:SDK] Managed build output exists; choose a new directory' }
foreach ($Name in @('LuiRuntime.dll','LuiWinUIFrames.dll','LuiWinUIText.dll','LuiTypeCheck.exe','LuiProjectTool.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $Native $Name) -PathType Leaf)) { throw "[LUI:SDK] Native component missing: $Name" }
}
& python (Join-Path $Root 'scripts/Stage-WindowsCppRuntime.py') --runtime $CppRuntime --check
if ($LASTEXITCODE -ne 0) { throw '[LUI:SDK] Pinned C++ runtime required' }
& python (Join-Path $Root 'scripts/Check-Toolchains.py')
if ($LASTEXITCODE -ne 0) { throw '[LUI:SDK] Build toolchain mismatch' }
New-Item -ItemType Directory -Path $Output | Out-Null
New-Item -ItemType Directory -Path $ManagedBuild | Out-Null
foreach ($Part in @(
    @{ Project='tools/cli/Lui.Cli.csproj'; Directory='Cli' },
    @{ Project='tools/preview-host/Lui.PreviewHost.csproj'; Directory='Preview' },
    @{ Project='backends/winui3/Lui.WinUI.csproj'; Directory='Native' }
)) {
    # Keep SDK resources separate from ordinary builds. A reused framework-dependent
    # WinUI PRI can omit the self-contained theme resources despite a successful publish.
    & dotnet publish (Join-Path $Root $Part.Project) --configuration Release --runtime win-x64 --self-contained true --artifacts-path $ManagedBuild `
        -p:Platform=x64 -p:LuiProductionPackage=true -p:WindowsAppSDKSelfContained=true `
        -p:PublishTrimmed=false -p:PublishSingleFile=false -p:PublishReadyToRun=false -p:DebugType=None -p:DebugSymbols=false `
        "-p:LuiNativeDll=$(Join-Path $Native 'LuiRuntime.dll')" --output (Join-Path $Output $Part.Directory)
    if ($LASTEXITCODE -ne 0) { throw '[LUI:SDK] Managed publish failed; preserve partial output for diagnosis' }
}
New-Item -ItemType Directory -Path (Join-Path $Output 'Tools') | Out-Null
Copy-Item -LiteralPath (Join-Path $Native 'LuiTypeCheck.exe'),(Join-Path $Native 'LuiProjectTool.exe') -Destination (Join-Path $Output 'Tools')
Copy-Item -LiteralPath (Join-Path $Root 'types/LUI.d.luau') -Destination (Join-Path $Output 'Tools/LUI.d.luau')
& python (Join-Path $Root 'scripts/Package-Sdk.py') --root $Root --sdk $Output --version $SdkVersion --native-build (Split-Path $Native) --cpp-runtime $CppRuntime
if ($LASTEXITCODE -ne 0) { throw '[LUI:SDK] SDK inventory/extension packaging failed' }
& (Join-Path $Output 'Cli/lui.exe') doctor --format json
if ($LASTEXITCODE -ne 0) { throw '[LUI:SDK] Published SDK verification failed' }
Write-Output '[LUI:SDK] Local preview prepared. Signing, downloaded-file OS launch and clean-machine qualification are separate release gates.'
