import Test from 'node:test';
import Assert from 'node:assert/strict';
import { readFileSync as ReadFile } from 'node:fs';
import { mkdtemp as Temporary, rm as Remove } from 'node:fs/promises';
import { tmpdir as TemporaryDirectory } from 'node:os';
import { fileURLToPath as FilePath } from 'node:url';
import Path from 'node:path';
import { Declarations } from '../scripts/Reference.mjs';
import { Members } from '../scripts/DocumentationContent.mjs';
import { Examples } from '../scripts/DocumentationExamples.mjs';
import { ValidateDocumentation, ImproveReference, ServiceSetup } from '../scripts/Documentation.mjs';
import { LibraryExamples } from '../scripts/DocumentationLibraries.mjs';

const Repository=FilePath(new URL('../../',import.meta.url));
const Schema=JSON.parse(ReadFile(Path.join(Repository,'types/schema.json'),'utf8'));
const Definitions=Declarations(ReadFile(Path.join(Repository,'types/LUI.d.luau'),'utf8'));
Test('every reflected member and public type has authored guidance and a usage example',()=>{
  Assert.deepEqual(ValidateDocumentation(Schema,Definitions),{Symbols:52,Members:237,Types:81,Globals:8});
  const Missing=structuredClone(Members);delete Missing.TextButton.Enabled;
  Assert.throws(()=>ValidateDocumentation(Schema,Definitions,Missing),/Incomplete member documentation/);
  const Typo=structuredClone(Members);Typo.Slider.Step='Unsupported invented member';
  Assert.throws(()=>ValidateDocumentation(Schema,Definitions,Typo),/Incomplete member documentation/);
  const Samples={...Examples};delete Samples.ClipboardService;
  Assert.throws(()=>ValidateDocumentation(Schema,Definitions,Members,Samples),/Missing usage example/);
});
Test('storage setup distinguishes the released SDK host contracts',()=>{
  Assert.match(ServiceSetup('StorageService','0.1.0-preview.2'),/ApplicationId/);
  Assert.match(ServiceSetup('StorageService','0.1.0-preview.1'),/does not support Storage.ApplicationId/);
  Assert.match(ServiceSetup('HttpService','0.1.0-preview.2'),/network.client/);
});
Test('reference generation keeps native method signatures and runnable source aligned',async()=>{
  const Site=await Temporary(Path.join(TemporaryDirectory(),'lui-doc-review-'));
  // ImproveReference writes qualification projects under Repository/build only.
  // Use a distinct temporary repository fixture so source workspaces remain untouched.
  const { mkdir:MakeDirectory, copyFile:CopyFile }=await import('node:fs/promises');
  const Root=Path.join(Site,'Repository');
  try {
    for(const Name of ['types/schema.json','types/LUI.d.luau']){await MakeDirectory(Path.dirname(Path.join(Root,Name)),{recursive:true});await CopyFile(Path.join(Repository,Name),Path.join(Root,Name));}
    const Release=JSON.parse(ReadFile(Path.join(Repository,'website/Release-preview.2.json'),'utf8'));
    const Report=await ImproveReference(Root,Site,{Metadata:Release});
    Assert.equal(Report.ReferenceExamples,89);
    const Output=Path.join(Site,'src/content/docs/docs/sdk-'+Release.SdkVersion);
    const Http=ReadFile(Path.join(Output,'api/services/httpservice.md'),'utf8');
    Assert.match(Http,/RequestAsync\(Options: HttpRequestOptions\) -> HttpResponse/);
    Assert.match(Http,/pcall/);Assert.match(Http,/network.client/);
    Assert.match(ReadFile(Path.join(Output,'api/classes/textbutton.md'),'utf8'),/No new-text|No arguments|without a payload/);
    const Manifest=JSON.parse(ReadFile(Path.join(Root,'build/DocsReference',Release.SdkVersion,'Global-require/lui.json'),'utf8'));
    Assert.deepEqual(Manifest.Modules.Files,['Greeting.luau','Main.luau']);
    Assert.match(ReadFile(Path.join(Output,'api/types/windowinit.md'),'utf8'),/never call `Instance.new\("WindowInit"\)`/);
    Assert.match(Http,/<details>/);
  } finally {await Remove(Site,{recursive:true,force:true});}
});
Test('all eight cataloged source modules have a complete app example with explicit imports',()=>{
  Assert.equal(Object.keys(LibraryExamples).length,8);
  for(const [Name,Sample]of Object.entries(LibraryExamples)) {
    Assert.match(Sample.Code,new RegExp('require\\("@[^"/]+/'+Name+'"\\)'));
    Assert.match(Sample.Code,/Window.Visible = true/);
    Assert.ok(Sample.Outcome);
  }
});
