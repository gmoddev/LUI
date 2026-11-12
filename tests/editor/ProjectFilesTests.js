'use strict';
const Test = require('node:test');
const Assert = require('node:assert/strict');
const Fs = require('node:fs');
const Os = require('node:os');
const Path = require('node:path');
const { ProjectFiles, Inspect } = require('../../vscode/ProjectFiles');

function GetFixture() {
    const Root = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'LuiProjectFiles μ-'));
    Fs.mkdirSync(Path.join(Root, 'Src'));
    const ManifestPath = Path.join(Root, 'lui.json'), File = Path.join(Root, 'Src', 'NewModule.luau');
    const Manifest = { SchemaVersion: 2, Script: 'Src/Main.luau', Capabilities: [], Extensions: [], Assets: [],
        Diagnostics: { Mode: 'Development' }, Modules: { Root: 'Src', Files: ['Main.luau'], Requires: [], Dependencies: [] } };
    const Text = JSON.stringify(Manifest, null, 2).replaceAll('\n', '\r\n');
    Fs.writeFileSync(ManifestPath, Text); Fs.writeFileSync(File, 'return { Value = 42 }\n');
    Fs.writeFileSync(Path.join(Root, 'Src', 'Main.luau'), 'return true\n');
    const Folder = { uri: { scheme: 'file', fsPath: Root } };
    const Uri = Value => ({ scheme: 'file', fsPath: Value });
    const Document = { uri: Uri(ManifestPath), Text, version: 1, isDirty: false,
        getText() { return this.Text; }, positionAt(Offset) { return { Offset }; } };
    const Source = { uri: Uri(File), isDirty: false };
    const Items = new Map(), Messages = [], Edits = [], Settings = { ManifestPath }; let Actions;
    const Vscode = {
        Uri: { file: Uri }, DiagnosticSeverity: { Warning: 1 },
        Range: class { constructor(...Arguments) { this.Arguments = Arguments; } },
        Diagnostic: class { constructor(Range, Message, Severity) { this.range = Range; this.message = Message; this.severity = Severity; } },
        WorkspaceEdit: class { replace(UriValue, Range, TextValue) { this.Edit = { Uri: UriValue, Range, Text: TextValue }; } },
        workspace: {
            isTrusted: true, textDocuments: [Source, Document], workspaceFolders: [Folder],
            getWorkspaceFolder: UriValue => Vscode.workspace.workspaceFolders.find(Value => {
                const Relative = Path.relative(Value.uri.fsPath, UriValue.fsPath);
                return Relative && !Relative.startsWith('..' + Path.sep) && Relative !== '..' && !Path.isAbsolute(Relative);
            }),
            getConfiguration: () => ({ get: (Name, Default) => Name === 'manifestPath' ? Settings.ManifestPath : Default }),
            openTextDocument: async () => Document,
            applyEdit: async Edit => {
                Edits.push(Edit.Edit);
                const [Start, End] = Edit.Edit.Range.Arguments;
                Document.Text = Document.Text.slice(0, Start.Offset) + Edit.Edit.Text + Document.Text.slice(End.Offset);
                Document.version++; Document.isDirty = true;
                return true;
            },
        },
        window: { activeTextEditor: { document: Source }, setStatusBarMessage() {}, showTextDocument: async () => {} },
        languages: {
            createDiagnosticCollection: () => ({ set: (UriValue, Diagnostics) => Items.set(UriValue.fsPath, Diagnostics),
                delete: UriValue => Items.delete(UriValue.fsPath), clear: () => Items.clear(), dispose: () => Items.clear() }),
            registerCodeActionsProvider: (Selector, Provider) => { Actions = Provider; return { dispose() { Actions = null; } }; },
        },
    };
    const Controller = new ProjectFiles(Vscode, { appendLine: Message => Messages.push(Message) });
    return { Root, ManifestPath, File, Text, Manifest, Folder, Document, Source, Items, Messages, Edits, Vscode, Settings, Controller,
        GetActions: () => Actions,
        Cleanup: () => { Controller.Dispose(); Fs.rmSync(Root, { recursive: true, force: true }); },
    };
}

Test('adding a module stages only Modules.Files in the editor buffer and exposes a reviewable quick fix', async () => {
    const Fixture = GetFixture();
    try {
        Fixture.Controller.OnDocument(Fixture.Source);
        const Warning = Fixture.Items.get(Fixture.File);
        Assert.equal(Warning[0].code, 'lui.undeclaredSource');
        const Actions = Fixture.GetActions().provideCodeActions(Fixture.Source, null, { diagnostics: Warning });
        Assert.equal(Actions[0].command, 'lui.addProjectFile'); Assert.equal(Actions[0].arguments[0], Fixture.Source.uri);
        await Fixture.Controller.AddFile();
        Assert.equal(Fixture.Edits.length, 1); Assert.equal(Fixture.Document.isDirty, true);
        Assert.equal(Fixture.Document.Text, Fixture.Text.replace('[\r\n      "Main.luau"\r\n    ]', '["Main.luau","NewModule.luau"]'));
        Assert.equal(Fs.readFileSync(Fixture.ManifestPath, 'utf8'), Fixture.Text);
        Assert.equal(Fs.readFileSync(Fixture.File, 'utf8'), 'return { Value = 42 }\n');
        Assert(Fixture.Messages.at(-1).includes('Review and save'));
        Fixture.Controller.Refresh(); Assert(Fixture.Items.has(Fixture.File), 'Unsaved declaration must not imply saved membership');
        Fs.writeFileSync(Fixture.ManifestPath, Fixture.Document.Text); Fixture.Document.isDirty = false;
        Fixture.Controller.Refresh(); Assert.equal(Fixture.Items.size, 0);
        await Fixture.Controller.AddFile(); Assert.equal(Fixture.Edits.length, 1);
    } finally { Fixture.Cleanup(); }
});

