'use strict';

const Fs = require('node:fs');
const Path = require('node:path');
const { spawn } = require('node:child_process');
const { EventEmitter } = require('node:events');

function ResolveNativeHost(Value) {
    const Candidate = Path.resolve(Value);
    const Host = Fs.existsSync(Candidate) && Fs.statSync(Candidate).isDirectory() ?
        Path.join(Candidate, 'Lui.WinUI.exe') : Candidate;
    const Runtime = Path.join(Path.dirname(Host), 'LuiRuntime.dll');
    if (Path.basename(Host).toLowerCase() !== 'lui.winui.exe' || !Fs.existsSync(Host) ||
        !Fs.statSync(Host).isFile() || !Fs.existsSync(Runtime) || !Fs.statSync(Runtime).isFile())
        throw new Error('WinUI host with LuiRuntime.dll was not found');
    return Host;
}

class NativePreview extends EventEmitter {
    constructor(ManifestPath, HostPath, SpawnProcess = spawn) {
        super();
        this.ManifestPath = Path.resolve(ManifestPath);
        this.HostPath = HostPath;
        this.SpawnProcess = SpawnProcess;
        this.Process = null;
        this.Stopping = false;
    }

    Start() {
        if (this.Process) return false;
        this.Stopping = false;
        const Child = this.SpawnProcess(this.HostPath, ['--manifest', this.ManifestPath], {
            cwd: Path.dirname(this.ManifestPath), windowsHide: false, stdio: 'ignore',
        });
        this.Process = Child;
        let Finished = false;
        let LaunchFailed = false;
        Child.once('error', Error => {
            if (Finished) return;
            LaunchFailed = true;
            this.emit('failure', 'Could not start native preview: ' + Error.message);
        });
        Child.once('close', (Code, Signal) => {
            if (Finished) return;
            Finished = true;
            if (this.Process === Child) this.Process = null;
            this.emit('exit', { Code, Signal, WasStopped: this.Stopping, LaunchFailed });
        });
        return true;
    }

    Stop() {
        if (!this.Process) return false;
        this.Stopping = true;
        try {
            if (this.Process.kill()) return true;
            this.Stopping = false;
            this.emit('failure', 'Could not stop native preview.');
            return false;
        }
        catch (Error) {
            this.Stopping = false;
            this.emit('failure', 'Could not stop native preview: ' + Error.message);
            return false;
        }
    }
}

module.exports = { NativePreview, ResolveNativeHost };
