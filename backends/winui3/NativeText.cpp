#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>
#include <richedit.h>
#include <commctrl.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <algorithm>
#include <atomic>
#include <new>

using Microsoft::WRL::ComPtr;

static constexpr int CodeEditorMaxUnits=256*1024;
static constexpr int CodeEditorMaxRawUnits=2*CodeEditorMaxUnits;
static constexpr int CodeEditorMaxUtf8Bytes=256*1024;
static constexpr SIZE_T CodeEditorMaxClipboardBytes=
    (static_cast<SIZE_T>(CodeEditorMaxRawUnits)+1)*sizeof(wchar_t);

struct LuiWinUITextColorSpanV1 {
    std::int32_t Start;
    std::int32_t End;
    std::uint32_t Color;
};
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetColorsV1(
    HWND Editor,const LuiWinUITextColorSpanV1* Spans,std::uint32_t Count) noexcept;

// Private prototype bridge. The caller owns the editor HWND and must invoke this
// on its UI thread. A zero result leaves the caller free to preserve plain text.
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextApplyTemporaryColorsV1(
    HWND Editor, const LuiWinUITextColorSpanV1* Spans, std::uint32_t Count) noexcept {
    if (!Editor || !Spans || Count == 0 || Count > 128 || !IsWindow(Editor) ||
        GetWindowThreadProcessId(Editor, nullptr) != GetCurrentThreadId()) return 0;
    wchar_t ClassName[32]{};
    if (GetClassNameW(Editor, ClassName, 32) == 0 || lstrcmpW(ClassName, MSFTEDIT_CLASS) != 0) return 0;
    std::int32_t PreviousEnd=0;
    for (std::uint32_t Index=0; Index<Count; ++Index) {
        if (Spans[Index].Start < 0 || Spans[Index].End <= Spans[Index].Start ||
            Spans[Index].Start < PreviousEnd || Spans[Index].End > CodeEditorMaxUnits ||
            (Spans[Index].Color & 0xFF000000u) != 0) return 0;
        PreviousEnd=Spans[Index].End;
    }

    ComPtr<IRichEditOle> Ole;
    if (!SendMessageW(Editor, EM_GETOLEINTERFACE, 0,
        reinterpret_cast<LPARAM>(Ole.GetAddressOf()))) return 0;
    ComPtr<ITextDocument> Document;
    if (FAILED(Ole.As(&Document))) return 0;
    for (std::uint32_t Index=0; Index<Count; ++Index) {
        ComPtr<ITextRange> Range;
        ComPtr<ITextFont> Font;
        if (FAILED(Document->Range(Spans[Index].Start, Spans[Index].End, &Range)) ||
            FAILED(Range->GetFont(&Font)) || FAILED(Font->Reset(tomApplyTmp)) ||
            FAILED(Font->SetForeColor(static_cast<long>(Spans[Index].Color)))) return 0;
    }
    return 1;
}

using NativeTextEventV1 = void (__cdecl*)(void*, int, const wchar_t*, int, int);
class NativeTextOleGuard;
static std::atomic<std::uint64_t> NextHostIdentity{1};
struct NativeTextHostV1 {
    HWND Host=nullptr;
    HWND Editor=nullptr;
    NativeTextEventV1 Callback=nullptr;
    void* Context=nullptr;
    bool Applying=false;
    int LastTextScalars=0;
    int MaxUndoScalars=0;
    int MaxLength=CodeEditorMaxUnits;
    wchar_t PendingHigh=0;
    bool Composing=false;
    bool ProbeFunctionKeys=false;
    NativeTextOleGuard* OleGuard=nullptr;
    std::uint64_t Identity=0;
    std::uint64_t ContextEpoch=0;
};

