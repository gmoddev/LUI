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
if ($Schema.schemaVersion -ne 2) { throw '[LUI:Reflection] Unsupported schema version' }

$Types = [System.Collections.Generic.List[string]]::new()
$Types.Add('-- Generated from LuiRuntime reflection metadata. Do not edit by hand.')
$Types.Add('export type Connection = { read Disconnect: (Self: Connection) -> () }')
$Types.Add('export type Signal = { read Connect: (Self: Signal, Callback: (...any) -> ()) -> Connection }')
$Types.Add('export type Vector2 = { read X: number, read Y: number }')
$Types.Add('export type UDim = { read Scale: number, read Offset: number }')
$Types.Add('export type UDim2 = { read X: UDim, read Y: UDim }')
$Types.Add('export type PointerInput = { read Device: "Mouse" | "Pen" | "Touch" | "Touchpad", read PointerId: number, read Position: Vector2, read IsCanceled: boolean }')
$Types.Add('export type KeyboardInput = { read Device: "Keyboard", read Key: string, read IsRepeat: boolean }')
$Types.Add('export type InputEvent = PointerInput | KeyboardInput')
$Types.Add('export type InputSignal = { read Connect: (Self: InputSignal, Callback: (Input: InputEvent) -> ()) -> Connection }')
$Types.Add('export type ThemeSignal = { read Connect: (Self: ThemeSignal, Callback: (Theme: string) -> ()) -> Connection }')
$Types.Add('')

$ClassMap = @{}
foreach ($Class in $Schema.classes) { $ClassMap[$Class.name] = $Class }

foreach ($Class in $Schema.classes) {
    $Inheritance = if ($Class.base) { "$($Class.base) & " } else { '' }
    $Types.Add("export type $($Class.name) = $Inheritance{" )
    foreach ($Property in $Class.properties) {
        $Access = if ($Property.readOnly) { 'read ' } else { '' }
        $Types.Add("    $Access$($Property.name): $($Property.type),")
    }
    foreach ($Method in $Class.methods) {
        $Types.Add("    read $($Method.name): $($Method.type),")
    }
    foreach ($Signal in $Class.signals) {
        $Types.Add("    read $($Signal.name): $($Signal.type),")
    }
    $Types.Add('}')
    $Types.Add('')
}

