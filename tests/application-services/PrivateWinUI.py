"""Launch the packaged WinUI example on a private, never-activated desktop."""
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import json
import subprocess
import sys
import time
import uuid
import tempfile
import argparse

User = C.WinDLL("user32", use_last_error=True)
Kernel = C.WinDLL("kernel32", use_last_error=True)
Enum = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

class Startup(C.Structure):
    _fields_ = [('cb',W.DWORD),('lpReserved',W.LPWSTR),('lpDesktop',W.LPWSTR),('lpTitle',W.LPWSTR),
                *[(Name,W.DWORD) for Name in ('dwX','dwY','dwXSize','dwYSize','dwXCountChars','dwYCountChars','dwFillAttribute','dwFlags')],
                ('wShowWindow',W.WORD),('cbReserved2',W.WORD),('lpReserved2',C.c_void_p),
                ('hStdInput',W.HANDLE),('hStdOutput',W.HANDLE),('hStdError',W.HANDLE)]

class Process(C.Structure):
    _fields_ = [('Process',W.HANDLE),('Thread',W.HANDLE),('Id',W.DWORD),('ThreadId',W.DWORD)]

def Bind(Lib, Name, Returns, *Arguments):
    Fn = getattr(Lib, Name)
    Fn.restype, Fn.argtypes = Returns, Arguments
    return Fn

CreateDesktop = Bind(User,'CreateDesktopW',W.HANDLE,W.LPCWSTR,W.LPCWSTR,C.c_void_p,W.DWORD,W.DWORD,C.c_void_p)
CloseDesktop = Bind(User,'CloseDesktop',W.BOOL,W.HANDLE)
Enumerate = Bind(User,'EnumDesktopWindows',W.BOOL,W.HANDLE,Enum,W.LPARAM)
Owner = Bind(User,'GetWindowThreadProcessId',W.DWORD,W.HWND,C.POINTER(W.DWORD))
Title = Bind(User,'GetWindowTextW',C.c_int,W.HWND,W.LPWSTR,C.c_int)
Post = Bind(User,'PostMessageW',W.BOOL,W.HWND,W.UINT,W.WPARAM,W.LPARAM)
Create = Bind(Kernel,'CreateProcessW',W.BOOL,W.LPCWSTR,W.LPWSTR,C.c_void_p,C.c_void_p,W.BOOL,W.DWORD,
              C.c_void_p,W.LPCWSTR,C.POINTER(Startup),C.POINTER(Process))
Wait = Bind(Kernel,'WaitForSingleObject',W.DWORD,W.HANDLE,W.DWORD)
ExitCode = Bind(Kernel,'GetExitCodeProcess',W.BOOL,W.HANDLE,C.POINTER(W.DWORD))
Stop = Bind(Kernel,'TerminateProcess',W.BOOL,W.HANDLE,W.UINT)
Close = Bind(Kernel,'CloseHandle',W.BOOL,W.HANDLE)
Bind(Kernel,'SetErrorMode',W.UINT,W.UINT)(0x0001|0x0002|0x8000)

Parser=argparse.ArgumentParser()
Parser.add_argument('--host',required=True,type=Path)
Parser.add_argument('--project',required=True,type=Path)
Parser.add_argument('--receipt',required=True,type=Path)
Parser.add_argument('--picker',action='store_true')
Parser.add_argument('--workers',action='store_true')
Parser.add_argument('--ffi-workbench',action='store_true')
Args=Parser.parse_args()
Host,Manifest,Receipt=Args.host.resolve(),Args.project.resolve(),Args.receipt.resolve()
Picker=Args.picker
if Picker:
    CurrentSession=W.DWORD()
    GetSession=Bind(Kernel,'ProcessIdToSessionId',W.BOOL,W.DWORD,C.POINTER(W.DWORD))
    import os
    if not GetSession(os.getpid(),C.byref(CurrentSession)) or CurrentSession.value!=0:
        raise RuntimeError('The chooser probe requires the isolated worker service session')
