'use strict';

const Fs = require('node:fs');
const Path = require('node:path');
const Vscode = require('vscode');
const { PreviewClient } = require('./PreviewClient');
const { InspectorProvider } = require('./InspectorProvider');
const { NativePreview, ResolveNativeHost } = require('./NativePreview');
const { TypeDiagnostics } = require('./TypeDiagnostics');
const { UsesSdk, GetBinding, ResolveSdk, GetRoot, GetManifest } = require('./SdkClient');
const { ChooseSdk, CreateProject } = require('./Onboarding');
const { ProjectFiles } = require('./ProjectFiles');
const { ReadManifest, GetSettingsIdentity, HasFolder, SetupMessage } = require('./ProjectSetup');

function GetFolder() {
    const ActivePath = Vscode.window.activeTextEditor?.document.uri.fsPath;
    return Vscode.workspace.workspaceFolders?.find(Folder => ActivePath &&
        (ActivePath === Folder.uri.fsPath || ActivePath.startsWith(Folder.uri.fsPath + Path.sep))) ||
        Vscode.workspace.workspaceFolders?.[0];
}

function GetConfiguredPath(Value, Root) {
    if (!Value) return '';
    return Path.isAbsolute(Value) ? Value : Path.resolve(Root, Value);
}

function FindFirstFile(Paths) { return Paths.find(Value => Fs.existsSync(Value)) || ''; }

function GetPaths(Folder) {
    const Root = Folder.uri.fsPath;
    const Settings = Vscode.workspace.getConfiguration('lui', Folder.uri);
    const Sdk = GetBinding(Folder, Vscode);
    if (Sdk) return { Manifest: Path.resolve(Root, Settings.get('manifestPath', 'lui.json')), Host: Sdk.Components.PreviewHost, Runtime: Sdk.Components.Runtime };
    const Manifest = GetConfiguredPath(Settings.get('manifestPath', 'lui.json'), Root);
    const Repo = Path.resolve(__dirname, '..');
    const Host = GetConfiguredPath(Settings.get('previewHostPath', ''), Root) || FindFirstFile([
        Path.join(Repo, 'tools/preview-host/bin/Debug/net10.0/lui-preview-host.dll'),
        Path.join(Repo, 'tools/preview-host/bin/Release/net10.0/lui-preview-host.dll'),
        Path.join(Root, 'tools/preview-host/bin/Debug/net10.0/lui-preview-host.dll'),
        Path.join(Root, 'tools/preview-host/bin/Release/net10.0/lui-preview-host.dll'),
    ]);
    const Runtime = GetConfiguredPath(Settings.get('runtimePath', ''), Root) ||
        GetConfiguredPath(process.env.LUI_RUNTIME || '', Root) || FindFirstFile([
            Path.join(Root, 'build/windows-x64/Debug/LuiRuntime.dll'),
            Path.join(Root, 'build/cli-native/LuiRuntime.dll'),
            Path.join(Root, 'build/linux-x64/libLuiRuntime.so'),
            Path.join(Repo, 'build/windows-x64/Debug/LuiRuntime.dll'),
            Path.join(Repo, 'build/cli-native/LuiRuntime.dll'),
            Path.join(Repo, 'build/linux-x64/libLuiRuntime.so'),
        ]);
    return { Manifest, Host, Runtime };
}

function GetNativeHostPath(Folder) {
    const Sdk = GetBinding(Folder, Vscode);
    if (Sdk) return Sdk.Components.NativeHost;
    const Root = Folder.uri.fsPath;
    const Settings = Vscode.workspace.getConfiguration('lui', Folder.uri);
    const Configured = GetConfiguredPath(Settings.get('nativeHostPath', ''), Root);
    if (Configured) return Configured;
    const Candidates = process.platform==='linux' ? [
        Path.join(Root,'build/gtk4/backends/gtk4/lui-gtk'),
        Path.join(Root,'build/linux-x64/backends/gtk4/lui-gtk'),
    ] : [
        Path.join(Root, 'backends/winui3/bin/x64/Debug/net10.0-windows10.0.19041.0/win-x64/Lui.WinUI.exe'),
        Path.join(Root, 'backends/winui3/bin/x64/Release/net10.0-windows10.0.19041.0/win-x64/Lui.WinUI.exe'),
    ];
    return Candidates.find(Candidate => {
        try { ResolveNativeHost(Candidate); return true; }
        catch { return false; }
    }) || '';
}

