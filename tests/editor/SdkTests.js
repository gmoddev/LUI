'use strict';
const Test = require('node:test');
const Assert = require('node:assert/strict');
const Path = require('node:path');
const { Validate, ResolveSdk, GetBinding } = require('../../vscode/SdkClient');
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
    Assert.throws(() => Validate({ ...Data, SdkRoot: { Private: 'SENSITIVE_ROOT_CANARY' } }, Root), /Invalid SDK discovery result/);
});

function GetSdkFixture() {
    const Parent = Path.resolve(Os.tmpdir());
    const Root = Fs.mkdtempSync(Path.join(Parent, 'LuiSdkFailure μ-'));
    const Cli = Path.join(Root, 'Cli', process.platform === 'win32' ? 'lui.exe' : 'lui');
    const Components = Object.fromEntries(['Cli', 'Checker', 'Definitions', 'Runtime', 'PreviewHost', 'NativeHost', 'ProjectTool'].map(Name => [Name, Name === 'Cli' ? Cli : Path.join(Root, Name)]));
    const Ready = { SchemaVersion: 1, Status: 'Ready', SdkVersion: '0.1.0-preview.2', SdkManifestSha256: 'a'.repeat(64), SdkRoot: Root, Components };
    const Vscode = { workspace: { isTrusted: true, getConfiguration: () => ({ get: (Name, Default) => Name === 'sdkPath' ? Root : Default }) } };
    return { Root, Cli, Ready, Vscode,
        Install() {
            Fs.mkdirSync(Path.dirname(Cli));
            try { Fs.linkSync(process.execPath, Cli); }
            catch (Failure) { if (!['EXDEV', 'EPERM', 'EACCES'].includes(Failure.code)) throw Failure; Fs.copyFileSync(process.execPath, Cli); }
        },
        Doctor(Output, Code = 0) { Fs.writeFileSync(Path.join(Root, 'doctor'), `process.stdout.write(${JSON.stringify(Output)});process.stderr.write('SENSITIVE_CHILD_CANARY');process.exit(${Code});`); },
        Dispose() {
            Assert.equal(Path.dirname(Path.resolve(Root)), Parent);
            Assert(Path.basename(Root).startsWith('LuiSdkFailure μ-'));
            Fs.rmSync(Root, { recursive: true, force: true, maxRetries: 5, retryDelay: 50 });
        },
    };
}

Test('failed SDK discovery settles with a fixed error for missing and nonzero CLI children', { timeout: 10000 }, async () => {
    for (const Mode of ['Missing', 'Nonzero']) {
        const Fixture = GetSdkFixture();
        try {
            if (Mode === 'Nonzero') { Fixture.Install(); Fixture.Doctor('SENSITIVE_DOCTOR_CANARY', 3); }
            await Assert.rejects(ResolveSdk(null, Fixture.Vscode), Failure => {
                Assert.equal(Failure.message, 'SDK unavailable or does not match this project. Use LUI: Choose SDK; run lui doctor for details.');
                Assert.doesNotMatch(Failure.message, /SENSITIVE|CANARY|LuiSdkFailure/);
                return true;
            });
            Assert.throws(() => GetBinding(null, Fixture.Vscode), /Choose an installed SDK/);
        } finally { Fixture.Dispose(); }
    }
});

Test('malformed successful doctor output rejects without echoing a canary prefix', { timeout: 10000 }, async () => {
    const Fixture = GetSdkFixture();
    try {
        Fixture.Install(); Fixture.Doctor('SENSITIVE_SDK_DOCTOR_CANARY');
        await Assert.rejects(ResolveSdk(null, Fixture.Vscode), Failure => {
            Assert.equal(Failure.message, 'Invalid SDK discovery result. Use LUI: Choose SDK; run lui doctor for details.');
            Assert.doesNotMatch(Failure.message, /SENSITIVE|DOCTOR_CANARY|LuiSdkFailure/);
            return true;
        });
        Assert.throws(() => GetBinding(null, Fixture.Vscode), /Choose an installed SDK/);
    } finally { Fixture.Dispose(); }
});

