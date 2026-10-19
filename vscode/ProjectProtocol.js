'use strict';
const Path=require('node:path');
const Fs=require('node:fs');
function SourceMap(Sources,Root) {
    if(!Array.isArray(Sources) || Sources.length>256) throw new Error('Invalid project sources');
    const Mapping=new Map(), Paths=new Set();
    for(const Source of Sources) {
        if(!Source || typeof Source.id!=='string' || !/^(app:|dep:[A-Za-z_][A-Za-z0-9_]*\/)[A-Za-z0-9_./-]+\.luau$/.test(Source.id) ||
            typeof Source.path!=='string' || Source.path.length>512 || !/^[A-Za-z0-9_./-]+\.luau$/.test(Source.path) ||
            Source.path.split('/').some(Part=>!Part || Part==='.' || Part==='..' || Part.endsWith('.')) ||
            typeof Source.hash!=='string' || !/^[a-f0-9]{64}$/.test(Source.hash) || Mapping.has(Source.id) || Paths.has(Source.path.toLowerCase()))
            throw new Error('Invalid project source mapping');
        const IdentityPath=Source.id.startsWith('app:')?Source.id.slice(4):Source.id.slice(Source.id.indexOf('/')+1);
        if(Source.id.length>512 || IdentityPath.split('/').some(Part=>!Part || Part==='.' || Part==='..' || Part.endsWith('.')))
            throw new Error('Invalid project identity');
        const Candidate=Path.resolve(Root,...Source.path.split('/'));
        const Relative=Path.relative(Root,Candidate);
        if(Path.isAbsolute(Relative) || Relative==='..' || Relative.startsWith('..'+Path.sep)) throw new Error('Escaped source mapping');
        Mapping.set(Source.id,{...Source,absolute:Candidate}); Paths.add(Source.path.toLowerCase());
    }
    return Mapping;
}
function ProjectDiagnostics(Result,Root) {
    if(!Result || Result.version!==2 || Result.profile!=='ProjectTypesV1' || Result.diagnosticsMode!=='Development' || Result.redacted!==false ||
        !/^[a-f0-9]{64}$/.test(Result.digest) || !/^[a-f0-9]{64}$/.test(Result.sourceDigest) || typeof Result.truncated!=='boolean' ||
        !Array.isArray(Result.diagnostics) || Result.diagnostics.length>256) throw new Error('Invalid project diagnostic envelope');
    const Map=SourceMap(Result.sources,Root);
    for(const Issue of Result.diagnostics) {
        if(!Issue || !['syntax','type','import','module','project'].includes(Issue.kind) || typeof Issue.message!=='string' ||
            Buffer.byteLength(Issue.message,'utf8')>2048) throw new Error('Invalid project diagnostic');
        if(Issue.kind==='project') {if(Issue.source!==null || Issue.range!==null) throw new Error('Invalid project location'); continue;}
        const Source=Map.get(Issue.source); const Start=Issue.range?.start, End=Issue.range?.end;
        if(!Source || !Start || !End || ![Start.line,Start.column,End.line,End.column].every(Value=>Number.isSafeInteger(Value) && Value>=0 && Value<=8*1024*1024) ||
            End.line<Start.line || (End.line===Start.line && End.column<Start.column)) throw new Error('Invalid project range');
    }
    return {...Result,SourceMap:Map};
}
function SafeSource(Map,Id) {
    const Source=Map?.get(Id); if(!Source) return null;
    try {
        // Mapping came from native preparation; current navigation never follows
        // a newly introduced link in its project-relative path.
        let Current=Source.absolute;
        while(true) {if(Fs.lstatSync(Current).isSymbolicLink()) return null; const Parent=Path.dirname(Current); if(Parent===Current) break; Current=Parent;}
        return Source.absolute;
    } catch {return null;}
}
module.exports={SourceMap,ProjectDiagnostics,SafeSource};
