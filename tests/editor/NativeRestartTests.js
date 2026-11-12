'use strict';

const Test = require('node:test');
const Assert = require('node:assert/strict');
const Fs = require('node:fs');
const Os = require('node:os');
const Path = require('node:path');
const Module = require('node:module');
const { EventEmitter } = require('node:events');
const { PassThrough } = require('node:stream');
const { spawn } = require('node:child_process');
const { NativePreview } = require('../../vscode/NativePreview');

const Tick = () => new Promise(Resolve => setImmediate(Resolve));
function Deferred() {
    let Resolve;
    const PromiseValue = new Promise(Value => { Resolve = Value; });
    return { Promise: PromiseValue, Resolve };
}
function GetChild() {
    const Child = new EventEmitter();
    Child.stderr = new PassThrough();
    Child.Kills = 0;
    Child.Closed = false;
    Child.kill = () => { Child.Kills++; return true; };
    Child.Retire = () => {
        if (Child.Closed) return;
        Child.Closed = true;
        Child.emit('exit', 0, null);
        Child.stderr.destroy();
        Child.emit('close', 0, null);
    };
    return Child;
}

// Exercise registered commands without exporting controller internals. Only the
// VS Code boundary, SDK discovery and native spawn are replaced.
async function GetEditor() {
    const Root = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'LuiRestart-'));
    const Folder = { uri: { scheme: 'file', fsPath: Root } };
    const Host = Path.join(Root, process.platform === 'win32' ? 'Lui.WinUI.exe' : 'lui-gtk');
    Fs.writeFileSync(Host, 'Synthetic host');
    Fs.writeFileSync(Path.join(Root, 'LuiRuntime.dll'), 'Synthetic runtime');
    const PreviewHost = Path.join(Root, 'PreviewHost.exe'); Fs.writeFileSync(PreviewHost, 'Synthetic preview');
    Fs.writeFileSync(Path.join(Root, 'lui.json'), '{"SchemaVersion":2}');
    const Commands = new Map(), Contexts = new Map(), Logs = [], Children = [], Events = {};
    const Status = { hide() { this.Visible = false; }, show() { this.Visible = true; }, dispose() { this.Visible = false; } };
    const Disposable = () => ({ dispose() {} });
    const Subscribe = Name => Handler => { Events[Name] = Handler; return Disposable(); };
    const Sdk = { Enabled: false, Calls: 0, Resolve: async () => {}, Root: 'SyntheticSdk', Manifest: Path.join(Root, 'lui.json'), Binding: null, Previews: [] };
    const Vscode = {
        workspace: {
            workspaceFolders: [Folder], textDocuments: [], isTrusted: true,
            getWorkspaceFolder: Uri => Uri.fsPath.startsWith(Root + Path.sep) ? Folder : undefined,
            getConfiguration: () => ({ get: (Name, Default) => Name === 'nativeHostPath' ? Host : Name === 'manifestPath' ? Sdk.Manifest : Name === 'previewHostPath' ? PreviewHost : Name === 'runtimePath' ? Path.join(Root, 'LuiRuntime.dll') : Default }),
            onDidSaveTextDocument: Subscribe('Save'), onDidOpenTextDocument: Subscribe('Open'),
            onDidChangeTextDocument: Subscribe('Change'), onDidChangeConfiguration: Subscribe('Configuration'),
            onDidChangeWorkspaceFolders: Subscribe('Folders'),
        },
        window: {
            createOutputChannel: () => ({ appendLine: Message => Logs.push(Message), dispose() {} }),
            createStatusBarItem: () => Status,
            setStatusBarMessage: Message => Logs.push(Message),
            createTreeView: () => ({ onDidChangeSelection: Subscribe('Selection'), dispose() {} }),
            registerWebviewViewProvider: (Name, Provider) => { Sdk.Inspector = Provider; return Disposable(); },
        },
        languages: { createDiagnosticCollection: () => ({ clear() {}, set() {}, delete() {}, dispose() {} }), registerCodeActionsProvider: Disposable },
        StatusBarAlignment: { Left: 1 },
        commands: {
            registerCommand: (Name, Handler) => { Commands.set(Name, Handler); return Disposable(); },
            executeCommand: async (Name, ...Arguments) => {
                if (Name === 'setContext') { Contexts.set(...Arguments); return; }
                Assert(Commands.has(Name), 'Unknown command ' + Name);
                return Commands.get(Name)(...Arguments);
            },
        },
        EventEmitter: class { constructor() { this.event = Subscribe('Tree'); } fire() {} dispose() {} },
    };
    const ExtensionPath = require.resolve('../../vscode/Extension');
    const NativePath = require.resolve('../../vscode/NativePreview');
    const PreviousExtension = require.cache[ExtensionPath], PreviousNative = require.cache[NativePath];
    const OriginalLoad = Module._load;
    let Extension;
    try {
        delete require.cache[ExtensionPath]; delete require.cache[NativePath];
        Module._load = function(Request, Parent, ...Arguments) {
            if (Request === 'vscode') return Vscode;
            if (Parent?.filename === ExtensionPath && Request === './PreviewClient') return {
                PreviewClient: class extends EventEmitter {
                    constructor(Manifest, Host, Runtime) {
                        super();
                        if (Sdk.PreviewThrow) throw new Error('PRIVATE_PREVIEW_CONSTRUCTOR');
                        this.Manifest = Manifest; this.Host = Host; this.Runtime = Runtime;
                        this.Tree = { Nodes: [], ById: new Map(), Children: new Map([[0, []]]) };
                        this.Classes = new Map(); this.Generation = 0; this.Version = 2;
                        Sdk.Previews.push(this);
                    }
                    InputTarget() { return false; }
                    Stop() { this.Stopped = true; }
                },
            };
            if (Parent?.filename === ExtensionPath && Request === './SdkClient') return {
                UsesSdk: () => Sdk.Enabled, GetBinding: () => Sdk.Binding,
                GetRoot: () => Sdk.Root, GetManifest: () => Path.resolve(Sdk.Manifest),
                ResolveSdk: async () => { Sdk.Calls++; return Sdk.Resolve(); },
            };
            if (Parent?.filename === NativePath && Request === 'node:child_process') return {
                spawn: (Host, Arguments) => {
                    Assert.equal(Children.filter(Child => !Child.Closed).length, 0, 'Native generations overlap');
                    const Child = GetChild(); Child.Arguments = Arguments; Children.push(Child);
                    if (!Sdk.HoldSpawn) queueMicrotask(() => Child.emit('spawn'));
                    return Child;
                },
            };
            return OriginalLoad.call(this, Request, Parent, ...Arguments);
        };
        Extension = require(ExtensionPath);
    } finally {
        Module._load = OriginalLoad;
        if (PreviousExtension) require.cache[ExtensionPath] = PreviousExtension; else delete require.cache[ExtensionPath];
        if (PreviousNative) require.cache[NativePath] = PreviousNative; else delete require.cache[NativePath];
    }
    await Extension.activate({ subscriptions: [], globalState: { get() {} } });
    return { Root, Folder, Vscode, Contexts, Logs, Children, Events, Sdk, Status,
        Run: Name => Vscode.commands.executeCommand('lui.' + Name),
        Dispose: () => Extension.deactivate(),
        Cleanup: () => {
            Extension.deactivate();
            for (const Child of Children) Child.Retire();
            Fs.rmSync(Root, { recursive: true, force: true });
        },
    };
}

