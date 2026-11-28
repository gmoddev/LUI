<#
Run in an isolated interactive Windows test desktop using a private SDK candidate.
Exercises real clipboard and native picker cancellation; does not select files.
Creates a fresh project/output directory and launches its native window.
#>
param(
    [Parameter(Mandatory=$true)][string]$Sdk,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$Sdk=[IO.Path]::GetFullPath($Sdk)
$Root=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $Root){throw 'Choose a fresh qualification output directory'}
$Metadata=Get-Content -LiteralPath "$Sdk/SdkManifest.json" -Raw|ConvertFrom-Json
$Cli=Join-Path $Sdk $Metadata.Components.Cli
New-Item -ItemType Directory -Path $Root|Out-Null
$Report=@{Status='Running';Scope='Windows native clipboard; immediate/delayed picker cancellation and reuse; close after request. No file selection or chooser-visibility assertion.';Inventory=(Get-FileHash "$Sdk/SdkManifest.json").Hash.ToLowerInvariant()}
$Child=$null
try {
    $App="$Root/App"
    & $Cli new $App --sdk $Sdk *> "$Root/New.log"
    if($LASTEXITCODE){throw 'Project creation failed'}
    $Manifest=Get-Content "$App/lui.json" -Raw|ConvertFrom-Json
    $Manifest.Capabilities=@('Clipboard','Dialogs')
    $Manifest|ConvertTo-Json -Depth 12|Set-Content "$App/lui.json"
    $Source=@'
--!strict
local Window=Instance.new('Window',{Title='Native services starting',Size=UDim2.fromOffset(440,200),Visible=true})
Instance.new('TextLabel',{Parent=Window,Text='Synthetic native services qualification',TextWrapped=true,Size=UDim2.fromScale(1,1)})
local Service=app:GetService('FileService')
local Completed=0
local Retries=0
local function Next()
    if Completed==6 then
        Window.Title='Native cancellation and clipboard passed'
        task.delay(1,function()
            Service:OpenFile(buffer.create(0),nil,function() error('RETIRED_PICKER_CALLBACK') end)
            task.delay(0.3,function() Window:Destroy() end)
        end)
        return
    end
    local Kind=Completed%3+1
    local Delay=if Completed<3 then 0 else 0.8
    local Settled=false
    local function Complete(Bytes:buffer?,Handle:NativeHandle?,Failure:string?)
        Settled=true
        if Failure=='WouldBlock' and not Bytes and not Handle and Retries<24 then
            Retries+=1
            task.delay(0.2,Next)
            return
        end
        if Bytes or Handle or Failure~='Canceled' then
            if Handle then Handle:Close() end
            Window.Title='Picker failure '..tostring(Kind)..' '..tostring(Failure)
            return
        end
        Completed+=1
        Window.Title='Picker completed '..tostring(Completed)
        task.delay(0.6,Next)
    end
    local Request:NativeRequest
    if Kind==1 then Request=Service:OpenFile(buffer.create(0),nil,Complete)
    elseif Kind==2 then Request=Service:SaveFile(buffer.fromstring('candidate-test.txt'),nil,Complete)
    else Request=Service:OpenFolder(buffer.create(0),nil,Complete) end
    task.delay(Delay,function()
        if Settled then return end
        local Canceled,Reason=Request:Cancel()
        assert(Canceled or Reason=='AlreadySettled',Reason)
    end)
end
task.delay(0.5,function()
    local Clipboard=app:GetService('ClipboardService')
    assert(Clipboard:WriteText('Synthetic LUI native clipboard'))
    Clipboard:ReadText(function(Text,Failure)
        assert(not Failure and Text=='Synthetic LUI native clipboard')
        Next()
    end)
end)
task.delay(25,function() Window:Destroy() end)
'@
    [IO.File]::WriteAllText("$App/Src/Main.luau",$Source,[Text.UTF8Encoding]::new($false))
    & $Cli check "$App/lui.json" --sdk $Sdk *> "$Root/Check.log"
    if($LASTEXITCODE){throw 'Native test source failed type checking'}
    $Child=Start-Process -FilePath $Cli -ArgumentList @('run',"`"$App/lui.json`"",'--sdk',"`"$Sdk`"") -WindowStyle Hidden -PassThru -RedirectStandardOutput "$Root/Run.out.log" -RedirectStandardError "$Root/Run.err.log"
    $null=$Child.Handle
    $Clock=[Diagnostics.Stopwatch]::StartNew();$Seen=$false;$Titles=@()
    while($Clock.Elapsed.TotalSeconds -lt 30 -and -not $Child.HasExited){
        $Items=@(Get-CimInstance Win32_Process -Filter "Name='Lui.WinUI.exe'"|Where-Object{$_.ParentProcessId -eq $Child.Id})
        foreach($Item in $Items){
            $Native=Get-Process -Id $Item.ProcessId -ErrorAction SilentlyContinue
            if($Native -and $Native.MainWindowHandle -ne [IntPtr]::Zero -and $Native.Responding){
                $Titles+=$Native.MainWindowTitle
                if($Native.MainWindowTitle -eq 'Native cancellation and clipboard passed'){$Seen=$true}
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if(-not $Child.WaitForExit(5000)){throw 'Native test timed out'}
    $Child.Refresh();$Report.Exit=$Child.ExitCode;$Report.Titles=@($Titles|Select-Object -Unique)
    if(-not $Seen -or $Child.ExitCode -ne 0){throw 'Native cancellation sequence did not complete'}
    if((Get-Content "$Root/Run.err.log" -Raw) -match '\[LUI:App\]'){throw 'Native callback or retirement diagnostic'}
    foreach($File in $Metadata.Files.PSObject.Properties){
        if((Get-FileHash -LiteralPath (Join-Path $Sdk $File.Name)).Hash.ToLowerInvariant() -ne $File.Value){throw 'SDK file changed'}
    }
    $Report.Status='PASS'
} catch {
    $Report.Status='FAIL';$Report.Reason=$_.Exception.Message
    throw
} finally {
    try {
        if($Child -and -not $Child.HasExited){
            # This exact CLI owns the native test descendant. Never enumerate or
            # stop unrelated applications or broad executable-name matches.
            & taskkill.exe /PID $Child.Id /T /F *> "$Root/Retire.log"
            $null=$Child.WaitForExit(5000)
        }
    } finally {
        $Report|ConvertTo-Json -Depth 5|Set-Content "$Root/Report.json"
    }
}
