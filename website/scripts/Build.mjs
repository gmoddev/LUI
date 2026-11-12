// The frozen base generator preserves released API identities and tutorial ZIPs.
// This explicit, catalog-hashed editorial revision adds explanations/examples.
import './Generate.mjs';
import { readFile as ReadFile, writeFile as WriteFile, readdir as ReadDirectory, mkdir as MakeDirectory } from 'node:fs/promises';
import { fileURLToPath as FilePath } from 'node:url';
import Path from 'node:path';
import { LoadReleases } from './Releases.mjs';
import { Hash } from './Reference.mjs';
import { ImproveReference, TagSearchPages } from './Documentation.mjs';
import { ImproveLibraries } from './DocumentationLibraries.mjs';
import { Examples, Globals } from './DocumentationExamples.mjs';

const Site=Path.resolve(FilePath(new URL('..',import.meta.url)));const Repository=Path.dirname(Site);
const Catalog=await LoadReleases(Repository);const Reports=[];
const Fence=Text=>'\n```luau\n'+Text.trimEnd()+'\n```\n';
async function MarkdownFiles(Directory) {
  const Files=[];
  for(const Entry of await ReadDirectory(Directory,{withFileTypes:true})) {
    const File=Path.join(Directory,Entry.name);
    if(Entry.isDirectory())Files.push(...await MarkdownFiles(File));else if(/\.mdx?$/.test(File))Files.push(File);
  }
  return Files;
}
for(const Entry of Catalog.Releases) {
  const Version=Entry.Metadata.SdkVersion;
  const Downloads=Path.join(Site,'public/downloads',Version);
  const Before=new Map();for(const Name of await ReadDirectory(Downloads))Before.set(Name,Hash(await ReadFile(Path.join(Downloads,Name))));
  const Reference=await ImproveReference(Repository,Site,Entry);
  const Root=Path.join(Site,'src/content/docs/docs/sdk-'+Version);
  const Templates={'ReadingApi.md.in':'reading-the-api.md'};
  if(Version===Catalog.Current)Object.assign(Templates,{'Libraries.md.in':'libraries.md','Layout.md.in':'layout.md','Modules.md.in':'modules.md','Capabilities.md.in':'capabilities.md'});
  const Banner='<div class="LuiRelease"><span>SDK '+Version+' · Experimental</span><a href="https://github.com/gmoddev/LUI/releases/tag/'+Entry.Metadata.Tag+'">Download &amp; release notes ↗</a></div>\n\n';
  for(const [Template,Destination] of Object.entries(Templates)) {
    let Text=await ReadFile(Path.join(Site,'Content/Review',Template),'utf8');
    Text=Text.replace(/\{\{(Example|GlobalExample):([^}]+)\}\}/g,(_,Kind,Name)=>{
      const Sample=(Kind==='Example'?Examples:Globals)[Name];if(!Sample)throw new Error('[LUI:Docs] Unknown guide example');return Fence(Sample.Code);
    });
    if(/\{\{/.test(Text))throw new Error('[LUI:Docs] Unexpanded editorial guide');
    Text=Text.replace(/^(---\n[\s\S]*?\n---\n)/,'$1\n'+Banner+(Version!==Catalog.Current?'> **Archived SDK.** These examples describe preview.1 core APIs. Optional libraries and automatic app-private storage provisioning first ship in preview.2.\n\n':''));
    await WriteFile(Path.join(Root,'guides',Destination),Text);
  }
  let Libraries=null;
  if(Entry.VersionedLibraries)Libraries=await ImproveLibraries(Repository,Site,Version);
  if(Version===Catalog.Current) {
    const First=Path.join(Root,'learn/first-project.md');let Text=await ReadFile(First,'utf8');
    Text=Text.replace('use **LUI: Stop App**, then **LUI: Run App**','use **LUI: Restart App**').replace('stop the native app and run it again after saving.','save the files and use Restart App.');
    await WriteFile(First,Text);
    const Advanced=Path.join(Root,'learn/advanced-project.md');Text=(await ReadFile(Advanced,'utf8')).replace('title: Your first advanced project','title: Build a modular app').replaceAll('a fresh generation','fresh Luau state').replaceAll('app generation','app run');await WriteFile(Advanced,Text);
  }
  await TagSearchPages(Root,Version);
  for(const [Name,Digest]of Before)if(Hash(await ReadFile(Path.join(Downloads,Name)))!==Digest)throw new Error('[LUI:Docs] Editorial update changed an existing tutorial download');
  Reports.push({...Reference,Libraries,Guides:Object.values(Templates),ExistingDownloadsPreserved:Before.size});
}
const Current=Path.join(Site,'src/content/docs/docs/sdk-'+Catalog.Current);
const Legacy=Path.join(Site,'src/content/docs/next');
for(const File of await MarkdownFiles(Legacy)) {
  const Relative=Path.relative(Legacy,File);const Canonical=Path.join(Current,Relative);
  const Route=Relative.replaceAll(Path.sep,'/').replace(/(?:^|\/)index\.mdx?$/,'/').replace(/\.mdx?$/,'/');
  let Text=await ReadFile(Canonical,'utf8');
  Text=Text.replace(/^(---\n[\s\S]*?\n---\n)/,'$1\n> **Legacy address for preview.2 documentation.** Use the [versioned page](/LUI/docs/sdk-'+Catalog.Current+'/'+Route+'). The API is the same.\n\n');
  await WriteFile(File,Text);
}
await TagSearchPages(Legacy,Catalog.Current,true);
const Home=Path.join(Site,'src/content/docs/index.mdx');let Text=await ReadFile(Home,'utf8');
Text=Text.replaceAll('Your first advanced project','Build a modular app').replace(/^(---\n[\s\S]*?\n---\n)/,'$1\n<span hidden data-pagefind-filter="SDK:'+Catalog.Current+'"></span>\n');await WriteFile(Home,Text);
const BuildReport=JSON.parse(await ReadFile(Path.join(Site,'BuildReport.json'),'utf8'));
BuildReport.DocumentationRevision='2026-11-12';BuildReport.Documentation=Reports;
BuildReport.Pages=(await MarkdownFiles(Path.join(Site,'src/content/docs'))).length+1;
await WriteFile(Path.join(Site,'BuildReport.json'),JSON.stringify(BuildReport,null,2)+'\n');
console.log('[LUI:Docs] Reviewed '+Reports[0].Symbols+' reflected APIs, '+Reports[0].Members+' members, '+Reports[0].Types+' types and '+Reports[0].Globals+' globals per SDK; usage examples and existing downloads verified.');