Test('Restart App retires one generation, coalesces repeated requests and rejects old output', async () => {
    const Editor = await GetEditor();
    try {
        await Editor.Run('openNativePreview');
        Assert.equal(Editor.Contexts.get('lui.nativeRunning'), true);
        const Old = Editor.Children[0];
        const First = Editor.Run('restartNativePreview'), Latest = Editor.Run('restartNativePreview');
        await Tick();
        Assert.equal(Old.Kills, 1);
        Assert.equal(Editor.Children.length, 1);
        Assert.equal(Editor.Contexts.get('lui.nativeStarting'), true);
        const DuringStop = Editor.Logs.length;
        const Diagnostic = '[LUI:Diagnostic] {"Version":1,"Code":"NetworkServerRequired","Phase":"Runtime"}\n';
        Old.stderr.emit('data', Diagnostic);
        Assert.equal(Editor.Logs.length, DuringStop);
        Old.Retire();
        await Promise.all([First, Latest]);
        Assert.equal(Editor.Children.length, 2);
        Assert.equal(Editor.Contexts.get('lui.nativeRunning'), true);
        Assert.equal(Editor.Contexts.get('lui.nativeStarting'), false);
        const Count = Editor.Logs.length;
        Old.stderr.emit('data', Diagnostic); Old.stderr.emit('data', 'PRIVATE_RETIRED_CANARY\n'); Old.emit('close', 9, null);
        Assert.equal(Editor.Logs.length, Count);
        Assert.equal(Editor.Contexts.get('lui.nativeRunning'), true);
    } finally { Editor.Cleanup(); }
});

