param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$BuildDirectory = '',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Build = if ($BuildDirectory) { $BuildDirectory } else { Join-Path $Root 'build/windows-x64' }
$Output = if ($OutputDirectory) { $OutputDirectory } else { Join-Path $Root 'build/native-example/win-x64' }
$HostOutput = Join-Path $Root "backends/winui3/bin/x64/$Configuration/net9.0-windows10.0.19041.0/win-x64"
$Extension = Join-Path $Build "$Configuration/LuiSampleExtension.dll"
if (-not (Test-Path (Join-Path $HostOutput 'Lui.WinUI.exe')) -or -not (Test-Path $Extension)) {
    throw '[LUI:Stage] Build the WinUI host and sample extension before staging'
}
New-Item -ItemType Directory -Force $Output | Out-Null
Copy-Item (Join-Path $HostOutput '*') $Output -Recurse -Force
Copy-Item $Extension (Join-Path $Output 'LuiSampleExtension.dll') -Force
Copy-Item (Join-Path $Root 'examples/native-service.luau') (Join-Path $Output 'native-service.luau') -Force
Copy-Item (Join-Path $Root 'examples/native-service.manifest.json') (Join-Path $Output 'native-service.manifest.json') -Force
Write-Host "[LUI:Stage] Native service example staged at $Output"
Write-Host "[LUI:Stage] Run Lui.WinUI.exe --manifest native-service.manifest.json from that directory"
