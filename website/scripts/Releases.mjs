import { readFile as ReadFile, readdir as ReadDirectory } from 'node:fs/promises';
import Path from 'node:path';
import { Hash } from './Reference.mjs';

const VersionPattern = /^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$/;
export async function ReadText(File) { return (await ReadFile(File,'utf8')).replaceAll('\r\n','\n'); }
export function ReleaseInput(Repository, Entry, Name) {
  const Root = Entry.SourceRepository || Repository;
  return Path.join(Name.startsWith('types/') || Name.startsWith('libraries/') ? Root : Repository, Name);
}
export async function LoadReleases(Repository, CatalogFile = Path.join(Repository, 'website/Releases.json')) {
  const Catalog = JSON.parse(await ReadFile(CatalogFile, 'utf8'));
  if (Catalog.SchemaVersion !== 1 || !Array.isArray(Catalog.Releases) || !Catalog.Releases.length || Catalog.Releases.length > 10)
    throw new Error('[LUI:Docs] Invalid release catalog');
  const Seen = new Set();
  const Entries = [];
  for (const Entry of Catalog.Releases) {
    if (!Entry.Inputs || !Entry.Templates || typeof Entry.Metadata !== 'string') throw new Error('[LUI:Docs] Missing frozen release inputs');
    if (Entry.SourceRoot !== undefined && (typeof Entry.SourceRoot !== 'string' || !/^website\/Released\/[A-Za-z0-9.-]+$/.test(Entry.SourceRoot)))
      throw new Error('[LUI:Docs] Invalid released source root');
    const SourceRepository = Entry.SourceRoot ? Path.resolve(Repository, Entry.SourceRoot) : Repository;
    for (const [Name, Expected] of Object.entries(Entry.Inputs)) {
      const File = ReleaseInput(Repository, {SourceRepository}, Name);
      if (Path.isAbsolute(Name) || Name.includes('\\') || Name.split('/').some(Part => !Part || Part === '..' || Part === '.') ||
          !File.startsWith(Path.resolve(Repository) + Path.sep) || !/^[a-f0-9]{64}$/.test(Expected))
        throw new Error('[LUI:Docs] Invalid release input');
      if (Hash(await ReadFile(File, 'utf8')) !== Expected) throw new Error('[LUI:Docs] Frozen release input differs: ' + Name);
    }
    const Required = ['types/schema.json', 'types/LUI.d.luau', 'website/scripts/Reference.mjs', 'website/scripts/Generate.mjs',
      'website/scripts/Editorial.mjs', 'website/scripts/Tutorials.mjs', 'website/scripts/Recipes.mjs', Entry.Metadata, ...Object.values(Entry.Templates)];
    if (Entry.VersionedLibraries) {
      Required.push('website/scripts/LibraryReference.mjs');
      for (const Name of ['LuiWorkbench','Json','JsonClient','LuiUI'])
        for (const File of await ReadDirectory(Path.join(SourceRepository,'libraries',Name),{withFileTypes:true}))
          if (File.isFile()) Required.push('libraries/' + Name + '/' + File.name);
    }
    if (Required.some(Name => typeof Name !== 'string' || !Object.hasOwn(Entry.Inputs, Name)))
      throw new Error('[LUI:Docs] Incomplete release input inventory');
    const Metadata = JSON.parse(await ReadFile(Path.join(Repository, Entry.Metadata), 'utf8'));
    if (!VersionPattern.test(Metadata.SdkVersion) || Seen.has(Metadata.SdkVersion)) throw new Error('[LUI:Docs] Invalid or duplicate release');
    Seen.add(Metadata.SdkVersion); Entries.push({ ...Entry, Metadata, SourceRepository });
  }
  if (!Seen.has(Catalog.Current) || Entries.at(-1).Metadata.SdkVersion !== Catalog.Current) throw new Error('[LUI:Docs] Current release must be last');
  return { Current: Catalog.Current, Releases: Entries };
}