Mode = 'picker' if Picker else 'workers' if Args.workers else 'ffi' if Args.ffi_workbench else 'message'
TitleText = {'picker':'File inspector','workers':'Parallel viewer','message':'Message viewer','ffi':'Native checksum'}[Mode]
Temp=tempfile.TemporaryDirectory(prefix='lui-private-ui-')
Sample=Path(Temp.name)/'chosen.bin'
Sample.write_bytes(bytes(range(32))+b'fixture')
Name = 'LuiQualification_' + uuid.uuid4().hex
Desk = CreateDesktop(Name,None,None,0,0x10000000,None)
if not Desk: raise C.WinError(C.get_last_error())
Info = Process()
try:
    Details = Startup()
    Details.cb = C.sizeof(Details)
    Details.lpDesktop = Name
    Options = ['--allow-native'] if Args.ffi_workbench else []
    Command = C.create_unicode_buffer(subprocess.list2cmdline([str(Host),'--project',str(Manifest),*Options]))
    if not Create(str(Host),Command,None,None,False,0,None,str(Manifest.parent),C.byref(Details),C.byref(Info)):
        raise C.WinError(C.get_last_error())
    Found = []
    @Enum
    def Inspect(Window, Data):
        Pid = W.DWORD()
        Owner(Window,C.byref(Pid))
        if Pid.value == Info.Id:
            Text = C.create_unicode_buffer(180)
            Title(Window,Text,len(Text))
            if Text.value == TitleText:
                Found.append(Window)
        return True
    Deadline = time.monotonic() + 18
    while time.monotonic() < Deadline and not Found and Wait(Info.Process,0) == 258:
        Enumerate(Desk,Inspect,0)
        if not Found: time.sleep(0.15)
    if not Found:
        Code = W.DWORD()
        ExitCode(Info.Process,C.byref(Code))
        raise RuntimeError(f'Private desktop window did not appear; process status {Code.value}')
    Ps = Path('C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe')
    Probe = Path(__file__).resolve().with_name('PrivateWinUI.ps1')
    Args = [str(Ps),'-WindowStyle','Hidden','-NoProfile','-NonInteractive','-File',str(Probe),
            str(Found[0]),str(Receipt),Name,str(Info.Id),str(Sample),Mode]
    ProbeInfo = Process()
    ProbeCommand = C.create_unicode_buffer(subprocess.list2cmdline(Args))
    if not Create(str(Ps),ProbeCommand,None,None,False,0x08000000,None,str(Probe.parent),C.byref(Details),C.byref(ProbeInfo)):
        raise C.WinError(C.get_last_error())
    try:
        if Wait(ProbeInfo.Process,22000) != 0:
            Stop(ProbeInfo.Process,1)
            raise RuntimeError('Native accessibility probe timed out')
        ProbeCode = W.DWORD()
        ExitCode(ProbeInfo.Process,C.byref(ProbeCode))
        if ProbeCode.value: raise RuntimeError('Native accessibility probe failed')
    finally:
        Close(ProbeInfo.Thread)
        Close(ProbeInfo.Process)
    if not Post(Found[0],0x10,0,0) or Wait(Info.Process,6000) != 0:
        raise RuntimeError('Private desktop close did not complete')
    Code = W.DWORD()
    ExitCode(Info.Process,C.byref(Code))
    if Code.value: raise RuntimeError(f'Native close returned {Code.value}')
    if Mode == 'ffi':
        with Receipt.open('ab') as Stream:
            Stream.write(b'[LUI:WinUI] Native window closed and host exited 0\r\n')
    print('[LUI:WinUI] '+TitleText+' verified on isolated desktop; exit 0')
finally:
    if Info.Process:
        if Wait(Info.Process,0) == 258:
            Stop(Info.Process,1)
            Wait(Info.Process,3000)
        Close(Info.Process)
        Close(Info.Thread)
    CloseDesktop(Desk)
    Temp.cleanup()
