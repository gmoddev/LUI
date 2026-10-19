#pragma once
#include "LuiRuntime.h"
#include <gtk/gtk.h>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

// Private native adapter. No GTK object enters the Luau API or extension ABI.
namespace Lui::Gtk {
struct Qualification;
class Backend {
public:
    static bool Initialize();
    explicit Backend(uint32_t Mode=LUI_DIAGNOSTICS_DEVELOPMENT);
    ~Backend();
    Backend(const Backend&)=delete;
    Backend& operator=(const Backend&)=delete;
    LuiRuntime* GetRuntime() const {return Runtime;}
    void Pump();
    bool HasWindows() const;
    void SetAsset(const std::string& Name,std::string Bytes);
    void Stop();
private:
    friend struct Qualification;
    struct View {
        Backend* Owner=nullptr;
        int Id=0,ParentId=0;
        int PaintRank=-1;
        bool InputVisible=true;
        bool Multiline=false,Secret=false,Composing=false,CompositionEnding=false;
        int SelectionStart=0,SelectionLength=0,MaxLength=65534;
        std::string Text,Placeholder,StyleJson,AccessibilityDescription;
        uint64_t ImageToken=0;
        std::string ImageKey;
        std::vector<GObject*> TextSources;
        uint64_t Epoch=0;
        std::string ClassName;
        std::string AccessibilityLabel;
        GtkWidget* Native=nullptr;
        GtkWidget* Slot=nullptr;
        GtkWidget* Container=nullptr;
        GtkCssProvider* Style=nullptr;
        GdkFrameClock* Clock=nullptr;
        std::chrono::steady_clock::time_point PresentationAt;
        bool PresentationPending=false;
        bool Alive=true,Applying=false,Visible=true,Enabled=true,Closed=false;
        double X=0,Y=0,Width=0,Height=0,Minimum=0,Maximum=100,Value=0;
        std::set<std::string> Keys;
        std::vector<GObject*> Controllers;
    };
    enum class Kind {Activate,Text,Checked,Value,Focus,Hover,Pointer,Key,Resize,Close,Scroll,TextInput,Composition};
    struct Event {
        Kind Type; int Id; uint64_t Epoch;
        int Phase=0,Device=0; double X=0,Y=0,Value=0;
        std::string Text;
        std::chrono::steady_clock::time_point QueuedAt;
        uint64_t TextRevision=0;
    };
    LuiRuntime* Runtime=nullptr;
    std::thread::id OwnerThread;
    std::map<int,std::unique_ptr<View>> Views;
    std::map<std::string,std::shared_ptr<const std::string>> Assets;
    std::deque<Event> Events;
    uint64_t NextEpoch=1,Dropped=0,AssetBytes=0,EventBytes=0;
    bool Stopping=false,Overflow=false,ThemePending=true;
    GtkSettings* Settings=nullptr;
    struct PlatformSession;
    std::shared_ptr<PlatformSession> Platform;
    struct ImageSession;
    std::shared_ptr<ImageSession> ImageWork;
    void StartImages();
    void StopImages();
    void PumpImages();
    void ReleaseImage(View& Target);
    size_t ImageCacheUsage() const;
    size_t ImageRequestCount() const;
    size_t ImageStagingUsage() const;
    static int LoadImage(void*,int,uint64_t,const char*);
    static void CancelImage(void*,uint64_t);
    void StartPlatform();
    void StopPlatform();
    static int WriteClipboard(void*,const char*);
    static int ReadClipboard(void*,uint64_t);
    static int OpenFile(void*,uint64_t);
    bool Owner() const;
    void Queue(View* Target,Event Value) noexcept;
    void Drain();
    void ResetInput();
    void ObserveTheme();
    void Connect(View& Target);
    void ConnectText(View& Target);
    void TextEvent(View& Target);
    void ApplySelection(View& Target);
    void ReplaceText(View& Target,bool Multiline,bool Secret,bool Force=false);
    bool Usable(const View& Target) const;
    static int Create(void*,int,const char*);
    static int Property(void*,int,const char*,const char*);
    static int Parent(void*,int,int);
    static int Arrange(void*,int,double,double,double,double,int);
    static int Present(void*,int,int,double,double,double,double,int,int);
    static int Destroy(void*,int);
    int Fail();
};
std::string GetKey(guint Value);
}
