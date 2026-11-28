"""Exercise the packaged live database through independent application processes."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def Main():
    Parser=argparse.ArgumentParser()
    Parser.add_argument('--sdk',type=Path,required=True)
    Sdk=Parser.parse_args().sdk.resolve()
    Components=json.loads((Sdk/'SdkManifest.json').read_bytes())['Components']
    Cli=Sdk/Components['Cli']
    Host=Sdk/Components['HeadlessHost']
    with tempfile.TemporaryDirectory(prefix='lui-live-') as Temporary:
        Root=Path(Temporary)
        App=Root/'App'
        shutil.copytree(Sdk/'Examples/live-records',App)
        Manifest=App/'lui.json'
        Db=Root/'private'
        Db.mkdir(mode=0o700)
        Stage=Root/'Stage'
        for Command in (['check',Manifest],['stage',Manifest,'--output',Stage],['verify',Stage]):
            subprocess.run([Cli,*Command,'--sdk',Sdk],check=True,stdout=subprocess.DEVNULL,timeout=20)
        Base=[Cli,'run',Stage/'lui.json','--sdk',Sdk,'--mode','headless','--stdio','true','--providers','SQLite']
        def Run(Args,Code=0):
            Done=subprocess.run(Args,capture_output=True,timeout=25)
            assert Done.returncode==Code,(Done.returncode,Done.stdout[:180],Done.stderr[:250])
            return Done.stdout
        Run([*Base,'--','list'],1)
        def Records(*Args):
            return Run([*Base,'--database-root',Db,'--',*Args])
        Run([*Base,'--database-root',Db,'--','check'],2)
        assert not (Db/'lui.db').exists()
        assert Records('list')==b'' and Records('check')==b'ok\n'
        with ThreadPoolExecutor(max_workers=4) as Pool:
            list(Pool.map(lambda I: Records('add',str(I)),range(8)))
        assert {Line.split(b'\t')[1] for Line in Records('list').splitlines()}=={str(I).encode() for I in range(8)}
        print('[LUI:SQLite] Eight concurrent client additions persisted through restart',flush=True)
        Script=App/'Src/Main.luau'
        Script.write_text("""--!strict
local D=require("@SQLite/SQLite")
local S=require("@Stdio/Stdio")
local Args=S.Arguments()
local Db=D.OpenLive(tonumber(Args[2]) or 2000)
S.SetExitStatus(7)
local function Finish(Results,Failure)
    Db:Close()
    if Failure then
        S.Write(Failure,function() S.SetExitStatus(1) end)
    elseif Results then
        local Rows=Results[#Results].Rows
        S.Write(if Rows[1] then tostring(Rows[1][1]) else "ok",function() S.SetExitStatus(0) end)
    end
end
if Args[1]=="seed" then
    local Groups={ {Sql="CREATE TABLE data (content BLOB NOT NULL)",Bindings={{}}},
        {Sql="INSERT INTO data VALUES (?)",Bindings={{buffer.create(32768)}}} } :: {D.Batch}
    for _=1,8 do table.insert(Groups,{Sql="INSERT INTO data SELECT content FROM data",Bindings={{}}}) end
    Db:Transaction(Groups,Finish)
elseif Args[1]=="change" then
    Db:Execute("UPDATE data SET content=?",{string.rep("x",32768)},Finish)
elseif Args[1]=="count" then
    Db:Execute("SELECT count(*) FROM data WHERE typeof(content)='blob'",{},Finish)
elseif Args[1]=="deny" then
    Db:Execute("ATTACH DATABASE 'other.db' AS other",{},Finish)
elseif Args[1]=="cancel" then
    local R=Db:Execute("INSERT INTO note(body) VALUES (?)",{"canceled_write"},Finish)
    R:Cancel()
end
""",encoding='utf-8')
        Native=[Host,'--project',Manifest,'--stdio','--provider','SQLite','--database-root',Db,'--']
        assert Run([*Native,'seed'])==b'ok'
        assert (Db/'lui.db').stat().st_size>8*1024*1024
        assert Run([*Native,'count'])==b'256'
        assert Run([*Native,'deny'],1)==b'CapabilityDenied'
        with (Db/'lui.lock').open('r+b') as Gate:
            if os.name=='nt':
                import msvcrt
                msvcrt.locking(Gate.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(Gate,fcntl.LOCK_EX)
            assert Run([*Native,'count','30'],1)==b'WouldBlock'
            assert Run([*Native,'cancel'],1)==b'Canceled'
        assert b'canceled_write' not in Records('list')
        # Kill the native host itself after observing a nonempty rollback journal.
        Journal=Db/'lui.db-journal'
        Magic=bytes.fromhex('d9d505f920a163d7')
        def Spilled():
            with Journal.open('rb') as Rollback, (Db/'lui.db').open('rb') as Disk:
                if Rollback.read(8)!=Magic: return False
                Disk.seek(4096)
                return b'x'*1024 in Disk.read(256*1024)
        with subprocess.Popen([*Native,'change'],stdout=subprocess.PIPE,stderr=subprocess.PIPE) as Child:
            Deadline=time.monotonic()+15
            while Child.poll() is None and time.monotonic()<Deadline:
                try:
                    if Spilled():
                        Child.kill()
                        break
                except FileNotFoundError:
                    pass
            Child.communicate(timeout=5)
            assert Child.returncode!=0 and Spilled(),'missed hot journal with uncommitted database pages'
        assert Records('check')==b'ok\n' and Run([*Native,'count'])==b'256'
        assert {F.name for F in Db.iterdir()}=={'lui.db','lui.lock'}
        if os.name!='nt':
            Other=Root/'outside'
            Other.write_bytes(b'unchanged')
            (Db/'lui.db-journal').symlink_to(Other)
            Run([*Native,'count'],1)
            assert Other.read_bytes()==b'unchanged'
            (Db/'lui.db-journal').unlink()
            Db.chmod(0o755)
            Run([*Native,'count'],1)
        print('[LUI:SQLite] 8 MiB growth, busy timeout, cancellation, ATTACH denial, interrupted journal and integrity passed')


if __name__=='__main__':
    Main()
