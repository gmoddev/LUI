'use strict';

const Fs = require('node:fs');
const Path = require('node:path');
const { spawn } = require('node:child_process');
const { EventEmitter } = require('node:events');

function ResolveNativeHost(Value, Platform = process.platform) {
    const Candidate = Path.resolve(Value);
    if(Platform==='linux') {
        const Host=Fs.existsSync(Candidate) && Fs.statSync(Candidate).isDirectory() ?
            (Fs.existsSync(Path.join(Candidate,'lui-gtk'))?Path.join(Candidate,'lui-gtk'):Path.join(Candidate,'bin','lui-gtk')):Candidate;
        if(Path.basename(Host)!=='lui-gtk' || !Fs.existsSync(Host) || !Fs.statSync(Host).isFile()) throw new Error('[LUI:NativePreview] GtkHostUnavailable');
        return Host;
    }
    if(Platform!=='win32') throw new Error('[LUI:NativePreview] PlatformUnavailable');
    const Host = Fs.existsSync(Candidate) && Fs.statSync(Candidate).isDirectory() ?
        Path.join(Candidate, 'Lui.WinUI.exe') : Candidate;
    const Runtime = Path.join(Path.dirname(Host), 'LuiRuntime.dll');
    if (Path.basename(Host).toLowerCase() !== 'lui.winui.exe' || !Fs.existsSync(Host) ||
        !Fs.statSync(Host).isFile() || !Fs.existsSync(Runtime) || !Fs.statSync(Runtime).isFile())
        throw new Error('WinUI host with LuiRuntime.dll was not found');
    return Host;
}

class NativePreview extends EventEmitter {
    constructor(ManifestPath, HostPath, SpawnProcess = spawn, Platform = process.platform) {
        super();
        this.ManifestPath = Path.resolve(ManifestPath);
        this.HostPath = HostPath;
        this.SpawnProcess = SpawnProcess;
        this.Process = null;
        this.Stopping = false;
        this.Platform = Platform;
    }

    Start() {
        if (this.Process) return false;
        this.Stopping = false;
        let Modular;
        try {
            if(Fs.statSync(this.ManifestPath).size>256*1024) throw new Error('limit');
            Modular=JSON.parse(Fs.readFileSync(this.ManifestPath,'utf8')).SchemaVersion===2;
        } catch {throw new Error('[LUI:NativePreview] InvalidManifest');}
        if(this.Platform==='linux' && !Modular) throw new Error('[LUI:NativePreview] Linux launch requires schema v2');
        const Child = this.SpawnProcess(this.HostPath, [Modular?'--project':'--manifest', this.ManifestPath], {
            cwd: Path.dirname(this.ManifestPath), windowsHide: false, stdio: 'ignore',
        });
        this.Process = Child;
        let Finished = false;
        let LaunchFailed = false;
        Child.once('spawn', () => { if (!this.Stopping) this.emit('started'); });
        Child.once('error', Error => {
            if (Finished) return;
            LaunchFailed = true;
            this.emit('failure', Modular?'[LUI:NativePreview] LaunchFailed':'Could not start native preview: ' + Error.message);
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
