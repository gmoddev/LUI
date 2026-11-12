'use strict';
const Assert = require('node:assert/strict');
const Fs = require('node:fs');
const Path = require('node:path');
const Os = require('node:os');
const ChildProcess = require('node:child_process');
const {NativePreview} = require('../../vscode/NativePreview');

async function Main() {
    const Host = Path.resolve(process.argv[2]);
    const Output = Path.resolve(process.argv[3]);
    const Directory = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'lui-gtk-diagnostics-'));
    const Cases = [];
    async function Run(Name, Policy, Capabilities, Source, ExpectedCode, Phase, ClosedPipe = false) {
        const Root = Path.join(Directory, Name);
        Fs.mkdirSync(Path.join(Root, 'Src'), {recursive: true});
        Fs.writeFileSync(Path.join(Root, 'Src/Main.luau'), Source);
        const Manifest = Path.join(Root, 'lui.json');
        Fs.writeFileSync(Manifest, JSON.stringify({SchemaVersion: 2, Script: 'Src/Main.luau',
            Capabilities, Extensions: [], Assets: [], ExtensionDeclarations: [], Diagnostics: {Mode: Policy},
            Modules: {Root: 'Src', Files: ['Main.luau'], Requires: [], Lock: 'Modules.lock.json', Dependencies: []}}));
        Fs.writeFileSync(Path.join(Root, 'Modules.lock.json'), JSON.stringify({SchemaVersion: 1, Packages: []}));
        const Records = [];
        let Child;
        const Preview = new NativePreview(Manifest, Host, (Executable, Arguments, Options) => {
            Child = ChildProcess.spawn(Executable, Arguments, {...Options,
                env: {...process.env, GDK_BACKEND: 'x11', GSK_RENDERER: 'cairo', GTK_USE_PORTAL: '0'}});
            if (ClosedPipe) Child.stderr.destroy();
            return Child;
        }, 'linux');
        Preview.on('diagnostic', Value => Records.push(Value));
        let Timeout;
        try {
            const Exited = new Promise((Resolve, Reject) => {
                Preview.once('exit', Resolve);
                Timeout = setTimeout(() => {Preview.Stop(); Reject(new Error('GTK diagnostic deadline'));}, 8000);
            });
            Assert.equal(Preview.Start(), true);
            const Exit = await Exited;
            Assert.equal(Exit.Code, Phase === 'Startup' ? 1 : 0, Name);
            if (!ClosedPipe) {
                Assert.ok(Records.some(Value => Value.Code === ExpectedCode && Value.Phase === Phase), Name);
                Assert.ok(Records.length <= 256);
                Assert.doesNotMatch(JSON.stringify(Records), /PRIVATE_|CANARY|Main\.luau|\/tmp\//);
                Assert.equal(Exit.StartupDiagnostic, Phase === 'Startup');
            }
            Cases.push({Name, Policy, Status: 'PASS', Exit: Exit.Code, Records: Records.length});
        } finally {
            clearTimeout(Timeout);
            Preview.Stop();
        }
    }
    try {
        for (const Policy of ['Development', 'Restricted']) {
            await Run(Policy + 'Unknown', Policy, ['PRIVATE_TOKEN_CANARY'], '', 'UnknownCapability', 'Startup');
            await Run(Policy + 'Startup', Policy, [], "app:GetService('HttpServerService')", 'NetworkServerRequired', 'Startup');
            const Runtime = "local Window=Instance.new('Window',{Title='Synthetic diagnostics',Visible=true})\n" +
                "task.defer(function() app:GetService('HttpServerService') end)\n" +
                "task.delay(0.08,function() Window:Destroy() end)";
            await Run(Policy + 'Runtime', Policy, [], Runtime, 'NetworkServerRequired', 'Runtime');
        }
        const Flood = "local Window=Instance.new('Window',{Visible=true})\n" +
            "for Index=1,600 do task.defer(function() error('PRIVATE_PAYLOAD_CANARY') end) end\n" +
            "task.delay(0.08,function() Window:Destroy() end)";
        await Run('Flood', 'Development', [], Flood, 'OperationFailed', 'Runtime');
        await Run('ClosedCollector', 'Development', [], Flood, 'OperationFailed', 'Runtime', true);
        Fs.writeFileSync(Output, JSON.stringify({Status: 'PASS', Boundary: 'Actual GTK host through NativePreview under Xvfb', Cases}, null, 2));
        console.log('[LUI:GtkDiagnostics] Real startup/runtime, policy privacy, flood and closed collector passed');
    } finally {
        Fs.rmSync(Directory, {recursive: true, force: true});
    }
}
Main().catch(Error => {console.error(Error.stack); process.exitCode = 1;});
