"""Run the installed spec-cli and its vendored libraries as standalone applications."""
import argparse
import json
import math
import os
from pathlib import Path
import hashlib
import subprocess
import tempfile


def Main():
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('--sdk', type=Path, required=True)
    Sdk = Parser.parse_args().sdk.resolve(strict=True)
    Cli = Sdk / json.loads((Sdk / 'SdkManifest.json').read_bytes())['Components']['Cli']
    Env = {Key: Value for Key, Value in os.environ.items() if Key not in ('LUI_SDK', 'LD_LIBRARY_PATH')}
    Flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
    with tempfile.TemporaryDirectory(prefix='lui spec ') as Temporary:
        Root = Path(Temporary)
        App = Root / 'MyTests'
        Manifest = App / 'lui.json'

        def Run(Args, Code=0):
            Done = subprocess.run(Args, cwd=Root, env=Env, stdin=subprocess.DEVNULL, capture_output=True,
                                  timeout=30, creationflags=Flags)
            assert Done.returncode == Code, (Args, Done.returncode, Done.stdout[:200], Done.stderr[:300])
            return Done

        def Build(Name):
            Output = Root / Name
            Run([Cli, 'check', Manifest, '--sdk', Sdk])
            Run([Cli, 'build', Manifest, '--sdk', Sdk, '--mode', 'headless', '--output', Output])
            return Output / ('Start-LUI.exe' if os.name == 'nt' else 'Start-LUI.sh')

        Digest = hashlib.sha256((Sdk / 'SdkManifest.json').read_bytes()).hexdigest()
        for Template, Destination in [('spec-cli', App), ('parallel-data', Root / 'MyWorkers')]:
            Run([Cli, 'new', Destination, '--template', Template, '--sdk', Sdk])
            assert json.loads((Destination/'LuiToolchain.json').read_bytes())['SdkManifestSha256'] == Digest
            Example = Sdk / 'Examples' / Template
            Inventory = json.loads((Sdk/'SdkManifest.json').read_bytes())['Files']
            Prefix = 'Examples/' + Template + '/'
            Expected = {Key[len(Prefix):] for Key in Inventory if Key.startswith(Prefix)}
            Actual = {File.relative_to(Destination).as_posix() for File in Destination.rglob('*') if File.is_file()}
            assert Actual == Expected | {'.gitignore', 'LuiToolchain.json'}
            assert all((Destination/Name).read_bytes() == (Example/Name).read_bytes() for Name in Expected)
            Run([Cli, 'check', Destination/'lui.json', '--sdk', Sdk])
        import concurrent.futures
        Race = Root / 'Concurrent'
        def Create():
            return subprocess.run([Cli, 'new', Race, '--template', 'spec-cli', '--sdk', Sdk], cwd=Root,
                                  env=Env, capture_output=True, timeout=30, creationflags=Flags).returncode
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as Helpers:
            Results = [Helpers.submit(Create) for _ in range(2)]
            assert sum(Job.result()==0 for Job in Results)==1
        Run([Cli, 'check', Race/'lui.json', '--sdk', Sdk])
        Launcher = Build('standalone')
        Both = ['seeded sequence', 'integer range']
        for Args, Names, Count in [
            ([], Both, 2), (['--list'], Both, 0),
            (['--list', '--filter=integer'], Both[1:], 0),
            (['--filter', 'seeded', '--repeat', '5'], Both[:1], 5),
            (['--repeat=2'], Both, 4), (['--repeat=100'], Both, 200),
            (['--list', '--filter', '.'], [], 0),
        ]:
            Done = Run([Launcher, '--stdio', '--', *Args])
            Report = json.loads(Done.stdout)
            assert not Done.stderr and Report['Cases'] == Names and Report['Runs'] == Count, Report
            assert Report['Passed'] is True and 0 <= Report['Milliseconds'] and math.isfinite(Report['Milliseconds'])
        for Args in (['--filter', 'absent'], ['--repeat', '0'], ['--repeat', '101'], ['--repeat', '1.5'],
                     ['--repeat', '1e2'], ['--filter', 'a' * 65], ['--list', '--list'], ['extra']):
            Done = Run([Launcher, '--stdio', '--', *Args], 2)
            assert not Done.stdout and Done.stderr.startswith(b'[LUI:Spec]'), (Args, Done.stderr)

        Suites = Path(__file__).resolve().parents[1] / 'libraries'
        Functions = []
        for Name in ('Spec', 'Random'):
            Body = (Suites / (Name + '.luau')).read_text(encoding='utf-8').removeprefix('--!strict\n')
            Functions.append('local function ' + Name + 'Suite()\n' + Body + '\nend\n' + Name + 'Suite()')
        MainSource = '--!strict\nlocal Stdio = require("@Stdio/Stdio")\nStdio.SetExitStatus(1)\n'
        (App / 'Src/Main.luau').write_text(MainSource + '\n'.join(Functions) + '\nStdio.SetExitStatus(0)\n', encoding='utf-8')
        Done = Run([Build('library suites'), '--stdio'])
        assert not Done.stdout and not Done.stderr, (Done.stdout, Done.stderr)
    print('[LUI:DeveloperTools] Standalone list, filter, repeat, usage, Spec cleanup and Random vectors passed')


if __name__ == '__main__':
    Main()
