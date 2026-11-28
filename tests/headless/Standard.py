"""Qualify selected first-party provider images via the packaged application entry point."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zlib


def Main():
    Parser = argparse.ArgumentParser()
    Parser.add_argument('--sdk', type=Path, required=True)
    Sdk = Parser.parse_args().sdk.resolve()
    Inventory = json.loads((Sdk / 'SdkManifest.json').read_bytes())
    Cli = Sdk / Inventory['Components']['Cli']
    Checks = []
    with tempfile.TemporaryDirectory(prefix='lui-providers-') as Temporary:
        Root = Path(Temporary)
        def Project(Name):
            Folder = Root / Name
            shutil.copytree(Sdk / 'Examples' / Name, Folder)
            Manifest = Folder / 'lui.json'
            for Args in ([Cli,'check',Manifest], [Cli,'stage',Manifest,'--output',Root/(Name+'Stage')],
                         [Cli,'verify',Root/(Name+'Stage')]):
                subprocess.run([*Args,'--sdk',Sdk],stdout=subprocess.DEVNULL,check=True,timeout=20)
            return Folder

        def Run(Folder, Provider, Arguments=(), Data=b'', Code=0, Storage=False, Streams=True):
            Cmd = [Cli,'run',Folder/'lui.json','--sdk',Sdk,'--mode','headless']
            if Streams: Cmd.extend(['--stdio','true'])
            if Provider: Cmd.extend(['--providers',Provider])
            if Storage: Cmd.extend(['--grant','Storage','--storage-root',Root/'private-data'])
            if Arguments: Cmd.extend(['--',*Arguments])
            Result = subprocess.run(Cmd,input=Data,capture_output=True,timeout=20)
            assert Result.returncode == Code, (Folder.name,Provider,Arguments,Result.returncode,Result.stdout[:130],Result.stderr[:160])
            return Result

        Digest = Project('digest')
        for Message in (b'', b'abc', bytes(range(256))*256):
            assert Run(Digest,'Crypto',Data=Message).stdout == hashlib.sha256(Message).hexdigest().encode()+b'\n'
        Run(Digest,'Crypto',Data=b'x'*65537,Code=1)
        Checks.append('SHA-256 vectors, binary bytes and ceiling')
        CryptoSource = Digest / 'Src/Main.luau'
        CryptoSource.write_text("""--!strict
