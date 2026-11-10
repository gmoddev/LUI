import { readFile as ReadFile, readdir as ReadDirectory, stat as Stat } from 'node:fs/promises';
import Path from 'node:path';
import { fileURLToPath as FileUrlToPath } from 'node:url';

const Root = Path.resolve(FileUrlToPath(new URL('../dist/', import.meta.url)));
async function Walk(Directory) {
  const Result = [];
  for (const Entry of await ReadDirectory(Directory, { withFileTypes: true })) {
    const FilePath = Path.join(Directory, Entry.name);
    if (Entry.isDirectory()) Result.push(...await Walk(FilePath)); else Result.push(FilePath);
  }
  return Result;
}
const Files = await Walk(Root); const HtmlFiles = Files.filter(Name => Name.endsWith('.html'));
const ExistingFiles = new Set(Files.map(Name => Path.relative(Root, Name).replaceAll('\\', '/')));
const Documents = new Map(); const Failures = [];
const Decode = Text => Text.replaceAll('&amp;', '&').replaceAll('&#x26;', '&');
for (const FilePath of HtmlFiles) {
  const Text = await ReadFile(FilePath, 'utf8');
  const Relative = Path.relative(Root, FilePath).replaceAll('\\', '/');
  Documents.set(Relative, { Text, Ids: new Set([...Text.matchAll(/\bid="([^"]+)"/g)].map(Match => Match[1])) });
  if (/\{\{(?:Code|Manifest|Download):/.test(Text)) Failures.push(Relative + ': unexpanded tutorial');
}
let Links = 0;
for (const [Relative, Document] of Documents) {
  const PageUrl = new URL('/LUI/' + Relative.replace(/index\.html$/, ''), 'https://gmoddev.github.io');
  for (const Match of Document.Text.matchAll(/\b(?:href|src)="([^"]+)"/g)) {
    const Href = Decode(Match[1]); if (/^(?:data:|mailto:|tel:)/.test(Href)) continue;
    const Target = new URL(Href, PageUrl); if (Target.origin !== PageUrl.origin) continue;
    if (!Target.pathname.startsWith('/LUI/')) { Failures.push(Relative + ': outside /LUI/ base: ' + Href); continue; }
    let Name = decodeURIComponent(Target.pathname.slice('/LUI/'.length));
    if (!Name || Name.endsWith('/')) Name += 'index.html';
    // GitHub Pages serves this document for unknown routes, including /404/.
    if (Name === '404/index.html' && Documents.has('404.html')) Name = '404.html';
    const FilePath = Path.resolve(Root, Name);
    if (!FilePath.startsWith(Root + Path.sep)) { Failures.push(Relative + ': escaped output path'); continue; }
    if (!ExistingFiles.has(Name)) { Failures.push(Relative + ': missing ' + Href); continue; }
    if (Target.hash && Name.endsWith('.html')) {
      const Id = decodeURIComponent(Target.hash.slice(1));
      if (!Documents.get(Name)?.Ids.has(Id)) Failures.push(Relative + ': missing anchor ' + Href);
    }
    Links++;
  }
}
if (!Files.some(Name => /[/\\]pagefind[/\\]pagefind\.js$/.test(Name))) Failures.push('Search index missing');
if (!Documents.has('404.html')) Failures.push('404 page missing');
if (Failures.length) throw new Error('[LUI:Docs] Built-site validation failed:\n' + [...new Set(Failures)].slice(0,30).join('\n'));
console.log('[LUI:Docs] Verified ' + HtmlFiles.length + ' HTML pages, ' + Links + ' local links/assets and the search index.');
