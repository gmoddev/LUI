param()
$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot
# Synthetic one-pixel PNG test fixture, captured as an owned declared asset.
$Pixel = [Convert]::FromBase64String('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=')
[IO.File]::WriteAllBytes((Join-Path $Root 'Avatar.png'), $Pixel)
$Names = @('Scope.luau','State.luau','VirtualList.luau')
$Pins = @($Names | ForEach-Object {
    [ordered]@{ Path = $_; Sha256 = (Get-FileHash -LiteralPath (Join-Path $Root "Vendor/LuiUI/$_") -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$Lock = [ordered]@{ SchemaVersion = 1; Packages = @([ordered]@{Alias='LuiUI'; Provenance=[ordered]@{Kind='LocalVendor';Id='LuiUI-0.1.0'}; Files=$Pins}) }
[IO.File]::WriteAllText((Join-Path $Root 'Modules.lock.json'), ($Lock | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
Write-Host '[LUI:Conversation] Synthetic asset and module lock prepared'
