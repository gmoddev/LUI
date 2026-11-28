param([long]$ViewerWindow,[string]$Receipt,[string]$DesktopName,[int]$ViewerPid,[string]$Sample,[string]$Mode)
$ErrorActionPreference='Stop'
try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    $Viewer=[System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]$ViewerWindow)
    $Scope=[System.Windows.Automation.TreeScope]::Descendants
    $All=[System.Windows.Automation.Condition]::TrueCondition
    function Wait-Text([string]$Text) {
        $End=[Diagnostics.Stopwatch]::StartNew()
        do {
            foreach($Node in $Viewer.FindAll($Scope,$All)) { if($Node.Current.Name.Contains($Text)) {return} }
            Start-Sleep -Milliseconds 80
        } while($End.ElapsedMilliseconds -lt 4500)
        if ($Mode -eq 'ffi') {
            $Labels = @($Viewer.FindAll($Scope,$All) | ForEach-Object { $_.Current.Name } | Where-Object { $_ })
            throw "Expected native text missing: $Text (observed $($Labels -join ', '))"
        }
        throw "Expected native text missing: $Text"
    }
    if($Mode -eq 'ffi') {
        function Find-Action([string]$Name) {
            $Condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,$Name)
            return $Viewer.FindFirst($Scope,$Condition).GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
        }
        Wait-Text 'Ready'
        $Run = Find-Action 'Run'
        $Cancel = Find-Action 'Cancel'
        $Condition = [System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::Edit)
        $Edit = $Viewer.FindFirst($Scope,$Condition).GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
        # Some UIA calls return after the native job has finished. Deliver the two
        # button actions from independent clients so cancellation reaches the live job.
        Add-Type -ReferencedAssemblies ([System.Windows.Automation.InvokePattern].Assembly.Location) -TypeDefinition @'