function GetNativeIdentity(Folder) {
    return JSON.stringify([GetRoot(Vscode), GetManifest(Folder, Vscode),
        Vscode.workspace.getConfiguration('lui', Folder.uri).get('nativeHostPath', '')]);
}

class ExplorerProvider {
    constructor() {
        this.Tree = { Nodes: [], ById: new Map(), Children: new Map([[0, []]]) };
        this.Items = new Map();
        this.OnChange = new Vscode.EventEmitter();
        this.onDidChangeTreeData = this.OnChange.event;
    }

    Update(Tree) {
        this.Tree = Tree;
        this.Items.clear();
        this.OnChange.fire();
    }

    getChildren(Item) {
        return (this.Tree.Children.get(Item?.Id || 0) || []).map(Id => this.GetItem(Id));
    }

    getParent(Item) {
        const ParentId = this.Tree.ById.get(Item.Id)?.parentId || 0;
        return ParentId ? this.GetItem(ParentId) : undefined;
    }

    GetItem(Id) {
        if (this.Items.has(Id)) return this.Items.get(Id);
        const Node = this.Tree.ById.get(Id);
        const HasChildren = (this.Tree.Children.get(Id) || []).length > 0;
        const Label = Node.name || Node.className;
        const Item = new Vscode.TreeItem(Label,
            HasChildren ? Vscode.TreeItemCollapsibleState.Collapsed : Vscode.TreeItemCollapsibleState.None);
        Item.Id = Id;
        Item.id = String(Id);
        Item.description = Node.className === Label ? '' : Node.className;
        Item.tooltip = `${Node.className} #${Id}${Node.text ? '\n' + Node.text.slice(0, 200) : ''}`;
        Item.contextValue = ['TextButton', 'CheckBox'].includes(Node.className) ? 'lui.activatable' : 'lui.instance';
        Item.iconPath = new Vscode.ThemeIcon(Node.className === 'Window' ? 'window' :
            Node.className.startsWith('UI') ? 'symbol-structure' : 'symbol-method');
        this.Items.set(Id, Item);
        return Item;
    }

    getTreeItem(Item) { return Item; }
    Dispose() { this.OnChange.dispose(); }
}

