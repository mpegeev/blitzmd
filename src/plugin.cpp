// MD Viewer for Total Commander — Stage 1 skeleton (C++26 / MSVC 14.50)
// Creates a child window with RichEdit and loads a hardcoded RTF.
// Next stage: feed actual markdown through md4c and convert to RTF.
//
// Note: WLX exports must remain extern "C" __stdcall with C-compatible
// signatures — that's the ABI Total Commander expects. Modern C++ shows up
// only in the *internal* helpers below.

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <richedit.h>

#include <array>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>

// ---- WLX show flags (subset from listplug.h) -------------------------------
inline constexpr int lcp_wraptext    = 1;
inline constexpr int lcp_fittowindow = 2;
inline constexpr int lcp_ansi        = 4;
inline constexpr int lcp_ascii       = 8;

// ---- Detect string ---------------------------------------------------------
// TC syntax: https://www.ghisler.com/plugins.htm
inline constexpr std::string_view kDetectString =
    R"(EXT="MD" | EXT="MARKDOWN" | EXT="MDOWN" | EXT="MKD")";

// ---- DLL globals -----------------------------------------------------------
namespace {
    HINSTANCE g_hInstance      = nullptr;
    HMODULE   g_richEditModule = nullptr;
    constexpr wchar_t kHostClass[] = L"MdViewerHostV1";
}

// ---- DllMain ---------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_hInstance = static_cast<HINSTANCE>(hModule);
            DisableThreadLibraryCalls(hModule);
            // RichEdit 4.1+ lives in msftedit.dll, window class "RICHEDIT50W".
            // On Windows 11 this is RichEdit 8.5.
            g_richEditModule = LoadLibraryW(L"msftedit.dll");
            break;
        case DLL_PROCESS_DETACH:
            if (g_richEditModule) FreeLibrary(g_richEditModule);
            break;
    }
    return TRUE;
}

// ---- RTF stream-in helper --------------------------------------------------
namespace {
    struct RtfStreamCtx {
        std::span<const char> data;
        size_t pos = 0;
    };

    DWORD CALLBACK RtfStreamCallback(DWORD_PTR cookie, LPBYTE buf,
                                     LONG cb, LONG* pcb) noexcept {
        auto* c = reinterpret_cast<RtfStreamCtx*>(cookie);
        const size_t remaining = c->data.size() - c->pos;
        const size_t toCopy    = std::min<size_t>(remaining, static_cast<size_t>(cb));
        std::memcpy(buf, c->data.data() + c->pos, toCopy);
        c->pos += toCopy;
        *pcb = static_cast<LONG>(toCopy);
        return 0;
    }

    void LoadRtfIntoRichEdit(HWND hRichEdit, std::string_view rtf) {
        RtfStreamCtx ctx{ .data = rtf };
        EDITSTREAM es{};
        es.dwCookie    = reinterpret_cast<DWORD_PTR>(&ctx);
        es.pfnCallback = RtfStreamCallback;
        SendMessageW(hRichEdit, EM_STREAMIN, SF_RTF,
                     reinterpret_cast<LPARAM>(&es));
    }

    // Convert wide path to UTF-8 for embedding in RTF.
    std::string ToUtf8(std::wstring_view ws) {
        if (ws.empty()) return {};
        const int bytes = WideCharToMultiByte(
            CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
            nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()),
                            out.data(), bytes, nullptr, nullptr);
        return out;
    }

    // RTF requires backslash and braces to be escaped.
    std::string EscapeForRtf(std::string_view s) {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s) {
            if (c == '\\' || c == '{' || c == '}') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }
}

// ---- Stage 1 placeholder content -------------------------------------------
// Just shows that the plugin loaded. Replace with md4c output in Stage 3.
namespace {
    void LoadPlaceholder(HWND hRichEdit, std::wstring_view fileName) {
        const std::string escapedPath = EscapeForRtf(ToUtf8(fileName));

        // \ansicpg65001 = UTF-8 codepage in RTF (RichEdit 8.5 handles this).
        const std::string rtf = std::format(
            R"({{\rtf1\ansi\ansicpg65001\deff0)"
            R"({{\fonttbl{{\f0 Segoe UI;}}{{\f1 Consolas;}}}})"
            R"({{\colortbl;\red0\green102\blue204;\red128\green128\blue128;}})"
            R"(\fs28\b MD Viewer \u8212? Stage 1\b0\par)"
            R"(\fs20\par)"
            R"(If you see this, the WLX plugin loaded correctly.\par)"
            R"(TC called \b ListLoadW\b0  with the file you pressed F3 on:\par)"
            R"(\par)"
            R"({{\f1\cf2 {0}}}\par)"
            R"(\par)"
            R"(Built with C++26 / MSVC 14.50.\par)"
            R"(Next step: parse markdown via md4c and convert to RTF.\par)"
            R"(}})", escapedPath);

        LoadRtfIntoRichEdit(hRichEdit, rtf);
    }
}

// ---- Host window -----------------------------------------------------------
// We return this HWND to TC. It owns the RichEdit and resizes it on WM_SIZE.
namespace {
    LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg,
                                 WPARAM wp, LPARAM lp) noexcept {
        if (msg == WM_SIZE) {
            if (HWND child = GetWindow(hwnd, GW_CHILD); child) {
                MoveWindow(child, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void EnsureHostClassRegistered() {
        static const bool registered = [] {
            WNDCLASSEXW wc{};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = HostWndProc;
            wc.hInstance     = g_hInstance;
            wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = kHostClass;
            return RegisterClassExW(&wc) != 0;
        }();
        (void)registered;
    }
}

// ============================================================================
// WLX exports — must stay C-compatible. No std:: types across the boundary.
// ============================================================================

extern "C" __declspec(dllexport)
void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    lstrcpynA(DetectString, kDetectString.data(), maxlen);
}

extern "C" __declspec(dllexport)
HWND __stdcall ListLoadW(HWND ParentWin, wchar_t* FileToLoad, int /*ShowFlags*/) {
    EnsureHostClassRegistered();

    RECT rc{};
    GetClientRect(ParentWin, &rc);

    HWND host = CreateWindowExW(
        0, kHostClass, L"",
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

    SendMessageW(richEdit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
    SendMessageW(richEdit, EM_AUTOURLDETECT, AURL_ENABLEURL, 0);
    SendMessageW(richEdit, EM_SETEVENTMASK, 0, ENM_LINK);

    LoadPlaceholder(richEdit, std::wstring_view{ FileToLoad });

    return host;
}

extern "C" __declspec(dllexport)
HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    std::array<wchar_t, MAX_PATH * 2> wpath{};
    MultiByteToWideChar(CP_ACP, 0, FileToLoad, -1,
                        wpath.data(), static_cast<int>(wpath.size()));
    return ListLoadW(ParentWin, wpath.data(), ShowFlags);
}

extern "C" __declspec(dllexport)
void __stdcall ListCloseWindow(HWND ListWin) {
    if (ListWin) DestroyWindow(ListWin);
}