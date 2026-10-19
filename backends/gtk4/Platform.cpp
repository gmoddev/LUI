#include "Backend.h"
#include <cstring>
#include <memory>
#include <stdexcept>

namespace Lui::Gtk {
struct Backend::PlatformSession {
    Backend* Owner=nullptr;
    struct Request {
        std::shared_ptr<PlatformSession> Session;
        uint64_t Id;
        GCancellable* Cancel=g_cancellable_new();
        GInputStream* Stream=nullptr;
        GtkFileDialog* Dialog=nullptr;
        guint Deadline=0;
        std::string Bytes;
        ~Request() {if(Deadline) g_source_remove(Deadline); if(Stream) g_object_unref(Stream); if(Dialog) g_object_unref(Dialog); g_object_unref(Cancel);}
        void Finish(const char* Error=nullptr,bool HasResult=true) {
            auto* Owner=Session->Owner;
            if(Owner && Owner->Runtime && !Owner->Stopping)
                Lui_CompletePlatformRequest(Owner->Runtime,Id,Error || !HasResult?nullptr:Bytes.c_str(),Error);
            Session->Pending.erase(Id);
            delete this;
        }
        void Read() {
            g_input_stream_read_bytes_async(Stream,64*1024,G_PRIORITY_DEFAULT,Cancel,
                +[](GObject* Object,GAsyncResult* Result,gpointer Data) {
                    auto* Self=static_cast<Request*>(Data); GError* Error=nullptr;
                    GBytes* Chunk=g_input_stream_read_bytes_finish(G_INPUT_STREAM(Object),Result,&Error);
                    if(Error) {g_error_free(Error); if(Chunk) g_bytes_unref(Chunk); Self->Finish("[LUI:Platform] ClipboardReadFailed"); return;}
                    if(!Chunk) {Self->Finish("[LUI:Platform] ClipboardReadFailed"); return;}
                    gsize Length=0; const auto* Bytes=static_cast<const char*>(g_bytes_get_data(Chunk,&Length));
                    if(!Self->Session->Owner || Self->Bytes.size()+Length>1024*1024) {
                        g_bytes_unref(Chunk); Self->Finish("[LUI:Platform] LimitExceeded"); return;
                    }
                    try {if(Length) Self->Bytes.append(Bytes,Length);} catch(...) {
                        g_bytes_unref(Chunk); Self->Finish("[LUI:Platform] LimitExceeded"); return;
                    }
                    g_bytes_unref(Chunk);
                    if(Length) {Self->Read(); return;}
                    if(Self->Bytes.find('\0')!=std::string::npos || !g_utf8_validate(Self->Bytes.data(),Self->Bytes.size(),nullptr)) {
                        Self->Finish("[LUI:Platform] InvalidInput"); return;
                    }
                    Self->Finish();
                },this);
        }
    };
    std::map<uint64_t,Request*> Pending;
};
void Backend::StartPlatform() {
    Platform=std::make_shared<PlatformSession>(); Platform->Owner=this;
    LuiPlatformCallbacksV1 Callbacks{sizeof(Callbacks),1,this,WriteClipboard,ReadClipboard,OpenFile};
    if(!Lui_SetPlatformCallbacks(Runtime,&Callbacks)) throw std::runtime_error("[LUI:GTK] PlatformUnavailable");
}
void Backend::StopPlatform() {
    if(!Platform) return;
    Platform->Owner=nullptr;
    for(auto& Pair:Platform->Pending) g_cancellable_cancel(Pair.second->Cancel);
    // Callback contexts keep the retired session alive through asynchronous
    // completion; none owns a live runtime or backend pointer after this point.
    Platform.reset();
}
int Backend::WriteClipboard(void* Context,const char* Text) {
    auto& Self=*static_cast<Backend*>(Context);
    if(!Self.Owner() || Self.Stopping || !Text) return 0;
    const auto Length=strnlen(Text,1024*1024+1);
    if(Length>1024*1024 || !g_utf8_validate(Text,Length,nullptr)) return 0;
    auto* Display=gdk_display_get_default(); if(!Display) return 0;
    gdk_clipboard_set_text(gdk_display_get_clipboard(Display),Text); return 1;
}
int Backend::ReadClipboard(void* Context,uint64_t Id) {
    auto& Self=*static_cast<Backend*>(Context);
    if(!Self.Owner() || Self.Stopping || !Self.Platform || Self.Platform->Pending.size()>=32) return 0;
    auto* Display=gdk_display_get_default(); if(!Display) return 0;
    try {
        auto Value=std::make_unique<PlatformSession::Request>(); Value->Session=Self.Platform; Value->Id=Id;
        if(!Self.Platform->Pending.emplace(Id,Value.get()).second) return 0;
        static const char* Types[]{"text/plain;charset=utf-8","text/plain",nullptr};
        auto* Request=Value.release();
        Request->Deadline=g_timeout_add_seconds(10,+[](gpointer Data)->gboolean {
            auto* Self=static_cast<PlatformSession::Request*>(Data); Self->Deadline=0; g_cancellable_cancel(Self->Cancel); return G_SOURCE_REMOVE;
        },Request);
        gdk_clipboard_read_async(gdk_display_get_clipboard(Display),Types,G_PRIORITY_DEFAULT,Request->Cancel,
            +[](GObject* Object,GAsyncResult* Result,gpointer Data) {
                auto* Self=static_cast<PlatformSession::Request*>(Data); GError* Error=nullptr;
                Self->Stream=gdk_clipboard_read_finish(GDK_CLIPBOARD(Object),Result,nullptr,&Error);
                if(Error) {g_error_free(Error); Self->Finish("[LUI:Platform] ClipboardReadFailed"); return;}
                if(!Self->Stream || !Self->Session->Owner) {Self->Finish("[LUI:Platform] ClipboardReadFailed"); return;}
                Self->Read();
            },Request); return 1;
    } catch(...) {return 0;}
}
int Backend::OpenFile(void* Context,uint64_t Id) {
    auto& Self=*static_cast<Backend*>(Context);
    if(!Self.Owner() || Self.Stopping || !Self.Platform || Self.Platform->Pending.size()>=32) return 0;
    // Explicit application requests require a visible LUI parent. A single
    // chooser per generation prevents unbounded application-driven popups.
    for(const auto& Pair:Self.Platform->Pending) if(Pair.second->Dialog) return 0;
    GtkWindow* Parent=nullptr;
    for(const auto& Pair:Self.Views) {
        const auto& View=*Pair.second;
        if(View.Alive && !View.Closed && GTK_IS_WINDOW(View.Native) && gtk_widget_get_visible(View.Native)) {
            Parent=GTK_WINDOW(View.Native); if(gtk_window_is_active(Parent)) break;
        }
    }
    if(!Parent) return 0;
    try {
        auto Value=std::make_unique<PlatformSession::Request>(); Value->Session=Self.Platform; Value->Id=Id;
        Value->Dialog=gtk_file_dialog_new();
        gtk_file_dialog_set_title(Value->Dialog,"Open file");
        gtk_file_dialog_set_modal(Value->Dialog,false);
        if(!Self.Platform->Pending.emplace(Id,Value.get()).second) return 0;
        auto* Request=Value.release();
        gtk_file_dialog_open(Request->Dialog,Parent,Request->Cancel,
            +[](GObject* Object,GAsyncResult* Result,gpointer Data) {
                auto* Self=static_cast<PlatformSession::Request*>(Data); GError* Error=nullptr;
                GFile* File=gtk_file_dialog_open_finish(GTK_FILE_DIALOG(Object),Result,&Error);
                if(Error) {
                    const bool Dismissed=g_error_matches(Error,GTK_DIALOG_ERROR,GTK_DIALOG_ERROR_DISMISSED) ||
                        g_error_matches(Error,GTK_DIALOG_ERROR,GTK_DIALOG_ERROR_CANCELLED) ||
                        g_error_matches(Error,G_IO_ERROR,G_IO_ERROR_CANCELLED);
                    g_error_free(Error); if(File) g_object_unref(File);
                    Self->Finish(Dismissed?nullptr:"[LUI:Platform] DialogFailed",false); return;
                }
                char* Path=File?g_file_get_path(File):nullptr; if(File) g_object_unref(File);
                if(!Self->Session->Owner || !Path || strnlen(Path,65537)>65536 || !g_utf8_validate(Path,-1,nullptr)) {
                    g_free(Path); Self->Finish("[LUI:Platform] DialogFailed",false); return;
                }
                try {Self->Bytes=Path;} catch(...) {g_free(Path); Self->Finish("[LUI:Platform] LimitExceeded",false); return;}
                g_free(Path); Self->Finish();
            },Request); return 1;
    } catch(...) {return 0;}
}
}