class EditorController {
    constructor(Context) {
        this.Context = Context;
        this.Output = Vscode.window.createOutputChannel('LUI Preview');
        this.NativeStatus = Vscode.window.createStatusBarItem(Vscode.StatusBarAlignment.Left, 10);
        this.NativeStatus.name = 'LUI App';
        this.Diagnostics = Vscode.languages.createDiagnosticCollection('LUI Preview');
        this.Types = new TypeDiagnostics(Vscode, this.Output);
        this.ProjectFiles = new ProjectFiles(Vscode, this.Output);
        this.Explorer = new ExplorerProvider();
        this.Inspector = new InspectorProvider(Message => this.OnInspectorAction(Message));
        this.TreeView = Vscode.window.createTreeView('lui.explorer', { treeDataProvider: this.Explorer });
        this.TreeView.message = 'Run “LUI: Start Preview” to inspect an app.';
        this.Client = null;
        this.Native = null;
        this.NativeFolder = null;
        this.NativePendingFolder = null;
        this.SelectedId = 0;
        this.Status = 'Preview stopped';
        this.Error = '';
        this.LastRuntimeErrorGeneration = 0;
        this.LastDiagnosticGeneration = 0;
        this.ManifestPath = '';
        this.ScriptPath = '';
        this.Folder = null;
        this.PreviewFolder = null;
        this.ReloadTimer = null;
        Context.subscriptions.push(this.Output, this.NativeStatus, this.Diagnostics, this.TreeView, this.Explorer,
            Vscode.window.registerWebviewViewProvider('lui.inspector', this.Inspector),
            Vscode.commands.registerCommand('lui.startPreview', () => this.Start()),
            Vscode.commands.registerCommand('lui.chooseSdk', () => ChooseSdk(Vscode, this.Output)),
            Vscode.commands.registerCommand('lui.createProject', () => CreateProject(Vscode, this.Output, Context)),
            Vscode.commands.registerCommand('lui.addProjectFile', Uri => this.ProjectFiles.AddFile(Uri)),
            Vscode.commands.registerCommand('lui.reloadPreview', () => this.Client?.RequestReload()),
            Vscode.commands.registerCommand('lui.stopPreview', () => this.Stop()),
            Vscode.commands.registerCommand('lui.openNativePreview', () => this.OpenNative()),
            Vscode.commands.registerCommand('lui.restartNativePreview', () => this.OpenNative(true)),
            Vscode.commands.registerCommand('lui.stopNativePreview', () => this.StopNative()),
            Vscode.commands.registerCommand('lui.checkTypes', () => this.Types.Check(GetFolder(), true)),
            Vscode.commands.registerCommand('lui.activateNode', Item => this.Activate(Item?.Id)),
            Vscode.commands.registerCommand('lui.openSource', Item => this.OpenSource(Item?.Id, 'created')),
            Vscode.workspace.onDidSaveTextDocument(Document => this.OnSave(Document)),
            Vscode.workspace.onDidOpenTextDocument(Document => { this.Types.OnDocument(Document); this.ProjectFiles.OnDocument(Document); }),
            Vscode.workspace.onDidChangeTextDocument(Event => {
                this.Types.OnChange(Event.document);
                if (Event.contentChanges.length) this.OnNativeSourceChange(Event.document);
            }),
            Vscode.workspace.onDidChangeConfiguration(Event => {
                if (Event.affectsConfiguration('lui')) {
                    if (this.Disposed) return;
                    this.CancelNativeLaunch();
                    if (this.Client || this.PreviewFolder) this.Stop();
                    this.ProjectFiles.Refresh();
                    this.Types.Check(GetFolder());
                }
            }),
            this.TreeView.onDidChangeSelection(Event => this.Select(Event.selection[0]?.Id || 0)));
        for (const Document of Vscode.workspace.textDocuments) { this.Types.OnDocument(Document); this.ProjectFiles.OnDocument(Document); }
        if (Vscode.workspace.onDidChangeWorkspaceFolders) Context.subscriptions.push(Vscode.workspace.onDidChangeWorkspaceFolders(Event => {
            if (this.Disposed) return;
            if (Event.removed.some(Removed => [this.NativeFolder, this.NativePendingFolder].some(Folder => Folder && Folder.uri.fsPath === Removed.uri.fsPath))) this.StopNative();
            if (Event.removed.some(Removed => [this.Folder, this.PreviewFolder].some(Folder => Folder && Folder.uri.fsPath === Removed.uri.fsPath))) this.Stop();
            if (Event.removed.some(Removed => this.Types.Folder?.uri.fsPath === Removed.uri.fsPath)) this.Types.Invalidate();
            this.ProjectFiles.Refresh();
        }));
        this.NativeContext('lui.nativeRunning', false);
        this.NativeContext('lui.nativeStarting', false);
    }

    Log(Message) { this.Output.appendLine('[LUI:Editor] ' + Message); }
    NativeMessage(Message) {
        this.Log(Message);
        Vscode.window.setStatusBarMessage('[LUI:NativePreview] ' + Message, 7000);
        this.RenderNativeState();
    }

