'use strict';

const Test = require('node:test');
const Assert = require('node:assert/strict');
const Fs = require('node:fs');
const Os = require('node:os');
const Path = require('node:path');
const { EventEmitter } = require('node:events');
const { PreviewClient } = require('../../vscode/PreviewClient');
const { ValidateSchema, ValidateTree, GetProperties } = require('../../vscode/PreviewModel');
const { NativePreview, ResolveNativeHost } = require('../../vscode/NativePreview');

function WaitFor(Emitter, Event, Predicate = () => true) {
    return new Promise((Resolve, Reject) => {
        const Timer = setTimeout(() => { Cleanup(); Reject(new Error('Timed out waiting for ' + Event)); }, 10000);
        const Handler = (...Arguments) => { if (Predicate(...Arguments)) { Cleanup(); Resolve(Arguments); } };
        const Failure = Message => { Cleanup(); Reject(new Error(Message)); };
        const Cleanup = () => {
            clearTimeout(Timer);
            Emitter.off(Event, Handler);
            Emitter.off('failure', Failure);
        };
        Emitter.on(Event, Handler);
        if (Event !== 'failure') Emitter.on('failure', Failure);
    });
}

Test('reflection metadata drives inspector properties and invalid trees fail closed', () => {
    const Schema = JSON.parse(Fs.readFileSync(Path.join(__dirname, '../../types/schema.json'), 'utf8'));
    const Classes = ValidateSchema(Schema);
    const Window = { id: 1, parentId: 0, className: 'Window', name: 'Main', title: 'Demo',
        text: '', source: '', accessibilityLabel: '', accessibilityDescription: '',
        visible: true, enabled: true, checked: false, isFocused: false,
        minimum: 0, maximum: 100, value: 0,
        bounds: { x: 0, y: 0, width: 400, height: 300 } };
    const Frame = { ...Window, id: 2, parentId: 1, className: 'Frame', name: 'Root' };
    const Tree = ValidateTree([Window, Frame], Classes);
    Assert.equal(Tree.Children.get(1)[0], 2);
    const Properties = GetProperties(Window, Classes);
    Assert.equal(Properties.find(Property => Property.name === 'Title').value, 'Demo');
    Assert.equal(Properties.find(Property => Property.name === 'Resolved bounds').value, '0, 0 · 400 × 300');
    Assert.throws(() => ValidateTree([{ ...Window, parentId: 2 }, { ...Frame, parentId: 1 }], Classes), /cycle/);
    Assert.throws(() => ValidateTree([{ ...Window, bounds: { ...Window.bounds, width: -1 } }], Classes), /bounds/);
    Assert.throws(() => ValidateTree([{ ...Window, className: 'Unknown' }], Classes), /class/);
});

Test('GTK preview resolves Linux hosts and enforces modular launch',()=>{
    const Root=Fs.mkdtempSync(Path.join(Os.tmpdir(),'LuiGtkPreview-'));
    try {
        Fs.mkdirSync(Path.join(Root,'bin')); const Host=Path.join(Root,'bin/lui-gtk'), Manifest=Path.join(Root,'lui.json');
        Fs.writeFileSync(Host,'Synthetic'); Fs.writeFileSync(Manifest,JSON.stringify({SchemaVersion:2}));
        Assert.equal(ResolveNativeHost(Root,'linux'),Host);
        const Child=new EventEmitter(); Child.kill=()=>true; let Started;
        const Preview=new NativePreview(Manifest,Host,(Exe,Arguments)=>{Started={Exe,Arguments}; return Child;},'linux');
        Preview.Start(); Assert.deepEqual(Started,{Exe:Host,Arguments:['--project',Manifest]}); Preview.Stop();
        Fs.writeFileSync(Manifest,JSON.stringify({SchemaVersion:1}));
        Assert.throws(()=>new NativePreview(Manifest,Host,()=>Child,'linux').Start(),/schema v2/);
        Fs.writeFileSync(Manifest,'{"PRIVATE_PARSE_CANARY');
        Assert.throws(()=>new NativePreview(Manifest,Host,()=>Child,'linux').Start(),Error=>Error.message==='[LUI:NativePreview] InvalidManifest');
    } finally {Fs.rmSync(Root,{recursive:true,force:true});}
});