function SetCountedDoctor(Fixture, Code = 0) {
    const Calls = Path.join(Fixture.Root, 'Calls');
    Fs.writeFileSync(Path.join(Fixture.Root, 'doctor'), `require('fs').appendFileSync(${JSON.stringify(Calls)},'1');setTimeout(()=>{process.stdout.write(${JSON.stringify(JSON.stringify(Fixture.Ready))});process.exit(${Code});},200);`);
    return () => Fs.existsSync(Calls) ? Fs.readFileSync(Calls, 'utf8').length : 0;
}

Test('type checking cannot switch to an earlier binding or launch after folder removal or disposal', async () => {
    for (const Mode of ['Manifest','Folder','DisposeFailure']) {
        const Fixture=GetSdkFixture(); let Controller;
        try {
            Fixture.Install(); SetCountedDoctor(Fixture);
            for (const [Name,File] of Object.entries(Fixture.Ready.Components)) if (Name!=='Cli') Fs.writeFileSync(File,'Synthetic');
            const Project=Path.join(Fixture.Root,'Project'); Fs.mkdirSync(Project);
            for (const Name of ['lui.json','Other.json']) Fs.writeFileSync(Path.join(Project,Name),'{"SchemaVersion":2}');
            const Folder={uri:{fsPath:Project}};
            const Settings=new Map([['sdkPath',Fixture.Root],['manifestPath','Other.json']]);
            const Vscode={workspace:{isTrusted:true,workspaceFolders:[Folder],textDocuments:[],getConfiguration:()=>({get:(Name,Default)=>Settings.get(Name)??Default})},
                languages:{createDiagnosticCollection:()=>({clear(){},dispose(){}})}};
            await ResolveSdk(Folder,Vscode); Settings.set('manifestPath','lui.json');
            const Session=new EventEmitter(); Session.Stop=()=>{}; let Starts=0; Session.Start=()=>Starts++;
            const Messages=[]; Controller=new TypeDiagnostics(Vscode,{appendLine:Message=>Messages.push(Message)},Session);
            if (Mode==='DisposeFailure') Fixture.Doctor('PRIVATE_RETIRED_SDK',3);
            const Check=Controller.Check(Folder,true);
            if (Mode==='Manifest') Settings.set('manifestPath','Other.json');
            if (Mode==='Folder') Vscode.workspace.workspaceFolders=[];
            if (Mode==='DisposeFailure') Controller.Dispose();
            await Check;
            Assert.equal(Starts,0,Mode); Assert.equal(Messages.length,0,Mode);
        } finally { Controller?.Dispose(); Fixture.Dispose(); }
    }
});

Test('overlapping SDK discovery shares one child but later calls recheck integrity', { timeout: 10000 }, async () => {
    const Fixture = GetSdkFixture();
    try {
        Fixture.Install(); const Count = SetCountedDoctor(Fixture);
        const Root = Path.join(Fixture.Root, 'Project'); Fs.mkdirSync(Root);
        const Folder = { uri: { fsPath: Root } };
        Fs.writeFileSync(Path.join(Root, 'lui.json'), '{}');
        Fs.writeFileSync(Path.join(Root, 'LuiToolchain.json'), JSON.stringify({ SchemaVersion: 1, SdkVersion: Fixture.Ready.SdkVersion, SdkManifestSha256: Fixture.Ready.SdkManifestSha256 }));
        const Results = await Promise.all(Array.from({ length: 8 }, () => ResolveSdk(Folder, Fixture.Vscode)));
        Assert.equal(Count(), 1);
        for (const Result of Results) Assert.equal(Result, Results[0]);
        Assert.equal(GetBinding(Folder, Fixture.Vscode), Results[0]);
        await ResolveSdk(Folder, Fixture.Vscode);
        Assert.equal(Count(), 2);
    } finally { Fixture.Dispose(); }
});

Test('shared failed discovery settles every caller and permits a fresh repaired attempt', { timeout: 10000 }, async () => {
    const Fixture = GetSdkFixture();
    try {
        Fixture.Install(); const Count = SetCountedDoctor(Fixture, 3);
        const Results = await Promise.allSettled(Array.from({ length: 4 }, () => ResolveSdk(null, Fixture.Vscode)));
        Assert.equal(Count(), 1);
        for (const Result of Results) { Assert.equal(Result.status, 'rejected'); Assert.match(Result.reason.message, /^SDK unavailable/); }
        Assert.throws(() => GetBinding(null, Fixture.Vscode), /Choose an installed SDK/);
        SetCountedDoctor(Fixture);
        Assert.equal((await ResolveSdk(null, Fixture.Vscode)).Status, 'Ready');
        Assert.equal(Count(), 2);
    } finally { Fixture.Dispose(); }
});