    NativeContext(Name, Value) {
        try { Vscode.commands.executeCommand('setContext', Name, Value)?.then(undefined, () => {}); }
        catch { /* Window shutdown may retire the command channel first. */ }
        this.RenderNativeState();
    }

    RenderNativeState() {
        if (!this.NativeShown || this.Disposed) { this.NativeStatus.hide(); return; }
        const Native = this.Native;
        let Text = '$(debug-stop) LUI · App stopped', Command = 'lui.openNativePreview';
        let Advice = 'Run App launches the active project using saved files.';
        if (this.NativePendingFolder) {
            Text = Native?.Process ? '$(sync~spin) LUI · Restarting app' : '$(sync~spin) LUI · Preparing app';
            Command = 'lui.stopNativePreview'; Advice = 'Click to cancel the pending launch or restart.';
        } else if (Native?.Process) {
            Command = 'lui.restartNativePreview';
            Advice = 'Click to restart this app. Save documents inside the app first; restart discards its memory. Restart after edits outside VS Code too.';
            if (Native.Exited) { Text = '$(sync~spin) LUI · Finishing app shutdown'; Command = 'lui.stopNativePreview'; Advice = 'The app process exited; finishing shutdown.'; }
            else if (Native.StopFailed) { Text = '$(warning) LUI · Stop failed · See Output'; Command = 'lui.stopNativePreview'; Advice = 'Click to retry Stop App, or close its native window if it is open. See LUI Preview output.'; }
            else if (Native.Stopping) { Text = '$(sync~spin) LUI · Stopping app'; Command = 'lui.stopNativePreview'; Advice = 'Waiting for the owned app to exit. A replacement cannot overlap it.'; }
            else if (!this.NativeLaunched) { Text = '$(sync~spin) LUI · Starting app'; Command = 'lui.stopNativePreview'; Advice = 'Click to cancel app startup.'; }
            else if (this.NativeFolder && this.NativeDirty(this.NativeFolder)) Text = '$(play) LUI · Running · Unsaved project edits';
            else if (this.NativeEdited) Text = '$(play) LUI · Running · Restart after project edits';
            else Text = '$(play) LUI · Running';
        }
        this.NativeStatus.text = Text;
        this.NativeStatus.command = Command;
        this.NativeStatus.tooltip = Advice;
        this.NativeStatus.show();
    }

    OnNativeSourceChange(Document) {
        if (!this.Native?.Process || !this.NativeFolder || Document.uri.scheme !== 'file') return;
        const Relative = Path.relative(this.NativeFolder.uri.fsPath, Document.uri.fsPath);
        if (Relative === '..' || Relative.startsWith('..' + Path.sep) || Path.isAbsolute(Relative)) return;
        this.NativeEdited = true;
        this.RenderNativeState();
    }

    NativeDirty(Folder) {
        const Root = Path.resolve(Folder.uri.fsPath);
        return Vscode.workspace.textDocuments.some(Document => {
            if (!Document.isDirty || Document.uri.scheme !== 'file') return false;
            const Relative = Path.relative(Root, Document.uri.fsPath);
            return Relative === '' || (Relative !== '..' && !Relative.startsWith('..' + Path.sep) && !Path.isAbsolute(Relative));
        });
    }

    SetStatus(Status, Error = '') {
        this.Status = Status;
        this.Error = Error;
        this.Render();
    }

    Render() {
        const Tree = this.Client?.Tree || this.Explorer.Tree;
        this.Inspector.Render(Tree, this.Client?.Generation || 0, this.SelectedId,
            this.Client?.Classes || new Map(), this.Status, this.Error,
            this.Client?.InputTarget(this.SelectedId) || false);
    }

