import CreateRuntime from './LuiBrowser.mjs';
import { Message, Bytes, Limits } from './Protocol.mjs';
import { Descriptor } from './Deployment.mjs';

let Module, Generation, Incoming = 0, Outgoing = 0, Timer, Closed = false, Phase = 'Created';
let Chain = Promise.resolve(), Queued = 0, QueuedBytes = 0;
const Send = (Kind, Fields = {}) => {
    const Data = { Version: 1, Generation, Sequence: Outgoing + 1, Kind, ...Fields };
    Message(Data, Generation, Outgoing, 'Worker');
    ++Outgoing; self.postMessage(Data);
};
// Fixed numeric aggregates only; no payloads, source names or per-event traces.
// This private worker diagnostic is not an application API or a paint metric.
self.LuiOwnerWork = { Calls: 0, TotalMs: 0, MaxMs: 0, Over8Ms: 0, MemoryBytes: 0, PeakMemoryBytes: 0 };
const Call = (Name, Result, Types = [], Values = []) => {
    const Begin = performance.now();
    try { return Module.ccall(`Lui_Browser${Name}`, Result, Types, Values); }
    finally {
        const Elapsed = performance.now() - Begin, Work = self.LuiOwnerWork;
        Work.MemoryBytes = Module.ccall('Lui_BrowserMemory', 'number');
        Work.PeakMemoryBytes = Math.max(Work.PeakMemoryBytes, Work.MemoryBytes);
        if (!['Start', 'Begin', 'Capture', 'Retire'].includes(Name)) {
            ++Work.Calls; Work.TotalMs += Elapsed; Work.MaxMs = Math.max(Work.MaxMs, Elapsed);
            if (Elapsed > 8) ++Work.Over8Ms;
        }
    }
};
function Stop() {
    clearInterval(Timer); Closed = true;
    if (Module) { try { Call('Retire', null); } catch {} }
}
function Fatal() {
    if (Closed) return;
    Stop();
    try { Send('Fatal', { Code: 'BrowserFailure' }); } catch {}
    self.close();
}
function Flush() {
    const Batch = JSON.parse(Call('Take', 'string'));
    if (Batch.Failed) { Fatal(); return; }
    if (Batch.Ops.length) Send('PresentBatch', { Revision: Batch.Revision, Ops: Batch.Ops });
}
function Run(Capture) {
    if (!Call('Start', 'number', ['string'], [Capture])) throw new Error('Start');
    Phase = 'Running'; Send('Ready'); Flush();
    Timer = setInterval(() => { try { Call('Pump', 'number'); Flush(); } catch { Fatal(); } }, 16);
}
async function Receive(Data) {
    if (Closed) return;
    Message(Data, Generation, Incoming, 'Page'); Incoming = Data.Sequence;
    if (Phase === 'Created') {
        if (!['Initialize', 'Begin'].includes(Data.Kind)) throw new Error('Initialization');
        Generation = Data.Generation; Phase = 'Preparing';
        Module = await CreateRuntime({ print: () => {}, printErr: () => {} });
        if (Closed) return;
        if (Data.Kind === 'Initialize') Run(Data.Capture);
        else {
            Descriptor(JSON.parse(Data.Descriptor));
            if (!Call('Begin', 'number', ['string'], [Data.Descriptor])) throw new Error('Descriptor');
            Phase = 'Capturing'; Send('Result', { For: 'Begin', Accepted: 1, ReplyTo: Data.Sequence });
        }
        return;
    }
    if (Data.Kind === 'Retire') { Stop(); Send('Retired'); self.close(); return; }
    if (Phase === 'Capturing') {
        if (Data.Kind === 'File') {
            if (!Call('Capture', 'number', ['string', 'array', 'number', 'number'], [Data.Path, Data.Chunk, Data.Chunk.byteLength, Number(Data.Final)])) throw new Error('File');
            Send('Result', { For: 'File', Accepted: 1, ReplyTo: Data.Sequence }); return;
        }
        if (Data.Kind === 'Start') { Run(''); return; }
        throw new Error('CapturePhase');
    }
    if (Phase !== 'Running') throw new Error('Phase');
    let Accepted;
    if (Data.Kind === 'Applied') Accepted = Call('Ack', 'number', ['number', 'number'], [Data.Revision, Number(Data.Success)]);
    else if (Data.Kind === 'Activate') Accepted = Call('Activate', 'number', ['number', 'number'], [Data.Id, Data.Revision]);
    else if (Data.Kind === 'Input') Accepted = Call('Event', 'number', ['number', 'number', 'string'], [Data.Id, Data.Revision, JSON.stringify(Data.Event)]);
    else if (Data.Kind === 'Viewport') Call('Viewport', 'number', ['number', 'number', 'number'], [Data.Id, Data.Width, Data.Height]);
    else if (Data.Kind === 'Theme') Call('Theme', 'number', ['string'], [Data.Theme]);
    else throw new Error('Kind');
    if (Data.Kind === 'Applied' && !Accepted) throw new Error('Ack');
    Call('Pump', 'number'); Flush();
    // Publish any resulting presentation before reopening input admission.
    if (Accepted !== undefined) Send('Result', { Accepted, For: Data.Kind, ReplyTo: Data.Sequence });
}
self.onmessage = Event => {
    if (Closed) return;
    let Size;
    try { Size = Bytes(Event.data); } catch { Fatal(); return; }
    if (Size > Limits.Message || Queued >= Limits.Events || Size > Limits.Queue - QueuedBytes) { Fatal(); return; }
    ++Queued; QueuedBytes += Size;
    Chain = Chain.then(() => Receive(Event.data)).catch(() => Fatal()).finally(() => { --Queued; QueuedBytes -= Size; });
};
self.onmessageerror = () => Fatal();