Test('in-progress SDK discovery is isolated by SDK root and project manifest', { timeout: 10000 }, async () => {
    const First = GetSdkFixture(), Second = GetSdkFixture();
    try {
        First.Install(); Second.Install();
        const CountFirst = SetCountedDoctor(First), CountSecond = SetCountedDoctor(Second);
        const Folder = Name => {
            const Root = Path.join(First.Root, Name); Fs.mkdirSync(Root); Fs.writeFileSync(Path.join(Root, 'lui.json'), '{}');
            return { uri: { fsPath: Root } };
        };
        const One = Folder('One'), Two = Folder('Two');
        const Results = await Promise.all([ResolveSdk(One, First.Vscode), ResolveSdk(Two, First.Vscode), ResolveSdk(One, Second.Vscode)]);
        Assert.equal(CountFirst(), 2); Assert.equal(CountSecond(), 1);
        Assert.equal(Results[0].SdkRoot, First.Root); Assert.equal(Results[1].SdkRoot, First.Root); Assert.equal(Results[2].SdkRoot, Second.Root);
    } finally { First.Dispose(); Second.Dispose(); }
});

Test('a newly created manifest and SDK pin never share a prior SDK-only discovery', { timeout: 10000 }, async () => {
    const Fixture = GetSdkFixture();
    try {
        Fixture.Install();
        const Root = Path.join(Fixture.Root, 'Project'); Fs.mkdirSync(Root);
        const Folder = { uri: { fsPath: Root } }, Manifest = Path.join(Root, 'lui.json'), Calls = Path.join(Fixture.Root, 'Calls');
        Fs.writeFileSync(Path.join(Fixture.Root, 'doctor'), `const Project=process.argv[2]===${JSON.stringify(Manifest)};require('fs').appendFileSync(${JSON.stringify(Calls)},Project?'1':'0');setTimeout(()=>{process.stdout.write(${JSON.stringify(JSON.stringify(Fixture.Ready))});process.exit(Project?3:0);},200);`);
        const Before = ResolveSdk(Folder, Fixture.Vscode);
        Fs.writeFileSync(Manifest, '{}'); Fs.writeFileSync(Path.join(Root, 'LuiToolchain.json'), '{"SdkVersion":"mismatch"}');
        const Results = await Promise.allSettled([Before, ResolveSdk(Folder, Fixture.Vscode)]);
        Assert.equal(Results[0].status, 'fulfilled'); Assert.equal(Results[1].status, 'rejected');
        Assert.match(Results[1].reason.message, /^SDK unavailable/);
        Assert.equal(Fs.readFileSync(Calls, 'utf8').split('').sort().join(''), '01');
    } finally { Fixture.Dispose(); }
});

Test('same-size SDK pin edits cannot share an earlier in-progress validation', { timeout: 10000 }, async () => {
    const Fixture = GetSdkFixture();
    try {
        Fixture.Install();
        const Root = Path.join(Fixture.Root, 'Project'); Fs.mkdirSync(Root);
        const Folder = { uri: { fsPath: Root } }, Pin = Path.join(Root, 'LuiToolchain.json'), Marker = Path.join(Fixture.Root, 'ReadPin'), Calls = Path.join(Fixture.Root, 'Calls');
        Fs.writeFileSync(Path.join(Root, 'lui.json'), '{}'); Fs.writeFileSync(Pin, '{"Pin":"a"}');
        Fs.writeFileSync(Path.join(Fixture.Root, 'doctor'), `const Fs=require('fs');const Valid=Fs.readFileSync(${JSON.stringify(Pin)},'utf8')==='{"Pin":"a"}';Fs.appendFileSync(${JSON.stringify(Calls)},'1');Fs.writeFileSync(${JSON.stringify(Marker)},'read');setTimeout(()=>{process.stdout.write(${JSON.stringify(JSON.stringify(Fixture.Ready))});process.exit(Valid?0:3);},500);`);
        const Before = ResolveSdk(Folder, Fixture.Vscode);
        const Deadline = Date.now() + 3000;
        while (!Fs.existsSync(Marker) && Date.now() < Deadline) await new Promise(Resolve => setTimeout(Resolve, 10));
        Assert(Fs.existsSync(Marker), 'The first real child must read the original pin');
        Fs.writeFileSync(Pin, '{"Pin":"b"}');
        const Results = await Promise.allSettled([Before, ResolveSdk(Folder, Fixture.Vscode)]);
        Assert.equal(Results[0].status, 'fulfilled'); Assert.equal(Results[1].status, 'rejected');
        Assert.equal(Fs.readFileSync(Calls, 'utf8'), '11');
    } finally { Fixture.Dispose(); }
});