    async Start(PreferredFolder) {
        if (this.Disposed) return;
        this.Stop();
        const Request = this.PreviewRequest = (this.PreviewRequest || 0) + 1;
        if (!Vscode.workspace.isTrusted) {
            this.SetStatus('Preview requires a trusted workspace.', 'Trust this workspace before running application Luau.');
            return;
        }
        const Folder = PreferredFolder || GetFolder();
        if (!Folder) { this.SetStatus('Open a workspace folder to start preview.'); return; }
        this.PreviewFolder = Folder;
        try {
            const Identity = GetSettingsIdentity(Folder, Vscode);
            if (!HasFolder(Folder, Vscode)) return;
            if (UsesSdk(Folder, Vscode)) await ResolveSdk(Folder, Vscode);
            if (this.Disposed || Request !== this.PreviewRequest || !Vscode.workspace.isTrusted || !HasFolder(Folder, Vscode) || Identity !== GetSettingsIdentity(Folder, Vscode)) return;
            const { Manifest, Host, Runtime } = GetPaths(Folder);
            for (const [Name, Value] of [['manifest', Manifest], ['preview host', Host], ['native runtime', Runtime]]) {
                if (!Value || !Fs.existsSync(Value)) {
                    const Error = `${Name} was not found. Check the LUI Preview settings.`;
                    this.Log(Error);
                    this.SetStatus('Preview could not start.', Error);
                    return;
                }
            }
            this.ManifestPath = Manifest;
            const Data = ReadManifest(Manifest);
            this.ScriptPath = Data.SchemaVersion === 2 ? '' : typeof Data.Script === 'string' ? Path.resolve(Path.dirname(Manifest), Data.Script) : '';
            this.Folder = Folder;
            this.Client = new PreviewClient(Manifest, Host, Runtime);
            this.LastRuntimeErrorGeneration = 0;
            const Client = this.Client;
            this.TreeView.message = 'Connecting to preview host…';
            this.SetStatus('Connecting to preview host…');
            Client.on('tree', (Tree, Generation, ChangedGeneration) => {
                if (this.Client !== Client) return;
                if (ChangedGeneration && this.LastDiagnosticGeneration !== Generation) this.Diagnostics.clear();
                if (ChangedGeneration || !Tree.ById.has(this.SelectedId))
                    this.SelectedId = Tree.Nodes.find(Node => Node.className === 'Window')?.id || Tree.Nodes[0]?.id || 0;
                this.Explorer.Update(Tree);
                this.TreeView.message = Tree.Nodes.length ? undefined : 'No live Instances in this generation.';
                if (!Tree.Nodes.length && this.LastRuntimeErrorGeneration === Generation)
                    this.SetStatus('Preview script failed.', this.Error);
                else this.SetStatus(`${Tree.Nodes.length} Instances · preview running`);
            });
            Client.on('consoleMessage', Message => { if (this.Client === Client) this.Log('Console: ' + Message); });
            Client.on('diagnostic', (Message, Generation, Location) => {
                if (this.Client !== Client) return;
                this.Log('Diagnostic: ' + Message);
                this.PublishDiagnostic(Message, Location, Generation);
                this.SetStatus(this.Status, Message);
            });
            Client.on('runtimeError', (Message, Generation, Location) => {
                if (this.Client !== Client) return;
                this.LastRuntimeErrorGeneration = Generation;
                this.Log('Runtime error: ' + Message);
                this.PublishDiagnostic(Message, Location, Generation);
                this.SetStatus('Preview script failed.', Message);
            });
            Client.on('failure', Message => { if (this.Client === Client) { this.Log(Message); this.SetStatus('Preview protocol failed.', Message); } });
            Client.on('exit', ({ Code, Signal, WasClosed }) => {
                if (this.Client !== Client) return;
                this.Client = null;
                this.Explorer.Update({ Nodes: [], ById: new Map(), Children: new Map([[0, []]]) });
                this.SelectedId = 0;
                this.TreeView.message = 'Run “LUI: Start Preview” to inspect an app.';
                if (!WasClosed) this.SetStatus('Preview host exited.', `Exit code: ${Code ?? 'unknown'}${Signal ? ' · ' + Signal : ''}`);
                else this.Render();
            });
        } catch (Failure) {
            if (!this.Disposed && Request === this.PreviewRequest) this.SetStatus('Preview could not start.', SetupMessage(Failure));
        } finally {
            if (Request === this.PreviewRequest) this.PreviewFolder = null;
        }
    }

