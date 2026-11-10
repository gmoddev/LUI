import { readFile as ReadFile, writeFile as WriteFile, mkdir as MakeDirectory, readdir as ReadDirectory, rm as Remove } from 'node:fs/promises';
import { fileURLToPath as FileUrlToPath } from 'node:url';
import Path from 'node:path';
import { Hash, Validate, MakeZip } from './Reference.mjs';
import { Summaries, Notes, Examples, GlobalSummaries } from './Editorial.mjs';
import { Checkpoints, ProjectFiles } from './Tutorials.mjs';

const Root = Path.resolve(FileUrlToPath(new URL('..', import.meta.url)));
const Repository = Path.dirname(Root);
const Release = JSON.parse(await ReadFile(Path.join(Root, 'Release.json'), 'utf8'));
const SchemaText = (await ReadFile(Path.join(Repository, 'types/schema.json'), 'utf8')).replaceAll('\r\n', '\n');
const Definitions = (await ReadFile(Path.join(Repository, 'types/LUI.d.luau'), 'utf8')).replaceAll('\r\n', '\n');
if (Hash(SchemaText) !== Release.SchemaSha256) throw new Error('[LUI:Docs] Reflection differs from the documented SDK');
const Schema = JSON.parse(SchemaText);
const Parsed = Validate(Schema, Definitions, Release, Summaries);
for (const Name of Object.keys(Notes)) if (!Summaries[Name]) throw new Error('[LUI:Docs] Unknown note symbol: ' + Name);
for (const Name of Object.keys(Examples)) if (!Summaries[Name]) throw new Error('[LUI:Docs] Unknown example symbol: ' + Name);
for (const Name of Parsed.Globals.keys()) if (!GlobalSummaries[Name]) throw new Error('[LUI:Docs] Missing global explanation: ' + Name);
for (const Name of Object.keys(GlobalSummaries)) if (!Parsed.Globals.has(Name)) throw new Error('[LUI:Docs] Unknown global explanation: ' + Name);