Test('pins outside the small regular-file coalescing profile still use separate native discovery', { timeout: 10000 }, async () => {
    const Fixture = GetSdkFixture();
    try {
        Fixture.Install(); const Count = SetCountedDoctor(Fixture);
        const Root = Path.join(Fixture.Root, 'Project'); Fs.mkdirSync(Root);
        const Folder = { uri: { fsPath: Root } }, Pin = Path.join(Root, 'LuiToolchain.json');
        Fs.writeFileSync(Path.join(Root, 'lui.json'), '{}'); Fs.writeFileSync(Pin, ' '.repeat(9000) + '{}');
        await Promise.all([ResolveSdk(Folder, Fixture.Vscode), ResolveSdk(Folder, Fixture.Vscode)]);
        Assert.equal(Count(), 2);
        Fs.unlinkSync(Pin); Fs.mkdirSync(Pin);
        await Promise.all([ResolveSdk(Folder, Fixture.Vscode), ResolveSdk(Folder, Fixture.Vscode)]);
        Assert.equal(Count(), 4);
    } finally { Fixture.Dispose(); }
});

Test('failed project creation settles and never opens or marks a project created', { timeout: 10000 }, async () => {
    const Fixture = GetSdkFixture();
    const Messages = [], Statuses = [], Commands = [], State = [];
    try {
        Fixture.Install(); Fixture.Doctor(JSON.stringify(Fixture.Ready));
        Fs.writeFileSync(Path.join(Fixture.Root, 'new'), "process.stderr.write('SENSITIVE_CREATION_CANARY');process.exit(4);");
        Fixture.Vscode.window = { showOpenDialog: async () => [{ fsPath: Fixture.Root }], showInputBox: async () => 'RejectedProject', setStatusBarMessage: Message => Statuses.push(Message) };
        Fixture.Vscode.commands = { executeCommand: async (...Arguments) => Commands.push(Arguments) };
        Fixture.Vscode.Uri = { file: Value => Value };
        await CreateProject(Fixture.Vscode, { appendLine: Message => Messages.push(Message) }, { globalState: { update: async (...Arguments) => State.push(Arguments) } });
        Assert.deepEqual(Messages, ['[LUI:SDK] Project creation failed. Choose a new directory and check the SDK.']);
        Assert.deepEqual(Statuses, Messages);
        Assert.deepEqual(Commands, []); Assert.deepEqual(State, []);
        Assert.equal(Fs.existsSync(Path.join(Fixture.Root, 'RejectedProject')), false);
        Assert.doesNotMatch(Messages.join('\n'), /SENSITIVE|CANARY|LuiSdkFailure/);
    } finally { Fixture.Dispose(); }
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
    const Vscode = { workspace: { isTrusted: true, textDocuments: [], workspaceFolders: [{uri:{fsPath:Root}}], getConfiguration: () => ({ get: (Name, Default) => Name === 'sdkPath' ? Path.join(Root, 'MissingSdk') : Default }) },
        languages: { createDiagnosticCollection: () => ({ clear() {}, dispose() {} }) } };
    const Controller = new TypeDiagnostics(Vscode, { appendLine: Message => Messages.push(Message) }, Session);
    try {
        await Controller.Check({ uri: { fsPath: Root } }, true);
        Assert.equal(Messages.length, 1);
        Assert.match(Messages[0], /Application manifest was not found/);
    } finally { Controller.Dispose(); Fs.rmSync(Root, { recursive: true, force: true }); }
});