Test('inspector discovery is canceled by settings changes, Stop and workspace removal', async () => {
    for (const Change of ['Manifest', 'Configuration', 'Stop', 'Folder', 'Dispose']) {
        const Editor = await GetEditor();
        try {
            Editor.Sdk.Enabled = true;
            const Pending = Deferred(); Editor.Sdk.Resolve = () => Pending.Promise;
            const Launch = Editor.Run('startPreview');
            if (Change === 'Manifest') Editor.Sdk.Manifest = Path.join(Editor.Root, 'Other.json');
            if (Change === 'Configuration') Editor.Events.Configuration({ affectsConfiguration: () => true });
            if (Change === 'Stop') await Editor.Run('stopPreview');
            if (Change === 'Folder') { Editor.Vscode.workspace.workspaceFolders = []; Editor.Events.Folders({ removed: [Editor.Folder], added: [] }); }
            if (Change === 'Dispose') Editor.Dispose();
            Pending.Resolve(); await Launch;
            Assert.equal(Editor.Sdk.Previews.length, 0, Change);
        } finally { Editor.Cleanup(); }
    }
});

Test('inspector setup failures settle with fixed messages and retired clients cannot publish', async () => {
    const Editor = await GetEditor();
    try {
        Fs.writeFileSync(Editor.Sdk.Manifest, '{"PRIVATE_MANIFEST_CANARY');
        await Assert.doesNotReject(Editor.Run('startPreview'));
        Assert.equal(Editor.Sdk.Previews.length, 0);
        Assert(!JSON.stringify(Editor.Sdk.Inspector.State).includes('PRIVATE_'));
        Fs.writeFileSync(Editor.Sdk.Manifest, '{"SchemaVersion":2}'); Editor.Sdk.PreviewThrow = true;
        await Assert.doesNotReject(Editor.Run('startPreview'));
        Assert(!JSON.stringify(Editor.Sdk.Inspector.State).includes('PRIVATE_'));
        Editor.Sdk.PreviewThrow = false; await Editor.Run('startPreview');
        const Client = Editor.Sdk.Previews[0]; Assert(Client);
        await Editor.Run('stopPreview');
        await Editor.Run('startPreview');
        const Replacement = Editor.Sdk.Previews[1]; Assert(Replacement);
        const State = JSON.stringify(Editor.Sdk.Inspector.State), Count = Editor.Logs.length;
        Client.emit('diagnostic', 'PRIVATE_RETIRED_INSPECTOR');
        Client.emit('runtimeError', 'PRIVATE_RETIRED_INSPECTOR');
        Client.emit('failure', 'PRIVATE_RETIRED_INSPECTOR');
        Client.emit('consoleMessage', 'PRIVATE_RETIRED_INSPECTOR');
        Client.emit('tree', { Nodes: [], ById:new Map(), Children:new Map() });
        Client.emit('exit', { Code: 0, WasClosed:false });
        Assert.equal(Editor.Logs.length, Count); Assert.equal(JSON.stringify(Editor.Sdk.Inspector.State), State);
    } finally { Editor.Cleanup(); }
});

