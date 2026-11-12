'use strict';

const Fs = require('node:fs');
const Path = require('node:path');
const { execFile: ExecFile } = require('node:child_process');
const { createHash: CreateHash } = require('node:crypto');
const Bindings = new Map();
const Pending = new Map();

function GetRoot(Vscode) {
    return Vscode.workspace.getConfiguration('lui').get('sdkPath', '') || process.env.LUI_SDK || '';
}
function GetManifest(Folder, Vscode) {
    return Folder ? Path.resolve(Folder.uri.fsPath, Vscode.workspace.getConfiguration('lui', Folder.uri).get('manifestPath', 'lui.json')) : '';
}
function UsesSdk(Folder, Vscode) {
    return Boolean(GetRoot(Vscode) || (Folder && Fs.existsSync(Path.join(Path.dirname(GetManifest(Folder, Vscode)), 'LuiToolchain.json'))));
}
function GetBinding(Folder, Vscode) {
    if (!UsesSdk(Folder, Vscode)) return null;
    const Binding = Bindings.get(GetManifest(Folder, Vscode));
    if (!Binding || Binding.SdkRoot !== Path.resolve(GetRoot(Vscode))) throw new Error('Choose an installed SDK with LUI: Choose SDK.');
    return Binding;
}
function Validate(Data, Root) {
    if (!Data || Data.SchemaVersion !== 1 || Data.Status !== 'Ready' || typeof Data.SdkVersion !== 'string' ||
        !/^[0-9a-f]{64}$/.test(Data.SdkManifestSha256) || typeof Data.SdkRoot !== 'string' || Path.resolve(Data.SdkRoot) !== Path.resolve(Root)) throw new Error('Invalid SDK discovery result.');
    for (const Name of ['Cli', 'Checker', 'Definitions', 'Runtime', 'PreviewHost', 'NativeHost', 'ProjectTool']) {
        const File = Data.Components?.[Name];
        if (typeof File !== 'string') throw new Error('SDK component missing.');
        const Relative = Path.relative(Path.resolve(Root), Path.resolve(File));
        if (!Relative || Relative.startsWith('..') || Path.isAbsolute(Relative)) throw new Error('SDK component escaped its installation.');
    }
    return Data;
}
function GetPinIdentity(Manifest) {
    if (!Manifest) return 'NoProject';
    const Pin = Path.join(Path.dirname(Manifest), 'LuiToolchain.json');
    let Before, Descriptor;
    try { Before = Fs.lstatSync(Pin, { bigint: true }); }
    catch (Failure) { return Failure.code === 'ENOENT' ? 'Missing' : null; }
    if (!Before.isFile() || Before.isSymbolicLink() || Before.size > 8192n) return null;
    const Matches = Value => Value.isFile() && !Value.isSymbolicLink() &&
        ['dev', 'ino', 'size', 'mtimeNs', 'ctimeNs'].every(Name => Value[Name] === Before[Name]);
    try {
        Descriptor = Fs.openSync(Pin, Fs.constants.O_RDONLY | (Fs.constants.O_NOFOLLOW || 0) | (Fs.constants.O_NONBLOCK || 0));
        if (!Matches(Fs.fstatSync(Descriptor, { bigint: true }))) return null;
        const Bytes = Buffer.alloc(Number(Before.size) + 1);
        let Count = 0;
        while (Count < Bytes.length) {
            const Read = Fs.readSync(Descriptor, Bytes, Count, Bytes.length - Count, null);
            if (!Read) break;
            Count += Read;
        }
        if (Count !== Number(Before.size) || !Matches(Fs.fstatSync(Descriptor, { bigint: true })) ||
            !Matches(Fs.lstatSync(Pin, { bigint: true }))) return null;
        return CreateHash('sha256').update(Bytes.subarray(0, Count)).digest('hex');
    } catch { return null; }
    finally { if (Descriptor !== undefined) { try { Fs.closeSync(Descriptor); } catch { /* Native discovery still validates the pin. */ } } }
}
async function ResolveSdk(Folder, Vscode) {
    if (!UsesSdk(Folder, Vscode)) return null;
    const Root = GetRoot(Vscode);
    if (!Root) throw new Error('This project pins an SDK. Use LUI: Choose SDK to select that installation.');
    const Manifest = GetManifest(Folder, Vscode);
    const Cli = Path.join(Path.resolve(Root), 'Cli', process.platform === 'win32' ? 'lui.exe' : 'lui');
    const Arguments = ['doctor', ...(Manifest && Fs.existsSync(Manifest) ? [Manifest] : []), '--sdk', Path.resolve(Root), '--format', 'json'];
    const Pin = GetPinIdentity(Arguments.includes(Manifest) ? Manifest : '');
    const Key = Pin === null ? Symbol() : JSON.stringify([Manifest, Cli, Arguments, Pin]);
    if (Pending.has(Key)) return Pending.get(Key);
    const Request = new Promise((Resolve, Reject) => {
        ExecFile(Cli, Arguments, { cwd: Path.resolve(Root), windowsHide: true, shell: false, timeout: 30000, maxBuffer: 1024 * 1024 }, (Failure, Output) => {
            if (Failure) { Reject(new Error('SDK unavailable or does not match this project. Use LUI: Choose SDK; run lui doctor for details.')); return; }
            try { Resolve(Validate(JSON.parse(Output), Root)); }
            catch { Reject(new Error('Invalid SDK discovery result. Use LUI: Choose SDK; run lui doctor for details.')); }
        });
    }).then(Data => { Bindings.set(Manifest, Data); return Data; });
    Pending.set(Key, Request);
    try { return await Request; }
    finally { if (Pending.get(Key) === Request) Pending.delete(Key); }
}

module.exports = { GetRoot, GetManifest, UsesSdk, GetBinding, ResolveSdk, Validate };
