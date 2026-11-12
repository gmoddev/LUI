'use strict';

const Fs = require('node:fs');
const Path = require('node:path');
const { GetManifest } = require('./SdkClient');
const Undeclared = 'This file is not declared in Modules.Files. Use LUI: Add File to Project, review the manifest, then save.';

function PortablePath(Value, Root = false) {
    return Root && Value === '.' || typeof Value === 'string' && Value.length > 0 && Value.length <= 512 &&
        /^[A-Za-z0-9_./-]+$/.test(Value) && Value.split('/').every(Part => Part && Part !== '.' && Part !== '..' &&
            !Part.endsWith('.') && !/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(Part));
}

function CheckFile(Root, File) {
    const Relative = Path.relative(Root, File);
    if (!Relative || Path.isAbsolute(Relative) || Relative === '..' || Relative.startsWith('..' + Path.sep)) throw new Error('OutsideProject');
    let Current = Root;
    const Parts = Relative.split(Path.sep);
    if (Fs.lstatSync(Current).isSymbolicLink()) throw new Error('LinkedPath');
    for (let Index = 0; Index < Parts.length; Index++) {
        if (!Fs.readdirSync(Current).includes(Parts[Index])) throw new Error('ExactCaseRequired');
        Current = Path.join(Current, Parts[Index]);
        const Stat = Fs.lstatSync(Current);
        if (Stat.isSymbolicLink() || (Index === Parts.length - 1 ? !Stat.isFile() : !Stat.isDirectory())) throw new Error('LinkedPath');
    }
}

function CaseCollision(Left, Right) {
    const A = Left.split('/'), B = Right.split('/');
    for (let Index = 0; Index < Math.min(A.length, B.length); Index++) {
        if (A[Index].toLowerCase() !== B[Index].toLowerCase()) return false;
        if (A[Index] !== B[Index]) return true;
    }
    return false;
}

// Locate one array for a minimal text edit. JSON.parse supplies syntax checking;
// this bounded walk only retains offsets and rejects ambiguous duplicate keys.
function FilesRange(Text) {
    const Tokens = [...Text.matchAll(/"(?:\\.|[^"\\])*"|[{}\[\]:,]|[^\s{}\[\]:,]+/g)];
    let Cursor = 0, Range;
    function Visit(Keys, Depth) {
        if (Depth > 32) throw new Error('ManifestDepth');
        const First = Tokens[Cursor++];
        if (First[0] === '{') {
            const Names = new Set();
            while (Tokens[Cursor][0] !== '}') {
                const Key = JSON.parse(Tokens[Cursor++][0]);
                if (Names.has(Key)) throw new Error('DuplicateKey');
                Names.add(Key); Cursor++; Visit([...Keys, Key], Depth + 1);
                if (Tokens[Cursor][0] === ',') Cursor++;
            }
            Cursor++;
        } else if (First[0] === '[') {
            while (Tokens[Cursor][0] !== ']') {
                Visit([...Keys, '*'], Depth + 1);
                if (Tokens[Cursor][0] === ',') Cursor++;
            }
            Cursor++;
        }
        if (Keys.length === 2 && Keys[0] === 'Modules' && Keys[1] === 'Files')
            Range = { Start: First.index, End: Tokens[Cursor - 1].index + Tokens[Cursor - 1][0].length };
    }
    Visit([], 0);
    if (!Range) throw new Error('MissingFiles');
    return Range;
}

function Inspect(ManifestPath, File, Text) {
    if (Buffer.byteLength(Text, 'utf8') > 256 * 1024) throw new Error('ManifestLimit');
    const Manifest = JSON.parse(Text);
    if (Manifest.SchemaVersion !== 2 || !PortablePath(Manifest.Modules?.Root, true) ||
        !Array.isArray(Manifest.Modules.Files) || Manifest.Modules.Files.length > 256 || !Array.isArray(Manifest.Modules.Dependencies) ||
        Manifest.Modules.Files.some(Value => !PortablePath(Value) || !Value.endsWith('.luau'))) throw new Error('InvalidModules');
    let Total = Manifest.Modules.Files.length;
    for (const Dependency of Manifest.Modules.Dependencies) {
        if (!PortablePath(Dependency.Root, true) || !Array.isArray(Dependency.Files) || Dependency.Files.length > 256 ||
            Dependency.Files.some(Value => !PortablePath(Value) || !Value.endsWith('.luau'))) throw new Error('InvalidModules');
        Total += Dependency.Files.length;
    }
    if (Total > 256) throw new Error('FileLimit');
    const Span = FilesRange(Text);
    const Root = Path.dirname(ManifestPath);
    CheckFile(Root, ManifestPath); CheckFile(Root, File);
    const ProjectRelative = Path.relative(Root, File).split(Path.sep).join('/');
    if (Manifest.Modules.Dependencies.some(Dependency => Dependency.Files.some(Value =>
        (Dependency.Root === '.' ? Value : Dependency.Root + '/' + Value) === ProjectRelative)))
        return { Declared: true, Dependency: true };
    const ModuleRoot = Path.resolve(Root, Manifest.Modules.Root);
    const Relative = Path.relative(ModuleRoot, File).split(Path.sep).join('/');
    if (!PortablePath(Relative) || !Relative.endsWith('.luau')) throw new Error('OutsideModuleRoot');
    const Files = Manifest.Modules.Files;
    const Declared = Files.includes(Relative);
    if (Declared) return { Declared: true };
    const Join = (RootValue, Value) => RootValue === '.' ? Value : RootValue + '/' + Value;
    const CandidatePath = Join(Manifest.Modules.Root, Relative);
    const Paths = [...Files.map(Value => Join(Manifest.Modules.Root, Value)),
        ...Manifest.Modules.Dependencies.flatMap(Dependency => Dependency.Files.map(Value => Join(Dependency.Root, Value)))];
    const Collision = Paths.some(Value => CaseCollision(Value, CandidatePath));
    const Replacement = JSON.stringify([...Files, Relative]);
    if (Buffer.byteLength(Text.slice(0, Span.Start) + Replacement + Text.slice(Span.End), 'utf8') > 256 * 1024) throw new Error('ManifestLimit');
    return { Declared: false, Relative, Span, Collision, Full: Total === 256, Replacement };
}

