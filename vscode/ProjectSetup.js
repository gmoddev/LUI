'use strict';

const Fs = require('node:fs');
const Path = require('node:path');

const Advice = Object.freeze({
    Manifest: 'Application manifest is unavailable or is not valid UTF-8 JSON. Check the saved manifest.',
    Limit: 'Application manifest exceeds its supported byte limit.',
    Settings: 'Invalid LUI tool settings. Check the manifest path and selected SDK.',
    Entry: 'Invalid application entry script.',
    Outside: 'Application entry script left the manifest directory.',
    Linked: 'Application entry script uses a symlink.',
    ScriptLimit: 'Entry script exceeds 8 MiB.',
    Source: 'Entry script is unavailable or is not a regular UTF-8 file.',
    Tools: 'Required tools were not found. Check the selected SDK or explicit tool paths.',
});
class ProjectSetupError extends Error {
    constructor(Code) { super(Advice[Code] || Advice.Settings); }
}
function SetupMessage(Failure) {
    return Failure instanceof ProjectSetupError ? Failure.message : 'Could not prepare this project. Check the saved files, tool settings and selected SDK; run lui doctor for details.';
}
function ReadText(File, Maximum, Kind = 'Manifest') {
    let Descriptor;
    try {
        if (!Fs.lstatSync(File).isFile()) throw new ProjectSetupError(Kind);
        Descriptor = Fs.openSync(File, Fs.constants.O_RDONLY | (Fs.constants.O_NOFOLLOW || 0) | (Fs.constants.O_NONBLOCK || 0));
        const Stat = Fs.fstatSync(Descriptor);
        if (!Stat.isFile()) throw new ProjectSetupError(Kind);
        if (Stat.size > Maximum) throw new ProjectSetupError(Kind === 'Manifest' ? 'Limit' : 'ScriptLimit');
        const Bytes = Buffer.alloc(Stat.size + 1);
        let Count = 0;
        while (Count < Bytes.length) {
            const Read = Fs.readSync(Descriptor, Bytes, Count, Bytes.length - Count, null);
            if (!Read) break;
            Count += Read;
        }
        if (Count !== Stat.size) throw new ProjectSetupError(Kind);
        return new TextDecoder('utf-8', { fatal: true }).decode(Bytes.subarray(0, Count));
    } catch (Failure) { throw Failure instanceof ProjectSetupError ? Failure : new ProjectSetupError(Kind); }
    finally { if (Descriptor !== undefined) Fs.closeSync(Descriptor); }
}
function ReadManifest(File, Maximum = 256 * 1024) {
    try {
        const Text = ReadText(File, Maximum);
        const Value = JSON.parse(Text);
        if (!Value || typeof Value !== 'object' || Array.isArray(Value) || ![1, 2].includes(Value.SchemaVersion)) throw new ProjectSetupError('Manifest');
        return Value;
    } catch (Failure) { throw Failure instanceof ProjectSetupError ? Failure : new ProjectSetupError('Manifest'); }
}
function GetSettingsIdentity(Folder, Vscode) {
    const Settings = Vscode.workspace.getConfiguration('lui', Folder.uri);
    const Names = ['manifestPath', 'sdkPath', 'previewHostPath', 'runtimePath', 'cliPath', 'checkerPath', 'definitionsPath'];
    const Values = Names.map(Name => Settings.get(Name, Name === 'manifestPath' ? 'lui.json' : ''));
    if (Values.some(Value => typeof Value !== 'string')) throw new ProjectSetupError('Settings');
    return JSON.stringify([Path.resolve(Folder.uri.fsPath), ...Values, process.env.LUI_SDK || '', process.env.LUI_RUNTIME || '', process.env.LUI_TYPECHECK || '']);
}
function HasFolder(Folder, Vscode) {
    return Vscode.workspace.workspaceFolders?.some(Current => Path.relative(Current.uri.fsPath, Folder.uri.fsPath) === '') || false;
}
function Contains(Root, File) {
    const Relative = Path.relative(Root, File);
    return Relative !== '..' && !Relative.startsWith('..' + Path.sep) && !Path.isAbsolute(Relative);
}

module.exports = { ReadText, ReadManifest, GetSettingsIdentity, HasFolder, Contains, ProjectSetupError, SetupMessage };
