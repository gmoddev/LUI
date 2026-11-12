#include "Backend.h"
#include "LuiProject.h"
#include "LuiApplicationServices.h"
#include "Files.h"
#include "../conversation/Fixture.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <cstdio>
#include <stdexcept>
#include <fstream>
#include <unistd.h>
#include <algorithm>

namespace Lui::Gtk {
// Explicit private test interop; never part of the public Luau or extension API.
struct Qualification {
    static GtkWidget* Native(Backend& Host,int Id) {return Host.Views.at(Id)->Native;}
    static GtkWidget* Slot(Backend& Host,int Id) {return Host.Views.at(Id)->Slot;}
    static GObject* Controller(Backend& Host,int Id,GType Type) {
        for(auto* Value:Host.Views.at(Id)->Controllers) if(G_TYPE_CHECK_INSTANCE_TYPE(Value,Type)) return Value;
        throw std::runtime_error("controller absent");
    }
    static GObject* BubbleKey(Backend& Host,int Id) {
        for(auto* Value:Host.Views.at(Id)->Controllers) if(GTK_IS_EVENT_CONTROLLER_KEY(Value) &&
            gtk_event_controller_get_propagation_phase(GTK_EVENT_CONTROLLER(Value))==GTK_PHASE_BUBBLE) return Value;
        throw std::runtime_error("bubble key controller absent");
    }
    static size_t Events(Backend& Host) {return Host.Events.size();}
    static uint64_t Dropped(Backend& Host) {return Host.Dropped;}
    static size_t Views(Backend& Host) {return Host.Views.size();}
    static std::string Style(Backend& Host,int Id) {return Host.Views.at(Id)->StyleJson;}
    static size_t CodeTags(Backend& Host,int Id) {return Host.Views.at(Id)->CodeTags.size();}
    static GtkTextTag* CodeTag(Backend& Host,int Id,int Index) {return Host.Views.at(Id)->CodeTags.at(static_cast<size_t>(Index));}
    static size_t ImageBytes(Backend& Host) {return Host.ImageCacheUsage();}
    static size_t ImageRequests(Backend& Host) {return Host.ImageRequestCount();}
    static size_t ImageStaging(Backend& Host) {return Host.ImageStagingUsage();}
};
}
using Lui::Gtk::Backend;
using Lui::Gtk::Qualification;
using Json=nlohmann::json;
static void Require(bool Value,const char* Label) {if(!Value) throw std::runtime_error(Label);}
static Json Tree(Backend& Host) {const char* Data=Lui_GetPreviewTreeJson(Host.GetRuntime()); Require(Data,"tree unavailable"); return Json::parse(Data);}
static int Id(Backend& Host,const char* Name) {for(const auto& Node:Tree(Host)) if(Node.at("name")==Name) return Node.at("id"); throw std::runtime_error("node absent");}
static void Settle(Backend& Host) {for(int Index=0;Index<24;++Index) {Host.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(5));}}
static void Run(Backend& Host,const char* Source,const char* Phase="script") {
    const bool Passed=Lui_RunScript(Host.GetRuntime(),Source,"Qualification");
    if(!Passed) throw std::runtime_error(std::string(Phase)+": "+Lui_GetLastError(Host.GetRuntime()));
}
static void Click(Backend& Host,int Id) {g_signal_emit_by_name(Qualification::Native(Host,Id),"clicked"); Host.Pump();}
static void Log(void* Context,const char*,const char* Text) {static_cast<std::string*>(Context)->append(Text);}