// Rich Edit can accept OLE data without passing through WM_PASTE. Import only
// bounded Unicode paste through the same LF/length admission as a native edit;
// decline drops until they have that contract.
class NativeTextOleGuard final : public IRichEditOleCallback {
    LONG References=1;
    NativeTextHostV1* Host;
public:
    explicit NativeTextOleGuard(NativeTextHostV1* Owner) noexcept:Host(Owner) {}
    void Detach() noexcept {Host=nullptr;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Interface,void** Result) override {
        if(!Result) return E_POINTER;
        *Result=nullptr;
        if(!IsEqualIID(Interface,IID_IUnknown) &&
            !IsEqualIID(Interface,IID_IRichEditOleCallback)) return E_NOINTERFACE;
        *Result=static_cast<IRichEditOleCallback*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(InterlockedIncrement(&References));
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG Remaining=static_cast<ULONG>(InterlockedDecrement(&References));
        if(Remaining==0) delete this;
        return Remaining;
    }
    HRESULT STDMETHODCALLTYPE GetNewStorage(LPSTORAGE*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetInPlaceContext(LPOLEINPLACEFRAME*,LPOLEINPLACEUIWINDOW*,
        LPOLEINPLACEFRAMEINFO) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ShowContainerUI(BOOL) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryInsertObject(LPCLSID,LPSTORAGE,LONG) override {return E_FAIL;}
    HRESULT STDMETHODCALLTYPE DeleteObject(LPOLEOBJECT) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryAcceptData(LPDATAOBJECT Data,CLIPFORMAT*,DWORD Operation,
        BOOL Really,HGLOBAL) override;
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetClipboardData(CHARRANGE*,DWORD,LPDATAOBJECT*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDragDropEffect(BOOL,DWORD,LPDWORD Effect) override {
        if(!Effect) return E_POINTER;
        *Effect=DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetContextMenu(WORD,LPOLEOBJECT,CHARRANGE*,HMENU*) override {return E_NOTIMPL;}
};

static NativeTextHostV1* State(HWND Host) noexcept {
    return reinterpret_cast<NativeTextHostV1*>(GetWindowLongPtrW(Host,GWLP_USERDATA));
}
static NativeTextHostV1* HostedState(HWND Editor) noexcept {
    HWND Host=GetParent(Editor);
    wchar_t ClassName[40]{};
    return Host && GetClassNameW(Host,ClassName,40) &&
        lstrcmpW(ClassName,L"LuiNativeCodeEditorHostV1")==0?State(Host):nullptr;
}
struct ApplyingGuard {
    NativeTextHostV1* Host;
    bool Previous;
    explicit ApplyingGuard(HWND Editor) noexcept:Host(HostedState(Editor)),Previous(Host?Host->Applying:false) {
        if(Host) Host->Applying=true;
    }
    ~ApplyingGuard() noexcept {if(Host) Host->Applying=Previous;}
};
static HWND ParkingWindow() noexcept {
    thread_local HWND Parking=nullptr;
    if(!Parking || !IsWindow(Parking)) Parking=CreateWindowExW(0,L"STATIC",L"",0,
        0,0,0,0,HWND_MESSAGE,nullptr,GetModuleHandleW(nullptr),nullptr);
    return Parking;
}
static HWND PopupParkingWindow() noexcept {
    thread_local HWND Parking=nullptr;
    if(!Parking || !IsWindow(Parking)) Parking=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,
        0,0,0,0,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    return Parking;
}
static int ScalarAt(const std::wstring& Text,int Utf16) noexcept {
    int Scalar=0;
    for(int Index=0;Index<Utf16 && Index<static_cast<int>(Text.size());++Index) {
        const wchar_t Value=Text[static_cast<size_t>(Index)];
        if(Value>=0xDC00 && Value<=0xDFFF && Index>0 &&
            Text[static_cast<size_t>(Index-1)]>=0xD800 && Text[static_cast<size_t>(Index-1)]<=0xDBFF) continue;
        ++Scalar;
    }
    return Scalar;
}
static int ScalarCount(const wchar_t* Text) noexcept {
    int Count=0;
    int Index=0;
    for(;Text[Index] && Index<=CodeEditorMaxUnits;++Index) {
        if(Text[Index]==L'\r' && Text[Index+1]==L'\n') continue;
        if(Text[Index]>=0xDC00 && Text[Index]<=0xDFFF && Index>0 &&
            Text[Index-1]>=0xD800 && Text[Index-1]<=0xDBFF) continue;
        if(++Count>CodeEditorMaxUnits) return Count;
    }
    return Text[Index]?CodeEditorMaxUnits+1:Count;
}
static bool ReadCanonicalText(HWND Editor,std::wstring& Canonical) {
    const int Length=GetWindowTextLengthW(Editor);
    if(Length<0 || Length>CodeEditorMaxRawUnits) return false;
    std::wstring Raw(static_cast<size_t>(Length)+1,L'\0');
    const int Copied=GetWindowTextW(Editor,Raw.data(),Length+1);
    if(Copied!=Length) return false;
    Raw.resize(static_cast<size_t>(Copied));
    Canonical.clear();
    Canonical.reserve(Raw.size());
    for(size_t Index=0;Index<Raw.size();++Index) {
        if(Raw[Index]==L'\r' && Index+1<Raw.size() && Raw[Index+1]==L'\n') continue;
        if(Canonical.size()==CodeEditorMaxUnits) return false;
        Canonical.push_back(Raw[Index]);
    }
    return Canonical.find(L'\r')==std::wstring::npos;
}
struct NativeEditContext {
    HWND Editor=nullptr,HostWindow=nullptr,Parent=nullptr,Owner=nullptr,Focus=nullptr;
    CHARRANGE Selection{};
    std::wstring Text;
    std::uint64_t Identity=0,Epoch=0;
};
static bool CaptureEditContext(NativeTextHostV1* Host,NativeEditContext& Context) {
    if(!Host || !Host->Editor || !Host->Host || !IsWindow(Host->Editor) ||
        HostedState(Host->Editor)!=Host) return false;
    Context.Editor=Host->Editor;
    Context.HostWindow=Host->Host;
    Context.Parent=GetParent(Host->Host);
    Context.Owner=GetWindow(Host->Host,GW_OWNER);
    Context.Focus=GetFocus();
    Context.Identity=Host->Identity;
    Context.Epoch=Host->ContextEpoch;
    SendMessageW(Context.Editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&Context.Selection));
    if(!ReadCanonicalText(Context.Editor,Context.Text) || !IsWindow(Context.Editor)) return false;
    auto* Current=HostedState(Context.Editor);
    return Current==Host && Current->Identity==Context.Identity &&
        Current->ContextEpoch==Context.Epoch;
}
static bool IsEditContextCurrent(NativeTextHostV1* Host,const NativeEditContext& Context) {
    if(!IsWindow(Context.Editor)) return false;
    auto* Current=HostedState(Context.Editor);
    if(Current!=Host || !Current || Current->Identity!=Context.Identity ||
        Current->ContextEpoch!=Context.Epoch || Current->Host!=Context.HostWindow ||
        GetParent(Context.HostWindow)!=Context.Parent ||
        GetWindow(Context.HostWindow,GW_OWNER)!=Context.Owner || GetFocus()!=Context.Focus) return false;
    CHARRANGE Selection{};
    SendMessageW(Context.Editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&Selection));
    if(Selection.cpMin!=Context.Selection.cpMin || Selection.cpMax!=Context.Selection.cpMax) return false;
    std::wstring Text;
    if(!ReadCanonicalText(Context.Editor,Text) || Text!=Context.Text ||
        !IsWindow(Context.Editor)) return false;
    Current=HostedState(Context.Editor);
    return Current==Host && Current->Identity==Context.Identity &&
        Current->ContextEpoch==Context.Epoch;
}
static bool PasteEligible(NativeTextHostV1* Host,HWND Editor) noexcept {
    return Host && Editor && IsWindow(Editor) && HostedState(Editor)==Host &&
        !Host->Applying && !Host->Composing && IsWindowEnabled(Editor) &&
        (GetWindowLongPtrW(Editor,GWL_STYLE)&ES_READONLY)==0;
}
static bool AllowReplacement(NativeTextHostV1* Host,const std::wstring& Replacement) {
    std::wstring Candidate;
    if(!ReadCanonicalText(Host->Editor,Candidate)) return false;
    CHARRANGE Selection{};
    SendMessageW(Host->Editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&Selection));
    if(Selection.cpMin<0 || Selection.cpMax<Selection.cpMin ||
        Selection.cpMax>static_cast<long>(Candidate.size())) return false;
    const size_t Selected=static_cast<size_t>(Selection.cpMax-Selection.cpMin);
    if(Replacement.size()>CodeEditorMaxUnits ||
        Candidate.size()-Selected>CodeEditorMaxUnits-Replacement.size()) return false;
    Candidate.replace(static_cast<size_t>(Selection.cpMin),Selected,Replacement);
    int Scalars=0;
    for(size_t Index=0;Index<Candidate.size();++Index) {
        const wchar_t Value=Candidate[Index];
        if(Value>=0xD800 && Value<=0xDBFF) {
            if(Index+1==Candidate.size() || Candidate[Index+1]<0xDC00 || Candidate[Index+1]>0xDFFF) return false;
            ++Index;
        } else if(Value>=0xDC00 && Value<=0xDFFF) return false;
        ++Scalars;
    }
    if(Scalars>Host->MaxLength) return false;
    const int Bytes=Candidate.empty()?0:WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,
        Candidate.data(),static_cast<int>(Candidate.size()),nullptr,0,nullptr,nullptr);
    return Candidate.empty() || (Bytes>0 && Bytes<=CodeEditorMaxUtf8Bytes);
}
static bool CanonicalPaste(const wchar_t* Wide,size_t Length,std::wstring& Text) {
    if(!Wide || Length>CodeEditorMaxRawUnits) return false;
    try {
        Text.clear();Text.reserve(Length);
        for(size_t Index=0;Index<Length;++Index) {
            if(Text.size()==CodeEditorMaxUnits && Wide[Index]!=L'\r') return false;
            if(Wide[Index]==L'\r') {
                if(Index+1<Length && Wide[Index+1]==L'\n') ++Index;
                if(Text.size()==CodeEditorMaxUnits) return false;
                Text.push_back(L'\n');
            } else Text.push_back(Wide[Index]);
        }
    } catch(...) {return false;}
    return true;
}
struct PasteMediumGuard {
    STGMEDIUM Value{};
    ~PasteMediumGuard() noexcept {
        if(Value.tymed!=TYMED_NULL) ReleaseStgMedium(&Value);
    }
};
static bool ClipboardText(HWND Editor,std::wstring& Text) {
    if(!OpenClipboard(Editor)) return false;
    HANDLE Data=GetClipboardData(CF_UNICODETEXT);
    if(!Data || GlobalSize(Data)>CodeEditorMaxClipboardBytes) {CloseClipboard();return false;}
    const auto* Wide=static_cast<const wchar_t*>(GlobalLock(Data));
    if(!Wide) {CloseClipboard();return false;}
    const size_t Capacity=GlobalSize(Data)/sizeof(wchar_t);
    size_t Length=0;
    while(Length<Capacity && Wide[Length]) ++Length;
    if(Length==Capacity) {GlobalUnlock(Data);CloseClipboard();return false;}
    const bool Accepted=CanonicalPaste(Wide,Length,Text);
    GlobalUnlock(Data);CloseClipboard();return Accepted;
}
HRESULT STDMETHODCALLTYPE NativeTextOleGuard::QueryAcceptData(LPDATAOBJECT Data,
    CLIPFORMAT*,DWORD Operation,BOOL Really,HGLOBAL) {
    AddRef();
    struct CallbackPin {
        NativeTextOleGuard* Guard;
        ~CallbackPin() noexcept {Guard->Release();}
    } Pin{this};
    try {
    NativeTextHostV1* OwnerState=Host;
    NativeEditContext Before;
    if(Operation!=RECO_PASTE || !Data || !CaptureEditContext(OwnerState,Before) ||
        !PasteEligible(OwnerState,Before.Editor)) return E_FAIL;
    FORMATETC Format{CF_UNICODETEXT,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
    bool Accepted=false;
    std::wstring Paste;
    {
        PasteMediumGuard Medium;
        try {if(FAILED(Data->GetData(&Format,&Medium.Value))) return E_FAIL;}
        catch(...) {return E_FAIL;}
        if(Medium.Value.tymed==TYMED_HGLOBAL && Medium.Value.hGlobal &&
            GlobalSize(Medium.Value.hGlobal)<=CodeEditorMaxClipboardBytes) {
            const auto* Wide=static_cast<const wchar_t*>(GlobalLock(Medium.Value.hGlobal));
            if(Wide) {
                const size_t Capacity=GlobalSize(Medium.Value.hGlobal)/sizeof(wchar_t);
                size_t Length=0;
                while(Length<Capacity && Wide[Length]) ++Length;
                if(Length<Capacity) Accepted=CanonicalPaste(Wide,Length,Paste);
                GlobalUnlock(Medium.Value.hGlobal);
            }
        }
    }
    if(!Accepted || !IsEditContextCurrent(OwnerState,Before) ||
        !PasteEligible(OwnerState,Before.Editor)) return E_FAIL;
    try {if(!AllowReplacement(OwnerState,Paste) ||
        !IsEditContextCurrent(OwnerState,Before)) return E_FAIL;}
    catch(...) {return E_FAIL;}
    if(Really) SendMessageW(Before.Editor,EM_REPLACESEL,TRUE,
        reinterpret_cast<LPARAM>(Paste.c_str()));
    // S_FALSE tells Rich Edit that the client checked/imported this data.
    return S_FALSE;
    } catch(...) {return E_FAIL;}
}
static bool ImePreedit(HWND Editor,std::wstring& Text) {
    HIMC Context=ImmGetContext(Editor);
    if(!Context) return false;
    const LONG Bytes=ImmGetCompositionStringW(Context,GCS_COMPSTR,nullptr,0);
    if(Bytes<0 || Bytes>8192 || (Bytes&1)!=0) {ImmReleaseContext(Editor,Context);return false;}
    Text.assign(static_cast<size_t>(Bytes/sizeof(wchar_t))+1,L'\0');
    const LONG Copied=ImmGetCompositionStringW(Context,GCS_COMPSTR,Text.data(),
        static_cast<DWORD>(Bytes));
    ImmReleaseContext(Editor,Context);
    if(Copied!=Bytes) return false;
    Text.resize(static_cast<size_t>(Bytes/sizeof(wchar_t)));
    return true;
}
static LRESULT CALLBACK EditorProc(HWND Editor,UINT Message,WPARAM WParam,LPARAM LParam,
    UINT_PTR, DWORD_PTR Context) noexcept {
    auto* Value=reinterpret_cast<NativeTextHostV1*>(Context);
    if(Value) {
        if(Message==WM_SETFOCUS || Message==WM_KILLFOCUS ||
            Message==WM_IME_STARTCOMPOSITION || Message==WM_IME_ENDCOMPOSITION)
            ++Value->ContextEpoch;
        if(Message==WM_IME_STARTCOMPOSITION) Value->Composing=true;
        else if(Message==WM_IME_ENDCOMPOSITION) Value->Composing=false;
    }
    // Probe-only: function keys without modifiers are outside Rich Edit's
    // documented editing chords. This is not a general handled-key signal.
    const bool ProbeAfterDefault=Value && Value->ProbeFunctionKeys && !Value->Applying &&
        !Value->Composing && Message==WM_KEYDOWN && WParam>=VK_F1 && WParam<=VK_F12 &&
        (GetKeyState(VK_CONTROL)&0x8000)==0 && (GetKeyState(VK_SHIFT)&0x8000)==0 &&
        (GetKeyState(VK_MENU)&0x8000)==0;
    if(Value && !Value->Applying) try {
        if(Message!=WM_CHAR) Value->PendingHigh=0;
        if(Message==WM_CHAR && WParam>=0xD800 && WParam<=0xDBFF) {
            Value->PendingHigh=static_cast<wchar_t>(WParam);
            return 0;
        }
        if(Message==WM_CHAR && WParam>=0xDC00 && WParam<=0xDFFF) {
            if(!Value->PendingHigh) return 0;
            std::wstring Pair;
            Pair.push_back(Value->PendingHigh);Pair.push_back(static_cast<wchar_t>(WParam));
            Value->PendingHigh=0;
            if(!AllowReplacement(Value,Pair)) return 0;
            SendMessageW(Editor,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(Pair.c_str()));
            return 0;
        }
        if(Message==WM_CHAR && (WParam>=32 || WParam==L'\r' || WParam==L'\t')) {
            const wchar_t Character=WParam==L'\r'?L'\n':static_cast<wchar_t>(WParam);
            Value->PendingHigh=0;
            if(!AllowReplacement(Value,std::wstring(1,Character))) return 0;
        } else if(Message==WM_PASTE) {
            std::wstring Paste;
            NativeEditContext Before;
            if(!PasteEligible(Value,Editor) || !CaptureEditContext(Value,Before) ||
                !ClipboardText(Editor,Paste) || !IsEditContextCurrent(Value,Before) ||
                !PasteEligible(Value,Editor) || !AllowReplacement(Value,Paste) ||
                !IsEditContextCurrent(Value,Before) || !PasteEligible(Value,Editor)) return 0;
            SendMessageW(Editor,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(Paste.c_str()));
            return 0;
        }
    } catch(...) {return 0;}
    if(Value && Value->Callback && !Value->Applying) {
        if(Message==WM_SETFOCUS) Value->Callback(Value->Context,2,L"",0,0);
        else if(Message==WM_KILLFOCUS) Value->Callback(Value->Context,3,L"",0,0);
        else if(Message==WM_IME_STARTCOMPOSITION) Value->Callback(Value->Context,6,L"",0,0);
        else if(Message==WM_IME_COMPOSITION && (LParam&GCS_COMPSTR)!=0) try {
            std::wstring Preedit;
            if(ImePreedit(Editor,Preedit)) Value->Callback(Value->Context,7,Preedit.c_str(),0,0);
        } catch(...) { /* A preedit notification must not unwind through Rich Edit. */ }
        else if(Message==WM_IME_ENDCOMPOSITION) Value->Callback(Value->Context,8,L"",0,0);
        else if(Message==WM_KEYDOWN || Message==WM_SYSKEYDOWN)
            Value->Callback(Value->Context,4,L"",static_cast<int>(WParam),
                (static_cast<unsigned long long>(LParam)>>30)&1?1:0);
        else if(Message==WM_KEYUP || Message==WM_SYSKEYUP)
            Value->Callback(Value->Context,5,L"",static_cast<int>(WParam),0);
        else if(Message==WM_LBUTTONDOWN || Message==WM_LBUTTONUP ||
            Message==WM_RBUTTONDOWN || Message==WM_RBUTTONUP ||
            Message==WM_MBUTTONDOWN || Message==WM_MBUTTONUP || Message==WM_MOUSEMOVE) {
            const int X=static_cast<short>(LOWORD(LParam));
            const int Y=static_cast<short>(HIWORD(LParam));
            const int Kind=Message==WM_LBUTTONDOWN?10:Message==WM_LBUTTONUP?11:
                Message==WM_MOUSEMOVE?12:Message==WM_RBUTTONDOWN?14:
                Message==WM_RBUTTONUP?15:Message==WM_MBUTTONDOWN?16:17;
            Value->Callback(Value->Context,Kind,L"",X,Y);
            if(Message==WM_MOUSEMOVE) {
                TRACKMOUSEEVENT Track{sizeof(Track),TME_LEAVE,Editor,0};
                TrackMouseEvent(&Track);
            }
        } else if(Message==WM_MOUSELEAVE) Value->Callback(Value->Context,13,L"",0,0);
    }
    if(!IsWindow(Editor)) return 0;
    if(Message==WM_NCDESTROY) RemoveWindowSubclass(Editor,EditorProc,1);
    const LRESULT Result=DefSubclassProc(Editor,Message,WParam,LParam);
    if(ProbeAfterDefault && IsWindow(Editor) && GetFocus()==Editor) {
        auto* Current=HostedState(Editor);
        if(Current==Value && Current->ProbeFunctionKeys && !Current->Applying &&
            !Current->Composing && Current->Callback)
            Current->Callback(Current->Context,18,L"",static_cast<int>(WParam),0);
    }
    return Result;
}
static LRESULT CALLBACK HostProc(HWND Host,UINT Message,WPARAM WParam,LPARAM LParam) noexcept {
    auto* Value=State(Host);
    if(Message==WM_NCCREATE) {
        auto* Create=reinterpret_cast<CREATESTRUCTW*>(LParam);
        Value=static_cast<NativeTextHostV1*>(Create->lpCreateParams);
        Value->Host=Host;
        SetWindowLongPtrW(Host,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(Value));
    }
    if(Value && Value->Editor &&
        ((Message==WM_COMMAND && reinterpret_cast<HWND>(LParam)==Value->Editor &&
            HIWORD(WParam)==EN_CHANGE) ||
         (Message==WM_NOTIFY && LParam &&
            reinterpret_cast<NMHDR*>(LParam)->hwndFrom==Value->Editor &&
            reinterpret_cast<NMHDR*>(LParam)->code==EN_SELCHANGE)))
        ++Value->ContextEpoch;
    if(Value && Value->Editor && !Value->Applying && Value->Callback) try {
        if(Message==WM_COMMAND && reinterpret_cast<HWND>(LParam)==Value->Editor &&
            HIWORD(WParam)==EN_CHANGE) {
            Value->Applying=true;
            const bool Cleared=LuiWinUITextSetColorsV1(Value->Editor,nullptr,0)==1;
            Value->Applying=false;
            if(!Cleared) return DefWindowProcW(Host,Message,WParam,LParam);
            std::wstring Text;
            if(!ReadCanonicalText(Value->Editor,Text)) return DefWindowProcW(Host,Message,WParam,LParam);
            const int CurrentScalars=ScalarAt(Text,static_cast<int>(Text.size()));
            if(Value->MaxUndoScalars<Value->LastTextScalars)
                Value->MaxUndoScalars=Value->LastTextScalars;
            if(Value->MaxUndoScalars<CurrentScalars)
                Value->MaxUndoScalars=CurrentScalars;
            Value->LastTextScalars=CurrentScalars;
            CHARRANGE Selection{};
            SendMessageW(Value->Editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&Selection));
            const int Start=ScalarAt(Text,Selection.cpMin);
            const int End=ScalarAt(Text,Selection.cpMax);
            Value->Callback(Value->Context,1,Text.c_str(),Start,End-Start);
        } else if(Message==WM_NOTIFY && reinterpret_cast<NMHDR*>(LParam)->hwndFrom==Value->Editor &&
            reinterpret_cast<NMHDR*>(LParam)->code==EN_SELCHANGE) {
            std::wstring Text;
            if(!ReadCanonicalText(Value->Editor,Text)) return DefWindowProcW(Host,Message,WParam,LParam);
            CHARRANGE Selection{};
            SendMessageW(Value->Editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&Selection));
            const int Start=ScalarAt(Text,Selection.cpMin);
            const int End=ScalarAt(Text,Selection.cpMax);
            Value->Callback(Value->Context,1,Text.c_str(),Start,End-Start);
        }
    } catch(...) { /* Native notifications cannot unwind through the window procedure. */ }
    if(Message==WM_NCDESTROY && Value) {
        if(Value->OleGuard) {
            Value->OleGuard->Detach();
            Value->OleGuard->Release();
            Value->OleGuard=nullptr;
        }
        SetWindowLongPtrW(Host,GWLP_USERDATA,0);
        delete Value;
    }
    return DefWindowProcW(Host,Message,WParam,LParam);
}
static HWND CreateHost(HWND Parent,NativeTextEventV1 Callback,void* Context,bool Popup) noexcept {
    if(!Parent) Parent=Popup?PopupParkingWindow():ParkingWindow();
    if(!Parent || !IsWindow(Parent) || !Callback ||
        GetWindowThreadProcessId(Parent,nullptr)!=GetCurrentThreadId()) return nullptr;
    static HMODULE RichEditModule=LoadLibraryW(L"Msftedit.dll");
    if(!RichEditModule) return nullptr;
    static const wchar_t Name[]=L"LuiNativeCodeEditorHostV1";
    WNDCLASSW Class{}; Class.lpfnWndProc=HostProc; Class.hInstance=GetModuleHandleW(nullptr);
    Class.lpszClassName=Name;
    if(!RegisterClassW(&Class) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    auto* Value=new(std::nothrow) NativeTextHostV1{};
    if(!Value) return nullptr;
    Value->Identity=NextHostIdentity.fetch_add(1,std::memory_order_relaxed);
    Value->Callback=Callback; Value->Context=Context;
    HWND Host=CreateWindowExW(Popup?WS_EX_TOOLWINDOW:0,Name,L"",
        (Popup?WS_POPUP:WS_CHILD)|WS_CLIPCHILDREN,
        0,0,1,1,Parent,nullptr,Class.hInstance,Value);
    if(!Host) {delete Value; return nullptr;}
    Value->Editor=CreateWindowExW(0,MSFTEDIT_CLASS,L"",
        WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|ES_NOHIDESEL,
        0,0,1,1,Host,nullptr,Class.hInstance,nullptr);
    if(!Value->Editor) {DestroyWindow(Host); return nullptr;}
    if(!SetWindowSubclass(Value->Editor,EditorProc,1,reinterpret_cast<DWORD_PTR>(Value))) {
        DestroyWindow(Host); return nullptr;
    }
    auto* OleGuard=new(std::nothrow) NativeTextOleGuard{Value};
    if(!OleGuard) {DestroyWindow(Host);return nullptr;}
    Value->OleGuard=OleGuard; // The host holds its initial COM reference.
    const bool OleInstalled=SendMessageW(Value->Editor,EM_SETOLECALLBACK,0,
        reinterpret_cast<LPARAM>(OleGuard))!=0;
    if(!OleInstalled) {DestroyWindow(Host);return nullptr;}
    SendMessageW(Value->Editor,EM_SETEVENTMASK,0,ENM_CHANGE|ENM_SELCHANGE);
    SendMessageW(Value->Editor,EM_EXLIMITTEXT,0,CodeEditorMaxRawUnits);
    if(!Popup) ShowWindow(Host,SW_SHOW);
    return Host;
}
extern "C" __declspec(dllexport) HWND __cdecl LuiWinUITextCreateHostV1(
    HWND Parent,NativeTextEventV1 Callback,void* Context) noexcept {
    return CreateHost(Parent,Callback,Context,false);
}
// Owned popup probe avoids the WinUI XAML child-HWND airspace obstruction. It
// remains an explicit qualification path until movement, clipping, focus and
// accessibility are verified in a real WinUI window.
extern "C" __declspec(dllexport) HWND __cdecl LuiWinUITextCreatePopupHostV1(
    HWND Owner,NativeTextEventV1 Callback,void* Context) noexcept {
    return CreateHost(Owner,Callback,Context,true);
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetOwnerV1(HWND Host,HWND Owner) noexcept {
    if(!Owner) Owner=PopupParkingWindow();
    if(!Host || !Owner || !IsWindow(Host) || !IsWindow(Owner) ||
        (GetWindowLongPtrW(Host,GWL_STYLE)&WS_POPUP)==0 ||
        GetWindowThreadProcessId(Host,nullptr)!=GetCurrentThreadId() ||
        GetWindowThreadProcessId(Owner,nullptr)!=GetCurrentThreadId()) return 0;
    SetLastError(0);
    const LONG_PTR Previous=SetWindowLongPtrW(Host,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(Owner));
    if(Previous==0 && GetLastError()!=0) return 0;
    if(GetWindow(Host,GW_OWNER)!=Owner) return 0;
    if(auto* Value=State(Host)) ++Value->ContextEpoch;
    else return 0;
    return 1;
}
extern "C" __declspec(dllexport) HWND __cdecl LuiWinUITextEditorV1(HWND Host) noexcept {
    auto* Value=Host && IsWindow(Host)?State(Host):nullptr;
    return Value?Value->Editor:nullptr;
}
// Private post-default ordering probe. It only observes bare function keys;
// no public Window.Shortcut event or generic handled-key claim follows.
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetShortcutProbeV1(
    HWND Host,int Enabled) noexcept {
    auto* Value=Host && IsWindow(Host)?State(Host):nullptr;
    if(!Value || !Value->Editor || GetWindowThreadProcessId(Host,nullptr)!=GetCurrentThreadId() ||
        (Enabled!=0 && Enabled!=1)) return 0;
    Value->ProbeFunctionKeys=Enabled!=0;
    return 1;
}
// Synthetic lifecycle regression only: an IME end must retire native state
// even while owner notifications are suppressed. This is not an actual TIP.
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextTestSuppressedImeEndV1(
    HWND Host) noexcept {
    auto* Value=Host && IsWindow(Host)?State(Host):nullptr;
    if(!Value || !Value->Editor || GetWindowThreadProcessId(Host,nullptr)!=GetCurrentThreadId()) return 0;
    HWND Editor=Value->Editor;
    const bool Previous=Value->Applying;
    Value->Applying=true;
    SendMessageW(Editor,WM_IME_STARTCOMPOSITION,0,0);
    if(!IsWindow(Editor) || HostedState(Editor)!=Value) return 0;
    const bool Started=Value->Composing;
    SendMessageW(Editor,WM_IME_ENDCOMPOSITION,0,0);
    if(!IsWindow(Editor) || HostedState(Editor)!=Value) return 0;
    const bool Ended=!Value->Composing;
    Value->Applying=Previous;
    return Started && Ended?1:0;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextDestroyHostV1(HWND Host) noexcept {
    return Host && IsWindow(Host) && GetWindowThreadProcessId(Host,nullptr)==GetCurrentThreadId() && DestroyWindow(Host)?1:0;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetParentV1(HWND Host,HWND Parent) noexcept {
    const bool Park=Parent==nullptr;
    if(Park) Parent=ParkingWindow();
    if(!Host || !Parent || !IsWindow(Host) || !IsWindow(Parent) ||
        GetWindowThreadProcessId(Host,nullptr)!=GetCurrentThreadId() ||
        GetWindowThreadProcessId(Parent,nullptr)!=GetCurrentThreadId()) return 0;
    if(!SetParent(Host,Parent)) return 0;
    if(!Park) ShowWindow(Host,SW_SHOW);
    if(auto* Value=State(Host)) ++Value->ContextEpoch;
    return 1;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetTextV1(HWND Host,const wchar_t* Text) noexcept {
    auto* Value=Host && IsWindow(Host)?State(Host):nullptr;
    if(!Value || !Value->Editor || !Text) return 0;
    const int Scalars=ScalarCount(Text);
    if(Scalars>Value->MaxLength) return 0;
    int Units=0;
    while(Units<=CodeEditorMaxUnits && Text[Units]) ++Units;
    if(Units>CodeEditorMaxUnits) return 0;
    for(int Index=0;Index<Units;++Index) if(Text[Index]==L'\r') return 0;
    const int Bytes=Units?WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,
        Text,Units,nullptr,0,nullptr,nullptr):0;
    if((Units && Bytes<=0) || Bytes>CodeEditorMaxUtf8Bytes) return 0;
    Value->Applying=true;
    const BOOL Result=SetWindowTextW(Value->Editor,Text);
    if(Result) {
        Value->LastTextScalars=Scalars;
        ++Value->ContextEpoch;
    }
    Value->Applying=false;
    return Result?1:0;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetLimitV1(HWND Host,int MaxLength) noexcept {
    auto* Value=Host && IsWindow(Host)?State(Host):nullptr;
    if(!Value || !Value->Editor || MaxLength<0 || MaxLength>CodeEditorMaxUnits) return 0;
    if(MaxLength<Value->LastTextScalars || MaxLength<Value->MaxUndoScalars) return 0;
    Value->MaxLength=MaxLength;
    return 1;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetSelectionV1(HWND Host,int Start,int End) noexcept {
    auto* Value=Host && IsWindow(Host)?State(Host):nullptr;
    if(!Value || !Value->Editor || Start<0 || End<Start || End>CodeEditorMaxUnits) return 0;
    CHARRANGE Selection{Start,End};
    Value->Applying=true;
    SendMessageW(Value->Editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&Selection));
    Value->Applying=false;
    ++Value->ContextEpoch;
    return 1;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetBoundsV1(HWND Host,int X,int Y,int Width,int Height) noexcept {
    auto* Value=Host && IsWindow(Host)?State(Host):nullptr;
    if(!Value || !Value->Editor || Width<0 || Height<0) return 0;
    if(!SetWindowPos(Host,HWND_TOP,X,Y,Width,Height,SWP_NOACTIVATE) ||
        !SetWindowPos(Value->Editor,nullptr,0,0,Width,Height,SWP_NOZORDER|SWP_NOACTIVATE)) return 0;
    ++Value->ContextEpoch;
    return 1;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetClipV1(HWND Host,int Left,int Top,int Right,int Bottom) noexcept {
    if(!Host || !IsWindow(Host) || Right<Left || Bottom<Top) return 0;
    HRGN Region=CreateRectRgn(Left,Top,Right,Bottom);
    if(!Region) return 0;
    if(!SetWindowRgn(Host,Region,TRUE)) {DeleteObject(Region); return 0;}
    return 1;
}
// A bounded private airspace probe. The caller supplies opaque higher-layer
// rectangles in host-local physical pixels. The resulting region belongs to
// the HWND; no editable text or formatting state is copied into the overlay.
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetClipV2(
    HWND Host,int Left,int Top,int Right,int Bottom,const RECT* Holes,std::uint32_t Count) noexcept {
    if(!Host || !IsWindow(Host) || GetWindowThreadProcessId(Host,nullptr)!=GetCurrentThreadId() ||
        Right<Left || Bottom<Top || Count>16 || (Count && !Holes)) return 0;
    wchar_t ClassName[40]{};
    if(GetClassNameW(Host,ClassName,40)==0 ||
        lstrcmpW(ClassName,L"LuiNativeCodeEditorHostV1")!=0 ||
        !State(Host) || !IsWindow(State(Host)->Editor)) return 0;
    constexpr int Limit=1000000;
    if(Left< -Limit || Top< -Limit || Right>Limit || Bottom>Limit) return 0;
    for(std::uint32_t Index=0;Index<Count;++Index)
        if(Holes[Index].right<Holes[Index].left || Holes[Index].bottom<Holes[Index].top ||
            Holes[Index].left< -Limit || Holes[Index].top< -Limit ||
            Holes[Index].right>Limit || Holes[Index].bottom>Limit) return 0;
    HRGN Region=CreateRectRgn(Left,Top,Right,Bottom);
    if(!Region) return 0;
    for(std::uint32_t Index=0;Index<Count;++Index) {
        HRGN Hole=CreateRectRgn(Holes[Index].left,Holes[Index].top,
            Holes[Index].right,Holes[Index].bottom);
        if(!Hole) {DeleteObject(Region);return 0;}
        const int Result=CombineRgn(Region,Region,Hole,RGN_DIFF);
        DeleteObject(Hole);
        if(Result==ERROR) {DeleteObject(Region);return 0;}
    }
    if(!SetWindowRgn(Host,Region,TRUE)) {DeleteObject(Region);return 0;}
    return 1;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetVisibleV1(HWND Host,int Visible) noexcept {
    if(!Host || !IsWindow(Host)) return 0;
    ShowWindow(Host,Visible?((GetWindowLongPtrW(Host,GWL_STYLE)&WS_POPUP)?SW_SHOWNOACTIVATE:SW_SHOW):SW_HIDE);
    return 1;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetEnabledV1(HWND Host,int Enabled) noexcept {
    auto* Value=Host && IsWindow(Host)?State(Host):nullptr;
    if(!Value || !Value->Editor) return 0;
    EnableWindow(Value->Editor,Enabled!=0);
    return IsWindowEnabled(Value->Editor)==(Enabled!=0)?1:0;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextFocusV1(HWND Host) noexcept {
    auto* Value=Host && IsWindow(Host)?State(Host):nullptr;
    if(!Value || !Value->Editor) return 0;
    SetFocus(Value->Editor); return GetFocus()==Value->Editor?1:0;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextClearTemporaryColorsV1(HWND Editor) noexcept {
    if(!Editor || !IsWindow(Editor) || GetWindowThreadProcessId(Editor,nullptr)!=GetCurrentThreadId()) return 0;
    wchar_t ClassName[32]{};
    if(GetClassNameW(Editor,ClassName,32)==0 || lstrcmpW(ClassName,MSFTEDIT_CLASS)!=0) return 0;
    ComPtr<IRichEditOle> Ole;
    if(!SendMessageW(Editor,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(Ole.GetAddressOf()))) return 0;
    ComPtr<ITextDocument> Document;
    if(FAILED(Ole.As(&Document))) return 0;
    ComPtr<ITextRange> Range;
    long Length=0;
    if(FAILED(Document->Range(0,0,&Range)) || FAILED(Range->GetStoryLength(&Length)) ||
        Length<1 || FAILED(Range->SetEnd(Length-1))) return 0;
    ComPtr<ITextFont> Font;
    return SUCCEEDED(Range->GetFont(&Font)) && SUCCEEDED(Font->Reset(tomApplyTmp)) &&
        SUCCEEDED(Font->SetForeColor(tomAutoColor))?1:0;
}
// Candidate order is kept separate until direct rendered-color qualification.
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextApplyCandidateColorsV1(
    HWND Editor,const LuiWinUITextColorSpanV1* Spans,std::uint32_t Count) noexcept {
    if(!Editor || !Spans || Count==0 || Count>128 || !IsWindow(Editor) ||
        GetWindowThreadProcessId(Editor,nullptr)!=GetCurrentThreadId()) return 0;
    wchar_t ClassName[32]{};
    if(GetClassNameW(Editor,ClassName,32)==0 || lstrcmpW(ClassName,MSFTEDIT_CLASS)!=0) return 0;
    int Previous=0;
    for(std::uint32_t Index=0;Index<Count;++Index) {
        if(Spans[Index].Start<Previous || Spans[Index].End<=Spans[Index].Start ||
            Spans[Index].End>CodeEditorMaxUnits || (Spans[Index].Color&0xFF000000u)!=0) return 0;
        Previous=Spans[Index].End;
    }
    ComPtr<IRichEditOle> Ole;
    if(!SendMessageW(Editor,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(Ole.GetAddressOf()))) return 0;
    ComPtr<ITextDocument> Document;
    if(FAILED(Ole.As(&Document))) return 0;
    for(std::uint32_t Index=0;Index<Count;++Index) {
        ComPtr<ITextRange> Range;ComPtr<ITextFont> Font;
        if(FAILED(Document->Range(Spans[Index].Start,Spans[Index].End,&Range)) ||
            FAILED(Range->GetFont(&Font)) || FAILED(Font->Reset(tomApplyLater)) ||
            FAILED(Font->SetForeColor(static_cast<long>(Spans[Index].Color))) ||
            FAILED(Font->Reset(tomApplyTmp))) return 0;
    }
    return 1;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetFixtureBaseColorV1(
    HWND Editor,std::uint32_t Color) noexcept {
    if(!Editor || !IsWindow(Editor) || (Color&0xFF000000u)!=0 ||
        GetWindowThreadProcessId(Editor,nullptr)!=GetCurrentThreadId()) return 0;
    CHARFORMAT2W Format{};Format.cbSize=sizeof(Format);Format.dwMask=CFM_COLOR;
    Format.crTextColor=static_cast<COLORREF>(Color);
    ApplyingGuard Guard(Editor);
    return SendMessageW(Editor,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&Format))?1:0;
}
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextGetFixtureColorV1(
    HWND Editor,long Start,long End,long* Color) noexcept {
    if(!Editor || !IsWindow(Editor) || !Color || Start<0 || End<=Start ||
        GetWindowThreadProcessId(Editor,nullptr)!=GetCurrentThreadId()) return 0;
    ComPtr<IRichEditOle> Ole;
    if(!SendMessageW(Editor,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(Ole.GetAddressOf()))) return 0;
    ComPtr<ITextDocument> Document;
    ComPtr<ITextRange> Range;
    ComPtr<ITextFont> Font;
    return SUCCEEDED(Ole.As(&Document)) && SUCCEEDED(Document->Range(Start,End,&Range)) &&
        SUCCEEDED(Range->GetFont(&Font)) && SUCCEEDED(Font->GetForeColor(Color))?1:0;
}

// Apply cosmetic character colors without placing formatting operations in the
// native edit history. This uses the documented TOM undo suspension; the Rich
// Edit modified bit is restored because formatting alone marks it dirty.
extern "C" __declspec(dllexport) int __cdecl LuiWinUITextSetColorsV1(
    HWND Editor,const LuiWinUITextColorSpanV1* Spans,std::uint32_t Count) noexcept {
    if(!Editor || !IsWindow(Editor) || (Count && !Spans) || Count>128 ||
        GetWindowThreadProcessId(Editor,nullptr)!=GetCurrentThreadId()) return 0;
    wchar_t ClassName[32]{};
    if(GetClassNameW(Editor,ClassName,32)==0 || lstrcmpW(ClassName,MSFTEDIT_CLASS)!=0) return 0;
    ComPtr<IRichEditOle> Ole;
    if(!SendMessageW(Editor,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(Ole.GetAddressOf()))) return 0;
    ComPtr<ITextDocument> Document;
    if(FAILED(Ole.As(&Document))) return 0;
    ComPtr<ITextRange> Story;
    long StoryLength=0;
    if(FAILED(Document->Range(0,0,&Story)) || FAILED(Story->GetStoryLength(&StoryLength)) ||
        StoryLength<1 || StoryLength>CodeEditorMaxUnits+1) return 0;
    std::int32_t Previous=0;
    for(std::uint32_t Index=0;Index<Count;++Index) {
        if(Spans[Index].Start<Previous || Spans[Index].End<=Spans[Index].Start ||
            Spans[Index].End>StoryLength-1 || (Spans[Index].Color&0xFF000000u)!=0) return 0;
        Previous=Spans[Index].End;
    }
    std::wstring Before;
    if(!ReadCanonicalText(Editor,Before)) return 0;
    CHARRANGE Selection{};
    SendMessageW(Editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&Selection));
    POINT Scroll{};
    SendMessageW(Editor,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&Scroll));
    const WPARAM WasModified=SendMessageW(Editor,EM_GETMODIFY,0,0)?TRUE:FALSE;
    ApplyingGuard Guard(Editor);
    if(FAILED(Document->Undo(tomSuspend,nullptr))) return 0;
    bool Applied=true;
    if(StoryLength>1) {
        ComPtr<ITextRange> Range;
        ComPtr<ITextFont> Font;
        Applied=SUCCEEDED(Document->Range(0,StoryLength-1,&Range)) &&
            SUCCEEDED(Range->GetFont(&Font)) && SUCCEEDED(Font->SetForeColor(tomAutoColor));
    }
    for(std::uint32_t Index=0;Applied && Index<Count;++Index) {
        ComPtr<ITextRange> Range;
        ComPtr<ITextFont> Font;
        Applied=SUCCEEDED(Document->Range(Spans[Index].Start,Spans[Index].End,&Range)) &&
            SUCCEEDED(Range->GetFont(&Font)) &&
            SUCCEEDED(Font->SetForeColor(static_cast<long>(Spans[Index].Color)));
    }
    const bool Resumed=SUCCEEDED(Document->Undo(tomResume,nullptr));
    SendMessageW(Editor,EM_SETMODIFY,WasModified,0);
    std::wstring After;
    CHARRANGE AfterSelection{};
    POINT AfterScroll{};
    SendMessageW(Editor,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&AfterSelection));
    SendMessageW(Editor,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&AfterScroll));
    const bool TextPreserved=ReadCanonicalText(Editor,After) && Before==After;
    const bool SelectionPreserved=Selection.cpMin==AfterSelection.cpMin && Selection.cpMax==AfterSelection.cpMax;
    const bool ScrollPreserved=Scroll.x==AfterScroll.x && Scroll.y==AfterScroll.y;
    const bool ModifiedPreserved=(SendMessageW(Editor,EM_GETMODIFY,0,0)!=0)==(WasModified!=0);
    return Applied && Resumed && TextPreserved && SelectionPreserved && ScrollPreserved && ModifiedPreserved?1:0;
}
