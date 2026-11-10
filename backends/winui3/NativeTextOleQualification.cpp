#include "NativeText.cpp"
#include <cstdio>
#include <utility>

static void __cdecl IgnoreEvent(void*,int,const wchar_t*,int,int) {}

class ReleaseDestroy final : public IUnknown {
    LONG References=1;
    HWND Host;
    HGLOBAL Data;
public:
    ReleaseDestroy(HWND Window,HGLOBAL Bytes) noexcept:Host(Window),Data(Bytes) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Interface,void** Result) override {
        if(!Result) return E_POINTER;
        *Result=nullptr;
        if(!IsEqualIID(Interface,IID_IUnknown)) return E_NOINTERFACE;
        *Result=static_cast<IUnknown*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return static_cast<ULONG>(InterlockedIncrement(&References));}
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG Remaining=static_cast<ULONG>(InterlockedDecrement(&References));
        if(!Remaining) {if(IsWindow(Host)) DestroyWindow(Host);GlobalFree(Data);delete this;}
        return Remaining;
    }
};

class ReentrantData final : public IDataObject {
    LONG References=1;
    HWND Host,Editor;
    int Mode;
    std::wstring Payload;
public:
    ReentrantData(HWND Window,HWND TextWindow,int Action,std::wstring Text=L"z"):
        Host(Window),Editor(TextWindow),Mode(Action),Payload(std::move(Text)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Interface,void** Result) override {
        if(!Result) return E_POINTER;
        *Result=nullptr;
        if(!IsEqualIID(Interface,IID_IUnknown) && !IsEqualIID(Interface,IID_IDataObject))
            return E_NOINTERFACE;
        *Result=static_cast<IDataObject*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return static_cast<ULONG>(InterlockedIncrement(&References));}
    ULONG STDMETHODCALLTYPE Release() override {return static_cast<ULONG>(InterlockedDecrement(&References));}
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* Format,STGMEDIUM* Medium) override {
        if(!Format || !Medium || Format->cfFormat!=CF_UNICODETEXT) return DV_E_FORMATETC;
        HGLOBAL Data=GlobalAlloc(GMEM_MOVEABLE,(Payload.size()+1)*sizeof(wchar_t));
        if(!Data) return E_OUTOFMEMORY;
        auto* Bytes=static_cast<wchar_t*>(GlobalLock(Data));
        if(!Bytes) {GlobalFree(Data);return E_OUTOFMEMORY;}
        std::copy(Payload.begin(),Payload.end(),Bytes);
        Bytes[Payload.size()]=L'\0';GlobalUnlock(Data);
        Medium->tymed=TYMED_HGLOBAL;Medium->hGlobal=Data;Medium->pUnkForRelease=nullptr;
        if(Mode==1) {
            CHARRANGE Selection{0,1};
            SendMessageW(Editor,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&Selection));
        } else if(Mode==2) {
            DestroyWindow(Host);
        } else if(Mode==3) {
            Medium->pUnkForRelease=new(std::nothrow) ReleaseDestroy(Host,Data);
            if(!Medium->pUnkForRelease) {Medium->tymed=TYMED_NULL;GlobalFree(Data);return E_OUTOFMEMORY;}
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*,STGMEDIUM*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*,FORMATETC*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*,STGMEDIUM*,BOOL) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD,IEnumFORMATETC**) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*,DWORD,IAdviseSink*,DWORD*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {return E_NOTIMPL;}
};

static bool TestMode(int Mode) {
    HWND Host=LuiWinUITextCreateHostV1(nullptr,IgnoreEvent,nullptr);
    if(!Host || !LuiWinUITextSetTextV1(Host,L"abc")) return false;
    HWND Editor=LuiWinUITextEditorV1(Host);
    if(!Editor || !LuiWinUITextSetSelectionV1(Host,3,3)) return false;
    auto* Guard=State(Host)->OleGuard;
    ReentrantData Data(Host,Editor,Mode);
    const HRESULT Result=Guard->QueryAcceptData(&Data,nullptr,RECO_PASTE,TRUE,nullptr);
    if(Result!=E_FAIL) return false;
    if(Mode==1) {
        std::wstring Text;
        const bool Good=IsWindow(Host) && ReadCanonicalText(Editor,Text) && Text==L"abc" &&
            SendMessageW(Editor,EM_CANUNDO,0,0)==0;
        DestroyWindow(Host);
        return Good;
    }
    return !IsWindow(Host);
}

