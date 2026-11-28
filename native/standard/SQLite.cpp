#include "LiveStore.h"
#include <sqlite3.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

namespace Lui::Standard {
namespace {
constexpr size_t MaxImage = 1024 * 1024, MaxPacket = 65536;
const LuiNativeMethodV2 Methods[] = {
    {sizeof(LuiNativeMethodV2),2,"Transaction",MaxImage+MaxPacket+4,MaxImage+MaxPacket+4,0,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"LiveTransaction",MaxPacket+4,MaxPacket+4,0,0,30000,0},
    {sizeof(LuiNativeMethodV2),2,"CheckLive",4,1,0,0,30000,0},
};
struct Failure { uint32_t Status; };
void Check(bool Condition, uint32_t Status=LUI_NATIVE_INVALID_ARGUMENT) {
    if (!Condition) throw Failure{Status};
}
struct Reader {
    const uint8_t* Data;
    size_t Left;
    const uint8_t* Take(size_t Count) {
        Check(Count <= Left);
        const auto* Start = Data;
        Data += Count;
        Left -= Count;
        return Start;
    }
    unsigned Byte() { return *Take(1); }
    std::string_view Text() {
        const auto* Header = Take(2);
        const size_t Count = unsigned(Header[0]) | unsigned(Header[1]) << 8;
        return {reinterpret_cast<const char*>(Take(Count)), Count};
    }
};
struct Writer {
    std::vector<uint8_t> Bytes;
    void Need(size_t Count) { Check(Count <= MaxPacket-Bytes.size(), LUI_NATIVE_LIMIT_EXCEEDED); }
    void Byte(unsigned Value) { Need(1); Bytes.push_back(static_cast<uint8_t>(Value)); }
    void Word(unsigned Value) { Byte(Value); Byte(Value >> 8); }
    void Text(const void* Data, size_t Size) {
        Check(Size <= 65535, LUI_NATIVE_LIMIT_EXCEEDED);
        Need(Size+2);
        Word(static_cast<unsigned>(Size));
        const auto* First = static_cast<const uint8_t*>(Data);
        if (Size) Bytes.insert(Bytes.end(), First, First+Size);
    }
};
struct Budget { const std::atomic<bool>& Canceled; bool Live=false; unsigned Remaining=3000; };
int Progress(void* Context) {
    auto& Limits = *static_cast<Budget*>(Context);
    return Limits.Canceled || !Limits.Remaining || !--Limits.Remaining;
}
int Authorize(void*,int Action,const char*,const char* Second,const char* Database,const char* Origin) {
    if (Origin || (Database && std::strcmp(Database,"main"))) return SQLITE_DENY;
    switch (Action) {
    case SQLITE_READ: case SQLITE_SELECT: case SQLITE_INSERT: case SQLITE_UPDATE: case SQLITE_DELETE:
    case SQLITE_CREATE_TABLE: case SQLITE_CREATE_INDEX: case SQLITE_DROP_TABLE: case SQLITE_DROP_INDEX:
        return SQLITE_OK;
    case SQLITE_FUNCTION: {
        const std::string_view Name = Second ? Second : "";
        for (auto Allowed : {"count","min","max","avg","sum","total","coalesce","ifnull","length","abs","round","lower","upper","substr","typeof"})
            if (Name == Allowed) return SQLITE_OK;
        break;
    }
    }
    return SQLITE_DENY;
}
void Sql(int Code, Budget& Limits) {
    if (Code == SQLITE_OK) return;
    if (Limits.Canceled) throw Failure{LUI_NATIVE_CANCELED};
    if (Code==SQLITE_AUTH || Code==SQLITE_PERM) throw Failure{LUI_NATIVE_CAPABILITY_DENIED};
    if (Code==SQLITE_INTERRUPT || Code==SQLITE_NOMEM || Code==SQLITE_TOOBIG || Code==SQLITE_FULL)
        throw Failure{LUI_NATIVE_LIMIT_EXCEEDED};
    if (Limits.Live && ((Code&255)==SQLITE_IOERR || Code==SQLITE_CANTOPEN || Code==SQLITE_CORRUPT || Code==SQLITE_NOTADB))
        throw Failure{LUI_NATIVE_PROVIDER_FAILURE};
    if (Code==SQLITE_BUSY || Code==SQLITE_LOCKED) throw Failure{LUI_NATIVE_WOULD_BLOCK};
    throw Failure{LUI_NATIVE_INVALID_ARGUMENT};
}
void Bind(sqlite3_stmt* Statement, int Index, Reader& Input, Budget& Limits) {
    const auto Type=Input.Byte();
    if (Type==0) { Sql(sqlite3_bind_null(Statement,Index),Limits); return; }
    auto Value=Input.Text();
    if (Type==1) {
        Check(Value.size()==8);
        double Number;
        std::memcpy(&Number,Value.data(),8);
        Check(std::isfinite(Number));
        if (std::trunc(Number)==Number && std::abs(Number)<=9007199254740991.0)
            Sql(sqlite3_bind_int64(Statement,Index,static_cast<sqlite3_int64>(Number)),Limits);
        else Sql(sqlite3_bind_double(Statement,Index,Number),Limits);
    } else if (Type==2) Sql(sqlite3_bind_text(Statement,Index,Value.data(),static_cast<int>(Value.size()),SQLITE_STATIC),Limits);
    else if (Type==3) Sql(sqlite3_bind_blob(Statement,Index,Value.data(),static_cast<int>(Value.size()),SQLITE_STATIC),Limits);
    else if (Type==4) {
        sqlite3_int64 Number=0;
        const char* End=Value.data()+Value.size();
        auto Parsed=std::from_chars(Value.data(),End,Number);
        Check(Parsed.ec==std::errc{} && Parsed.ptr==End);
        Sql(sqlite3_bind_int64(Statement,Index,Number),Limits);
    } else throw Failure{LUI_NATIVE_INVALID_ARGUMENT};
}
void Cell(Writer& Out, sqlite3_stmt* Statement, int Column) {
    const auto Type=sqlite3_column_type(Statement,Column);
    if (Type==SQLITE_NULL) { Out.Byte(0); return; }
    if (Type==SQLITE_INTEGER) {
        auto Value=std::to_string(sqlite3_column_int64(Statement,Column));
        Out.Byte(4); Out.Text(Value.data(),Value.size()); return;
    }
    if (Type==SQLITE_FLOAT) {
        const auto Value=sqlite3_column_double(Statement,Column);
        Check(std::isfinite(Value)); Out.Byte(1); Out.Text(&Value,sizeof(Value)); return;
    }
    const bool Text=Type==SQLITE_TEXT;
    Out.Byte(Text?2:3);
    const void* Value=Text ? static_cast<const void*>(sqlite3_column_text(Statement,Column)) : sqlite3_column_blob(Statement,Column);
    const int Count=sqlite3_column_bytes(Statement,Column);
    Check(Value || !Count,LUI_NATIVE_LIMIT_EXCEEDED);
    Out.Text(Value,static_cast<size_t>(Count));
}
void Batch(sqlite3* Db, Reader& Input, Writer& Rows, Budget& Limits) {
    const unsigned Groups=Input.Byte();
    Check(Groups>=1 && Groups<=32);
    unsigned Runs=0, TotalRows=0;
    Rows.Word(0);
    for (unsigned Group=0;Group<Groups;++Group) {
        auto Query=Input.Text();
        Check(!Query.empty() && Query.size()<=8192 && Query.find('\0')==Query.npos);
        sqlite3_stmt* Raw=nullptr;
        const char* Tail=nullptr;
        int Code=sqlite3_prepare_v3(Db,Query.data(),static_cast<int>(Query.size()),SQLITE_PREPARE_NO_VTAB,&Raw,&Tail);
        std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)> Statement(Raw,sqlite3_finalize);
        Sql(Code,Limits); Check(Raw);
        for (auto Position=Tail;Position<Query.data()+Query.size();++Position)
            Check(*Position==' ' || *Position=='\r' || *Position=='\n' || *Position=='\t');
        const unsigned Repeats=Input.Byte();
        Check(Repeats && Repeats<=64-Runs,LUI_NATIVE_LIMIT_EXCEEDED);
        Runs+=Repeats;
        for (unsigned Repeat=0;Repeat<Repeats;++Repeat) {
            Check(!Limits.Canceled,LUI_NATIVE_CANCELED);
            const auto Count=Input.Byte();
            Check(Count==static_cast<unsigned>(sqlite3_bind_parameter_count(Raw)));
            for (unsigned Index=0;Index<Count;++Index) Bind(Raw,static_cast<int>(Index+1),Input,Limits);
            const int Columns=sqlite3_column_count(Raw);
            Check(Columns<=16,LUI_NATIVE_LIMIT_EXCEEDED);
            Rows.Byte(Columns);
            for (int Index=0;Index<Columns;++Index) {
                const char* Name=sqlite3_column_name(Raw,Index); Check(Name,LUI_NATIVE_LIMIT_EXCEEDED);
                Rows.Text(Name,std::strlen(Name));
            }
            const auto RowOffset=Rows.Bytes.size();
            Rows.Word(0); unsigned CountRows=0;
            while ((Code=sqlite3_step(Raw))==SQLITE_ROW) {
                Check(++TotalRows<=256,LUI_NATIVE_LIMIT_EXCEEDED); ++CountRows;
                for (int Index=0;Index<Columns;++Index) Cell(Rows,Raw,Index);
            }
            if (Code!=SQLITE_DONE) Sql(Code,Limits);
            Rows.Bytes[RowOffset]=static_cast<uint8_t>(CountRows);
            Rows.Bytes[RowOffset+1]=static_cast<uint8_t>(CountRows>>8);
            Sql(sqlite3_reset(Raw),Limits);
            Sql(sqlite3_clear_bindings(Raw),Limits);
        }
    }
    Check(!Input.Left);
    Rows.Bytes[0]=static_cast<uint8_t>(Runs);
}
}
const LuiNativeServiceV2 Service{sizeof(Service),2,"SQLite",Methods,3,0,3,1,0};
uint32_t ProcessLive(LiveStore* Store,uint32_t Method,const uint8_t* Bytes,size_t Size,const std::atomic<bool>& Canceled,std::vector<uint8_t>& Output) {
    try {
        Check(Method<=2 && Size>=4 && Size<=(Method?0:MaxImage)+MaxPacket+4);
        if (Method) Check(Store,LUI_NATIVE_CAPABILITY_DENIED);
        Reader Input{Bytes,Size};
        const auto* Header=Input.Take(4);
        const uint32_t Argument=uint32_t(Header[0]) | uint32_t(Header[1])<<8 | uint32_t(Header[2])<<16 | uint32_t(Header[3])<<24;
        const size_t ImageSize=Method?0:Argument;
        Check(ImageSize<=MaxImage && (!Method || Argument<=5000));
        const auto* Image=Input.Take(ImageSize);
        Check(Input.Left<=MaxPacket);
        Budget Limits{Canceled,Method!=0};
        sqlite3* Raw=nullptr;
        struct HeldLock {
            LiveStore* Owner=nullptr;
            ~HeldLock() { if (Owner) Owner->Unlock(); }
        } Gate;
        if (Method) {
            const uint32_t Status=Store->Open(Argument,Method==1,Canceled,&Raw);
            Check(Status==LUI_NATIVE_OK,Status); Gate.Owner=Store;
        }
        const int Opened=Method?SQLITE_OK:sqlite3_open_v2(":memory:",&Raw,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_MEMORY|SQLITE_OPEN_PRIVATECACHE|SQLITE_OPEN_NOMUTEX,nullptr);
        std::unique_ptr<sqlite3,decltype(&sqlite3_close)> Db(Raw,sqlite3_close);
        Sql(Opened,Limits);
        sqlite3_progress_handler(Raw,1000,Progress,&Limits);
        Sql(sqlite3_db_config(Raw,SQLITE_DBCONFIG_DEFENSIVE,1,nullptr),Limits);
        Sql(sqlite3_db_config(Raw,SQLITE_DBCONFIG_ENABLE_FKEY,1,nullptr),Limits);
        Sql(sqlite3_db_config(Raw,SQLITE_DBCONFIG_TRUSTED_SCHEMA,0,nullptr),Limits);
        Sql(sqlite3_db_config(Raw,SQLITE_DBCONFIG_ENABLE_TRIGGER,0,nullptr),Limits);
        Sql(sqlite3_db_config(Raw,SQLITE_DBCONFIG_ENABLE_VIEW,0,nullptr),Limits);
        sqlite3_limit(Raw,SQLITE_LIMIT_ATTACHED,0);
        sqlite3_limit(Raw,SQLITE_LIMIT_VDBE_OP,20000);
        if (ImageSize) {
            Check(ImageSize>=4096 && !(ImageSize%4096) && std::memcmp(Image,"SQLite format 3\0",16)==0 &&
                Image[16]==16 && Image[17]==0 && Image[18]==1 && Image[19]==1);
            auto* Copy=static_cast<unsigned char*>(sqlite3_malloc64(MaxImage+20));
            Check(Copy,LUI_NATIVE_LIMIT_EXCEEDED);
            std::memcpy(Copy,Image,ImageSize);
            std::memset(Copy+ImageSize,0,MaxImage+20-ImageSize);
            Sql(sqlite3_deserialize(Raw,"main",Copy,ImageSize,MaxImage,SQLITE_DESERIALIZE_FREEONCLOSE),Limits);
        } else if (!Method) {
            Sql(sqlite3_exec(Raw,"PRAGMA page_size=4096; PRAGMA user_version=0",nullptr,nullptr,nullptr),Limits);
        }
        Sql(sqlite3_exec(Raw,Method ?
            "PRAGMA page_size=4096; PRAGMA max_page_count=32768; PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA temp_store=MEMORY" :
            "PRAGMA max_page_count=256",nullptr,nullptr,nullptr),Limits);
        if (Method) {
            sqlite3_stmt* Page=nullptr;
            int Code=sqlite3_prepare_v2(Raw,"PRAGMA page_size",-1,&Page,nullptr);
            std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)> Owner(Page,sqlite3_finalize);
            Sql(Code,Limits); Check(sqlite3_step(Page)==SQLITE_ROW && sqlite3_column_int(Page,0)==4096,LUI_NATIVE_PROVIDER_FAILURE);
        }
        if (Method==2) {
            Check(!Input.Left);
            sqlite3_stmt* Statement=nullptr;
            int Code=sqlite3_prepare_v2(Raw,"PRAGMA integrity_check(1)",-1,&Statement,nullptr);
            std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)> Owner(Statement,sqlite3_finalize);
            Sql(Code,Limits); Code=sqlite3_step(Statement);
            if (Code!=SQLITE_ROW) Sql(Code,Limits);
            const auto* Result=sqlite3_column_text(Statement,0);
            Check(Result && sqlite3_column_bytes(Statement,0)==2 && !std::memcmp(Result,"ok",2),LUI_NATIVE_PROVIDER_FAILURE);
            Code=sqlite3_step(Statement);
            if (Code!=SQLITE_DONE) Sql(Code,Limits);
            Output.push_back(1); return LUI_NATIVE_OK;
        }
        const unsigned Mode=Input.Byte(); Check(Mode<=2);
        Sql(sqlite3_exec(Raw,Mode==0?"BEGIN DEFERRED":Mode==1?"BEGIN IMMEDIATE":"BEGIN EXCLUSIVE",nullptr,nullptr,nullptr),Limits);
        Sql(sqlite3_set_authorizer(Raw,Authorize,nullptr),Limits);
        Writer Rows;
        Batch(Raw,Input,Rows,Limits);
        Check(!Canceled,LUI_NATIVE_CANCELED);
        Sql(sqlite3_set_authorizer(Raw,nullptr,nullptr),Limits);
        if (Method) {
            // Allocate before committing so an output allocation failure rolls back.
            Output.assign(4,0); Output.insert(Output.end(),Rows.Bytes.begin(),Rows.Bytes.end());
        }
        Sql(sqlite3_exec(Raw,"COMMIT",nullptr,nullptr,nullptr),Limits);
        if (Method) return LUI_NATIVE_OK;
        sqlite3_int64 Length=0;
        auto* Result=sqlite3_serialize(Raw,"main",&Length,0);
        std::unique_ptr<unsigned char,decltype(&sqlite3_free)> Owner(Result,sqlite3_free);
        Check(Length>=0 && Length<=static_cast<sqlite3_int64>(MaxImage) && (Result || !Length),LUI_NATIVE_LIMIT_EXCEEDED);
        for (unsigned Shift=0;Shift<32;Shift+=8) Output.push_back(static_cast<uint8_t>(Length>>Shift));
        if (Length) Output.insert(Output.end(),Result,Result+Length);
        Output.insert(Output.end(),Rows.Bytes.begin(),Rows.Bytes.end());
        return LUI_NATIVE_OK;
    } catch (Failure Error) { return Error.Status; }
}
uint32_t Process(uint32_t Method,const uint8_t* Bytes,size_t Size,const std::atomic<bool>& Canceled,std::vector<uint8_t>& Output) {
    if (Method) return LUI_NATIVE_INVALID_ARGUMENT;
    return ProcessLive(nullptr,0,Bytes,Size,Canceled,Output);
}
}