const Version = 'docs/sdk-' + Release.SdkVersion;
const Output = Path.join(Root, 'src/content/docs', Version);
const Downloads = Path.join(Root, 'public/downloads', Release.SdkVersion);
const Projects = Path.join(Repository, 'build/DocsTutorials');
// These directories contain only generated output, never authored content.
for (const Directory of [Output, Downloads, Projects]) {
  const Absolute = Path.resolve(Directory);
  if (![Path.join(Root, 'src/content/docs/docs'), Path.join(Root, 'public/downloads'), Path.join(Repository, 'build')].some(Parent => Absolute.startsWith(Parent + Path.sep))) throw new Error('[LUI:Docs] Invalid generated output root');
  await Remove(Absolute, { recursive: true, force: true });
  await MakeDirectory(Absolute, { recursive: true });
}
const SiteBase = '/LUI/' + Version;
const SourceBase = 'https://github.com/gmoddev/LUI/blob/' + Release.PublicCommit;
const ReferencePaths = new Map();
for (const Group of ['classes', 'services', 'objects']) for (const Item of Schema[Group]) ReferencePaths.set(Item.name, 'api/' + (Group === 'objects' ? 'types' : Group) + '/' + Item.name.toLowerCase() + '/');
for (const Name of Parsed.Exports.keys()) if (!ReferencePaths.has(Name)) ReferencePaths.set(Name, 'api/types/' + Name.toLowerCase() + '/');
const Banner = `<div class="LuiRelease"><span>SDK ${Release.SdkVersion} · Experimental</span><a href="https://github.com/gmoddev/LUI/releases/tag/${Release.Tag}">Download &amp; release notes ↗</a></div>\n\n`;
const Fence = (Text, Language = 'luau') => '\n```' + Language + '\n' + Text.trimEnd() + '\n```\n';
const Inline = Text => '`' + String(Text).replaceAll('|', '\\|').replaceAll('`', '') + '`';
const Ref = Name => '[' + Name + '](' + SiteBase + '/' + ReferencePaths.get(Name) + ')';
const Pages = [];
async function Page(Relative, Text, BannerEnabled = true) {
  const Destination = Path.join(Output, Relative);
  await MakeDirectory(Path.dirname(Destination), { recursive: true });
  if (BannerEnabled) Text = Text.replace(/^(---\n[\s\S]*?\n---\n)/, '$1\n' + Banner);
  await WriteFile(Destination, Text, 'utf8'); Pages.push(Relative);
}
function Header(Title, Description) { return '---\ntitle: ' + JSON.stringify(Title) + '\ndescription: ' + JSON.stringify(Description) + '\n---\n\n'; }
function MemberSections(Item) {
  let Text = '';
  if (Item.properties.length) {
    Text += '## Properties\n\n| Property | Type | Default | Access |\n| --- | --- | --- | --- |\n';
    for (const Property of Item.properties) Text += '| [' + Property.name + '](#' + Property.name.toLowerCase() + ') | ' + Inline(Property.type) + ' | ' + (Property.default === '' ? 'Empty string' : Property.default !== undefined ? Inline(Property.default) : 'Not specified in reflection') + ' | ' + (Property.readOnly === true ? 'Read only' : Property.readOnly === false ? 'Read/write' : 'See declaration') + ' |\n';
    for (const Property of Item.properties) Text += '\n### ' + Property.name + '\n' + Fence(Property.name + ': ' + Property.type) + '\n' + (Property.readOnly === true ? 'This property is read only.\n' : Property.readOnly === false ? 'This property can be read and assigned.\n' : 'Use the full declaration below for its read/write contract.\n');
  }
  if (Item.methods.length) {
    Text += '\n## Methods\n\nSignatures show named parameters and return types. `Self` is the receiver supplied by a colon call, not an extra argument to pass.\n';
    for (const Method of Item.methods) Text += '\n### ' + Method.name + '\n' + Fence(Method.name + ': ' + Method.type);
  }
  if (Item.signals.length) {
    Text += '\n## Signals\n\nConnect a callback to a signal with `:Connect(...)`. Keep the returned connection if you need to disconnect it explicitly.\n';
    for (const Signal of Item.signals) Text += '\n### ' + Signal.name + '\n' + Fence(Signal.name + ': ' + Signal.type) + '\n' + (ReferencePaths.has(Signal.type) ? 'Callback payload: ' + Ref(Signal.type) + '.\n' : 'See the signal declaration and class notes for its payload.\n');
  }
  return Text;
}
for (const Group of ['classes', 'services', 'objects']) for (const Item of Schema[Group]) {
  const Name = Item.name; let Text = Header(Name, Summaries[Name]) + Summaries[Name] + '\n\n';
  if (Group === 'classes') {
    Text += '**Inheritance:** ' + (Item.base ? Ref(Item.base) : 'None') + '. **Creatable:** ' + (Item.creatable ? 'Yes' : 'No — use a concrete subclass') + '.\n\n';
    Text += '**Children:** ' + (Item.acceptsChildren ? 'Accepts child content.' : 'Does not accept visual child content.') + ' Parent rule: ' + Inline(Item.parentRule) + '.\n\n';
  }
  if (Notes[Name]) Text += '## Behavior and limits\n\n' + Notes[Name].map(Note => '- ' + Note).join('\n') + '\n\n';
  if (Examples[Name]) Text += '## Example\n\n' + (Name === 'Window' ? 'This is a complete minimal window.\n' : 'This fragment assumes an existing `Window`' + (Name === 'UIListLayout' ? ' and a container named `Content`' : '') + '. For a complete runnable project, follow [your first project](' + SiteBase + '/learn/first-project/).\n') + Fence(Examples[Name]);
  else if (Group === 'services') Text += '## Get the service\n\n' + Fence('local Service = app:GetService("' + Name + '")') + '\nThe service must be available in the host and its capabilities granted. Merely looking it up does not install a provider or add permissions.\n';
  Text += MemberSections(Item);
  if (Item.base) {
    Text += '\n## Inherited members\n\n'; let Base = Item.base;
    while (Base) {
      const Parent = Schema.classes.find(Value => Value.name === Base);
      const Members = [...Parent.properties, ...Parent.methods, ...Parent.signals];
      Text += '### From ' + Base + '\n\n' + (Members.length ? Members.map(Member => '[' + Member.name + '](' + SiteBase + '/' + ReferencePaths.get(Base) + '#' + Member.name.toLowerCase() + ')').join(' · ') : 'No own members.') + '\n\n'; Base = Parent.base;
    }
  }
  Text += '\n## Full type declaration\n' + Fence(Parsed.Exports.get(Name));
  const Related = [...new Set([...Item.properties, ...Item.methods, ...Item.signals].flatMap(Member => Member.type.match(/[A-Za-z][A-Za-z0-9_]*/g) ?? []))].filter(Token => Token !== Name && ReferencePaths.has(Token));
  if (Related.length) Text += '\n## Related types\n\n' + Related.map(Ref).join(' · ') + '\n';
  Text += '\n## Availability and source\n\nA shared API entry is not a promise of support on every host. Read [platform support](' + SiteBase + '/guides/platforms/) and the [released application contract](' + SourceBase + '/docs/SPEC.md) for capability, editing, networking and lifetime limits.\n\n[Reflection source](' + SourceBase + '/types/schema.json) · [Generated declarations](' + SourceBase + '/types/LUI.d.luau).\n';
  await Page(ReferencePaths.get(Name).replace(/\/$/, '.md'), Text);
}
for (const [Name, Declaration] of Parsed.Exports) if (![...Schema.classes, ...Schema.services, ...Schema.objects].some(Item => Item.name === Name)) {
  const Description = Name.endsWith('Init') ? 'Optional initializer fields for ' + Name.slice(0,-4) + '. Use these keys in the Instance.new properties table.' : 'The public Luau declaration of ' + Name + ', used by LUI callbacks, values and helpers.';
  let Text = Header(Name, Description) + Description + '\n\n## Declaration\n' + Fence(Declaration);
  if (Name.endsWith('Init') && ReferencePaths.has(Name.slice(0,-4))) Text += '\nSee ' + Ref(Name.slice(0,-4)) + ' for defaults, inheritance and behavior.\n';
  if (Name.endsWith('Signal') || Name === 'Connection') Text += '\nUse `:Connect(Callback)` to subscribe and `Connection:Disconnect()` to retire an explicit connection. Callbacks execute through the authoritative scheduler.\n';
  Text += '\n[Generated declaration source](' + SourceBase + '/types/LUI.d.luau).\n';
  await Page(ReferencePaths.get(Name).replace(/\/$/, '.md'), Text);
}
for (const [Name, Declaration] of Parsed.Globals) {
  let Text = Header(Name, GlobalSummaries[Name]) + GlobalSummaries[Name] + '\n\n## Signature\n' + Fence(Declaration);
  if (ReferencePaths.has(Name)) Text += '\n## Related type\n\n' + Ref(Name) + '\n';
  if (Name === 'require') Text += '\nSee [modules and project structure](' + SiteBase + '/guides/modules/) for declared paths and initialization constraints.\n';
  Text += '\n[Declaration source](' + SourceBase + '/types/LUI.d.luau).\n';
  await Page('api/globals/' + Name.toLowerCase() + '.md', Text);
}
let Index = Header('API reference', 'Find implemented LUI classes, services, value types, globals and exact signatures.') + 'Find the exact names, signatures, defaults and inherited members available in **SDK ' + Release.SdkVersion + '**. Start with [your first project](' + SiteBase + '/learn/first-project/) if you are learning LUI.\n\nUse the search box to find a class or member, including names such as `Activated`, `CanvasPosition` or `RequestClose`.\n';
for (const [Group, Label] of [['classes','Classes'], ['services','Services'], ['objects','Value and resource types']]) Index += '\n## ' + Label + '\n\n' + Schema[Group].map(Item => '- ' + Ref(Item.name) + ' — ' + Summaries[Item.name]).join('\n') + '\n';
Index += '\n## Globals and constructors\n\n' + [...Parsed.Globals.keys()].map(Name => '- [' + Name + '](' + SiteBase + '/api/globals/' + Name.toLowerCase() + '/) — ' + GlobalSummaries[Name]).join('\n') + '\n\n## Helper and initializer types\n\n' + [...Parsed.Exports.keys()].filter(Name=>![...Schema.classes,...Schema.services,...Schema.objects].some(Item=>Item.name===Name)).map(Name=>'- '+Ref(Name)).join('\n') + '\n\n## Version and availability\n\nThe reference uses reflection and generated definitions from the public SDK source [' + Release.PublicCommit.slice(0,7) + '](' + SourceBase.replace('/blob/','/tree/') + '). Written notes explain behavior but do not replace those signatures. Types, defaults and inherited links are checked during the site build.\n\nThe API is experimental. Platform and capability limits remain explicit; see [platform support](' + SiteBase + '/guides/platforms/).\n';
await Page('api/index.md', Index);