static void Controls() {
    Backend Host; std::string Logs; Lui_SetLogCallback(Host.GetRuntime(),&Logs,Log);
    const unsigned char Pixel[]{0,0,0,255}; auto* PixelBytes=g_bytes_new(Pixel,sizeof(Pixel));
    auto* Texture=gdk_memory_texture_new(1,1,GDK_MEMORY_R8G8B8A8,PixelBytes,4); g_bytes_unref(PixelBytes);
    auto* Encoded=gdk_texture_save_to_png_bytes(GDK_TEXTURE(Texture)); g_object_unref(Texture);
    gsize Length=0; auto* Data=static_cast<const char*>(g_bytes_get_data(Encoded,&Length)); Host.SetAsset("Pixel.png",std::string(Data,Length)); g_bytes_unref(Encoded);
    Run(Host,R"(
        W=Instance.new('Window',{Title='GTK controls',Size=UDim2.fromOffset(420,340)})
        Root=Instance.new('Frame',{Name='Root',Position=UDim2.fromOffset(10,20),Size=UDim2.fromOffset(390,300),Parent=W})
        Input=Instance.new('TextBox',{Name='Input',Text='initial',AccessibilityLabel='Input label',AccessibilityDescription='Input description',Position=UDim2.fromOffset(12,15),Size=UDim2.fromOffset(200,36),Parent=Root})
        Input.TextChanged:Connect(function() LastText=Input.Text; TextCalls=(TextCalls or 0)+1 end)
        Check=Instance.new('CheckBox',{Name='Check',Text='Agreement',Checked=true,Position=UDim2.fromOffset(12,60),Size=UDim2.fromOffset(200,36),Parent=Root})
        Check.CheckedChanged:Connect(function() LastChecked=Check.Checked; CheckCalls=(CheckCalls or 0)+1 end)
        Slider=Instance.new('Slider',{Name='Slider',Minimum=10,Maximum=20,Value=15,Position=UDim2.fromOffset(12,110),Size=UDim2.fromOffset(200,36),Parent=Root})
        Slider.ValueChanged:Connect(function() LastValue=Slider.Value; ValueCalls=(ValueCalls or 0)+1 end)
        Progress=Instance.new('ProgressBar',{Name='Progress',Minimum=10,Maximum=20,Value=14,Position=UDim2.fromOffset(12,160),Size=UDim2.fromOffset(200,24),Parent=Root})
        Button=Instance.new('TextButton',{Name='Button',Text='Action',Position=UDim2.fromOffset(12,200),Size=UDim2.fromOffset(200,36),Parent=Root})
        Button.Activated:Connect(function() Clicks=(Clicks or 0)+1 end)
        Button.InputBegan:Connect(function(Value) if Value.Device=='Keyboard' then LastKey=Value.Key end end)
        Hidden=Instance.new('Frame',{Name='Hidden',Visible=false,Parent=Root})
        Picture=Instance.new('ImageLabel',{Name='Picture',Source='Pixel.png',Position=UDim2.fromOffset(250,15),Size=UDim2.fromOffset(24,24),Parent=Root})
        W.Visible=true
    )","controls bootstrap");
    Settle(Host);
    const int Input=Id(Host,"Input"), Check=Id(Host,"Check"), Slider=Id(Host,"Slider"), Progress=Id(Host,"Progress"), Button=Id(Host,"Button");
    Require(GTK_IS_ENTRY(Qualification::Native(Host,Input)) && GTK_IS_CHECK_BUTTON(Qualification::Native(Host,Check)) && GTK_IS_SCALE(Qualification::Native(Host,Slider)),"control classes mismatch");
    Require(std::string(gtk_editable_get_text(GTK_EDITABLE(Qualification::Native(Host,Input))))=="initial","native text missing");
    Require(gtk_check_button_get_active(GTK_CHECK_BUTTON(Qualification::Native(Host,Check))),"native check missing");
    Require(gtk_range_get_value(GTK_RANGE(Qualification::Native(Host,Slider)))==15,"native range missing");
    Require(std::abs(gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(Qualification::Native(Host,Progress)))-0.4)<0.001,"native progress mismatch");
    Require(!gtk_widget_get_visible(Qualification::Slot(Host,Id(Host,"Hidden"))),"hidden frame rendered");
    Require(gtk_accessible_get_accessible_role(GTK_ACCESSIBLE(Qualification::Native(Host,Input)))==GTK_ACCESSIBLE_ROLE_TEXT_BOX,"accessibility role mismatch");
    Require(gtk_picture_get_paintable(GTK_PICTURE(Qualification::Native(Host,Id(Host,"Picture"))))!=nullptr,"declared image did not decode");
    auto* Slot=Qualification::Slot(Host,Input);
    Require(gtk_widget_get_width(Slot)==200 && gtk_widget_get_height(Slot)==36,"canonical allocation changed by toolkit");
    graphene_point_t Origin{0,0}, Position{};
    Require(gtk_widget_compute_point(Slot,Qualification::Native(Host,Id(Host,"Root")),&Origin,&Position) && Position.x==12 && Position.y==15,"parent-local geometry mismatch");
    gtk_editable_set_text(GTK_EDITABLE(Qualification::Native(Host,Input)),"native edit");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(Qualification::Native(Host,Check)),false);
    gtk_range_set_value(GTK_RANGE(Qualification::Native(Host,Slider)),17);
    Host.Pump(); Run(Host,"assert(LastText=='native edit' and LastChecked==false and LastValue==17)","native edits");
    // Bootstrap/property application must not synthesize user activation/change.
    Run(Host,"assert(Clicks==nil); BeforeTextCalls=TextCalls; BeforeCheckCalls=CheckCalls; BeforeValueCalls=ValueCalls; Input.Text='program'; Check.Checked=true; Slider.Value=12");
    Host.Pump(); Run(Host,"assert(LastText=='program' and LastChecked==true and LastValue==12 and TextCalls==BeforeTextCalls+1 and CheckCalls==BeforeCheckCalls+1 and ValueCalls==BeforeValueCalls+1)","setter feedback");
    Click(Host,Button); Run(Host,"assert(Clicks==1)");
    Run(Host,"Button.Enabled=false"); Click(Host,Button); Run(Host,"assert(Clicks==1); Button.Enabled=true");
    gtk_widget_grab_focus(Qualification::Native(Host,Button)); Settle(Host);
    gboolean Handled=false;
    g_signal_emit_by_name(Qualification::Controller(Host,Button,GTK_TYPE_EVENT_CONTROLLER_KEY),"key-pressed",GDK_KEY_a,0,GdkModifierType(0),&Handled);
    Host.Pump(); Run(Host,"assert(LastKey=='A' and Button.IsFocused)","key focus");
    g_signal_emit_by_name(Qualification::Controller(Host,Button,GTK_TYPE_EVENT_CONTROLLER_KEY),"key-released",GDK_KEY_a,0,GdkModifierType(0));
    Host.Pump();
    // A tiny logical allocation remains tiny despite the GTK button minimum.
    Run(Host,"Button.Size=UDim2.fromOffset(3,4)"); Settle(Host);
    Require(gtk_widget_get_width(Qualification::Slot(Host,Button))==3 && gtk_widget_get_height(Qualification::Slot(Host,Button))==4,"toolkit minimum enlarged logical bounds");
    auto* Retained=Qualification::Native(Host,Button); g_object_ref(Retained);
    g_signal_emit_by_name(Retained,"clicked"); Run(Host,"Button:Destroy()"); Host.Pump(); Run(Host,"assert(Clicks==1)");
    g_signal_emit_by_name(Retained,"clicked"); Require(Qualification::Events(Host)==0,"dead widget retained callback"); g_object_unref(Retained);
    const int InputAgain=Id(Host,"Input");
    Run(Host,"Input.Sensitive=true; Input.Text='GTK_PRIVATE_CANARY'");
    Require(std::string(gtk_editable_get_text(GTK_EDITABLE(Qualification::Native(Host,InputAgain))))=="GTK_PRIVATE_CANARY","redaction changed native content");
    Require(Tree(Host).dump().find("GTK_PRIVATE_CANARY")==std::string::npos && Logs.find("GTK_PRIVATE_CANARY")==std::string::npos,"GTK private diagnostic leaked");
    g_object_set(gtk_settings_get_default(),"gtk-application-prefer-dark-theme",TRUE,nullptr); Host.Pump();
    Run(Host,"assert(app:GetService('ThemeService').CurrentTheme=='Dark')");
    Host.Stop(); Require(Qualification::Views(Host)==0,"native teardown incomplete"); Host.Stop();
}
static void Modular() {
    const std::string Path=LUI_MODULAR_MANIFEST; LuiProjectOptionsV1 Options{}; Options.StructSize=sizeof(Options); Options.AbiVersion=1;
    LuiProject* Project=nullptr; char Error[256]{};
    Require(Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Project,Error,sizeof(Error)),Error);
    for(int Generation=0;Generation<2;++Generation) {
        Backend Host; LuiProjectRunOptionsV1 Run{sizeof(Run),1,0,0,0};
        Require(Lui_RunProject(Host.GetRuntime(),Project,&Run),Lui_GetLastError(Host.GetRuntime())); Settle(Host);
        Click(Host,Id(Host,"Increment"));
        bool Changed=false; for(const auto& Node:Tree(Host)) Changed|=Node.at("text")=="Actions: 1"; Require(Changed,"modular activation failed");
        Click(Host,Id(Host,"ShowLibrary")); Require(Id(Host,"Library")>0,"modular library missing");
        Click(Host,Id(Host,"Project_Workshop"));
        Require(Tree(Host).dump().find("Selected: Workshop")!=std::string::npos,"modular selection failed");
        Click(Host,Id(Host,"ShowHome")); Require(Tree(Host).dump().find("Actions: 1")!=std::string::npos,"modular state lost");
        Require(Tree(Host).dump().find("SYNTHETIC_PRIVATE_CANARY")==std::string::npos,"modular canary leaked");
        const int Button=Id(Host,"Increment");
        for(int Index=0;Index<600;++Index) g_signal_emit_by_name(Qualification::Native(Host,Button),"clicked");
        Require(Qualification::Events(Host)<=256 && Qualification::Dropped(Host)>0,"native queue unbounded"); Host.Pump();
        const int Window=Id(Host,"ModularWorkspace"); gtk_window_close(GTK_WINDOW(Qualification::Native(Host,Window))); Host.Pump();
        Require(!Host.HasWindows(),"native close did not retire application"); Host.Stop();
    }
    Lui_ProjectRelease(Project);
}
static void Clipboard() {
    Backend Denied;
    Run(Denied,"assert(not pcall(function() app:GetService('ClipboardService') end)); assert(not pcall(function() app:GetService('DialogService') end))","platform capability denial"); Denied.Stop();
    Backend Host; std::string Logs; Lui_SetLogCallback(Host.GetRuntime(),&Logs,Log);
    LuiCapabilityDeclarationV1 Caps{sizeof(Caps),1,LUI_CAPABILITY_CLIPBOARD|LUI_CAPABILITY_DIALOGS};
    Require(Lui_DeclareCapabilities(Host.GetRuntime(),&Caps),"clipboard grants failed");
    Run(Host,R"(
        assert(not pcall(function() app:GetService('DialogService'):OpenFile(function() end) end))
        local Clipboard=app:GetService('ClipboardService')
        Clipboard:WriteText('GTK_CLIPBOARD_CANARY_😀')
        Clipboard:ReadText(function(Value,Error) ClipboardValue=Value; ClipboardError=Error; Done=true end)
    )","clipboard request");
    for(int Index=0;Index<100;++Index) {Host.Pump(); g_usleep(5000);}
    Run(Host,"assert(Done and ClipboardError==nil and ClipboardValue=='GTK_CLIPBOARD_CANARY_😀')","clipboard result");
    Require(Logs.find("CANARY")==std::string::npos,"clipboard leaked into diagnostics");
    std::string Large(1024*1024+1,'x'); auto* Bytes=g_bytes_new(Large.data(),Large.size());
    auto* Provider=gdk_content_provider_new_for_bytes("text/plain;charset=utf-8",Bytes); g_bytes_unref(Bytes);
    Require(gdk_clipboard_set_content(gdk_display_get_clipboard(gdk_display_get_default()),Provider),"clipboard fixture failed"); g_object_unref(Provider);
    Run(Host,"LargeDone=false; app:GetService('ClipboardService'):ReadText(function(Value,Error) assert(Value==nil and Error~=nil); LargeDone=true end)");
    for(int Index=0;Index<100;++Index) {Host.Pump(); g_usleep(5000);}
    Run(Host,"assert(LargeDone)","clipboard read bound");
    Run(Host,"app:GetService('ClipboardService'):ReadText(function() error('RETIRED_CLIPBOARD_CANARY') end)");
    Host.Stop();
    for(int Index=0;Index<20;++Index) {while(g_main_context_pending(nullptr)) g_main_context_iteration(nullptr,false); g_usleep(1000);}
    Require(Logs.find("CANARY")==std::string::npos,"retired clipboard callback delivered");
}
static GtkWindow* Chooser(Backend& Host) {
    for(int Attempt=0;Attempt<200;++Attempt) {
        Host.Pump(); auto* Windows=gtk_window_get_toplevels();
        for(guint Index=0;Index<g_list_model_get_n_items(Windows);++Index) {
            auto* Window=GTK_WINDOW(g_list_model_get_item(Windows,Index));
            const bool Found=GTK_IS_FILE_CHOOSER(Window) && gtk_widget_get_visible(GTK_WIDGET(Window));
            g_object_unref(Window); if(Found) return Window;
        }
        g_usleep(5000);
    }
    throw std::runtime_error("file chooser absent");
}
static void Dialogs() {
    Backend Host; std::string Logs; Lui_SetLogCallback(Host.GetRuntime(),&Logs,Log);
    LuiCapabilityDeclarationV1 Caps{sizeof(Caps),1,LUI_CAPABILITY_DIALOGS};
    Require(Lui_DeclareCapabilities(Host.GetRuntime(),&Caps),"dialog grant failed");
    Run(Host,"W=Instance.new('Window',{Title='Dialog qualification'}); W.Visible=true"); Settle(Host);
    Run(Host,"app:GetService('DialogService'):OpenFile(function(Value,Error) Path=Value; DialogError=Error; DialogDone=true end)");
    auto* Window=Chooser(Host);
    Require(!gtk_window_get_modal(Window),"chooser unexpectedly modal");
    Run(Host,"assert(not pcall(function() app:GetService('DialogService'):OpenFile(function() error('EXTRA_DIALOG_CANARY') end) end))","single chooser admission");
    GFileIOStream* Stream=nullptr; GError* Error=nullptr;
    auto* File=g_file_new_tmp("lui-dialog-CANARY-XXXXXX",&Stream,&Error);
    Require(File && !Error,"dialog fixture failed"); g_io_stream_close(G_IO_STREAM(Stream),nullptr,nullptr); g_object_unref(Stream);
    Require(gtk_file_chooser_set_file(GTK_FILE_CHOOSER(Window),File,&Error) && !Error,"dialog selection failed"); Settle(Host);
    char* Expected=g_file_get_path(File);
    gtk_dialog_response(GTK_DIALOG(Window),GTK_RESPONSE_ACCEPT); Settle(Host);
    Run(Host,"assert(DialogDone and DialogError==nil and string.find(Path,'lui%-dialog%-CANARY'))","dialog result");
    Run(Host,("assert(Path=="+Json(Expected).dump()+")").c_str(),"selected local path mismatch");
    Require(Logs.find("CANARY")==std::string::npos,"dialog path leaked into logs");
    g_free(Expected); g_file_delete(File,nullptr,nullptr); g_object_unref(File);
    Run(Host,"DialogDone=false; app:GetService('DialogService'):OpenFile(function(Value,Error) assert(Value==nil and Error==nil); DialogDone=true end)");
    gtk_dialog_response(GTK_DIALOG(Chooser(Host)),GTK_RESPONSE_CANCEL); Settle(Host);
    Run(Host,"assert(DialogDone)","dialog dismissal result");
    Run(Host,"app:GetService('DialogService'):OpenFile(function() error('RETIRED_DIALOG_CANARY') end)"); Chooser(Host);
    Host.Stop();
    for(int Index=0;Index<40;++Index) {while(g_main_context_pending(nullptr)) g_main_context_iteration(nullptr,false); g_usleep(1000);}
    Require(Logs.find("CANARY")==std::string::npos,"retired dialog callback delivered");
}
static void Viewports() {
    Backend Host;
    Run(Host,R"(
        W=Instance.new('Window',{Title='Viewport qualification',Size=UDim2.fromOffset(320,240)})
        S=Instance.new('ScrollingFrame',{Name='Scroll',Parent=W,Position=UDim2.fromOffset(20,20),Size=UDim2.fromOffset(120,100),CanvasSize=UDim2.fromOffset(120,400)})
        A=Instance.new('TextButton',{Name='Clipped',Parent=S,Position=UDim2.fromOffset(0,90),Size=UDim2.fromOffset(120,40),Text='Partial'})
        B=Instance.new('TextButton',{Name='Overlay',Parent=S,Size=UDim2.fromOffset(80,40),Text='On top',ZIndex=5})
        Calls=0; S.CanvasChanged:Connect(function() Calls+=1 end)
        W.Visible=true
    )"); Settle(Host);
    const int Scroll=Id(Host,"Scroll"), Clipped=Id(Host,"Clipped"), Overlay=Id(Host,"Overlay");
    int Hit=0; Require(Lui_HitTest(Host.GetRuntime(),Id(Host,"Window"),30,30,&Hit) && Hit==Overlay,"native canonical hit test diverged");
    auto* Root=gtk_window_get_child(GTK_WINDOW(Qualification::Native(Host,Id(Host,"Window"))));
    auto* Outside=gtk_widget_pick(Root,30,135,GTK_PICK_DEFAULT);
    Require(!Outside || !gtk_widget_is_ancestor(Outside,Qualification::Slot(Host,Clipped)),"GTK picked clipped content");
    Require(Lui_ScrollInput(Host.GetRuntime(),Scroll,0,40),"GTK logical scroll rejected"); Settle(Host);
    Run(Host,"assert(S.AbsoluteCanvasPosition.Y==40 and A.AbsolutePosition.Y==70 and Calls==2)","GTK scroll geometry");
    Require(!gtk_widget_get_sensitive(Qualification::Native(Host,Overlay)),"offscreen native control remained editable");
    Require(Lui_ScrollInput(Host.GetRuntime(),Scroll,0,-40),"GTK reverse scroll rejected"); Settle(Host);
    Require(gtk_widget_get_sensitive(Qualification::Native(Host,Overlay)),"visible native control did not recover");
    auto* Controller=Qualification::Controller(Host,Scroll,GTK_TYPE_EVENT_CONTROLLER_SCROLL);
    Require(gtk_event_controller_get_propagation_phase(GTK_EVENT_CONTROLLER(Controller))==GTK_PHASE_BUBBLE,"nearest scroller must receive native wheel first");
    gboolean Handled=false; g_signal_emit_by_name(Controller,"scroll",0.0,1.0,&Handled); Host.Pump();
    Require(Handled,"native scroll was not consumed");
    Run(Host,"assert(S.AbsoluteCanvasPosition.Y>0)","native scroll controller");
    Run(Host,"S.BorderSizePixel=8; S.CornerRadius=12; S.Opacity=0.75; S.TextSize=22; S.BackgroundColor3=Color3.fromRGB(40,80,120)"); Settle(Host);
    Require(gtk_widget_get_width(Qualification::Native(Host,Scroll))==120 && gtk_widget_get_height(Qualification::Native(Host,Scroll))==100,"style changed canonical viewport extent");
    Require(std::abs(gtk_widget_get_opacity(Qualification::Slot(Host,Scroll))-0.75)<0.001,"canonical opacity did not map");
    graphene_point_t Origin{0,0}, Point{};
    Require(gtk_widget_compute_point(Qualification::Slot(Host,Clipped),Qualification::Native(Host,Scroll),&Origin,&Point) && Point.x==0 && Point.y==50,"decorative border shifted content origin");
    Run(Host,"S:Destroy(); S:Destroy()"); Settle(Host);
    Require(Qualification::Views(Host)==1,"viewport teardown leaked views");
}