Test('Stop App cancels a restart while the old app is retiring', async () => {
    const Editor = await GetEditor();
    try {
        await Editor.Run('openNativePreview');
        const Restart = Editor.Run('restartNativePreview'), Stop = Editor.Run('stopNativePreview');
        Editor.Children[0].Retire();
        await Promise.all([Restart, Stop]);
        Assert.equal(Editor.Children.length, 1);
        Assert.equal(Editor.Contexts.get('lui.nativeStarting'), false);
        Assert.equal(Editor.Contexts.get('lui.nativeRunning'), false);
    } finally { Editor.Cleanup(); }
});

Test('Stop and disposal invalidate pending SDK discovery; each later launch validates again', async () => {
    const Editor = await GetEditor();
    try {
        Editor.Sdk.Enabled = true;
        let Pending = Deferred(); Editor.Sdk.Resolve = () => Pending.Promise;
        let Launch = Editor.Run('openNativePreview');
        await Editor.Run('stopNativePreview'); Pending.Resolve(); await Launch;
        Assert.equal(Editor.Children.length, 0);
        Editor.Sdk.Resolve = async () => {};
        await Editor.Run('openNativePreview');
        Assert.equal(Editor.Sdk.Calls, 2);
        const Restart = Editor.Run('restartNativePreview'); Editor.Children[0].Retire(); await Restart;
        Assert.equal(Editor.Sdk.Calls, 3);
        const Stop = Editor.Run('stopNativePreview'); Editor.Children[1].Retire(); await Stop;
        Pending = Deferred(); Editor.Sdk.Resolve = () => Pending.Promise;
        Launch = Editor.Run('openNativePreview'); Editor.Dispose(); Pending.Resolve(); await Launch;
        Assert.equal(Editor.Children.length, 2);
    } finally { Editor.Cleanup(); }
});

Test('dirty project files refuse restart before stop and edits made during discovery refuse launch', async () => {
    const Editor = await GetEditor();
    try {
        const Document = { isDirty: true, uri: { scheme: 'file', fsPath: Path.join(Editor.Root, 'Src', 'Main.luau') } };
        Editor.Vscode.workspace.textDocuments.push(Document);
        await Editor.Run('openNativePreview'); Assert.equal(Editor.Children.length, 0);
        Document.isDirty = false; await Editor.Run('openNativePreview');
        Document.isDirty = true; await Editor.Run('restartNativePreview'); Assert.equal(Editor.Children[0].Kills, 0);
        Document.uri.fsPath = Editor.Root + '-Other' + Path.sep + 'Main.luau';
        const Restart = Editor.Run('restartNativePreview'); Editor.Children[0].Retire(); await Restart;
        Assert.equal(Editor.Children.length, 2);
        const Stop = Editor.Run('stopNativePreview'); Editor.Children[1].Retire(); await Stop;
        const Pending = Deferred(); Editor.Sdk.Enabled = true; Editor.Sdk.Resolve = () => Pending.Promise;
        const Launch = Editor.Run('openNativePreview'); Document.uri.fsPath = Path.join(Editor.Root, 'Main.luau');
        Pending.Resolve(); await Launch; Assert.equal(Editor.Children.length, 2);
        Assert(Editor.Logs.some(Message => Message.includes('Save project files')));
    } finally { Editor.Cleanup(); }
});

