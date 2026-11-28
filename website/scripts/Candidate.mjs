import { readFile as ReadFile, writeFile as WriteFile, mkdir as MakeDirectory } from 'node:fs/promises';
import Path from 'node:path';
import { ModuleDeclarations } from './LibraryReference.mjs';
import { ReadText } from './Releases.mjs';
import { Declarations } from './Reference.mjs';

async function WriteRuntimeLibraries(Repository, Output, Experimental = false) {
  const Version = Experimental ? '0.1.0-preview.4-ffi.1' : '0.1.0-preview.3';
  const Filter = Experimental ? 'experimental FFI' : 'preview.3 candidate';
  const Directory = Path.join(Output, 'libraries');
  await MakeDirectory(Directory, {recursive: true});
  for (const Name of Experimental ? ['Ffi', 'FfiBroker'] : ['Binary', 'Config', 'Encoding', 'Path', 'Spec', 'Utc', 'Stdio', 'Compression', 'Crypto', 'SQLite', 'Regex', 'Toml', 'Process', 'Context', 'Args', 'Messages', 'Workers', 'Language', 'Random']) {
    const Library = Path.join(Repository, 'libraries', Name);
    const Catalog = JSON.parse(await ReadText(Path.join(Library, 'Library.json')));
    const [Module] = Catalog.Modules;
    if (Catalog.TargetSdk !== Version || Catalog.Name !== Name ||
        Catalog.Modules.length !== 1 || Module.Name !== Name) throw new Error('[LUI:Docs] Candidate library mismatch');
    const Source = await ReadText(Path.join(Library, Name + '.luau'));
    const Prefix = 'function ' + Name + '.';
    const Functions = Source.split('\n').filter(Line => Line.startsWith(Prefix));
    const Types = ModuleDeclarations(Source);
    const Signatures = Module.Functions.map(Member => {
      const Signature = Functions.find(Line => Line.startsWith(Prefix + Member + '('));
      if (!Signature) throw new Error('[LUI:Docs] Missing candidate signature ' + Name + '.' + Member);
      return Signature.slice('function '.length).replace(/\s+return .+ end$/, '');
    });
    if (new Set(Signatures).size !== Signatures.length || Signatures.length !== Functions.length ||
        new Set(Module.Types).size !== Module.Types.length || Types.size !== Module.Types.length ||
        Module.Types.some(Type => !Types.has(Type))) throw new Error('[LUI:Docs] Candidate declaration drift');
    const Guide = (await ReadText(Path.join(Library, 'README.md'))).replace(/^# [^\n]+\n/, '');
    const Page = ['---', 'title: ' + Name, 'description: ' + Module.Summary, '---', '',
      '<span hidden data-pagefind-filter="SDK:' + Filter + '"></span>', '',
      (Experimental ? '**Experimental SDK 0.1.0-preview.4-ffi.1.**' : '**Development library for preview.3.**'), Guide, '## Declarations', '',
      '~~~luau', ...Types.values(), ...Signatures, '~~~', ''].join('\n');
    await WriteFile(Path.join(Directory, Name.toLowerCase() + '.md'), Page);
  }
}

export async function GenerateCandidate(Repository, Site) {
  const Catalog = JSON.parse(await ReadFile(Path.join(Repository,'libraries/LuiWorkbench/Library.json'),'utf8'));
  if (Catalog.TargetSdk !== '0.1.0-preview.3') throw new Error('[LUI:Docs] Unknown candidate library version');
  const Source = await ReadFile(Path.join(Repository,'libraries/LuiWorkbench/Commands.luau'),'utf8');
  const Types = ModuleDeclarations(Source);
  const Expected = Catalog.Modules.find(Module=>Module.Name==='Commands')?.Types;
  if (!Expected || Types.size!==Expected.length || Expected.some(Name=>!Types.has(Name))) throw new Error('[LUI:Docs] Candidate module declarations differ');
  let Text = await ReadText(Path.join(Site,'Content/Candidate/Commands.md.in'));
  const Example = await ReadText(Path.join(Repository,'examples/command-shortcuts/Src/Main.luau'));
  Text = Text.replace('{{Example}}',Example.trimEnd());
  Text = Text.replace('{{Declarations}}',[...Types.values()].map(Type=>'\n```luau\n'+Type+'\n```\n').join('\n'));
  if (Text.includes('{{')) throw new Error('[LUI:Docs] Unexpanded candidate guide');
  const Output=Path.join(Site,'src/content/docs/candidate/preview.3');
  await MakeDirectory(Output,{recursive:true});
  await WriteFile(Path.join(Output,'commands.md'),Text);
  const Native = Declarations(await ReadText(Path.join(Repository,'types/LUI.d.luau')));
  const Names = ['HttpService','HttpRequestOptions','HttpCancellationToken','HttpServer','WebSocketService','WebSocket','WebSocketMessage','WebSocketOptions','WebSocketServerOptions'];
  const Records = Names.map(Name=>{
    const Definition=Native.Exports.get(Name);
    if (!Definition) throw new Error('[LUI:Docs] Missing candidate native declaration: '+Name);
    return '\n### '+Name+'\n\n```luau\n'+Definition+'\n```\n';
  });
  const Schema=JSON.parse(await ReadText(Path.join(Repository,'types/schema.json')));
  if (!Schema.services.find(Service=>Service.name==='HttpService')?.methods.some(Method=>Method.name==='CreateCancellationToken') ||
      !Schema.objects.find(Object=>Object.name==='HttpServer')?.methods.some(Method=>Method.name==='RouteWebSocket'))
    throw new Error('[LUI:Docs] Candidate native metadata differs');
  const Echo=await ReadText(Path.join(Repository,'examples/websocket-echo/Src/Main.luau'));
  Text=(await ReadText(Path.join(Site,'Content/Candidate/Networking.md.in'))).replace('{{Example}}',Echo.trimEnd()).replace('{{Declarations}}',Records.join('\n'));
  if(Text.includes('{{'))throw new Error('[LUI:Docs] Unexpanded candidate networking guide');
  await WriteFile(Path.join(Output,'networking.md'),Text);
  await WriteFile(Path.join(Output,'headless.md'),await ReadText(Path.join(Site,'Content/Candidate/Headless.md.in')));
  const FileTypes=ModuleDeclarations(await ReadText(Path.join(Repository,'libraries/LuiWorkbench/Files.luau')));
  const FileCatalog=Catalog.Modules.find(Module=>Module.Name==='Files')?.Types;
  if(!FileCatalog || FileCatalog.length!==FileTypes.size || FileCatalog.some(Name=>!FileTypes.has(Name)))throw new Error('[LUI:Docs] Candidate Files declarations differ');
  const FileService=Native.Exports.get('FileService');
  const FileMethods = Schema.services.find(Service=>Service.name==='FileService')?.methods.map(Method=>Method.name) ?? [];
  const ExpectedFileMethods = ['OpenFile','SaveFile','OpenFolder','Read','Write','List','Open','ReadRange','ByteLength','BeginWriter','WriteAt','CommitWriter','OpenOutput','CreateDirectory','RemoveDirectory','RemoveFile','Rename'];
  if(!FileService || FileMethods.length !== ExpectedFileMethods.length || ExpectedFileMethods.some(Name => !FileMethods.includes(Name)))throw new Error('[LUI:Docs] Candidate FileService metadata differs');
  Text=(await ReadText(Path.join(Site,'Content/Candidate/Files.md.in')))
    .replace('{{Example}}',(await ReadText(Path.join(Repository,'examples/selected-files/Src/Main.luau'))).trimEnd())
    .replace('{{Declarations}}',[...FileTypes.values()].map(Type=>'\n```luau\n'+Type+'\n```\n').join('\n'))
    .replace('{{Native}}','\n```luau\n'+FileService+'\n```\n');
  if(Text.includes('{{'))throw new Error('[LUI:Docs] Unexpanded candidate Files guide');
  await WriteFile(Path.join(Output,'files.md'),Text);
  const ImageProperties = Schema.classes.find(Class=>Class.name==='ImageLabel')?.properties ?? [];
  for (const Name of ['SourceRectOffset','SourceRectSize'])
    if (!ImageProperties.some(Property=>Property.name===Name && Property.type==='Vector2' && !Property.readOnly))
      throw new Error('[LUI:Docs] Candidate image region metadata differs');
  await WriteFile(Path.join(Output,'images.md'),await ReadText(Path.join(Site,'Content/Candidate/Images.md.in')));
  await WriteRuntimeLibraries(Repository, Output);
  await WriteRuntimeLibraries(Repository, Path.join(Site, 'src/content/docs/candidate/ffi'), true);
}