static void AdvancedText() {
    Backend Host;
    Run(Host,R"(
        W=Instance.new('Window',{Title='Advanced text qualification',Size=UDim2.fromOffset(400,260)})
        Input=Instance.new('TextBox',{Parent=W,Multiline=true,PlaceholderText='Compose a message',Text='A\u{1F642}B\nC',SelectionStart=1,SelectionLength=1,Size=UDim2.fromOffset(280,140)})
        Password=Instance.new('TextBox',{Parent=W,Secret=true,Text='SECRET_WIDGET_CANARY',Position=UDim2.fromOffset(0,160),Size=UDim2.fromOffset(280,40)})
        Starts,Changes,Ends=0,0,0
        Input.CompositionStarted:Connect(function() Starts+=1 end)
        Input.CompositionChanged:Connect(function() Changes+=1 end)
        Input.CompositionEnded:Connect(function() Ends+=1 end)
        W.Visible=true
    )"); Settle(Host);
    auto* Input=Qualification::Native(Host,2);
    auto* Password=Qualification::Native(Host,3);
    Require(GTK_IS_TEXT_VIEW(Input) && GTK_IS_ENTRY(Password) && !gtk_entry_get_visibility(GTK_ENTRY(Password)),"multiline/password mapping failed");
    auto* Buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(Input)); GtkTextIter First,Last;
    Require(gtk_text_buffer_get_selection_bounds(Buffer,&First,&Last) && gtk_text_iter_get_offset(&First)==1 && gtk_text_iter_get_offset(&Last)==2,"scalar selection split emoji");
    gtk_text_buffer_set_text(Buffer,"native old",-1);
    Require(Qualification::Events(Host)>0,"stale native multiline fixture did not queue an edit");
    Run(Host,"Input.Text='script current'; Input.SelectionStart=2; Input.SelectionLength=3"); Host.Pump();
    Run(Host,"assert(Input.Text=='script current' and Input.SelectionStart==2 and Input.SelectionLength==3)","script writes retire queued native multiline edits");
    Run(Host,"Input.Text='A\\u{1F642}B\\nC'; Input.SelectionStart=1; Input.SelectionLength=1");
    g_signal_emit_by_name(Input,"preedit-changed","\xE6\x96\x87"); Host.Pump();
    Run(Host,"assert(Input.IsComposing and Starts==1)","native preedit start");
    g_signal_emit_by_name(Input,"preedit-changed","\xE6\x96\x87\xE5\xAD\x97");
    gtk_text_buffer_set_text(Buffer,"A\xF0\x9F\x99\x82\xE6\x96\x87",-1);
    g_signal_emit_by_name(Input,"preedit-changed",""); Host.Pump();
    Run(Host,"assert(not Input.IsComposing and Input.Text=='A\\u{1F642}\\u{6587}' and Starts==1 and Changes==1 and Ends==1)","native composition commit");
    Run(Host,"Save=Instance.new('TextButton',{Parent=W,Name='Save',Text='Save'});Save.Activated:Connect(function() Saved=Input.Text;SavedComposing=Input.IsComposing end)");
    g_signal_emit_by_name(Input,"preedit-changed","ni");
    gtk_text_buffer_set_text(Buffer,"\xE4\xBD\xA0\xE5\xA5\xBD",-1);
    g_signal_emit_by_name(Input,"preedit-changed","");
    g_signal_emit_by_name(Qualification::Native(Host,Id(Host,"Save")),"clicked");
    Host.Pump();
    Run(Host,"assert(Saved=='\\u{4F60}\\u{597D}' and not SavedComposing)","IME commit precedes following native Save in one GTK batch");
    // Include GTK's other commit ordering: empty preedit before buffer change.
    g_signal_emit_by_name(Input,"preedit-changed","first");
    g_signal_emit_by_name(Input,"preedit-changed","");
    gtk_text_buffer_set_text(Buffer,"first",-1);
    g_signal_emit_by_name(Qualification::Native(Host,Id(Host,"Save")),"clicked");
    g_signal_emit_by_name(Input,"preedit-changed","second");
    gtk_text_buffer_set_text(Buffer,"second",-1);
    g_signal_emit_by_name(Input,"preedit-changed","");
    Host.Pump();
    Run(Host,"assert(Saved=='first' and Input.Text=='second' and not Input.IsComposing and Starts==4 and Ends==4)","two IME commits preserve intervening Save boundary");
    g_signal_emit_by_name(Input,"preedit-changed","first");
    gtk_text_buffer_set_text(Buffer,"first",-1);
    g_signal_emit_by_name(Input,"preedit-changed","");
    g_signal_emit_by_name(Qualification::Native(Host,Id(Host,"Save")),"clicked");
    gtk_text_buffer_set_text(Buffer,"ordinary",-1);
    Host.Pump();
    Run(Host,"assert(Saved=='first' and Input.Text=='ordinary' and not Input.IsComposing)","ordinary edit cannot rewrite an earlier IME/Save boundary");
    g_signal_emit_by_name(Input,"preedit-changed","old");
    gtk_text_buffer_set_text(Buffer,"old",-1);
    g_signal_emit_by_name(Input,"preedit-changed","");
    Run(Host,"Input.Text='script'"); Host.Pump();
    gtk_text_buffer_get_end_iter(Buffer,&Last); gtk_text_buffer_insert(Buffer,&Last,"x",1); Host.Pump();
    Run(Host,"assert(Input.Text=='scriptx' and not Input.IsComposing);Save:Destroy()","retired IME revision does not suppress subsequent native editing");
    Run(Host,"Input.Multiline=false; Input.Text='normal'"); Settle(Host);
    gtk_editable_set_text(GTK_EDITABLE(Qualification::Native(Host,2)),"native old");
    Run(Host,"Input.Text='script current'"); Host.Pump();
    Run(Host,"assert(Input.Text=='script current'); Input.Secret=true; assert(Input.EffectiveSensitive)"); Settle(Host);
    auto* Replaced=Qualification::Native(Host,2);
    Require(GTK_IS_ENTRY(Replaced) && !gtk_entry_get_visibility(GTK_ENTRY(Replaced)),"replacement did not mask text");
    Run(Host,"Input.Secret=false; assert(Input.EffectiveSensitive); Input:Destroy(); Input:Destroy()"); Settle(Host);
    Require(Qualification::Views(Host)==2,"text replacement/destruction leaked native views");
    const auto Snapshot=Tree(Host).dump(); Require(Snapshot.find("SECRET_WIDGET_CANARY")==std::string::npos,"secret widget leaked into preview");
}