Test('native preview uses the WinUI host and tracks its process lifecycle', () => {
    const Directory = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'LuiNativePreviewTest-'));
    try {
        const Host = Path.join(Directory, 'Lui.WinUI.exe');
        const Manifest = Path.join(Directory, 'lui.json');
        Fs.writeFileSync(Host, 'test host');
        Fs.writeFileSync(Path.join(Directory, 'LuiRuntime.dll'), 'test runtime');
        Fs.writeFileSync(Manifest, '{}');
        Assert.equal(ResolveNativeHost(Directory,'win32'), Host);
        Assert.equal(ResolveNativeHost(Host,'win32'), Host);
        Fs.rmSync(Path.join(Directory, 'LuiRuntime.dll'));
        Assert.throws(() => ResolveNativeHost(Host,'win32'), /LuiRuntime/);
        Fs.writeFileSync(Path.join(Directory, 'LuiRuntime.dll'), 'test runtime');

        const Child = new EventEmitter();
        Child.kill = () => { Child.emit('close', null, 'SIGTERM'); return true; };
        let Spawned;
        const Native = new NativePreview(Manifest, Host, (Exe, Arguments, Options) => {
            Spawned = { Exe, Arguments, Options };
            return Child;
        },'win32');
        let Exit;
        Native.on('exit', Value => { Exit = Value; });
        Assert.equal(Native.Start(), true);
        Assert.equal(Native.Start(), false);
        Assert.equal(Spawned.Exe, Host);
        Assert.deepEqual(Spawned.Arguments, ['--manifest', Manifest]);
        Assert.equal(Spawned.Options.cwd, Directory);
        Assert.equal(Spawned.Options.windowsHide, false);
        Assert.equal(Native.Stop(), true);
        Assert.equal(Exit.WasStopped, true);
        Assert.equal(Native.Process, null);

        const FailedChild = new EventEmitter();
        const Failed = new NativePreview(Manifest, Host, () => FailedChild,'win32');
        let Failure;
        let FailedExit;
        Failed.on('failure', Message => { Failure = Message; });
        Failed.on('exit', Value => { FailedExit = Value; });
        Failed.Start();
        FailedChild.emit('error', new Error('spawn denied'));
        FailedChild.emit('close', -2, null);
        Assert.match(Failure, /spawn denied/);
        Assert.equal(FailedExit.LaunchFailed, true);
        Assert.equal(Failed.Process, null);
    } finally { Fs.rmSync(Directory, { recursive: true, force: true }); }
});

