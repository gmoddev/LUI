'use strict';

const MaxNodes = 8192;
const SnapshotFields = Object.freeze({
    ClassName: 'className', Name: 'name', Parent: 'parentId', Title: 'title',
    Text: 'text', Source: 'source', AccessibilityLabel: 'accessibilityLabel',
    AccessibilityDescription: 'accessibilityDescription', Visible: 'visible',
    Enabled: 'enabled', Checked: 'checked', IsFocused: 'isFocused',
    Minimum: 'minimum', Maximum: 'maximum', Value: 'value',
});

function Require(Condition, Message) {
    if (!Condition) throw new Error('[LUI:PreviewProtocol] ' + Message);
}

function ValidateSchema(Schema) {
    Require(Schema && typeof Schema === 'object' && Number.isInteger(Schema.schemaVersion) &&
        Schema.schemaVersion >= 1 && Array.isArray(Schema.classes), 'invalid reflection schema');
    const Classes = new Map();
    for (const Class of Schema.classes) {
        Require(Class && typeof Class.name === 'string' && typeof Class.base === 'string' &&
            Array.isArray(Class.properties) && !Classes.has(Class.name), 'invalid class metadata');
        Classes.set(Class.name, Class);
    }
    return Classes;
}

function ValidateTree(Nodes, Classes) {
    Require(Array.isArray(Nodes) && Nodes.length <= MaxNodes, 'invalid or oversized tree');
    const ById = new Map();
    const Children = new Map([[0, []]]);
    let PreviousId = 0;
    for (const Node of Nodes) {
        Require(Node && Number.isSafeInteger(Node.id) && Node.id > PreviousId &&
            Number.isSafeInteger(Node.parentId) && Node.parentId >= 0 &&
            Classes.has(Node.className), 'invalid node identity or class');
        for (const Field of ['name', 'title', 'text', 'source', 'accessibilityLabel', 'accessibilityDescription'])
            Require(typeof Node[Field] === 'string', 'invalid ' + Field);
        for (const Field of ['visible', 'enabled', 'checked', 'isFocused'])
            Require(typeof Node[Field] === 'boolean', 'invalid ' + Field);
        for (const Field of ['minimum', 'maximum', 'value'])
            Require(Node[Field] === null || Number.isFinite(Node[Field]), 'invalid ' + Field);
        const Bounds = Node.bounds;
        Require(Bounds && ['x', 'y', 'width', 'height'].every(Field =>
            typeof Bounds[Field] === 'number' && Number.isFinite(Bounds[Field])) &&
            Bounds.width >= 0 && Bounds.height >= 0, 'invalid node bounds');
        ById.set(Node.id, Node);
        Children.set(Node.id, []);
        PreviousId = Node.id;
    }
    for (const Node of Nodes) {
        Require(Node.parentId === 0 || ById.has(Node.parentId), 'tree has a missing parent');
        Children.get(Node.parentId).push(Node.id);
    }
    const Seen = new Set();
    const Stack = [...Children.get(0)];
    while (Stack.length) {
        const Id = Stack.pop();
        Require(!Seen.has(Id), 'tree contains a parenting cycle');
        Seen.add(Id);
        Stack.push(...Children.get(Id));
    }
    Require(Seen.size === Nodes.length, 'tree contains an unreachable cycle');
    return { Nodes, ById, Children };
}

function GetProperties(Node, Classes) {
    if (!Node) return [];
    const Chain = [];
    const Visited = new Set();
    let Class = Classes.get(Node.className);
    while (Class && !Visited.has(Class.name)) {
        Chain.unshift(Class);
        Visited.add(Class.name);
        Class = Classes.get(Class.base);
    }
    const Properties = [];
    const Added = new Set();
    for (const Item of Chain) for (const Property of Item.properties) {
        if (!Property || typeof Property.name !== 'string' || Added.has(Property.name)) continue;
        Added.add(Property.name);
        const Field = SnapshotFields[Property.name];
        if (Field === undefined) continue;
        const Value = Property.name === 'Parent' && Node.parentId === 0 ? null : Node[Field];
        Properties.push({ name: Property.name, type: Property.type, value: Value });
    }
    const Bounds = Node.bounds;
    Properties.push({ name: 'Resolved bounds', type: 'logical units',
        value: `${Bounds.x}, ${Bounds.y} · ${Bounds.width} × ${Bounds.height}` });
    return Properties;
}

module.exports = { ValidateSchema, ValidateTree, GetProperties };
