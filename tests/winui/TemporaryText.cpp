#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Require(bool Value, const char* Name) { if (!Value) throw std::runtime_error(Name); }
static void Check(HRESULT Value, const char* Name) { Require(SUCCEEDED(Value), Name); }
static std::wstring Text(HWND Edit) {
    int Length=GetWindowTextLengthW(Edit);
    std::wstring Value(static_cast<size_t>(Length)+1,L'\0');
    Require(GetWindowTextW(Edit,Value.data(),Length+1)==Length,"ReadText");
    Value.resize(Length); return Value;
}
static std::string Escaped(const std::wstring& Value) {
    std::string Result;
    for(wchar_t Character: Value) {
        if(Character==L'\r')Result+="<CR>";
        else if(Character==L'\n')Result+="<LF>";
        else if(Character<128)Result+=static_cast<char>(Character);
        else Result+="<Other>";
    }
    return Result;
}
static void Style(ITextDocument* Document, long Start, long End, long Color) {
    ComPtr<ITextRange> Range; ComPtr<ITextFont> Font;
    Check(Document->Range(Start,End,&Range),"Range");
    Check(Range->GetFont(&Font),"Font");
    Check(Font->Reset(tomApplyTmp),"TemporaryFormat");
    Check(Font->SetForeColor(Color),"Color");
}
int wmain() {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    HRESULT Initialized=OleInitialize(nullptr);
    if(FAILED(Initialized))return 2;
    HWND Parent=nullptr,Edit=nullptr;
    HMODULE Module=nullptr;
    int ExitCode=1;
    try {
        Module=LoadLibraryW(L"Msftedit.dll"); Require(Module!=nullptr,"LoadSystemRichEdit");
        Parent=CreateWindowExW(0,L"STATIC",L"LUI native text proof",WS_POPUP,0,0,700,400,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Require(Parent!=nullptr,"Parent");
        Edit=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL,
            0,0,680,380,Parent,nullptr,GetModuleHandleW(nullptr),nullptr);
        Require(Edit!=nullptr,"Editor");
        ComPtr<IRichEditOle> Ole;
        Require(SendMessageW(Edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(Ole.GetAddressOf()))!=0,"GetOle");
        ComPtr<ITextDocument> Document;
        Check(Ole.As(&Document),"QuerySupportedTOM");
        Require(SetWindowTextW(Edit,L"A\r\nB\nC\rD")!=0,"MixedEndingsSource");
        const std::wstring MixedExternal=Text(Edit);
        wchar_t RawBuffer[32]{};
        GETTEXTEX RawQuery{};
        RawQuery.cb=sizeof(RawBuffer);RawQuery.flags=GT_RAWTEXT;RawQuery.codepage=1200;
        const LRESULT RawLength=SendMessageW(Edit,EM_GETTEXTEX,
            reinterpret_cast<WPARAM>(&RawQuery),reinterpret_cast<LPARAM>(RawBuffer));
        std::printf("[LUI:NativeTextProof] MixedEndings External=%s Raw=%s\n",
            Escaped(MixedExternal).c_str(),Escaped(RawBuffer).c_str());
        Require(MixedExternal==L"A\r\nB\r\nC\r\nD" && RawLength==7 &&
            std::wstring(RawBuffer)==L"A\rB\rC\rD","MixedEndingsCollapse");
        Require(SetWindowTextW(Edit,L"A\nreturn")!=0,"LfSource");
        Require(Text(Edit)==L"A\r\nreturn","RichEditExternalCrLfNormalization");
        ComPtr<ITextRange> LfRange;
        Check(Document->Range(2,8,&LfRange),"LfTomRange");
        BSTR LfText=nullptr;
        Check(LfRange->GetText(&LfText),"LfTomText");
        Require(LfText && std::wstring(LfText,SysStringLen(LfText))==L"return","LfScalarToTomPosition");
        SysFreeString(LfText);
        const std::wstring Base=L"local Value = '\U0001F600'\r\nreturn Value";
        Require(SetWindowTextW(Edit,Base.c_str())!=0,"InitialSource");
        const std::wstring Initial=Text(Edit);
        const size_t ReturnAt=Initial.find(L"return");
        Require(ReturnAt!=std::wstring::npos,"ReturnTokenPresent");
        ComPtr<ITextRange> ExternalRange;
        Check(Document->Range(static_cast<long>(ReturnAt),static_cast<long>(ReturnAt+6),&ExternalRange),"ExternalCrLfRange");
        BSTR ExternalText=nullptr;
        Check(ExternalRange->GetText(&ExternalText),"ExternalCrLfRangeText");
        Require(!ExternalText || std::wstring(ExternalText,SysStringLen(ExternalText))!=L"return","ExternalCrLfOffsetDiffers");
        SysFreeString(ExternalText);
        ComPtr<ITextRange> ReturnRange;
        Check(Document->Range(static_cast<long>(ReturnAt-1),static_cast<long>(ReturnAt+5),&ReturnRange),"NormalizedUnicodeRange");
        BSTR ReturnText=nullptr;
        Check(ReturnRange->GetText(&ReturnText),"NormalizedUnicodeRangeText");
        Require(ReturnText && std::wstring(ReturnText,SysStringLen(ReturnText))==L"return","NormalizedUnicodeOffsetAgreement");
        SysFreeString(ReturnText);
        SendMessageW(Edit,EM_EMPTYUNDOBUFFER,0,0); // Only fixture setup, never styling.
        SendMessageW(Edit,EM_SETSEL,static_cast<WPARAM>(-1),-1);
        SendMessageW(Edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L" -- edit1"));
        SendMessageW(Edit,EM_STOPGROUPTYPING,0,0);
        const std::wstring Edited=Text(Edit);
        Require(Edited!=Initial,"NativeUndoableEdit");
        CHARRANGE Selection{2,8}; SendMessageW(Edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&Selection));
        POINT Scroll{}; SendMessageW(Edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&Scroll));
        SendMessageW(Edit,EM_SETMODIFY,FALSE,0);
        for(long Index=0;Index<8;++Index) Style(Document.Get(),0,5,RGB(120+Index,80,190));
        CHARRANGE After{}; SendMessageW(Edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&After));
        POINT AfterScroll{}; SendMessageW(Edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&AfterScroll));
        Require(Text(Edit)==Edited,"StylePreservesExactText");
        Require(After.cpMin==Selection.cpMin && After.cpMax==Selection.cpMax,"StylePreservesSelection");
        Require(AfterScroll.x==Scroll.x && AfterScroll.y==Scroll.y,"StylePreservesScroll");
        Require(SendMessageW(Edit,EM_GETMODIFY,0,0)==0,"StylePreservesModifiedFlag");
        Require(SendMessageW(Edit,EM_CANUNDO,0,0)!=0,"UndoAvailable");
        Require(SendMessageW(Edit,EM_UNDO,0,0)!=0 && Text(Edit)==Initial,"OneUndoRestoresText");
        Require(SendMessageW(Edit,EM_CANREDO,0,0)!=0,"RedoAvailable");
        Style(Document.Get(),0,5,RGB(10,120,230));
        Require(SendMessageW(Edit,EM_CANREDO,0,0)!=0,"StylingPreservesRedo");
        Require(SendMessageW(Edit,EM_REDO,0,0)!=0 && Text(Edit)==Edited,"RedoRestoresExactText");
        std::printf("[LUI:NativeTextProof] PASS Text Selection Scroll Modified Undo Redo Unicode\n");
        std::wstring Large;
        while(Large.size()<32000)Large+=L"local Value = 'text' -- comment\r\n";
        Require(SetWindowTextW(Edit,Large.c_str())!=0,"LargeFixture");
        const std::wstring LargeBefore=Text(Edit);
        std::vector<double> Samples;
        for(int Sample=0;Sample<136;++Sample) {
            auto Started=std::chrono::steady_clock::now();
            {
              for(long Span=0;Span<128;++Span) {
                ComPtr<ITextRange> Range; ComPtr<ITextFont> Font;
                Check(Document->Range(Span*32,Span*32+5,&Range),"BatchRange");
                Check(Range->GetFont(&Font),"BatchFont");
                Check(Font->Reset(tomApplyTmp),"BatchTemporaryFormat");
                Check(Font->SetForeColor(RGB(20+Sample%100,110,180)),"BatchColor");
              }
            }
            if(Sample>=8)Samples.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-Started).count());
        }
        Require(Text(Edit)==LargeBefore,"LargeExactSource");
        std::sort(Samples.begin(),Samples.end());
        std::printf("[LUI:NativeTextProof] NativeRangeBatchUs P50=%.3f P95=%.3f P99=%.3f Spans=128 Samples=128 UTF16Units=%zu DisplayLatency=NOT_MEASURED Allocations=NOT_MEASURED\n",
            Samples[63],Samples[121],Samples[126],LargeBefore.size());
        Document.Reset(); Ole.Reset(); ExitCode=0;
    } catch(const std::exception& Error) { std::fprintf(stderr,"[LUI:NativeTextProof] FAIL %s\n",Error.what()); }
    if(Edit)DestroyWindow(Edit);
    if(Parent)DestroyWindow(Parent);
    if(Module)FreeLibrary(Module);
    OleUninitialize(); return ExitCode;
}