static void CodeEditor() {
    Backend Host;
    Run(Host,R"(
        W=Instance.new('Window',{Title='GTK CodeEditor',Visible=true,Size=UDim2.fromOffset(420,280)})
        Editor=Instance.new('CodeEditor',{Name='CodeEditor',Parent=W,Text='A\u{1F600}\nreturn',SelectionStart=1,SelectionLength=1,Size=UDim2.fromOffset(340,200)})
        Changes=0
        Editor.TextChanged:Connect(function()
            Changes+=1
            assert(not Editor:SetStyles(Editor.TextRevision,{{Start=0,End=1,Color=0xD12A44}}))
        end)
        assert(Editor:IsA('TextBox') and Editor.Multiline and Editor.TextRevision==2)
        assert(not Editor:SetStyles(Editor.TextRevision,{{Start=0,End=1,Color=0xD12A44}}))
    )","CodeEditor bootstrap"); Settle(Host);
    const int EditorId=Id(Host,"CodeEditor");
    auto* Native=Qualification::Native(Host,EditorId);
    Require(GTK_IS_TEXT_VIEW(Native),"CodeEditor did not own GtkTextView");
    auto* Buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(Native));
    GtkTextIter First,Last;
    gtk_text_buffer_get_selection_bounds(Buffer,&First,&Last);
    Require(gtk_text_iter_get_offset(&First)==1 && gtk_text_iter_get_offset(&Last)==2,
        "CodeEditor selection split Unicode scalar");
    gtk_text_buffer_get_start_iter(Buffer,&First);
    Require(Qualification::CodeTags(Host,EditorId)==0,
        "Unadmitted CodeEditor styles created native tags");
    gtk_text_buffer_begin_irreversible_action(Buffer);
    gtk_text_buffer_end_irreversible_action(Buffer);
    gtk_text_buffer_set_modified(Buffer,false);
    gtk_text_buffer_get_start_iter(Buffer,&First);
    gtk_text_buffer_insert(Buffer,&First,"x",1);
    Host.Pump();
    Run(Host,"assert(Editor.Text=='xA\\u{1F600}\\nreturn' and Editor.TextRevision==3 and Changes==1)","CodeEditor native edit");
    Require(gtk_text_buffer_get_modified(Buffer) && Qualification::CodeTags(Host,EditorId)==0,
        "Unadmitted CodeEditor styles changed edit modified flag or tag pool");
    Require(gtk_text_buffer_get_can_undo(Buffer),"CodeEditor style retired native Undo");
    gtk_text_buffer_undo(Buffer); Host.Pump();
    Run(Host,"assert(Editor.Text=='A\\u{1F600}\\nreturn' and Editor.TextRevision==4 and Changes==2)","CodeEditor native Undo");
    Require(gtk_text_buffer_get_can_redo(Buffer),"CodeEditor style retired native Redo");
    gtk_text_buffer_redo(Buffer); Host.Pump();
    Run(Host,"assert(Editor.Text=='xA\\u{1F600}\\nreturn' and Editor.TextRevision==5 and Changes==3)","CodeEditor native Redo");
    Require(Qualification::CodeTags(Host,EditorId)==0,"Unadmitted CodeEditor styles created tags across edits");
    gtk_text_buffer_get_end_iter(Buffer,&Last);
    gtk_text_buffer_insert(Buffer,&Last,"\r\nz",3);Host.Pump();
    Run(Host,"assert(Editor.Text=='xA\\u{1F600}\\nreturn\\nz' and Editor.TextRevision==6 and Changes==4)",
        "CodeEditor canonical paste ingress");
    Require(gtk_text_buffer_get_can_undo(Buffer),"CodeEditor canonical paste lost native Undo");
    gtk_text_buffer_undo(Buffer);Host.Pump();
    Run(Host,"assert(Editor.Text=='xA\\u{1F600}\\nreturn' and Editor.TextRevision==7 and Changes==5)",
        "CodeEditor canonical paste Undo");
    Require(gtk_text_buffer_get_can_redo(Buffer),"CodeEditor canonical paste lost Redo");
    std::string Oversized(524289,'x');
    gtk_text_buffer_get_end_iter(Buffer,&Last);
    gtk_text_buffer_insert(Buffer,&Last,Oversized.c_str(),static_cast<gint>(Oversized.size()));Host.Pump();
    Run(Host,"assert(Editor.Text=='xA\\u{1F600}\\nreturn' and Editor.TextRevision==7 and Changes==5)",
        "CodeEditor oversized paste admission");
    Require(gtk_text_buffer_get_can_redo(Buffer),"CodeEditor rejected paste changed Redo");
    Run(Host,"Editor:Destroy();assert(not pcall(function() Editor:SetStyles(5,{}) end))");
    Host.Stop(); Require(Qualification::Views(Host)==0,"CodeEditor host leaked native views");
}

