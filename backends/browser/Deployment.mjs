import { Shape, Integer, Limits } from './Protocol.mjs';

export const RuntimeFiles = Object.freeze(['Worker.mjs', 'Host.mjs', 'Protocol.mjs', 'Deployment.mjs', 'Application.mjs', 'LuiBrowser.mjs', 'LuiBrowser.wasm', 'Probe.css']);
const Hex = Value => typeof Value === 'string' && /^[0-9a-f]{64}$/.test(Value);
export async function Digest(Bytes) {
    return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', Bytes)), Value => Value.toString(16).padStart(2, '0')).join('');
}
export function Descriptor(Data) {
    if (!Shape(Data, ['FormatVersion', 'Profile', 'FrameworkVersion', 'SchemaSha256', 'LuauRevision', 'SourceDigest', 'CompiledDigest', 'Content', 'Runtime']) ||
        Data.FormatVersion !== 1 || Data.Profile !== 'BrowserHostV1' || Data.FrameworkVersion !== '0.0.1' ||
        Data.LuauRevision !== 'c0e346edd89066b44dca174c9f54ce84c746a540' ||
        !['SchemaSha256', 'SourceDigest', 'CompiledDigest'].every(Name => Hex(Data[Name]))) throw new Error('Descriptor');
    if (!Array.isArray(Data.Content) || !Data.Content.length || Data.Content.length > Limits.Files || !Array.isArray(Data.Runtime)) throw new Error('Files');
    let Total = 0;
    for (const [List, Maximum] of [[Data.Content, Limits.File], [Data.Runtime, 64 * 1024 * 1024]]) {
        const Paths = new Map();
        for (const File of List) {
            if (!Shape(File, ['Path', 'Bytes', 'Sha256']) || typeof File.Path !== 'string' || File.Path.length > 512 ||
                !File.Path.split('/').every(Part => /^[A-Za-z0-9_.-]+$/.test(Part) && !['.', '..'].includes(Part) && !Part.endsWith('.')) ||
                !Integer(File.Bytes, 0, Maximum) || !Hex(File.Sha256)) throw new Error('File');
            const Parts = File.Path.split('/');
            for (let Index = 1; Index <= Parts.length; ++Index) {
                const Path = Parts.slice(0, Index).join('/'), Key = Path.toLowerCase(), Directory = Index < Parts.length;
                const Previous = Paths.get(Key);
                if (Previous && (Previous.Path !== Path || !Previous.Directory || !Directory)) throw new Error('Alias');
                Paths.set(Key, { Path, Directory });
            }
            if (List === Data.Content) { Total += File.Bytes; if (Total > Limits.Capture) throw new Error('Capture'); }
        }
    }
    if (Data.Runtime.map(File => File.Path).sort().join('|') !== [...RuntimeFiles].sort().join('|')) throw new Error('Runtime');
    return Data;
}
export async function Read(URL, Maximum, Signal) {
    if (URL.origin !== location.origin || !['https:', 'http:'].includes(URL.protocol) ||
        (URL.protocol === 'http:' && !['localhost', '127.0.0.1', '[::1]'].includes(URL.hostname))) throw new Error('Origin');
    const Response = await fetch(URL, { credentials: 'omit', redirect: 'error', cache: 'no-store', signal: Signal });
    if (!Response.ok || Response.url !== URL.href || !Response.body || Number(Response.headers.get('Content-Length') || 0) > Maximum) throw new Error('Response');
    const Reader = Response.body.getReader(), Chunks = [];
    let Total = 0;
    try {
        while (true) {
            const { value: Chunk, done: Done } = await Reader.read();
            if (Done) break;
            Total += Chunk.byteLength;
            if (Total > Maximum) throw new Error('Limit');
            Chunks.push(Chunk);
        }
    } catch (Error) { await Reader.cancel().catch(() => {}); throw Error; }
    finally { Reader.releaseLock(); }
    const Result = new Uint8Array(Total); let Offset = 0;
    for (const Chunk of Chunks) { Result.set(Chunk, Offset); Offset += Chunk.byteLength; }
    return Result;
}