    Stop() {
        this.PreviewRequest = (this.PreviewRequest || 0) + 1;
        if (this.ReloadTimer) { clearTimeout(this.ReloadTimer); this.ReloadTimer = null; }
        const Client = this.Client;
        this.Client = null;
        this.Folder = null;
        this.PreviewFolder = null;
        Client?.Stop();
        this.Explorer.Update({ Nodes: [], ById: new Map(), Children: new Map([[0, []]]) });
        this.SelectedId = 0;
        this.Diagnostics.clear();
        this.LastDiagnosticGeneration = 0;
        this.TreeView.message = 'Run “LUI: Start Preview” to inspect an app.';
        this.SetStatus('Preview stopped');
    }

    async OpenNative(Restart = false) {
        this.NativeShown = true;
        if (process.platform!=='win32' && process.platform!=='linux') {
            this.NativeMessage('Native preview requires Windows or Linux.');
            return;
        }
        if (!Vscode.workspace.isTrusted) {
            this.NativeMessage('Native preview requires a trusted workspace.');
            return;
        }
        if (this.Native?.Process && !Restart) {
            this.NativeMessage(this.Native.Exited ? 'The previous app is finishing shutdown. Use Stop App or wait.' : 'Native preview is already open.');
            return;
        }
        const Folder = (Restart && this.NativeFolder) || GetFolder();
        if (!Folder) { this.NativeMessage('Open a workspace folder to launch native preview.'); return; }
        if (this.NativeDirty(Folder)) { this.NativeMessage('Save project files before running or restarting the app.'); return; }
        let Identity;
        try { Identity = GetNativeIdentity(Folder); }
        catch { this.NativeMessage('Could not read launch settings. Check the project and selected SDK.'); return; }
        const Request = this.NativeRequest = (this.NativeRequest || 0) + 1;
        this.NativePendingFolder = Folder;
        this.NativeContext('lui.nativeStarting', true);
        try {
            if (Restart && this.Native?.Process && !await this.Native.StopAsync()) return;
            if (this.Disposed || Request !== this.NativeRequest || !Vscode.workspace.isTrusted || Identity !== GetNativeIdentity(Folder)) return;
            try { if (UsesSdk(Folder, Vscode)) await ResolveSdk(Folder, Vscode); }
            catch (Error) { if (!this.Disposed && Request === this.NativeRequest) this.NativeMessage(Error.message); return; }
            if (!Vscode.workspace.isTrusted || this.Disposed || this.Native?.Process || Request !== this.NativeRequest || Identity !== GetNativeIdentity(Folder)) return;
            if (!Vscode.workspace.workspaceFolders?.some(Current => Current.uri.fsPath === Folder.uri.fsPath)) return;
            if (this.NativeDirty(Folder)) { this.NativeMessage('Save project files before running or restarting the app.'); return; }
            const Manifest = GetPaths(Folder).Manifest;
            if (!Fs.existsSync(Manifest)) { this.NativeMessage('Native preview manifest was not found.'); return; }
            let Host;
            try { Host = ResolveNativeHost(GetNativeHostPath(Folder)); }
            catch (Error) { this.NativeMessage(Error.message + ' Set lui.nativeHostPath to the built native host.'); return; }
            const Native = new NativePreview(Manifest, Host);
            this.Native = Native;
            this.NativeFolder = Folder;
            this.NativeEdited = false;
            this.NativeLaunched = false;
            Native.once('started', () => { if (this.Native === Native) { this.NativeLaunched = true; this.NativeContext('lui.appLaunched', true); this.NativeContext('lui.nativeRunning', true); this.NativeContext('lui.nativeStarting', false); } });
            Native.once('retiring', () => { if (this.Native === Native) { this.NativeContext('lui.nativeRunning', false); this.NativeContext('lui.nativeStarting', true); } });
            Native.on('failure', Message => { if (this.Native === Native) this.NativeMessage(Message); });
            Native.on('diagnostic', Diagnostic => { if (this.Native === Native) this.NativeMessage(Diagnostic.Message); });
            Native.on('exit', ({ Code, Signal, WasStopped, LaunchFailed, StartupDiagnostic }) => {
                if (this.Native !== Native) return;
                this.Native = null;
                this.NativeFolder = null;
                this.NativeContext('lui.nativeRunning', false);
                this.NativeContext('lui.nativeStarting', Boolean(this.NativePendingFolder));
                if (LaunchFailed) return;
                if (WasStopped) this.Log('Native preview stopped.');
                else if (Code === 0) this.Log('Native preview window closed.');
                else if (!StartupDiagnostic) this.NativeMessage(`App stopped with exit code ${Code ?? 'unknown'}${Signal ? ' · ' + Signal : ''}. ` +
                    'No startup reason was received. Run Check Types, check the selected SDK with lui doctor, and include the SDK version and reproduction steps in a bug report.');
            });
            try {
                Native.Start();
                this.Log('Opening native preview for ' + Manifest);
            } catch { this.Native = null; this.NativeFolder = null; this.NativeMessage('Could not launch native preview. Check the project and selected SDK.'); }
        } catch {
            if (!this.Disposed && Request === this.NativeRequest) this.NativeMessage('Could not launch native preview. Check the project and selected SDK.');
        } finally {
            if (Request === this.NativeRequest) { this.NativePendingFolder = null; this.NativeContext('lui.nativeStarting', Boolean(this.Native?.Process && !this.NativeLaunched)); }
        }
    }

