import { readFile as ReadFile, writeFile as WriteFile, mkdir as MakeDirectory, rm as Remove } from 'node:fs/promises';
import Path from 'node:path';

// Module types are separate from the frozen released runtime declarations.
export function ModuleDeclarations(Source) {
  const Result = new Map();
  const Lines = Source.replaceAll('\r\n', '\n').split('\n');
  for (let Index = 0; Index < Lines.length; Index++) {
    const Match = /^export type (\w+)\s*=/.exec(Lines[Index]);
    if (!Match) continue;
    if (Result.has(Match[1])) throw new Error('[LUI:Docs] Duplicate module type');
    const Block = []; let Balance = 0;
    do {
      const Line = Lines[Index]; Block.push(Line);
      for (const Character of Line) {
        if ('{('.includes(Character)) Balance++;
        if ('})'.includes(Character)) Balance--;
      }
      if (Balance < 0 || (Balance > 0 && ++Index >= Lines.length)) throw new Error('[LUI:Docs] Unbalanced module declaration');
    } while (Balance > 0);
    Result.set(Match[1], Block.join('\n'));
  }
  return Result;
}

async function GenerateLibrary(Repository, Site, Name, Options) {
  const Slug = Name.toLowerCase();
  const Library = Path.join(Repository, 'libraries', Name);
  const Catalog = JSON.parse(await ReadFile(Path.join(Library, 'Library.json'), 'utf8'));
  if (Catalog.SchemaVersion !== 1 || Catalog.Status !== 'Development' || Catalog.TargetSdk !== '0.1.0-preview.2') throw new Error('[LUI:Docs] Unknown development library contract');
  const Base = Options.Base || 'next';
  if (Base !== 'next' && !/^docs\/sdk-\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$/.test(Base)) throw new Error('[LUI:Docs] Invalid versioned library root');
  const Output = Path.resolve(Site, 'src/content/docs', Base, 'libraries', Slug);
  const GeneratedRoot = Path.resolve(Site, 'src/content/docs', Base, 'libraries');
  if (Catalog.Name !== Name || Output !== Path.join(GeneratedRoot, Slug) || !Output.startsWith(Path.resolve(Site) + Path.sep)) throw new Error('[LUI:Docs] Invalid development output root');
  await Remove(Output, { recursive: true, force: true });
  await MakeDirectory(Output, { recursive: true });
  const Seen = new Set();
  const Banner = Options.Version === Catalog.TargetSdk ? '> **Included in SDK ' + Options.Version + '.** Experimental source libraries; see the versioned setup and platform limits.\n\n' :
    '> **Development API for the next SDK.** Not included in the released 0.1.0-preview.1 download. No new SDK release is available.\n\n';
  const Fence = Text => '\n```luau\n' + Text + '\n```\n';
  let Index = '---\ntitle: "' + Name + '"\ndescription: Optional source libraries.\n---\n\n' + Banner;
  if (Options.Version === Catalog.TargetSdk) Index += '> **Current release scope:** Windows/Linux headless and editor integration gates pass; the selected Windows virtual recipient also passes. Native GUI/IME and physical-device limits remain explicit in the [platform guide](/LUI/docs/sdk-' + Options.Version + '/guides/platforms/). The bundled guide below retains preparation-era development/status notes; those notes do not supersede this release scope.\n\n';
  const Guide = await ReadFile(Path.join(Library, 'README.md'), 'utf8');
  Index += Guide.replace(/^# [^\n]+\n/, '') + '\n## Module reference\n\n';
  for (const Module of Catalog.Modules) {
    if (!/^[A-Z][A-Za-z]+$/.test(Module.Name) || Seen.has(Module.Name)) throw new Error('[LUI:Docs] Invalid module name');
    Seen.add(Module.Name);
    const Source = await ReadFile(Path.join(Library, Module.Name + '.luau'), 'utf8');
    const Types = ModuleDeclarations(Source);
    if (Types.size !== Module.Types.length || Module.Types.some(Name => !Types.has(Name))) throw new Error('[LUI:Docs] Module type catalog drift');
    const Functions = Module.Functions || ['New'];
    const Signatures = Functions.map(FunctionName => {
      if (!/^[A-Z][A-Za-z]+$/.test(FunctionName)) throw new Error('[LUI:Docs] Invalid function name');
      const Signature = Source.split(/\r?\n/).find(Line => Line.startsWith('function ' + Module.Name + '.' + FunctionName + '('));
      if (!Signature) throw new Error('[LUI:Docs] Missing module function');
      return Signature.replace(/^function /, '');
    });
    if (!Functions.length || !Module.Summary) throw new Error('[LUI:Docs] Missing module contract');
    let Text = '---\ntitle: ' + Module.Name + '\ndescription: ' + Module.Summary + '\n---\n\n' + Banner + Module.Summary + '\n\n';
    Text += '[Usage, options, limits, ownership and cancellation](../).\n\nImport with ' + '`require("@' + Name + '/' + Module.Name + '")`' + '.\n';
    Text += '\n## Functions\n' + Signatures.map(Signature => Fence(Signature)).join('');
    for (const [Name, Declaration] of Types) Text += '\n## ' + Name + '\n' + Fence(Declaration);
    await WriteFile(Path.join(Output, Module.Name.toLowerCase() + '.md'), Text);
    Index += '- [' + Module.Name + '](/LUI/' + Base + '/libraries/' + Slug + '/' + Module.Name.toLowerCase() + '/): ' + Module.Summary + '\n';
  }
  await WriteFile(Path.join(Output, 'index.md'), Index);
  return { TargetSdk: Catalog.TargetSdk, Modules: [...Seen], Pages: Seen.size + 1 };
}

export async function GenerateLibraryReference(Repository, Site, Options = {}) {
  const Libraries = [];
  for (const Name of ['LuiWorkbench', 'Json', 'JsonClient', 'LuiUI']) Libraries.push(await GenerateLibrary(Repository, Site, Name, Options));
  const Guide = Path.resolve(Site, 'src/content/docs', Options.Base || 'next', 'guides');
  await MakeDirectory(Guide, { recursive: true });
  await WriteFile(Path.join(Guide, 'workflow.md'), await ReadFile(Path.join(Repository, Options.Workflow || 'website/Content/NextWorkflow.md.in'), 'utf8'));
  return {TargetSdk: '0.1.0-preview.2', Libraries, Guides: ['workflow'], Pages: 1 + Libraries.reduce((Count, Library) => Count + Library.Pages, 0)};
}
