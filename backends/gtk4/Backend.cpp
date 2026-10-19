#include "Backend.h"
#include "Geometry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <sstream>
#include <locale>
#include "../../runtime/internal/Text.h"

namespace Lui::Gtk {
namespace {
GLogWriterOutput SafeLog(GLogLevelFlags,const GLogField*,gsize,gpointer) {
    std::fputs("[LUI:GTK] NativeDiagnostic\n",stderr); return G_LOG_WRITER_HANDLED;
}
void SafeLegacyLog(const gchar*,GLogLevelFlags,const gchar*,gpointer) {std::fputs("[LUI:GTK] NativeDiagnostic\n",stderr);}
}
bool Backend::Initialize() {
    static bool Writer=false;
    if(!Writer) {g_log_set_writer_func(SafeLog,nullptr,nullptr); g_log_set_default_handler(SafeLegacyLog,nullptr); Writer=true;}
    const bool NameMissing=!g_get_application_name();
    if(!g_get_prgname()) g_set_prgname("lui-gtk");
    if(NameMissing) g_set_application_name("LUI");
    return gtk_init_check();
}
Backend::Backend(uint32_t Mode):OwnerThread(std::this_thread::get_id()) {
    LuiDiagnosticPolicyV1 Policy{sizeof(Policy),1,Mode,0}; Runtime=Lui_CreateWithPolicy(&Policy);
    if(!Runtime) throw std::runtime_error("[LUI:GTK] StartupFailed");
    Lui_SetBackend(Runtime,{this,Create,Property,Parent,Arrange,Destroy}); Lui_SetBackendName(Runtime,"gtk4");
    try {
    LuiPresentationCallbacksV1 Presentation{sizeof(Presentation),1,this,Present};
    if(!Lui_SetPresentationCallbacks(Runtime,&Presentation)) throw std::runtime_error("[LUI:GTK] PresentationFailed");
    StartImages();
    StartPlatform();
    Settings=gtk_settings_get_default();
    if(Settings) {
        g_signal_connect(Settings,"notify::gtk-theme-name",reinterpret_cast<GCallback>(+[](GObject*,GParamSpec*,gpointer Data) {static_cast<Backend*>(Data)->ThemePending=true;}),this);
        g_signal_connect(Settings,"notify::gtk-application-prefer-dark-theme",reinterpret_cast<GCallback>(+[](GObject*,GParamSpec*,gpointer Data) {static_cast<Backend*>(Data)->ThemePending=true;}),this);
    }
    ObserveTheme();
    } catch(...) {Stop(); throw;}
}
Backend::~Backend() {Stop();}
bool Backend::Owner() const {return OwnerThread==std::this_thread::get_id();}
int Backend::Fail() {Lui_ReportBackendError(Runtime,"[LUI:Backend] OperationFailed"); return 0;}
void Backend::SetAsset(const std::string& Name,std::string Bytes) {
    if(!Owner() || Bytes.size()>16*1024*1024 || Assets.size()>=256 || Assets.count(Name) || AssetBytes+Bytes.size()>64*1024*1024) throw std::runtime_error("[LUI:GTK] LimitExceeded");
    if(!Lui_RegisterAsset(Runtime,Name.c_str())) throw std::runtime_error("[LUI:GTK] AssetFailed");
    AssetBytes+=Bytes.size(); Assets.emplace(Name,std::make_shared<const std::string>(std::move(Bytes)));
}
void Backend::Stop() {
    if(!Runtime || !Owner()) return;
    Stopping=true; Events.clear(); EventBytes=0;
    StopImages();
    StopPlatform();
    if(Settings) {g_signal_handlers_disconnect_by_data(Settings,this); Settings=nullptr;}
    Lui_Destroy(Runtime); Runtime=nullptr;
    // Runtime destruction normally removes all views; failed creation may leave
    // an uncommitted object, so native ownership is also drained explicitly.
    while(!Views.empty()) Destroy(this,Views.begin()->first);
    Assets.clear();
}
bool Backend::HasWindows() const {
    for(const auto& Pair:Views) if(Pair.second->ClassName=="Window" && Pair.second->Alive && !Pair.second->Closed) return true;
    return false;
}
bool Backend::Usable(const View& Target) const {
    const View* Current=&Target;
    for(size_t Depth=0;Depth<=Views.size();++Depth) {
        if(!Current->Alive || Current->Closed || !Current->Visible || !Current->Enabled) return false;
        if(!Current->ParentId) return true;
        auto Parent=Views.find(Current->ParentId); if(Parent==Views.end()) return false; Current=Parent->second.get();
    }
    return false;
}
void Backend::Queue(View* Target,Event Value) noexcept {
    try {
        if(Stopping || !Target->Alive || Target->Applying || !Owner()) return;
        Value.Id=Target->Id; Value.Epoch=Target->Epoch;
        if((Value.Type==Kind::TextInput || Value.Type==Kind::Composition || Value.Type==Kind::Text) &&
            !Lui_GetTextInputRevision(Runtime,Value.Id,&Value.TextRevision)) return;
        Value.QueuedAt=std::chrono::steady_clock::now();
        if(Value.Text.size()>8*1024*1024) {Overflow=true; ++Dropped; return;}
        if(Events.size()>=256 || EventBytes+Value.Text.size()>8*1024*1024) {Events.clear(); EventBytes=0; Overflow=true; ++Dropped;}
        EventBytes+=Value.Text.size();
        Events.push_back(std::move(Value));
    } catch(...) {Overflow=true; ++Dropped;}
}
void Backend::ResetInput() {
    std::vector<int> Ids; for(const auto& Pair:Views) Ids.push_back(Pair.first);
    for(int Id:Ids) {
        auto Found=Views.find(Id); if(Found!=Views.end()) Found->second->Keys.clear();
        Lui_FocusChanged(Runtime,Id,0); Lui_HoverChanged(Runtime,Id,0);
        Lui_PointerInput(Runtime,Id,3,0,0,0,0);
    }
}
void Backend::Drain() {
    for(auto& Pair:Views) if(Pair.second->CompositionEnding) {
        auto& Target=*Pair.second; Target.CompositionEnding=false; Target.Composing=false;
        TextEvent(Target); Event End{Kind::Composition,0,0}; End.Phase=2; Queue(&Target,std::move(End));
    }
    if(Overflow) {Overflow=false; ResetInput();}
    auto Ready=std::move(Events); Events.clear(); EventBytes=0;
    for(auto& Value:Ready) {
        auto Found=Views.find(Value.Id);
        if(Found==Views.end() || Found->second->Epoch!=Value.Epoch || !Found->second->Alive) continue;
        uint64_t Revision=0;
        if(Value.TextRevision && (!Lui_GetTextInputRevision(Runtime,Value.Id,&Revision) || Revision!=Value.TextRevision)) continue;
        auto& Target=*Found->second;
        if(Value.Type==Kind::Close) {
            Target.Closed=true; gtk_widget_set_visible(Target.Native,false);
            if(!HasWindows()) {Stopping=true; Events.clear(); EventBytes=0; break;}
            continue;
        }
        if(Value.Type==Kind::Resize) {Lui_WindowResized(Runtime,Value.Id,Value.X,Value.Y); continue;}
        if((Value.Type==Kind::Focus || Value.Type==Kind::Hover) && !Value.Phase) {
            if(Value.Type==Kind::Focus) Lui_FocusChanged(Runtime,Value.Id,0); else Lui_HoverChanged(Runtime,Value.Id,0); continue;
        }
        if(!Usable(Target)) continue;
        switch(Value.Type) {
            case Kind::Activate: Lui_Activate(Runtime,Value.Id); break;
            case Kind::Text: Lui_TextChanged(Runtime,Value.Id,Value.Text.c_str()); break;
            case Kind::Checked: Lui_CheckedChanged(Runtime,Value.Id,Value.Phase); break;
            case Kind::Value: Target.Value=Value.Value; Lui_ValueChanged(Runtime,Value.Id,Value.Value); break;
            case Kind::Focus: Lui_FocusChanged(Runtime,Value.Id,Value.Phase); break;
            case Kind::Hover: Lui_HoverChanged(Runtime,Value.Id,Value.Phase); break;
            case Kind::Pointer: Lui_PointerInput(Runtime,Value.Id,Value.Phase,Value.Device,0,Value.X,Value.Y); break;
            case Kind::Key: Lui_KeyInput(Runtime,Value.Id,Value.Phase,Value.Text.c_str()); break;
            case Kind::Scroll: Lui_ScrollInput(Runtime,Value.Id,Value.X,Value.Y); break;
            case Kind::TextInput: Lui_TextInput(Runtime,Value.Id,Value.Text.c_str(),static_cast<int>(Value.X),static_cast<int>(Value.Y)); break;
            case Kind::Composition: Lui_TextComposition(Runtime,Value.Id,Value.Phase,Value.Text.c_str()); break;
            default: break;
        }
        if(!Stopping && Runtime) Lui_RecordPerformance(Runtime,1,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-Value.QueuedAt).count());
    }
}
void Backend::ObserveTheme() {
    if(!ThemePending || !Settings) return;
    ThemePending=false; gboolean Dark=false; char* Name=nullptr;
    g_object_get(Settings,"gtk-application-prefer-dark-theme",&Dark,"gtk-theme-name",&Name,nullptr);
    const std::string Theme=Name?Name:""; g_free(Name);
    const bool Contrast=Theme.find("HighContrast")!=std::string::npos;
    Lui_SystemThemeChanged(Runtime,Contrast?"HighContrast":Dark || Theme.find("dark")!=std::string::npos?"Dark":"Light");
}
void Backend::Pump() {
    if(!Runtime || Stopping || !Owner()) return;
    for(int Count=0;Count<64 && g_main_context_pending(nullptr);++Count) g_main_context_iteration(nullptr,false);
    // Native signals collect IDs/data only. VM callbacks run after GTK returns.
    Drain(); if(Stopping) return; PumpImages(); ObserveTheme(); Lui_Pump(Runtime);
    std::vector<int> Windows; for(const auto& Pair:Views) if(Pair.second->ClassName=="Window") Windows.push_back(Pair.first);
    for(int Id:Windows) {
        auto Found=Views.find(Id); if(Found==Views.end() || Found->second->Closed) continue;
        auto& Target=*Found->second;
        const int Width=gtk_widget_get_width(Target.Container), Height=gtk_widget_get_height(Target.Container);
        if(Width>0 && Height>0 && (Width!=Target.Width || Height!=Target.Height)) {
            // Compare to the last arranged viewport; runtime overrides avoid
            // script-size/native-viewport feedback on the next pump.
            Lui_WindowResized(Runtime,Id,Width,Height);
        }
    }
    // Owner response/layout has its own counters. Submission starts after the
    // complete owner batch, preserving an earlier update still awaiting paint.
    for(auto& Pair:Views) {
        auto& Target=*Pair.second;
        if(Target.PresentationPending) {
            if(!Target.PresentationAt.time_since_epoch().count()) Target.PresentationAt=std::chrono::steady_clock::now();
            Target.PresentationPending=false;
        }
    }
}
std::string GetKey(guint Value) {
    const auto Unicode=gdk_keyval_to_unicode(Value);
    if(Unicode>='a' && Unicode<='z') return std::string(1,static_cast<char>(Unicode-'a'+'A'));
    if((Unicode>='A' && Unicode<='Z') || (Unicode>='0' && Unicode<='9')) return std::string(1,static_cast<char>(Unicode));
    if(Value>=GDK_KEY_F1 && Value<=GDK_KEY_F24) return "F"+std::to_string(Value-GDK_KEY_F1+1);
    switch(Value) {
        case GDK_KEY_Return: case GDK_KEY_KP_Enter:return "Enter";
        case GDK_KEY_Escape:return "Escape"; case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab:return "Tab";
        case GDK_KEY_space:return "Space"; case GDK_KEY_BackSpace:return "Backspace";
        case GDK_KEY_Delete:return "Delete"; case GDK_KEY_Insert:return "Insert";
        case GDK_KEY_Home:return "Home"; case GDK_KEY_End:return "End";
        case GDK_KEY_Page_Up:return "PageUp"; case GDK_KEY_Page_Down:return "PageDown";
        case GDK_KEY_Left:return "ArrowLeft"; case GDK_KEY_Right:return "ArrowRight";
        case GDK_KEY_Up:return "ArrowUp"; case GDK_KEY_Down:return "ArrowDown";
        case GDK_KEY_Shift_L:case GDK_KEY_Shift_R:return "Shift";
        case GDK_KEY_Control_L:case GDK_KEY_Control_R:return "Control";
        case GDK_KEY_Alt_L:case GDK_KEY_Alt_R:return "Alt";case GDK_KEY_Caps_Lock:return "CapsLock";
        default:return {};
    }
}
void Backend::Connect(View& Target) {
    auto* Data=&Target;
    if(Target.ClassName=="Window") {
        g_signal_connect(Target.Native,"realize",reinterpret_cast<GCallback>(+[](GtkWidget* Widget,gpointer Context) {
            auto* Target=static_cast<View*>(Context); Target->Clock=gtk_widget_get_frame_clock(Widget);
            if(Target->Clock) g_signal_connect(Target->Clock,"after-paint",reinterpret_cast<GCallback>(+[](GdkFrameClock*,gpointer Context) {
                auto* Target=static_cast<View*>(Context);
                if(Target->Alive && Target->Owner->Runtime && Target->PresentationAt.time_since_epoch().count()) {
                    Lui_RecordPerformance(Target->Owner->Runtime,2,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-Target->PresentationAt).count());
                    Target->PresentationAt={};
                }
            }),Context);
        }),Data);
        g_signal_connect(Target.Native,"close-request",reinterpret_cast<GCallback>(+[](GtkWindow*,gpointer Context)->gboolean {
            auto* Target=static_cast<View*>(Context); Target->Owner->Queue(Target,{Kind::Close,0,0}); return true;
        }),Data); return;
    }
    if(Target.ClassName=="TextButton") g_signal_connect(Target.Native,"clicked",reinterpret_cast<GCallback>(+[](GtkButton*,gpointer Context) {
        auto* Target=static_cast<View*>(Context); Target->Owner->Queue(Target,{Kind::Activate,0,0});
    }),Data);
    if(Target.ClassName=="TextBox") ConnectText(Target);
    if(Target.ClassName=="CheckBox") g_signal_connect(Target.Native,"toggled",reinterpret_cast<GCallback>(+[](GtkCheckButton* Check,gpointer Context) {
        auto* Target=static_cast<View*>(Context); Event Value{Kind::Checked,0,0}; Value.Phase=gtk_check_button_get_active(Check); Target->Owner->Queue(Target,std::move(Value));
    }),Data);
    if(Target.ClassName=="Slider") g_signal_connect(Target.Native,"value-changed",reinterpret_cast<GCallback>(+[](GtkRange* Range,gpointer Context) {
        auto* Target=static_cast<View*>(Context); Event Value{Kind::Value,0,0}; Value.Value=gtk_range_get_value(Range); Target->Owner->Queue(Target,std::move(Value));
    }),Data);
    auto Add=[&](GtkEventController* Controller) {Target.Controllers.push_back(G_OBJECT(Controller)); gtk_event_controller_set_propagation_phase(Controller,GTK_PHASE_CAPTURE); gtk_widget_add_controller(Target.Native,Controller);};
    if(Target.ClassName=="TextBox" || Target.ClassName=="TextButton" || Target.ClassName=="CheckBox" || Target.ClassName=="Slider") {
        auto* Focus=gtk_event_controller_focus_new();
        g_signal_connect(Focus,"enter",reinterpret_cast<GCallback>(+[](GtkEventControllerFocus*,gpointer Context) {auto* Target=static_cast<View*>(Context); Event Value{Kind::Focus,0,0}; Value.Phase=1; Target->Owner->Queue(Target,std::move(Value));}),Data);
        g_signal_connect(Focus,"leave",reinterpret_cast<GCallback>(+[](GtkEventControllerFocus*,gpointer Context) {auto* Target=static_cast<View*>(Context); Target->Owner->Queue(Target,{Kind::Focus,0,0});}),Data); Add(Focus);
        auto* Key=gtk_event_controller_key_new();
        g_signal_connect(Key,"key-pressed",reinterpret_cast<GCallback>(+[](GtkEventControllerKey*,guint Code,guint,GdkModifierType,gpointer Context)->gboolean {
            auto* Target=static_cast<View*>(Context); auto Name=GetKey(Code); if(Name.empty()) return false;
            Event Value{Kind::Key,0,0}; Value.Phase=Target->Keys.count(Name)?1:0; Target->Keys.insert(Name); Value.Text=Name;
            Target->Owner->Queue(Target,std::move(Value)); return false;
        }),Data);
        g_signal_connect(Key,"key-released",reinterpret_cast<GCallback>(+[](GtkEventControllerKey*,guint Code,guint,GdkModifierType,gpointer Context) {
            auto* Target=static_cast<View*>(Context); auto Name=GetKey(Code); if(Name.empty()) return;
            Target->Keys.erase(Name); Event Value{Kind::Key,0,0}; Value.Phase=2; Value.Text=Name; Target->Owner->Queue(Target,std::move(Value));
        }),Data); Add(Key);
    }
    auto* Motion=gtk_event_controller_motion_new();
    g_signal_connect(Motion,"enter",reinterpret_cast<GCallback>(+[](GtkEventControllerMotion*,double,double,gpointer Context) {auto* Target=static_cast<View*>(Context); Event Value{Kind::Hover,0,0}; Value.Phase=1; Target->Owner->Queue(Target,std::move(Value));}),Data);
    g_signal_connect(Motion,"leave",reinterpret_cast<GCallback>(+[](GtkEventControllerMotion*,gpointer Context) {auto* Target=static_cast<View*>(Context); Target->Owner->Queue(Target,{Kind::Hover,0,0}); Event Value{Kind::Pointer,0,0}; Value.Phase=3; Target->Owner->Queue(Target,std::move(Value));}),Data);
    g_signal_connect(Motion,"motion",reinterpret_cast<GCallback>(+[](GtkEventControllerMotion* Controller,double X,double Y,gpointer Context) {
        auto* Device=gtk_event_controller_get_current_event_device(GTK_EVENT_CONTROLLER(Controller));
        if(!Device || gdk_device_get_source(Device)!=GDK_SOURCE_MOUSE) return;
        auto* Target=static_cast<View*>(Context); Event Value{Kind::Pointer,0,0}; Value.Phase=1; Value.X=X; Value.Y=Y; Target->Owner->Queue(Target,std::move(Value));
    }),Data); Add(Motion);
    auto* Click=gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(Click),1);
    // GTK callback ABI needs noncapturing adapters. Mouse only in this initial profile.
    g_signal_connect(Click,"pressed",reinterpret_cast<GCallback>(+[](GtkGestureClick* Gesture,int,double X,double Y,gpointer Context) {
        auto* Device=gtk_event_controller_get_current_event_device(GTK_EVENT_CONTROLLER(Gesture)); if(!Device || gdk_device_get_source(Device)!=GDK_SOURCE_MOUSE) return;
        auto* Target=static_cast<View*>(Context); Event Value{Kind::Pointer,0,0}; Value.X=X; Value.Y=Y; Target->Owner->Queue(Target,std::move(Value));
    }),Data);
    g_signal_connect(Click,"released",reinterpret_cast<GCallback>(+[](GtkGestureClick* Gesture,int,double X,double Y,gpointer Context) {
        auto* Device=gtk_event_controller_get_current_event_device(GTK_EVENT_CONTROLLER(Gesture)); if(!Device || gdk_device_get_source(Device)!=GDK_SOURCE_MOUSE) return;
        auto* Target=static_cast<View*>(Context); Event Value{Kind::Pointer,0,0}; Value.Phase=2; Value.X=X; Value.Y=Y; Target->Owner->Queue(Target,std::move(Value));
    }),Data);
    g_signal_connect(Click,"cancel",reinterpret_cast<GCallback>(+[](GtkGesture*,GdkEventSequence*,gpointer Context) {auto* Target=static_cast<View*>(Context); Event Value{Kind::Pointer,0,0}; Value.Phase=3; Target->Owner->Queue(Target,std::move(Value));}),Data);
    Add(GTK_EVENT_CONTROLLER(Click));
    if(Target.ClassName=="ScrollingFrame") {
        auto* Scroll=gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
        g_signal_connect(Scroll,"scroll",reinterpret_cast<GCallback>(+[](GtkEventControllerScroll* Controller,double X,double Y,gpointer Context)->gboolean {
            auto* Target=static_cast<View*>(Context);
            const double Factor=gtk_event_controller_scroll_get_unit(Controller)==GDK_SCROLL_UNIT_WHEEL?40.0:1.0;
            Event Value{Kind::Scroll,0,0}; Value.X=X*Factor; Value.Y=Y*Factor;
            Target->Owner->Queue(Target,std::move(Value)); return true;
        }),Data); Add(Scroll);
        gtk_event_controller_set_propagation_phase(Scroll,GTK_PHASE_BUBBLE);
    }
}
void Backend::TextEvent(View& Target) {
    if(!Target.Alive || Target.Applying) return;
    std::string Text; int Start=0,End=0;
    if(Target.Multiline) {
        auto* Buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(Target.Native)); GtkTextIter First,Last;
        gtk_text_buffer_get_bounds(Buffer,&First,&Last); char* Bytes=gtk_text_buffer_get_text(Buffer,&First,&Last,false);
        Text=Bytes; g_free(Bytes);
        if(gtk_text_buffer_get_selection_bounds(Buffer,&First,&Last)) {Start=gtk_text_iter_get_offset(&First); End=gtk_text_iter_get_offset(&Last);}
        else {gtk_text_buffer_get_iter_at_mark(Buffer,&First,gtk_text_buffer_get_insert(Buffer)); Start=End=gtk_text_iter_get_offset(&First);}
    } else {
        Text=gtk_editable_get_text(GTK_EDITABLE(Target.Native));
        if(!gtk_editable_get_selection_bounds(GTK_EDITABLE(Target.Native),&Start,&End)) Start=End=gtk_editable_get_position(GTK_EDITABLE(Target.Native));
    }
    int Characters;
    if(!LuiText::Count(Text,Characters) || Characters>Target.MaxLength) {
        Target.Applying=true;
        if(Target.Multiline) gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(Target.Native)),Target.Text.c_str(),static_cast<int>(Target.Text.size()));
        else gtk_editable_set_text(GTK_EDITABLE(Target.Native),Target.Text.c_str());
        ApplySelection(Target); Target.Applying=false; return;
    }
    Target.Text=Text; Target.SelectionStart=Target.Secret?0:std::min(Start,End); Target.SelectionLength=Target.Secret?0:std::abs(End-Start);
    if(Target.Composing) return;
    Event Value{Kind::TextInput,0,0}; Value.Text=Text; Value.X=Target.SelectionStart; Value.Y=Target.SelectionLength; Queue(&Target,std::move(Value));
}
void Backend::ApplySelection(View& Target) {
    if(Target.Multiline) {
        auto* Buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(Target.Native)); GtkTextIter First,Last;
        gtk_text_buffer_get_iter_at_offset(Buffer,&First,Target.SelectionStart); gtk_text_buffer_get_iter_at_offset(Buffer,&Last,Target.SelectionStart+Target.SelectionLength);
        gtk_text_buffer_select_range(Buffer,&First,&Last);
    } else gtk_editable_select_region(GTK_EDITABLE(Target.Native),Target.SelectionStart,Target.SelectionStart+Target.SelectionLength);
}
void Backend::ConnectText(View& Target) {
    gpointer Data=&Target;
    auto Preedit=+[](GObject*,const char* Text,gpointer Context) {
        auto* Target=static_cast<View*>(Context); if(Target->Applying || !Target->Alive || Target->Secret) return;
        if(!Text || !*Text) {if(Target->Composing) Target->CompositionEnding=true; return;}
        Event Value{Kind::Composition,0,0}; Value.Phase=Target->Composing?1:0; Value.Text=Text;
        Target->Composing=true; Target->CompositionEnding=false; Target->Owner->Queue(Target,std::move(Value));
    };
    if(Target.Multiline) {
        auto* Buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(Target.Native)); Target.TextSources.push_back(G_OBJECT(Buffer));
        g_signal_connect(Buffer,"changed",reinterpret_cast<GCallback>(+[](GtkTextBuffer*,gpointer Context) {auto* Target=static_cast<View*>(Context); Target->Owner->TextEvent(*Target);}),Data);
        g_signal_connect(Buffer,"mark-set",reinterpret_cast<GCallback>(+[](GtkTextBuffer*,GtkTextIter*,GtkTextMark*,gpointer Context) {auto* Target=static_cast<View*>(Context); Target->Owner->TextEvent(*Target);}),Data);
        g_signal_connect(Target.Native,"preedit-changed",reinterpret_cast<GCallback>(Preedit),Data);
    } else {
        auto* Delegate=gtk_editable_get_delegate(GTK_EDITABLE(Target.Native)); if(Delegate) {Target.TextSources.push_back(G_OBJECT(Delegate)); g_signal_connect(Delegate,"preedit-changed",reinterpret_cast<GCallback>(Preedit),Data);}
        g_signal_connect(Target.Native,"changed",reinterpret_cast<GCallback>(+[](GtkEditable*,gpointer Context) {auto* Target=static_cast<View*>(Context); Target->Owner->TextEvent(*Target);}),Data);
        auto Selection=+[](GObject*,GParamSpec*,gpointer Context) {auto* Target=static_cast<View*>(Context); Target->Owner->TextEvent(*Target);};
        g_signal_connect(Target.Native,"notify::cursor-position",reinterpret_cast<GCallback>(Selection),Data);
        g_signal_connect(Target.Native,"notify::selection-bound",reinterpret_cast<GCallback>(Selection),Data);
    }
}
void Backend::ReplaceText(View& Target,bool Multiline,bool Secret,bool Force) {
    if(Target.Multiline==Multiline && Target.Secret==Secret && !Force) return;
    for(auto* Source:Target.TextSources) g_signal_handlers_disconnect_by_data(Source,&Target);
    Target.TextSources.clear(); g_signal_handlers_disconnect_by_data(Target.Native,&Target);
    for(auto* Controller:Target.Controllers) g_signal_handlers_disconnect_by_data(Controller,&Target);
    Target.Controllers.clear(); Target.Keys.clear(); Target.Epoch=NextEpoch++;
    gtk_widget_unparent(Target.Native); g_object_unref(Target.Native);
    Target.Multiline=Multiline; Target.Secret=Secret; Target.Composing=Target.CompositionEnding=false;
    Target.Native=Multiline?LuiGtkTextView():gtk_entry_new(); g_object_ref_sink(Target.Native);
    gtk_widget_set_parent(Target.Native,Target.Slot);
    gtk_style_context_add_provider(gtk_widget_get_style_context(Target.Native),GTK_STYLE_PROVIDER(Target.Style),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    if(Multiline) {gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(Target.Native),GTK_WRAP_WORD_CHAR); gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(Target.Native)),Target.Text.c_str(),static_cast<int>(Target.Text.size()));}
    else {gtk_entry_set_visibility(GTK_ENTRY(Target.Native),!Secret); gtk_editable_set_text(GTK_EDITABLE(Target.Native),Target.Text.c_str());}
    gtk_widget_set_sensitive(Target.Native,Target.Enabled && Target.InputVisible);
    Connect(Target); ApplySelection(Target);
    if(!Target.AccessibilityLabel.empty()) gtk_accessible_update_property(GTK_ACCESSIBLE(Target.Native),GTK_ACCESSIBLE_PROPERTY_LABEL,Target.AccessibilityLabel.c_str(),-1);
    if(!Target.AccessibilityDescription.empty()) gtk_accessible_update_property(GTK_ACCESSIBLE(Target.Native),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,Target.AccessibilityDescription.c_str(),-1);
    Lui_FocusChanged(Runtime,Target.Id,0);
}
int Backend::Create(void* Context,int Id,const char* ClassName) {
    auto& Self=*static_cast<Backend*>(Context);
    try {
    if(!Self.Owner() || Self.Stopping || Self.Views.size()>=1024 || Self.Views.count(Id)) return Self.Fail();
    auto Target=std::make_unique<View>(); Target->Owner=&Self; Target->Id=Id; Target->Epoch=Self.NextEpoch++; Target->ClassName=ClassName;
    if(Target->ClassName=="Window") {Target->Native=gtk_window_new(); Target->Container=LuiGtkSurface(); gtk_window_set_child(GTK_WINDOW(Target->Native),Target->Container); Target->Visible=false;}
    else if(Target->ClassName=="Frame" || Target->ClassName=="ScrollingFrame") Target->Native=Target->Container=LuiGtkSurface();
    else if(Target->ClassName=="TextLabel") {Target->Native=gtk_label_new(""); gtk_label_set_wrap(GTK_LABEL(Target->Native),true); gtk_label_set_xalign(GTK_LABEL(Target->Native),0);}
    else if(Target->ClassName=="TextButton") Target->Native=gtk_button_new();
    else if(Target->ClassName=="TextBox") Target->Native=gtk_entry_new();
    else if(Target->ClassName=="CheckBox") Target->Native=gtk_check_button_new();
    else if(Target->ClassName=="Slider") {Target->Native=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,0,100,1); gtk_scale_set_draw_value(GTK_SCALE(Target->Native),false);}
    else if(Target->ClassName=="ProgressBar") Target->Native=gtk_progress_bar_new();
    else if(Target->ClassName=="ImageLabel") {Target->Native=gtk_picture_new(); gtk_picture_set_content_fit(GTK_PICTURE(Target->Native),GTK_CONTENT_FIT_CONTAIN); gtk_picture_set_can_shrink(GTK_PICTURE(Target->Native),true);}
    else return Self.Fail();
    g_object_ref_sink(Target->Native);
    Target->Style=gtk_css_provider_new();
    gtk_style_context_add_provider(gtk_widget_get_style_context(Target->Native),GTK_STYLE_PROVIDER(Target->Style),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    if(Target->ClassName!="Window") {
        Target->Slot=LuiGtkSlot(Target->Native); g_object_ref_sink(Target->Slot);
        if(Target->Container) gtk_widget_set_overflow(Target->Slot,GTK_OVERFLOW_VISIBLE);
    }
    auto* Stored=Target.get(); Self.Views.emplace(Id,std::move(Target)); Self.Connect(*Stored); return 1;
    } catch(...) {Destroy(Context,Id); return Self.Fail();}
}
int Backend::Property(void* Context,int Id,const char* Name,const char* Text) {
    auto& Self=*static_cast<Backend*>(Context); auto Found=Self.Views.find(Id); if(Found==Self.Views.end() || !Self.Owner()) return Self.Fail();
    auto& Target=*Found->second; Target.Applying=true;
    struct Guard {View& Target; ~Guard(){Target.Applying=false;}} Applying{Target};
    try {
        const std::string Property=Name, Value=Text;
        if(Property=="__Style") {
            Target.StyleJson=Value;
            const auto Style=nlohmann::json::parse(Value);
            const bool Contrast=Style.at("HighContrast").get<bool>();
            std::ostringstream Css; Css.imbue(std::locale::classic()); Css<<"* {";
            auto Color=[&](const char* Name,const char* Rule,double Alpha) {
                const auto& Channels=Style.at(Name);
                if(Contrast) Css<<Rule<<": "<<(std::string(Name)=="BackgroundColor3"?"@theme_bg_color":"@theme_fg_color")<<";";
                else if(!Channels.is_null()) Css<<Rule<<": rgba("<<Channels[0].get<double>()*255<<","<<Channels[1].get<double>()*255<<","<<Channels[2].get<double>()*255<<","<<Alpha<<");";
            };
            if(!Target.Container) Color("BackgroundColor3","background-color",1-Style.at("BackgroundTransparency").get<double>());
            Color("TextColor3","color",1); Color("BorderColor3","border-color",1);
            const double BorderWidth=Target.Container?0:Contrast && (Target.ClassName=="TextButton" || Target.ClassName=="TextBox" || Target.ClassName=="CheckBox")?
                std::max(1.0,Style.at("BorderSizePixel").get<double>()):Style.at("BorderSizePixel").get<double>();
            Css<<"border-width:"<<BorderWidth<<"px;border-style:solid;border-radius:"<<Style.at("CornerRadius").get<double>()<<"px;";
            Css<<"font-size:"<<Style.at("TextSize").get<double>()<<"px;font-family:\""<<(Style.at("FontFamily")=="System"?"sans":Style.at("FontFamily").get<std::string>())<<"\";";
            Css<<"font-weight:"<<(Style.at("FontWeight")=="Bold"?"bold":"normal")<<";}";
            gtk_css_provider_load_from_string(Target.Style,Css.str().c_str());
            if(Target.Container) {
                auto ColorValue=[&](const char* Name,const char* Theme,GdkRGBA Fallback)->GdkRGBA {
                    const auto& Channels=Style.at(Name);
                    if(Contrast || Channels.is_null()) {gtk_style_context_lookup_color(gtk_widget_get_style_context(Target.Native),Theme,&Fallback); return Fallback;}
                    return {static_cast<float>(Channels[0].get<double>()),static_cast<float>(Channels[1].get<double>()),static_cast<float>(Channels[2].get<double>()),1};
                };
                auto Background=ColorValue("BackgroundColor3","theme_bg_color",{0,0,0,1});
                if(!Contrast) Background.alpha=1-Style.at("BackgroundTransparency").get<double>();
                LuiGtkDecoration(Target.Container,Contrast || !Style.at("BackgroundColor3").is_null(),Background,
                    ColorValue("BorderColor3","theme_fg_color",{1,1,1,1}),Style.at("BorderSizePixel").get<double>(),Style.at("CornerRadius").get<double>());
            }
            gtk_widget_set_opacity(Target.Slot?Target.Slot:Target.Native,Style.at("Opacity").get<double>());
            GtkWidget* Label=GTK_IS_LABEL(Target.Native)?Target.Native:GTK_IS_BUTTON(Target.Native)?gtk_button_get_child(GTK_BUTTON(Target.Native)):nullptr;
            if(Label && GTK_IS_LABEL(Label)) {
                gtk_label_set_wrap(GTK_LABEL(Label),Style.at("TextWrapped").get<bool>());
                gtk_label_set_xalign(GTK_LABEL(Label),Style.at("TextXAlignment")=="Center"?0.5:Style.at("TextXAlignment")=="Right"?1:0);
                gtk_label_set_yalign(GTK_LABEL(Label),Style.at("TextYAlignment")=="Center"?0.5:Style.at("TextYAlignment")=="Bottom"?1:0);
            }
            if(GTK_IS_TEXT_VIEW(Target.Native)) gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(Target.Native),Style.at("TextWrapped").get<bool>()?GTK_WRAP_WORD_CHAR:GTK_WRAP_NONE);
        } else if(Property=="__TextConfig" && Target.ClassName=="TextBox") {
            const auto First=Value.find(','),Second=Value.find(',',First+1),Third=Value.find(',',Second+1);
            if(First==std::string::npos || Second==std::string::npos || Third==std::string::npos) return Self.Fail();
            Target.MaxLength=std::stoi(Value.substr(Second+1,Third-Second-1)); Target.Placeholder=Value.substr(Third+1);
            Self.ReplaceText(Target,Value.substr(0,First)=="1",Value.substr(First+1,Second-First-1)=="1",Target.Composing);
            if(!Target.Multiline) {gtk_entry_set_max_length(GTK_ENTRY(Target.Native),Target.MaxLength); gtk_entry_set_placeholder_text(GTK_ENTRY(Target.Native),Target.Placeholder.c_str());}
            else LuiGtkTextHint(Target.Native,Target.Placeholder.c_str());
        } else if(Property=="__Selection" && Target.ClassName=="TextBox") {
            const auto Comma=Value.find(','); if(Comma==std::string::npos) return Self.Fail();
            Target.SelectionStart=std::stoi(Value.substr(0,Comma)); Target.SelectionLength=std::stoi(Value.substr(Comma+1)); Self.ApplySelection(Target);
        } else if(Property=="Title" && Target.ClassName=="Window") gtk_window_set_title(GTK_WINDOW(Target.Native),Text);
        else if(Property=="Text") {
            if(Target.ClassName=="TextLabel") gtk_label_set_text(GTK_LABEL(Target.Native),Text);
            else if(Target.ClassName=="TextButton") gtk_button_set_label(GTK_BUTTON(Target.Native),Text);
            else if(Target.ClassName=="TextBox") {
                Target.Text=Value;
                if(Target.Multiline) gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(Target.Native)),Text,static_cast<int>(Value.size()));
                else gtk_editable_set_text(GTK_EDITABLE(Target.Native),Text);
                Self.ApplySelection(Target);
            }
            else if(Target.ClassName=="CheckBox") gtk_check_button_set_label(GTK_CHECK_BUTTON(Target.Native),Text);
            if(!Target.AccessibilityLabel.empty()) {
                // GTK's label child relation otherwise wins over an explicit
                // canonical AccessibilityLabel and may be recreated by Text.
                gtk_accessible_reset_relation(GTK_ACCESSIBLE(Target.Native),GTK_ACCESSIBLE_RELATION_LABELLED_BY);
                gtk_accessible_update_property(GTK_ACCESSIBLE(Target.Native),GTK_ACCESSIBLE_PROPERTY_LABEL,Target.AccessibilityLabel.c_str(),-1);
            }
        } else if(Property=="Visible") {
            Target.Visible=Value=="true";
            if(Target.ClassName=="Window") {if(Target.Visible && !Target.Closed) gtk_window_present(GTK_WINDOW(Target.Native)); else gtk_widget_set_visible(Target.Native,false);}
            else gtk_widget_set_visible(Target.Slot,Target.Visible);
        } else if(Property=="Enabled") {Target.Enabled=Value=="true"; gtk_widget_set_sensitive(Target.Native,Target.Enabled);}
        else if(Property=="Checked") gtk_check_button_set_active(GTK_CHECK_BUTTON(Target.Native),Value=="true");
        else if(Property=="Minimum" || Property=="Maximum" || Property=="Value") {
            char* End=nullptr; const double Number=g_ascii_strtod(Text,&End);
            if(!std::isfinite(Number) || !End || *End) return Self.Fail();
            if(Property=="Minimum") Target.Minimum=Number; else if(Property=="Maximum") Target.Maximum=Number; else Target.Value=Number;
            if(Target.ClassName=="Slider") {
                auto* Adjustment=gtk_range_get_adjustment(GTK_RANGE(Target.Native));
                gtk_adjustment_configure(Adjustment,Target.Value,Target.Minimum,Target.Maximum,1,10,0);
            } else if(Target.ClassName=="ProgressBar") gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(Target.Native),Target.Maximum>Target.Minimum?std::clamp((Target.Value-Target.Minimum)/(Target.Maximum-Target.Minimum),0.0,1.0):0);
        } else if(Property=="AccessibilityLabel") {
            Target.AccessibilityLabel=Value;
            if(Value.empty()) {
                gtk_accessible_reset_property(GTK_ACCESSIBLE(Target.Native),GTK_ACCESSIBLE_PROPERTY_LABEL);
                GtkWidget* Label=Target.ClassName=="TextButton"?gtk_button_get_child(GTK_BUTTON(Target.Native)):
                    Target.ClassName=="CheckBox"?gtk_check_button_get_child(GTK_CHECK_BUTTON(Target.Native)):nullptr;
                if(Label) gtk_accessible_update_relation(GTK_ACCESSIBLE(Target.Native),GTK_ACCESSIBLE_RELATION_LABELLED_BY,Label,nullptr,-1);
            } else {
                gtk_accessible_reset_relation(GTK_ACCESSIBLE(Target.Native),GTK_ACCESSIBLE_RELATION_LABELLED_BY);
                gtk_accessible_update_property(GTK_ACCESSIBLE(Target.Native),GTK_ACCESSIBLE_PROPERTY_LABEL,Text,-1);
            }
        } else if(Property=="AccessibilityDescription") {
            Target.AccessibilityDescription=Value;
            if(Value.empty()) gtk_accessible_reset_property(GTK_ACCESSIBLE(Target.Native),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION);
            else gtk_accessible_update_property(GTK_ACCESSIBLE(Target.Native),GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,Text,-1);
        } else if(Property=="Source" && Target.ClassName=="ImageLabel") {
            if(Value.empty()) Self.ReleaseImage(Target);
        }
        return 1;
    } catch(...) {return Self.Fail();}
}
int Backend::Parent(void* Context,int Id,int ParentId) {
    auto& Self=*static_cast<Backend*>(Context); auto Found=Self.Views.find(Id); if(Found==Self.Views.end() || !Self.Owner()) return Self.Fail();
    auto& Target=*Found->second; if(!Target.Slot) return ParentId?Self.Fail():1;
    GtkWidget* Container=nullptr;
    if(ParentId) {auto Parent=Self.Views.find(ParentId); if(Parent==Self.Views.end() || !(Container=Parent->second->Container)) return Self.Fail();}
    LuiGtkPlace(Container,Target.Slot); Target.ParentId=ParentId; Target.PaintRank=-1; return 1;
}
int Backend::Arrange(void* Context,int Id,double X,double Y,double Width,double Height,int ResizeWindow) {
    auto& Self=*static_cast<Backend*>(Context); auto Found=Self.Views.find(Id); if(Found==Self.Views.end() || !Self.Owner()) return Self.Fail();
    if(!std::isfinite(X) || !std::isfinite(Y) || !std::isfinite(Width) || !std::isfinite(Height) || Width<0 || Height<0 ||
        std::abs(X)>1000000 || std::abs(Y)>1000000 || Width>1000000 || Height>1000000) return Self.Fail();
    auto& Target=*Found->second; Target.X=X; Target.Y=Y; Target.Width=Width; Target.Height=Height;
    if(Target.ClassName=="Window") {if(ResizeWindow) gtk_window_set_default_size(GTK_WINDOW(Target.Native),std::max(1,static_cast<int>(std::round(Width))),std::max(1,static_cast<int>(std::round(Height))));}
    else {
        double ParentX=0,ParentY=0; auto Parent=Self.Views.find(Target.ParentId);
        if(Parent!=Self.Views.end()) {ParentX=Parent->second->X; ParentY=Parent->second->Y;}
        LuiGtkRect(Target.Slot,X-ParentX,Y-ParentY,Width,Height);
    }
    return 1;
}
int Backend::Present(void* Context,int Id,int HasClip,double X,double Y,double Width,double Height,int Rank,int InputVisible) {
    auto& Self=*static_cast<Backend*>(Context); auto Found=Self.Views.find(Id);
    if(!Self.Owner() || Found==Self.Views.end()) return Self.Fail();
    auto& Target=*Found->second; Target.InputVisible=InputVisible!=0;
    if(Target.ClassName=="Window") Target.PresentationPending=true;
    if(!Target.Container) gtk_widget_set_sensitive(Target.Native,Target.Enabled && Target.InputVisible);
    auto* Widget=Target.Slot?Target.Slot:Target.Container;
    if(Target.Container) gtk_widget_set_overflow(Widget,(HasClip&2)?GTK_OVERFLOW_HIDDEN:GTK_OVERFLOW_VISIBLE);
    LuiGtkClip(Widget,HasClip!=0,X,Y,Width,Height);
    if(Target.InputVisible==false && gtk_widget_has_focus(Target.Native)) {
        if(auto* Root=gtk_widget_get_root(Target.Native); Root && GTK_IS_WINDOW(Root)) gtk_window_set_focus(GTK_WINDOW(Root),nullptr);
    }
    if(Target.Slot && Target.PaintRank!=Rank) {
        Target.PaintRank=Rank;
        if(auto* Parent=gtk_widget_get_parent(Target.Slot)) {
            std::vector<View*> Siblings;
            for(auto& Pair:Self.Views) if(Pair.second->Slot && Pair.second->ParentId==Target.ParentId) Siblings.push_back(Pair.second.get());
            std::stable_sort(Siblings.begin(),Siblings.end(),[](View* Left,View* Right){return Left->PaintRank<Right->PaintRank;});
            GtkWidget* Previous=nullptr;
            for(auto* Sibling:Siblings) {gtk_widget_insert_after(Sibling->Slot,Parent,Previous); Previous=Sibling->Slot;}
        }
    }
    return 1;
}
int Backend::Destroy(void* Context,int Id) {
    auto& Self=*static_cast<Backend*>(Context); auto Found=Self.Views.find(Id); if(Found==Self.Views.end()) return 1;
    auto Target=std::move(Found->second); Self.Views.erase(Found); Target->Alive=false;
    if(Target->ClassName=="ImageLabel") Self.ReleaseImage(*Target);
    g_signal_handlers_disconnect_by_data(Target->Native,Target.get());
    if(Target->Clock) g_signal_handlers_disconnect_by_data(Target->Clock,Target.get());
    for(auto* Controller:Target->Controllers) g_signal_handlers_disconnect_by_data(Controller,Target.get());
    for(auto* Source:Target->TextSources) g_signal_handlers_disconnect_by_data(Source,Target.get());
    if(Target->ClassName=="Window") gtk_window_destroy(GTK_WINDOW(Target->Native));
    else {LuiGtkPlace(nullptr,Target->Slot); g_object_unref(Target->Slot);}
    g_object_unref(Target->Native); if(Target->Style) g_object_unref(Target->Style); return 1;
}
}
