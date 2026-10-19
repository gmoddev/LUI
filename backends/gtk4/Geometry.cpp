#include "Geometry.h"
#include <algorithm>
#include <cmath>

struct ClipValue {bool Enabled; double X,Y,Width,Height;};
struct LuiSurface {GtkWidget Parent; ClipValue Clip; bool Background; GdkRGBA BackgroundColor,BorderColor; double BorderSize,Radius;};
struct LuiSurfaceClass {GtkWidgetClass Parent;};
struct LuiSlot {GtkWidget Parent; double X,Y,Width,Height; ClipValue Clip;};
struct LuiSlotClass {GtkWidgetClass Parent;};
G_DEFINE_TYPE(LuiSurface,lui_surface,GTK_TYPE_WIDGET)
G_DEFINE_TYPE(LuiSlot,lui_slot,GTK_TYPE_WIDGET)
struct LuiTextView {GtkTextView Parent; char* Hint; bool Preedit;};
struct LuiTextViewClass {GtkTextViewClass Parent;};
G_DEFINE_TYPE(LuiTextView,lui_text_view,GTK_TYPE_TEXT_VIEW)
static void TextViewDispose(GObject* Object) {
    auto* Value=reinterpret_cast<LuiTextView*>(Object); g_clear_pointer(&Value->Hint,g_free);
    G_OBJECT_CLASS(lui_text_view_parent_class)->dispose(Object);
}
static void TextViewSnapshot(GtkWidget* Widget,GtkSnapshot* Output) {
    GTK_WIDGET_CLASS(lui_text_view_parent_class)->snapshot(Widget,Output);
    auto* Value=reinterpret_cast<LuiTextView*>(Widget);
    if(!Value->Hint || !*Value->Hint || Value->Preedit || gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(GTK_TEXT_VIEW(Widget)))) return;
    auto* Layout=gtk_widget_create_pango_layout(Widget,Value->Hint);
    pango_layout_set_width(Layout,std::max(0,gtk_widget_get_width(Widget))*PANGO_SCALE); pango_layout_set_wrap(Layout,PANGO_WRAP_WORD_CHAR);
    GdkRGBA Color; gtk_style_context_get_color(gtk_widget_get_style_context(Widget),&Color); Color.alpha*=0.65;
    gtk_snapshot_append_layout(Output,Layout,&Color); g_object_unref(Layout);
}
static void lui_text_view_class_init(LuiTextViewClass* Class) {
    G_OBJECT_CLASS(Class)->dispose=TextViewDispose; GTK_WIDGET_CLASS(Class)->snapshot=TextViewSnapshot;
}
static void lui_text_view_init(LuiTextView* Value) {
    g_signal_connect(Value,"preedit-changed",reinterpret_cast<GCallback>(+[](GtkTextView* Widget,const char* Text,gpointer) {
        reinterpret_cast<LuiTextView*>(Widget)->Preedit=Text && *Text; gtk_widget_queue_draw(GTK_WIDGET(Widget));
    }),nullptr);
}
GtkWidget* LuiGtkTextView() {return GTK_WIDGET(g_object_new(lui_text_view_get_type(),nullptr));}
void LuiGtkTextHint(GtkWidget* Widget,const char* Text) {
    auto* Value=reinterpret_cast<LuiTextView*>(Widget); g_free(Value->Hint); Value->Hint=g_strdup(Text); gtk_widget_queue_draw(Widget);
}
static void Measure(GtkWidget*,GtkOrientation,int,int* Minimum,int* Natural,int* MinBaseline,int* NaturalBaseline) {
    *Minimum=*Natural=0; *MinBaseline=*NaturalBaseline=-1;
}
static void Clear(GtkWidget* Widget) {
    while(auto* Child=gtk_widget_get_first_child(Widget)) gtk_widget_unparent(Child);
}
static void SurfaceDispose(GObject* Object) {
    auto* Widget=GTK_WIDGET(Object);
    Clear(Widget); G_OBJECT_CLASS(lui_surface_parent_class)->dispose(Object);
}
static void SlotDispose(GObject* Object) {Clear(GTK_WIDGET(Object)); G_OBJECT_CLASS(lui_slot_parent_class)->dispose(Object);}
static void Snapshot(GtkWidget* Widget,GtkSnapshot* Output) {
    if(G_TYPE_CHECK_INSTANCE_TYPE(Widget,lui_surface_get_type())) {
        auto* Surface=reinterpret_cast<LuiSurface*>(Widget);
        const graphene_rect_t Rect=GRAPHENE_RECT_INIT(0,0,static_cast<float>(gtk_widget_get_width(Widget)),static_cast<float>(gtk_widget_get_height(Widget)));
        GskRoundedRect Rounded; gsk_rounded_rect_init_from_rect(&Rounded,&Rect,static_cast<float>(std::min({Surface->Radius,Rect.size.width/2.0,Rect.size.height/2.0})));
        if(Surface->Background) {
            gtk_snapshot_push_rounded_clip(Output,&Rounded); gtk_snapshot_append_color(Output,&Surface->BackgroundColor,&Rect); gtk_snapshot_pop(Output);
        }
        if(Surface->BorderSize>0) {
            const float Width=static_cast<float>(std::min({Surface->BorderSize,Rect.size.width/2.0,Rect.size.height/2.0}));
            const float Widths[4]={Width,Width,Width,Width};
            const GdkRGBA Colors[4]={Surface->BorderColor,Surface->BorderColor,Surface->BorderColor,Surface->BorderColor};
            gtk_snapshot_append_border(Output,&Rounded,Widths,Colors);
        }
    }
    const ClipValue& Clip=G_TYPE_CHECK_INSTANCE_TYPE(Widget,lui_slot_get_type())?
        reinterpret_cast<LuiSlot*>(Widget)->Clip:reinterpret_cast<LuiSurface*>(Widget)->Clip;
    if(Clip.Enabled) {
        const graphene_rect_t Rect=GRAPHENE_RECT_INIT(static_cast<float>(Clip.X),static_cast<float>(Clip.Y),
            static_cast<float>(Clip.Width),static_cast<float>(Clip.Height));
        gtk_snapshot_push_clip(Output,&Rect);
    }
    for(auto* Child=gtk_widget_get_first_child(Widget);Child;Child=gtk_widget_get_next_sibling(Child))
        if(gtk_widget_get_visible(Child)) gtk_widget_snapshot_child(Widget,Child,Output);
    if(Clip.Enabled) gtk_snapshot_pop(Output);
}
static gboolean Contains(GtkWidget* Widget,double X,double Y) {
    const ClipValue& Clip=G_TYPE_CHECK_INSTANCE_TYPE(Widget,lui_slot_get_type())?
        reinterpret_cast<LuiSlot*>(Widget)->Clip:reinterpret_cast<LuiSurface*>(Widget)->Clip;
    if(Clip.Enabled && (X<Clip.X || Y<Clip.Y || X>=Clip.X+Clip.Width || Y>=Clip.Y+Clip.Height)) return false;
    return X>=0 && Y>=0 && X<gtk_widget_get_width(Widget) && Y<gtk_widget_get_height(Widget);
}
static int Size(double Value) {return static_cast<int>(std::max(0.0,std::round(Value)));}
static void SurfaceAllocate(GtkWidget* Widget,int,int,int) {
    for(auto* Child=gtk_widget_get_first_child(Widget);Child;Child=gtk_widget_get_next_sibling(Child)) {
        if(!gtk_widget_should_layout(Child)) continue;
        auto* Slot=reinterpret_cast<LuiSlot*>(Child);
        graphene_point_t Point{static_cast<float>(Slot->X),static_cast<float>(Slot->Y)};
        gtk_widget_allocate(Child,Size(Slot->Width),Size(Slot->Height),-1,gsk_transform_translate(nullptr,&Point));
    }
}
static void SlotAllocate(GtkWidget* Widget,int Width,int Height,int Baseline) {
    if(auto* Child=gtk_widget_get_first_child(Widget)) gtk_widget_allocate(Child,Width,Height,Baseline,nullptr);
}
static void lui_surface_class_init(LuiSurfaceClass* Class) {
    G_OBJECT_CLASS(Class)->dispose=SurfaceDispose;
    auto* Widget=GTK_WIDGET_CLASS(Class); Widget->measure=Measure; Widget->size_allocate=SurfaceAllocate; Widget->snapshot=Snapshot; Widget->contains=Contains;
    gtk_widget_class_set_accessible_role(Widget,GTK_ACCESSIBLE_ROLE_GROUP);
}
static void lui_surface_init(LuiSurface*) {}
static void lui_slot_class_init(LuiSlotClass* Class) {
    G_OBJECT_CLASS(Class)->dispose=SlotDispose;
    auto* Widget=GTK_WIDGET_CLASS(Class); Widget->measure=Measure; Widget->size_allocate=SlotAllocate; Widget->snapshot=Snapshot; Widget->contains=Contains;
    // GTK 4.14 omits this subtree for PRESENTATION. Keep a group so the
    // canonical control's role, name, description and focus reach AT-SPI.
    gtk_widget_class_set_accessible_role(Widget,GTK_ACCESSIBLE_ROLE_GROUP);
}
static void lui_slot_init(LuiSlot* Slot) {gtk_widget_set_overflow(GTK_WIDGET(Slot),GTK_OVERFLOW_HIDDEN);}
GtkWidget* LuiGtkSurface() {return GTK_WIDGET(g_object_new(lui_surface_get_type(),nullptr));}
GtkWidget* LuiGtkSlot(GtkWidget* Child) {
    auto* Slot=GTK_WIDGET(g_object_new(lui_slot_get_type(),nullptr)); gtk_widget_set_parent(Child,Slot); return Slot;
}
void LuiGtkPlace(GtkWidget* Surface,GtkWidget* Slot) {
    if(gtk_widget_get_parent(Slot)) gtk_widget_unparent(Slot);
    if(Surface) gtk_widget_set_parent(Slot,Surface);
}
void LuiGtkRect(GtkWidget* Widget,double X,double Y,double Width,double Height) {
    auto* Slot=reinterpret_cast<LuiSlot*>(Widget); Slot->X=X; Slot->Y=Y; Slot->Width=Width; Slot->Height=Height;
    if(auto* Parent=gtk_widget_get_parent(Widget)) gtk_widget_queue_allocate(Parent);
    gtk_widget_queue_draw(Widget);
}
void LuiGtkClip(GtkWidget* Widget,bool HasClip,double X,double Y,double Width,double Height) {
    ClipValue& Clip=G_TYPE_CHECK_INSTANCE_TYPE(Widget,lui_slot_get_type())?
        reinterpret_cast<LuiSlot*>(Widget)->Clip:reinterpret_cast<LuiSurface*>(Widget)->Clip;
    Clip={HasClip,X,Y,Width,Height}; gtk_widget_queue_draw(Widget);
}
void LuiGtkDecoration(GtkWidget* Widget,bool Background,const GdkRGBA& BackgroundColor,const GdkRGBA& BorderColor,double BorderSize,double Radius) {
    auto* Surface=reinterpret_cast<LuiSurface*>(Widget);
    Surface->Background=Background; Surface->BackgroundColor=BackgroundColor; Surface->BorderColor=BorderColor;
    Surface->BorderSize=BorderSize; Surface->Radius=Radius; gtk_widget_queue_draw(Widget);
}
