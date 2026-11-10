import { createHash as CreateHash } from 'node:crypto';

export function Hash(Text) { return CreateHash('sha256').update(Buffer.isBuffer(Text) ? Text : Text.replaceAll('\r\n', '\n')).digest('hex'); }
export function Declarations(Text) {
  const Starts = [...Text.matchAll(/^(?:export type|type|declare) [A-Za-z][A-Za-z0-9_]*/gm)];
  const Exports = new Map(); const Globals = new Map();
  for (let Index = 0; Index < Starts.length; Index++) {
    const Block = Text.slice(Starts[Index].index, Starts[Index + 1]?.index ?? Text.length).trim();
    const Export = /^export type (\w+)/.exec(Block); const Global = /^declare (\w+):/.exec(Block);
    if (Export) Exports.set(Export[1], Block);
    if (Global) Globals.set(Global[1], Block);
  }
  return { Exports, Globals };
}
export function Validate(Schema, Definitions, Release, Editorial) {
  if (Schema.schemaVersion !== 2) throw new Error('[LUI:Docs] Unsupported reflection schema');
  if (Hash(Definitions) !== Release.DefinitionsSha256) throw new Error('[LUI:Docs] Definitions differ from selected SDK');
  const Symbols = new Set(); const Classes = new Map(Schema.classes.map(Item => [Item.name, Item]));
  for (const Group of ['classes', 'services', 'objects']) for (const Item of Schema[Group]) {
    if (Symbols.has(Item.name)) throw new Error('[LUI:Docs] Duplicate symbol: ' + Item.name);
    Symbols.add(Item.name);
    if (!Editorial[Item.name]) throw new Error('[LUI:Docs] Missing explanation: ' + Item.name);
    if (Group === 'classes') {
      const Seen = new Set([Item.name]); let Base = Item.base;
      while (Base) { if (!Classes.has(Base) || Seen.has(Base)) throw new Error('[LUI:Docs] Invalid inheritance: ' + Item.name); Seen.add(Base); Base = Classes.get(Base).base; }
    }
    const Members = new Set();
    for (const Member of [...Item.properties, ...Item.methods, ...Item.signals]) {
      if (Members.has(Member.name)) throw new Error('[LUI:Docs] Duplicate member: ' + Item.name + '.' + Member.name);
      Members.add(Member.name);
    }
  }
  for (const Name of Object.keys(Editorial)) if (!Symbols.has(Name)) throw new Error('[LUI:Docs] Unknown editorial symbol: ' + Name);
  const Parsed = Declarations(Definitions);
  for (const Name of Symbols) if (!Parsed.Exports.has(Name)) throw new Error('[LUI:Docs] Missing public declaration: ' + Name);
  for (const Item of Schema.globals) if (!Parsed.Globals.has(Item.name)) throw new Error('[LUI:Docs] Missing global declaration: ' + Item.name);
  return Parsed;
}
export function MakeZip(Files) {
  const Entries = []; const Central = []; let Offset = 0;
  for (const [Name, Content] of [...Files].sort(([Left], [Right]) => Left.localeCompare(Right, 'en'))) {
    if (!/^[A-Za-z0-9_./-]+$/.test(Name) || Name.split('/').some(Part => Part === '..' || !Part)) throw new Error('[LUI:Docs] Invalid archive path');
    const Data = Buffer.from(Content); const Path = Buffer.from(Name); let Crc = 0xffffffff;
    for (const Byte of Data) { Crc ^= Byte; for (let Bit = 0; Bit < 8; Bit++) Crc = (Crc >>> 1) ^ ((Crc & 1) ? 0xedb88320 : 0); } Crc = (Crc ^ 0xffffffff) >>> 0;
    const Header = Buffer.alloc(30); Header.writeUInt32LE(0x04034b50); Header.writeUInt16LE(20,4); Header.writeUInt16LE(33,12); Header.writeUInt32LE(Crc,14); Header.writeUInt32LE(Data.length,18); Header.writeUInt32LE(Data.length,22); Header.writeUInt16LE(Path.length,26);
    const Directory = Buffer.alloc(46); Directory.writeUInt32LE(0x02014b50); Directory.writeUInt16LE(20,4); Directory.writeUInt16LE(20,6); Directory.writeUInt16LE(33,14); Directory.writeUInt32LE(Crc,16); Directory.writeUInt32LE(Data.length,20); Directory.writeUInt32LE(Data.length,24); Directory.writeUInt16LE(Path.length,28); Directory.writeUInt32LE(Offset,42);
    Entries.push(Header, Path, Data); Central.push(Directory, Path); Offset += Header.length + Path.length + Data.length;
  }
  const Directory = Buffer.concat(Central); const End = Buffer.alloc(22); End.writeUInt32LE(0x06054b50); End.writeUInt16LE(Files.size,8); End.writeUInt16LE(Files.size,10); End.writeUInt32LE(Directory.length,12); End.writeUInt32LE(Offset,16);
  return Buffer.concat([...Entries, Directory, End]);
}
