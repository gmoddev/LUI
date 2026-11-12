import Test from 'node:test';
import Assert from 'node:assert/strict';
import { readFileSync as ReadFile } from 'node:fs';
import { Hash, Declarations, Validate, MakeZip } from '../scripts/Reference.mjs';
import { Summaries } from '../scripts/Editorial.mjs';
import { Checkpoints, ProjectFiles } from '../scripts/Tutorials.mjs';
import { Recipes } from '../scripts/Recipes.mjs';
import { LoadReleases } from '../scripts/Releases.mjs';
import { ModuleDeclarations, GenerateLibraryReference } from '../scripts/LibraryReference.mjs';
import { mkdtemp as MakeTemporary, mkdir as MakeDirectory, writeFile as WriteFile, rm as Remove, copyFile as CopyFile } from 'node:fs/promises';
import { existsSync as Exists } from 'node:fs';
import { tmpdir as TemporaryDirectory } from 'node:os';
import { fileURLToPath as FilePath } from 'node:url';
import Path from 'node:path';

const Schema = JSON.parse(ReadFile(new URL('../../types/schema.json', import.meta.url), 'utf8'));
const Definitions = ReadFile(new URL('../../types/LUI.d.luau', import.meta.url), 'utf8');
const Release = JSON.parse(ReadFile(new URL('../Release.json', import.meta.url), 'utf8'));
Test('two release inputs retain independent SDK pins and refuse historical input drift', async () => {
  const Repository = FilePath(new URL('../../', import.meta.url));
  const Catalog = await LoadReleases(Repository);
  Assert.equal(Catalog.Current, '0.1.0-preview.2');
  Assert.deepEqual(Catalog.Releases.map(Entry => Entry.Metadata.SdkVersion), ['0.1.0-preview.1','0.1.0-preview.2']);
  Assert.equal(Catalog.Releases[0].Metadata.SdkManifestSha256, Release.SdkManifestSha256);
  Assert.notEqual(Catalog.Releases[1].Metadata.SdkManifestSha256, Release.SdkManifestSha256);
  const Temporary = await MakeTemporary(Path.join(TemporaryDirectory(), 'lui-release-inputs-'));
  try {
    const Data = JSON.parse(ReadFile(Path.join(Repository, 'website/Releases.json'), 'utf8'));
    const Files = new Set(['website/Releases.json', ...Data.Releases.flatMap(Entry => Object.keys(Entry.Inputs))]);
    for (const Name of Files) { await MakeDirectory(Path.dirname(Path.join(Temporary, Name)), {recursive:true}); await CopyFile(Path.join(Repository, Name), Path.join(Temporary, Name)); }
    await WriteFile(Path.join(Temporary,'website/Content/Setup.md.in'), 'changed historical guide');
    await Assert.rejects(LoadReleases(Temporary), /Frozen release input differs/);
    await CopyFile(Path.join(Repository,'website/Content/Setup.md.in'),Path.join(Temporary,'website/Content/Setup.md.in'));
    await WriteFile(Path.join(Temporary,'website/scripts/Generate.mjs'),'changed historical generator');
    await Assert.rejects(LoadReleases(Temporary), /Frozen release input differs/);
  } finally { await Remove(Temporary,{recursive:true,force:true}); }
});
Test('development generation removes retired pages only inside its generated library subtree', async () => {
  const Site = await MakeTemporary(Path.join(TemporaryDirectory(), 'lui-library-docs-'));
  try {
    const Output = Path.join(Site, 'src/content/docs/next/libraries/luiworkbench');
    await MakeDirectory(Output, { recursive: true });
    await WriteFile(Path.join(Output, 'retired.md'), 'retired API');
    await WriteFile(Path.join(Site, 'keep.txt'), 'unrelated');
    const Result = await GenerateLibraryReference(FilePath(new URL('../../', import.meta.url)), Site);
    Assert.equal(Result.Pages, 13); Assert.deepEqual(Result.Guides, ['workflow']);
    Assert.equal(Exists(Path.join(Output, 'retired.md')), false);
    Assert.equal(Exists(Path.join(Output, 'commands.md')), true);
    Assert.equal(Exists(Path.join(Site, 'src/content/docs/next/libraries/json/json.md')), true);
    Assert.equal(Exists(Path.join(Site, 'src/content/docs/next/libraries/jsonclient/jsonclient.md')), true);
    Assert.equal(Exists(Path.join(Site, 'src/content/docs/next/libraries/luiui/numericinput.md')), true);
    Assert.equal(ReadFile(Path.join(Site, 'src/content/docs/next/guides/workflow.md'), 'utf8'),
      ReadFile(new URL('../Content/NextWorkflow.md.in', import.meta.url), 'utf8'));
    Assert.equal(Exists(Path.join(Site, 'keep.txt')), true);
  } finally { await Remove(Site, { recursive: true, force: true }); }
});
Test('development module declarations stop before implementations and reconcile the catalog', () => {
  const Catalog = JSON.parse(ReadFile(new URL('../../libraries/LuiWorkbench/Library.json', import.meta.url), 'utf8'));
  Assert.equal(Catalog.Status, 'Development');
  for (const Module of Catalog.Modules) {
    const Types = ModuleDeclarations(ReadFile(new URL('../../libraries/LuiWorkbench/' + Module.Name + '.luau', import.meta.url), 'utf8'));
    Assert.deepEqual([...Types.keys()], Module.Types);
    for (const Declaration of Types.values()) Assert.equal(/^(?:local |function )/m.test(Declaration), false);
  }
  Assert.throws(() => ModuleDeclarations('export type Broken = {\n'), /Unbalanced/);
  Assert.throws(() => ModuleDeclarations('export type A = {}\nexport type A = {}'), /Duplicate/);
});
Test('reference reconciles every reflected class/service/object and constructor', () => {
  const Parsed = Validate(Schema, Definitions, Release, Summaries);
  for (const Name of ['Instance','Vector2','UDim','UDim2','app','task','require','Color3']) Assert.ok(Parsed.Globals.has(Name));
  for (const Name of ['Connection','Signal','InputEvent','KeyboardShortcut','TextButtonInit']) Assert.ok(Parsed.Exports.has(Name));
});
Test('SDK definition drift is refused', () => Assert.throws(() => Validate(Schema, Definitions + '\n-- changed', Release, Summaries), /differ/));
Test('unknown editorial symbols are refused', () => Assert.throws(() => Validate(Schema, Definitions, Release, { ...Summaries, ImaginaryButton:'Unsupported' }), /Unknown editorial/));
Test('missing explanations are refused', () => { const Copy={...Summaries};delete Copy.TextButton;Assert.throws(()=>Validate(Schema,Definitions,Release,Copy),/Missing explanation/); });
Test('missing and cyclic bases are refused', () => {
  for (const Base of ['NotAClass', 'TextButton']) { const Copy=structuredClone(Schema);Copy.classes.find(Item=>Item.name==='TextButton').base=Base;Assert.throws(()=>Validate(Copy,Definitions,Release,Summaries),/inheritance/); }
});
Test('all tutorial files are declared exactly once and pin the selected SDK', () => {
  for (const Checkpoint of Checkpoints()) {
    const Files = ProjectFiles(Checkpoint,Release);const Manifest=JSON.parse(Files.get('lui.json'));const Pin=JSON.parse(Files.get('LuiToolchain.json'));
    Assert.deepEqual(Manifest.Modules.Files.sort(), Object.keys(Checkpoint.Files).sort());
    Assert.equal(Pin.SdkManifestSha256,Release.SdkManifestSha256);Assert.equal(Pin.SdkVersion,Release.SdkVersion);
    Assert.deepEqual(Manifest.Capabilities,[]);Assert.deepEqual(Manifest.Extensions,[]);
  }
});
Test('archives are deterministic and reject parent traversal', () => {
  const Files=ProjectFiles(Checkpoints()[2],Release);Assert.deepEqual(MakeZip(Files),MakeZip(Files));
  Assert.throws(()=>MakeZip(new Map([['../escape','bad']])),/Invalid archive path/);
  Assert.equal(MakeZip(Files).readUInt32LE(0),0x04034b50);
});
Test('practical recipes declare complete projects, bounded local networking and binary assets', () => {
  const Projects = Recipes();
  Assert.equal(Projects.length, 4);
  for (const Project of Projects) {
    const Files = ProjectFiles(Project, Release);
    const Manifest = JSON.parse(Files.get('lui.json'));
    Assert.deepEqual(Manifest.Modules.Files, Object.keys(Project.Files).sort());
    Assert.deepEqual(Manifest.Assets, Object.keys(Project.Assets ?? {}).sort());
    for (const Name of Manifest.Assets) Assert.ok(Buffer.isBuffer(Files.get(Name)));
    Assert.equal(JSON.parse(Files.get('LuiToolchain.json')).SdkManifestSha256, Release.SdkManifestSha256);
    Assert.deepEqual(MakeZip(Files), MakeZip(Files));
  }
  const Http = JSON.parse(ProjectFiles(Projects.find(Project => Project.Name === 'Recipe-Http'), Release).get('lui.json'));
  Assert.deepEqual(Http.Capabilities, ['network.client', 'network.server']);
  Assert.equal(Http.NetworkPolicy.ClientLoopbackOnly, true); Assert.equal(Http.NetworkPolicy.ServerLoopbackOnly, true);
});
Test('CRLF checkouts do not change source identity; archive hashing preserves bytes', () => {
  Assert.equal(Hash(Definitions),Hash(Definitions.replaceAll('\r\n','\n').replaceAll('\n','\r\n')));
  Assert.notEqual(Hash(Buffer.from([0xff])),Hash(Buffer.from([0xef,0xbf,0xbd])));
  Assert.equal(Declarations(Definitions).Globals.get('task').includes('delay'),true);
});