    CancelNativeLaunch() {
        this.NativeRequest = (this.NativeRequest || 0) + 1;
        this.NativePendingFolder = null;
        this.NativeContext('lui.nativeStarting', Boolean(this.Native?.Process && (!this.NativeLaunched || this.Native.Exited)));
    }

    async StopNative() {
        this.CancelNativeLaunch();
        const Native = this.Native;
        if (!Native) return true;
        const Stopped = Native.StopAsync();
        this.RenderNativeState();
        const Result = await Stopped;
        this.RenderNativeState();
        return Result;
    }

    Select(Id) {
        if (!this.Client?.Tree.ById.has(Id)) return;
        this.SelectedId = Id;
        this.Render();
    }

    Activate(Id) {
        if (this.Client?.Tree.ById.has(Id)) this.Client.Activate(Id);
    }

    GetSourceUri(Location) {
        if(this.Client?.Version===2) {
            if(!Location || !Number.isSafeInteger(Location.line) || Location.line<=0) return null;
            const File=require('./ProjectProtocol').SafeSource(this.Client.Sources,Location.source);
            return File?Vscode.Uri.file(File):null;
        }
        if (!Location || !this.ScriptPath || typeof Location.source !== 'string' ||
            !Number.isSafeInteger(Location.line) || Location.line <= 0) return null;
        const Candidate = Path.resolve(Path.dirname(this.ManifestPath), Location.source);
        return Candidate === Path.resolve(this.ScriptPath) ? Vscode.Uri.file(Candidate) : null;
    }

    PublishDiagnostic(Message, Location, Generation) {
        const Uri = this.GetSourceUri(Location);
        if (!Uri) return;
        this.LastDiagnosticGeneration = Generation;
        const Line = Location.line - 1;
        const Range = new Vscode.Range(Line, 0, Line, 1);
        const Diagnostic = new Vscode.Diagnostic(Range, Message, Vscode.DiagnosticSeverity.Error);
        Diagnostic.source = 'LUI preview';
        this.Diagnostics.set(Uri, [Diagnostic]);
    }

