import { Message, Bytes, Limits, Classes } from './Protocol.mjs';
import { Descriptor, Digest, Read } from './Deployment.mjs';

const Scalars = (Text, Offset) => Array.from(Text.slice(0, Offset)).length;
const Utf16 = (Text, Offset) => Array.from(Text).slice(0, Offset).join('').length;
const KeyName = Key => Key === ' ' ? 'Space' : /^[a-z]$/i.test(Key) ? Key.toUpperCase() : Key;

export class BrowserHost {
    constructor(Container) {
        this.Container = Container; this.Nodes = new Map(); this.Assets = new Map(); this.Images = new Map();
        this.ImageQueue = []; this.ImageActive = 0; this.ImageCache = new Map(); this.ImageBytes = 0;
        this.Generation = Array.from(crypto.getRandomValues(new Uint8Array(16)), Value => Value.toString(16).padStart(2, '0')).join('');
        this.Sequence = 0; this.Incoming = 0; this.Revision = 0;
        this.State = 'Created'; this.Results = []; this.Frames = 0; this.Worker = null;
        this.Hold = false; this.Pending = null; this.FailNext = false; this.Waiter = null;
        this.Abort = new AbortController(); this.NodeBytes = 0; this.Events = []; this.EventBytes = 0;
        this.Awaiting = false; this.Frame = 0; this.Closed = false; this.Timers = new Set();
        this.Activations = new Map(); this.TerminalInputs = new Map(); this.Acknowledged = 0; this.AckSequence = 0; this.AckRevision = 0;
        this.InputEpoch = 0;
        this.InputGeometryEpoch = 0;
        this.FailureCode = 'None';
    }
    Later(Callback, Delay) {
        const Timer = setTimeout(() => { this.Timers.delete(Timer); Callback(); }, Delay);
        this.Timers.add(Timer); return Timer;
    }
    CancelTimer(Timer) { clearTimeout(Timer); this.Timers.delete(Timer); }
    Track(Outstanding, Kind, Fields) {
        if (Outstanding.size >= Limits.Events) { this.Fail(); return; }
        const Sequence = this.Send(Kind, Fields);
        if (Sequence) Outstanding.set(Sequence, this.Later(() => { if (Outstanding.has(Sequence)) this.Fail('OperationDeadline'); }, 5000));
    }
    Send(Kind, Fields = {}) {
        if (!this.Worker || this.Closed) return;
        if (this.Sequence >= Number.MAX_SAFE_INTEGER) { this.Fail(); return; }
        try { this.Worker.postMessage({ Version: 1, Generation: this.Generation, Sequence: ++this.Sequence, Kind, ...Fields }); return this.Sequence; }
        catch { this.Fail(); }
    }
    Request(Kind, Fields) {
        if (this.Waiter) throw new Error('OutstandingRequest');
        return new Promise((Resolve, Reject) => {
            this.Waiter = { Kind, Resolve, Reject, Sequence: this.Sequence + 1 };
            this.Send(Kind, Fields);
            const Sequence = this.Waiter?.Sequence;
            if (this.Waiter) this.Waiter.Timer = this.Later(() => { if (this.Waiter?.Sequence === Sequence) this.Fail(); }, 10000);
        });
    }
    Connect(WorkerURL = new URL('./Worker.mjs', import.meta.url)) {
        this.Worker = new Worker(WorkerURL, { type: 'module' });
        this.Worker.onerror = Event => { Event.preventDefault(); this.Fail('WorkerError'); };
        this.Worker.onmessageerror = () => this.Fail('WorkerMessage');
        this.Worker.onmessage = Event => {
            if (this.Closed) return;
            try {
                const Data = Message(Event.data, this.Generation, this.Incoming, 'Worker');
                this.Incoming = Data.Sequence;
                // In-flight results precede Retired on the worker's ordered channel.
                // Retirement stops projection/input admission, not message delivery.
                if (this.State === 'Retiring') {
                    if (Data.Kind === 'Retired' || Data.Kind === 'Fatal') { this.Clean(); this.State = 'Retired'; }
                    return;
                }
                if (Data.Kind === 'Ready') {
                    if (this.State !== 'Starting') throw new Error('Ready');
                    this.State = 'Running'; this.Observe();
                } else if (Data.Kind === 'Fatal') this.Fail('WorkerFatal');
                else if (Data.Kind === 'Result') {
                    this.Results.push({ Accepted: Data.Accepted, For: Data.For });
                    if (this.Results.length > 64) this.Results.shift();
                    if (this.Waiter && Data.For === this.Waiter.Kind && Data.ReplyTo === this.Waiter.Sequence) {
                        const Waiter = this.Waiter; this.Waiter = null;
                        this.CancelTimer(Waiter.Timer);
                        if (Data.Accepted) Waiter.Resolve(); else Waiter.Reject(new Error('Admission'));
                    }
                    if (Data.For === 'Input' && Data.ReplyTo === this.Awaiting) {
                        this.CancelTimer(this.InputTimer);
                        this.Awaiting = false; this.ActiveInput = null;
                        this.ScheduleEvents();
                    }
                    for (const [Kind, Outstanding] of [['Activate', this.Activations], ['Input', this.TerminalInputs]]) {
                        if (Data.For === Kind && Outstanding.has(Data.ReplyTo)) {
                            this.CancelTimer(Outstanding.get(Data.ReplyTo)); Outstanding.delete(Data.ReplyTo);
                            this.ScheduleEvents();
                        }
                    }
                    if (Data.For === 'Applied' && Data.Accepted && Data.ReplyTo === this.AckSequence) {
                        this.CancelTimer(this.PresentationTimer);
                        this.Acknowledged = this.AckRevision;
                        this.ScheduleEvents();
                    }
                } else if (Data.Kind === 'Retired') { this.Clean(); this.State = 'Retired'; }
                else if (Data.Kind === 'PresentBatch') {
                    if (this.State !== 'Running' || this.Pending || Data.Revision !== this.Revision + 1) throw new Error('Batch');
                    this.Pending = Data;
                    this.CancelTimer(this.PresentationTimer);
                    this.PresentationTimer = this.Later(() => { if (this.Pending || this.Acknowledged !== this.Revision) this.Fail('PresentationDeadline'); }, 5000);
                    // Acknowledgment means committed DOM state, not a physical paint.
                    // Compositor callbacks may stop even while a document is visible.
                    if (!this.Hold) this.ApplyPending();
                }
            } catch { this.Fail('Protocol'); }
        };
        this.Later(() => { if (!['Running', 'Retired'].includes(this.State) || (this.State === 'Running' && !this.Revision)) this.Fail(); }, 30000);
    }
    Start(Files) {
        if (this.State !== 'Created') throw new Error('[LUI:Browser] AlreadyStarted');
        this.State = 'Starting'; this.Connect();
        this.Send('Initialize', { Capture: JSON.stringify({ Files }) });
    }
    async Load(IndexURL) {
        if (this.State !== 'Created') throw new Error('[LUI:Browser] AlreadyStarted');
        this.State = 'Loading';
        this.Later(() => { if (this.State === 'Loading') this.Fail(); }, 30000);
        try {
            const Base = new URL('./', IndexURL);
            if (Base.origin !== location.origin || Base.search || Base.hash) throw new Error('Base');
            const Data = Descriptor(JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(await Read(IndexURL, 192 * 1024, this.Abort.signal))));
            // Sequential bounded reads; no Promise fan-out or runtime URL imports.
            for (const File of Data.Runtime) {
                const Input = await Read(new URL('Runtime/' + File.Path, Base), File.Bytes, this.Abort.signal);
                if (Input.byteLength !== File.Bytes || await Digest(Input) !== File.Sha256) throw new Error('RuntimeDigest');
            }
            this.State = 'Starting'; this.Connect(new URL('Runtime/Worker.mjs', Base));
            await this.Request('Begin', { Descriptor: JSON.stringify(Data) });
            for (const File of Data.Content) {
                const Input = await Read(new URL('Content/' + File.Path, Base), File.Bytes, this.Abort.signal);
                if (Input.byteLength !== File.Bytes || await Digest(Input) !== File.Sha256) throw new Error('ContentDigest');
                // Only image-sized files are retained on the page; sources live in the worker.
                if (Input.byteLength <= 1024 * 1024 && File.Path.toLowerCase().endsWith('.png')) this.Assets.set(File.Path, Input);
                for (let Offset = 0; Offset < Math.max(1, Input.byteLength); Offset += 65536) {
                    await this.Request('File', { Path: File.Path, Chunk: Input.slice(Offset, Offset + 65536), Final: Offset + 65536 >= Input.byteLength });
                }
            }
            if (this.Closed) throw new Error('Retired');
            this.Send('Start');
        } catch { if (!this.Closed) this.Fail(); throw new Error('[LUI:Browser] DeploymentFailed'); }
    }
    Observe() {
        const Options = { signal: this.Abort.signal };
        const Theme = () => this.Send('Theme', { Theme: matchMedia('(forced-colors: active)').matches ? 'HighContrast' : matchMedia('(prefers-color-scheme: dark)').matches ? 'Dark' : 'Light' });
        for (const Query of ['(forced-colors: active)', '(prefers-color-scheme: dark)']) matchMedia(Query).addEventListener('change', Theme, Options);
        Theme();
        this.Resize = new ResizeObserver(() => this.ReportViewport()); this.Resize.observe(this.Container);
        window.addEventListener('blur', () => this.Release(), Options);
        document.addEventListener('visibilitychange', () => { if (document.hidden) this.Release(); else this.ReportViewport(); }, Options);
        document.addEventListener('selectionchange', () => {
            const Control = document.activeElement, Node = this.Nodes.get(Number(Control?.dataset?.id));
            if (Node?.Class === 'TextBox' && Node.Control === Control && !Node.Composing) this.Edit(Node);
        }, { ...Options, capture: true });
        window.addEventListener('pagehide', () => this.Dispose(), Options);
    }
    ReportViewport() {
        const Root = [...this.Nodes.entries()].find(([, Node]) => Node.Class === 'Window');
        const Rect = this.Container.getBoundingClientRect();
        if (Root && Rect.width > 0 && Rect.height > 0) this.Send('Viewport', { Id: Root[0], Width: Rect.width, Height: Rect.height });
    }
    ApplyPending() {
        this.Frame = 0;
        if (!this.Pending || this.State !== 'Running') return;
        const Batch = this.Pending; this.Pending = null;
        try {
            if (this.FailNext) throw new Error('InjectedApplyFailure');
            for (const Op of Batch.Ops) this.Apply(Op);
            for (const Node of this.Nodes.values()) this.Geometry(Node);
            this.Revision = Batch.Revision; ++this.Frames;
            this.AckRevision = Batch.Revision;
            this.AckSequence = this.Send('Applied', { Revision: Batch.Revision, Success: true });
            this.ScheduleEvents();
        } catch {
            this.Send('Applied', { Revision: Batch.Revision, Success: false });
            this.Fail('Presentation');
        }
    }
    Listen(Node, Name, Callback, Extra = {}) {
        Node.Control.addEventListener(Name, Callback, { signal: Node.Abort.signal, ...Extra });
    }
    Create(Op) {
        if (this.Nodes.has(Op.Id) || this.Nodes.size >= Limits.Nodes || (Op.Class === 'Window' && [...this.Nodes.values()].some(Node => Node.Class === 'Window'))) throw new Error('Create');
        const Element = document.createElement(Op.Class === 'CheckBox' ? 'label' : Classes[Op.Class]);
        const Node = { Element, Control: Element, Class: Op.Class, Id: Op.Id, Parent: 0, Props: new Map(), Abort: new AbortController(), Bounds: null, Composing: false, Pointers: new Set(), Keys: new Set(), ContentEpoch: 0 };
        if (Op.Class === 'CheckBox') {
            Node.Control = document.createElement('input'); Node.Control.type = 'checkbox';
            Node.Label = document.createElement('span'); Element.append(Node.Control, Node.Label);
        }
        if (Op.Class === 'Slider') { Element.type = 'range'; Element.step = 'any'; }
        if (Op.Class === 'TextButton') Element.type = 'button';
        if (Op.Class === 'TextBox') { Element.type = 'text'; Element.autocomplete = 'off'; Element.spellcheck = false; }
        if (Op.Class === 'ImageLabel') Element.alt = '';
        Element.className = 'LuiNode'; Element.dataset.id = String(Op.Id);
        if (Op.Class === 'Window') { Element.setAttribute('role', 'region'); this.Container.append(Element); }
        this.Nodes.set(Op.Id, Node); this.Bind(Node);
    }
    Bind(Node) {
        const Id = Node.Id;
        this.Listen(Node, 'focus', () => this.Input(Id, { Kind: 'Focus', Value: true }));
        this.Listen(Node, 'blur', () => { this.Input(Id, { Kind: 'Focus', Value: false }); this.CancelNode(Node); });
        this.Listen(Node, 'pointerenter', () => this.Input(Id, { Kind: 'Hover', Value: true }));
        this.Listen(Node, 'pointerleave', () => this.Input(Id, { Kind: 'Hover', Value: false }));
        if (Node.Class === 'TextButton') this.Listen(Node, 'click', Event => { Event.stopPropagation(); this.Activate(Id); });
        if (Node.Class === 'CheckBox') this.Listen(Node, 'change', () => this.Input(Id, { Kind: 'Checked', Value: Node.Control.checked }));
        if (Node.Class === 'Slider') this.Listen(Node, 'input', () => this.Input(Id, { Kind: 'Value', Value: Number(Node.Control.value) }));
        this.Listen(Node, 'wheel', Event => {
            if (Node.Class === 'TextBox') return; // Native editing scroll is local to its text viewport.
            Event.preventDefault(); Event.stopPropagation();
            const Unit = Event.deltaMode === 1 ? 16 : Event.deltaMode === 2 ? this.Container.clientHeight : 1;
            this.Input(Id, { Kind: 'Scroll', X: Math.max(-1e6, Math.min(1e6, Event.deltaX * Unit)), Y: Math.max(-1e6, Math.min(1e6, Event.deltaY * Unit)) });
        }, { passive: false });
        for (const [Name, Phase] of [['pointerdown', 0], ['pointermove', 1], ['pointerup', 2], ['pointercancel', 3]]) {
            this.Listen(Node, Name, Event => {
                Event.stopPropagation();
                if (Phase === 0) { Node.Pointers.add(Event.pointerId); try { Node.Control.setPointerCapture(Event.pointerId); } catch {} }
                if (Phase >= 2) Node.Pointers.delete(Event.pointerId);
                const Rect = this.Container.getBoundingClientRect();
                this.Input(Id, { Kind: 'Pointer', Phase, Device: ({ mouse: 0, pen: 1, touch: 2 })[Event.pointerType] ?? 0, PointerId: Event.pointerId, X: Event.clientX - Rect.left, Y: Event.clientY - Rect.top });
            });
        }
        this.Listen(Node, 'lostpointercapture', Event => {
            if (Node.Pointers.delete(Event.pointerId)) this.Input(Id, { Kind: 'Pointer', Phase: 3, Device: 0, PointerId: Event.pointerId, X: 0, Y: 0 });
        });
        for (const Name of ['keydown', 'keyup']) this.Listen(Node, Name, Event => {
            Event.stopPropagation();
            const Key = KeyName(Event.key);
            if (!/^[A-Za-z0-9]{1,32}$/.test(Key)) return;
            if (Name === 'keydown') Node.Keys.add(Key); else Node.Keys.delete(Key);
            this.Input(Id, { Kind: 'Key', Phase: Name === 'keyup' ? 2 : Event.repeat ? 1 : 0, Key });
        });
        if (Node.Class === 'TextBox') {
            this.Listen(Node, 'input', Event => { if (!Event.isComposing && !Node.Composing) this.Edit(Node); });
            this.Listen(Node, 'select', () => { if (!Node.Composing) this.Edit(Node); });
            for (const [Name, Phase] of [['compositionstart', 0], ['compositionupdate', 1], ['compositionend', 2]]) this.Listen(Node, Name, Event => {
                Node.Composing = Phase !== 2;
                if (!Node.Secret) this.Input(Id, { Kind: 'Composition', Phase, Text: Phase === 2 ? '' : Event.data || '', InputRevision: Node.InputRevision || [0, 0] });
                if (Phase === 2) { this.Edit(Node); if (Node.DeferredText) { const State = Node.DeferredText; Node.DeferredText = null; this.TextState(Node, State); } }
            });
            for (const Name of ['copy', 'cut', 'dragstart']) this.Listen(Node, Name, Event => { if (Node.Secret) Event.preventDefault(); });
        }
    }
    Edit(Node) {
        if (!Node.InputRevision || Node.Applying) return;
        const Text = Node.Control.value.replace(/\r\n?/g, '\n');
        const Start = Node.Secret ? 0 : Scalars(Text, Node.Control.selectionStart || 0);
        const Length = Node.Secret ? 0 : Scalars(Text, Node.Control.selectionEnd || 0) - Start;
        if (Bytes(Text) > 65534 || Array.from(Text).length > Node.MaxLength) { this.TextState(Node, Node.AcceptedText); return; }
        const Pending = this.Events.findLast(Item => Item.Id === Node.Id && Item.Event?.Kind === 'Text' && this.CurrentTextObservation(Item, Node));
        const Active = this.ActiveInput?.Id === Node.Id && this.ActiveInput.Event?.Kind === 'Text' && this.CurrentTextObservation(this.ActiveInput, Node) ? this.ActiveInput : null;
        const Previous = Pending?.Event || Active?.Event || Node.AcceptedText;
        if (Previous && Text === Previous.Text && Start === Previous.Start && Length === Previous.Length) return;
        this.Input(Node.Id, { Kind: 'Text', Text, Start, Length, InputRevision: Node.InputRevision });
    }
    TextState(Node, Op) {
        if (!Op) return;
        // HTML has no multiline password control. Refuse before assigning text.
        if (Op.Secret && Op.Multiline) throw new Error('SecretMultiline');
        if (Node.Composing) { Node.DeferredText = Op; return; }
        const Tag = Op.Multiline ? 'TEXTAREA' : 'INPUT';
        if (Node.Control.tagName !== Tag) {
            const WasFocused = document.activeElement === Node.Control;
            Node.Abort.abort(); Node.Abort = new AbortController();
            const Element = document.createElement(Tag.toLowerCase());
            Element.className = Node.Element.className; Element.dataset.id = String(Node.Id);
            Element.style.cssText = Node.Element.style.cssText;
            Element.hidden = Node.Element.hidden; Element.inert = Node.Element.inert; Element.disabled = Node.Control.disabled;
            for (const Name of ['aria-label', 'aria-description']) if (Node.Element.hasAttribute(Name)) Element.setAttribute(Name, Node.Element.getAttribute(Name));
            Node.Element.replaceWith(Element); Node.Element = Node.Control = Element; this.Bind(Node);
            if (WasFocused) Element.focus();
        }
        Node.Applying = true;
        Node.Secret = Op.Secret; Node.MaxLength = Op.MaxLength; Node.InputRevision = Op.InputRevision; Node.AcceptedText = Op;
        if (!Op.Multiline) Node.Control.type = Op.Secret ? 'password' : 'text';
        Node.Control.placeholder = Op.Placeholder; Node.Control.autocomplete = 'off'; Node.Control.spellcheck = false;
        // A previous admitted edit must not overwrite newer local typing that
        // still observes the same canonical input revision and geometry.
        const PendingEdit = this.Events.some(Item => Item.Id === Node.Id && Item.Event?.Kind === 'Text' && this.CurrentTextObservation(Item, Node));
        if (!PendingEdit) {
            if (Node.Control.value !== Op.Text) Node.Control.value = Op.Text;
            if (!Op.Secret && document.activeElement === Node.Control) Node.Control.setSelectionRange(Utf16(Op.Text, Op.Start), Utf16(Op.Text, Op.Start + Op.Length));
        }
        Node.Applying = false;
    }
    Apply(Op) {
        // Preserve the observed view across focus/hover style-only frames. Any
        // canonical content, topology, geometry or input-state change invalidates
        // queued observations; this does not reinterpret application semantics.
        const Existing = this.Nodes.get(Op.Id);
        let Changed = Op.Kind === 'Create' || Op.Kind === 'Destroy';
        if (Op.Kind === 'Parent') Changed = Existing?.Parent !== Op.Parent;
        if (Op.Kind === 'Property') Changed = Op.Name !== '__Style' && Existing?.Props.get(Op.Name) !== Op.Value;
        if (Op.Kind === 'Arrange') Changed = JSON.stringify(Existing?.Bounds) !== JSON.stringify(Op);
        if (Op.Kind === 'Present') Changed = JSON.stringify(Existing?.Presentation) !== JSON.stringify(Op);
        if (Op.Kind === 'TextState') Changed = JSON.stringify(Existing?.DeferredText || Existing?.AcceptedText) !== JSON.stringify(Op);
        if (Changed && ++this.InputEpoch >= Number.MAX_SAFE_INTEGER) throw new Error('InputEpoch');
        if (Changed && Existing && ++Existing.ContentEpoch >= Number.MAX_SAFE_INTEGER) throw new Error('InputEpoch');
        if (Changed && ['Create', 'Destroy', 'Parent', 'Arrange', 'Present'].includes(Op.Kind) && ++this.InputGeometryEpoch >= Number.MAX_SAFE_INTEGER) throw new Error('InputEpoch');
        if (Op.Kind === 'Create') { this.Create(Op); return; }
        if (Op.Kind === 'CancelImage') { this.CancelImage(Op.Token); return; }
        const Node = this.Nodes.get(Op.Id);
        if (!Node) throw new Error('MissingNode');
        const Element = Node.Element;
        if (Op.Kind === 'Destroy') {
            this.CancelNode(Node); Node.Abort.abort(); Element.remove(); this.Nodes.delete(Op.Id);
            for (const Value of Node.Props.values()) this.NodeBytes -= Bytes(Value);
            for (const Image of this.Images.values()) if (Image.Id === Op.Id) this.CancelImage(Image.Token);
            this.ReleaseImage(Node);
        } else if (Op.Kind === 'Parent') {
            const Parent = Op.Parent ? this.Nodes.get(Op.Parent)?.Element : null;
            if (Op.Parent && !Parent) throw new Error('Parent');
            if (Parent) Parent.append(Element); else if (Node.Class !== 'Window') Element.remove();
            Node.Parent = Op.Parent;
        } else if (Op.Kind === 'Property') this.Property(Node, Op.Name, Op.Value);
        else if (Op.Kind === 'Arrange') Node.Bounds = Op;
        else if (Op.Kind === 'Present') {
            Node.Presentation = Op;
            Element.style.zIndex = String(Op.Rank); Element.style.pointerEvents = Op.Input ? 'auto' : 'none';
            Element.inert = !Op.Input;
            const Bounds = Node.Bounds;
            Element.style.clipPath = Bounds && (Op.Clip & 1) ? 'inset(' + [Math.max(0, Op.Y), Math.max(0, Bounds.Width - Op.X - Op.Width), Math.max(0, Bounds.Height - Op.Y - Op.Height), Math.max(0, Op.X)].map(Value => Value + 'px').join(' ') + ')' : '';
            if (!Op.Input && document.activeElement === Node.Control) Node.Control.blur();
        } else if (Op.Kind === 'TextState') this.TextState(Node, Op);
        else if (Op.Kind === 'Image') this.QueueImage(Op);
        else throw new Error('Operation');
    }
    Geometry(Node) {
        const Bounds = Node.Bounds; if (!Bounds) return;
        const Parent = this.Nodes.get(Node.Parent)?.Bounds;
        Node.Element.style.left = (Bounds.X - (Parent?.X || 0)) + 'px';
        Node.Element.style.top = (Bounds.Y - (Parent?.Y || 0)) + 'px';
        Node.Element.style.width = Math.max(0, Bounds.Width) + 'px'; Node.Element.style.height = Math.max(0, Bounds.Height) + 'px';
    }
    Property(Node, Name, Value) {
        this.NodeBytes += Bytes(Value) - Bytes(Node.Props.get(Name) || '');
        if (this.NodeBytes > 16 * 1024 * 1024) throw new Error('ProjectionLimit');
        Node.Props.set(Name, Value);
        const Element = Node.Element, Control = Node.Control;
        if (Name === 'Text' && Node.Class !== 'TextBox') {
            if (Node.Label) Node.Label.textContent = Value;
            else if (['TextButton', 'TextLabel'].includes(Node.Class)) Element.textContent = Value;
        } else if (Name === 'Source' && Node.Class === 'ImageLabel' && Value === '') {
            this.ReleaseImage(Node);
            for (const Record of this.Images.values()) if (Record.Id === Node.Id) this.CancelImage(Record.Token);
        } else if (Name === 'Visible') Element.hidden = Value !== 'true';
        else if (Name === 'Enabled') Control.disabled = Value !== 'true';
        else if (Name === 'Checked') Control.checked = Value === 'true';
        else if (['Minimum', 'Maximum', 'Value'].includes(Name)) {
            const NumberValue = Number(Value); if (!Number.isFinite(NumberValue)) throw new Error('Number');
            const Minimum = Number(Node.Props.get('Minimum') || 0), Maximum = Number(Node.Props.get('Maximum') || 100);
            const Current = Number(Node.Props.get('Value') || 0);
            if (Node.Class === 'Slider') { Control.min = Minimum; Control.max = Maximum; Control.value = Current; }
            else if (Node.Class === 'ProgressBar') { Control.max = 1; Control.value = Maximum > Minimum ? (Current - Minimum) / (Maximum - Minimum) : 0; }
        } else if (Name === 'AccessibilityLabel' || Name === 'Title') {
            const Label = Node.Props.get('AccessibilityLabel') || (Node.Class === 'Window' ? Node.Props.get('Title') : '');
            if (Label) Control.setAttribute('aria-label', Label); else Control.removeAttribute('aria-label');
        } else if (Name === 'AccessibilityDescription') {
            if (Value) Control.setAttribute('aria-description', Value); else Control.removeAttribute('aria-description');
        } else if (Name === '__Style') this.Style(Node, JSON.parse(Value));
        // Remaining fields are canonical data; TextState supplies atomic editing state.
    }
    Style(Node, Style) {
        const Css = Node.Element.style;
        const Color = (Value, Alpha = 1) => Value ? 'rgba(' + Value.map(Channel => Channel * 255).join(',') + ',' + Alpha + ')' : '';
        Css.backgroundColor = Style.HighContrast ? 'Canvas' : Color(Style.BackgroundColor3, 1 - Style.BackgroundTransparency);
        Css.color = Style.HighContrast ? 'CanvasText' : Color(Style.TextColor3);
        Css.borderColor = Style.HighContrast ? 'CanvasText' : Color(Style.BorderColor3);
        Css.borderStyle = 'solid'; Css.borderWidth = Style.BorderSizePixel + 'px'; Css.borderRadius = Style.CornerRadius + 'px';
        Css.opacity = Style.Opacity; Css.fontSize = Style.TextSize + 'px'; Css.fontFamily = Style.FontFamily === 'System' ? 'system-ui,sans-serif' : Style.FontFamily;
        Css.fontWeight = Style.FontWeight === 'Bold' ? 'bold' : 'normal';
        Css.whiteSpace = Style.TextWrapped ? 'pre-wrap' : 'pre'; Css.textAlign = Style.TextXAlignment.toLowerCase();
        // Text labels use a single text-only flex box so vertical alignment does not alter layout.
        if (Node.Class === 'TextLabel') {
            Css.display = 'flex'; Css.alignItems = { Top: 'flex-start', Center: 'center', Bottom: 'flex-end' }[Style.TextYAlignment];
            Css.justifyContent = { Left: 'flex-start', Center: 'center', Right: 'flex-end' }[Style.TextXAlignment];
        }
    }
    QueueImage(Op) {
        if (this.Images.size >= 32) throw new Error('Images');
        const Node = this.Nodes.get(Op.Id);
        if (Node) this.ReleaseImage(Node);
        const Key = Op.Token.join(':');
        if (this.Images.has(Key)) throw new Error('Token');
        const Record = { ...Op, Key }; this.Images.set(Key, Record); this.ImageQueue.push(Record); this.PumpImages();
    }
    PumpImages() {
        while (!this.Closed && this.ImageActive < 2 && this.ImageQueue.length) {
            const Record = this.ImageQueue.shift();
            if (!this.Images.has(Record.Key)) continue;
            ++this.ImageActive;
            this.DecodeImage(Record).catch(() => this.CompleteImage(Record, false)).finally(() => { --this.ImageActive; this.PumpImages(); });
        }
    }
    async DecodeImage(Record) {
        const Node = this.Nodes.get(Record.Id), Input = this.Assets.get(Record.Source);
        if (!Node || !Input || Input.byteLength < 33 || Input.byteLength > 1024 * 1024) throw new Error('Image');
        const Header = new DataView(Input.buffer, Input.byteOffset, Input.byteLength);
        if (Header.getUint32(0) !== 0x89504e47 || Header.getUint32(4) !== 0x0d0a1a0a || Header.getUint32(8) !== 13 ||
            Header.getUint32(12) !== 0x49484452 || ![1, 2, 4, 8].includes(Input[24])) throw new Error('PNG');
        const Width = Header.getUint32(16), Height = Header.getUint32(20), Size = Width * Height * 4;
        if (!Width || !Height || Width * Height > 1048576) throw new Error('Dimensions');
        let Cached = this.ImageCache.get(Record.Source);
        if (!Cached) {
            for (const [Key, Entry] of this.ImageCache) {
                if (this.ImageBytes + Size <= 32 * 1024 * 1024 && this.ImageCache.size < 16) break;
                if (!Entry.Users) { URL.revokeObjectURL(Entry.URL); this.ImageBytes -= Entry.Size; this.ImageCache.delete(Key); }
            }
            if (this.ImageCache.size >= 16 || this.ImageBytes + Size > 32 * 1024 * 1024) throw new Error('Cache');
            Cached = { URL: URL.createObjectURL(new Blob([Input], { type: 'image/png' })), Users: 0, Size };
            this.ImageCache.set(Record.Source, Cached); this.ImageBytes += Size;
        }
        ++Cached.Users;
        const Test = new Image(); Record.Test = Test;
        try {
            await new Promise((Resolve, Reject) => {
                Record.Cancel = Reject;
                Test.onload = Resolve; Test.onerror = Reject;
                Record.Timer = this.Later(Reject, 10000); Test.src = Cached.URL;
            });
            if (Test.naturalWidth !== Width || Test.naturalHeight !== Height) throw new Error('Dimensions');
            if (!this.Images.has(Record.Key) || this.Closed || this.Nodes.get(Record.Id) !== Node) return;
            this.ReleaseImage(Node); Node.Image = Cached; ++Cached.Users;
            Node.Element.src = Cached.URL;
            this.CompleteImage(Record, true);
        } finally {
            --Cached.Users; Test.onload = Test.onerror = null; Test.removeAttribute('src');
            if (Record.Timer) { clearTimeout(Record.Timer); this.Timers.delete(Record.Timer); }
        }
    }
    CompleteImage(Record, Success) {
        if (!this.Images.delete(Record.Key) || this.Closed) return;
        this.Send('Input', { Id: Record.Id, Revision: this.Revision || 1, Event: { Kind: 'Image', Token: Record.Token, Success } });
    }
    CancelImage(Token) {
        const Key = Token.join(':'), Record = this.Images.get(Key);
        if (Record) { this.Images.delete(Key); Record.Cancel?.(); }
    }
    ReleaseImage(Node) { if (Node.Image) { --Node.Image.Users; Node.Image = null; Node.Element.removeAttribute('src'); } }
    Input(Id, Event) {
        if (this.State !== 'Running' || this.Closed) return;
        // Cancellation has reserved admission while normal input waits for a frame.
        // These are exactly the terminal forms accepted by the shared bridge.
        const Terminal = (Event.Kind === 'Focus' && !Event.Value) || (Event.Kind === 'Key' && Event.Phase === 2) || (Event.Kind === 'Pointer' && Event.Phase === 3);
        const Related = this.Events.some(Item => Item.Id === Id);
        if (Terminal && !Related) {
            this.Track(this.TerminalInputs, 'Input', { Id, Revision: this.Revision, Event }); return;
        }
        this.Enqueue({ Kind: 'Input', Id, Event, Epoch: this.InputEpoch, GeometryEpoch: this.InputGeometryEpoch, Terminal });
    }
    CurrentTextObservation(Item, Node) {
        return ['Text', 'Composition'].includes(Item.Event?.Kind) && Item.GeometryEpoch === this.InputGeometryEpoch &&
            Node.InputRevision && Item.Event.InputRevision[0] === Node.InputRevision[0] && Item.Event.InputRevision[1] === Node.InputRevision[1];
    }
    CurrentActivationObservation(Item, Node) {
        return Item.Kind === 'Activate' && Item.GeometryEpoch === this.InputGeometryEpoch && Item.ContentEpoch === Node.ContentEpoch;
    }
    Enqueue(Item) {
        const Size = Bytes(Item);
        if (this.Events.length >= Limits.Events || Size > Limits.Queue - this.EventBytes) { this.Fail(); return; }
        this.Events.push(Item); this.EventBytes += Size; this.ScheduleEvents();
    }
    ScheduleEvents() {
        if (this.EventFrame || this.Closed || this.Awaiting || this.Activations.size || this.Pending || !this.Events.length || this.State !== 'Running' || this.Acknowledged !== this.Revision) return;
        this.EventFrame = 1;
        queueMicrotask(() => {
            this.EventFrame = 0;
            if (this.Awaiting || this.Activations.size || this.Pending || !this.Events.length || this.State !== 'Running' || this.Acknowledged !== this.Revision) return;
            const Item = this.Events.shift(); this.EventBytes -= Bytes(Item);
            const Node = this.Nodes.get(Item.Id);
            if (!Node || (!Item.Terminal && Item.Epoch !== this.InputEpoch && !this.CurrentTextObservation(Item, Node) && !this.CurrentActivationObservation(Item, Node))) {
                // A stale release still retires its accepted press by cancellation.
                if (Node && Item.Event?.Kind === 'Pointer' && Item.Event.Phase === 2)
                    this.Track(this.TerminalInputs, 'Input', { Id: Item.Id, Revision: this.Revision, Event: { ...Item.Event, Phase: 3 } });
                if (Item.Event?.Kind === 'Text' && Node?.AcceptedText) this.TextState(Node, Node.AcceptedText);
                this.ScheduleEvents(); return;
            }
            if (Item.Kind === 'Activate') {
                this.Track(this.Activations, 'Activate', { Id: Item.Id, Revision: this.Revision }); return;
            }
            this.ActiveInput = Item;
            this.Awaiting = this.Send('Input', { Id: Item.Id, Revision: this.Revision, Event: Item.Event });
            if (!this.Awaiting) { this.ActiveInput = null; return; }
            const Sequence = this.Awaiting;
            this.InputTimer = this.Later(() => { if (this.Awaiting === Sequence) this.Fail('InputDeadline'); }, 5000);
        });
    }
    Activate(Id, Revision) {
        if (this.State !== 'Running') return;
        if (Revision !== undefined) this.Track(this.Activations, 'Activate', { Id, Revision });
        else this.Enqueue({ Kind: 'Activate', Id, Epoch: this.InputEpoch, GeometryEpoch: this.InputGeometryEpoch, ContentEpoch: this.Nodes.get(Id)?.ContentEpoch });
    }
    CancelNode(Node) {
        for (const PointerId of Node.Pointers) {
            this.Input(Node.Id, { Kind: 'Pointer', Phase: 3, Device: 0, PointerId, X: 0, Y: 0 });
            try { Node.Control.releasePointerCapture(PointerId); } catch {}
        }
        for (const Key of Node.Keys) this.Input(Node.Id, { Kind: 'Key', Phase: 2, Key });
        Node.Pointers.clear(); Node.Keys.clear();
        if (Node.Composing) {
            Node.Composing = false;
            if (!Node.Secret) this.Input(Node.Id, { Kind: 'Composition', Phase: 2, Text: '', InputRevision: Node.InputRevision });
        }
    }
    Release() { for (const Node of this.Nodes.values()) { this.CancelNode(Node); if (document.activeElement === Node.Control) Node.Control.blur(); } }
    Retire() {
        if (this.Closed) return;
        if (this.Worker) {
            this.State = 'Retiring';
            for (const Timer of this.Timers) clearTimeout(Timer);
            this.Timers.clear(); this.EventFrame = 0;
            this.Send('Retire'); this.Later(() => { if (!this.Closed) { this.Clean(); this.State = 'Retired'; } }, 1000);
        }
        else { this.Clean(); this.State = 'Retired'; }
    }
    Dispose() { this.Clean(); this.State = 'Retired'; }
    Clean() {
        if (this.Closed) return;
        this.Closed = true; this.Abort.abort(); this.Resize?.disconnect();
        for (const Timer of this.Timers) clearTimeout(Timer);
        this.Timers.clear(); this.EventFrame = 0; this.Frame = 0;
        this.Worker?.terminate(); this.Worker = null;
        this.Waiter?.Reject(new Error('Retired')); this.Waiter = null;
        for (const Node of this.Nodes.values()) Node.Abort.abort();
        for (const Record of this.Images.values()) Record.Cancel?.();
        for (const Entry of this.ImageCache.values()) URL.revokeObjectURL(Entry.URL);
        this.ImageCache.clear(); this.ImageBytes = 0; this.Images.clear(); this.ImageQueue = []; this.Assets.clear();
        this.Activations.clear(); this.TerminalInputs.clear(); this.Awaiting = false; this.ActiveInput = null;
        this.Pending = null; this.Events = []; this.EventBytes = 0; this.Nodes.clear(); this.NodeBytes = 0; this.Container.replaceChildren();
    }
    Fail(Code = 'BrowserFailure') {
        if (this.Closed) return;
        this.FailureCode = ['BrowserFailure', 'Protocol', 'Presentation', 'WorkerError', 'WorkerMessage', 'WorkerFatal', 'OperationDeadline', 'PresentationDeadline', 'InputDeadline'].includes(Code) ? Code : 'BrowserFailure';
        const Retiring = this.State === 'Retiring';
        this.Clean(); this.State = 'Failed';
        if (Retiring) { this.State = 'Retired'; return; }
        const Status = document.createElement('p'); Status.setAttribute('role', 'status');
        Status.textContent = 'The application could not continue.'; this.Container.replaceChildren(Status);
    }
}
// Compatibility for the retained 7A harness.
export const BrowserProbe = BrowserHost;
