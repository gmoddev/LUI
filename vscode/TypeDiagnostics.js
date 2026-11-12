'use strict';

const Fs = require('node:fs');
const Path = require('node:path');
const { TypeCheckSession } = require('./TypeCheckClient');
const {SafeSource}=require('./ProjectProtocol');
const { UsesSdk, GetBinding, ResolveSdk, GetManifest } = require('./SdkClient');
const { ReadText, ReadManifest, GetSettingsIdentity, HasFolder, Contains, ProjectSetupError, SetupMessage } = require('./ProjectSetup');

function GetTypePaths(Folder, Vscode) {
    const Root = Folder.uri.fsPath;
    const Repo = Path.resolve(__dirname, '..');
    const Settings = Vscode.workspace.getConfiguration('lui', Folder.uri);
    const Sdk = GetBinding(Folder, Vscode);
    if (Sdk) return { Manifest: Path.resolve(Root, Settings.get('manifestPath', 'lui.json')), Cli: Sdk.Components.Cli,
        Checker: Sdk.Components.Checker, Definitions: Sdk.Components.Definitions };
    const Configured = Name => {
        const Value = Settings.get(Name, '');
        return Value ? Path.resolve(Root, Value) : '';
    };
    const First = Candidates => Candidates.find(Candidate => Fs.existsSync(Candidate)) || '';
    const Cli = Configured('cliPath') || First([Root, Repo].flatMap(Directory => ['Debug', 'Release'].map(Configuration =>
        Path.join(Directory, `tools/cli/bin/${Configuration}/net10.0/lui.dll`))));
    const Name = process.platform === 'win32' ? 'LuiTypeCheck.exe' : 'LuiTypeCheck';
    const Checker = Configured('checkerPath') || (process.env.LUI_TYPECHECK ? Path.resolve(Root, process.env.LUI_TYPECHECK) : '') ||
        First([Root, Repo].flatMap(Directory => [
        Path.join(Directory, 'build/windows-x64/Debug', Name),
        Path.join(Directory, 'build/windows-x64/Release', Name),
        Path.join(Directory, 'build/linux-x64', Name),
    ]));
    const Definitions = Configured('definitionsPath') || (Checker ? Path.join(Path.dirname(Checker), 'LUI.d.luau') : '');
    return { Manifest: Configured('manifestPath') || Path.join(Root, 'lui.json'), Cli, Checker, Definitions };
}

function GetEntry(ManifestPath) {
    const Manifest = ReadManifest(ManifestPath, 64 * 1024);
    const Root = Path.dirname(ManifestPath);
    if (Manifest.SchemaVersion !== 1 || typeof Manifest.Script !== 'string' || !Manifest.Script || Path.isAbsolute(Manifest.Script))
        throw new ProjectSetupError('Entry');
    const Script = Path.resolve(Root, Manifest.Script);
    const Relative = Path.relative(Root, Script);
    if (!Relative || Relative === '..' || Relative.startsWith('..' + Path.sep) || Path.isAbsolute(Relative))
        throw new ProjectSetupError('Outside');
    // Match the shared manifest's prohibition on reparse points/symlinks.
    let Component = Root;
    for (const Part of Relative.split(Path.sep)) {
        Component = Path.join(Component, Part);
        if (Fs.lstatSync(Component).isSymbolicLink()) throw new ProjectSetupError('Linked');
    }
    return { Script, SourceName: Manifest.Script.replaceAll('\\', '/') };
}

class TypeDiagnostics {
    constructor(Vscode, Output, Session = new TypeCheckSession()) {
        this.Vscode = Vscode;
        this.Output = Output;
        this.Session = Session;
        this.Diagnostics = Vscode.languages.createDiagnosticCollection('LUI Types');
        this.Script = '';
        this.Manifest = '';
        this.Modular=false;
        Session.on('failure', Message => this.Log(Message));
        Session.on('result', Result => {
            if(Result.version===2) {
                this.Diagnostics.clear(); const Groups=new Map();
                for(const Item of Result.diagnostics) {
                    if(!Item.range) {this.Log(Item.message); continue;}
                    const File=SafeSource(Result.SourceMap,Item.source); if(!File) continue;
                    const {start:Start,end:End}=Item.range;
                    const Issue=new Vscode.Diagnostic(new Vscode.Range(Start.line,Start.column,End.line,End.column),Item.message,Vscode.DiagnosticSeverity.Error);
                    Issue.source='LUI '+Item.kind;
                    if(!Groups.has(File)) Groups.set(File,[]); Groups.get(File).push(Issue);
                }
                for(const [File,Items] of Groups) this.Diagnostics.set(Vscode.Uri.file(File),Items);
                return;
            }
            const Items = Result.diagnostics.map(Item => {
                const { start: Start, end: End } = Item.range;
                const Diagnostic = new Vscode.Diagnostic(new Vscode.Range(Start.line, Start.column, End.line, End.column),
                    Item.message, Vscode.DiagnosticSeverity.Error);
                Diagnostic.source = 'LUI ' + Item.kind;
                return Diagnostic;
            });
            this.Diagnostics.set(Vscode.Uri.file(this.Script), Items);
            if (Result.truncated) this.Log('Some type diagnostics were truncated. Fix the first errors and check again.');
        });
    }

