import { readFile as ReadFile, writeFile as WriteFile, mkdir as MakeDirectory, readdir as ReadDirectory } from 'node:fs/promises';
import Path from 'node:path';
import { Validate, Hash } from './Reference.mjs';
import { ProjectFiles } from './Tutorials.mjs';
import { Recipes } from './Recipes.mjs';
import { Summaries, Members, Advanced, BaseClasses, UsageNotes } from './DocumentationContent.mjs';
import { Examples, Globals, TypeExample } from './DocumentationExamples.mjs';

const Fence = (Text, Language = 'luau') => '\n```' + Language + '\n' + Text.trimEnd() + '\n```\n';
const Inline = Value => '`' + String(Value).replaceAll('|','\\|').replaceAll('`','') + '`';
export function ValidateDocumentation(Schema, Parsed, Content = Members, Samples = Examples) {
  const Items = [...Schema.classes, ...Schema.services, ...Schema.objects];
  const Names = new Set(Items.map(Item => Item.name));
  for (const Name of Object.keys(Content)) if (!Names.has(Name)) throw new Error('[LUI:Docs] Unknown member documentation: ' + Name);
  for (const Name of Object.keys(Samples)) if (!Names.has(Name)) throw new Error('[LUI:Docs] Unknown reference example: ' + Name);
  for (const Item of Items) {
    const Expected = [...Item.properties, ...Item.methods, ...Item.signals].map(Member => Member.name).sort();
    if (!Content[Item.name] || JSON.stringify(Object.keys(Content[Item.name]).sort()) !== JSON.stringify(Expected))
      throw new Error('[LUI:Docs] Incomplete member documentation: ' + Item.name);
    if (Object.values(Content[Item.name]).some(Text => typeof Text !== 'string' || !Text.trim()))
      throw new Error('[LUI:Docs] Missing member explanation: ' + Item.name);
    if (!Samples[Item.name]?.Code || !Samples[Item.name]?.Outcome) throw new Error('[LUI:Docs] Missing usage example: ' + Item.name);
  }
  for (const Name of Parsed.Exports.keys()) if (!TypeExample(Name)?.Code) throw new Error('[LUI:Docs] Missing type example: ' + Name);
  for (const Name of Parsed.Globals.keys()) if (!Globals[Name]?.Code) throw new Error('[LUI:Docs] Missing global example: ' + Name);
  for (const Name of Object.keys(Globals)) if (!Parsed.Globals.has(Name)) throw new Error('[LUI:Docs] Unknown global example: ' + Name);
  return { Symbols: Items.length, Members: Items.reduce((Count, Item) => Count + Item.properties.length + Item.methods.length + Item.signals.length, 0), Types: Parsed.Exports.size, Globals: Parsed.Globals.size };
}

export function ServiceSetup(Name, Version) {
  const Setup = {
    HttpService: '`network.client`; the native SDK supplies the HTTP transport. Your host’s address/port policy still applies.',
    HttpServerService: '`network.server`; the native SDK supplies the server transport. Bind to loopback for local-only examples.',
    NetworkService: '`network.raw` plus `network.client` for outgoing connections/sends and `network.server` for listening/receiving. Use HTTP services instead if that is your protocol.',
    ClipboardService: '`Clipboard` and a supported desktop clipboard. The supplied native runner installs its supported adapter.',
    DialogService: '`Dialogs` and a supported file picker. Picking a path does not grant unrestricted file-reading permission.',
    StorageService: Version === '0.1.0-preview.2' ? '`Storage` and `"Storage": {"ApplicationId": "com.example.notes"}` in lui.json. The supplied preview.2 desktop runner provisions app-private storage. A copied/packaged project has a separate scope.' : '`Storage` and a custom embedding host that explicitly installs app-private storage. The standard preview.1 project runner does not support Storage.ApplicationId. Do not copy a preview.2 storage manifest into preview.1.',
    NotificationService: '`Notifications` and the supplied Windows shell adapter. GTK reports unsupported.',
    TrayService: '`Tray` and the supplied Windows shell adapter. GTK reports unsupported.',
  };
  return Setup[Name] || 'No additional capability token. This service is available through the runtime; individual feature results still depend on the selected host.';
}

