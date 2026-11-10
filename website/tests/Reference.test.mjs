import Test from 'node:test';
import Assert from 'node:assert/strict';
import { readFileSync as ReadFile } from 'node:fs';
import { Hash, Declarations, Validate, MakeZip } from '../scripts/Reference.mjs';
import { Summaries } from '../scripts/Editorial.mjs';
import { Checkpoints, ProjectFiles } from '../scripts/Tutorials.mjs';

const Schema = JSON.parse(ReadFile(new URL('../../types/schema.json', import.meta.url), 'utf8'));
const Definitions = ReadFile(new URL('../../types/LUI.d.luau', import.meta.url), 'utf8');
const Release = JSON.parse(ReadFile(new URL('../Release.json', import.meta.url), 'utf8'));
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
Test('CRLF checkouts do not change source identity; archive hashing preserves bytes', () => {
  Assert.equal(Hash(Definitions),Hash(Definitions.replaceAll('\r\n','\n').replaceAll('\n','\r\n')));
  Assert.notEqual(Hash(Buffer.from([0xff])),Hash(Buffer.from([0xef,0xbf,0xbd])));
  Assert.equal(Declarations(Definitions).Globals.get('task').includes('delay'),true);
});