using System;
using System.Threading;
using System.Threading.Tasks;
using System.Text;
using System.Runtime.InteropServices;
using System.Windows.Automation;
public static class WorkbenchInput {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr window, StringBuilder text, int size);
    public static string Title(IntPtr window) {
        var text = new StringBuilder(256);
        GetWindowText(window, text, text.Capacity);
        return text.ToString();
    }
    public static void RunAndCancel(InvokePattern run, InvokePattern cancel) {
        using (var started = new ManualResetEventSlim()) {
            var first = Task.Run(() => { started.Set(); run.Invoke(); });
            if (!started.Wait(3000)) throw new TimeoutException();
            Thread.Sleep(40);
            cancel.Invoke();
            if (!first.Wait(3000)) throw new TimeoutException();
        }
    }
}
'@
        $Timer = [Diagnostics.Stopwatch]::StartNew()
        [WorkbenchInput]::RunAndCancel($Run,$Cancel)
        Wait-Text 'Canceled'
        $Latency = $Timer.ElapsedMilliseconds
        $CompletionTimer = [Diagnostics.Stopwatch]::StartNew()
        $Run.Invoke()
        $EditTimer = [Diagnostics.Stopwatch]::StartNew()
        $Edit.SetValue('active')
        do {
            $CurrentTitle = [WorkbenchInput]::Title([IntPtr]$ViewerWindow)
            if ($CurrentTitle -eq 'Native checksum active') { break }
            Start-Sleep -Milliseconds 60
        } while ($EditTimer.ElapsedMilliseconds -lt 4500)
        if ($CurrentTitle -ne 'Native checksum active') { throw "Native edit did not change title '$CurrentTitle'" }
        $EditLatency = $EditTimer.ElapsedMilliseconds
        Wait-Text 'Checksum 322936262'
        $ChecksumLatency = $CompletionTimer.ElapsedMilliseconds
        $Run.Invoke()
    } elseif($Mode -eq 'message') {
        Wait-Text 'bytes'
        $Cond=[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::Edit)
        $Edit=$Viewer.FindFirst($Scope,$Cond)
        $Value=$Edit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
        $Value.SetValue('invalid')
        Wait-Text 'Enter a supported JSON document.'
        $Value.SetValue('{"x":1}')
        Wait-Text '12 bytes'
    } elseif ($Mode -eq 'workers') {
        function Get-Batches {
            foreach ($Node in $Viewer.FindAll($Scope,$All)) {
                if ($Node.Current.Name -match '^Batches (\d+)$') { return [long]$Matches[1] }
            }
            return 0
        }
        Wait-Text 'Batches '
        $Before=Get-Batches
        $Edit=$Viewer.FindFirst($Scope,[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::ControlTypeProperty,[System.Windows.Automation.ControlType]::Edit))
        $Timer=[Diagnostics.Stopwatch]::StartNew()
        $Edit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue('active')
        Wait-Text 'Edited active'
        $Latency=$Timer.ElapsedMilliseconds
        $Timer.Restart()
        do { $After=Get-Batches; if ($After -gt $Before) { break }; Start-Sleep -Milliseconds 50 } while ($Timer.ElapsedMilliseconds -lt 4500)
        if ($After -le $Before) { throw 'Workers did not advance during native interaction' }
        $Button=$Viewer.FindFirst($Scope,[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,'Stop'))
        $Button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        Wait-Text 'Stopped'
        $Timer.Restart()
        $Edit.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue('idle')
        Wait-Text 'Edited idle'
        $Idle=$Timer.ElapsedMilliseconds
        $Mode="workers batches=$Before..$After edit-observed-ms=$Latency idle-observed-ms=$Idle"
    } else {
        Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class PrivateDialogs {
    delegate bool Callback(IntPtr Window,IntPtr Param);
    [DllImport("user32",CharSet=CharSet.Unicode)] static extern IntPtr OpenDesktop(string Name,uint Flags,bool Inherit,uint Access);
    [DllImport("user32")] static extern bool CloseDesktop(IntPtr Desk);
    [DllImport("user32")] static extern bool EnumDesktopWindows(IntPtr Desk,Callback Fn,IntPtr Arg);
    [DllImport("user32")] static extern IntPtr GetWindow(IntPtr Window,uint Relation);
    [DllImport("user32")] static extern uint GetWindowThreadProcessId(IntPtr Window,out uint Pid);
    public static IntPtr[] Owned(string Name,IntPtr Viewer,uint Pid) {
        var Result=new List<IntPtr>();IntPtr Desk=OpenDesktop(Name,0,false,0x41);
        if(Desk==IntPtr.Zero) return Result.ToArray();
        try {EnumDesktopWindows(Desk,(W,A)=>{
            uint Owner;GetWindowThreadProcessId(W,out Owner);
            if(W!=Viewer && (Owner==Pid || GetWindow(W,4)==Viewer)) Result.Add(W);
            return true;
        },IntPtr.Zero);} finally {CloseDesktop(Desk);}
        return Result.ToArray();
    }
}
'@
        $Pick=$Viewer.FindFirst($Scope,[System.Windows.Automation.PropertyCondition]::new([System.Windows.Automation.AutomationElement]::NameProperty,'Choose a file'))
        $Pick.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
        $End=[Diagnostics.Stopwatch]::StartNew()
        $Selected=$false
        do {
            foreach($Window in [PrivateDialogs]::Owned($DesktopName,[IntPtr]$ViewerWindow,$ViewerPid)) {
                $Dialog=[System.Windows.Automation.AutomationElement]::FromHandle($Window)
                $Elements=$Dialog.FindAll($Scope,$All)
                $Filename=$Elements | Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Edit -and ($_.Current.Name -match 'File name' -or $_.Current.AutomationId -eq '1148')} | Select-Object -First 1
                $Open=$Elements | Where-Object {$_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and $_.Current.Name -eq 'Open'} | Select-Object -First 1
                if($Filename -and $Open) {
                    $Filename.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($Sample)
                    $Open.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
                    $Selected=$true
                    break
                }
            }
            if(-not $Selected){Start-Sleep -Milliseconds 90}
        } while(-not $Selected -and $End.ElapsedMilliseconds -lt 7000)
        if(-not $Selected){throw 'Owned chooser was unavailable in the private service desktop'}
        Wait-Text 'chosen.bin'
        Wait-Text '000102030405060708090A0B0C0D0E0F'
    }
    $Outcome = if ($Mode -eq 'ffi') { "ffi edit, cancel, reference checksum and rerun passed (observed cancel $Latency ms, edit $EditLatency ms, checksum $ChecksumLatency ms)" } else { "$Mode passed" }
    "[LUI:WinUI] $Outcome" | Set-Content -LiteralPath $Receipt
} catch {
    $_.Exception.Message | Set-Content -LiteralPath $Receipt
    exit 1
}