Test('enrollment refuses untrusted workspaces and pre-existing dirty manifest edits', async () => {
    const Fixture = GetFixture();
    try {
        Fixture.Vscode.workspace.isTrusted = false; await Fixture.Controller.AddFile(); Assert.equal(Fixture.Edits.length, 0);
        Fixture.Vscode.workspace.isTrusted = true; Fixture.Document.isDirty = true;
        await Fixture.Controller.AddFile(); Assert.equal(Fixture.Edits.length, 0);
        Assert(Fixture.Messages.at(-1).includes('Save or undo'));
    } finally { Fixture.Cleanup(); }
});

Test('disposal, trust loss, folder removal and changed manifest cancel a pending enrollment', async () => {
    for (const Change of ['Dispose', 'Trust', 'Folder', 'Manifest']) {
        const Fixture = GetFixture();
        try {
            let Resolve;
            Fixture.Vscode.workspace.openTextDocument = () => new Promise(Value => { Resolve = Value; });
            const Operation = Fixture.Controller.AddFile();
            if (Change === 'Dispose') Fixture.Controller.Dispose();
            if (Change === 'Trust') Fixture.Vscode.workspace.isTrusted = false;
            if (Change === 'Folder') Fixture.Vscode.workspace.workspaceFolders = [];
            if (Change === 'Manifest') Fixture.Settings.ManifestPath = Path.join(Fixture.Root, 'Other.json');
            Resolve(Fixture.Document); await Operation;
            Assert.equal(Fixture.Edits.length, 0, Change);
        } finally { Fixture.Cleanup(); }
    }
});

Test('concurrent enrollment does not duplicate or overwrite a staged edit', async () => {
    const Fixture = GetFixture();
    try {
        await Promise.all([Fixture.Controller.AddFile(), Fixture.Controller.AddFile()]);
        Assert.equal(Fixture.Edits.length, 1);
        await Fixture.Controller.AddFile(); Assert.equal(Fixture.Edits.length, 1);
        Assert.deepEqual(JSON.parse(Fixture.Document.Text).Modules.Files, ['Main.luau', 'NewModule.luau']);
    } finally { Fixture.Cleanup(); }
});

Test('project-file hints retain undeclared state when the list is full or spelling collides', async () => {
    const Fixture = GetFixture();
    try {
        for (const Files of [Array.from({ length: 256 }, (_, Index) => 'File' + Index + '.luau'), ['newmodule.luau']]) {
            const Text = JSON.stringify({ ...Fixture.Manifest, Modules: { ...Fixture.Manifest.Modules, Files } });
            Fs.writeFileSync(Fixture.ManifestPath, Text); Fixture.Document.Text = Text;
            Fixture.Controller.OnDocument(Fixture.Source); Assert(Fixture.Items.has(Fixture.File));
            await Fixture.Controller.AddFile(); Assert.equal(Fixture.Edits.length, 0);
            Assert(Fixture.Messages.at(-1).includes('exact path spelling'));
        }
    } finally { Fixture.Cleanup(); }
});

Test('saved source outside Modules.Root is explicitly excluded from project checking', () => {
    const Fixture = GetFixture();
    try {
        const Outside = Path.join(Fixture.Root, 'Outside.luau'); Fs.writeFileSync(Outside, 'error("NOT_RUN")');
        const Document = { uri: { scheme: 'file', fsPath: Outside } };
        Fixture.Controller.OnDocument(Document);
        const Warning = Fixture.Items.get(Outside);
        Assert.equal(Warning[0].code, 'lui.projectSource');
        Assert(Warning[0].message.includes('not been included in project checking'));
        Assert.deepEqual(Fixture.GetActions().provideCodeActions(Document, null, { diagnostics: Warning }), []);
    } finally { Fixture.Cleanup(); }
});

