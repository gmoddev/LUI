'use strict';

const Path = require('node:path');
const { execFile: ExecFile } = require('node:child_process');
const { ResolveSdk, GetRoot } = require('./SdkClient');

async function ChooseSdk(Vscode, Output) {
    const Selected = await Vscode.window.showOpenDialog({ canSelectFiles: false, canSelectFolders: true, canSelectMany: false, openLabel: 'Choose LUI SDK' });
    if (!Selected?.length) return;
    const Previous = GetRoot(Vscode);
    await Vscode.workspace.getConfiguration('lui').update('sdkPath', Selected[0].fsPath, Vscode.ConfigurationTarget.Global);
    try {
        const Sdk = await ResolveSdk(null, Vscode);
        await Vscode.commands.executeCommand('setContext', 'lui.sdkReady', true);
        Output.appendLine('[LUI:SDK] Ready: ' + Sdk.SdkVersion);
        Vscode.window.setStatusBarMessage('[LUI:SDK] Ready · ' + Sdk.SdkVersion, 7000);
    } catch (Error) {
        await Vscode.workspace.getConfiguration('lui').update('sdkPath', Previous || undefined, Vscode.ConfigurationTarget.Global);
        Output.appendLine('[LUI:SDK] ' + Error.message);
        Vscode.window.setStatusBarMessage('[LUI:SDK] SDK validation failed. See LUI Preview output.', 7000);
    }
}

async function CreateProject(Vscode, Output, Context) {
    if (!Vscode.workspace.isTrusted) { Output.appendLine('[LUI:SDK] Trust the workspace before creating or running a project.'); return; }
    try {
        const Sdk = await ResolveSdk(null, Vscode);
        if (!Sdk) throw new Error('Use LUI: Choose SDK first.');
        const Parent = await Vscode.window.showOpenDialog({ canSelectFiles: false, canSelectFolders: true, canSelectMany: false, openLabel: 'Choose parent folder' });
        if (!Parent?.length) return;
        const Name = await Vscode.window.showInputBox({ prompt: 'New LUI project name', value: 'MyApp', validateInput: Value =>
            /^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(Value) ? null : 'Start with a letter; use letters, numbers, _ or - (64 characters maximum).' });
        if (!Name || !Vscode.workspace.isTrusted) return;
        const Destination = Path.join(Parent[0].fsPath, Name);
        await new Promise((Resolve, Reject) => ExecFile(Sdk.Components.Cli, ['new', Destination, '--sdk', Sdk.SdkRoot],
            { cwd: Sdk.SdkRoot, shell: false, windowsHide: true, timeout: 30000, maxBuffer: 1024 * 1024 }, Error => Error ? Reject(new Error('Project creation failed. Choose a new directory and check the SDK.')) : Resolve()));
        await Vscode.commands.executeCommand('setContext', 'lui.projectCreated', true);
        if (Context) await Context.globalState.update('LuiPendingProject', Destination);
        await Vscode.commands.executeCommand('vscode.openFolder', Vscode.Uri.file(Destination));
    } catch (Error) {
        Output.appendLine('[LUI:SDK] ' + Error.message);
        Vscode.window.setStatusBarMessage('[LUI:SDK] ' + Error.message, 7000);
    }
}

module.exports = { ChooseSdk, CreateProject };