foreach ($Service in $Schema.services) {
    $Types.Add("export type $($Service.name) = {")
    foreach ($Property in $Service.properties) {
        $Types.Add("    read $($Property.name): $($Property.type),")
    }
    foreach ($Method in $Service.methods) {
        $Types.Add("    read $($Method.name): $($Method.type),")
    }
    foreach ($Signal in $Service.signals) {
        $Types.Add("    read $($Signal.name): $($Signal.type),")
    }
    $Types.Add('}')
    $Types.Add('')
}
foreach ($Object in $Schema.objects) {
    $Types.Add("export type $($Object.name) = {")
    foreach ($Property in $Object.properties) {
        $Access = if ($Object.name.EndsWith('Options')) { '' } else { 'read ' }
        $Types.Add("    $Access$($Property.name): $($Property.type),")
    }
    foreach ($Method in $Object.methods) {
        $Types.Add("    read $($Method.name): $($Method.type),")
    }
    foreach ($Signal in $Object.signals) {
        $Types.Add("    read $($Signal.name): $($Signal.type),")
    }
    $Types.Add('}')
    $Types.Add('')
}
$ServiceOverloads = @($Schema.services | ForEach-Object { "{ read GetService: (Self: App, Name: `"$($_.name)`") -> $($_.name) }" })
while ($ServiceOverloads.Count -gt 1) {
    $Pairs = [System.Collections.Generic.List[string]]::new()
    for ($Index = 0; $Index -lt $ServiceOverloads.Count; $Index += 2) {
        if ($Index + 1 -lt $ServiceOverloads.Count) {
            $Pairs.Add("($($ServiceOverloads[$Index]) & $($ServiceOverloads[$Index + 1]))")
        } else { $Pairs.Add($ServiceOverloads[$Index]) }
    }
    $ServiceOverloads = @($Pairs)
}
$Types.Add("export type App = $($ServiceOverloads[0])")
$Types.Add('')

$ContainerTypes = @($Schema.classes | Where-Object acceptsChildren | ForEach-Object name) -join ' | '
foreach ($Class in $Schema.classes) {
    if (-not $Class.creatable) { continue }
    $Lineage = [System.Collections.Generic.List[object]]::new()
    $Cursor = $Class
    while ($Cursor) {
        $Lineage.Insert(0, $Cursor)
        $Cursor = if ($Cursor.base) { $ClassMap[$Cursor.base] } else { $null }
    }
    $Types.Add("export type $($Class.name)Init = {")
    foreach ($Ancestor in $Lineage) {
        foreach ($Property in $Ancestor.properties) {
            if ($Property.readOnly -or ($Class.parentRule -eq 'none' -and $Property.name -eq 'Parent')) { continue }
            $PropertyType = if ($Property.name -eq 'Parent' -and $Class.parentRule -eq 'visual') { "($ContainerTypes | GuiObject)?" }
                elseif ($Property.name -eq 'Parent') { "($ContainerTypes)?" }
                elseif ($Property.type.EndsWith('?')) { $Property.type }
                else { "$($Property.type)?" }
            $Types.Add("    $($Property.name): $PropertyType,")
        }
    }
    $Types.Add('}')
    $Types.Add('')
}

$CreatableClasses = @($Schema.classes | Where-Object creatable)
$ConstructorOverloads = @($CreatableClasses | Select-Object -First 7 | ForEach-Object {
    "((ClassName: `"$($_.name)`", Properties: any) -> $($_.name))"
})
$SharedClasses = @($CreatableClasses | Select-Object -Skip 7)
if ($SharedClasses.Count) {
    # Keep the definition below the pinned Luau solver's overload complexity limit.
    $Names = @($SharedClasses | ForEach-Object { "`"$($_.name)`"" }) -join ' | '
    $ResultTypes = @($SharedClasses | ForEach-Object { $_.name }) -join ' | '
    $ConstructorOverloads += "((ClassName: $Names, Properties: any) -> ($ResultTypes))"
}
while ($ConstructorOverloads.Count -gt 1) {
    $Pairs = [System.Collections.Generic.List[string]]::new()
    for ($Index = 0; $Index -lt $ConstructorOverloads.Count; $Index += 2) {
        if ($Index + 1 -lt $ConstructorOverloads.Count) {
            $Pairs.Add("($($ConstructorOverloads[$Index]) & $($ConstructorOverloads[$Index + 1]))")
        } else {
            $Pairs.Add($ConstructorOverloads[$Index])
        }
    }
    $ConstructorOverloads = @($Pairs)
}
$Types.Add("declare Instance: { read new: $($ConstructorOverloads[0]) }")
$Types.Add('declare Vector2: { read new: (X: number, Y: number) -> Vector2 }')
$Types.Add('declare UDim: { read new: (Scale: number, Offset: number) -> UDim }')
$Types.Add('declare UDim2: {')
$Types.Add('    read new: (XScale: number, XOffset: number, YScale: number, YOffset: number) -> UDim2,')
$Types.Add('    read fromOffset: (X: number, Y: number) -> UDim2,')
$Types.Add('    read fromScale: (X: number, Y: number) -> UDim2,')
$Types.Add('}')
$Types.Add('declare app: App')
$Types.Add('declare task: {')
$Types.Add('    read defer: (Callback: () -> ()) -> (),')
$Types.Add('    read spawn: (Callback: () -> ()) -> (),')
$Types.Add('    read delay: (Seconds: number, Callback: () -> ()) -> (),')
$Types.Add('}')

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
    if ($Class.methods.Count) {
        $Docs.Add('| Method | Type |')
        $Docs.Add('| --- | --- |')
        foreach ($Method in $Class.methods) { $Docs.Add("| ``$($Method.name)`` | ``$($Method.type)`` |") }
        $Docs.Add('')
    }
    if ($Class.signals.Count) {
        $Docs.Add('| Signal | Type |')
        $Docs.Add('| --- | --- |')
        foreach ($Signal in $Class.signals) { $Docs.Add("| ``$($Signal.name)`` | ``$($Signal.type)`` |") }
        $Docs.Add('')
    }
}
$Docs.Add('## Services')
$Docs.Add('')
foreach ($Service in $Schema.services) {
    $Names = @($Service.methods | ForEach-Object name) + @($Service.signals | ForEach-Object name)
    $Docs.Add("- ``$($Service.name)``: $($Names -join ', ')")
}
$Docs.Add('')
$Docs.Add('## Networking value and resource types')
$Docs.Add('')
foreach ($Object in $Schema.objects) {
    $Docs.Add("### $($Object.name)")
    $Docs.Add('')
    foreach ($Property in $Object.properties) { $Docs.Add("- ``$($Property.name)``: ``$($Property.type)``") }
    foreach ($Method in $Object.methods) { $Docs.Add("- ``$($Method.name)``: ``$($Method.type)``") }
    foreach ($Signal in $Object.signals) { $Docs.Add("- ``$($Signal.name)``: ``$($Signal.type)``") }
    $Docs.Add('')
}
if ($Docs.Count -gt 0 -and $Docs[$Docs.Count - 1] -eq '') {
    $Docs.RemoveAt($Docs.Count - 1)
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