Test('changed manifest, SDK or host selection cannot launch from an earlier discovery', async () => {
    const Editor = await GetEditor();
    try {
        Editor.Sdk.Enabled = true;
        let Pending = Deferred(); Editor.Sdk.Resolve = () => Pending.Promise;
        let Launch = Editor.Run('openNativePreview');
        const Other = Path.join(Editor.Root, 'Other.json'); Fs.writeFileSync(Other, '{"SchemaVersion":2}');
        // A prior binding for another manifest must not substitute for this request.
        Editor.Sdk.Manifest = Other;
        Editor.Sdk.Binding = { Components: { NativeHost: Path.join(Editor.Root, process.platform === 'win32' ? 'Lui.WinUI.exe' : 'lui-gtk') } };
        Pending.Resolve(); await Launch; Assert.equal(Editor.Children.length, 0);
        Pending = Deferred(); Editor.Sdk.Resolve = () => Pending.Promise;
        Launch = Editor.Run('openNativePreview'); Editor.Sdk.Root = 'ChangedSdk';
        Pending.Resolve(); await Launch; Assert.equal(Editor.Children.length, 0);
        Pending = Deferred(); Editor.Sdk.Resolve = () => Pending.Promise;
        Launch = Editor.Run('openNativePreview');
        const Previous = Editor.Vscode.workspace.getConfiguration;
        Editor.Vscode.workspace.getConfiguration = () => ({ get: (Name, Default) => Name === 'nativeHostPath' ? 'ChangedHost' : Previous().get(Name, Default) });
        Pending.Resolve(); await Launch; Assert.equal(Editor.Children.length, 0);
    } finally { Editor.Cleanup(); }
});

Test('Restart App keeps its running project when another workspace folder becomes active', async () => {
    const Editor = await GetEditor();
    try {
        await Editor.Run('openNativePreview');
        const Other = { uri: { scheme: 'file', fsPath: Editor.Root + '-Other' } };
        Editor.Vscode.workspace.workspaceFolders.push(Other);
        const Document = { isDirty: true, uri: { scheme: 'file', fsPath: Path.join(Other.uri.fsPath, 'Main.luau') } };
        Editor.Vscode.workspace.textDocuments.push(Document);
        Editor.Vscode.window.activeTextEditor = { document: Document };
        Editor.Events.Change({ document: Document, contentChanges: [{ text: 'Other project' }] });
        Assert.equal(Editor.Status.text, '$(play) LUI · Running');
        const Restart = Editor.Run('restartNativePreview'); Editor.Children[0].Retire(); await Restart;
        Assert.equal(Editor.Children.length, 2);
        Assert.deepEqual(Editor.Children[1].Arguments, ['--project', Path.join(Editor.Root, 'lui.json')]);
    } finally { Editor.Cleanup(); }
});

Test('invalid launch configuration reports fixed advice without stopping the current app', async () => {
    const Editor = await GetEditor();
    try {
        await Editor.Run('openNativePreview'); Editor.Sdk.Manifest = 17;
        await Assert.doesNotReject(Editor.Run('restartNativePreview'));
        Assert.equal(Editor.Children[0].Kills, 0);
        Assert(Editor.Logs.some(Message => Message.includes('Could not read launch settings.')));
        Assert(!Editor.Logs.some(Message => Message.includes('ERR_INVALID_ARG_TYPE')));
    } finally { Editor.Cleanup(); }
});

Test('persistent app status distinguishes preparation, running edits, restart, stop and disposal', async () => {
    const Editor = await GetEditor();
    try {
        Assert.equal(Editor.Status.Visible, false);
        const Pending = Deferred(); Editor.Sdk.Enabled = true; Editor.Sdk.Resolve = () => Pending.Promise;
        const Launch = Editor.Run('openNativePreview'); Assert(Editor.Status.text.includes('Preparing app'));
        Assert.equal(Editor.Status.command, 'lui.stopNativePreview');
        Pending.Resolve(); await Launch; Assert.equal(Editor.Status.text, '$(play) LUI · Running');
        const Document = { isDirty: true, uri: { scheme: 'file', fsPath: Path.join(Editor.Root, 'Main.luau') } };
        Editor.Vscode.workspace.textDocuments.push(Document);
        Editor.Events.Change({ document: Document, contentChanges: [{ text: 'Edit' }] });
        Assert(Editor.Status.text.includes('Unsaved project edits'));
        Document.isDirty = false; Editor.Events.Save(Document);
        Assert(Editor.Status.text.includes('Restart after project edits'));
        Assert.equal(Editor.Status.command, 'lui.restartNativePreview');
        const Restart = Editor.Run('restartNativePreview'); Assert(Editor.Status.text.includes('Restarting app'));
        Editor.Children[0].Retire(); await Restart; Assert.equal(Editor.Status.text, '$(play) LUI · Running');
        const Stop = Editor.Run('stopNativePreview'); Assert(Editor.Status.text.includes('Stopping app'));
        Editor.Children[1].Retire(); await Stop; Assert(Editor.Status.text.includes('App stopped'));
        Editor.Dispose(); Assert.equal(Editor.Status.Visible, false);
    } finally { Editor.Cleanup(); }
});

