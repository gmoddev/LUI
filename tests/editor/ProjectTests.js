'use strict';
const Test=require('node:test'), Assert=require('node:assert/strict');
const {SourceMap,ProjectDiagnostics}=require('../../vscode/ProjectProtocol');
const {ValidateTree,ValidateSchema}=require('../../vscode/PreviewModel');
const Schema=require('../../types/schema.json');
const Fs=require('node:fs'), Os=require('node:os'), Path=require('node:path');
const {PreviewClient}=require('../../vscode/PreviewClient');
const {TypeCheckSession}=require('../../vscode/TypeCheckClient');
const {TypeDiagnostics}=require('../../vscode/TypeDiagnostics');
const {SafeSource}=require('../../vscode/ProjectProtocol');
function WaitFor(Emitter,Event,Predicate=()=>true) {
    return new Promise((Resolve,Reject)=>{
        const Cleanup=()=>{clearTimeout(Timer); Emitter.off(Event,Handler); Emitter.off('failure',Failed);};
        const Handler=(...Values)=>{if(Predicate(...Values)) {Cleanup(); Resolve(Values);}};
        const Failed=Message=>{Cleanup(); Reject(new Error(Message));};
        const Timer=setTimeout(()=>Failed('Project event timed out'),15000);
        Emitter.on(Event,Handler); Emitter.on('failure',Failed);
    });
}
Test('real modular editor transactions retain identities, project Problems, redaction and reload recovery',async Context=>{
    const Inputs=['LUI_TEST_PREVIEW_HOST','LUI_TEST_RUNTIME','LUI_TEST_CLI','LUI_TEST_TYPECHECK'].map(Name=>process.env[Name]);
    if(Inputs.some(Value=>!Value)) {Context.skip('Set LUI_TEST tool paths'); return;}
    const [Host,Runtime,Cli,Checker]=Inputs.map(Value=>Path.resolve(__dirname,'../..',Value));
    const Root=Fs.mkdtempSync(Path.join(Os.tmpdir(),'LuiModularEditor-'));
    Fs.cpSync(Path.resolve(__dirname,'../../examples/modular'),Root,{recursive:true});
    const Manifest=Path.join(Root,'lui.json'), Source=Path.join(Root,'Src/State.luau');
    const Client=new PreviewClient(Manifest,Host,Runtime), Types=new TypeCheckSession();
    try {
        const [Tree]=await WaitFor(Client,'tree');
        Assert.equal(Client.Version,2); Assert.equal(Client.Sources.size,5);
        const Private=Tree.Nodes.find(Node=>Node.effectiveSensitive);
        Assert.ok(Private); Assert.equal(Private.text,'[Redacted]');
        Assert.equal(JSON.stringify(Tree.Nodes).includes('SYNTHETIC_PRIVATE_CANARY'),false);
        Assert.equal(SafeSource(Client.Sources,'app:Screens/Home.luau'),Path.join(Root,'Src/Screens/Home.luau'));
        const Original=Fs.readFileSync(Source,'utf8');
        Fs.writeFileSync(Source,'local Wrong: number = "TYPE_PRIVATE_CANARY"\n'+Original);
        const Checked=WaitFor(Types,'result');
        Types.Start({Manifest,Cli,Checker,Definitions:Path.resolve(__dirname,'../../types/LUI.d.luau'),Modular:true});
        const [Result]=await Checked;
        Assert.ok(Result.diagnostics.some(Issue=>Issue.source==='app:State.luau' && Issue.kind==='type'));
        Assert.equal(JSON.stringify(Result.diagnostics).includes('TYPE_PRIVATE_CANARY'),false);
        const Reloaded=WaitFor(Client,'tree',(_,Generation)=>Generation===2); Client.RequestReload(); await Reloaded;
        Fs.writeFileSync(Source,'error("RUNTIME_PRIVATE_CANARY")');
        const Failed=WaitFor(Client,'runtimeError'), Empty=WaitFor(Client,'tree',(_,Generation)=>Generation===3);
        Client.RequestReload(); Assert.equal((await Failed)[0].includes('PRIVATE_CANARY'),false);
        Assert.equal((await Empty)[0].Nodes.length,0); Assert.equal(Client.Sources.size,0);
        Fs.writeFileSync(Source,Original);
        const Recovered=WaitFor(Client,'tree',(_,Generation)=>Generation===4); Client.RequestReload();
        Assert.ok((await Recovered)[0].Nodes.length>0); Assert.equal(Client.Sources.size,5);
        const Exit=WaitFor(Client,'exit'); Client.Stop(); await Exit;
    } finally {Types.Stop(); Client.Stop(); Fs.rmSync(Root,{recursive:true,force:true});}
});
Test('project identities reject escaped paths and duplicate mappings',()=>{
    const Source={id:'app:Main.luau',path:'Src/Main.luau',hash:'a'.repeat(64)};
    Assert.equal(SourceMap([Source],process.cwd()).size,1);
    Assert.throws(()=>SourceMap([{...Source,path:'../Escaped.luau'}],process.cwd()));
    Assert.throws(()=>SourceMap([Source,Source],process.cwd()));
});
Test('first saved modular document starts project checking without an earlier explicit check',()=>{
    const Root=Fs.mkdtempSync(Path.join(Os.tmpdir(),'LuiFirstModule-'));
    try {
        Fs.writeFileSync(Path.join(Root,'lui.json'),JSON.stringify({SchemaVersion:2}));
        for(const Name of ['cli.dll','checker.exe','types.luau']) Fs.writeFileSync(Path.join(Root,Name),'Synthetic');
        const Settings={cliPath:'cli.dll',checkerPath:'checker.exe',definitionsPath:'types.luau'};
        const Folder={uri:{fsPath:Root}}, Session=new(require('node:events').EventEmitter)(); let Started;
        Session.Stop=()=>{}; Session.Start=Paths=>{Started=Paths;};
        const Collection={clear(){},dispose(){}};
        const Vscode={workspace:{isTrusted:true,textDocuments:[],getWorkspaceFolder:()=>Folder,getConfiguration:()=>({get:(Name,Default)=>Settings[Name]??Default})},
            languages:{createDiagnosticCollection:()=>Collection}};
        const Controller=new TypeDiagnostics(Vscode,{appendLine(){}},Session);
        Controller.OnDocument({uri:{scheme:'file',fsPath:Path.join(Root,'Src/State.luau')}});
        Assert.equal(Started?.Modular,true); Controller.Dispose();
    } finally {Fs.rmSync(Root,{recursive:true,force:true});}
});
Test('redacted control payloads remain distinguishable from false or zero',()=>{
    const Node={id:1,parentId:0,className:'CheckBox',name:'[Redacted]',title:'[Redacted]',text:'[Redacted]',source:'[Redacted]',
        accessibilityLabel:'[Redacted]',accessibilityDescription:'[Redacted]',visible:true,enabled:true,checked:null,isFocused:false,
        minimum:null,maximum:null,value:null,redacted:true,bounds:{x:0,y:0,width:20,height:20}};
    Assert.equal(ValidateTree([Node],ValidateSchema(Schema)).Nodes.length,1);
    Assert.throws(()=>ValidateTree([{...Node,redacted:false}],ValidateSchema(Schema)));
});
Test('project diagnostic protocol rejects version downgrade and source mismatch',()=>{
    const Result={version:2,profile:'ProjectTypesV1',diagnosticsMode:'Development',redacted:false,digest:'a'.repeat(64),sourceDigest:'b'.repeat(64),
        truncated:false,sources:[{id:'app:Main.luau',path:'Src/Main.luau',hash:'c'.repeat(64)}],diagnostics:[]};
    Assert.equal(ProjectDiagnostics(Result,process.cwd()).SourceMap.size,1);
    Assert.throws(()=>ProjectDiagnostics({...Result,version:1},process.cwd()));
    Assert.throws(()=>ProjectDiagnostics({...Result,diagnostics:[{source:'app:Missing.luau',kind:'type',message:'Type mismatch',range:{start:{line:0,column:0},end:{line:0,column:1}}}]},process.cwd()));
});
