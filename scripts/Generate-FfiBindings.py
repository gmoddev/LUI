"""Generate reviewable C bindings using an installed Clang AST frontend."""
import argparse
import json
from pathlib import Path
import re
import subprocess


Types = {'void': 'void', '_Bool': 'bool8', 'float': 'f32', 'double': 'f64', 'signed char': 'i8',
         'unsigned char': 'u8', 'short': 'i16', 'unsigned short': 'u16', 'int': 'i32',
         'unsigned int': 'u32', 'long': 'long', 'unsigned long': 'ulong', 'long long': 'i64',
         'unsigned long long': 'u64', 'size_t': 'size', 'intptr_t': 'intptr', 'uintptr_t': 'uintptr'}
for Width in (8, 16, 32, 64):
    Types['int%d_t' % Width] = 'i' + str(Width)
    Types['uint%d_t' % Width] = 'u' + str(Width)
Promoted = {'i32', 'u32', 'i64', 'u64', 'long', 'ulong', 'size', 'intptr', 'uintptr', 'f64', 'pointer', 'utf8'}


def CType(Name, Aliases, Model):
    Name = ' '.join(re.sub(r'\b(const|volatile|restrict)\b', ' ', Name).split())
    if Name in Aliases:
        Resolved = CType(Aliases[Name], Aliases, Model)
        Named = Types.get(Name)
        if Named and Model.get(Named, Named) == Model.get(Resolved, Resolved):
            return Named
        return Resolved
    if Name in Types:
        return Types[Name]
    # Encoding and pointee ownership require human review. Function pointers,
    # vector/address-space attributes and array declarators need explicit bindings.
    Pointer = re.fullmatch(r'([\w ]+?)\s*(?:\*\s*)+', Name)
    if Pointer:
        Base = Pointer[1].strip()
        while Base in Aliases:
            Base = Aliases[Base]
        if '(' not in Base and '[' not in Base:
            return 'pointer'
    raise ValueError('Unsupported C type: ' + Name)


