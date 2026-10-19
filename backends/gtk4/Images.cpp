#include "Backend.h"
#include "../../runtime/assets/ImageLimits.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <algorithm>
#include <stdexcept>

namespace Lui::Gtk {
struct Backend::ImageSession {
    struct Job {int Id; uint64_t Token; std::string Key; std::shared_ptr<const std::string> Bytes; std::shared_ptr<std::atomic<bool>> Canceled;};
    struct Ready {Job Input; GdkTexture* Texture=nullptr; size_t HeldBytes=0; bool CachedPin=false;};
    struct Cached {GdkTexture* Texture; size_t Bytes,Pins=0; uint64_t Used=0;};
    std::mutex Mutex; std::condition_variable Wake;
    std::deque<Job> Jobs; std::deque<Ready> Completed;
    std::vector<std::thread> Workers;
    std::map<uint64_t,std::shared_ptr<std::atomic<bool>>> Requests;
    std::map<std::string,Cached> Cache;
    size_t CacheBytes=0; uint64_t Clock=0; bool Stopping=false;
    size_t CompletedBytes=0;
};
void Backend::StartImages() {
    auto Session=std::make_shared<ImageSession>(); ImageWork=Session;
    LuiImageCallbacksV1 Callbacks{sizeof(Callbacks),1,this,LoadImage,CancelImage};
    if(!Lui_SetImageCallbacks(Runtime,&Callbacks)) throw std::runtime_error("[LUI:Image] BridgeFailed");
    for(int Index=0;Index<2;++Index) Session->Workers.emplace_back([Session] {
        for(;;) {
            ImageSession::Job Job;
            {
                std::unique_lock Lock(Session->Mutex); Session->Wake.wait(Lock,[&]{return Session->Stopping || !Session->Jobs.empty();});
                if(Session->Stopping) return;
                Job=std::move(Session->Jobs.front()); Session->Jobs.pop_front();
            }
            GdkTexture* Texture=nullptr;
            if(!Job.Canceled->load()) {
                uint32_t Width=0,Height=0;
                if(LuiImages::Dimensions(*Job.Bytes,Width,Height)) {
                    auto* Bytes=g_bytes_new(Job.Bytes->data(),Job.Bytes->size()); GError* Error=nullptr;
                    Texture=gdk_texture_new_from_bytes(Bytes,&Error); g_bytes_unref(Bytes); if(Error) g_error_free(Error);
                    if(Texture && (gdk_texture_get_width(Texture)!=static_cast<int>(Width) || gdk_texture_get_height(Texture)!=static_cast<int>(Height))) {g_object_unref(Texture); Texture=nullptr;}
                    if(Texture) {
                        // Normalize high precision/native formats before cache accounting.
                        // Texture.download guarantees four bytes per pixel (ARGB32).
                        std::string Pixels(size_t(Width)*Height*4, '\0');
                        gdk_texture_download(Texture,reinterpret_cast<guchar*>(Pixels.data()),size_t(Width)*4);
                        auto* NormalizedBytes=g_bytes_new(Pixels.data(),Pixels.size());
                        auto* Normalized=gdk_memory_texture_new(Width,Height,GDK_MEMORY_DEFAULT,NormalizedBytes,size_t(Width)*4);
                        g_bytes_unref(NormalizedBytes); g_object_unref(Texture); Texture=Normalized;
                    }
                }
            }
            std::unique_lock Lock(Session->Mutex);
            const size_t Bytes=Texture?size_t(gdk_texture_get_width(Texture))*gdk_texture_get_height(Texture)*4:0;
            Session->Wake.wait(Lock,[&]{return Session->Stopping || Job.Canceled->load() || Session->CompletedBytes+Bytes<=LuiImages::MaxCache;});
            if(Session->Stopping || Job.Canceled->load()) {if(Texture) g_object_unref(Texture);}
            else {Session->CompletedBytes+=Bytes; Session->Completed.push_back({std::move(Job),Texture,Bytes});}
        }
    });
}
void Backend::ReleaseImage(View& Target) {
    if(ImageWork && !Target.ImageKey.empty()) {
        auto Found=ImageWork->Cache.find(Target.ImageKey); if(Found!=ImageWork->Cache.end() && Found->second.Pins) --Found->second.Pins;
    }
    Target.ImageKey.clear();
    if(Target.ClassName=="ImageLabel") gtk_picture_set_paintable(GTK_PICTURE(Target.Native),nullptr);
}
int Backend::LoadImage(void* Context,int Id,uint64_t Token,const char* Source) {
    auto& Self=*static_cast<Backend*>(Context); auto Found=Self.Views.find(Id);
    if(!Self.Owner() || Self.Stopping || !Self.ImageWork || Found==Self.Views.end()) return 0;
    Self.ReleaseImage(*Found->second);
    if(Self.ImageWork->Requests.size()>=32) return 0;
    auto Asset=Self.Assets.find(Source); if(Asset==Self.Assets.end()) return 0;
    auto& Target=*Found->second; Target.ImageToken=Token;
    auto Session=Self.ImageWork; auto Canceled=std::make_shared<std::atomic<bool>>(false); Session->Requests.emplace(Token,Canceled);
    ImageSession::Job Job{Id,Token,Source,Asset->second,Canceled};
    std::lock_guard Lock(Session->Mutex);
    const auto Cached=Session->Cache.find(Source);
    if(Cached!=Session->Cache.end()) {++Cached->second.Pins; g_object_ref(Cached->second.Texture); Session->Completed.push_back({std::move(Job),Cached->second.Texture,0,true});}
    else {Session->Jobs.push_back(std::move(Job)); Session->Wake.notify_one();}
    return 1;
}
void Backend::CancelImage(void* Context,uint64_t Token) {
    auto& Self=*static_cast<Backend*>(Context); if(!Self.ImageWork) return;
    auto& Requests=Self.ImageWork->Requests; auto Found=Requests.find(Token);
    if(Found!=Requests.end()) {Found->second->store(true); Requests.erase(Found);}
    std::lock_guard Lock(Self.ImageWork->Mutex);
    auto& Jobs=Self.ImageWork->Jobs;
    Jobs.erase(std::remove_if(Jobs.begin(),Jobs.end(),[&](const ImageSession::Job& Job){return Job.Token==Token;}),Jobs.end());
    auto& Ready=Self.ImageWork->Completed;
    for(auto Iterator=Ready.begin();Iterator!=Ready.end();) {
        if(Iterator->Input.Token==Token) {
            Self.ImageWork->CompletedBytes-=Iterator->HeldBytes;
            if(Iterator->CachedPin) {auto Entry=Self.ImageWork->Cache.find(Iterator->Input.Key); if(Entry!=Self.ImageWork->Cache.end() && Entry->second.Pins) --Entry->second.Pins;}
            if(Iterator->Texture) g_object_unref(Iterator->Texture);
            Iterator=Ready.erase(Iterator);
        } else ++Iterator;
    }
    for(auto& View:Self.Views) if(View.second->ImageToken==Token) View.second->ImageToken=0;
    Self.ImageWork->Wake.notify_all();
}
void Backend::PumpImages() {
    if(!ImageWork || Stopping) return;
    auto Session=ImageWork; std::deque<ImageSession::Ready> Ready;
    {std::lock_guard Lock(Session->Mutex); Ready.swap(Session->Completed);}
    auto ReleaseStaging=[&](ImageSession::Ready& Result) {
        if(Result.Texture) {g_object_unref(Result.Texture); Result.Texture=nullptr;}
        std::lock_guard Lock(Session->Mutex);
        Session->CompletedBytes-=Result.HeldBytes; Result.HeldBytes=0;
        Session->Wake.notify_all();
    };
    for(auto& Result:Ready) {
        if(Result.CachedPin) {auto Entry=Session->Cache.find(Result.Input.Key); if(Entry!=Session->Cache.end() && Entry->second.Pins) --Entry->second.Pins;}
        const auto Request=Session->Requests.find(Result.Input.Token); const auto Target=Views.find(Result.Input.Id);
        if(Request==Session->Requests.end() || Result.Input.Canceled->load() || Target==Views.end() || Target->second->ImageToken!=Result.Input.Token) {ReleaseStaging(Result); continue;}
        Session->Requests.erase(Request);
        bool Success=Result.Texture!=nullptr;
        if(Success) {
            auto Cached=Session->Cache.find(Result.Input.Key);
            if(Cached==Session->Cache.end()) {
                const size_t Bytes=size_t(gdk_texture_get_width(Result.Texture))*gdk_texture_get_height(Result.Texture)*4;
                while((Session->Cache.size()>=16 || Session->CacheBytes+Bytes>LuiImages::MaxCache) && !Session->Cache.empty()) {
                    auto Victim=Session->Cache.end();
                    for(auto Entry=Session->Cache.begin();Entry!=Session->Cache.end();++Entry)
                        if(!Entry->second.Pins && (Victim==Session->Cache.end() || Entry->second.Used<Victim->second.Used)) Victim=Entry;
                    if(Victim==Session->Cache.end()) break;
                    Session->CacheBytes-=Victim->second.Bytes; g_object_unref(Victim->second.Texture); Session->Cache.erase(Victim);
                }
                if(Session->Cache.size()>=16 || Session->CacheBytes+Bytes>LuiImages::MaxCache) Success=false;
                else {g_object_ref(Result.Texture); Cached=Session->Cache.emplace(Result.Input.Key,ImageSession::Cached{Result.Texture,Bytes,0,++Session->Clock}).first; Session->CacheBytes+=Bytes;}
            }
            if(Success) {
                ++Cached->second.Pins; Cached->second.Used=++Session->Clock;
                Target->second->ImageKey=Result.Input.Key; gtk_picture_set_paintable(GTK_PICTURE(Target->second->Native),GDK_PAINTABLE(Cached->second.Texture));
            }
        }
        ReleaseStaging(Result);
        Lui_ImageCompleted(Runtime,Result.Input.Id,Result.Input.Token,Success?1:0);
    }
}
void Backend::StopImages() {
    if(!ImageWork) return;
    auto Session=ImageWork;
    {std::lock_guard Lock(Session->Mutex); Session->Stopping=true; for(auto& Request:Session->Requests) Request.second->store(true);}
    Session->Wake.notify_all(); for(auto& Worker:Session->Workers) if(Worker.joinable()) Worker.join();
    for(auto& Result:Session->Completed) {
        if(Result.Texture) g_object_unref(Result.Texture);
        Session->CompletedBytes-=Result.HeldBytes;
    }
    Session->Completed.clear(); Session->Jobs.clear(); Session->Requests.clear();
    for(auto& Entry:Session->Cache) g_object_unref(Entry.second.Texture);
    Session->Cache.clear(); Session->CacheBytes=0;
}
size_t Backend::ImageCacheUsage() const {return ImageWork?ImageWork->CacheBytes:0;}
size_t Backend::ImageRequestCount() const {return ImageWork?ImageWork->Requests.size():0;}
size_t Backend::ImageStagingUsage() const {
    if(!ImageWork) return 0;
    std::lock_guard Lock(ImageWork->Mutex); return ImageWork->CompletedBytes;
}
}
