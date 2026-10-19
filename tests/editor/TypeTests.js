'use strict';

const Test = require('node:test');
const Assert = require('node:assert/strict');
const Fs = require('node:fs');
const Os = require('node:os');
const Path = require('node:path');
const { EventEmitter } = require('node:events');
const { TypeCheckSession, ValidateDiagnostics } = require('../../vscode/TypeCheckClient');
const { TypeDiagnostics, GetEntry } = require('../../vscode/TypeDiagnostics');

const Result = {
    version: 1, source: 'main.luau', truncated: false, diagnostics: [{ kind: 'type', message: 'Expected number',
        range: { start: { line: 0, column: 1 }, end: { line: 0, column: 2 } } }],
};

function FakeChild() {
    const Child = new EventEmitter();
    Child.stdout = new EventEmitter();
    Child.stderr = new EventEmitter();
    Child.Killed = false;
    Child.kill = () => { Child.Killed = true; };
    return Child;
}

Test('type diagnostics validate entry identity, UTF-16 ranges, protocol version and bounds', () => {
    Assert.equal(ValidateDiagnostics(Result, 'main.luau', 'abc'), Result);
    Assert.throws(() => ValidateDiagnostics(Result, 'other.luau', 'abc'), /envelope/);
    Assert.throws(() => ValidateDiagnostics({ ...Result, version: 2 }, 'main.luau', 'abc'), /envelope/);
    Assert.throws(() => ValidateDiagnostics({ ...Result, diagnostics: Array(257).fill(Result.diagnostics[0]) }, 'main.luau', 'abc'));
    const Bad = Position => ({ ...Result, diagnostics: [{ ...Result.diagnostics[0],
        range: { start: { line: 0, column: 1 }, end: Position } }] });
    for (const Position of [{ line: 0, column: 0 }, { line: 1, column: 0 }, { line: 0, column: 4 },
        { line: 0, column: -1 }, { line: 0, column: 1.5 }])
        Assert.throws(() => ValidateDiagnostics(Bad(Position), 'main.luau', 'abc'), /range/);
});

Test('type sessions hide processes, suppress stale saves and reject invalid/excessive output', () => {
    const Children = [];
    let Options;
    const Session = new TypeCheckSession((Executable, Arguments, Settings) => {
        Options = { Executable, Arguments, Settings };
        const Child = FakeChild(); Children.push(Child); return Child;
    });
    const Paths = { Manifest: Path.resolve('lui.json'), Cli: 'cli.dll', Checker: 'checker', Definitions: 'defs',
        SourceName: 'main.luau', Source: 'abc' };
    let Published = 0;
    let Failure = '';
    Session.on('result', () => ++Published);
    Session.on('failure', Message => { Failure = Message; });
    Session.Start(Paths);
    Assert.equal(Options.Executable, 'dotnet');
    Assert.equal(Options.Settings.windowsHide, true);
    Assert.equal(Options.Settings.shell, false);
    Assert.deepEqual(Options.Arguments, ['cli.dll', 'check', Paths.Manifest, '--checker', 'checker', '--definitions', 'defs', '--format', 'json']);
    Session.Start(Paths);
    Assert.equal(Children[0].Killed, true);
    Children[0].stdout.emit('data', Buffer.from(JSON.stringify(Result)));
    Children[0].emit('close', 1);
    Assert.equal(Published, 0);
    const BufferValue = Buffer.from(JSON.stringify(Result));
    Children[1].stdout.emit('data', BufferValue.subarray(0, 20));
    Children[1].stdout.emit('data', BufferValue.subarray(20));
    Children[1].emit('close', 1);
    Assert.equal(Published, 1);
    Session.Start(Paths);
    Children[2].stdout.emit('data', Buffer.from(JSON.stringify({ ...Result, source: '../secret.luau' })));
    Children[2].emit('close', 1);
    Assert.match(Failure, /envelope/);
    Session.Start(Paths);
    Children[3].stdout.emit('data', Buffer.alloc(4 * 1024 * 1024 + 1));
    Assert.equal(Children[3].Killed, true);
    Assert.match(Failure, /limit/);
    Session.Stop();
});