Test('dependency sources count toward the shared limit and declared vendored files receive no false exclusion', async () => {
    const Fixture = GetFixture();
    try {
        const Dependencies = [{ Alias: 'Lib', Root: 'Vendor', Requires: [], Files: Array.from({ length: 8 }, (_, Index) => 'Library' + Index + '.luau') }];
        const Files = Array.from({ length: 248 }, (_, Index) => 'File' + Index + '.luau');
        const Text = JSON.stringify({ ...Fixture.Manifest, Modules: { ...Fixture.Manifest.Modules, Files, Dependencies } });
        Fs.writeFileSync(Fixture.ManifestPath, Text); Fixture.Document.Text = Text;
        Assert.equal(Inspect(Fixture.ManifestPath, Fixture.File, Text).Full, true);
        await Fixture.Controller.AddFile(); Assert.equal(Fixture.Edits.length, 0);
        const Vendor = Path.join(Fixture.Root, 'Vendor'); Fs.mkdirSync(Vendor);
        const Library = Path.join(Vendor, 'Library0.luau'); Fs.writeFileSync(Library, 'return true');
        const Document = { uri: { scheme: 'file', fsPath: Library } };
        Fixture.Controller.OnDocument(Document); Assert.equal(Fixture.Items.size, 0);
        if (process.platform === 'win32') {
            const ManifestPath = Fixture.ManifestPath.replace(/^[a-z]:/i, Drive => Drive.toUpperCase());
            const File = Library.replace(/^[a-z]:/i, Drive => Drive.toLowerCase());
            Assert.equal(Inspect(ManifestPath, File, Text).Dependency, true);
        }
        await Fixture.Controller.AddFile(Document.uri); Assert.equal(Fixture.Edits.length, 0);
        Assert(Fixture.Messages.at(-1).includes('already declared'));
    } finally { Fixture.Cleanup(); }
});

Test('the offset walk handles escaped keys and nested arrays without changing unrelated manifest text', () => {
    const Fixture = GetFixture();
    try {
        const Text = Fixture.Text.replace('"Files"', '"F\\u0069les"').replace('"Requires": []', '"Requires": [], "Future": {"Files": ["braces } [ and \\\"quotes\\\""]}');
        const Candidate = Inspect(Fixture.ManifestPath, Fixture.File, Text);
        const Edited = Text.slice(0, Candidate.Span.Start) + Candidate.Replacement + Text.slice(Candidate.Span.End);
        const Parsed = JSON.parse(Edited);
        Assert.deepEqual(Parsed.Modules.Files, ['Main.luau', 'NewModule.luau']);
        Assert.deepEqual(Parsed.Modules.Future, JSON.parse(Text).Modules.Future);
    } finally { Fixture.Cleanup(); }
});

Test('directory spelling collisions and edits exceeding the manifest byte limit are refused', async () => {
    const Fixture = GetFixture();
    try {
        const Directory = Path.join(Fixture.Root, 'Src', 'foo'); Fs.mkdirSync(Directory);
        const File = Path.join(Directory, 'B.luau'); Fs.writeFileSync(File, 'return true');
        const Text = JSON.stringify({ ...Fixture.Manifest, Modules: { ...Fixture.Manifest.Modules, Files: ['Foo/A.luau'] } });
        Assert.equal(Inspect(Fixture.ManifestPath, File, Text).Collision, true);
        const Base = JSON.stringify(Fixture.Manifest);
        const Padded = Base + ' '.repeat(256 * 1024 - Buffer.byteLength(Base));
        Fs.writeFileSync(Fixture.ManifestPath, Padded); Fixture.Document.Text = Padded;
        Assert.throws(() => Inspect(Fixture.ManifestPath, Fixture.File, Padded), /ManifestLimit/);
        await Fixture.Controller.AddFile(); Assert.equal(Fixture.Edits.length, 0);
    } finally { Fixture.Cleanup(); }
});

Test('invalid, ambiguous, oversized and escaped declarations never produce a manifest edit', () => {
    const Fixture = GetFixture();
    try {
        const Invalid = [Fixture.Text.replace('"SchemaVersion": 2', '"SchemaVersion": 1'),
            Fixture.Text.replace('"Root": "Src"', '"Root": "../Other"'),
            Fixture.Text.replace('"Files": [', '"Files": [], "Files": ['),
            Fixture.Text.replace('"Main.luau"', '"../Main.luau"'),
            Fixture.Text.replace('"Main.luau"', '"CON.luau"'),
            Fixture.Text + ' '.repeat(256 * 1024), '{PRIVATE_JSON_CANARY'];
        for (const Text of Invalid) Assert.throws(() => Inspect(Fixture.ManifestPath, Fixture.File, Text));
        Assert.throws(() => Inspect(Fixture.ManifestPath, Path.join(Fixture.Root, 'Other.luau'), Fixture.Text));
        Assert.throws(() => Inspect(Fixture.ManifestPath, Path.join(Fixture.Root, '..', 'Outside.luau'), Fixture.Text));
        Assert.throws(() => Inspect(Fixture.ManifestPath, Fixture.File.replace('NewModule', 'newmodule'), Fixture.Text));
        const External = Path.join(Fixture.Root, 'External'); Fs.mkdirSync(External); Fs.writeFileSync(Path.join(External, 'Linked.luau'), 'return true');
        Fs.symlinkSync(External, Path.join(Fixture.Root, 'Src', 'Linked'), process.platform === 'win32' ? 'junction' : 'dir');
        Assert.throws(() => Inspect(Fixture.ManifestPath, Path.join(Fixture.Root, 'Src', 'Linked', 'Linked.luau'), Fixture.Text));
    } finally { Fixture.Cleanup(); }
});
