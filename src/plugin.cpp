// MD Viewer for Total Commander — Stage 2 (with ESC fix)
// Reads the actual file from disk, handles BOMs and UTF-8/UTF-16,
// displays raw markdown as plain text in RichEdit (monospaced).
// Forwards Lister navigation keys (ESC, N, P, F2) to TC's parent window
// so they keep working when the RichEdit has focus.

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <richedit.h>
#include <commctrl.h>     // SetWindowSubclass

#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "comctl32.lib")

// ---- Detect string ---------------------------------------------------------
inline constexpr std::string_view kDetectString =
R"(EXT="MD" | EXT="MARKDOWN" | EXT="MDOWN" | EXT="MKD")";

inline constexpr uint64_t kMaxFileBytes = 50ull * 1024 * 1024;

namespace {
    HINSTANCE g_hInstance = nullptr;
    HMODULE   g_richEditModule = nullptr;
    constexpr wchar_t kHostClass[] = L"MdViewerHostV1";
    constexpr UINT_PTR kSubclassId = 0xCAFE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hInstance = static_cast<HINSTANCE>(hModule);
        DisableThreadLibraryCalls(hModule);
        g_richEditModule = LoadLibraryW(L"msftedit.dll");
        break;
    case DLL_PROCESS_DETACH:
        if (g_richEditModule) FreeLibrary(g_richEditModule);
        break;
    }
    return TRUE;
}

// ============================================================================
// File reading + encoding detection
// ============================================================================
namespace {

    enum class ReadError { OpenFailed, TooLarge, ReadFailed, InvalidEncoding };

    std::expected<std::wstring, ReadError> DecodeBytes(std::span<const uint8_t> bytes) {
        if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
            const auto* wchars = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
            const size_t wcount = (bytes.size() - 2) / sizeof(wchar_t);
            return std::wstring(wchars, wcount);
        }
        if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
            const size_t wcount = (bytes.size() - 2) / 2;
            std::wstring out(wcount, L'\0');
            for (size_t i = 0; i < wcount; ++i) {
                out[i] = static_cast<wchar_t>(
                    (bytes[2 + i * 2] << 8) | bytes[2 + i * 2 + 1]);
            }
            return out;
        }
        size_t offset = 0;
        if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
            offset = 3;
        }
        const auto* utf8 = reinterpret_cast<const char*>(bytes.data() + offset);
        const int   utf8Size = static_cast<int>(bytes.size() - offset);
        if (utf8Size == 0) return std::wstring{};

        int wideCount = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8, utf8Size, nullptr, 0);
        if (wideCount == 0) {
            wideCount = MultiByteToWideChar(CP_ACP, 0, utf8, utf8Size, nullptr, 0);
            if (wideCount == 0) return std::unexpected(ReadError::InvalidEncoding);
            std::wstring out(static_cast<size_t>(wideCount), L'\0');
            MultiByteToWideChar(CP_ACP, 0, utf8, utf8Size, out.data(), wideCount);
            return out;
        }
        std::wstring out(static_cast<size_t>(wideCount), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            utf8, utf8Size, out.data(), wideCount);
        return out;
    }

    std::expected<std::wstring, ReadError> ReadFileToWString(const wchar_t* path) {
        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr);
        if (h == INVALID_HANDLE_VALUE) return std::unexpected(ReadError::OpenFailed);

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(h, &size) || size.QuadPart < 0) {
            CloseHandle(h);
            return std::unexpected(ReadError::ReadFailed);
        }
        if (static_cast<uint64_t>(size.QuadPart) > kMaxFileBytes) {
            CloseHandle(h);
            return std::unexpected(ReadError::TooLarge);
        }

        std::vector<uint8_t> buffer(static_cast<size_t>(size.QuadPart));
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(h, buffer.data(), static_cast<DWORD>(buffer.size()),
            &bytesRead, nullptr);
        CloseHandle(h);
        if (!ok || bytesRead != buffer.size()) {
            return std::unexpected(ReadError::ReadFailed);
        }
        return DecodeBytes(buffer);
    }

    std::wstring DescribeError(ReadError e, std::wstring_view path) {
        switch (e) {
        case ReadError::OpenFailed:      return std::format(L"Cannot open file:\n{}", path);
        case ReadError::TooLarge:        return std::format(L"File is too large (>50 MB):\n{}", path);
        case ReadError::ReadFailed:      return std::format(L"Failed to read file:\n{}", path);
        case ReadError::InvalidEncoding: return std::format(L"File encoding is not recognized:\n{}", path);
        }
        return L"Unknown error";
    }

} // namespace

