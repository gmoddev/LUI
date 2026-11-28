#include "Backend.h"
#include "../../runtime/assets/ImageLimits.h"
#include <algorithm>
#include <condition_variable>
#include <list>
#include <mutex>
#include <stdexcept>

namespace Lui::Gtk {
namespace {
constexpr size_t MaxEntries = 2048, MaxInactiveEntries = 1024;
constexpr size_t MaxInactiveBytes = 8*1024*1024;
constexpr size_t IconBytes = 64*64*4, MaxReusableBytes = 1024*1024;

struct TextureRelease {
    void operator()(GdkTexture* Texture) const {g_object_unref(Texture);}
};
using TexturePtr = std::unique_ptr<GdkTexture,TextureRelease>;

// The paintable owns a reference to the shared texture and describes its crop
// as draw geometry. GTK's picture keeps the crop's own intrinsic aspect ratio.
struct LuiImageRegion {
    GObject Parent;
    GdkTexture* Texture;
    int X,Y,Width,Height;
};
struct LuiImageRegionClass {GObjectClass Parent;};

void ImageRegionInterface(GdkPaintableInterface* Interface) {
    Interface->get_flags=+[](GdkPaintable*) {
        return static_cast<GdkPaintableFlags>(GDK_PAINTABLE_STATIC_SIZE | GDK_PAINTABLE_STATIC_CONTENTS);
    };
    Interface->get_intrinsic_width=+[](GdkPaintable* Paintable) {
        return reinterpret_cast<LuiImageRegion*>(Paintable)->Width;
    };
    Interface->get_intrinsic_height=+[](GdkPaintable* Paintable) {
        return reinterpret_cast<LuiImageRegion*>(Paintable)->Height;
    };
    Interface->snapshot=+[](GdkPaintable* Paintable,GdkSnapshot* Output,double Width,double Height) {
        const auto& Region=*reinterpret_cast<LuiImageRegion*>(Paintable);
        auto* Snapshot=GTK_SNAPSHOT(Output);
        const double ScaleX=Width/Region.Width,ScaleY=Height/Region.Height;
        graphene_rect_t Clip,Image;
        graphene_rect_init(&Clip,0,0,Width,Height);
        graphene_rect_init(&Image,-Region.X*ScaleX,-Region.Y*ScaleY,
            gdk_texture_get_width(Region.Texture)*ScaleX,gdk_texture_get_height(Region.Texture)*ScaleY);
        gtk_snapshot_push_clip(Snapshot,&Clip);
        gtk_snapshot_append_texture(Snapshot,Region.Texture,&Image);
        gtk_snapshot_pop(Snapshot);
    };
}
G_DEFINE_TYPE_WITH_CODE(LuiImageRegion,lui_image_region,G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(GDK_TYPE_PAINTABLE,ImageRegionInterface))
void lui_image_region_init(LuiImageRegion*) {}
void lui_image_region_class_init(LuiImageRegionClass* Class) {
    G_OBJECT_CLASS(Class)->finalize=+[](GObject* Object) {
        auto* Region=reinterpret_cast<LuiImageRegion*>(Object);
        g_clear_object(&Region->Texture);
        G_OBJECT_CLASS(lui_image_region_parent_class)->finalize(Object);
    };
}

TexturePtr DecodeTexture(std::string_view Encoded,uint32_t Width,uint32_t Height) {
    auto* Bytes=g_bytes_new_static(Encoded.data(),Encoded.size());
    GError* Error=nullptr;
    TexturePtr Texture(gdk_texture_new_from_bytes(Bytes,&Error));
    g_bytes_unref(Bytes);
    if(Error) g_error_free(Error);
    if(!Texture || gdk_texture_get_width(Texture.get())!=static_cast<int>(Width) ||
        gdk_texture_get_height(Texture.get())!=static_cast<int>(Height)) return {};
    // The normalized texture owns exactly one four-byte-per-pixel allocation.
    const size_t Size=size_t(Width)*Height*4;
    auto* Pixels=static_cast<guchar*>(g_malloc(Size));
    gdk_texture_download(Texture.get(),Pixels,size_t(Width)*4);
    auto* Normalized=g_bytes_new_take(Pixels,Size);
    TexturePtr Result(gdk_memory_texture_new(Width,Height,GDK_MEMORY_DEFAULT,Normalized,size_t(Width)*4));
    g_bytes_unref(Normalized);
    return Result;
}
TexturePtr CreatePixelTexture(const LuiImageSourceViewV1& View) {
    // Copy once into a native-owned texture. An inactive cache entry must
    // never hold the last source lease and prevent future source admission.
    auto* Bytes=g_bytes_new(View.Data,View.ByteCount);
    TexturePtr Texture(gdk_memory_texture_new(View.Width,View.Height,GDK_MEMORY_B8G8R8A8_PREMULTIPLIED,Bytes,View.RowStride));
    g_bytes_unref(Bytes);
    return Texture;
}
}

struct Backend::ImageSession {
    struct Flight {
        std::string Key;
        std::shared_ptr<const std::string> Encoded;
        std::shared_ptr<LuiImageSource> Source;
        std::set<uint64_t> Tokens; // Owner-thread only.
        bool Started=false; // Protected by Mutex.
    };
    struct Ready {std::shared_ptr<Flight> Decode; TexturePtr Texture; size_t Bytes;};
    struct Cached {
        std::string Key;
        TexturePtr Texture;
        size_t Bytes,Pins=0;
        std::list<Cached*>::iterator Inactive;
    };
    struct Request {int Id; Cached* Hit=nullptr; std::shared_ptr<Flight> Decode;};
    struct Tier {std::list<Cached*> Entries; size_t Bytes=0;};