    async OpenSource(Id, Kind) {
        const Node = this.Client?.Tree.ById.get(Id);
        const Location = Kind === 'lastChanged' ? Node?.lastChangedAt : Node?.createdAt;
        const Uri = this.GetSourceUri(Location);
        if (!Uri) { this.Log('No source location is available for that Instance.'); return; }
        try {
            const Document = await Vscode.workspace.openTextDocument(Uri);
            const Line = Math.min(Location.line - 1, Math.max(0, Document.lineCount - 1));
            const Selection = new Vscode.Range(Line, 0, Line, 0);
            await Vscode.window.showTextDocument(Document, { preview: true, selection: Selection });
        } catch (Error) { this.Log('Could not open source: ' + Error.message); }
    }

    OnInspectorAction(Message) {
        if (Message?.type === 'resetInput' && this.Client && Message.generation === this.Client.Generation) {
            this.Client.ResetInput(); return;
        }
        if (!Message || !this.Client || Message.generation !== this.Client.Generation ||
            !Number.isSafeInteger(Message.id) || !this.Client.Tree.ById.has(Message.id)) return;
        if (Message.type === 'select') {
            this.Select(Message.id);
            this.TreeView.reveal(this.Explorer.GetItem(Message.id), { select: true, focus: false })
                .then(undefined, Error => this.Log('Could not reveal Instance: ' + Error.message));
        } else if (Message.type === 'activate') this.Activate(Message.id);
        else if (Message.type === 'openSource' && (Message.kind === 'created' || Message.kind === 'lastChanged'))
            this.OpenSource(Message.id, Message.kind);
        else if (Message.type === 'resizeViewport')
            this.Client.ResizeViewport(Message.id, Message.width, Message.height);
        else if (Message.type === 'focus') this.Client.Focus(Message.id, Message.focused);
        else if (Message.type === 'pointerInput')
            this.Client.PointerInput(Message.id, Message.phase, Message.device, Message.pointerId, Message.x, Message.y);
        else if (Message.type === 'keyInput') this.Client.KeyInput(Message.id, Message.phase, Message.key);
    }

    OnSave(Document) {
        this.Types.OnDocument(Document);
        this.ProjectFiles.Refresh();
        this.RenderNativeState();
        if (!this.Client) return;
        if (Path.resolve(Document.uri.fsPath) === Path.resolve(this.ManifestPath)) {
            this.Start(this.Folder);
            return;
        }
        if(this.Client.Version===2) {
            if(!Path.resolve(Document.uri.fsPath).startsWith(Path.dirname(this.ManifestPath)+Path.sep)) return;
        } else if (Path.resolve(Document.uri.fsPath) !== Path.resolve(this.ScriptPath)) return;
        const Folder = this.Folder;
        if (!Folder || !Vscode.workspace.getConfiguration('lui', Folder.uri).get('autoReload', true)) return;
        if (this.ReloadTimer) clearTimeout(this.ReloadTimer);
        this.ReloadTimer = setTimeout(() => { this.ReloadTimer = null; this.Client?.RequestReload(); }, 250);
    }

    Dispose() {
        if (this.Disposed) return;
        this.Disposed = true;
        this.RenderNativeState();
        this.Types.Dispose();
        this.ProjectFiles.Dispose();
        this.Native?.removeAllListeners();
        this.StopNative();
        this.Native = null;
        this.Stop();
    }
}

function OperatingSystemIsWindows() { return process.platform === 'win32'; }

let Controller;
async function activate(Context) {
    Controller = new EditorController(Context);
    Context.subscriptions.push({ dispose: () => Controller?.Dispose() });
    const Pending = Context.globalState.get('LuiPendingProject');
    if (Pending && Vscode.workspace.workspaceFolders?.some(Folder => Path.resolve(Folder.uri.fsPath) === Path.resolve(Pending))) {
        await Context.globalState.update('LuiPendingProject', undefined);
        await Vscode.window.showTextDocument(Vscode.Uri.file(Path.join(Pending, 'Src', 'Main.luau')));
    }
}
function deactivate() { Controller?.Dispose(); Controller = undefined; }

module.exports = { activate, deactivate };