// ============================================================================
// Lister key forwarding — RichEdit subclass
// ============================================================================
namespace {

    // Lister navigation keys that TC handles in the parent window.
    // When focus is inside our RichEdit, RichEdit eats these — so we forward
    // them up to the Lister parent ourselves.
    //
    // Keys we keep for RichEdit (do NOT forward): Ctrl+C copy, Ctrl+A select all,
    // Ctrl+End/Ctrl+Home, arrows, PgUp/PgDn — all standard text-navigation.
    bool IsListerKey(WPARAM vk) noexcept {
        switch (vk) {
        case VK_ESCAPE:    // close lister
        case VK_F2:        // reload
        case 'N':          // next file
        case 'P':          // previous file
            return true;
        default:
            return false;
        }
    }

    LRESULT CALLBACK RichEditSubclassProc(HWND hwnd, UINT msg,
        WPARAM wp, LPARAM lp,
        UINT_PTR /*uIdSubclass*/,
        DWORD_PTR /*dwRefData*/) {
        if (msg == WM_KEYDOWN && IsListerKey(wp)) {
            // Don't forward letter keys if a modifier is held — user is typing
            // a real shortcut (Ctrl+N, etc), not a Lister navigation key.
            const bool isLetter = (wp == 'N' || wp == 'P');
            const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (isLetter && (ctrlDown || altDown)) {
                return DefSubclassProc(hwnd, msg, wp, lp);
            }

            // Walk up the parent chain to find TC's Lister window:
            //   hwnd (RichEdit) -> host -> Lister (TC)
            HWND host = GetParent(hwnd);
            HWND lister = host ? GetParent(host) : nullptr;
            if (lister) {
                PostMessageW(lister, msg, wp, lp);
                return 0; // we handled it; don't let RichEdit beep
            }
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

} // namespace

// ============================================================================
// RichEdit setup
// ============================================================================
namespace {

    void ApplyMonospacedFont(HWND hRichEdit) {
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET;
        cf.yHeight = 11 * 20;
        cf.bCharSet = DEFAULT_CHARSET;
        lstrcpynW(cf.szFaceName, L"Cascadia Code", LF_FACESIZE);
        SendMessageW(hRichEdit, EM_SETCHARFORMAT, SCF_ALL,
            reinterpret_cast<LPARAM>(&cf));
    }

    void SetPlainText(HWND hRichEdit, std::wstring_view text) {
        SETTEXTEX st{};
        st.flags = ST_DEFAULT;
        st.codepage = 1200; // UTF-16 LE
        std::wstring zterminated(text);
        SendMessageW(hRichEdit, EM_SETTEXTEX,
            reinterpret_cast<WPARAM>(&st),
            reinterpret_cast<LPARAM>(zterminated.c_str()));
    }

} // namespace

// ============================================================================
// Host window
// ============================================================================
namespace {

    LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
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
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = HostWndProc;
            wc.hInstance = g_hInstance;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.lpszClassName = kHostClass;
            return RegisterClassExW(&wc) != 0;
            }();
        (void)registered;
    }

} // namespace

// ============================================================================
// WLX exports
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
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
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
    SendMessageW(richEdit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);

    // Forward Lister navigation keys (ESC, F2, N, P) to TC's parent window.
    // Auto-cleaned up when RichEdit is destroyed.
    SetWindowSubclass(richEdit, RichEditSubclassProc, kSubclassId, 0);

    ApplyMonospacedFont(richEdit);

    auto content = ReadFileToWString(FileToLoad);
    if (content) {
        SetPlainText(richEdit, *content);
    }
    else {
        SetPlainText(richEdit, DescribeError(content.error(), FileToLoad));
    }

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