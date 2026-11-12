param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [Parameter(Mandatory = $true)][string]$CppRuntimeDirectory,
    [string]$OutputDirectory = '',
    [string]$LuauSourceDir = '',
    [string]$BuildDirectory = '',
    [string]$ExtensionDirectory = '',
    [ValidateSet('entry','project')][string]$Mode = 'entry'
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Build = if ($BuildDirectory) { $BuildDirectory } else { Join-Path $Root 'build/windows-x64' }
$Output = if ($OutputDirectory) { $OutputDirectory } else { Join-Path $Root 'build/package/win-x64' }
$PublishedHost = Join-Path $Build ("published-host-$([Guid]::NewGuid().ToString('N'))")
$ManagedBuild = $PublishedHost + '.ManagedBuild'
& python (Join-Path $Root 'scripts/Stage-WindowsCppRuntime.py') --runtime $CppRuntimeDirectory --check
if ($LASTEXITCODE -ne 0) { throw '[LUI:Package] Pinned C++ runtime required' }
& (Join-Path $Root 'scripts/Build-Windows.ps1') -Configuration Release -BuildDirectory $Build -LuauSourceDir $LuauSourceDir
if ($LASTEXITCODE -ne 0) { throw '[LUI:Package] Release build failed' }
$NativeDll = Join-Path $Build 'Release/LuiRuntime.dll'
& dotnet publish (Join-Path $Root 'backends/winui3/Lui.WinUI.csproj') `
    --configuration Release --runtime win-x64 --self-contained true --artifacts-path $ManagedBuild `
    -p:Platform=x64 -p:LuiProductionPackage=true -p:WindowsAppSDKSelfContained=true `
    -p:PublishTrimmed=false -p:PublishSingleFile=false -p:PublishReadyToRun=false `
    "-p:LuiNativeDll=$NativeDll" --output $PublishedHost
if ($LASTEXITCODE -ne 0) { throw '[LUI:Package] Windows host publish failed' }
& python (Join-Path $Root 'scripts/Stage-WindowsCppRuntime.py') --runtime $CppRuntimeDirectory --destination $PublishedHost
if ($LASTEXITCODE -ne 0) { throw '[LUI:Package] C++ runtime staging failed' }
& python (Join-Path $Root 'scripts/Stage-WindowsNotices.py') --native-build $Build --host $PublishedHost
if ($LASTEXITCODE -ne 0) { throw '[LUI:Package] Dependency notice staging failed' }
$Extensions = if ($ExtensionDirectory) { $ExtensionDirectory } else { Join-Path $Build 'Release' }
if($Mode -eq 'project') {
    & dotnet run --project (Join-Path $Root 'tools/cli/Lui.Cli.csproj') --configuration Release -- build $ManifestPath --mode project --host $PublishedHost --output $Output --project-tool (Join-Path $Build 'Release/LuiProjectTool.exe')
    if($LASTEXITCODE -ne 0) {throw '[LUI:Package] Modular package staging failed'}
    Write-Host '[LUI:Package] Run Start-LUI.cmd in the output folder'
    return
}
& dotnet run --project (Join-Path $Root 'tools/packaging/Packaging.csproj') `
    --configuration Release -- $ManifestPath $PublishedHost $Output $Extensions
if ($LASTEXITCODE -ne 0) { throw '[LUI:Package] Package staging failed' }
Write-Host "[LUI:Package] Run $Output\Lui.WinUI.exe --manifest $(Split-Path $ManifestPath -Leaf)"