    Log(Message) { this.Output.appendLine('[LUI:Types] ' + Message); }

    async Check(Folder, Explicit = false) {
        if (this.Disposed) return;
        this.Invalidate();
        const Request = this.SdkRequest;
        if (!Folder) { if (Explicit) this.Log('Open an application workspace folder to check types.'); return; }
        const Vscode = this.Vscode;
        if (!Vscode.workspace.isTrusted) { if (Explicit) this.Log('Type checking requires a trusted workspace.'); return; }
        try {
            if (!HasFolder(Folder, Vscode)) return;
            if (!Explicit && !Vscode.workspace.getConfiguration('lui', Folder.uri).get('autoCheck', true)) return;
            const Identity = GetSettingsIdentity(Folder, Vscode);
            this.Folder = Folder;
            if (!Fs.existsSync(GetManifest(Folder, Vscode))) {
                if (Explicit) this.Log('Application manifest was not found.');
                return;
            }
            if (UsesSdk(Folder, Vscode)) {
                await ResolveSdk(Folder, Vscode);
            }
            if (this.Disposed || Request !== this.SdkRequest || !Vscode.workspace.isTrusted || !HasFolder(Folder, Vscode) || Identity !== GetSettingsIdentity(Folder, Vscode)) return;
            const Paths = GetTypePaths(Folder, Vscode);
            if (!Fs.existsSync(Paths.Manifest)) { if (Explicit) this.Log('Application manifest was not found.'); return; }
            for (const Value of [Paths.Cli, Paths.Checker, Paths.Definitions])
                if (!Value || !Fs.existsSync(Value)) throw new ProjectSetupError('Tools');
            this.Modular=ReadManifest(Paths.Manifest).SchemaVersion===2;
            if(this.Modular) {
                this.Manifest=Paths.Manifest;
                if(Vscode.workspace.textDocuments.some(Document=>Document.isDirty && Document.uri.scheme==='file' &&
                    Contains(Path.dirname(Paths.Manifest), Document.uri.fsPath))) {
                    if(Explicit) this.Log('Save project files before checking.'); return;
                }
                this.Session.Start({...Paths,Modular:true}); return;
            }
            const { Script, SourceName } = GetEntry(Paths.Manifest);
            const Dirty = Vscode.workspace.textDocuments.some(Document => Document.isDirty &&
                [Script, Paths.Manifest].some(File => Path.relative(File, Document.uri.fsPath) === ''));
            if (Dirty) { if (Explicit) this.Log('Save the entry script and manifest before checking types.'); return; }
            const Source = ReadText(Script, 8 * 1024 * 1024, 'Source');
            this.Script = Script;
            this.Manifest = Paths.Manifest;
            this.Session.Start({ ...Paths, SourceName, Source });
        } catch (Failure) { if (!this.Disposed && Request === this.SdkRequest) this.Log(SetupMessage(Failure)); }
    }

    OnChange(Document) {
        if (this.Disposed) return;
        const File = Path.resolve(Document.uri.fsPath);
        if ((this.Script && Path.relative(this.Script,File) === '') || (this.Manifest && Path.relative(this.Manifest,File) === '') ||
            (this.Modular && Contains(Path.dirname(this.Manifest),File))) this.Invalidate();
    }

    OnDocument(Document) {
        if (this.Disposed) return;
        if (Document.uri.scheme !== 'file') return;
        const Folder = this.Vscode.workspace.getWorkspaceFolder(Document.uri);
        if (!Folder) return;
        try {
            if (UsesSdk(Folder, this.Vscode)) {
                if (Fs.existsSync(GetManifest(Folder, this.Vscode))) this.Check(Folder);
                return;
            }
            const { Manifest } = GetTypePaths(Folder, this.Vscode);
            const File = Path.resolve(Document.uri.fsPath);
            const Modular=ReadManifest(Manifest).SchemaVersion===2;
            if(Modular && Contains(Path.dirname(Manifest),File)) {this.Check(Folder); return;}
            if (Path.relative(Manifest,File) === '' || Path.relative(GetEntry(Manifest).Script,File) === '') this.Check(Folder);
        } catch { /* No matching readable entry script yet. Explicit checks report setup errors. */ }
    }

    Invalidate() { this.SdkRequest = (this.SdkRequest || 0) + 1; this.Session.Stop(); if (!this.Disposed) this.Diagnostics.clear(); }
    Dispose() {
        if (this.Disposed) return;
        this.Disposed = true;
        this.Invalidate();
        this.Session.removeAllListeners();
        this.Diagnostics.dispose();
    }
}

module.exports = { TypeDiagnostics, GetEntry, GetTypePaths };
