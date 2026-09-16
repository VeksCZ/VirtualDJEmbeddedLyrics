#include "EmbeddedLyricsEditor.hpp"
#ifdef _WIN32
#include <algorithm>
namespace { constexpr wchar_t kClass[]=L"EmbeddedLyricsEditorDialog"; constexpr int kMode=100,kText=101; struct State{EmbeddedLyricsEdit value;bool accepted{},done{};HWND owner{};};
HWND Add(HWND p,const wchar_t*k,const wchar_t*t,DWORD s,int x,int y,int w,int h,int id=0){return CreateWindowExW(0,k,t,WS_CHILD|WS_VISIBLE|s,x,y,w,h,p,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);}
void Store(HWND w,State&s,bool timed){HWND e=GetDlgItem(w,kText);int n=GetWindowTextLengthW(e);std::wstring v(static_cast<size_t>(std::max(0,n))+1,L'\0');GetWindowTextW(e,v.data(),n+1);v.resize(static_cast<size_t>(std::max(0,n)));if(timed)s.value.timedText=std::move(v);else s.value.untimedText=std::move(v);}
void Show(HWND w,State&s){const auto&v=SendDlgItemMessageW(w,kMode,CB_GETCURSEL,0,0)==0?s.value.timedText:s.value.untimedText;SetDlgItemTextW(w,kText,v.c_str());}
LRESULT CALLBACK Proc(HWND w,UINT m,WPARAM a,LPARAM b){auto*s=reinterpret_cast<State*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(m==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(b)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}switch(m){case WM_CREATE:{Add(w,L"STATIC",L"Lyrics type",0,16,18,100,20);auto mode=Add(w,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,118,14,180,110,kMode);SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Timed (LRC timestamps)"));SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Untimed"));SendMessageW(mode,CB_SETCURSEL,s->value.synchronized?0:1,0);Add(w,L"STATIC",L"Timed rows use [mm:ss.xx] text. Each type has its own saved text.",0,16,48,650,20);Add(w,L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,16,72,650,350,kText);SendDlgItemMessageW(w,kText,EM_SETLIMITTEXT,1024*1024,0);Show(w,*s);Add(w,L"BUTTON",L"Save to MP3 tag",BS_DEFPUSHBUTTON|WS_TABSTOP,430,438,130,30,IDOK);Add(w,L"BUTTON",L"Cancel",BS_PUSHBUTTON|WS_TABSTOP,572,438,94,30,IDCANCEL);return 0;}case WM_COMMAND:if(LOWORD(a)==kMode&&HIWORD(a)==CBN_SELCHANGE){Store(w,*s,SendDlgItemMessageW(w,kMode,CB_GETCURSEL,0,0)!=0);Show(w,*s);return 0;}if(LOWORD(a)==IDOK){Store(w,*s,SendDlgItemMessageW(w,kMode,CB_GETCURSEL,0,0)==0);s->value.synchronized=SendDlgItemMessageW(w,kMode,CB_GETCURSEL,0,0)==0;s->accepted=true;s->done=true;DestroyWindow(w);return 0;}if(LOWORD(a)==IDCANCEL){s->done=true;DestroyWindow(w);return 0;}break;case WM_CLOSE:if(s)s->done=true;DestroyWindow(w);return 0;}return DefWindowProcW(w,m,a,b);}}
bool ShowEmbeddedLyricsEditor(HWND o,EmbeddedLyricsEdit&e){WNDCLASSEXW c{sizeof(c)};c.lpfnWndProc=Proc;c.hInstance=GetModuleHandleW(nullptr);c.hCursor=LoadCursorW(nullptr,IDC_IBEAM);c.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);c.lpszClassName=kClass;RegisterClassExW(&c);State s{e,false,false,o};
    // The dialog now runs on its own background thread (see Plugin.cpp), so its input queue is
    // not attached to the owner window's thread by default. Without AttachThreadInput, Windows
    // can fail to hand activation back to the owner when this popup is destroyed and instead
    // minimizes it. Attaching for the lifetime of the dialog makes activation behave as if this
    // ran on the owner's own thread, like it did before the dialog was moved off it.
    const DWORD ownerThreadId = o ? GetWindowThreadProcessId(o, nullptr) : 0;
    const DWORD thisThreadId = GetCurrentThreadId();
    const bool attached = ownerThreadId && ownerThreadId != thisThreadId &&
        AttachThreadInput(thisThreadId, ownerThreadId, TRUE);
    HWND w=CreateWindowExW(WS_EX_DLGMODALFRAME,kClass,L"Edit embedded lyrics",WS_CAPTION|WS_SYSMENU|WS_POPUP,CW_USEDEFAULT,CW_USEDEFAULT,690,510,o,nullptr,GetModuleHandleW(nullptr),&s);
    if(!w){if(attached)AttachThreadInput(thisThreadId,ownerThreadId,FALSE);return false;}
    if(o)EnableWindow(o,FALSE);ShowWindow(w,SW_SHOW);SetForegroundWindow(w);MSG m{};while(!s.done&&GetMessageW(&m,nullptr,0,0)>0)if(!IsDialogMessageW(w,&m)){TranslateMessage(&m);DispatchMessageW(&m);}if(o){EnableWindow(o,TRUE);SetActiveWindow(o);SetForegroundWindow(o);}if(attached)AttachThreadInput(thisThreadId,ownerThreadId,FALSE);if(s.accepted)e=std::move(s.value);return s.accepted;}
#endif