export async function ImproveReference(Repository, Site, Entry) {
  const Release = Entry.Metadata;
  const Version = Release.SdkVersion;
  const Base = '/LUI/docs/sdk-' + Version;
  const Output = Path.join(Site, 'src/content/docs/docs/sdk-' + Version);
  const Source = 'https://github.com/gmoddev/LUI/blob/' + Release.PublicCommit;
  const SchemaText = await ReadFile(Path.join(Repository,'types/schema.json'),'utf8');
  const Definitions = await ReadFile(Path.join(Repository,'types/LUI.d.luau'),'utf8');
  if (Hash(SchemaText) !== Release.SchemaSha256) throw new Error('[LUI:Docs] Documentation schema differs from SDK');
  const Schema = JSON.parse(SchemaText);
  const Parsed = Validate(Schema, Definitions, Release, Summaries);
  const Coverage = ValidateDocumentation(Schema, Parsed);
  const Paths = new Map();
  for (const [Group, Slug] of [['classes','classes'],['services','services'],['objects','types']])
    for (const Item of Schema[Group]) Paths.set(Item.name, 'api/' + Slug + '/' + Item.name.toLowerCase() + '/');
  for (const Name of Parsed.Exports.keys()) if (!Paths.has(Name)) Paths.set(Name, 'api/types/' + Name.toLowerCase() + '/');
  const Ref = Name => '[' + Name + '](' + Base + '/' + Paths.get(Name) + ')';
  const Banner = '<div class="LuiRelease"><span>SDK ' + Version + ' · Experimental</span><a href="https://github.com/gmoddev/LUI/releases/tag/' + Release.Tag + '">Download &amp; release notes ↗</a></div>\n\n';
  const Header = (Name, Description) => '---\ntitle: ' + JSON.stringify(Name) + '\ndescription: ' + JSON.stringify(Description) + '\n---\n\n' + Banner;
  const ProjectRoot = Path.join(Repository, 'build/DocsReference', Version);
  const ProjectNames = new Map();
  const SampleFiles = [];
  async function Sample(Name, Sample) {
    const Key = Name.replace(/[^A-Za-z0-9_-]/g,'');
    const Project = {Name: 'Reference-' + Key, Title: Name + ' reference example', Files: {'Main.luau': Sample.Code, ...Sample.Files}, Capabilities: Sample.Capabilities || []};
    if (Sample.Assets) Project.Assets = Object.fromEntries(Sample.Assets.map(Name => {
      const Bytes = Recipes().find(Item => Item.Name === 'Recipe-Images')?.Assets?.[Name];
      if (!Bytes) throw new Error('[LUI:Docs] Missing example asset: ' + Name);
      return [Name,Bytes];
    }));
    const Files = ProjectFiles(Project, Release);
    const Manifest = JSON.parse(Files.get('lui.json'));
    if (Sample.Network) Manifest.NetworkPolicy = {ClientLoopbackOnly:true,ServerLoopbackOnly:true,ClientPortMin:0,ClientPortMax:0,ServerPortMin:0,ServerPortMax:0};
    if (Sample.Storage && Version === '0.1.0-preview.2') Manifest.Storage = {ApplicationId:'com.example.docs'};
    if (Sample.Assets) Manifest.Assets = Sample.Assets;
    Files.set('lui.json',JSON.stringify(Manifest,null,2)+'\n');
    const Directory = Path.join(ProjectRoot,Key);
    await MakeDirectory(Directory,{recursive:true});
    for (const [File, Data] of Files) { await MakeDirectory(Path.dirname(Path.join(Directory,File)),{recursive:true}); await WriteFile(Path.join(Directory,File),Data); }
    ProjectNames.set(Name, Key);SampleFiles.push({Name, Directory:Key, Capabilities:Project.Capabilities, HasAssets:!!Sample.Assets, NeedsCustomHost:Sample.Storage && Version !== '0.1.0-preview.2'});
    let Text = '\n## Example\n\n' + (Sample.Limits ? Sample.Limits + '\n\n' : '') + '**Where to put it:** replace `Src/Main.luau` in a project created with this SDK. This is the complete entry file.\n\n';
    if (Sample.Capabilities?.length) Text += '**Project setup:** add ' + Sample.Capabilities.map(Inline).join(', ') + ' to `Capabilities` in `lui.json`. ' + (Sample.Storage ? Version === '0.1.0-preview.2' ? 'Also add `"Storage": {"ApplicationId": "com.example.docs"}`. ' : 'This example requires a custom host that installs storage; the ordinary preview.1 runner cannot provision it. ' : '') + 'See the [capability guide](' + Base + '/guides/capabilities/).\n\n';
    if (Sample.Assets) Text += '**Asset setup:** declare ' + Sample.Assets.map(Inline).join(', ') + ' in `Assets` and include the files. The [complete image recipe](' + Base + '/guides/recipes/#a-bundled-image-or-icon) supplies the asset.\n\n';
    Text += Fence(Sample.Code);
    if (Sample.Files) for (const [File, Code] of Object.entries(Sample.Files)) Text += '\nAlso create `Src/' + File + '` and add it to `Modules.Files`:\n' + Fence(Code);
    Text += '\n**Expected result:** ' + Sample.Outcome + '\n';
    if (Sample.Recipe) Text += '\nFor source, manifest, pin and assets together, use the [complete practical recipe](' + Base + '/guides/recipes/).\n';
    return Text;
  }
  async function Page(Relative, Text) {
    const File = Path.join(Output, Relative);await MakeDirectory(Path.dirname(File),{recursive:true});await WriteFile(File,Text);
  }
  const TypeDetails = Declaration => '\n<details>\n<summary>Full type declaration — for type checking and deeper reference</summary>\n\nThe declaration is generated from the SDK. `Self` is supplied by a colon call; `read` means the field cannot be assigned. See [how to read this reference](' + Base + '/guides/reading-the-api/).\n' + Fence(Declaration) + '\n</details>\n';
  for (const Group of ['classes','services','objects']) for (const Item of Schema[Group]) {
    const Name = Item.name;
    let Text = Header(Name,Summaries[Name]) + Summaries[Name] + '\n\n';
    Text += Advanced.has(Name) ? '**Advanced app/host integration API.** Useful for lower-level work; ordinary app authors can start with the [app-building path](' + Base + '/guides/reading-the-api/#choose-the-level-you-need). This is public API, not permission to access private toolkit objects.\n\n' : BaseClasses.has(Name) ? '**Shared base class.** You normally create a concrete control such as TextButton or Frame, then use the inherited members documented here.\n\n' : '';
    if (Group === 'classes') {
      Text += '**Create:** ' + (Item.creatable ? '`Instance.new("'+Name+'", {...})`.' : 'This base class cannot be created directly.') + (Item.base ? ' **Inherits:** '+Ref(Item.base)+'.' : '') + '\n\n';
      Text += '**Parent and children:** ' + (Name === 'Window' ? 'Top-level window; do not give it a Parent.' : Item.parentRule === 'visual' ? 'Parent this helper to the visual object whose size it constrains.' : 'Use Window, Frame or ScrollingFrame as the Parent.') + ' ' + (!Item.creatable ? 'Child support depends on the concrete subclass.' : Item.acceptsChildren ? 'It can own child content.' : 'It does not accept visual child content.') + '\n\n';
    }
    if (Group === 'services') Text += '## Required setup\n\n' + ServiceSetup(Name,Version) + '\n\nThe **host** is the program running your Luau code: normally the SDK’s native runner. Its **provider/adapter** implements an optional service. You do not need to write one to use a service already supplied by the SDK. [Capabilities](' + Base + '/guides/capabilities/) explains the manifest and remaining platform limits.\n\n';
    Text += await Sample(Name,Examples[Name]);
    if (UsageNotes[Name]) Text += '\n## Behavior and limits\n\n' + UsageNotes[Name].map(Value=>'- '+Value).join('\n')+'\n';
    for (const [Kind, Label] of [['properties','Properties'],['methods','Methods'],['signals','Signals']]) if (Item[Kind].length) {
      Text += '\n## '+Label+'\n';
      for (const Member of Item[Kind]) {
        Text += '\n### '+Member.name+'\n\n'+Members[Name][Member.name]+'\n';
        if (Kind === 'methods') Text += Fence(Member.name + Member.type.replace(/^\(Self: \w+(?:, )?/,'('));
        else {
          const Field=new RegExp('(\\bread\\s+)?\\b'+Member.name+'\\s*:').exec(Parsed.Exports.get(Name));
          const ReadOnly=Member.readOnly ?? Boolean(Field?.[1]);
          Text += '\n**Type:** '+Inline(Member.type)+'. ' + (Kind === 'properties' ? (Member.default !== undefined ? '**Default:** '+(Member.default === '' ? 'empty string' : Inline(Member.default))+'. ' : '') + (ReadOnly ? '**Read-only.**\n' : '**Read/write field.**\n') : 'Subscribe with `:Connect(function(...) ... end)`.\n');
        }
      }
    }
    if (Item.base) {
      Text += '\n## Inherited members\n\nYou can use these directly on '+Name+'. Follow a member link for its explanation and example.\n';let BaseName=Item.base;
      while (BaseName) {const Parent=Schema.classes.find(Value=>Value.name===BaseName);const All=[...Parent.properties,...Parent.methods,...Parent.signals];Text+='\n### From '+BaseName+'\n\n'+(All.length?All.map(Member=>'['+Member.name+']('+Base+'/'+Paths.get(BaseName)+'#'+Member.name.toLowerCase()+')').join(' · '):'No additional members; continue to the next base.')+'\n';BaseName=Parent.base;}
    }
    Text += TypeDetails(Parsed.Exports.get(Name));
    Text += '\n## Availability and deeper reading\n\nSee [platform limits]('+Base+'/guides/platforms/) before relying on host-specific behavior. The [framework design specification]('+Source+'/docs/SPEC.md) and [reflection/type sources]('+Source+'/types/schema.json) are deeper engineering references, not extra setup required to build an app.\n';
    await Page(Paths.get(Name).replace(/\/$/,'.md'),Text);
  }
  const Reflected=new Set([...Schema.classes,...Schema.services,...Schema.objects].map(Item=>Item.name));
  for (const [Name,Declaration] of Parsed.Exports) if (!Reflected.has(Name)) {
    const Initializer=Name.endsWith('Init');const Owner=Initializer?Name.slice(0,-4):null;
    const Description=Initializer?'A type describing the optional starting properties for '+Owner+'. It is a properties table, not a class to create.':Name==='App'?'The type of the existing app global. You do not construct an App object.':Name.endsWith('Signal')||Name==='Signal'?'A subscription interface for an event. Receive it from an Instance or service rather than constructing it.':Name==='Connection'?'The connection returned by Signal:Connect. Disconnect it when you no longer need the callback.':Name==='InputEvent'?'The keyboard-or-pointer record passed to input callbacks. Check Device before accessing device-specific fields.':Name==='KeyboardShortcut'?'The shortcut record passed by Window.Shortcut, including modifier and text-editing flags.':Name==='PointerInput'?'Pointer input data supplied by the runtime, with local logical position and cancellation state.':Name==='KeyboardInput'?'Keyboard input data supplied by the runtime, with the key and repeat state.':'A small immutable layout value. Use its constructor; it is not an Instance.';
    let Text=Header(Name,Description)+Description+'\n\n';
    if (Initializer) Text+='Use these fields in `Instance.new("'+Owner+'", {...})`. Omit a field to use its class default. See '+Ref(Owner)+' for supported fields, inherited behavior and a complete example. You never call `Instance.new("'+Name+'")`.\n';
    Text+=await Sample('Type-'+Name,TypeExample(Name));
    Text+=TypeDetails(Declaration)+'\n[SDK declaration source]('+Source+'/types/LUI.d.luau). This page explains a public type, not a separate feature you must initialize.\n';
    await Page(Paths.get(Name).replace(/\/$/,'.md'),Text);
  }
  for (const [Name,Declaration] of Parsed.Globals) {
    const Description=Name==='require'?'Loads a declared Luau source module. It does not download packages.':Name==='task'?'Schedules callbacks in LUI. Use a task for I/O methods that yield.':Name==='app'?'The application object already provided to your script. Use it to get services.':'Constructor helpers for '+Name+'. Values and classes are explained separately in the related type.';
    let Text=Header(Name,Description)+Description+'\n';Text+=await Sample('Global-'+Name,Globals[Name]);
    Text+=TypeDetails(Declaration);
    if (Paths.has(Name)) Text+='\nRelated type: '+Ref(Name)+'.\n';
    if (Name==='require') Text+='\nSee [modules and project structure]('+Base+'/guides/modules/) for the complete manifest and import rules.\n';
    await Page('api/globals/'+Name.toLowerCase()+'.md',Text);
  }
  let Index=Header('API reference','Choose a control, service or value type. Examples explain how to use each SDK API.')+'Use this reference to answer a specific question while building an app. Every API below has an example and member explanations; you do not need to read the entire list first.\n\nNew to the notation? Read [how to read the API]('+Base+'/guides/reading-the-api/). New to LUI? Start with [your first app]('+Base+'/learn/first-project/).\n\n## Find an API by task\n\n| I want to… | Start here |\n| --- | --- |\n| Open a window and respond to a button | '+Ref('Window')+' · '+Ref('TextButton')+' |\n| Read user input | '+Ref('TextBox')+' · '+Ref('CheckBox')+' · '+Ref('Slider')+' |\n| Arrange or scroll content | '+Ref('UIListLayout')+' · '+Ref('UIGridLayout')+' · '+Ref('ScrollingFrame')+' |\n| Show a declared image | '+Ref('ImageLabel')+' |\n| Call an HTTP endpoint | '+Ref('HttpService')+' |\n| Save ordinary app preferences | '+(Version==='0.1.0-preview.2'?'[Preferences]('+Base+'/libraries/luiworkbench/preferences/)':'Advanced '+Ref('StorageService')+' with a custom host')+' |\n| Understand a type/signature | [Reading the API]('+Base+'/guides/reading-the-api/) |\n';
  for (const [Group,Label] of [['classes','Classes'],['services','Services'],['objects','Value and resource types']]) {
    Index+='\n## '+Label+'\n\n'+Schema[Group].map(Item=>'- '+Ref(Item.name)+(Advanced.has(Item.name)?' **Advanced.**':'')+' '+Summaries[Item.name]).join('\n')+'\n';
  }
  Index+='\n## Globals and constructors\n\n'+[...Parsed.Globals.keys()].map(Name=>'- ['+Name+']('+Base+'/api/globals/'+Name.toLowerCase()+'/) — '+(Name==='task'?'schedule callbacks':Name==='app'?'get services':Name==='require'?'load declared modules':'create '+Name+' values/objects')).join('\n')+'\n\n## Helper and initializer types\n\nThese types describe existing values, event payloads or creation tables. They are not extra services or classes to initialize.\n\n'+[...Parsed.Exports.keys()].filter(Name=>!Reflected.has(Name)).map(Ref).join(' · ')+'\n\n## Version and availability\n\nThis reference describes SDK '+Version+'. The original SDK/schema/type identities and tutorial pins are unchanged. Explanations and examples can be corrected without upgrading your project. Search defaults to the SDK version of the page you are reading; choose another version in Search when needed.\n';
  await Page('api/index.md',Index);
  await WriteFile(Path.join(ProjectRoot,'Examples.json'),JSON.stringify(SampleFiles,null,2)+'\n');
  return {...Coverage,Version,ReferenceExamples:SampleFiles.length,Examples:SampleFiles};
}

export async function TagSearchPages(Directory, Version, Exclude = false) {
  for (const Entry of await ReadDirectory(Directory,{withFileTypes:true})) {
    const File=Path.join(Directory,Entry.name);
    if (Entry.isDirectory()) await TagSearchPages(File,Version,Exclude);
    else if (/\.mdx?$/.test(Entry.name)) {
      let Text=await ReadFile(File,'utf8');
      if (Exclude) Text=Text.replace(/^---\n/,'---\npagefind: false\n');
      else Text=Text.replace(/^(---\n[\s\S]*?\n---\n)/,'$1\n<span hidden data-pagefind-filter="SDK:'+Version+'"></span>\n');
      await WriteFile(File,Text);
    }
  }
}