class ProjectFiles {
    constructor(Vscode, Output) {
        this.Vscode = Vscode;
        this.Output = Output;
        this.Diagnostics = Vscode.languages.createDiagnosticCollection('LUI Project');
        this.Actions = Vscode.languages.registerCodeActionsProvider({ scheme: 'file', language: 'luau' }, {
            provideCodeActions: (Document, Range, Context) => Context.diagnostics.some(Item => Item.code === 'lui.undeclaredSource') ?
                [{ title: 'Add file to project', command: 'lui.addProjectFile', arguments: [Document.uri] }] : [],
        });
    }

    Message(Text) {
        this.Output.appendLine('[LUI:Project] ' + Text);
        this.Vscode.window.setStatusBarMessage('[LUI:Project] ' + Text, 7000);
    }

    GetInputs(Uri) {
        if (Uri?.scheme !== 'file' || !Uri.fsPath.endsWith('.luau')) throw new Error('SelectSource');
        const Folder = this.Vscode.workspace.getWorkspaceFolder(Uri);
        if (!Folder) throw new Error('MissingFolder');
        const ManifestPath = GetManifest(Folder, this.Vscode);
        CheckFile(Folder.uri.fsPath, ManifestPath);
        if (Fs.statSync(ManifestPath).size > 256 * 1024) throw new Error('ManifestLimit');
        return { Folder, ManifestPath, File: Uri.fsPath };
    }

    OnDocument(Document) {
        if (this.Disposed || Document.uri.scheme !== 'file' || !Document.uri.fsPath.endsWith('.luau')) return;
        this.Diagnostics.delete(Document.uri);
        if (!this.Vscode.workspace.isTrusted) return;
        try {
            const Inputs = this.GetInputs(Document.uri);
            const Text = new TextDecoder('utf-8', { fatal: true }).decode(Fs.readFileSync(Inputs.ManifestPath));
            if (JSON.parse(Text).SchemaVersion !== 2) return;
            let Message = Undeclared, Code = 'lui.undeclaredSource';
            try { if (Inspect(Inputs.ManifestPath, Inputs.File, Text).Declared) return; }
            catch { Message = 'This file has not been included in project checking. Check its saved path, Modules.Root and Modules.Files.'; Code = 'lui.projectSource'; }
            const Diagnostic = new this.Vscode.Diagnostic(new this.Vscode.Range(0, 0, 0, 1), Message, this.Vscode.DiagnosticSeverity.Warning);
            Diagnostic.source = 'LUI project'; Diagnostic.code = Code;
            this.Diagnostics.set(Document.uri, [Diagnostic]);
        } catch { /* The shared checker reports malformed projects; this hint claims no validation. */ }
    }

    Refresh() {
        if (this.Disposed) return;
        this.Diagnostics.clear();
        for (const Document of this.Vscode.workspace.textDocuments) this.OnDocument(Document);
    }

    async AddFile(Uri = this.Vscode.window.activeTextEditor?.document.uri) {
        if (this.Disposed || this.Adding) return;
        if (!this.Vscode.workspace.isTrusted) { this.Message('Trust the workspace before editing its project declaration.'); return; }
        this.Adding = true;
        try {
            const Inputs = this.GetInputs(Uri);
            const Document = await this.Vscode.workspace.openTextDocument(this.Vscode.Uri.file(Inputs.ManifestPath));
            if (this.Disposed || !this.Vscode.workspace.isTrusted) return;
            const Current = this.GetInputs(Uri);
            if (Current.ManifestPath !== Inputs.ManifestPath || Current.Folder !== Inputs.Folder) return;
            if (Document.isDirty) { this.Message('Save or undo existing manifest edits before adding a file.'); return; }
            const Text = Document.getText();
            const Candidate = Inspect(Inputs.ManifestPath, Inputs.File, Text);
            if (Candidate.Declared) { this.Message('This file is already declared in the project.'); return; }
            if (Candidate.Collision || Candidate.Full) { this.Message('Could not add the file: check exact path spelling and the 256-file project limit.'); return; }
            const Range = new this.Vscode.Range(Document.positionAt(Candidate.Span.Start), Document.positionAt(Candidate.Span.End));
            const Edit = new this.Vscode.WorkspaceEdit();
            Edit.replace(Document.uri, Range, Candidate.Replacement);
            if (!await this.Vscode.workspace.applyEdit(Edit)) { this.Message('The manifest changed. Review it and try again.'); return; }
            if (this.Disposed) return;
            await this.Vscode.window.showTextDocument(Document, { preview: false, selection: Range });
            this.Message('File added to the manifest buffer. Review and save it, then run Check Types.');
        } catch { if (!this.Disposed) this.Message('Could not add this file. Select a saved .luau file inside Modules.Root and check the manifest, path spelling and file limit.'); }
        finally { this.Adding = false; }
    }

    Dispose() {
        if (this.Disposed) return;
        this.Disposed = true; this.Actions.dispose(); this.Diagnostics.dispose();
    }
}

module.exports = { ProjectFiles, Inspect };
