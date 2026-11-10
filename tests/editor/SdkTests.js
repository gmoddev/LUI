'use strict';
const Test = require('node:test');
const Assert = require('node:assert/strict');
const Path = require('node:path');
const { Validate } = require('../../vscode/SdkClient');
const { CreateProject } = require('../../vscode/Onboarding');
const { TypeDiagnostics } = require('../../vscode/TypeDiagnostics');
const { EventEmitter } = require('node:events');
const Os = require('node:os');
const Fs = require('node:fs');

Test('SDK discovery rejects escaped, incomplete and unversioned component results', () => {
    const Root = Path.resolve('SDK with spaces μ');
    const Components = Object.fromEntries(['Cli', 'Checker', 'Definitions', 'Runtime', 'PreviewHost', 'NativeHost', 'ProjectTool'].map(Name => [Name, Path.join(Root, Name)]));
    const Data = { SchemaVersion: 1, Status: 'Ready', SdkVersion: '0.1.0-preview.1', SdkManifestSha256: 'a'.repeat(64), SdkRoot: Root, Components };
    Assert.equal(Validate(Data, Root), Data);
    Assert.throws(() => Validate({ ...Data, SchemaVersion: 2 }, Root));
    Assert.throws(() => Validate({ ...Data, Components: { ...Components, Cli: Path.resolve(Root, '../Elsewhere/lui.exe') } }, Root));
    Assert.throws(() => Validate({ ...Data, Components: { Cli: Components.Cli } }, Root));
    Assert.throws(() => Validate({ ...Data, SdkRoot: Path.resolve('Other') }, Root));
});

Test('untrusted workspaces cannot begin project creation or invoke SDK processes', async () => {
    const Messages = [];
    await CreateProject({ workspace: { isTrusted: false } }, { appendLine: Message => Messages.push(Message) });
    Assert.equal(Messages.length, 1);
    Assert.match(Messages[0], /Trust the workspace/);
});

Test('an SDK machine setting does not start discovery for unrelated workspaces', async () => {
    const Root = Fs.mkdtempSync(Path.join(Os.tmpdir(), 'LuiUnrelatedWorkspace-'));
    const Session = new EventEmitter(); Session.Stop = () => {}; Session.Start = () => Assert.fail('Unrelated project was checked');
    const Messages = [];
    const Vscode = { workspace: { isTrusted: true, textDocuments: [], getConfiguration: () => ({ get: (Name, Default) => Name === 'sdkPath' ? Path.join(Root, 'MissingSdk') : Default }) },
        languages: { createDiagnosticCollection: () => ({ clear() {}, dispose() {} }) } };
    const Controller = new TypeDiagnostics(Vscode, { appendLine: Message => Messages.push(Message) }, Session);
    try {
        await Controller.Check({ uri: { fsPath: Root } }, true);
        Assert.equal(Messages.length, 1);
        Assert.match(Messages[0], /Application manifest was not found/);
    } finally { Controller.Dispose(); Fs.rmSync(Root, { recursive: true, force: true }); }
});
