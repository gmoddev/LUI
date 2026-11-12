'use strict';

const { EventEmitter } = require('node:events');
const { spawn } = require('node:child_process');
const { ValidateSchema, ValidateTree } = require('./PreviewModel');
const {SourceMap}=require('./ProjectProtocol');
const { ReadManifest } = require('./ProjectSetup');

const MaxLineBytes = 4 * 1024 * 1024 + 64 * 1024;

class PreviewClient extends EventEmitter {
    constructor(ManifestPath, HostPath, RuntimePath) {
        super();
        this.Generation = 0;
        this.Classes = new Map();
        this.Tree = { Nodes: [], ById: new Map(), Children: new Map([[0, []]]) };
        this.HelloSeen = false;
        this.InputSupported = false;
        this.ReloadPending = false;
        this.ReloadAgain = false;
        this.Closed = false;
        this.Buffer = '';
        this.Root=require('node:path').dirname(ManifestPath);
        this.Version=ReadManifest(ManifestPath).SchemaVersion;
        this.Sources=new Map();
        const IsDll = HostPath.toLowerCase().endsWith('.dll');
        this.Process = spawn(IsDll ? 'dotnet' : HostPath,
            [...(IsDll ? [HostPath] : []), this.Version===2?'--project':'--manifest', ManifestPath, '--runtime', RuntimePath],
            { cwd: require('node:path').dirname(HostPath), windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
        this.Process.stdout.setEncoding('utf8');
        this.Process.stderr.setEncoding('utf8');
        this.Process.stdout.on('data', Chunk => this.OnData(Chunk));
        this.Process.stderr.on('data', Chunk => this.emit('diagnostic', this.Version===2?'[LUI:Preview] HostFailed':String(Chunk).trim()));
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
        if (!Message || Message.version !== (this.Version||1) || !Number.isSafeInteger(Message.generation) ||
            typeof Message.type !== 'string') throw new Error('Unsupported preview protocol envelope');
        if (!this.HelloSeen) {
            if (Message.type !== 'hello' || Message.generation !== 0)
                throw new Error('Preview host did not start with hello');
            if(this.Version===2 && (Message.redacted!==true || !['Development','Restricted'].includes(Message.diagnosticsMode)))
                throw new Error('Invalid diagnostic policy');
            this.Classes = ValidateSchema(Message.schema);
            if (Message.inputCommands !== undefined && (!Array.isArray(Message.inputCommands) ||
                Message.inputCommands.length > 32 || Message.inputCommands.some(Value => typeof Value !== 'string' || Value.length > 64)))
                throw new Error('Invalid input capabilities');
            this.InputSupported = ['focus', 'pointerInput', 'keyInput', 'resetInput']
                .every(Name => Message.inputCommands?.includes(Name));
            this.HelloSeen = true;
            this.emit('schema', this.Classes);
            return;
        }
        if(Message.type==='project' && this.Version===2) {
            if(Message.redacted!==true || !['Development','Restricted'].includes(Message.diagnosticsMode) || !/^[a-f0-9]{64}$/.test(Message.digest) ||
                (Message.generation!==this.Generation+1 && Message.generation!==this.Generation)) throw new Error('Invalid prepared project transaction');
            if(Message.diagnosticsMode==='Restricted' && Message.sources.length!==0) throw new Error('Restricted source mapping');
            this.Sources=SourceMap(Message.sources,this.Root); this.ProjectDigest=Message.digest; this.emit('project',Message); return;
        }
        if (Message.type === 'fullTree') {
            if(this.Version===2 && Message.redacted!==true) throw new Error('Unprotected project tree');
            const NextGeneration = this.Generation === 0 ? 1 :
                this.ReloadPending ? this.Generation + 1 : this.Generation;
            if (Message.generation !== this.Generation && Message.generation !== NextGeneration)
                throw new Error('Unexpected preview generation');
            const Tree = ValidateTree(Message.nodes, this.Classes);
            const ChangedGeneration = Message.generation !== this.Generation;
            if(this.Version===2 && Tree.Nodes.length===0) this.Sources=new Map();
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
            if(this.Version===2 && Location && !this.Sources.has(Location.source)) throw new Error('Unknown project diagnostic source');
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
        this.Process.stdin.write(JSON.stringify({ version: this.Version||1, type: Type,
            generation: this.Generation, ...Fields }) + '\n');
        return true;
    }

    RequestReload() {
        if (this.ReloadPending) { this.ReloadAgain = true; return; }
        if (this.Send('reload')) {this.ReloadPending = true; if(this.Version===2) this.Sources=new Map();}
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

    InputTarget(Id) {
        if (!this.InputSupported || this.ReloadPending || !Number.isSafeInteger(Id)) return false;
        let Class = this.Classes.get(this.Tree.ById.get(Id)?.className);
        for (let Depth = 0; Class && Depth < this.Classes.size; ++Depth) {
            if (Class.name === 'GuiObject') return true;
            Class = this.Classes.get(Class.base);
        }
        return false;
    }

    Focus(Id, Focused) {
        return this.InputTarget(Id) && typeof Focused === 'boolean' &&
            this.Send('focus', { id: Id, focused: Focused });
    }

    PointerInput(Id, Phase, Device, PointerId, X, Y) {
        return this.InputTarget(Id) && ['Began', 'Changed', 'Ended', 'Canceled'].includes(Phase) &&
            Number.isInteger(Device) && Device >= 0 && Device <= 3 &&
            Number.isInteger(PointerId) && PointerId >= 0 && PointerId <= 31 &&
            Number.isFinite(X) && Number.isFinite(Y) && Math.abs(X) <= 100000 && Math.abs(Y) <= 100000 &&
            this.Send('pointerInput', { id: Id, phase: Phase, device: Device, pointerId: PointerId, x: X, y: Y });
    }

    KeyInput(Id, Phase, Key) {
        return this.InputTarget(Id) && ['Began', 'Changed', 'Ended'].includes(Phase) &&
            typeof Key === 'string' && Key.length > 0 && Key.length <= 32 && !Key.includes('\0') &&
            this.Send('keyInput', { id: Id, phase: Phase, key: Key });
    }

    ResetInput() {
        return this.InputSupported && !this.ReloadPending && this.Send('resetInput');
    }

    Fail(Message) {
        if (this.Closed) return;
        this.emit('failure', '[LUI:PreviewProtocol] ' + (this.Version===2?'OperationFailed':Message));
        this.Stop();
    }

    Stop() {
        if (this.Closed) return;
        this.Closed = true;
        if (this.Process.stdin.writable) this.Process.stdin.end(JSON.stringify({version:this.Version||1,type:'shutdown'})+'\n');
        this.KillTimer = setTimeout(() => { if (!this.Process.killed) this.Process.kill(); }, 1000);
        this.KillTimer.unref();
        this.Process.once('close', () => clearTimeout(this.KillTimer));
    }
}

module.exports = { PreviewClient };
