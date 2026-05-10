// MD Viewer for Total Commander — Stage 1 skeleton
// Creates a child window with RichEdit and loads a hardcoded RTF.
// Next stage: feed actual markdown through md4c and convert to RTF on the fly.

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <richedit.h>
#include <cstring>

// ---- WLX show flags (subset from listplug.h) -------------------------------
#define lcp_wraptext       1
#define lcp_fittowindow    2
#define lcp_ansi           4
#define lcp_ascii          8

// ---- Detect string ---------------------------------------------------------
// TC syntax: https://www.ghisler.com/plugins.htm
// We claim files with .md / .markdown / .mdown / .mkd extensions.
static const char DETECT_STRING[] =
    "EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MDOWN\" | EXT=\"MKD\"";

// ---- DLL globals -----------------------------------------------------------
static HINSTANCE g_hInstance       = nullptr;
static HMODULE   g_richEditModule  = nullptr;
static const wchar_t HOST_CLASS[]  = L"MdViewerHostV1";

// ---- DllMain ---------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hInstance = (HINSTANCE)hModule;
            DisableThreadLibraryCalls(hModule);
            // RichEdit 4.1+ lives in msftedit.dll, window class "RICHEDIT50W".
            g_richEditModule = LoadLibraryW(L"msftedit.dll");
            break;
        case DLL_PROCESS_DETACH:
            if (g_richEditModule) FreeLibrary(g_richEditModule);
            break;
    }
    return TRUE;
}

// ---- RTF stream-in helper --------------------------------------------------
struct RtfStreamCtx {
    const char* data;
    size_t      size;
    size_t      pos;
};

static DWORD CALLBACK RtfStreamCallback(DWORD_PTR cookie, LPBYTE buf,
                                        LONG cb, LONG* pcb) {
    auto* c = reinterpret_cast<RtfStreamCtx*>(cookie);
    size_t remaining = c->size - c->pos;
    size_t toCopy = ((LONG)remaining < cb) ? remaining : (size_t)cb;
    memcpy(buf, c->data + c->pos, toCopy);
    c->pos += toCopy;
    *pcb = (LONG)toCopy;
    return 0;
}

static void LoadRtfIntoRichEdit(HWND hRichEdit, const char* rtf) {
    RtfStreamCtx ctx = { rtf, strlen(rtf), 0 };
    EDITSTREAM es = {};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&ctx);
    es.pfnCallback = RtfStreamCallback;
    SendMessageW(hRichEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
}

// ---- Stage 1 placeholder content -------------------------------------------
// Just shows that the plugin loaded. Replace with md4c output in Stage 3.
static void LoadPlaceholder(HWND hRichEdit, const wchar_t* fileName) {
    // Build RTF dynamically so we can mention the file path.
    char rtf[2048];
    char fileNameUtf8[1024] = {0};
    WideCharToMultiByte(CP_UTF8, 0, fileName, -1,
                        fileNameUtf8, sizeof(fileNameUtf8), nullptr, nullptr);

    // Escape backslashes (they're RTF control chars).
    char fileNameEscaped[1024] = {0};
    char* w = fileNameEscaped;
    for (const char* r = fileNameUtf8;
         *r && (w - fileNameEscaped) < (int)sizeof(fileNameEscaped) - 2; ++r) {
        if (*r == '\\' || *r == '{' || *r == '}') *w++ = '\\';
        *w++ = *r;
    }
    *w = '\0';

    _snprintf_s(rtf, sizeof(rtf), _TRUNCATE,
        "{\\rtf1\\ansi\\ansicpg65001\\deff0"
        "{\\fonttbl{\\f0 Segoe UI;}{\\f1 Consolas;}}"
        "{\\colortbl;\\red0\\green102\\blue204;\\red128\\green128\\blue128;}"
        "\\fs28\\b MD Viewer \\u8212? Stage 1\\b0\\par"
        "\\fs20\\par"
        "If you see this, the WLX plugin loaded correctly.\\par"
        "TC called \\b ListLoadW\\b0  with the file you pressed F3 on:\\par"
        "\\par"
        "{\\f1\\cf2 %s}\\par"
        "\\par"
        "Next step: parse markdown via md4c and convert to RTF on the fly.\\par"
        "}", fileNameEscaped);

    LoadRtfIntoRichEdit(hRichEdit, rtf);
}

// ---- Host window -----------------------------------------------------------
// We return this HWND to TC. It owns the RichEdit and resizes it on WM_SIZE.
static LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg,
                                    WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: {
            HWND child = GetWindow(hwnd, GW_CHILD);
            if (child) {
                MoveWindow(child, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void EnsureHostClassRegistered() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = HostWndProc;
    wc.hInstance     = g_hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = HOST_CLASS;
    RegisterClassExW(&wc);
    registered = true;
}

// ============================================================================
// WLX exports
// ============================================================================

// Required: TC asks which files this plugin handles.
extern "C" __declspec(dllexport)
void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    lstrcpynA(DetectString, DETECT_STRING, maxlen);
}

// Required (Unicode variant): open file in viewer, return HWND.
extern "C" __declspec(dllexport)
HWND __stdcall ListLoadW(HWND ParentWin, wchar_t* FileToLoad, int /*ShowFlags*/) {
    EnsureHostClassRegistered();

    RECT rc;
    GetClientRect(ParentWin, &rc);

    HWND host = CreateWindowExW(
        0, HOST_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, rc.right, rc.bottom,
        ParentWin, nullptr, g_hInstance, nullptr);
    if (!host) return nullptr;

    HWND richEdit = CreateWindowExW(
        0, MSFTEDIT_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, rc.right, rc.bottom,
        host, nullptr, g_hInstance, nullptr);
    if (!richEdit) {
        DestroyWindow(host);
        return nullptr;
    }

    // Make the RichEdit look pleasant out of the box.
    SendMessageW(richEdit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
    SendMessageW(richEdit, EM_AUTOURLDETECT, AURL_ENABLEURL, 0);
    SendMessageW(richEdit, EM_SETEVENTMASK, 0, ENM_LINK);

    LoadPlaceholder(richEdit, FileToLoad);

    return host;
}

// Required (ANSI variant): TC may call either; we just forward to W version.
extern "C" __declspec(dllexport)
HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    wchar_t wpath[MAX_PATH * 2];
    MultiByteToWideChar(CP_ACP, 0, FileToLoad, -1, wpath, _countof(wpath));
    return ListLoadW(ParentWin, wpath, ShowFlags);
}

// Required: close the viewer window.
extern "C" __declspec(dllexport)
void __stdcall ListCloseWindow(HWND ListWin) {
    if (ListWin) DestroyWindow(ListWin);
}