Test('Stop remains available before spawn and an exited process is shown as finishing shutdown', async () => {
    const Editor = await GetEditor();
    try {
        Editor.Sdk.HoldSpawn = true;
        await Editor.Run('openNativePreview');
        Assert.equal(Editor.Contexts.get('lui.nativeStarting'), true);
        Assert(Editor.Status.text.includes('Starting app')); Assert.equal(Editor.Status.command, 'lui.stopNativePreview');
        const Stop = Editor.Run('stopNativePreview'); Editor.Children[0].emit('spawn');
        Assert.equal(Editor.Contexts.get('lui.nativeRunning'), false);
        Editor.Children[0].Retire(); await Stop;
        Editor.Sdk.HoldSpawn = false; await Editor.Run('openNativePreview');
        Editor.Children[1].emit('exit', 0, null);
        Assert(Editor.Status.text.includes('Finishing app shutdown'));
        Assert.equal(Editor.Contexts.get('lui.nativeRunning'), false);
        Assert.equal(Editor.Contexts.get('lui.nativeStarting'), true);
        await Editor.Run('openNativePreview');
        Assert(Editor.Logs.at(-1).includes('finishing shutdown'));
        Editor.Children[1].Retire(); Assert(Editor.Status.text.includes('App stopped'));
    } finally { Editor.Cleanup(); }
});

Test('workspace removal and revoked trust prevent replacement after retirement', async () => {
    const Editor = await GetEditor();
    try {
        await Editor.Run('openNativePreview');
        let Restart = Editor.Run('restartNativePreview'); Editor.Vscode.workspace.isTrusted = false;
        Editor.Children[0].Retire(); await Restart; Assert.equal(Editor.Children.length, 1);
        Editor.Vscode.workspace.isTrusted = true; await Editor.Run('openNativePreview');
        Restart = Editor.Run('restartNativePreview'); Editor.Vscode.workspace.workspaceFolders = [];
        Editor.Events.Folders({ removed: [Editor.Folder], added: [] });
        Editor.Children[1].Retire(); await Restart; Assert.equal(Editor.Children.length, 2);
    } finally { Editor.Cleanup(); }
});

Test('failed termination never launches a replacement and does not expose exception text', async () => {
    const Editor = await GetEditor();
    try {
        await Editor.Run('openNativePreview');
        Editor.Children[0].kill = () => { throw new Error('PRIVATE_KILL_CANARY'); };
        await Editor.Run('restartNativePreview'); Assert.equal(Editor.Children.length, 1);
        Assert(Editor.Status.text.includes('Stop failed'));
        Assert.equal(Editor.Status.command, 'lui.stopNativePreview');
        Assert(Editor.Logs.some(Message => Message.includes('Could not stop')));
        Assert(!Editor.Logs.some(Message => Message.includes('PRIVATE_KILL_CANARY')));
    } finally { Editor.Cleanup(); }
});

Test('StopAsync waits for a real owned child process to exit', { timeout: 10000 }, async () => {
    const Root = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'LuiStopProcess-'));
    Fs.writeFileSync(Path.join(Root, 'lui.json'), '{"SchemaVersion":2}');
    const Native = new NativePreview(Path.join(Root, 'lui.json'), process.execPath,
        (Host, Arguments, Options) => spawn(Host, ['-e', 'setInterval(()=>{},1000)'], { ...Options, windowsHide: true }));
    try {
        const Started = new Promise(Resolve => Native.once('started', Resolve));
        Native.Start(); await Started;
        const Child = Native.Process;
        Assert.equal(await Native.StopAsync(), true);
        Assert.equal(Native.Process, null);
        Assert(Child.exitCode !== null || Child.signalCode !== null);
    } finally { if (Native.Process) { Native.Stop(); await Native.Retirement; } Fs.rmSync(Root, { recursive: true, force: true }); }
});

