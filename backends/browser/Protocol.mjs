// Private BrowserHostV1 wire contract. No application-defined message fields.
export const Limits = Object.freeze({ Message: 256 * 1024, Queue: 1024 * 1024, Events: 256, Nodes: 4096, Files: 256, File: 8 * 1024 * 1024, Capture: 32 * 1024 * 1024 });
export const Classes = Object.freeze({ Window: 'section', Frame: 'div', ScrollingFrame: 'div', TextLabel: 'span', TextButton: 'button', CheckBox: 'input', Slider: 'input', ProgressBar: 'progress', TextBox: 'input', ImageLabel: 'img' });
const Encoder = new TextEncoder();
export const Bytes = Value => {
    if (typeof Value === 'string') return Encoder.encode(Value).byteLength;
    let Binary = 0;
    const Text = JSON.stringify(Value, (_Name, Field) => {
        if (Field instanceof Uint8Array) { Binary += Field.byteLength; return null; }
        return Field;
    });
    return Encoder.encode(Text).byteLength + Binary;
};
export const Integer = (Value, Minimum = 0, Maximum = 0x7fffffff) => Number.isSafeInteger(Value) && Value >= Minimum && Value <= Maximum;
export function Shape(Value, Fields) {
    return Value !== null && typeof Value === 'object' && !Array.isArray(Value) &&
        Object.keys(Value).sort().join('|') === [...Fields].sort().join('|');
}
const Schemas = {
    Initialize: ['Capture'], Begin: ['Descriptor'], File: ['Path', 'Chunk', 'Final'], Start: [],
    Applied: ['Revision', 'Success'], Activate: ['Id', 'Revision'], Input: ['Id', 'Revision', 'Event'],
    Viewport: ['Id', 'Width', 'Height'], Theme: ['Theme'], Retire: [],
    Ready: [], Fatal: ['Code'], Retired: [], Result: ['Accepted', 'For', 'ReplyTo'],
    PresentBatch: ['Revision', 'Ops'],
};
export function Message(Data, Generation, Sequence, Direction) {
    const Fields = Schemas[Data?.Kind];
    const Allowed = Direction === 'Worker' ? ['Ready', 'Fatal', 'Retired', 'Result', 'PresentBatch'] :
        ['Initialize', 'Begin', 'File', 'Start', 'Applied', 'Activate', 'Input', 'Viewport', 'Theme', 'Retire'];
    if (!Fields || !Allowed.includes(Data.Kind) || !Shape(Data, ['Version', 'Generation', 'Sequence', 'Kind', ...Fields]) ||
        Data.Version !== 1 || !/^[0-9a-f]{32}$/.test(Data.Generation) ||
        (Generation && Data.Generation !== Generation) || Data.Sequence !== Sequence + 1 ||
        !Integer(Data.Sequence, 1, Number.MAX_SAFE_INTEGER) || Bytes(Data) > Limits.Message) throw new Error('Protocol');
    if (['Applied', 'Activate', 'Input', 'PresentBatch'].includes(Data.Kind) && !Integer(Data.Revision, 1, 0xffffffff)) throw new Error('Revision');
    if (['Activate', 'Input', 'Viewport'].includes(Data.Kind) && !Integer(Data.Id, 1)) throw new Error('Node');
    if (Data.Kind === 'Applied' && typeof Data.Success !== 'boolean') throw new Error('Ack');
    if (Data.Kind === 'Result' && (![0, 1].includes(Data.Accepted) || !Integer(Data.ReplyTo, 1, Number.MAX_SAFE_INTEGER) || !['Applied', 'Activate', 'Input', 'File', 'Begin'].includes(Data.For))) throw new Error('Result');
    if (Data.Kind === 'Fatal' && Data.Code !== 'BrowserFailure') throw new Error('Fatal');
    if (Data.Kind === 'Theme' && !['Light', 'Dark', 'HighContrast'].includes(Data.Theme)) throw new Error('Theme');
    if (Data.Kind === 'Viewport' && !['Width', 'Height'].every(Name => Number.isFinite(Data[Name]) && Data[Name] > 0 && Data[Name] <= 1e7)) throw new Error('Viewport');
    if (Data.Kind === 'Initialize' && typeof Data.Capture !== 'string') throw new Error('Capture');
    if (Data.Kind === 'Begin' && typeof Data.Descriptor !== 'string') throw new Error('Descriptor');
    if (Data.Kind === 'File' && (typeof Data.Path !== 'string' || Data.Path.length > 512 || !(Data.Chunk instanceof Uint8Array) ||
        Data.Chunk.byteLength > 65536 || typeof Data.Final !== 'boolean')) throw new Error('File');
    if (Data.Kind === 'File') Data.Chunk.subarray(0, 0); // Detached buffers throw; genuine empty files remain valid.
    if (Data.Kind === 'Input') Input(Data.Event);
    if (Data.Kind === 'PresentBatch') {
        if (!Array.isArray(Data.Ops) || Data.Ops.length > 4096) throw new Error('Batch');
        for (const Op of Data.Ops) Operation(Op);
    }
    return Data;
}
const EventFields = {
    Focus: ['Value'], Hover: ['Value'], Checked: ['Value'], Value: ['Value'],
    Scroll: ['X', 'Y'], Pointer: ['Phase', 'Device', 'PointerId', 'X', 'Y'], Key: ['Phase', 'Key'],
    Text: ['Text', 'Start', 'Length', 'InputRevision'], Composition: ['Phase', 'Text', 'InputRevision'], Image: ['Token', 'Success'],
};
export function Input(Event) {
    const Fields = EventFields[Event?.Kind];
    if (!Fields || !Shape(Event, ['Kind', ...Fields])) throw new Error('Input');
    if (['Focus', 'Hover', 'Checked'].includes(Event.Kind) && typeof Event.Value !== 'boolean') throw new Error('Boolean');
    for (const Name of ['X', 'Y', 'Value']) if (Object.hasOwn(Event, Name) && typeof Event[Name] !== 'boolean' &&
        (!Number.isFinite(Event[Name]) || Math.abs(Event[Name]) > 1e7)) throw new Error('Number');
    for (const Name of ['Phase', 'Device', 'PointerId', 'Start', 'Length']) if (Object.hasOwn(Event, Name) && !Integer(Event[Name])) throw new Error('Integer');
    if (Object.hasOwn(Event, 'Text') && (typeof Event.Text !== 'string' || Bytes(Event.Text) > (Event.Kind === 'Composition' ? 4096 : 65534) || Event.Text.includes('\0'))) throw new Error('Text');
    if (typeof Event.Text === 'string' && Array.from(Event.Text).some(Character => Character.length === 1 && Character.charCodeAt(0) >= 0xd800 && Character.charCodeAt(0) <= 0xdfff)) throw new Error('Surrogate');
    if (Event.Kind === 'Key' && (typeof Event.Key !== 'string' || !/^[A-Za-z0-9]{1,32}$/.test(Event.Key) || Event.Phase > 2)) throw new Error('Key');
    if (Event.Kind === 'Pointer' && (Event.Phase > 3 || Event.Device > 3)) throw new Error('Pointer');
    if (Event.Kind === 'Composition' && Event.Phase > 2) throw new Error('Composition');
    for (const Name of ['Token', 'InputRevision']) if (Object.hasOwn(Event, Name) &&
        (!Array.isArray(Event[Name]) || Event[Name].length !== 2 || !Event[Name].every(Word => Integer(Word, 0, 0xffffffff)))) throw new Error('U64');
    if (Event.Kind === 'Image' && typeof Event.Success !== 'boolean') throw new Error('Image');
}
const OpFields = {
    Create: ['Class'], Destroy: [], Parent: ['Parent'], Property: ['Name', 'Value'],
    Arrange: ['X', 'Y', 'Width', 'Height'], Present: ['Clip', 'X', 'Y', 'Width', 'Height', 'Rank', 'Input'],
    TextState: ['Text', 'Start', 'Length', 'InputRevision', 'Multiline', 'Secret', 'MaxLength', 'Placeholder'],
    Image: ['Token', 'Source'], CancelImage: ['Token'],
};
export function Operation(Op) {
    const Fields = OpFields[Op?.Kind];
    if (!Fields || !Shape(Op, ['Kind', 'Id', ...Fields]) || !Integer(Op.Id, 1)) throw new Error('Operation');
    if (Op.Kind === 'Create' && !Object.hasOwn(Classes, Op.Class)) throw new Error('Class');
    if (Op.Kind === 'Parent' && !Integer(Op.Parent)) throw new Error('Parent');
    if (Op.Kind === 'Property' && (typeof Op.Name !== 'string' || Op.Name.length > 64 || typeof Op.Value !== 'string' || Bytes(Op.Value) > 65534)) throw new Error('Property');
    for (const Name of ['X', 'Y', 'Width', 'Height']) if (Object.hasOwn(Op, Name) && (!Number.isFinite(Op[Name]) || Math.abs(Op[Name]) > 1e7)) throw new Error('Geometry');
    if (Op.Kind === 'Present' && (!Integer(Op.Clip, 0, 3) || !Integer(Op.Rank) || ![0, 1].includes(Op.Input))) throw new Error('Presentation');
    if (Op.Kind === 'TextState') {
        Input({ Kind: 'Text', Text: Op.Text, Start: Op.Start, Length: Op.Length, InputRevision: Op.InputRevision });
        if (typeof Op.Multiline !== 'boolean' || typeof Op.Secret !== 'boolean' || !Integer(Op.MaxLength, 0, 65534) || typeof Op.Placeholder !== 'string') throw new Error('TextState');
    }
    if (['Image', 'CancelImage'].includes(Op.Kind)) Input({ Kind: 'Image', Token: Op.Token, Success: false });
    if (Op.Kind === 'Image' && (typeof Op.Source !== 'string' || Op.Source.length > 128)) throw new Error('Source');
}
