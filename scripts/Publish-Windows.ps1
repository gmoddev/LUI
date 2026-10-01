param(
    [Parameter(Mandatory = $true)][string]$ManifestPath,
    [string]$OutputDirectory = '',
    [string]$LuauSourceDir = '',
    [string]$BuildDirectory = '',
    [string]$ExtensionDirectory = ''
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Build = if ($BuildDirectory) { $BuildDirectory } else { Join-Path $Root 'build/windows-x64' }
$Output = if ($OutputDirectory) { $OutputDirectory } else { Join-Path $Root 'build/package/win-x64' }
$PublishedHost = Join-Path $Build ("published-host-$([Guid]::NewGuid().ToString('N'))")
& (Join-Path $Root 'scripts/Build-Windows.ps1') -Configuration Release -BuildDirectory $Build -LuauSourceDir $LuauSourceDir
if ($LASTEXITCODE -ne 0) { throw '[LUI:Package] Release build failed' }
$NativeDll = Join-Path $Build 'Release/LuiRuntime.dll'
& dotnet publish (Join-Path $Root 'backends/winui3/Lui.WinUI.csproj') `
    --configuration Release --runtime win-x64 --self-contained true `
    -p:Platform=x64 -p:LuiProductionPackage=true -p:WindowsAppSDKSelfContained=true `
    -p:PublishTrimmed=false -p:PublishSingleFile=false -p:PublishReadyToRun=false `
    "-p:LuiNativeDll=$NativeDll" --output $PublishedHost
if ($LASTEXITCODE -ne 0) { throw '[LUI:Package] Windows host publish failed' }
$Extensions = if ($ExtensionDirectory) { $ExtensionDirectory } else { Join-Path $Build 'Release' }
& dotnet run --project (Join-Path $Root 'tools/packaging/Packaging.csproj') `
    --configuration Release -- $ManifestPath $PublishedHost $Output $Extensions
if ($LASTEXITCODE -ne 0) { throw '[LUI:Package] Package staging failed' }
Write-Host "[LUI:Package] Run $Output\Lui.WinUI.exe --manifest $(Split-Path $ManifestPath -Leaf)"