    std::mutex Mutex;
    std::condition_variable Wake;
    std::deque<std::shared_ptr<Flight>> Jobs;
    std::deque<Ready> Completed;
    std::vector<std::thread> Workers;
    size_t CompletedBytes=0;
    uint64_t DecodeCount=0,DecodeNanoseconds=0;
    bool Stopping=false;

    std::map<uint64_t,Request> Requests;
    std::map<std::string,std::shared_ptr<Flight>> Flights;
    std::map<std::string,Cached> Cache;
    Tier Icons,Thumbnails;
    size_t CacheBytes=0,PeakCacheBytes=0;
    uint64_t Hits=0,Misses=0,Coalesced=0,Canceled=0,Evictions=0,Retired=0;

    Tier& InactiveTier(const Cached& Entry) {return Entry.Bytes<=IconBytes?Icons:Thumbnails;}
    void RemoveInactive(Cached& Entry) {
        auto& Tier=InactiveTier(Entry);
        Tier.Entries.erase(Entry.Inactive);
        Tier.Bytes-=Entry.Bytes;
    }
    void Pin(Cached& Entry) {
        if(Entry.Pins++==0) RemoveInactive(Entry);
    }
    void Forget(Cached& Entry) {
        CacheBytes-=Entry.Bytes;
        ++Retired;
        const auto Position=Cache.find(Entry.Key);
        Cache.erase(Position);
    }
    void Evict(Cached& Entry) {
        ++Evictions;
        RemoveInactive(Entry);
        Forget(Entry);
    }
    void Unpin(Cached& Entry) {
        if(--Entry.Pins!=0) return;
        if(Entry.Bytes>MaxReusableBytes) {Forget(Entry); return;}
        auto& Tier=InactiveTier(Entry);
        try {Entry.Inactive=Tier.Entries.insert(Tier.Entries.end(),&Entry);}
        catch(const std::bad_alloc&) {Forget(Entry); return;}
        Tier.Bytes+=Entry.Bytes;
        while(Tier.Bytes>MaxInactiveBytes || Tier.Entries.size()>MaxInactiveEntries) Evict(*Tier.Entries.front());
    }
    Cached* Admit(Ready& Result) {
        if(!Result.Texture) return nullptr;
        if(CacheBytes-Icons.Bytes-Thumbnails.Bytes+Result.Bytes>LuiImages::MaxCache ||
            Cache.size()-Icons.Entries.size()-Thumbnails.Entries.size()>=MaxEntries) return nullptr;
        while(Cache.size()>=MaxEntries || CacheBytes+Result.Bytes>LuiImages::MaxCache) {
            if(!Thumbnails.Entries.empty()) Evict(*Thumbnails.Entries.front());
            else if(!Icons.Entries.empty()) Evict(*Icons.Entries.front());
            else return nullptr;
        }
        const auto& Key=Result.Decode->Key;
        try {
            auto Entry=Cache.emplace(Key,Cached{Key,std::move(Result.Texture),Result.Bytes,1}).first;
            CacheBytes+=Result.Bytes;
            PeakCacheBytes=std::max(PeakCacheBytes,CacheBytes);
            return &Entry->second;
        } catch(const std::bad_alloc&) {return nullptr;}
    }
};

void Backend::StartImages() {
    auto Session=std::make_shared<ImageSession>();
    ImageWork=Session;
    LuiImageCallbacksV1 Callbacks{sizeof(Callbacks),1,this,LoadImage,CancelImage};
    if(!Lui_SetImageCallbacks(Runtime,&Callbacks)) throw std::runtime_error("[LUI:Image] BridgeFailed");
    LuiImageSourceCallbacksV1 Sources{sizeof(Sources),1,this,LoadImageSource};
    if(!Lui_SetImageSourceCallbacksV1(Runtime,&Sources)) throw std::runtime_error("[LUI:Image] SourceBridgeFailed");
    for(int Index=0;Index<2;++Index) Session->Workers.emplace_back([Session] {
        for(;;) {
            std::shared_ptr<ImageSession::Flight> Job;
            {
                std::unique_lock Lock(Session->Mutex);
                Session->Wake.wait(Lock,[&]{return Session->Stopping || !Session->Jobs.empty();});
                if(Session->Stopping) return;
                Job=std::move(Session->Jobs.front());
                Session->Jobs.pop_front();
                Job->Started=true;
            }
            uint32_t Width=0,Height=0;
            const auto Started=std::chrono::steady_clock::now();
            LuiImageSourceViewV1 View{sizeof(View),1};
            TexturePtr Texture;
            bool Decoded=false;
            if(Job->Source && Lui_ImageSourceViewV1(Job->Source.get(),&View)) {
                Width=View.Width;
                Height=View.Height;
                if(View.Kind==2) Texture=CreatePixelTexture(View);
                else if(View.Kind==1) {
                    Decoded=true;
                    Texture=DecodeTexture({static_cast<const char*>(View.Data),size_t(View.ByteCount)},Width,Height);
                }
            } else if(Job->Encoded && LuiImages::Dimensions(*Job->Encoded,Width,Height)) {
                Decoded=true;
                Texture=DecodeTexture(*Job->Encoded,Width,Height);
            }
            const auto Duration=std::chrono::steady_clock::now()-Started;
            const size_t Bytes=Texture?size_t(Width)*Height*4:0;
            std::unique_lock Lock(Session->Mutex);
            if(Decoded) {
                ++Session->DecodeCount;
                Session->DecodeNanoseconds+=std::chrono::duration_cast<std::chrono::nanoseconds>(Duration).count();
            }
            Session->Wake.wait(Lock,[&]{return Session->Stopping || Session->CompletedBytes+Bytes<=LuiImages::MaxCache;});
            if(Session->Stopping) return;
            Session->CompletedBytes+=Bytes;
            Session->Completed.push_back({std::move(Job),std::move(Texture),Bytes});
        }
    });
}
void Backend::ReleaseImage(View& Target) {
    if(Target.ClassName!="ImageLabel") return;
    gtk_picture_set_paintable(GTK_PICTURE(Target.Native),nullptr);
    if(ImageWork && !Target.ImageKey.empty()) {
        auto Entry=ImageWork->Cache.find(Target.ImageKey);
        if(Entry!=ImageWork->Cache.end()) ImageWork->Unpin(Entry->second);
    }
    Target.ImageKey.clear();
}
void Backend::PresentImage(View& Target) {
    if(!ImageWork || Target.ImageKey.empty()) return;
    const auto Entry=ImageWork->Cache.find(Target.ImageKey);
    if(Entry==ImageWork->Cache.end()) return;
    auto* Picture=GTK_PICTURE(Target.Native);
    auto* Texture=Entry->second.Texture.get();
    if(Target.ImageWidth==0) {
        gtk_picture_set_paintable(Picture,GDK_PAINTABLE(Texture));
        return;
    }
    if(Target.ImageX+Target.ImageWidth>gdk_texture_get_width(Texture) ||
        Target.ImageY+Target.ImageHeight>gdk_texture_get_height(Texture)) {
        gtk_picture_set_paintable(Picture,nullptr);
        return;
    }
    auto* Region=reinterpret_cast<LuiImageRegion*>(g_object_new(lui_image_region_get_type(),nullptr));
    Region->Texture=GDK_TEXTURE(g_object_ref(Texture));
    Region->X=Target.ImageX;
    Region->Y=Target.ImageY;
    Region->Width=Target.ImageWidth;
    Region->Height=Target.ImageHeight;
    gtk_picture_set_paintable(Picture,GDK_PAINTABLE(Region));
    g_object_unref(Region);
}
int Backend::LoadImage(void* Context,int Id,uint64_t Token,const char* Source) {
    auto& Self=*static_cast<Backend*>(Context);
    if(!Self.Owner()) return 0;
    try {
        auto Asset=Self.Assets.find(Source);
        if(Asset==Self.Assets.end()) return 0;
        return Self.AcceptImage(Id,Token,Source,Asset->second,nullptr);
    }
    catch(...) {CancelImage(Context,Token); return 0;}
}
int Backend::LoadImageSource(void* Context,int Id,uint64_t Token,LuiImageSource* Source) {
    auto& Self=*static_cast<Backend*>(Context);
    LuiImageSourceViewV1 View{sizeof(View),1};
    if(!Lui_ImageSourceViewV1(Source,&View)) return 0;
    try {return Self.AcceptImage(Id,Token,std::string(1,'\0')+std::to_string(View.Identity),{},Source);}
    catch(...) {CancelImage(Context,Token); return 0;}
}
int Backend::AcceptImage(int Id,uint64_t Token,const std::string& Key,std::shared_ptr<const std::string> Encoded,LuiImageSource* Source) {
    if(!Owner() || Stopping || !ImageWork) return 0;
    auto Target=Views.find(Id);
    auto Session=ImageWork;
    if(Target==Views.end() || Session->Requests.size()>=32) return 0;
    auto Cached=Session->Cache.find(Key);
    auto Pending=Session->Flights.find(Key);
    if(Cached==Session->Cache.end() && Pending==Session->Flights.end() && Session->Flights.size()>=32) return 0;
    auto& Request=Session->Requests.emplace(Token,ImageSession::Request{Id}).first->second;
    Target->second->ImageToken=Token;
    if(Cached!=Session->Cache.end()) {
        ++Session->Hits;
        Request.Hit=&Cached->second;
        Session->Pin(*Request.Hit);
        return 1;
    }
    ++Session->Misses;
    if(Pending!=Session->Flights.end()) {
        ++Session->Coalesced;
        Request.Decode=Pending->second;
        Request.Decode->Tokens.insert(Token);
        return 1;
    }
    auto Decode=std::make_shared<ImageSession::Flight>();
    Decode->Key=Key;
    Decode->Encoded=std::move(Encoded);
    if(Source) {
        Lui_RetainImageSourceV1(Source);
        Decode->Source=std::shared_ptr<LuiImageSource>(Source,Lui_ReleaseImageSourceV1);
    }
    Decode->Tokens.insert(Token);
    Session->Flights.emplace(Key,Decode);
    Request.Decode=Decode;
    {
        std::lock_guard Lock(Session->Mutex);
        Session->Jobs.push_back(std::move(Decode));
    }
    Session->Wake.notify_one();
    return 1;
}
void Backend::CancelImage(void* Context,uint64_t Token) {
    auto& Self=*static_cast<Backend*>(Context);
    if(!Self.ImageWork) return;
    auto Session=Self.ImageWork;
    auto Found=Session->Requests.find(Token);
    if(Found==Session->Requests.end()) return;
    auto Request=std::move(Found->second);
    Session->Requests.erase(Found);
    ++Session->Canceled;
    if(Request.Hit) Session->Unpin(*Request.Hit);
    if(auto Decode=Request.Decode) {
        Decode->Tokens.erase(Token);
        if(Decode->Tokens.empty()) {
            std::lock_guard Lock(Session->Mutex);
            if(!Decode->Started) {
                auto Job=std::find(Session->Jobs.begin(),Session->Jobs.end(),Decode);
                if(Job!=Session->Jobs.end()) Session->Jobs.erase(Job);
                Session->Flights.erase(Decode->Key);
            }
        }
    }
    auto Target=Self.Views.find(Request.Id);
    if(Target!=Self.Views.end() && Target->second->ImageToken==Token) Target->second->ImageToken=0;
}
void Backend::PumpImages() {
    if(!ImageWork || Stopping) return;
    auto Session=ImageWork;
    auto Complete=[&](uint64_t Token,ImageSession::Cached* Value) {
        auto Pending=Session->Requests.find(Token);
        if(Pending==Session->Requests.end()) return;
        const int Id=Pending->second.Id;
        auto* Reserved=Pending->second.Hit;
        Session->Requests.erase(Pending);
        auto Target=Views.find(Id);
        const bool Current=Target!=Views.end() && Target->second->ImageToken==Token;
        if(Current) {
            Target->second->ImageToken=0;
            if(Value) {
                Session->Pin(*Value);
                Target->second->ImageKey=Value->Key;
                PresentImage(*Target->second);
            }
        }
        if(Reserved) Session->Unpin(*Reserved);
        if(Current) Lui_ImageCompleted(Runtime,Id,Token,Value?1:0);
    };
    std::vector<uint64_t> Warm;
    for(const auto& Pair:Session->Requests)
        if(Pair.second.Hit) Warm.push_back(Pair.first);
    std::deque<ImageSession::Ready> Ready;
    {
        std::lock_guard Lock(Session->Mutex);
        Ready.swap(Session->Completed);
    }
    for(uint64_t Token:Warm) {
        if(Stopping) break;
        auto Request=Session->Requests.find(Token);
        if(Request!=Session->Requests.end()) Complete(Token,Request->second.Hit);
    }
    for(auto& Result:Ready) {
        auto Decode=Result.Decode;
        auto* Cached=Stopping || Decode->Tokens.empty()?nullptr:Session->Admit(Result);
        Session->Flights.erase(Decode->Key);
        Result.Texture.reset();
        {
            std::lock_guard Lock(Session->Mutex);
            Session->CompletedBytes-=Result.Bytes;
        }
        Session->Wake.notify_all();
        // Callbacks can cancel another token or change this view's Source.
        const auto Tokens=std::move(Decode->Tokens);
        for(uint64_t Token:Tokens) {
            if(Stopping) break;
            Complete(Token,Cached);
        }
        if(Cached && !Stopping) Session->Unpin(*Cached);
    }
}
void Backend::StopImages() {
    if(!ImageWork) return;
    auto Session=ImageWork;
    {
        std::lock_guard Lock(Session->Mutex);
        Session->Stopping=true;
    }
    Session->Wake.notify_all();
    for(auto& Worker:Session->Workers) if(Worker.joinable()) Worker.join();
    for(auto& Result:Session->Completed) Session->CompletedBytes-=Result.Bytes;
    Session->Completed.clear();
    Session->Jobs.clear();
    Session->Requests.clear();
    Session->Flights.clear();
    for(auto& Pair:Views) ReleaseImage(*Pair.second);
    Session->Icons={};
    Session->Thumbnails={};
    Session->Retired+=Session->Cache.size();
    Session->Cache.clear();
    Session->CacheBytes=0;
}
size_t Backend::ImageCacheUsage() const {return ImageWork?ImageWork->CacheBytes:0;}
size_t Backend::ImageRequestCount() const {return ImageWork?ImageWork->Requests.size():0;}
size_t Backend::ImageStagingUsage() const {
    if(!ImageWork) return 0;
    std::lock_guard Lock(ImageWork->Mutex);
    return ImageWork->CompletedBytes;
}
std::string Backend::ImageMetrics() const {
    if(!ImageWork || !Owner()) return {};
    const auto& Session=*ImageWork;
    uint64_t Count=0,Duration=0;
    size_t Staged=0;
    {
        std::lock_guard Lock(ImageWork->Mutex);
        Count=Session.DecodeCount;
        Duration=Session.DecodeNanoseconds;
        Staged=Session.CompletedBytes;
    }
    const size_t Inactive=Session.Icons.Entries.size()+Session.Thumbnails.Entries.size();
    return "decodes="+std::to_string(Count)+" decodeNs="+std::to_string(Duration)+
        " hits="+std::to_string(Session.Hits)+" misses="+std::to_string(Session.Misses)+
        " coalesced="+std::to_string(Session.Coalesced)+" canceled="+std::to_string(Session.Canceled)+
        " evictions="+std::to_string(Session.Evictions)+" active="+std::to_string(Session.Cache.size()-Inactive)+
        " inactive="+std::to_string(Inactive)+" retainedBytes="+std::to_string(Session.CacheBytes)+
        " peakBytes="+std::to_string(Session.PeakCacheBytes)+" encodedBytes="+std::to_string(AssetBytes)+
        " stagedBytes="+std::to_string(Staged)+" pending="+std::to_string(Session.Requests.size())+
        " retired="+std::to_string(Session.Retired);
}
}
