param(
    [Parameter(Mandatory = $true)]
    [string]$SchemaDump,
    [Parameter(Mandatory = $true)]
    [string]$Root
)

$ErrorActionPreference = 'Stop'
$Json = (& $SchemaDump) -join "`n"
if ($LASTEXITCODE -ne 0) { throw '[LUI:Reflection] Schema dump failed' }
$Schema = $Json | ConvertFrom-Json
if ($Schema.schemaVersion -ne 1) { throw '[LUI:Reflection] Unsupported schema version' }

$Types = [System.Collections.Generic.List[string]]::new()
$Types.Add('-- Generated from LuiRuntime reflection metadata. Do not edit by hand.')
$Types.Add('export type Connection = { Disconnect: (Self: Connection) -> () }')
$Types.Add('export type Signal = { Connect: (Self: Signal, Callback: (...any) -> ()) -> Connection }')
$Types.Add('export type Vector2 = { X: number, Y: number }')
$Types.Add('export type UDim = { Scale: number, Offset: number }')
$Types.Add('export type UDim2 = { X: UDim, Y: UDim }')
$Types.Add('')

$MethodTypes = @{
    Destroy = '(Self: Instance) -> ()'
    Clone = '(Self: Instance) -> Instance'
    GetChildren = '(Self: Instance) -> {Instance}'
    GetDescendants = '(Self: Instance) -> {Instance}'
    FindFirstChild = '(Self: Instance, Name: string) -> Instance?'
    IsA = '(Self: Instance, ClassName: string) -> boolean'
}

foreach ($Class in $Schema.classes) {
    $Inheritance = if ($Class.base) { "$($Class.base) & " } else { '' }
    $Types.Add("export type $($Class.name) = $Inheritance{" )
    foreach ($Property in $Class.properties) {
        $Suffix = if ($Property.readOnly) { ' -- read-only at runtime' } else { '' }
        $Types.Add("    $($Property.name): $($Property.type),$Suffix")
    }
    foreach ($Method in $Class.methods) {
        if (-not $MethodTypes.ContainsKey($Method)) { throw "[LUI:Reflection] Unknown method: $Method" }
        $Types.Add("    ${Method}: $($MethodTypes[$Method]),")
    }
    foreach ($Signal in $Class.signals) { $Types.Add("    ${Signal}: Signal,") }
    $Types.Add('}')
    $Types.Add('')
}

$ServiceMethodTypes = @{
    GetWindows = '(Self: WindowService) -> {Window}'
    Supports = '(Self: PlatformService, Capability: string) -> boolean'
}
foreach ($Service in $Schema.services) {
    $Types.Add("export type $($Service.name) = {")
    if ($Service.name -eq 'PlatformService') { $Types.Add('    BackendName: string, -- read-only at runtime') }
    foreach ($Method in $Service.methods) {
        if (-not $ServiceMethodTypes.ContainsKey($Method)) { throw "[LUI:Reflection] Unknown service method: $Method" }
        $Types.Add("    ${Method}: $($ServiceMethodTypes[$Method]),")
    }
    $Types.Add('}')
    $Types.Add('')
}
$Types.Add('export type App = { GetService: (Self: App, Name: string) -> any }')

$Docs = [System.Collections.Generic.List[string]]::new()
$Docs.Add('# LUI API: implemented classes')
$Docs.Add('')
$Docs.Add('Generated from the runtime reflection table. This lists the implemented surface, not the full planned specification.')
$Docs.Add('')
foreach ($Class in $Schema.classes) {
    $Docs.Add("## $($Class.name)")
    $Docs.Add('')
    $BaseLabel = if ($Class.base) { $Class.base } else { 'none' }
    $Docs.Add("Base: ``$BaseLabel``. Creatable: ``$($Class.creatable.ToString().ToLowerInvariant())``.")
    $Docs.Add('')
    $Docs.Add('| Property | Type | Default | Read only |')
    $Docs.Add('| --- | --- | --- | --- |')
    foreach ($Property in $Class.properties) {
        $ReadOnly = if ($Property.readOnly) { 'Yes' } else { 'No' }
        $Docs.Add("| ``$($Property.name)`` | ``$($Property.type)`` | $($Property.default) | $ReadOnly |")
    }
    $Docs.Add('')
    if ($Class.methods.Count) { $Docs.Add("Methods: $($Class.methods -join ', ').") ; $Docs.Add('') }
    if ($Class.signals.Count) { $Docs.Add("Signals: $($Class.signals -join ', ').") ; $Docs.Add('') }
}
$Docs.Add('## Services')
$Docs.Add('')
foreach ($Service in $Schema.services) {
    $Docs.Add("- ``$($Service.name)``: $($Service.methods -join ', ')")
}

$Encoding = [System.Text.UTF8Encoding]::new($false)
$TypePath = Join-Path $Root 'types/LUI.d.luau'
$SchemaPath = Join-Path $Root 'types/schema.json'
$DocPath = Join-Path $Root 'docs/API.md'
New-Item -ItemType Directory -Path (Split-Path $TypePath), (Split-Path $DocPath) -Force | Out-Null
[System.IO.File]::WriteAllText($TypePath, ($Types -join "`n") + "`n", $Encoding)
[System.IO.File]::WriteAllText($SchemaPath, $Json + "`n", $Encoding)
[System.IO.File]::WriteAllText($DocPath, ($Docs -join "`n") + "`n", $Encoding)
Write-Host '[LUI:Reflection] Types and API docs generated from runtime schema'