Test('native preview retires an exited owner that spawned a real descendant', { timeout: 10000 }, async () => {
    const Root = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'LuiStopDrain-'));
    Fs.writeFileSync(Path.join(Root, 'lui.json'), '{"SchemaVersion":2}');
    let Descendant;
    const Script = "const Child=require('node:child_process').spawn(process.execPath,['-e',\"process.stdout.write('Ready');setTimeout(()=>{},8000)\"],{stdio:['ignore','pipe',2],windowsHide:true});Child.stdout.once('data',()=>{process.stderr.write('OwnedPID:'+Child.pid+'\\n',()=>{Child.unref();process.exit(0);});});";
    const Native = new NativePreview(Path.join(Root, 'lui.json'), process.execPath, (Host, Arguments, Options) => {
        const Child = spawn(Host, ['-e', Script], { ...Options, windowsHide: true });
        Child.stderr.on('data', Data => { const Match = /OwnedPID:(\d+)/.exec(String(Data)); if (Match) Descendant = Number(Match[1]); });
        return Child;
    });
    try {
        const Exited = new Promise(Resolve => Native.once('exit', Resolve));
        const Start = Date.now(); Native.Start(); await Exited;
        Assert(Descendant, 'Descendant PID was not observed');
        Assert(Date.now() - Start < 6000, 'Generation waited for the descendant to exit');
        Assert.equal(Native.Process, null);
    } finally {
        if (Descendant) { try { process.kill(Descendant); } catch (Failure) { if (Failure.code !== 'ESRCH') throw Failure; } }
        Fs.rmSync(Root, { recursive: true, force: true });
    }
});

Test('an exited child with an open stderr pipe retires after the bounded diagnostic drain', { timeout: 5000 }, async () => {
    const Root = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'LuiOpenDrain-'));
    Fs.writeFileSync(Path.join(Root, 'lui.json'), '{"SchemaVersion":2}');
    const Child = GetChild();
    const Native = new NativePreview(Path.join(Root, 'lui.json'), 'Synthetic', () => Child);
    try {
        const Exited = new Promise(Resolve => Native.once('exit', Resolve));
        Native.Start(); const Start = Date.now(); Child.emit('exit', 0, null);
        Assert.equal(Native.Process, Child); Assert.equal(Child.stderr.destroyed, false);
        await Exited;
        Assert(Date.now() - Start >= 900);
        Assert.equal(Child.stderr.destroyed, true); Assert.equal(Native.Process, null);
        Assert.equal(Child.Kills, 0);
    } finally { Child.Retire(); Fs.rmSync(Root, { recursive: true, force: true }); }
});

Test('StopAsync timeout keeps the old generation bound and permits a later stop retry', { timeout: 10000 }, async () => {
    const Root = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'LuiStopTimeout-'));
    Fs.writeFileSync(Path.join(Root, 'lui.json'), '{"SchemaVersion":2}');
    const Child = GetChild(), Failures = [];
    const Native = new NativePreview(Path.join(Root, 'lui.json'), 'Synthetic', () => Child);
    Native.on('failure', Message => Failures.push(Message));
    try {
        Native.Start();
        Assert.equal(await Native.StopAsync(), false);
        Assert.equal(Native.StopFailed, true);
        Assert.equal(Native.Process, Child); Assert.equal(Native.Start(), false);
        Assert.deepEqual(Failures, ['App did not stop. Close its window and try again.']);
        const Retry = Native.StopAsync(); Child.Retire(); Assert.equal(await Retry, true);
        Assert.equal(Child.Kills, 2);
        Assert.equal(Native.StopFailed, false);
    } finally { Child.Retire(); Fs.rmSync(Root, { recursive: true, force: true }); }
});