static void CodeEditorEnvelope() {
    Backend Host;
    Run(Host,R"(
        W=Instance.new('Window',{Title='GTK editor envelope',Visible=true})
        Editor=Instance.new('CodeEditor',{Name='LargeEditor',Parent=W})
        assert(Editor.MaxLength==262144)
    )","CodeEditor envelope bootstrap");Settle(Host);
    auto* Buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(Qualification::Native(Host,Id(Host,"LargeEditor"))));
    std::string Full(262144,'a');
    GtkTextIter End;
    gtk_text_buffer_get_end_iter(Buffer,&End);
    gtk_text_buffer_insert(Buffer,&End,Full.c_str(),static_cast<gint>(Full.size()));Host.Pump();
    Require(gtk_text_buffer_get_char_count(Buffer)==262144 && gtk_text_buffer_get_can_undo(Buffer),
        "CodeEditor full native source/Undo admission failed");
    Run(Host,R"(
        assert(#Editor.Text==262144 and Editor.Text==string.rep('a',262144))
        assert(not Editor:SetStyles(Editor.TextRevision,{{Start=262143,End=262144,Color=0x1255AA}}))
    )","CodeEditor full native source/style");Host.Pump();
    gtk_text_buffer_get_end_iter(Buffer,&End);
    gtk_text_buffer_insert(Buffer,&End,"x",1);Host.Pump();
    Run(Host,"assert(#Editor.Text==262144)","CodeEditor full native rejection");
    Require(gtk_text_buffer_get_char_count(Buffer)==262144 && gtk_text_buffer_get_can_undo(Buffer),
        "CodeEditor overflow changed native buffer/Undo");
    gtk_text_buffer_undo(Buffer);Host.Pump();
    Run(Host,"assert(Editor.Text=='')","CodeEditor full native Undo");
    Require(gtk_text_buffer_get_can_redo(Buffer),"CodeEditor full native Redo missing");
    std::string CrLf;CrLf.reserve(524288);
    for(int Index=0;Index<262144;++Index) CrLf+="\r\n";
    gtk_text_buffer_get_end_iter(Buffer,&End);
    gtk_text_buffer_insert(Buffer,&End,CrLf.c_str(),static_cast<gint>(CrLf.size()));Host.Pump();
    Run(Host,"assert(#Editor.Text==262144 and Editor.Text==string.rep('\\n',262144))",
        "CodeEditor full CRLF paste normalization");
    Require(gtk_text_buffer_get_char_count(Buffer)==262144 && gtk_text_buffer_get_can_undo(Buffer),
        "CodeEditor full CRLF native buffer/Undo admission failed");
    gtk_text_buffer_undo(Buffer);Host.Pump();
    Run(Host,"assert(Editor.Text=='')","CodeEditor full CRLF Undo");
    Host.Stop();Require(Qualification::Views(Host)==0,"CodeEditor envelope leaked views");
}