Test('editor publishes Problems independently of preview, clears errors on edit and respects trust', () => {
    const Directory = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'LuiTypeEditor-'));
    try {
        const Manifest = Path.join(Directory, 'lui.json');
        const Script = Path.join(Directory, 'main.luau');
        Fs.writeFileSync(Manifest, JSON.stringify({ SchemaVersion: 1, Script: 'main.luau' }));
        Fs.writeFileSync(Script, 'abc');
        Assert.equal(GetEntry(Manifest).Script, Script);
        const Settings = new Map([['cliPath', 'cli.dll'], ['checkerPath', 'checker.exe'], ['definitionsPath', 'defs.luau']]);
        for (const Name of Settings.values()) Fs.writeFileSync(Path.join(Directory, Name), 'test');
        const Collection = { Items: [], Disposed: false, set(Uri, Items) { this.Items = Items; this.Uri = Uri; },
            clear() { Assert.equal(this.Disposed, false); this.Items = []; }, dispose() { this.Disposed = true; } };
        const Vscode = {
            workspace: { isTrusted: false, textDocuments: [], getConfiguration: () => ({ get: (Key, Default) => Settings.get(Key) ?? Default }) },
            languages: { createDiagnosticCollection: () => Collection },
            Uri: { file: File => File }, DiagnosticSeverity: { Error: 0 },
            Range: class { constructor(...Arguments) { this.Arguments = Arguments; } },
            Diagnostic: class { constructor(Range, Message) { this.range = Range; this.message = Message; } },
        };
        const Session = new EventEmitter();
        let Started = 0;
        let Stopped = 0;
        Session.Start = () => ++Started;
        Session.Stop = () => ++Stopped;
        const Messages = [];
        const Controller = new TypeDiagnostics(Vscode, { appendLine: Text => Messages.push(Text) }, Session);
        const Folder = { uri: { fsPath: Directory } };
        Controller.Check(Folder, true);
        Assert.equal(Started, 0);
        Assert.match(Messages[0], /trusted workspace/);
        Vscode.workspace.isTrusted = true;
        Controller.Check(Folder, true);
        Assert.equal(Started, 1);
        Session.emit('result', Result);
        Assert.equal(Collection.Uri, Script);
        Assert.deepEqual(Collection.Items[0].range.Arguments, [0, 1, 0, 2]);
        Assert.equal(Collection.Items[0].source, 'LUI type');
        Controller.OnChange({ uri: { fsPath: Script } });
        Assert.equal(Collection.Items.length, 0);
        Assert.ok(Stopped >= 3);
        Vscode.workspace.textDocuments = [{ isDirty: true, uri: { fsPath: Script } }];
        Controller.Check(Folder, true);
        Assert.equal(Started, 1);
        Assert.match(Messages.at(-1), /Save/);
        Fs.writeFileSync(Manifest, JSON.stringify({ SchemaVersion: 1, Script: '../outside.luau' }));
        Assert.throws(() => GetEntry(Manifest), /left/);
        Settings.set('checkerPath', { Invalid: 'configuration' });
        Assert.doesNotThrow(() => Controller.Check(Folder, true));
        Assert.ok(Messages.at(-1).startsWith('[LUI:Types] '));
        Controller.Dispose();
        Assert.doesNotThrow(() => Controller.Dispose());
    } finally { Fs.rmSync(Directory, { recursive: true, force: true }); }
});

Test('real CLI/editor type checking rejects invalid LUI code, recovers, and never executes it', async Context => {
    if (!process.env.LUI_TEST_CLI || !process.env.LUI_TEST_TYPECHECK) {
        Context.skip('Set LUI_TEST_CLI and LUI_TEST_TYPECHECK'); return;
    }
    const Root = Path.resolve(__dirname, '../..');
    const Cli = Path.resolve(Root, process.env.LUI_TEST_CLI);
    const Checker = Path.resolve(Root, process.env.LUI_TEST_TYPECHECK);
    const Directory = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'LuiTypeIntegration-'));
    const Session = new TypeCheckSession();
    const Manifest = Path.join(Directory, 'lui.json');
    const Script = Path.join(Directory, 'main.luau');
    Fs.writeFileSync(Manifest, JSON.stringify({ SchemaVersion: 1, Script: 'main.luau', Capabilities: [], Extensions: [], Assets: [] }));
    async function Check(Source) {
        Fs.writeFileSync(Script, Source);
        return new Promise((Resolve, Reject) => {
            const Timer = setTimeout(() => { Cleanup(); Reject(new Error('Timed out waiting for type diagnostics')); }, 15000);
            const ResultHandler = Result => { Cleanup(); Resolve(Result); };
            const FailureHandler = Message => { Cleanup(); Reject(new Error(Message)); };
            const Cleanup = () => { clearTimeout(Timer); Session.off('result', ResultHandler); Session.off('failure', FailureHandler); };
            Session.once('result', ResultHandler); Session.once('failure', FailureHandler);
            Session.Start({ Manifest, Cli, Checker, Definitions: Path.join(Path.dirname(Checker), 'LUI.d.luau'), SourceName: 'main.luau', Source });
        });
    }
    try {
        const Invalid = await Check('local W = Instance.new("Window")\nW.AbsoluteSize = Vector2.new(1, 1)\n');
        Assert.match(Invalid.diagnostics[0].message, /read-only/);
        Assert.equal(Invalid.diagnostics[0].range.start.line, 1);
        const Valid = await Check('local W = Instance.new("Window")\nW.Title = "Recovered"\nerror("must not execute")\n');
        Assert.equal(Valid.diagnostics.length, 0);
    } finally { Session.Stop(); Fs.rmSync(Directory, { recursive: true, force: true }); }
});
