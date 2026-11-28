"""Qualify owner consent, pinned Ffi admission, worker authority and standalone launch."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def Read(PathName):
    return json.loads(PathName.read_bytes())


def Write(PathName, Value):
    PathName.write_text(json.dumps(Value, indent=2) + '\n', encoding='utf-8')


def Run(Arguments, Success, Expected=b''):
    Result = subprocess.run(Arguments, capture_output=True, timeout=35)
    Evidence = Result.stdout + Result.stderr
    assert (Result.returncode == 0) == Success and Expected in Evidence, (
        Arguments[:3], Result.returncode, Evidence[:450], Expected)
    if not Success:
        assert b'FFI-ADMITTED' not in Evidence


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--sdk', type=Path, required=True)
    Parser.add_argument('--minimal-host', type=Path, help='optional headless binary built without the Ffi provider')
    Parser.add_argument('--desktop-host', type=Path, help='optional genuine WinUI or GTK project host, with a display available on Linux')
    Args = Parser.parse_args()
    Sdk = Args.sdk.resolve()
    Parts = Read(Sdk / 'SdkManifest.json')['Components']
    Cli, Host = Sdk / Parts['Cli'], Sdk / Parts['HeadlessHost']
    with tempfile.TemporaryDirectory(prefix='lui-ffi-authority-') as Temporary:
        Root = Path(Temporary)
        App = Root / 'App'
        shutil.copytree(Sdk / 'Examples/ffi-platform', App)
        Manifest = App / 'lui.json'
        Original = Read(Manifest)
        Entry = App / Original['Script']
        Streams = 'Stdio.json' in Original['ExtensionDeclarations']
        HostMode = ['--stdio'] if Streams else ['--diagnostics', 'development']
        CliMode = ['--stdio', 'true'] if Streams else ['--diagnostics', 'development']
        Done = ('require("@Stdio/Stdio").Write("FFI-ADMITTED",function() end)' if Streams else 'print("FFI-ADMITTED")')
        Platform = '''local Ffi = require("@Ffi/Ffi")
local Info = Ffi.Platform()
assert((Info.OS == "Windows" or Info.OS == "Linux") and Info.PointerBytes == 8)
'''
        Entry.write_text('--!strict\n' + Platform + Done + '\n', encoding='utf-8')
        Native = [Host, '--project', Manifest, *HostMode]
        Managed = [Cli, 'run', Manifest, '--sdk', Sdk, '--mode', 'headless', *CliMode]
        for Arguments, Diagnostic in [
            (Native, b'FfiTrustRequired'),
            ([*Native, '--provider', 'Ffi'], b'InvalidArguments'),
            ([*Native, '--provider', 'Crypto'], b'FfiTrustRequired'),
            ([Host, '--project', Manifest, '--stdio', '--', '--trusted-ffi'], b'FfiTrustRequired'),
            ([*Managed, '--providers', 'Ffi'], b'Unknown standard provider'),
            ([*Managed, '--trusted-ffi', 'yes'], b'--trusted-ffi accepts true'),
            ([*Native, '--trusted-ffi', '--trusted-ffi'], b'InvalidArguments'),
        ]:
            Run(Arguments, False, Diagnostic)
        Run([*Native, '--trusted-ffi'], True, b'FFI-ADMITTED')
        Run([*Managed, '--trusted-ffi', 'true'], True, b'FFI-ADMITTED')
        Entry.write_text('--!strict\n' + Platform + '''for _ = 1, 12 do
    local Library = Ffi.System("process")
    Library:Close()
end
''' + Done, encoding='utf-8')
        Run([*Native, '--trusted-ffi'], True, b'FFI-ADMITTED')
        Entry.write_text('--!strict\n' + Platform + Done + '\n', encoding='utf-8')
        Run([Sdk / Parts['PreviewHost'], '--project', Manifest, '--runtime', Sdk / Parts['Runtime']],
            False, b'Inspect Layout cannot run privileged capabilities')
        if Args.desktop_host:
            Desktop = Args.desktop_host.resolve()
            Run([Desktop, '--project', Manifest], False, b'CapabilityDenied')
            if os.name == 'nt':
                Legacy = Root / 'Legacy'
                Legacy.mkdir()
                (Legacy / 'Main.luau').write_text('error("unexpected entry")', encoding='utf-8')
                Write(Legacy / 'lui.json', {'SchemaVersion': 1, 'Script': 'Main.luau',
                      'Capabilities': ['NativeExtensions'], 'Extensions': [], 'Assets': []})
                Run([Desktop, '--manifest', Legacy / 'lui.json'], False, b'CapabilityDenied')
        if Args.minimal_host:
            MinimalBin = Root / 'Minimal/bin'
            MinimalBin.mkdir(parents=True)
            MinimalHost = MinimalBin / Host.name
            shutil.copy2(Args.minimal_host, MinimalHost)
            LibraryDirectory = MinimalBin if os.name == 'nt' else Root / 'Minimal/lib'
            LibraryDirectory.mkdir(exist_ok=True)
            shutil.copy2(Sdk / Parts['Runtime'], LibraryDirectory)
            Run([MinimalHost, '--project', Manifest, *HostMode, '--trusted-ffi'], False, b'ProviderNotQualified')

        for Capability in ('NativeExtensions', 'HostServices'):
            Write(Manifest, {**Original, 'Capabilities': [Name for Name in Original['Capabilities'] if Name != Capability]})
            Run([*Native, '--trusted-ffi'], False, b'ProjectPreparationFailed')
        Write(Manifest, Original)
        Destination = Root / 'Standalone'
        Run([Cli, 'build', Manifest, '--sdk', Sdk, '--mode', 'headless', '--output', Destination], True)
        Launcher = Destination / ('Start-LUI.exe' if os.name == 'nt' else 'Start-LUI.sh')
        Run([Launcher, *HostMode], False, b'FfiTrustRequired')
        Run([Launcher, *HostMode, '--trusted-ffi'], True, b'FFI-ADMITTED')

        # Re-pin harmless, non-executable bytes in the application's declaration.
        # Matching application metadata still does not satisfy the SDK host's compiled pin.
        DeclarationPath = App / 'Ffi.json'
        Declaration = Read(DeclarationPath)
        Binding = Declaration['Extension']['Bindings'][0]
        ImagePath = App / Binding['Path']
        SavedImage = ImagePath.read_bytes()
        SavedDeclaration = DeclarationPath.read_bytes()
        Substitute = b'LUI qualification pin mismatch'
        ImagePath.write_bytes(Substitute)
        Binding['Sha256'] = hashlib.sha256(Substitute).hexdigest()
        Write(DeclarationPath, Declaration)
        Run([*Native, '--trusted-ffi'], False, b'ProviderNotQualified')
        DeclarationPath.write_bytes(SavedDeclaration)
        ImagePath.write_bytes(SavedImage)

        # Bring the SDK's real worker library and its captured dependencies into the root graph.
        WorkerExample = Sdk / 'Examples/parallel-data'
        WorkerManifest = Read(WorkerExample / 'lui.json')
        LockPath = App / 'Modules.lock.json'
        Lock = Read(LockPath)
        Aliases = {Dep['Alias'] for Dep in Original['Modules']['Dependencies']}
        for Dep in WorkerManifest['Modules']['Dependencies']:
            Alias = Dep['Alias']
            if Alias not in ('Workers', 'Messages', 'Json') or Alias in Aliases:
                continue
            shutil.copytree(WorkerExample / Dep['Root'], App / Dep['Root'])
            Original['Modules']['Dependencies'].append(Dep)
            Lock['Packages'].append(next(P for P in Read(WorkerExample / 'Modules.lock.json')['Packages'] if P['Alias'] == Alias))
        Original['Modules']['Requires'].append('Workers')
        Original['Modules']['Files'].append('AuthorityWorker.luau')
        Write(Manifest, Original)
        Write(LockPath, Lock)
        (Entry.parent / 'AuthorityWorker.luau').write_text('''--!strict
local Workers = require("@Workers/Workers")
return Workers.Handler(function(Message: any): any
    if Message == "ping" then return "ready" end
    return require("@Ffi/Ffi").Platform()
end)
''', encoding='utf-8')
        Entry.write_text('--!strict\n' + Platform + '''local Workers = require("@Workers/Workers")
Workers.Start("./AuthorityWorker", {}, function(Worker, Err)
    assert(Worker and not Err)
    local Active = assert(Worker)
    local Owned = Ffi.Allocate(8)
    local Sent = pcall(function() Active:Send(Owned, function() end) end)
    assert(Owned:Close() and not Sent)
    Active:Send("ping", function(Value, Failure)
        assert(Value == "ready" and not Failure)
        Active:Send("ffi", function(Unauthorized, Why)
            Active:Close()
            assert(Unauthorized == nil and Why ~= nil)
            ''' + Done + '''
        end)
    end)
end)
''', encoding='utf-8')
        Run([*Native, '--trusted-ffi'], True, b'FFI-ADMITTED')

        HelperRoot = App / 'Vendor/AuthorityHelper'
        HelperRoot.mkdir()
        Helper = HelperRoot / 'Helper.luau'
        Helper.write_text('''--!strict
return table.freeze({
    Import = function(Name: string) return require(Name) end,
    Call = function(Name: string) return require(Name).Platform() end,
})
''', encoding='utf-8')
        Original['Modules']['Dependencies'].append({'Alias': 'AuthorityHelper', 'Root': 'Vendor/AuthorityHelper',
                                                    'Files': ['Helper.luau'], 'Requires': ['Ffi']})
        Original['Modules']['Requires'].append('AuthorityHelper')
        Lock['Packages'].append({'Alias': 'AuthorityHelper', 'Provenance': {'Kind': 'LocalVendor', 'Id': 'authority-helper-v1'},
                                 'Files': [{'Path': 'Helper.luau', 'Sha256': hashlib.sha256(Helper.read_bytes()).hexdigest()}]})
        Write(LockPath, Lock)
        Write(Manifest, {**Original, 'Capabilities': [], 'Extensions': [], 'ExtensionDeclarations': []})
        Entry.write_text('''--!strict
local Name = table.concat({"@F", "fi/", "Ffi"})
local Helper = require("@AuthorityHelper/Helper")
local Import = require("@Ffi/Ffi")
assert(type(Import.Platform) == "function" and require(Name) == Import)
assert(not pcall(function() return require(Name).Platform() end))
assert(Helper.Import(Name) == Import and not pcall(Helper.Call, Name))
assert(require("@Ffi/Ffi") == Import and require("@Json/Json").Decode("42") == 42)
print("IMPORT-WITHOUT-AUTHORITY")
''', encoding='utf-8')
        Run([Host, '--project', Manifest, '--diagnostics', 'development'], True, b'IMPORT-WITHOUT-AUTHORITY')
        Run([Host, '--project', Manifest, '--trusted-ffi'], False, b'ProviderNotQualified')
        Entry.write_text('''--!strict
local Sum = 0
for Index = 1, 50000 do Sum += Index end
print(Sum)
''', encoding='utf-8')
        Run([Host, '--project', Manifest, '--diagnostics', 'development'], False, b'RuntimeFailed')
    print('[LUI:Ffi] Explicit admission, project and SDK pins, worker denial, source-only import and standalone consent passed')


if __name__ == '__main__':
    Main()