const Tutorials = Checkpoints(); const ProjectReport = [];
for (const Checkpoint of Tutorials) {
  const Files = ProjectFiles(Checkpoint, Release); const Archive = MakeZip(Files);
  const Directory = Path.join(Projects, Checkpoint.Name); await MakeDirectory(Directory, { recursive: true });
  for (const [Name, Text] of Files) { const FilePath = Path.join(Directory, Name); await MakeDirectory(Path.dirname(FilePath), { recursive: true }); await WriteFile(FilePath, Text); }
  await WriteFile(Path.join(Downloads, Checkpoint.Name + '.zip'), Archive);
  ProjectReport.push({ Name: Checkpoint.Name, ArchiveSha256: Hash(Archive), Files: [...Files.keys()] });
}
function Expand(Text) {
  return Text.replace(/\{\{(Code|Manifest|Download):([^}:]+)(?::([^}]+))?\}\}/g, (_, Kind, Name, File) => {
    const Checkpoint = Tutorials.find(Item => Item.Name === Name); if (!Checkpoint) throw new Error('[LUI:Docs] Unknown tutorial checkpoint');
    if (Kind === 'Download') return '/LUI/downloads/' + Release.SdkVersion + '/' + Name + '.zip';
    if (Kind === 'Manifest') return Fence(ProjectFiles(Checkpoint, Release).get('lui.json'), 'json');
    if (!Checkpoint.Files[File]) throw new Error('[LUI:Docs] Unknown tutorial file');
    return Fence(Checkpoint.Files[File]);
  });
}
const Authored = { 'FirstProject.md.in':'learn/first-project.md', 'AdvancedProject.md.in':'learn/advanced-project.md', 'Setup.md.in':'guides/setup.md', 'Workflow.md.in':'guides/workflow.md', 'Modules.md.in':'guides/modules.md', 'Layout.md.in':'guides/layout.md', 'Packaging.md.in':'guides/packaging.md', 'Platforms.md.in':'guides/platforms.md' };
for (const [Source, Destination] of Object.entries(Authored)) {
  const Text = Expand(await ReadFile(Path.join(Root, 'Content', Source), 'utf8'));
  if (/\{\{/.test(Text)) throw new Error('[LUI:Docs] Unexpanded tutorial placeholder');
  await Page(Destination, Text);
}
const HeroSource = Tutorials.find(Item => Item.Name === 'First-03-Event').Files['Main.luau'];
const HeroHtml = '<div class="LuiHeroCode"><div>Src/Main.luau <span>Luau</span></div><pre><code>' + HeroSource.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;') + '</code></pre></div>';
const Home = Expand(await ReadFile(Path.join(Root, 'Content/Index.mdx.in'), 'utf8')).replace('{{HeroCode}}', JSON.stringify(HeroHtml));
if (/\{\{/.test(Home)) throw new Error('[LUI:Docs] Unexpanded home content');
await WriteFile(Path.join(Root, 'src/content/docs/index.mdx'), Home);
await Remove(Path.join(Root, 'src/content/docs/404.md'), { force: true });
let Notices = 'LUI documentation website — open-source notices\n\n' + await ReadFile(Path.join(Repository,'LICENSE'),'utf8') + '\n\n';
Notices += 'The following inventory includes browser and build dependencies. License and notice files supplied by each installed package are reproduced below.\n\n';
const PackageRoots = [];
for (const Entry of await ReadDirectory(Path.join(Root,'node_modules'),{withFileTypes:true})) {
  if (!Entry.isDirectory() || Entry.name.startsWith('.')) continue;
  const Directory=Path.join(Root,'node_modules',Entry.name);
  if(Entry.name.startsWith('@')) { for(const Child of await ReadDirectory(Directory,{withFileTypes:true})) if(Child.isDirectory()) PackageRoots.push(Path.join(Directory,Child.name)); }
  else PackageRoots.push(Directory);
}
for(const Directory of PackageRoots.sort()) {
  let Package; try { Package=JSON.parse(await ReadFile(Path.join(Directory,'package.json'),'utf8')); } catch { continue; }
  Notices += '\n---\n' + Package.name + ' ' + Package.version + '\nLicense: ' + (typeof Package.license==='string'?Package.license:JSON.stringify(Package.license??'See package notices')) + '\n';
  for(const Entry of await ReadDirectory(Directory,{withFileTypes:true})) if(Entry.isFile() && /^(?:licen[cs]e|copying|notice)(?:[.-].*)?$/i.test(Entry.name)) {
    Notices += '\n'+Entry.name+'\n'+await ReadFile(Path.join(Directory,Entry.name),'utf8')+'\n';
  }
}
await WriteFile(Path.join(Root,'public/ThirdPartyNotices.txt'),Notices);
await WriteFile(Path.join(Root, 'BuildReport.json'), JSON.stringify({ SdkVersion: Release.SdkVersion, PublicCommit: Release.PublicCommit, SchemaSha256: Hash(SchemaText), DefinitionsSha256: Hash(Definitions), Classes: Schema.classes.length, Services: Schema.services.length, Objects: Schema.objects.length, PublicTypes: Parsed.Exports.size, Globals: Parsed.Globals.size, Pages: Pages.length + 2, TutorialCheckpoints: ProjectReport }, null, 2) + '\n');
console.log('[LUI:Docs] Generated ' + (Pages.length+2) + ' pages and ' + Tutorials.length + ' complete tutorial projects.');
