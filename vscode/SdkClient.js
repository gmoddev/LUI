'use strict';

const Fs = require('node:fs');
const Path = require('node:path');
const { execFile: ExecFile } = require('node:child_process');
const Bindings = new Map();

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
        !/^[0-9a-f]{64}$/.test(Data.SdkManifestSha256) || Path.resolve(Data.SdkRoot) !== Path.resolve(Root)) throw new Error('Invalid SDK discovery result.');
    for (const Name of ['Cli', 'Checker', 'Definitions', 'Runtime', 'PreviewHost', 'NativeHost', 'ProjectTool']) {
        const File = Data.Components?.[Name];
        if (typeof File !== 'string') throw new Error('SDK component missing.');
        const Relative = Path.relative(Path.resolve(Root), Path.resolve(File));
        if (!Relative || Relative.startsWith('..') || Path.isAbsolute(Relative)) throw new Error('SDK component escaped its installation.');
    }
    return Data;
}
async function ResolveSdk(Folder, Vscode) {
    if (!UsesSdk(Folder, Vscode)) return null;
    const Root = GetRoot(Vscode);
    if (!Root) throw new Error('This project pins an SDK. Use LUI: Choose SDK to select that installation.');
    const Manifest = GetManifest(Folder, Vscode);
    const Cli = Path.join(Path.resolve(Root), 'Cli', process.platform === 'win32' ? 'lui.exe' : 'lui');
    const Arguments = ['doctor', ...(Manifest && Fs.existsSync(Manifest) ? [Manifest] : []), '--sdk', Path.resolve(Root), '--format', 'json'];
    const Data = await new Promise((Resolve, Reject) => {
        ExecFile(Cli, Arguments, { cwd: Path.resolve(Root), windowsHide: true, shell: false, timeout: 30000, maxBuffer: 1024 * 1024 }, (Error, Output) => {
            if (Error) { Reject(new Error('SDK unavailable or does not match this project. Use LUI: Choose SDK; run lui doctor for details.')); return; }
            try { Resolve(Validate(JSON.parse(Output), Root)); } catch (Failure) { Reject(Failure); }
        });
    });
    Bindings.set(Manifest, Data);
    return Data;
}

module.exports = { GetRoot, GetManifest, UsesSdk, GetBinding, ResolveSdk, Validate };