static bool TestLiteralPaste(const wchar_t* Name,std::wstring Payload,
    const std::wstring& Expected,bool Admit) {
    HWND Host=LuiWinUITextCreateHostV1(nullptr,IgnoreEvent,nullptr);
    if(!Host) return false;
    if(!LuiWinUITextSetTextV1(Host,L"")) {DestroyWindow(Host);return false;}
    HWND Editor=LuiWinUITextEditorV1(Host);
    if(!Editor || !LuiWinUITextSetSelectionV1(Host,0,0)) {
        DestroyWindow(Host);return false;
    }
    SendMessageW(Editor,EM_EMPTYUNDOBUFFER,0,0);
    ReentrantData Data(Host,Editor,0,std::move(Payload));
    const HRESULT Result=State(Host)->OleGuard->QueryAcceptData(&Data,nullptr,RECO_PASTE,TRUE,nullptr);
    std::wstring Text;
    bool Good=ReadCanonicalText(Editor,Text) &&
        (Admit?Result==S_FALSE && Text==Expected && SendMessageW(Editor,EM_CANUNDO,0,0)!=0:
            Result==E_FAIL && Text.empty() && SendMessageW(Editor,EM_CANUNDO,0,0)==0);
    const size_t AcceptedUnits=Text.size();
    bool UndoPreserved=!Admit;
    if(Admit) {
        UndoPreserved=SendMessageW(Editor,EM_UNDO,0,0)!=0 &&
            ReadCanonicalText(Editor,Text) && Text.empty();
        Good=Good && UndoPreserved;
    }
    std::printf("[LUI:NativeText] %ls passed=%d hr=0x%08lX acceptedUnits=%zu undoPreserved=%d\n",
        Name,Good,static_cast<unsigned long>(Result),AcceptedUnits,UndoPreserved);
    DestroyWindow(Host);
    return Good;
}

