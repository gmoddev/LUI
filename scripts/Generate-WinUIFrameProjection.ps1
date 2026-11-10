param([Parameter(Mandatory=$true)][string]$BuildDirectory)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
& dotnet restore (Join-Path $Root 'backends/winui3/Lui.WinUI.csproj') -p:Platform=x64
if($LASTEXITCODE){throw '[LUI:Build] WinUI metadata restore failed'}
$Assets=Get-Content -Raw (Join-Path $Root 'backends/winui3/obj/project.assets.json') | ConvertFrom-Json
$Metadata=[System.Collections.Generic.List[string]]::new()
foreach($Package in $Assets.libraries.PSObject.Properties) {
    if($Package.Name -notlike 'Microsoft.WindowsAppSDK*/*' -and $Package.Name -notlike 'Microsoft.Web.WebView2/*'){continue}
    foreach($Cache in $Assets.packageFolders.PSObject.Properties.Name) {
        $Directory=Join-Path $Cache $Package.Name.ToLowerInvariant()
        if(Test-Path $Directory){
            foreach($File in Get-ChildItem -LiteralPath $Directory -Filter *.winmd -Recurse){$Metadata.Add($File.FullName)}
        }
    }
}
$Xaml=@($Metadata | Where-Object {[IO.Path]::GetFileName($_) -eq 'Microsoft.UI.Xaml.winmd'})
if($Xaml.Count -ne 1){throw '[LUI:Build] Exact WinUI metadata not found'}
$Sdk='C:/Program Files (x86)/Windows Kits/10'
$Compiler=Join-Path $Sdk 'bin/10.0.26100.0/x64/cppwinrt.exe'
$Windows=Join-Path $Sdk 'UnionMetadata/10.0.26100.0/Windows.winmd'
$Output=Join-Path $BuildDirectory 'WinUIProjection'
$Arguments=@('-reference',$Windows,'-output',$Output)
foreach($File in $Metadata){$Arguments+=@('-input',$File)}
& $Compiler @Arguments
if($LASTEXITCODE){throw '[LUI:Build] WinUI C++ projection generation failed'}
$Include=Join-Path (Split-Path (Split-Path $Xaml[0])) 'include'
[IO.File]::WriteAllText((Join-Path $BuildDirectory 'WinUIFrameProjection.json'),(@{Projection=$Output;Include=$Include}|ConvertTo-Json))
Write-Output "[LUI:Build] WinUIProjection=$Output; WinUIInclude=$Include"
