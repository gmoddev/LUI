'use strict';

const Fs = require('node:fs');
const Path = require('node:path');
const { TypeCheckSession } = require('./TypeCheckClient');

function GetTypePaths(Folder, Vscode) {
    const Root = Folder.uri.fsPath;
    const Repo = Path.resolve(__dirname, '..');
    const Settings = Vscode.workspace.getConfiguration('lui', Folder.uri);
    const Configured = Name => {
        const Value = Settings.get(Name, '');
        return Value ? Path.resolve(Root, Value) : '';
    };
    const First = Candidates => Candidates.find(Candidate => Fs.existsSync(Candidate)) || '';
    const Cli = Configured('cliPath') || First([Root, Repo].flatMap(Directory => ['Debug', 'Release'].map(Configuration =>
        Path.join(Directory, `tools/cli/bin/${Configuration}/net9.0/lui.dll`))));
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
    if (Fs.statSync(ManifestPath).size > 64 * 1024) throw new Error('Application manifest exceeds 64 KiB');
    const Manifest = JSON.parse(Fs.readFileSync(ManifestPath, 'utf8'));
    const Root = Path.dirname(ManifestPath);
    if (Manifest.SchemaVersion !== 1 || typeof Manifest.Script !== 'string' || !Manifest.Script || Path.isAbsolute(Manifest.Script))
        throw new Error('Invalid application entry script');
    const Script = Path.resolve(Root, Manifest.Script);
    const Relative = Path.relative(Root, Script);
    if (!Relative || Relative === '..' || Relative.startsWith('..' + Path.sep) || Path.isAbsolute(Relative))
        throw new Error('Application entry script left the manifest directory');
    // Match the shared manifest's prohibition on reparse points/symlinks.
    let Component = Root;
    for (const Part of Relative.split(Path.sep)) {
        Component = Path.join(Component, Part);
        if (Fs.lstatSync(Component).isSymbolicLink()) throw new Error('Application entry script uses a symlink');
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
        Session.on('failure', Message => this.Log(Message));
        Session.on('result', Result => {
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

    Check(Folder, Explicit = false) {
        this.Invalidate();
        if (!Folder) { if (Explicit) this.Log('Open an application workspace folder to check types.'); return; }
        const Vscode = this.Vscode;
        if (!Vscode.workspace.isTrusted) { if (Explicit) this.Log('Type checking requires a trusted workspace.'); return; }
        if (!Explicit && !Vscode.workspace.getConfiguration('lui', Folder.uri).get('autoCheck', true)) return;
        const Paths = GetTypePaths(Folder, Vscode);
        if (!Fs.existsSync(Paths.Manifest)) { if (Explicit) this.Log('Application manifest was not found.'); return; }
        try {
            for (const [Name, Value] of [['CLI', Paths.Cli], ['checker', Paths.Checker], ['definitions', Paths.Definitions]])
                if (!Value || !Fs.existsSync(Value)) throw new Error(`${Name} was not found. Build the tooling or set the LUI type-check settings.`);
            const { Script, SourceName } = GetEntry(Paths.Manifest);
            const Dirty = Vscode.workspace.textDocuments.some(Document => Document.isDirty &&
                [Script, Paths.Manifest].includes(Path.resolve(Document.uri.fsPath)));
            if (Dirty) { if (Explicit) this.Log('Save the entry script and manifest before checking types.'); return; }
            if (Fs.statSync(Script).size > 8 * 1024 * 1024) throw new Error('Entry script exceeds 8 MiB');
            const Source = new TextDecoder('utf-8', { fatal: true }).decode(Fs.readFileSync(Script));
            this.Script = Script;
            this.Manifest = Paths.Manifest;
            this.Session.Start({ ...Paths, SourceName, Source });
        } catch (Error) { this.Log(Error.message); }
    }

    OnChange(Document) {
        const File = Path.resolve(Document.uri.fsPath);
        if (File === this.Script || File === this.Manifest) this.Invalidate();
    }

    OnDocument(Document) {
        if (Document.uri.scheme !== 'file') return;
        const Folder = this.Vscode.workspace.getWorkspaceFolder(Document.uri);
        if (!Folder) return;
        const { Manifest } = GetTypePaths(Folder, this.Vscode);
        try {
            const File = Path.resolve(Document.uri.fsPath);
            if (File === Manifest || File === GetEntry(Manifest).Script) this.Check(Folder);
        } catch { /* No matching readable entry script yet. Explicit checks report setup errors. */ }
    }

    Invalidate() { this.Session.Stop(); this.Diagnostics.clear(); }
    Dispose() { this.Invalidate(); this.Session.removeAllListeners(); this.Diagnostics.dispose(); }
}

module.exports = { TypeDiagnostics, GetEntry, GetTypePaths };
