#include "SelectedFiles.h"
#include "../../runtime/project/Files.h"
#include <fstream>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
namespace Fs=std::filesystem;
using Lui::Native::SelectedFile;
static void Require(bool Value,const char* Message) {if(!Value) throw std::runtime_error(Message);}
static void Write(const Fs::path& Path,const std::string& Bytes) {std::ofstream File(Path,std::ios::binary); File<<Bytes; Require(bool(File),"fixture write");}
template<class Action> static void Refused(Action Run,const char* Code=nullptr) {
    try {Run();} catch(const Lui::Project::Internal::Failure& Error) {Require(!Code || std::string(Error.what())==Code,"wrong rejection"); return;}
    throw std::runtime_error("unsafe selected-file operation admitted");
}
int main() {
#if defined(_WIN32)
    SetErrorMode(0x8003);
#endif
    const auto Root=Fs::temp_directory_path()/("LuiSelectedFiles-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fs::create_directories(Root/"Selected"); Fs::create_directories(Root/"Outside");
    try {
        const auto Folder=Root/"Selected", Input=Folder/Fs::u8path("note-\xc3\xa9.txt");
        Write(Input,"first"); Write(Root/"Outside/secret.txt","OUTSIDE_CANARY");
        for(const auto* Name:{"../escape","a/b","C:stream",".","..","bad\nname","CON","LPT1.txt","tail.","tail "})
            Require(!Lui::Native::SelectedName(Name),"unsafe leaf accepted");
        Require(Lui::Native::SelectedName("note-\xc3\xa9.txt"),"UTF8 leaf refused");
        {
            SelectedFile File(Input,false,false); Require(File.Read({})=="first","selected read");
            Require(File.ByteLength()==5 && File.ReadRange(1,3,{})=="irs","bounded random read");
            Require(File.ReadRange(4,1024,{})=="t" && File.ReadRange(5,1,{}).empty(),"short read and EOF");
            Refused([&]{File.ReadRange(0,65537,{});},"InvalidArgument");
            Refused([&]{File.ReadRange(0,1,[]{return true;});},"Canceled");
            Refused([&]{File.Write("bad",{});},"CapabilityDenied");
            Refused([&]{File.Read([]{return true;});},"Canceled");
        }
        {
            SelectedFile Save(Input,false,true); Save.Write(std::string("second\0data",11),{});
            Require(Save.Read({})==std::string("second\0data",11),"atomic replacement bytes");
            Save.Write("third",{}); Require(Save.Read({})=="third","repeat save");
            Refused([&]{Save.Write(std::string(4*1024*1024+1,'x'),{});},"LimitExceeded");
            Refused([&]{Save.Write("cancel",[]{return true;});},"Canceled");
            Require(Save.Read({})=="third","canceled write changed file");
            Write(Input,"externally changed"); Refused([&]{Save.Write("must not overwrite",{});});
            Require(Save.Read({})=="externally changed","external change overwritten");
        }
        {
            SelectedFile Save(Folder/"new.txt",false,true); Save.Write("new document",{});
            Require(Save.Read({})=="new document","new file creation");
        }
        {
            auto Scope=std::make_shared<Lui::Native::SelectionScope>();
            SelectedFile Directory(Folder,true,false,Scope); const auto Entries=Directory.List({});
            Require(Entries.size()==2 && !Entries[0].Directory,"selected folder list");
            auto Child=Directory.Open(Input.filename().u8string()); Require(Child->Read({})=="externally changed","folder child read");
            Refused([&]{Directory.Open("../Outside/secret.txt");},"InvalidArgument");
            Refused([&]{Directory.Open("bad/child.txt");},"InvalidArgument");
            SelectedFile Copy(Folder/"while-folder-open.txt",false,true,Scope); Copy.Write("folder stays open",{});
            Require(Copy.Read({})=="folder stays open","save while selected folder remains open");
        }
        Fs::create_hard_link(Root/"Outside/secret.txt",Folder/"hard.txt");
        {
            Fs::create_directory(Folder/"Subfolder");
            auto Scope=std::make_shared<Lui::Native::SelectionScope>();
            SelectedFile Descendant(Folder/"Subfolder",true,false,Scope);
            SelectedFile AncestorSave(Folder/"ancestor-copy.txt",false,true,Scope);
            AncestorSave.Write("ancestor save with live descendant",{});
            Require(AncestorSave.Read({})=="ancestor save with live descendant","descendant folder blocked ancestor save");
        }
        Refused([&]{SelectedFile File(Folder/"hard.txt",false,false); File.Read({});});
        Refused([&]{SelectedFile File(Folder/"hard.txt",false,true); File.Write("bad",{});});
        std::error_code Error; Fs::create_symlink(Root/"Outside/secret.txt",Folder/"link.txt",Error);
        if(!Error) {
            Refused([&]{SelectedFile File(Folder/"link.txt",false,false); File.Read({});});
            Refused([&]{SelectedFile File(Folder/"link.txt",false,true); File.Write("bad",{});});
        }
        Error.clear(); Fs::create_directory_symlink(Root/"Outside",Root/"LinkedParent",Error);
        if(!Error) Refused([&]{SelectedFile File(Root/"LinkedParent/secret.txt",false,false); File.Read({});});
        Write(Folder/"big.txt",std::string(4*1024*1024+1,'x'));
        Refused([&]{SelectedFile File(Folder/"big.txt",false,false); File.Read({});},"LimitExceeded");
        for(int Index=0;Index<260;++Index) Write(Folder/("entry"+std::to_string(Index)),"");
        Refused([&]{SelectedFile Directory(Folder,true,false); Directory.List({});},"LimitExceeded");
        Refused([&]{SelectedFile File(Fs::path("relative.txt"),false,false);},"InvalidArgument");
        for(const auto& Entry:Fs::directory_iterator(Folder)) Require(Entry.path().filename().u8string().rfind(".lui-selected-",0)!=0,"temporary/guard leaked");
        std::ifstream Outside(Root/"Outside/secret.txt"); std::string Bytes; Outside>>Bytes; Require(Bytes=="OUTSIDE_CANARY","outside file mutated"); Outside.close();
        Fs::remove_all(Root); std::puts("[LUI:SelectedFiles] Read/write, selected roots, links, quotas, cancellation and recovery passed"); return 0;
    } catch(const std::exception& Error) {std::fprintf(stderr,"[LUI:SelectedFiles] %s\n",Error.what()); return 1;}
}