static size_t WorkingSet() {
    std::ifstream File("/proc/self/statm"); size_t Pages=0,Resident=0; File>>Pages>>Resident;
    return Resident*static_cast<size_t>(sysconf(_SC_PAGESIZE));
}
static void ImageOwnership() {
    Backend Host;
    std::string Pixels(1024*1024*8,static_cast<char>(127));
    auto* Bytes=g_bytes_new(Pixels.data(),Pixels.size()); auto* Texture=gdk_memory_texture_new(1024,1024,GDK_MEMORY_R16G16B16A16,Bytes,1024*8); g_bytes_unref(Bytes);
    auto* Encoded=gdk_texture_save_to_png_bytes(Texture); g_object_unref(Texture); gsize Length=0;
    const char* Data=static_cast<const char*>(g_bytes_get_data(Encoded,&Length)); std::string Image(Data,Length); g_bytes_unref(Encoded);
    for(int Index=1;Index<=9;++Index) Host.SetAsset("Image"+std::to_string(Index)+".png",Image);
    Host.SetAsset("Invalid.png","synthetic invalid encoded image");
    Run(Host,"Images={}; for Index=1,9 do Images[Index]=Instance.new('ImageLabel',{Source='Image'..Index..'.png'}) end; Invalid=Instance.new('ImageLabel',{Source='Invalid.png'})");
    for(int Step=0;Step<500 && Qualification::ImageRequests(Host);++Step) {Host.Pump(); g_usleep(4000);}
    Require(Qualification::ImageRequests(Host)==0 && Qualification::ImageBytes(Host)<=32*1024*1024,"image completion staging did not retire within bounds");
    Run(Host,"Ready,Failed=0,0; for _,Image in Images do if Image.LoadState=='Ready' then Ready+=1 else Failed+=1 end end; assert(Ready==8 and Failed==1 and Invalid.LoadState=='Failed')","pinned image admission");
    auto* Presented=gtk_picture_get_paintable(GTK_PICTURE(Qualification::Native(Host,1)));
    Require(Presented && gdk_texture_get_format(GDK_TEXTURE(Presented))==GDK_MEMORY_DEFAULT,"high precision image cache was not normalized to four-byte pixels");
    Run(Host,"for _,Image in Images do Image:Destroy() end; Invalid:Destroy(); Pending=Instance.new('ImageLabel',{Source='Image1.png'})");
    Run(Host,"Pending:Destroy(); Pending:Destroy()");
    for(int Step=0;Step<20;++Step) {Host.Pump(); g_usleep(4000);}
    Require(Qualification::ImageRequests(Host)==0 && Qualification::Views(Host)==0,"image cancellation/destruction leaked ownership");
    Host.Stop(); Require(Qualification::ImageBytes(Host)==0,"image shutdown retained cache bytes");

    Backend Staging;
    for(int Index=1;Index<=24;++Index) Staging.SetAsset("Image"+std::to_string(Index)+".png",Image);
    struct StagingCheck {Backend* Host; bool Observed=false,Good=false;} Check{&Staging};
    Lui_SetLogCallback(Staging.GetRuntime(),&Check,+[](void* Context,const char* Level,const char*) {
        auto& Value=*static_cast<StagingCheck*>(Context);
        if(Value.Observed || std::string(Level)!="Print") return;
        Value.Observed=true;
        const auto Before=Qualification::ImageStaging(*Value.Host);
        g_usleep(350000); // Let the two workers fill available staging during this owner callback.
        const auto After=Qualification::ImageStaging(*Value.Host);
        Value.Good=Before>=28*1024*1024 && Before<=32*1024*1024 && After<=32*1024*1024;
    });
    Run(Staging,"Images={}; for Index=1,24 do local Image=Instance.new('ImageLabel',{Source='Image'..Index..'.png'}); Images[Index]=Image; Image.LoadChanged:Connect(function() if Image.LoadState=='Ready' then print('staging checkpoint') end end) end");
    for(int Step=0;Step<500 && Qualification::ImageStaging(Staging)<32*1024*1024;++Step) g_usleep(4000);
    Require(Qualification::ImageStaging(Staging)==32*1024*1024,"staging pressure fixture did not fill the queue");
    Staging.Pump();
    Require(Check.Observed && Check.Good,"transferred completion textures escaped the staging budget");
    for(int Step=0;Step<500 && Qualification::ImageRequests(Staging);++Step) {Staging.Pump(); g_usleep(4000);}
    Run(Staging,"for _,Image in Images do Image:Destroy() end; Images={}; for Index=1,24 do Images[Index]=Instance.new('ImageLabel',{Source='Image'..Index..'.png'}) end");
    for(int Step=0;Step<500 && Qualification::ImageStaging(Staging)<32*1024*1024;++Step) g_usleep(4000);
    Require(Qualification::ImageStaging(Staging)==32*1024*1024,"shutdown staging fixture did not fill the queue");
    Staging.Stop();
    Require(Qualification::ImageStaging(Staging)==0 && Qualification::ImageRequests(Staging)==0,"staging cancellation/shutdown retained ownership");
}
static int ButtonId(Backend& Host,const char* Text) {
    for(const auto& Node:Tree(Host)) if(Node.at("className")=="TextButton" && Node.at("text")==Text) return Node.at("id");
    throw std::runtime_error("conversation action missing");
}
static void Conversation() {
    const unsigned char Pixel[]{42,110,180,255}; auto* Bytes=g_bytes_new(Pixel,sizeof(Pixel));
    auto* Texture=gdk_memory_texture_new(1,1,GDK_MEMORY_R8G8B8A8,Bytes,4); g_bytes_unref(Bytes);
    auto* Encoded=gdk_texture_save_to_png_bytes(Texture); g_object_unref(Texture);
    gsize Count=0; const char* Data=static_cast<const char*>(g_bytes_get_data(Encoded,&Count)); const std::string Image(Data,Count); g_bytes_unref(Encoded);
    LuiTests::ConversationFixture Fixture(LUI_CONVERSATION_DIR,Image);
    const auto Path=(Fixture.Root/"lui.json").string(); char Error[256]{}; LuiProject* Project=nullptr;
    LuiProjectOptionsV1 Options{}; Options.StructSize=sizeof(Options); Options.AbiVersion=1;
    Require(Lui_ProjectPrepare(&Options,Path.data(),Path.size(),&Project,Error,sizeof(Error)),Error);
    struct Release {LuiProject* Value; ~Release(){Lui_ProjectRelease(Value);}} Released{Project};
    const auto Started=std::chrono::steady_clock::now(); const size_t Baseline=WorkingSet();
    Backend Host; Host.SetAsset("Avatar.png",Image);
    LuiCapabilityDeclarationV1 Caps{sizeof(Caps),1,LUI_CAPABILITY_CLIPBOARD}; Require(Lui_DeclareCapabilities(Host.GetRuntime(),&Caps),"conversation grants failed");
    LuiProjectRunOptionsV1 RunOptions{sizeof(RunOptions),1,0,0,0}; Require(Lui_RunProject(Host.GetRuntime(),Project,&RunOptions),Lui_GetLastError(Host.GetRuntime()));
    Settle(Host);
    const int Scroll=Id(Host,"History"), Composer=Id(Host,"Composer");
    Require(Tree(Host).dump().find("10000 messages")!=std::string::npos,"conversation did not realize its full model");
    const double Startup=std::chrono::duration<double>(std::chrono::steady_clock::now()-Started).count(); Require(Startup<=10,"conversation startup budget exceeded");
    auto* Controller=Qualification::Controller(Host,Scroll,GTK_TYPE_EVENT_CONTROLLER_SCROLL);
    size_t Peak=WorkingSet();
    for(int Index=0;Index<200;++Index) {
        switch(Index%6) {
            case 0: {gboolean Handled=false; g_signal_emit_by_name(Controller,"scroll",0.0,3.0,&Handled); Require(Handled,"conversation wheel not consumed"); break;}
            case 1: Click(Host,ButtonId(Host,"Insert above")); break;
            case 2: Click(Host,ButtonId(Host,"Edit visible")); break;
            case 3: Click(Host,ButtonId(Host,"Delete visible")); break;
            case 4: gtk_window_set_default_size(GTK_WINDOW(Qualification::Native(Host,1)),Index%12?1000:1100,760); break;
            case 5: g_object_set(gtk_settings_get_default(),"gtk-application-prefer-dark-theme",Index%12==5?TRUE:FALSE,nullptr); break;
        }
        for(int Frame=0;Frame<8;++Frame) {Host.Pump(); g_usleep(4000);}
        Peak=std::max(Peak,WorkingSet());
        Require(Qualification::Views(Host)<=256 && Qualification::ImageBytes(Host)<=32*1024*1024 && Qualification::ImageRequests(Host)<=32,"conversation resource bound exceeded");
    }
    auto Metrics=Json::parse(Lui_GetPerformanceJson(Host.GetRuntime()));
    Metrics["StartupSeconds"]=Startup; Metrics["WorkingSetBaselineBytes"]=Baseline; Metrics["WorkingSetPeakBytes"]=Peak;
    std::printf("[LUI:ConversationTest] %s\n",Metrics.dump().c_str());
    Require(Peak<=512*1024*1024 && Metrics["LiveInstances"]<=256 && Metrics["Connections"]<=192,"conversation memory/object budget exceeded");
    Require(Metrics["Input"]["Samples"]>=100 && Metrics["Input"]["P95Ms"]<=100 && Metrics["Input"]["P99Ms"]<=250,"conversation input budget exceeded");
    Require(Metrics["Layout"]["P95Ms"]<=50 && Metrics["Layout"]["P99Ms"]<=100 && Metrics["Render"]["Samples"]>=100 && Metrics["Render"]["P95Ms"]<=50,"conversation presentation budget exceeded");
    Require(GTK_IS_TEXT_VIEW(Qualification::Native(Host,Composer)),"conversation composer did not use multiline widget");
    Host.Stop(); Require(Qualification::Views(Host)==0 && Qualification::ImageRequests(Host)==0,"conversation retirement leaked ownership");
}