Test('editor client consumes the real preview host and reloads the tree', async Context => {
    const Host = process.env.LUI_TEST_PREVIEW_HOST;
    const Runtime = process.env.LUI_TEST_RUNTIME;
    if (!Host || !Runtime) { Context.skip('Set LUI_TEST_PREVIEW_HOST and LUI_TEST_RUNTIME'); return; }
    const Directory = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'LuiEditorTest-'));
    const Manifest = Path.join(Directory, 'lui.json');
    Fs.writeFileSync(Manifest, JSON.stringify({ SchemaVersion: 1, Script: 'main.luau',
        Capabilities: [], Extensions: [], Assets: [] }));
    Fs.writeFileSync(Path.join(Directory, 'main.luau'),
        'local W = Instance.new("Window", {Title="Editor", Size=UDim2.fromOffset(400,300)})\n' +
        'local B = Instance.new("TextButton", {Text="Go", Parent=W})\n' +
        'B.Activated:Connect(function() B.Text="Clicked" end)\n' +
        'B.InputBegan:Connect(function(E) B.Text=E.Device=="Keyboard" and "Key:"..E.Key or "Pointer:"..E.Position.X end)\nW.Visible=true\n');
    const ResolveInput = Value => Path.isAbsolute(Value) ? Value : Path.resolve(__dirname, '../..', Value);
    const Client = new PreviewClient(Manifest, ResolveInput(Host), ResolveInput(Runtime));
    try {
        const [First, Generation] = await WaitFor(Client, 'tree', (_, Value) => Value === 1);
        Assert.equal(Generation, 1);
        Assert.equal(First.Nodes[0].bounds.width, 400);
        Assert.equal(First.Nodes[0].createdAt.line, 1);
        Assert.equal(First.Nodes[1].createdAt.line, 2);
        Assert.equal(Client.Classes.get('Window').name, 'Window');
        const Activated = WaitFor(Client, 'tree', Tree => Tree.Nodes[1]?.text === 'Clicked');
        Client.Activate(2);
        Assert.equal((await Activated)[0].Nodes[1].lastChangedAt.property, 'Text');
        Assert.equal(Client.InputSupported, true);
        const Focused = WaitFor(Client, 'tree', Tree => Tree.Nodes[1]?.isFocused);
        Assert.equal(Client.Focus(2, true), true);
        await Focused;
        const Key = WaitFor(Client, 'tree', Tree => Tree.Nodes[1]?.text === 'Key:A');
        Assert.equal(Client.KeyInput(2, 'Began', 'A'), true);
        await Key;
        const Pointer = WaitFor(Client, 'tree', Tree => Tree.Nodes[1]?.text === 'Pointer:12');
        Assert.equal(Client.PointerInput(2, 'Began', 0, 1, 12, 0), true);
        await Pointer;
        const Blurred = WaitFor(Client, 'tree', Tree => !Tree.Nodes[1]?.isFocused);
        Client.ResetInput();
        await Blurred;
        const Reloaded = WaitFor(Client, 'tree', (_, Value) => Value === 2);
        Client.RequestReload();
        const [Second] = await Reloaded;
        Assert.equal(Second.Nodes[1].text, 'Go');
        const Resized = WaitFor(Client, 'tree', Tree => Tree.Nodes[0]?.bounds.width === 500);
        Client.ResizeViewport(1, 500, 200);
        await Resized;
        const Diagnostic = WaitFor(Client, 'diagnostic', Message => Message.includes('Stale preview generation'));
        Client.Process.stdin.write('{"version":1,"type":"activate","generation":1,"id":2}\n');
        await Diagnostic;
        Fs.writeFileSync(Path.join(Directory, 'main.luau'), 'error("expected preview failure")\n');
        const RuntimeError = WaitFor(Client, 'runtimeError', (_, Generation) => Generation === 3);
        const EmptyTree = WaitFor(Client, 'tree', (_, Generation) => Generation === 3);
        Client.RequestReload();
        const [ErrorMessage, , ErrorLocation] = await RuntimeError;
        Assert.match(ErrorMessage, /expected preview failure/);
        Assert.equal(ErrorLocation.line, 1);
        Assert.equal(ErrorLocation.source, 'main.luau');
        Assert.equal((await EmptyTree)[0].Nodes.length, 0);
        Fs.writeFileSync(Path.join(Directory, 'main.luau'), 'Instance.new("Window", {Title="Recovered"})\n');
        const Recovered = WaitFor(Client, 'tree', (_, Generation) => Generation === 4);
        Client.RequestReload();
        Assert.equal((await Recovered)[0].Nodes[0].title, 'Recovered');
        const Exited = WaitFor(Client, 'exit');
        Client.Stop();
        await Exited;
    } finally {
        Client.Stop();
        Fs.rmSync(Directory, { recursive: true, force: true });
    }
});

Test('input client rejects non-GUI targets, invalid fields, old hosts, and reload transactions', () => {
    const Client = Object.create(PreviewClient.prototype);
    Client.InputSupported = true;
    Client.ReloadPending = false;
    Client.Tree = { ById: new Map([[1, {className:'Window'}], [2, {className:'TextButton'}]]) };
    Client.Classes = new Map([['TextButton', {name:'TextButton',base:'GuiObject'}], ['GuiObject', {name:'GuiObject',base:''}],
        ['Window', {name:'Window',base:'Instance'}]]);
    const Sent = [];
    Client.Send = (Type, Fields) => { Sent.push({Type, Fields}); return true; };
    Assert.equal(Client.Focus(1, true), false);
    Assert.equal(Client.Focus(2, 1), false);
    Assert.equal(Client.PointerInput(2, 'Began', 0, 32, 0, 0), false);
    Assert.equal(Client.PointerInput(2, 'Began', 0, 1, Infinity, 0), false);
    Assert.equal(Client.KeyInput(2, 'Canceled', 'A'), false);
    Assert.equal(Client.KeyInput(2, 'Began', 'A\0junk'), false);
    Assert.equal(Sent.length, 0);
    Assert.equal(Client.Focus(2, true), true);
    Assert.equal(Client.PointerInput(2, 'Canceled', 0, 1, 0, 0), true);
    Client.ReloadPending = true;
    Assert.equal(Client.KeyInput(2, 'Began', 'A'), false);
    Assert.equal(Client.ResetInput(), false);
    Client.ReloadPending = false;
    Client.InputSupported = false;
    Assert.equal(Client.Focus(2, true), false);
    Assert.equal(Client.ResetInput(), false);
    Assert.equal(Sent.length, 2);
    Client.InputSupported = true;
    Client.Classes.set('TextButton', {name:'TextButton',base:'TextButton'});
    Assert.equal(Client.InputTarget(2), false);
});
