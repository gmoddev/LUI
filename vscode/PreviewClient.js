'use strict';

const { EventEmitter } = require('node:events');
const { spawn } = require('node:child_process');
const { ValidateSchema, ValidateTree } = require('./PreviewModel');

const MaxLineBytes = 4 * 1024 * 1024 + 64 * 1024;

class PreviewClient extends EventEmitter {
    constructor(ManifestPath, HostPath, RuntimePath) {
        super();
        this.Generation = 0;
        this.Classes = new Map();
        this.Tree = { Nodes: [], ById: new Map(), Children: new Map([[0, []]]) };
        this.HelloSeen = false;
        this.ReloadPending = false;
        this.ReloadAgain = false;
        this.Closed = false;
        this.Buffer = '';
        const IsDll = HostPath.toLowerCase().endsWith('.dll');
        this.Process = spawn(IsDll ? 'dotnet' : HostPath,
            [...(IsDll ? [HostPath] : []), '--manifest', ManifestPath, '--runtime', RuntimePath],
            { cwd: require('node:path').dirname(HostPath), windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
        this.Process.stdout.setEncoding('utf8');
        this.Process.stderr.setEncoding('utf8');
        this.Process.stdout.on('data', Chunk => this.OnData(Chunk));
        this.Process.stderr.on('data', Chunk => this.emit('diagnostic', String(Chunk).trim()));
        this.Process.stdin.on('error', Error => this.Fail('Preview input closed: ' + Error.message));
        this.Process.on('error', Error => this.Fail('Could not start preview host: ' + Error.message));
        this.Process.on('close', (Code, Signal) => {
            const WasClosed = this.Closed;
            this.Closed = true;
            this.emit('exit', { Code, Signal, WasClosed });
        });
    }

    OnData(Chunk) {
        if (this.Closed) return;
        this.Buffer += Chunk;
        if (Buffer.byteLength(this.Buffer, 'utf8') > MaxLineBytes && !this.Buffer.includes('\n')) {
            this.Fail('Preview message exceeds the protocol size limit');
            return;
        }
        let End;
        while ((End = this.Buffer.indexOf('\n')) >= 0) {
            const Line = this.Buffer.slice(0, End).trimEnd();
            this.Buffer = this.Buffer.slice(End + 1);
            if (Buffer.byteLength(Line, 'utf8') > MaxLineBytes) {
                this.Fail('Preview message exceeds the protocol size limit');
                return;
            }
            try { this.OnMessage(JSON.parse(Line)); }
            catch (Error) { this.Fail('Invalid preview message: ' + Error.message); return; }
        }
    }

    OnMessage(Message) {
        if (!Message || Message.version !== 1 || !Number.isSafeInteger(Message.generation) ||
            typeof Message.type !== 'string') throw new Error('Unsupported preview protocol envelope');
        if (!this.HelloSeen) {
            if (Message.type !== 'hello' || Message.generation !== 0)
                throw new Error('Preview host did not start with hello');
            this.Classes = ValidateSchema(Message.schema);
            this.HelloSeen = true;
            this.emit('schema', this.Classes);
            return;
        }
        if (Message.type === 'fullTree') {
            const NextGeneration = this.Generation === 0 ? 1 :
                this.ReloadPending ? this.Generation + 1 : this.Generation;
            if (Message.generation !== this.Generation && Message.generation !== NextGeneration)
                throw new Error('Unexpected preview generation');
            const Tree = ValidateTree(Message.nodes, this.Classes);
            const ChangedGeneration = Message.generation !== this.Generation;
            this.Generation = Message.generation;
            this.Tree = Tree;
            if (ChangedGeneration) {
                this.ReloadPending = false;
                if (this.ReloadAgain) {
                    this.ReloadAgain = false;
                    this.RequestReload();
                }
            }
            this.emit('tree', Tree, this.Generation, ChangedGeneration);
            return;
        }
        if (Message.generation !== this.Generation &&
            !(this.ReloadPending && Message.generation === this.Generation + 1) &&
            !(this.Generation === 0 && Message.generation === 1))
            throw new Error('Unexpected preview diagnostic generation');
        if (['diagnostic', 'consoleMessage', 'runtimeError'].includes(Message.type) &&
            typeof Message.message === 'string') {
            const Location = Message.location;
            if (Location !== undefined && Location !== null &&
                (!Location || typeof Location.source !== 'string' || Location.source.length > 1024 ||
                    !Number.isSafeInteger(Location.line) || Location.line <= 0))
                throw new Error('Invalid diagnostic source location');
            this.emit(Message.type, Message.message, Message.generation, Location || null);
            return;
        }
        throw new Error('Unknown preview message type');
    }

    Send(Type, Fields = {}) {
        if (this.Closed || !this.HelloSeen || this.Generation === 0 || !this.Process.stdin.writable) return false;
        this.Process.stdin.write(JSON.stringify({ version: 1, type: Type,
            generation: this.Generation, ...Fields }) + '\n');
        return true;
    }

    RequestReload() {
        if (this.ReloadPending) { this.ReloadAgain = true; return; }
        if (this.Send('reload')) this.ReloadPending = true;
    }

    Activate(Id) {
        if (Number.isSafeInteger(Id) && this.Tree.ById.has(Id)) this.Send('activate', { id: Id });
    }

    ResizeViewport(Id, Width, Height) {
        if (this.Tree.ById.get(Id)?.className === 'Window' &&
            Number.isFinite(Width) && Number.isFinite(Height) &&
            Width >= 0 && Height >= 0 && Width <= 100000 && Height <= 100000)
            this.Send('resizeViewport', { id: Id, width: Width, height: Height });
    }

    Fail(Message) {
        if (this.Closed) return;
        this.emit('failure', '[LUI:PreviewProtocol] ' + Message);
        this.Stop();
    }

    Stop() {
        if (this.Closed) return;
        this.Closed = true;
        if (this.Process.stdin.writable) this.Process.stdin.end('{"version":1,"type":"shutdown"}\n');
        this.KillTimer = setTimeout(() => { if (!this.Process.killed) this.Process.kill(); }, 1000);
        this.KillTimer.unref();
        this.Process.once('close', () => clearTimeout(this.KillTimer));
    }
}

module.exports = { PreviewClient };
