'use strict';

const { spawn: Spawn } = require('node:child_process');
const { EventEmitter } = require('node:events');
const Path = require('node:path');

function ValidateDiagnostics(Result, SourceName, Source) {
    if (!Result || Result.version !== 1 || Result.source !== SourceName.replaceAll('\\', '/') ||
        typeof Result.truncated !== 'boolean' || !Array.isArray(Result.diagnostics) || Result.diagnostics.length > 256)
        throw new Error('Invalid type diagnostic envelope');
    const Lines = Source.split('\n');
    const Position = Value => Value && Number.isSafeInteger(Value.line) && Number.isSafeInteger(Value.column) &&
        Value.line >= 0 && Value.line < Lines.length && Value.column >= 0 && Value.column <= Lines[Value.line].length;
    for (const Diagnostic of Result.diagnostics) {
        const Start = Diagnostic?.range?.start;
        const End = Diagnostic?.range?.end;
        if (!['type', 'syntax'].includes(Diagnostic?.kind) || typeof Diagnostic.message !== 'string' ||
            !Diagnostic.message.length || Diagnostic.message.length > 2048 || !Position(Start) || !Position(End) ||
            End.line < Start.line || (End.line === Start.line && End.column < Start.column))
            throw new Error('Invalid type diagnostic range or message');
    }
    return Result;
}

// A session owns one process. Invalidation suppresses replies from earlier saves/edits.
class TypeCheckSession extends EventEmitter {
    constructor(SpawnProcess = Spawn) {
        super();
        this.SpawnProcess = SpawnProcess;
        this.Job = null;
    }

    Start({ Manifest, Cli, Checker, Definitions, SourceName, Source }) {
        this.Stop();
        const Arguments = ['check', Manifest, '--checker', Checker, '--definitions', Definitions, '--format', 'json'];
        const IsDll = Cli.toLowerCase().endsWith('.dll');
        if (IsDll) Arguments.unshift(Cli);
        const Child = this.SpawnProcess(IsDll ? 'dotnet' : Cli, Arguments, {
            cwd: Path.dirname(Manifest), windowsHide: true, shell: false, detached: process.platform !== 'win32',
            stdio: ['ignore', 'pipe', 'pipe'],
        });
        const Job = { Child, Timer: null, Output: [], Errors: [], OutputBytes: 0, ErrorBytes: 0 };
        this.Job = Job;
        const Fail = Message => {
            if (this.Job !== Job) return;
            this.Stop();
            this.emit('failure', Message);
        };
        Job.Timer = setTimeout(() => Fail('Type checking exceeded the 35 second process limit'), 35000);
        for (const [Stream, Parts, Counter, Limit] of [[Child.stdout, Job.Output, 'OutputBytes', 4 * 1024 * 1024],
            [Child.stderr, Job.Errors, 'ErrorBytes', 64 * 1024]]) {
            Stream.on('data', Chunk => {
                if (this.Job !== Job) return;
                const Bytes = Buffer.from(Chunk);
                Job[Counter] += Bytes.length;
                if (Job[Counter] > Limit) { Fail('Type checker output exceeded its limit'); return; }
                Parts.push(Bytes);
            });
        }
        Child.on('error', Error => Fail('Could not start type checker: ' + Error.message));
        Child.on('close', Code => {
            if (this.Job !== Job) return;
            clearTimeout(Job.Timer);
            this.Job = null;
            try {
                if (Code !== 0 && Code !== 1) throw new Error('Type checker exited: ' + Code);
                const Json = new TextDecoder('utf-8', { fatal: true }).decode(Buffer.concat(Job.Output));
                const Result = ValidateDiagnostics(JSON.parse(Json), SourceName, Source);
                if ((Code === 0) !== (Result.diagnostics.length === 0) || (Code === 0 && Result.truncated))
                    throw new Error('Type checker status disagrees with diagnostics');
                this.emit('result', Result);
            } catch (Error) {
                this.emit('failure', Buffer.concat(Job.Errors).toString('utf8').trim() || Error.message);
            }
        });
    }

    Stop() {
        const Job = this.Job;
        this.Job = null;
        if (Job) {
            clearTimeout(Job.Timer);
            if (Number.isSafeInteger(Job.Child.pid) && Job.Child.pid > 0) {
                if (process.platform === 'win32') {
                    // Include the native analyzer owned by the CLI, without opening a console window.
                    const Killer = Spawn('taskkill', ['/pid', String(Job.Child.pid), '/T', '/F'], {
                        windowsHide: true, shell: false, stdio: 'ignore',
                    });
                    Killer.on('error', () => Job.Child.kill());
                } else {
                    try { process.kill(-Job.Child.pid, 'SIGTERM'); }
                    catch { Job.Child.kill(); }
                }
            } else Job.Child.kill();
        }
    }
}

module.exports = { TypeCheckSession, ValidateDiagnostics };