def Generate(Header, Symbols, Target, Clang, TailOptions=(), TextOptions=()):
    if len(Symbols) != len(set(Symbols)) or any(not re.fullmatch(r'[A-Za-z_]\w*', S) for S in Symbols):
        raise ValueError('Expected unique C identifiers')
    Tails = {}
    for Option in TailOptions:
        Name, Separator, Tail = Option.partition('=')
        if not Separator or Name not in Symbols or Name in Tails:
            raise ValueError('Expected one explicit variadic tail per selected symbol: ' + Name)
        Parameters = Tail.split(',') if Tail else []
        if any(Type not in Promoted for Type in Parameters):
            raise ValueError('Unsupported promoted variadic type for ' + Name)
        Tails[Name] = Parameters
    Text = {}
    for Option in TextOptions:
        Name, Separator, Positions = Option.partition('=')
        Items = Positions.split(',')
        if (not Separator or Name not in Symbols or Name in Text or len(Items) != len(set(Items))
                or any(not re.fullmatch(r'result|[1-9][0-9]*', Item) for Item in Items)):
            raise ValueError('Expected explicit UTF-8 positions for one selected symbol: ' + Name)
        Text[Name] = set(Items)
    Command = [Clang, '--target=' + Target, '-x', 'c', '-std=c11', '-fsyntax-only', '-Werror',
               '-Xclang', '-ast-dump=json', str(Header.resolve())]
    Ast = json.loads(subprocess.run(Command, stdout=subprocess.PIPE, check=True, timeout=45).stdout)
    Windows = Target == 'x86_64-pc-windows-msvc'
    Model = {'long': 'i32' if Windows else 'i64', 'ulong': 'u32' if Windows else 'u64',
             'size': 'u64', 'intptr': 'i64', 'uintptr': 'u64'}
    Aliases = {Node['name']: Node['type'].get('desugaredQualType', Node['type']['qualType']) for Node in Ast.get('inner', [])
               if Node.get('kind') == 'TypedefDecl'}
    Functions = {}
    for Node in Ast.get('inner', []):
        Name = Node.get('name')
        if Node.get('kind') != 'FunctionDecl' or Name not in Symbols:
            continue
        # Clang removes explicit calls using the target's default convention from
        # the desugared type. A foreign convention remains visible and is rejected.
        Qualified = Node['type'].get('desugaredQualType', Node['type']['qualType'])
        Prototype = re.fullmatch(r'(.+?)\s*\(([^()]*)\)', Qualified)
        if (not Prototype or Prototype[2] == '' or bool(Node.get('variadic')) != (Name in Tails) or Node.get('storageClass') == 'static'
                or any(Item['kind'].endswith('Attr') and Item['kind'] not in ('DLLImportAttr','DLLExportAttr','VisibilityAttr')
                       for Item in Node.get('inner', []))):
            raise ValueError('Unsupported prototype for ' + Name + ': ' + Qualified)
        ParameterTypes = [Item['type']['qualType'] for Item in Node.get('inner', []) if Item['kind'] == 'ParmVarDecl']
        TextPositions = Text.get(Name, set())
        if any(Position != 'result' and int(Position) > len(ParameterTypes) for Position in TextPositions):
            raise ValueError('Unsupported UTF-8 position for ' + Name)

        def Map(Type, Position):
            if Position not in TextPositions:
                return CType(Type, Aliases, Model)
            if not re.fullmatch(r'(?:const\s+char|char\s+const)\s*\*', Aliases.get(Type, Type)):
                raise ValueError('UTF-8 requires const char * for ' + Name + ' at ' + Position)
            return 'utf8'

        Params = [Map(Type, str(Index)) for Index, Type in enumerate(ParameterTypes, 1)]
        if len(Params) > 16:
            raise ValueError('Unsupported argument count for ' + Name)
        if Name in Tails and (not Params or len(Params) + len(Tails[Name]) > 16):
            raise ValueError('Unsupported variadic argument count for ' + Name)
        Function = (Params, Map(Prototype[1], 'result'))
        if Name in Functions and Functions[Name] != Function:
            raise ValueError('Conflicting prototypes for ' + Name)
        Functions[Name] = Function
    if set(Functions) != set(Symbols):
        raise ValueError('Missing declarations: ' + ', '.join(sorted(set(Symbols) - set(Functions))))
    Lines = ['--!strict', '-- Generated C prototypes. Review pointer lifetimes and encoding against the native API.',
             'local Ffi = require("@Ffi/Ffi")', 'return function(Image: Ffi.Library)',
             '    assert(Ffi.Platform().OS == "%s", "[LUI:Ffi] BindingTargetMismatch")' % ('Windows' if Windows else 'Linux'),
             '    local Opened: {Ffi.Foreign} = {}',
             '    local function Keep(Function: Ffi.Foreign): Ffi.Foreign',
             '        table.insert(Opened, Function)',
             '        return Function',
             '    end',
             '    local function BindAll()',
             '        return {']
    for Name in Symbols:
        Params, Result = Functions[Name]
        if Name in Tails:
            Arguments = ', '.join(json.dumps(T) for T in Params + Tails[Name])
            Lines.append('            ["%s"] = Keep(Image:BindVariadic("%s", %d, {%s}, "%s")),' %
                         (Name, Name, len(Params), Arguments, Result))
        else:
            Lines.append('            ["%s"] = Keep(Image:Bind("%s", {%s}, "%s")),' %
                         (Name, Name, ', '.join(json.dumps(T) for T in Params), Result))
    return '\n'.join([*Lines, '        }', '    end', '    local function CloseAll()',
                      '        for Index = #Opened, 1, -1 do Opened[Index]:Close() end',
                      '        table.clear(Opened)', '    end', '    if not pcall then return BindAll(), CloseAll end',
                      '    local Ok, Bindings = pcall(BindAll)', '    if not Ok then', '        CloseAll()',
                      '        error(Bindings, 0)', '    end', '    return Bindings, CloseAll', 'end', ''])


if __name__ == '__main__':
    Parser = argparse.ArgumentParser(description=__doc__)
    Parser.add_argument('header', type=Path)
    Parser.add_argument('--symbol', action='append', required=True)
    Parser.add_argument('--variadic-tail', action='append', default=[], metavar='SYMBOL=TYPE[,TYPE...]')
    Parser.add_argument('--utf8', action='append', default=[], metavar='SYMBOL=POSITION[,POSITION...]',
                        help='opt in const char * arguments by one-based index or use result')
    Parser.add_argument('--target', choices=('x86_64-pc-windows-msvc', 'x86_64-unknown-linux-gnu'), required=True)
    Parser.add_argument('--clang', default='clang')
    Parser.add_argument('--output', type=Path, required=True)
    Args = Parser.parse_args()
    try:
        Source = Generate(Args.header, Args.symbol, Args.target, Args.clang, Args.variadic_tail, Args.utf8)
        with Args.output.open('x', encoding='utf-8', newline='\n') as Output:
            Output.write(Source)
    except (ValueError, OSError, subprocess.SubprocessError) as Error:
        Parser.exit(1, '[LUI:Ffi] ' + str(Error) + '\n')