static void WindowShortcuts() {
    Backend Host;
    Run(Host,R"(
        W=Instance.new('Window',{Visible=true})
        Input=Instance.new('TextBox',{Parent=W,Text='retained'})
        Seen=0
        W.Shortcut:Connect(function(Event)
            assert(Event.Key=='S' and Event.Control and Event.Shift and not Event.Alt and Event.TextEditing)
            assert(not pcall(function() Event.Control=false end))
            LastRepeat=Event.IsRepeat; Seen+=1
        end)
    )","shortcut bootstrap");
    Settle(Host);
    auto* Key=Qualification::BubbleKey(Host,1);
    auto* State=Qualification::Controller(Host,1,GTK_TYPE_EVENT_CONTROLLER_KEY);
    Require(gtk_event_controller_get_propagation_phase(GTK_EVENT_CONTROLLER(Key))==GTK_PHASE_BUBBLE,"shortcut capture bypasses native editing");
    Require(gtk_widget_grab_focus(Qualification::Native(Host,2)),"shortcut native focus rejected");
    Settle(Host);
    gboolean Handled=false;
    g_signal_emit_by_name(State,"key-pressed",GDK_KEY_s,0,GdkModifierType(GDK_CONTROL_MASK|GDK_SHIFT_MASK),&Handled);
    g_signal_emit_by_name(Key,"key-pressed",GDK_KEY_s,0,GdkModifierType(GDK_CONTROL_MASK|GDK_SHIFT_MASK),&Handled);
    Require(!Handled,"shortcut consumed native event"); Host.Pump();
    g_signal_emit_by_name(State,"key-pressed",GDK_KEY_s,0,GdkModifierType(GDK_CONTROL_MASK|GDK_SHIFT_MASK),&Handled);
    g_signal_emit_by_name(Key,"key-pressed",GDK_KEY_s,0,GdkModifierType(GDK_CONTROL_MASK|GDK_SHIFT_MASK),&Handled);
    Host.Pump(); Run(Host,"assert(Seen==2 and LastRepeat and Input.Text=='retained')","shortcut snapshots");
    g_signal_emit_by_name(State,"key-released",GDK_KEY_s,0,GDK_CONTROL_MASK);
    Run(Host,"W.Visible=false");
    g_signal_emit_by_name(Key,"key-pressed",GDK_KEY_s,0,GdkModifierType(GDK_CONTROL_MASK|GDK_SHIFT_MASK),&Handled);
    Host.Pump(); Run(Host,"assert(Seen==2)","hidden shortcut isolation");
    Host.Stop(); Require(Qualification::Views(Host)==0,"shortcut shutdown retained views");
}

// Run against a fresh project captured by Stage-LibraryExample.py. This uses
// the existing GTK widget callbacks and shared workload counters, not a second
// benchmark harness. Native XTest split/keyboard journeys remain separate.
static void Workbench(const char* Manifest,const char* Storage,const char* Report) {
    const auto Began=std::chrono::steady_clock::now();
    const auto Path=std::filesystem::absolute(std::filesystem::u8path(Manifest));
    LuiProject* Project=nullptr;char Error[256]{};
    LuiProjectOptionsV1 Options{};Options.StructSize=sizeof(Options);Options.AbiVersion=1;
    Require(Lui_ProjectPrepare(&Options,Path.string().data(),Path.string().size(),&Project,Error,sizeof(Error)),Error);
    struct Release {LuiProject* Value;~Release(){Lui_ProjectRelease(Value);}} Released{Project};
    Backend Host;auto* Runtime=Host.GetRuntime();
    Json Result{{"Passed",false},{"Transitions",0},{"WorkingSetBaselineBytes",WorkingSet()}};
    auto Node=[&](const char* Name) {for(const auto& Value:Tree(Host)) if(Value.at("name")==Name) return Value;throw std::runtime_error("workbench node absent");};
    auto NoteEditor=[&] {return Node("LeftEditor").at("text").template get<std::string>().find("Synthetic document 3")!=std::string::npos?"LeftEditor":"RightEditor";};
    auto Rows=[&] {Json Values=Json::array();for(const auto& Value:Tree(Host)) if(Value.at("name").template get<std::string>().find("Open-")==0) Values.push_back({Value.at("name"),Value.at("bounds")});return Values;};
    auto Tick=[&] {for(int Frame=0;Frame<8;++Frame){Host.Pump();g_usleep(4000);}};
    auto Until=[&](auto Predicate,const char* Code) {
        const auto Started=std::chrono::steady_clock::now();
        while(!Predicate() && std::chrono::steady_clock::now()-Started<std::chrono::seconds(5)) Tick();
        Require(Predicate(),Code);
    };
    GtkWidget* Left=nullptr;GtkWidget* Right=nullptr;
    try {
        LuiCapabilityDeclarationV1 Caps{sizeof(Caps),1,LUI_CAPABILITY_STORAGE};
        Require(Lui_DeclareCapabilities(Runtime,&Caps),"workbench grants");
        Require(Lui_InstallProjectStorageV1(Runtime,Project,Storage,Path.parent_path().string().c_str())==0,"workbench storage");
        LuiProjectRunOptionsV1 RunOptions{sizeof(RunOptions),1,0,0,0};Require(Lui_RunProject(Runtime,Project,&RunOptions),"workbench execution");
        Tick();Until([&]{return Node("Inspector").at("text").template get<std::string>().find("open documents")!=std::string::npos;},"workbench ready");
        const double Startup=std::chrono::duration<double>(std::chrono::steady_clock::now()-Began).count();
        Result["StartupSeconds"]=Startup;Require(Startup<=10,"workbench startup budget");
        Left=Qualification::Native(Host,Id(Host,"LeftEditor"));Right=Qualification::Native(Host,Id(Host,"RightEditor"));
        g_object_add_weak_pointer(G_OBJECT(Left),reinterpret_cast<gpointer*>(&Left));
        g_object_add_weak_pointer(G_OBJECT(Right),reinterpret_cast<gpointer*>(&Right));
        size_t Peak=WorkingSet(),PeakViews=0,PeakEditors=0;std::vector<double> EditSamples;
        for(int Index=0;Index<200;++Index) {
            switch(Index%8) {
                case 0: Click(Host,Id(Host,"Tab-Note-3"));break;
                case 1: {
                    Click(Host,Id(Host,"Tab-Note-3"));Tick();
                    const char* Name=NoteEditor();
                    const std::string Before=Node(Name).at("text");
                    auto* Editor=GTK_TEXT_VIEW(Qualification::Native(Host,Id(Host,Name)));
                    Require(gtk_widget_grab_focus(GTK_WIDGET(Editor)),"workbench focus");
                    auto* Buffer=gtk_text_view_get_buffer(Editor);GtkTextIter End;
                    gtk_text_buffer_get_end_iter(Buffer,&End);gtk_text_buffer_place_cursor(Buffer,&End);
                    const auto EditAt=std::chrono::steady_clock::now();gtk_text_buffer_insert_at_cursor(Buffer,"x",1);Host.Pump();
                    const double EditMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-EditAt).count();
                    EditSamples.push_back(EditMs);Lui_RecordPerformance(Runtime,1,EditMs);
                    const std::string After=Node(Name).at("text");
                    Require(After==Before+"x" && Node("Tab-Note-3").at("text").template get<std::string>().find("* ")==0,"workbench native edit");
                    break;
                }
                case 2:
                    Click(Host,Id(Host,"Document.Save"));Until([&]{return Node("Tab-Note-3").at("text").template get<std::string>().find("* ")!=0;},"workbench save");break;
                case 3: {
                    const std::string Before=NoteEditor();Click(Host,Id(Host,"Workspace.Move"));Tick();Require(NoteEditor()!=Before,"workbench redock");break;
                }
                case 4: {
                    const auto Before=Node("LeftEditor").at("bounds").at("width");
                    gtk_window_set_default_size(GTK_WINDOW(Qualification::Native(Host,Id(Host,"Workbench"))),Index%16==4?1220:1180,780);
                    Until([&]{return Node("LeftEditor").at("bounds").at("width")!=Before;},"workbench resize");break;
                }
                case 5: {
                    const auto Before=Qualification::Style(Host,Id(Host,"LeftEditor"));Click(Host,Id(Host,"View.Theme"));Tick();
                    Require(Qualification::Style(Host,Id(Host,"LeftEditor"))!=Before,"workbench theme");break;
                }
                case 6: {
                    auto* Controller=Qualification::Controller(Host,Id(Host,"Navigation"),GTK_TYPE_EVENT_CONTROLLER_SCROLL);
                    const auto Before=Rows();gboolean Handled=false;g_signal_emit_by_name(Controller,"scroll",0.0,Index%16==6?120.0:-120.0,&Handled);Tick();
                    Require(Handled && Rows()!=Before,"workbench native navigation scroll");break;
                }
                case 7:
                    Click(Host,Id(Host,"Tab-Note-2"));Tick();Require(Node("Inspector").at("text").template get<std::string>().find("\n\nNote 2\n")!=std::string::npos,"workbench tab2");
                    Click(Host,Id(Host,"Tab-Note-3"));Tick();Require(Node("Inspector").at("text").template get<std::string>().find("\n\nNote 3\n")!=std::string::npos,"workbench tab3");break;
            }
            Tick();Result["Transitions"]=Index+1;
            Peak=std::max(Peak,WorkingSet());PeakViews=std::max(PeakViews,Qualification::Views(Host));
            size_t Editors=0;for(const auto& Value:Tree(Host)) if(Value.at("className")=="CodeEditor") ++Editors;
            PeakEditors=std::max(PeakEditors,Editors);
            Require(Left && Right && Left==Qualification::Native(Host,Id(Host,"LeftEditor")) && Right==Qualification::Native(Host,Id(Host,"RightEditor")) && Editors==2 && PeakViews<=256 && Qualification::ImageBytes(Host)<=32*1024*1024 && Qualification::ImageRequests(Host)<=32,"workbench native resource budget");
            const auto Sample=Json::parse(Lui_GetPerformanceJson(Runtime));Require(Sample.at("LuaMemoryBytes")<=67108864,"workbench VM budget");
        }
        Click(Host,Id(Host,"Workspace.Save"));Until([&]{return Node("Status").at("text")=="Layout saved locally.";},"workbench layout save");
        const auto Metrics=Json::parse(Lui_GetPerformanceJson(Runtime));Result["Metrics"]=Metrics;
        std::sort(EditSamples.begin(),EditSamples.end());Result["EditSamplesMs"]=EditSamples;
        Require(EditSamples.size()==25 && EditSamples[23]<=100 && EditSamples[24]<=250,"workbench native edit budget");
        Result["WorkingSetPeakBytes"]=Peak;Result["PeakViews"]=PeakViews;Result["PeakEditors"]=PeakEditors;
        Require(Peak<=512*1024*1024 && Metrics.at("LiveInstances")<=256 && Metrics.at("Connections")<=192,"workbench memory/object budget");
        Require(Metrics["Input"]["Samples"]>=100 && Metrics["Input"]["P95Ms"]<=100 && Metrics["Input"]["P99Ms"]<=250,"workbench input budget");
        Require(Metrics["Layout"]["Samples"]>=200 && Metrics["Layout"]["P95Ms"]<=50 && Metrics["Layout"]["P99Ms"]<=100 && Metrics["Render"]["Samples"]>=100 && Metrics["Render"]["P95Ms"]<=50,"workbench presentation budget");
        Host.Stop();Result["Retired"]=!Left && !Right && Qualification::Views(Host)==0 && Qualification::ImageRequests(Host)==0;
        Require(Result["Retired"],"workbench retirement");Result["Passed"]=true;
    } catch(const std::exception& Failure) {Result["Failure"]=Failure.what();}
    Host.Stop();
    if(Left) g_object_remove_weak_pointer(G_OBJECT(Left),reinterpret_cast<gpointer*>(&Left));
    if(Right) g_object_remove_weak_pointer(G_OBJECT(Right),reinterpret_cast<gpointer*>(&Right));
    Result["Boundary"]="Native GTK widget callbacks through owner response and existing native render submission counters; excludes physical input/display and pacing";
    std::ofstream Output(Report);Output<<Result.dump(2)<<'\n';Require(static_cast<bool>(Output),"workbench report");
    std::printf("[LUI:WorkbenchTest] %s\n",Result.dump().c_str());Require(Result["Passed"],"workbench qualification failed");
}