static bool TestDisabledPaste() {
    HWND Host=LuiWinUITextCreateHostV1(nullptr,IgnoreEvent,nullptr);
    if(!Host || !LuiWinUITextSetTextV1(Host,L"abc")) {
        std::printf("[LUI:NativeText] PasteAdmission setupHost failed error=%lu\n",GetLastError());
        return false;
    }
    HWND Editor=LuiWinUITextEditorV1(Host);
    if(!Editor || !LuiWinUITextSetSelectionV1(Host,3,3)) {
        std::printf("[LUI:NativeText] PasteAdmission setupSelection failed error=%lu\n",GetLastError());
        DestroyWindow(Host);return false;
    }
    HGLOBAL Data=GlobalAlloc(GMEM_MOVEABLE,2*sizeof(wchar_t));
    if(!Data) {
        std::printf("[LUI:NativeText] PasteAdmission GlobalAlloc failed error=%lu\n",GetLastError());
        DestroyWindow(Host);return false;
    }
    auto* Bytes=static_cast<wchar_t*>(GlobalLock(Data));
    if(!Bytes) {
        std::printf("[LUI:NativeText] PasteAdmission GlobalLock failed error=%lu\n",GetLastError());
        GlobalFree(Data);DestroyWindow(Host);return false;
    }
    Bytes[0]=L'z';Bytes[1]=L'\0';GlobalUnlock(Data);
    if(!OpenClipboard(Editor)) {
        std::printf("[LUI:NativeText] PasteAdmission OpenClipboard failed error=%lu\n",GetLastError());
        GlobalFree(Data);DestroyWindow(Host);return false;
    }
    SetLastError(ERROR_SUCCESS);
    const int PriorFormats=CountClipboardFormats();
    const DWORD CountError=GetLastError();
    if(PriorFormats!=0 || CountError!=ERROR_SUCCESS) {
        std::printf("[LUI:NativeText] PasteAdmission priorFormats=%d countError=%lu; untouched\n",
            PriorFormats,CountError);
        CloseClipboard();GlobalFree(Data);DestroyWindow(Host);return false;
    }
    if(!EmptyClipboard()) {
        std::printf("[LUI:NativeText] PasteAdmission EmptyClipboard failed error=%lu\n",GetLastError());
        CloseClipboard();GlobalFree(Data);DestroyWindow(Host);return false;
    }
    if(!SetClipboardData(CF_UNICODETEXT,Data)) {
        std::printf("[LUI:NativeText] PasteAdmission SetClipboardData failed error=%lu\n",GetLastError());
        CloseClipboard();GlobalFree(Data);
        DestroyWindow(Host);return false;
    }
    CloseClipboard();
    EnableWindow(Editor,FALSE);
    const bool DisabledState=!IsWindowEnabled(Editor);
    SendMessageW(Editor,WM_PASTE,0,0);
    std::wstring Text;
    const bool DisabledUnchanged=ReadCanonicalText(Editor,Text) && Text==L"abc";
    EnableWindow(Editor,TRUE);
    const bool ReadOnlySet=SendMessageW(Editor,EM_SETREADONLY,TRUE,0)!=0;
    const bool ReadOnlyStyle=(GetWindowLongPtrW(Editor,GWL_STYLE)&ES_READONLY)!=0;
    SendMessageW(Editor,WM_PASTE,0,0);
    const bool ReadOnlyUnchanged=ReadCanonicalText(Editor,Text) && Text==L"abc";
    const bool NoUndo=SendMessageW(Editor,EM_CANUNDO,0,0)==0;
    SendMessageW(Editor,EM_SETREADONLY,FALSE,0);
    bool Restored=false;
    if(OpenClipboard(Editor)) {
        const bool StillOurs=GetClipboardOwner()==Editor;
        const bool Cleared=StillOurs && EmptyClipboard()!=FALSE;
        const bool Closed=CloseClipboard()!=FALSE;
        if(Cleared && Closed && OpenClipboard(Editor)) {
            SetLastError(ERROR_SUCCESS);
            const int RemainingFormats=CountClipboardFormats();
            const DWORD RemainingError=GetLastError();
            const bool VerifyClosed=CloseClipboard()!=FALSE;
            Restored=RemainingFormats==0 && RemainingError==ERROR_SUCCESS && VerifyClosed;
        }
        std::printf("[LUI:NativeText] PasteAdmission clipboardOwnerRetained=%d cleared=%d closed=%d emptyRestored=%d\n",
            StillOurs,Cleared,Closed,Restored);
    } else std::printf("[LUI:NativeText] PasteAdmission cleanupOpen failed error=%lu\n",GetLastError());
    std::printf("[LUI:NativeText] PasteAdmission disabledState=%d disabledText=%d readOnlyReturn=%d readOnlyStyle=%d readOnlyText=%d noUndo=%d clipboardRestored=%d\n",
        DisabledState,DisabledUnchanged,ReadOnlySet,ReadOnlyStyle,ReadOnlyUnchanged,NoUndo,Restored);
    DestroyWindow(Host);
    return DisabledState && DisabledUnchanged && ReadOnlySet && ReadOnlyStyle &&
        ReadOnlyUnchanged && NoUndo && Restored;
}

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    const HRESULT Started=OleInitialize(nullptr);
    if(FAILED(Started)) return 2;
    const bool Selection=TestMode(1);
    const bool GetDataDestroy=TestMode(2);
    const bool ReleaseDestroyCase=TestMode(3);
    const bool DisabledPaste=TestDisabledPaste();
    const bool FullAscii=TestLiteralPaste(L"FullAscii",std::wstring(262144,L'a'),
        std::wstring(262144,L'a'),true);
    std::wstring FullCrLf;FullCrLf.reserve(524288);
    for(int Index=0;Index<262144;++Index) FullCrLf+=L"\r\n";
    const bool FullCrLfPaste=TestLiteralPaste(L"FullCrLf",std::move(FullCrLf),
        std::wstring(262144,L'\n'),true);
    std::wstring FullEmoji;FullEmoji.reserve(131073);
    for(int Index=0;Index<65536;++Index) {FullEmoji.push_back(0xD83D);FullEmoji.push_back(0xDE00);}
    const bool FullUtf8=TestLiteralPaste(L"FullUtf8",FullEmoji,FullEmoji,true);
    FullEmoji.push_back(L'x');
    const bool OverflowUtf8=TestLiteralPaste(L"OverflowUtf8",std::move(FullEmoji),L"",false);
    OleUninitialize();
    std::printf("[LUI:NativeText] ReentrantSelection=%d GetDataDestroy=%d ReleaseDestroy=%d DisabledPaste=%d FullAscii=%d FullCrLf=%d FullUtf8=%d OverflowUtf8=%d\n",
        Selection,GetDataDestroy,ReleaseDestroyCase,DisabledPaste,
        FullAscii,FullCrLfPaste,FullUtf8,OverflowUtf8);
    return Selection && GetDataDestroy && ReleaseDestroyCase && DisabledPaste &&
        FullAscii && FullCrLfPaste && FullUtf8 && OverflowUtf8?0:1;
}
