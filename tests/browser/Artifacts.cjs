// Inspect the emitted module rather than inferring portability from CMake flags.
const Assert = require('node:assert/strict');
const Fs = require('node:fs');
const Path = require('node:path');
const Bytes = Fs.readFileSync(Path.join(Path.resolve(process.argv[2]), 'LuiBrowser.wasm'));
const Module = new WebAssembly.Module(Bytes);
const Allowed = new Set(['__cxa_begin_catch', '__cxa_end_catch', '__cxa_rethrow', '__cxa_throw', '__cxa_uncaught_exceptions',
    '__assert_fail', '__call_sighandler', '__resumeException', 'llvm_eh_typeid_for', 'emscripten_get_now', 'emscripten_date_now',
    'emscripten_resize_heap', '_tzset_js', '_localtime_js', '_gmtime_js', '_abort_js', '_emscripten_runtime_keepalive_clear', '_setitimer_js']);
const Wasi = new Set(['clock_time_get', 'proc_exit', 'fd_close', 'fd_write', 'fd_seek', 'environ_sizes_get', 'environ_get']);
const Imports = WebAssembly.Module.imports(Module);
for (const Import of Imports) {
    Assert.equal(Import.kind, 'function');
    Assert.ok((Import.module === 'env' && (Allowed.has(Import.name) || /^invoke_[vijdf]+$/.test(Import.name) || /^__cxa_find_matching_catch_[2-5]$/.test(Import.name))) ||
        (Import.module === 'wasi_snapshot_preview1' && Wasi.has(Import.name)), `Unexpected import ${Import.module}.${Import.name}`);
}
const Exports = WebAssembly.Module.exports(Module);
for (const Name of ['Begin', 'Capture', 'Start', 'Take', 'Ack', 'Activate', 'Event', 'Viewport', 'Theme', 'Pump', 'Retire', 'Memory'])
    Assert.ok(Exports.some(Export => Export.name === 'Lui_Browser' + Name && Export.kind === 'function'));
let Offset = 8, Memory;
function Unsigned() {
    let Value = 0, Shift = 0;
    for (let Count = 0; Count < 5; ++Count) {
        Assert.ok(Offset < Bytes.length); const Byte = Bytes[Offset++]; Value += (Byte & 127) * 2 ** Shift;
        if (!(Byte & 128)) return Value; Shift += 7;
    }
    throw new Error('InvalidLEB');
}
while (Offset < Bytes.length) {
    const Id = Bytes[Offset++], Size = Unsigned(), End = Offset + Size; Assert.ok(End <= Bytes.length);
    if (Id === 5) {
        Assert.equal(Unsigned(), 1); const Flags = Unsigned();
        Assert.equal(Flags, 1); // wasm32, unshared, explicit maximum.
        Memory = { InitialBytes: Unsigned() * 65536, MaximumBytes: Unsigned() * 65536 };
        Assert.equal(Offset, End);
    }
    Offset = End;
}
Assert.deepEqual(Memory, { InitialBytes: 33554432, MaximumBytes: 268435456 });
console.log(`[LUI:BrowserTest] WASM import allowlist (${Imports.length}), portable exports and nonshared memory ${JSON.stringify(Memory)} passed`);