static void WindowLifecycle() {
    Backend Host;
    Run(Host,R"(
        Main=Instance.new('Window',{Visible=true})
        Floating=Instance.new('Window',{Visible=true})
        Editor=Instance.new('TextBox',{Text='retained',Parent=Floating})
        Attempts,Closed=0,0; Block=true
        Floating.CloseRequested:Connect(function()
            Attempts+=1
            if Block then assert(Floating:PreventClose()) else Editor.Parent=Main end
        end)
        Floating.Closed:Connect(function() Closed+=1;assert(Floating.IsClosed) end)
    )");
    Settle(Host);
    auto* Editor=Qualification::Native(Host,3);
    gtk_window_close(GTK_WINDOW(Qualification::Native(Host,2)));
    Settle(Host);
    Run(Host,"assert(Attempts==1 and Closed==0 and not Floating.IsClosed and Editor.Text=='retained');Block=false");
    gtk_window_close(GTK_WINDOW(Qualification::Native(Host,2)));
    Settle(Host);
    Run(Host,"assert(Attempts==2 and Closed==1 and Floating.IsClosed and Editor.Parent==Main and Editor.Text=='retained');Floating:Destroy()");
    Require(Editor==Qualification::Native(Host,3),"closed float replaced retained native editor");
    Run(Host,R"(
        Later=Instance.new('Window',{Visible=true})
        LateEditor=Instance.new('TextBox',{Parent=Later,Text='after close'})
    )");
    Settle(Host);
    auto* LateEditor=Qualification::Native(Host,5);
    Run(Host,"assert(Later:RequestClose())","post-close request accepted");
    Settle(Host);
    Run(Host,"assert(Later.IsClosed and LateEditor.Parent==Later and LateEditor.Text=='after close');LateEditor.Parent=Main;Later:Destroy()",
        "post-close acknowledgement and redock");
    Settle(Host);
    Require(LateEditor==Qualification::Native(Host,5) && gtk_widget_get_root(LateEditor)==GTK_ROOT(Qualification::Native(Host,1)),
        "post-close redocking lost retained native editor");
    Host.Stop();Require(Qualification::Views(Host)==0,"window lifecycle retained views");
}

static void Group(const char* Name,void (*Execute)()) {
    std::printf("[LUI:GtkTest] Begin %s\n",Name); std::fflush(stdout);
    Execute();
    std::printf("[LUI:GtkTest] Passed %s\n",Name); std::fflush(stdout);
}
int main(int Count,char** Arguments) {
    try {
        Require(Backend::Initialize(),"display unavailable");
        if(Count==5 && std::string(Arguments[1])=="--workbench") {Workbench(Arguments[2],Arguments[3],Arguments[4]);return 0;}
        Require(Count==1,"unexpected test arguments");
        Group("Controls",Controls); Group("WindowShortcuts",WindowShortcuts);
        Group("WindowLifecycle",WindowLifecycle); Group("Modular",Modular);
        Group("Clipboard",Clipboard); Group("Dialogs",Dialogs); Group("Viewports",Viewports);
        Group("AdvancedText",AdvancedText); Group("CodeEditor",CodeEditor);
        Group("CodeEditorEnvelope",CodeEditorEnvelope);
        Group("ImageOwnership",ImageOwnership); Group("Conversation",Conversation);
        std::puts("[LUI:GtkTest] Native mappings, canonical geometry, edit/input, sticky privacy, modular screens and lifecycle passed"); return 0;
    } catch(const std::exception& Error) {std::fprintf(stderr,"[LUI:GtkTest] %s\n",Error.what()); return 1;}
}
