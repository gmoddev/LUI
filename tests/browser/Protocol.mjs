import Assert from 'node:assert/strict';
import { Message, Input, Operation, Bytes } from '../../backends/browser/Protocol.mjs';
import { Descriptor, RuntimeFiles } from '../../backends/browser/Deployment.mjs';
const Generation = 'a'.repeat(32);
const Base = { Version: 1, Generation, Sequence: 1 };
for (const Data of [
    { ...Base, Kind: 'Retire', Extra: 1 }, { ...Base, Kind: 'Activate', Id: -1, Revision: 1 },
    { ...Base, Kind: 'Applied', Revision: 0, Success: true }, { ...Base, Kind: 'Applied', Revision: 1, Success: 1 },
    { ...Base, Kind: 'Retire', Sequence: 2 }, { ...Base, Kind: 'Retire', Generation: 'b'.repeat(32) },
    { ...Base, Kind: 'Viewport', Id: 1, Width: Infinity, Height: 1 }, { ...Base, Kind: 'Theme', Theme: 'Unknown' },
    { ...Base, Kind: 'Ready' }, { ...Base, Kind: 'Initialize', Capture: 'a'.repeat(256 * 1024) },
]) Assert.throws(() => Message(Data, Generation, 0, 'Page'));
Message({ ...Base, Kind: 'Retire' }, Generation, 0, 'Page');
const Chunk = new Uint8Array(65536);
Message({ ...Base, Kind: 'File', Path: 'Src/Main.luau', Chunk, Final: false }, Generation, 0, 'Page');
Assert.ok(Bytes({ Chunk }) < 66000);
structuredClone(Chunk, { transfer: [Chunk.buffer] });
Assert.equal(Chunk.byteLength, 0);
Assert.throws(() => Message({ ...Base, Kind: 'File', Path: 'Empty.luau', Chunk, Final: true }, Generation, 0, 'Page'));
for (const Event of [
    { Kind: 'Focus', Value: 1 }, { Kind: 'Text', Text: 'a', Start: 0, Length: 0, InputRevision: [0, 2 ** 32] },
    { Kind: 'Text', Text: '\0', Start: 0, Length: 0, InputRevision: [1, 0] },
    { Kind: 'Composition', Phase: 1, Text: 'a'.repeat(4097), InputRevision: [1, 0] },
    { Kind: 'Pointer', Phase: 4, Device: 0, PointerId: 1, X: 0, Y: 0 },
    { Kind: 'Key', Phase: 0, Key: 'constructor.prototype' },
]) Assert.throws(() => Input(Event));
Input({ Kind: 'Text', Text: 'A🙂éאב', Start: 2, Length: 1, InputRevision: [1, 0] });
Assert.throws(() => Operation({ Kind: 'Create', Id: 1, Class: 'script' }));
Assert.throws(() => Operation({ Kind: 'Destroy', Id: 1, Extra: true }));
Assert.throws(() => Operation({ Kind: 'Arrange', Id: 1, X: 0, Y: NaN, Width: 1, Height: 1 }));
Assert.throws(() => Descriptor({ FormatVersion: 1 }));
const Deployment = {
    FormatVersion: 1, Profile: 'BrowserHostV1', FrameworkVersion: '0.0.1',
    SchemaSha256: 'a'.repeat(64), LuauRevision: 'c0e346edd89066b44dca174c9f54ce84c746a540',
    SourceDigest: 'b'.repeat(64), CompiledDigest: 'c'.repeat(64),
    Content: [{ Path: 'lui.json', Bytes: 2, Sha256: 'd'.repeat(64) }],
    Runtime: RuntimeFiles.map(Path => ({ Path, Bytes: 1, Sha256: 'e'.repeat(64) })),
};
Descriptor(Deployment);
for (const Paths of [['../Escape'], ['Src/One.luau', 'src/Two.luau'], ['File', 'File/Child'], ['lui.json', 'lui.json']]) {
    Assert.throws(() => Descriptor({ ...Deployment, Content: Paths.map(Path => ({ Path, Bytes: 1, Sha256: 'd'.repeat(64) })) }));
}
Assert.throws(() => Descriptor({ ...Deployment, Runtime: Deployment.Runtime.slice(1) }));
Assert.throws(() => Descriptor({ ...Deployment, SourceDigest: '' }));
console.log('[LUI:BrowserTest] Protocol boundaries passed');