local C = require("@Crypto/Crypto")
local E = require("@Encoding/Encoding")
assert(E.HexEncode(C.HmacSha256(string.rep(string.char(11),20),"Hi There")) == "B0344C61D8DB38535CA8AFCEAF0BF12B881DC200C9833DA726E9376C2E32CFF7")
assert(#C.RandomBytes(1) == 1 and #C.RandomBytes(4096) == 4096)
local Key = string.rep(string.char(0),32)
local Salt = E.HexDecode("000102030405060708090A0B0C")
local Info = E.HexDecode("F0F1F2F3F4F5F6F7F8F9")
assert(#C.HkdfSha256(string.rep("x",2048),Salt,Info,64)==64)
assert(not pcall(C.HkdfSha256,string.rep("x",2049),Salt,Info,64))
assert(E.HexEncode(C.HkdfSha256(string.rep(string.char(11),22),Salt,Info,42)) ==
    "3CB25F25FAACD57A90434F64D0362F2A2D2D0A90CF1A5A4C5DB02D56ECC4C5BF34007208D5B887185865")
assert(E.HexEncode(C.HkdfSha256(string.rep(string.char(11),22),"","",42)) ==
    "8DA4E775A563C18F715F802A063C5A31B8A11F5C5EE1879EC3454E5F3C738D2D9D201395FAA4B61A96C8")
local Known=E.HexDecode("01"..string.rep("00",12).."CEA7403D4D606B6E074EC5D3BAF39D18D0D1C8A799996BF0265B98B5D48AB919")
assert(C.Open(Key,Known)==string.rep(string.char(0),16))
for _, Message in {"", "abc", string.rep("z",65536)} do
    local Envelope=C.Seal(Key,Message,"record")
    assert(#Envelope==#Message+29 and C.Open(Key,Envelope,"record")==Message)
    assert(not pcall(C.Open,Key,Envelope,"other"))
    local Changed=string.sub(Envelope,1,-2)..string.char(bit32.bxor(string.byte(Envelope,-1),1))
    assert(not pcall(C.Open,Key,Changed,"record"))
end
assert(not pcall(C.Seal,"bad","x") and not pcall(C.HkdfSha256,"short","","",32))
assert(not pcall(C.RandomBytes,0) and not pcall(C.RandomBytes,4097) and not pcall(C.HmacSha256,"","m"))
assert(not pcall(function() (math :: any).sqrt = nil end))
""",encoding='utf-8')
        Spec=json.loads((Digest/'lui.json').read_bytes())
        Spec['ExtensionDeclarations'].remove('Stdio.json')
        Spec['Modules']['Requires'].remove('Stdio')
        Spec['Modules']['Dependencies']=[D for D in Spec['Modules']['Dependencies'] if D['Alias']!='Stdio']
        Lock=json.loads((Digest/'Modules.lock.json').read_bytes())
        Lock['Packages']=[P for P in Lock['Packages'] if P['Alias']!='Stdio']
        (Digest/'Modules.lock.json').write_text(json.dumps(Lock))
        (Digest/'lui.json').write_text(json.dumps(Spec))
        assert b'NativeResources=0' in Run(Digest,'Crypto',Streams=False).stdout
        Checks.append('standalone Crypto, RFC 4231/5869, AES-GCM vector, authenticated failure, random limits and retirement')
        Codec = Project('codec')
        for Name in ('Zlib-LICENSE.txt','Zstd-LICENSE.txt'):
            assert (Root/'codecStage'/Name).read_bytes() == (Sdk/'Libraries/Compression'/Name).read_bytes()
        Raw = bytes(range(256))*3072
        for Format,Window in [('gzip',31),('zlib',15),('deflate',-15)]:
            Encoded=Run(Codec,'Compression',('encode',Format),Raw).stdout
            assert zlib.decompress(Encoded,Window)==Raw
            Python=zlib.compressobj(wbits=Window)
            Foreign=Python.compress(Raw)+Python.flush()
            assert Run(Codec,'Compression',('decode',Format),Foreign).stdout==Raw
            Run(Codec,'Compression',('decode',Format),Foreign[:-1],1)
            Run(Codec,'Compression',('decode',Format),Foreign+b'extra',1)
        Run(Codec,'Compression',('decode','gzip'),gzip.compress(b'x'*(1048576+1)),1)
        # One ordinary Zstandard frame containing an uncompressed byte block.
        Frame = bytes.fromhex('28b52ffd2005290000') + b'hello'
        assert Run(Codec,'Compression',('decode','zstd'),Frame).stdout==b'hello'
        Packed = Run(Codec,'Compression',('encode','zstd'),Raw).stdout
        assert Run(Codec,'Compression',('decode','zstd'),Packed).stdout==Raw
        Run(Codec,'Compression',('decode','zstd'),Packed[:-1],1)
        Run(Codec,'Compression',('decode','zstd'),Frame+Frame,1)
        Run(Codec,None,('encode','gzip'),b'a',1)
        Checks.append('four codecs, 768KiB, independent frames, truncation, trailing bytes, expansion cap and missing grant')
        (Codec/'Src/Main.luau').write_text("""--!strict
local C = require("@Compression/Compression")
local S = require("@Stdio/Stdio")
S.SetExitStatus(3)
local Accepted = false
local R = C.Encode("gzip", buffer.create(1048576), 1048576, function(Output, Failure)
    if (Accepted and Failure == "Canceled") or (not Accepted and Output and not Failure) then S.SetExitStatus(0) end
end)
Accepted = R:Cancel()
""",encoding='utf-8')
        Run(Codec,'Compression')
        Checks.append('compression cancellation')
        Decl=Codec/'Compression.json'
        Record=json.loads(Decl.read_bytes())
        Binding=Record['Extension']['Bindings'][0]
        (Codec/Binding['Path']).write_bytes(b'not an executable provider')
        Binding['Sha256']=hashlib.sha256((Codec/Binding['Path']).read_bytes()).hexdigest()
        Decl.write_text(json.dumps(Record))
        Refused = Run(Codec,'Compression',Code=1)
        assert not Refused.stdout and b'ProviderNotQualified' in Refused.stderr
        Checks.append('unapproved image refused before app entry')
        Records=Project('records')
        Combined=Root/'combined'
        shutil.copytree(Records,Combined)
        Manifest=json.loads((Combined/'lui.json').read_bytes())
        Packages=json.loads((Combined/'Modules.lock.json').read_bytes())
        Manifest['Capabilities'].remove('Storage')
        del Manifest['Storage']
        for Name,Example in [('Compression','codec'),('Crypto','digest')]:
            Source=Sdk/'Examples'/Example
            Spec=json.loads((Source/'lui.json').read_bytes())
            Lock=json.loads((Source/'Modules.lock.json').read_bytes())
            Manifest['Extensions'].append(Name)
            Decl=Name+'.json'
            Manifest['ExtensionDeclarations'].append(Decl)
            shutil.copyfile(Source/Decl,Combined/Decl)
            Relative=json.loads((Source/Decl).read_bytes())['Extension']['Bindings'][0]['Path']
            shutil.copyfile(Source/Relative,Combined/Relative)
            for Notice in Spec['Assets']:
                Manifest['Assets'].append(Notice)
                shutil.copyfile(Source/Notice,Combined/Notice)
            Dependency=next(Item for Item in Spec['Modules']['Dependencies'] if Item['Alias']==Name)
            Manifest['Modules']['Dependencies'].append(Dependency)
            Manifest['Modules']['Requires'].append(Name)
            Packages['Packages'].append(next(Item for Item in Lock['Packages'] if Item['Alias']==Name))
            shutil.copytree(Source/Dependency['Root'],Combined/Dependency['Root'])
        (Combined/'lui.json').write_text(json.dumps(Manifest))
        (Combined/'Modules.lock.json').write_text(json.dumps(Packages))
        (Combined/'Src/Main.luau').write_text("""--!strict
local C = require("@Crypto/Crypto")
local Z = require("@Compression/Compression")
local D = require("@SQLite/SQLite")
local S = require("@Stdio/Stdio")
local Hash = C.Sha256("Combined")
local Db = D.Open()
S.SetExitStatus(5)
Z.Encode("gzip",buffer.fromstring(Hash),4096,function(Packed, Why)
    assert(Packed and not Why)
    Db:Execute("SELECT length(?)", {Packed}, function(Result, Err)
        assert(Result and not Err and Result[1].Rows[1][1]==buffer.len(Packed))
        Db:Close()
        S.SetExitStatus(0)
    end)
end)
""")
        Stage=Root/'combinedStage'
        for Command in (['check',Combined/'lui.json'],['stage',Combined/'lui.json','--output',Stage],['verify',Stage]):
            subprocess.run([Cli,*Command,'--sdk',Sdk],check=True,stdout=subprocess.DEVNULL,timeout=20)
        Run(Stage,'Crypto,Compression,SQLite')
        Checks.append('combined three-provider project checked, staged, verified and executed')
        RuntimeRoot = Root/'portable'
        Components = Inventory['Components']
        for Key in ('HeadlessHost','Runtime'):
            Relative = Path(Components[Key])
            (RuntimeRoot/Relative.parent).mkdir(parents=True,exist_ok=True)
            shutil.copy2(Sdk/Relative,RuntimeRoot/Relative)
        Host = RuntimeRoot/Components['HeadlessHost']
        Environment = dict(os.environ)
        if os.name == 'nt':
            for File in ('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll'):
                shutil.copyfile((Sdk/Components['HeadlessHost']).parent/File,Host.parent/File)
            Environment['PATH'] = str(Path(os.environ['SystemRoot'])/'System32')
        else:
            Environment.pop('LD_LIBRARY_PATH',None)
        for Package,Names,Data,Expected in ((Root/'digestStage',['Crypto'],b'abc',hashlib.sha256(b'abc').hexdigest().encode()+b'\n'),
                                            (Stage,['Crypto','Compression','SQLite'],b'',b'')):
            Arguments = [Host,'--project',Package/'lui.json','--stdio']
            for Name in Names: Arguments += ['--provider',Name]
            Child = subprocess.run(Arguments,input=Data,capture_output=True,cwd=RuntimeRoot,env=Environment,timeout=15)
            assert (Child.returncode,Child.stdout,Child.stderr)==(0,Expected,b''), (Names,Child.returncode,Child.stderr[:140])
        Checks.append('isolated native-only deployment runs selected and combined staged applications')
        (Root/'private-data').mkdir()
        assert Run(Records,'SQLite',('list',),Storage=True).stdout==b''
        Run(Records,'SQLite',('add','two\tcolumns\nand rows'),Code=2,Storage=True)
        assert Run(Records,'SQLite',('add',"quote's ?; value"),Storage=True).stdout==b"1\tquote's ?; value\n"
        assert Run(Records,'SQLite',('list',),Storage=True).stdout==b"1\tquote's ?; value\n"
        Checks.append('SQL parameters and scoped persistence after process restart')
        (Records/'Src/Main.luau').write_text("""--!strict
local D = require("@SQLite/SQLite")
local S = require("@Stdio/Stdio")
local Db = D.Open()
S.SetExitStatus(5)
local Closing = D.Open()
Closing:Execute("SELECT 1", {}, function() error("closed callback escaped") end)
assert(not pcall(function() Closing:Snapshot() end))
assert(not pcall(function() Closing:Execute("SELECT 2",{},function() end) end))
Closing:Close()
assert(not pcall(function() Closing:Prepare("SELECT 1") end))
assert(not pcall(function() Db:Execute("SELECT ?", {buffer.create(65536)}, function() end) end))
Db:Execute("CREATE TABLE t (k INTEGER PRIMARY KEY, v BLOB)", {}, function(_, Failure)
    assert(not Failure)
    local Insert = Db:Prepare("INSERT INTO t VALUES (?,?)")
    Insert:RunMany({{1,buffer.fromstring("a")},{2,buffer.fromstring("b")}},function(_, Fail)
        assert(not Fail)
        Insert:Close()
        local Before=buffer.tostring(Db:Snapshot())
        Db:Transaction({{Sql="INSERT INTO t VALUES(3,NULL)",Bindings={{}}},{Sql="INSERT INTO t VALUES(1,NULL)",Bindings={{}}}},function(_, Err)
            assert(Err and buffer.tostring(Db:Snapshot())==Before)
            Db:Execute("ATTACH DATABASE ? AS extra", {":memory:"}, function(_, Denied)
                assert(Denied=="CapabilityDenied")
                Db:Execute("SELECT k,v,NULL FROM t ORDER BY k", {}, function(Result, SelectError)
                    assert(Result and not SelectError and #Result[1].Rows==2)
                    assert(Result[1].Rows[1][1]==1 and buffer.tostring(Result[1].Rows[1][2] :: buffer)=="a" and Result[1].Rows[1][3]==D.Null)
                    local Accepted = false
                    local Request=Db:Execute("SELECT count(*) FROM t a,t b,t c,t d,t e,t f,t g,t h,t i,t j,t k,t l", {}, function(Last, Cancel)
                        assert((Accepted and Cancel=="Canceled") or (not Accepted and Last and not Cancel))
                        Db:Close()
                        local Bad=D.Open(buffer.fromstring("invalid database"))
                        Bad:Execute("SELECT 1", {}, function(_, Why)
                            assert(Why=="InvalidArgument")
                            Bad:Close(); S.SetExitStatus(0)
                        end)
                    end)
                    Accepted = Request:Cancel()
                end)
            end)
        end,"Immediate")
    end)
end)
""",encoding='utf-8')
        Run(Records,'SQLite',Storage=True)
        Checks.append('SQLite prepared reuse, BLOB/NULL, failed transaction rollback, ATTACH denial, cancellation and invalid image')
    print(json.dumps({'Status':'PASS','Checks':Checks}))


if __name__=='__main__':
    Main()
