'use strict';

const Fs = require('node:fs');
const Path = require('node:path');
const Vscode = require('vscode');
const { PreviewClient } = require('./PreviewClient');
const { InspectorProvider } = require('./InspectorProvider');

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
    const Manifest = GetConfiguredPath(Settings.get('manifestPath', 'lui.json'), Root);
    const Repo = Path.resolve(__dirname, '..');
    const Host = GetConfiguredPath(Settings.get('previewHostPath', ''), Root) || FindFirstFile([
        Path.join(Repo, 'tools/preview-host/bin/Debug/net9.0/lui-preview-host.dll'),
        Path.join(Repo, 'tools/preview-host/bin/Release/net9.0/lui-preview-host.dll'),
        Path.join(Root, 'tools/preview-host/bin/Debug/net9.0/lui-preview-host.dll'),
        Path.join(Root, 'tools/preview-host/bin/Release/net9.0/lui-preview-host.dll'),
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

function GetEntryScript(ManifestPath) {
    try {
        const Manifest = JSON.parse(Fs.readFileSync(ManifestPath, 'utf8'));
        return typeof Manifest.Script === 'string' ? Path.resolve(Path.dirname(ManifestPath), Manifest.Script) : '';
    } catch { return ''; }
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
        this.Diagnostics = Vscode.languages.createDiagnosticCollection('LUI Preview');
        this.Explorer = new ExplorerProvider();
        this.Inspector = new InspectorProvider(Message => this.OnInspectorAction(Message));
        this.TreeView = Vscode.window.createTreeView('lui.explorer', { treeDataProvider: this.Explorer });
        this.TreeView.message = 'Run “LUI: Start Preview” to inspect an app.';
        this.Client = null;
        this.SelectedId = 0;
        this.Status = 'Preview stopped';
        this.Error = '';
        this.LastRuntimeErrorGeneration = 0;
        this.LastDiagnosticGeneration = 0;
        this.ManifestPath = '';
        this.ScriptPath = '';
        this.Folder = null;
        this.ReloadTimer = null;
        Context.subscriptions.push(this.Output, this.Diagnostics, this.TreeView, this.Explorer,
            Vscode.window.registerWebviewViewProvider('lui.inspector', this.Inspector),
            Vscode.commands.registerCommand('lui.startPreview', () => this.Start()),
            Vscode.commands.registerCommand('lui.reloadPreview', () => this.Client?.RequestReload()),
            Vscode.commands.registerCommand('lui.stopPreview', () => this.Stop()),
            Vscode.commands.registerCommand('lui.activateNode', Item => this.Activate(Item?.Id)),
            Vscode.commands.registerCommand('lui.openSource', Item => this.OpenSource(Item?.Id, 'created')),
            Vscode.workspace.onDidSaveTextDocument(Document => this.OnSave(Document)),
            this.TreeView.onDidChangeSelection(Event => this.Select(Event.selection[0]?.Id || 0)));
    }

    Log(Message) { this.Output.appendLine('[LUI:Editor] ' + Message); }

    SetStatus(Status, Error = '') {
        this.Status = Status;
        this.Error = Error;
        this.Render();
    }

    Render() {
        const Tree = this.Client?.Tree || this.Explorer.Tree;
        this.Inspector.Render(Tree, this.Client?.Generation || 0, this.SelectedId,
            this.Client?.Classes || new Map(), this.Status, this.Error);
    }

    Start(PreferredFolder) {
        this.Stop();
        if (!Vscode.workspace.isTrusted) {
            this.SetStatus('Preview requires a trusted workspace.', 'Trust this workspace before running application Luau.');
            return;
        }
        const Folder = PreferredFolder || GetFolder();
        if (!Folder) { this.SetStatus('Open a workspace folder to start preview.'); return; }
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
        this.ScriptPath = GetEntryScript(Manifest);
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
        Client.on('consoleMessage', Message => this.Log('Console: ' + Message));
        Client.on('diagnostic', (Message, Generation, Location) => {
            this.Log('Diagnostic: ' + Message);
            this.PublishDiagnostic(Message, Location, Generation);
            this.SetStatus(this.Status, Message);
        });
        Client.on('runtimeError', (Message, Generation, Location) => {
            this.LastRuntimeErrorGeneration = Generation;
            this.Log('Runtime error: ' + Message);
            this.PublishDiagnostic(Message, Location, Generation);
            this.SetStatus('Preview script failed.', Message);
        });
        Client.on('failure', Message => { this.Log(Message); this.SetStatus('Preview protocol failed.', Message); });
        Client.on('exit', ({ Code, Signal, WasClosed }) => {
            if (this.Client !== Client) return;
            this.Client = null;
            this.Explorer.Update({ Nodes: [], ById: new Map(), Children: new Map([[0, []]]) });
            this.SelectedId = 0;
            this.TreeView.message = 'Run “LUI: Start Preview” to inspect an app.';
            if (!WasClosed) this.SetStatus('Preview host exited.', `Exit code: ${Code ?? 'unknown'}${Signal ? ' · ' + Signal : ''}`);
            else this.Render();
        });
    }

    Stop() {
        if (this.ReloadTimer) { clearTimeout(this.ReloadTimer); this.ReloadTimer = null; }
        const Client = this.Client;
        this.Client = null;
        Client?.Stop();
        this.Explorer.Update({ Nodes: [], ById: new Map(), Children: new Map([[0, []]]) });
        this.SelectedId = 0;
        this.Diagnostics.clear();
        this.LastDiagnosticGeneration = 0;
        this.TreeView.message = 'Run “LUI: Start Preview” to inspect an app.';
        this.SetStatus('Preview stopped');
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
    }

    OnSave(Document) {
        if (!this.Client) return;
        if (Path.resolve(Document.uri.fsPath) === Path.resolve(this.ManifestPath)) {
            this.Start(this.Folder);
            return;
        }
        if (Path.resolve(Document.uri.fsPath) !== Path.resolve(this.ScriptPath)) return;
        const Folder = this.Folder;
        if (!Folder || !Vscode.workspace.getConfiguration('lui', Folder.uri).get('autoReload', true)) return;
        if (this.ReloadTimer) clearTimeout(this.ReloadTimer);
        this.ReloadTimer = setTimeout(() => { this.ReloadTimer = null; this.Client?.RequestReload(); }, 250);
    }

    Dispose() { this.Stop(); }
}

let Controller;
function activate(Context) {
    Controller = new EditorController(Context);
    Context.subscriptions.push({ dispose: () => Controller?.Dispose() });
}
function deactivate() { Controller?.Dispose(); Controller = undefined; }

module.exports = { activate, deactivate };
