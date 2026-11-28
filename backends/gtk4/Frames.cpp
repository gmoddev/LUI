#include "Backend.h"
#include <stdexcept>

namespace Lui::Gtk {
namespace {
constexpr char PreparedTexture[]="lui-prepared-frame-texture";
constexpr char TextureFrame[]="lui-texture-frame";

void ReleaseFrame(gpointer Data) {
    Lui_FrameRelease(static_cast<LuiFrame*>(Data));
}
}

void Backend::StartFrames() {
    LuiFrameCallbacksV1 Callbacks{sizeof(Callbacks),1,this,SubmitFrame,PublishFrame,ClearFrame,0};
    if(!Lui_SetFrameCallbacks(Runtime,&Callbacks)) throw std::runtime_error("[LUI:GTK] FrameBridgeFailed");
}

GtkPicture* Backend::FindFrame(int Id) const {
    if(!Owner()) return nullptr;
    const auto Found=Views.find(Id);
    if(Found==Views.end() || !Found->second->Alive || Found->second->ClassName!="FrameSurface") return nullptr;
    return GTK_PICTURE(Found->second->Native);
}

int Backend::SubmitFrame(void* Context,int Id,LuiFrame* Frame,uint32_t) {
    auto& Self=*static_cast<Backend*>(Context);
    auto* Picture=Self.FindFrame(Id);
    LuiFrameViewV1 View{sizeof(View),1};
    if(!Picture || Self.Stopping || !Lui_FrameView(Frame,&View) || View.Format!=1 ||
        !View.Width || !View.Height || View.RowStride<static_cast<uint64_t>(View.Width)*4 ||
        View.ByteCount!=static_cast<uint64_t>(View.RowStride)*View.Height) return 0;

    // The toolkit can realign a strided image. Bound that native storage before handing it the lease.
    if(!Lui_FrameReserveStaging(Frame,View.ByteCount) || !Lui_FrameRetain(Frame)) return 0;
    auto* Bytes=g_bytes_new_with_free_func(View.Pixels,View.ByteCount,ReleaseFrame,Frame);
    auto* Texture=gdk_memory_texture_new(View.Width,View.Height,GDK_MEMORY_B8G8R8A8_PREMULTIPLIED,Bytes,View.RowStride);
    g_bytes_unref(Bytes);
    if(!Texture) return 0;
    g_object_set_data(G_OBJECT(Texture),TextureFrame,Frame);
    g_object_set_data_full(G_OBJECT(Picture),PreparedTexture,Texture,g_object_unref);
    if(Lui_FrameComplete(Frame,1)) return 1;
    g_object_set_data(G_OBJECT(Picture),PreparedTexture,nullptr);
    return 0;
}

int Backend::PublishFrame(void* Context,int Id,LuiFrame* Frame,uint32_t) {
    auto& Self=*static_cast<Backend*>(Context);
    auto* Picture=Self.FindFrame(Id);
    if(!Picture || Self.Stopping) return 0;
    auto* Texture=static_cast<GdkTexture*>(g_object_get_data(G_OBJECT(Picture),PreparedTexture));
    if(!Texture || g_object_get_data(G_OBJECT(Texture),TextureFrame)!=Frame) return 0;
    gtk_picture_set_paintable(Picture,GDK_PAINTABLE(Texture));
    g_object_set_data(G_OBJECT(Picture),PreparedTexture,nullptr);
    return 1;
}

void Backend::ClearFrame(void* Context,int Id,uint64_t) {
    auto& Self=*static_cast<Backend*>(Context);
    if(auto* Picture=Self.FindFrame(Id)) {
        g_object_set_data(G_OBJECT(Picture),PreparedTexture,nullptr);
        gtk_picture_set_paintable(Picture,nullptr);
    }
}
}
