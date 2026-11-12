'use strict';

const Fs = require('node:fs');
const Path = require('node:path');
const { spawn } = require('node:child_process');
const { EventEmitter } = require('node:events');
const { ReadDiagnostic } = require('./NativeDiagnostics');

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
        this.Exited = false;
        this.Retirement = null;
        this.StopWait = null;
        this.StopFailed = false;
        this.Platform = Platform;
    }

    Start() {
        if (this.Process) return false;
        this.Stopping = false;
        this.Exited = false;
        this.StopWait = null;
        this.StopFailed = false;
        let Modular;
        try {
            if(Fs.statSync(this.ManifestPath).size>256*1024) throw new Error('limit');
            Modular=JSON.parse(Fs.readFileSync(this.ManifestPath,'utf8')).SchemaVersion===2;
        } catch {throw new Error('[LUI:NativePreview] InvalidManifest');}
        if(this.Platform==='linux' && !Modular) throw new Error('[LUI:NativePreview] Linux launch requires schema v2');
        const Child = this.SpawnProcess(this.HostPath, [Modular?'--project':'--manifest', this.ManifestPath], {
            cwd: Path.dirname(this.ManifestPath), windowsHide: false, stdio: ['ignore', 'ignore', 'pipe'],
        });
        this.Process = Child;
        let Retire;
        this.Retirement = new Promise(Resolve => { Retire = Resolve; });
        let Finished = false;
        let DrainTimer;
        let LaunchFailed = false;
        let StartupDiagnostic = false;
        let Pending = '';
        let Oversized = false;
        let Records = 0;
        Child.stderr?.setEncoding('utf8');
        Child.stderr?.on('error', () => {});
        Child.stderr?.on('data', Chunk => {
            if (Finished || this.Process !== Child || this.Stopping || Records >= 256) return;
            for (const Part of String(Chunk).split(/(?<=\n)/)) {
                const Complete = Part.endsWith('\n');
                if (!Oversized && Pending.length + Part.length <= 1026) Pending += Part;
                else Oversized = true;
                if (!Complete) continue;
                const Diagnostic = Oversized ? null : ReadDiagnostic(Pending.replace(/\r?\n$/, ''));
                Pending = ''; Oversized = false;
                if (Diagnostic && Records++ < 256) {
                    StartupDiagnostic ||= Diagnostic.Phase === 'Startup';
                    this.emit('diagnostic', Diagnostic);
                }
            }
        });
        Child.once('spawn', () => { if (!Finished && this.Process === Child && !this.Stopping) this.emit('started'); });
        Child.once('error', Error => {
            if (Finished) return;
            LaunchFailed = true;
            this.emit('failure', Modular?'[LUI:NativePreview] LaunchFailed':'Could not start native preview: ' + Error.message);
        });
        const Finish = (Code, Signal) => {
            if (Finished) return;
            Finished = true;
            clearTimeout(DrainTimer);
            if (this.Process === Child) this.Process = null;
            Pending = '';
            Retire();
            this.emit('exit', { Code, Signal, WasStopped: this.Stopping, LaunchFailed, StartupDiagnostic });
        };
        Child.once('exit', (Code, Signal) => {
            if (Finished) return;
            this.Exited = true;
            this.emit('retiring');
            // A descendant may retain stderr after the owned app has exited.
            // Give diagnostics the same bounded drain used by the CLI.
            DrainTimer = setTimeout(() => { Child.stderr?.destroy(); Finish(Code, Signal); }, 1000);
            if (this.Stopping) Child.stderr?.destroy();
        });
        Child.once('close', Finish);
        return true;
    }

    Stop() {
        if (!this.Process) return false;
        this.StopFailed = false;
        this.Stopping = true;
        if (this.Exited) { this.Process.stderr?.destroy(); return true; }
        try {
            if (this.Process.kill()) return true;
            this.Stopping = false;
            this.StopFailed = true;
            this.emit('failure', 'Could not stop native preview.');
            return false;
        }
        catch {
            this.Stopping = false;
            this.StopFailed = true;
            this.emit('failure', 'Could not stop native preview. Close its window and try again.');
            return false;
        }
    }

    async StopAsync() {
        if (!this.Process) return true;
        if (this.StopWait) return this.StopWait;
        const Retirement = this.Retirement;
        const Operation = (async () => {
            if (!this.Stop()) return false;
            let Timer;
            try {
                return await Promise.race([
                    Retirement.then(() => true),
                    new Promise(Resolve => { Timer = setTimeout(() => {
                        this.StopFailed = true;
                        this.emit('failure', 'App did not stop. Close its window and try again.');
                        Resolve(false);
                    }, 5000); }),
                ]);
            } finally { clearTimeout(Timer); }
        })();
        this.StopWait = Operation;
        try { return await Operation; }
        finally { if (this.StopWait === Operation) this.StopWait = null; }
    }
}

module.exports = { NativePreview, ResolveNativeHost };
