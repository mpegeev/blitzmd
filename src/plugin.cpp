// MD Viewer for Total Commander — Stage 3
// Parses markdown via md4c and streams RTF into RichEdit.
// Supported: H1-H6, paragraphs, **bold**, *italic*, ~~strike~~, `code`,
// ```code blocks```, [links], > quotes, lists (ul/ol, nested), --- HRs.
//
// UNICODE / _UNICODE / WIN32_LEAN_AND_MEAN / NOMINMAX are defined via CMake
// (target_compile_definitions in CMakeLists.txt), не дублируем здесь.

#include <windows.h>
#include <richedit.h>
#include <commctrl.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "md4c.h"

#pragma comment(lib, "comctl32.lib")

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
// File reading: bytes → UTF-8 (md4c expects UTF-8)
// ============================================================================
namespace {

    enum class ReadError { OpenFailed, TooLarge, ReadFailed, InvalidEncoding };

    std::string WideToUtf8(std::wstring_view w) {
        if (w.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(),
            static_cast<int>(w.size()),
            nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
            out.data(), n, nullptr, nullptr);
        return out;
    }

    std::expected<std::string, ReadError> DecodeBytesToUtf8(std::span<const uint8_t> bytes) {
        if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
            const auto* w = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
            const size_t wcount = (bytes.size() - 2) / sizeof(wchar_t);
            return WideToUtf8(std::wstring_view{ w, wcount });
        }
        if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
            const size_t wcount = (bytes.size() - 2) / 2;
            std::wstring tmp(wcount, L'\0');
            for (size_t i = 0; i < wcount; ++i) {
                tmp[i] = static_cast<wchar_t>(
                    (bytes[2 + i * 2] << 8) | bytes[2 + i * 2 + 1]);
            }
            return WideToUtf8(tmp);
        }
        size_t offset = 0;
        if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
            offset = 3;
        }
        std::string utf8(reinterpret_cast<const char*>(bytes.data() + offset),
            bytes.size() - offset);

        if (!utf8.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            utf8.data(), static_cast<int>(utf8.size()),
            nullptr, 0) == 0) {
            const int wn = MultiByteToWideChar(CP_ACP, 0,
                utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
            if (wn == 0) return std::unexpected(ReadError::InvalidEncoding);
            std::wstring w(static_cast<size_t>(wn), L'\0');
            MultiByteToWideChar(CP_ACP, 0, utf8.data(),
                static_cast<int>(utf8.size()), w.data(), wn);
            return WideToUtf8(w);
        }
        return utf8;
    }

    std::expected<std::string, ReadError> ReadFileToUtf8(const wchar_t* path) {
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
        return DecodeBytesToUtf8(buffer);
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
// Markdown → RTF conversion via md4c callbacks
// ============================================================================
namespace md_render {

    struct ListContext {
        bool ordered;
        int next_number;
    };

    struct RtfBuilder {
        std::string out;
        std::vector<ListContext> list_stack;

        void append_raw(std::string_view s) { out.append(s); }

        void emit_utf16(char16_t c) {
            switch (c) {
            case u'\\': append_raw("\\\\"); return;
            case u'{':  append_raw("\\{");  return;
            case u'}':  append_raw("\\}");  return;
            case u'\n': append_raw("\\line "); return;
            case u'\r': return;
            }
            if (c < 0x20) return;
            if (c < 0x80) { out += static_cast<char>(c); return; }
            char buf[16];
            const int n = std::snprintf(buf, sizeof(buf), "\\u%d?",
                static_cast<int>(static_cast<int16_t>(c)));
            out.append(buf, static_cast<size_t>(n));
        }

        void append_inline(std::string_view utf8) {
            if (utf8.empty()) return;
            const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                static_cast<int>(utf8.size()), nullptr, 0);
            if (wlen <= 0) return;
            std::wstring w(static_cast<size_t>(wlen), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                static_cast<int>(utf8.size()), w.data(), wlen);
            for (wchar_t c : w) emit_utf16(static_cast<char16_t>(c));
        }
    };

    int OnEnterBlock(MD_BLOCKTYPE type, void* detail, void* ud) {
        auto& b = *static_cast<RtfBuilder*>(ud);
        switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_H: {
            auto* d = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            constexpr int sizes[] = { 0, 36, 32, 28, 26, 24, 22 };
            const unsigned level = (d->level >= 1 && d->level <= 6)
                ? static_cast<unsigned>(d->level) : 1u;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "{\\fs%d\\b\\sa180 ", sizes[level]);
            b.append_raw(buf);
            break;
        }
        case MD_BLOCK_P: break;
        case MD_BLOCK_QUOTE:
            b.append_raw("{\\li360\\cf3\\i ");
            break;
        case MD_BLOCK_UL:
            b.list_stack.push_back({ false, 0 });
            break;
        case MD_BLOCK_OL: {
            auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
            b.list_stack.push_back({ true, static_cast<int>(d->start) });
            break;
        }
        case MD_BLOCK_LI: {
            const int level = static_cast<int>(b.list_stack.size());
            const int indent = level * 360;
            char buf[80];
            if (!b.list_stack.empty() && b.list_stack.back().ordered) {
                const int n = b.list_stack.back().next_number++;
                std::snprintf(buf, sizeof(buf),
                    "{\\li%d\\fi-360 %d.\\tab ", indent, n);
            }
            else {
                std::snprintf(buf, sizeof(buf),
                    "{\\li%d\\fi-180\\bullet\\tab ", indent);
            }
            b.append_raw(buf);
            break;
        }
        case MD_BLOCK_HR:
            b.append_raw("{\\pard\\brdrb\\brdrs\\brdrw10\\par}");
            break;
        case MD_BLOCK_CODE:
            b.append_raw("{\\f1\\fs18\\cf4\\sa60\\sb60 ");
            break;
        case MD_BLOCK_HTML:    break;
        default: break;
        }
        return 0;
    }

    int OnLeaveBlock(MD_BLOCKTYPE type, void* /*detail*/, void* ud) {
        auto& b = *static_cast<RtfBuilder*>(ud);
        switch (type) {
        case MD_BLOCK_H:     b.append_raw("}\\par\n"); break;
        case MD_BLOCK_P:     b.append_raw("\\par\n");  break;
        case MD_BLOCK_QUOTE: b.append_raw("}\\par\n"); break;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            if (!b.list_stack.empty()) b.list_stack.pop_back();
            break;
        case MD_BLOCK_LI:    b.append_raw("\\par}\n"); break;
        case MD_BLOCK_CODE:  b.append_raw("}\\par\n"); break;
        default: break;
        }
        return 0;
    }

    int OnEnterSpan(MD_SPANTYPE type, void* detail, void* ud) {
        auto& b = *static_cast<RtfBuilder*>(ud);
        switch (type) {
        case MD_SPAN_EM:     b.append_raw("{\\i ");          break;
        case MD_SPAN_STRONG: b.append_raw("{\\b ");          break;
        case MD_SPAN_DEL:    b.append_raw("{\\strike ");     break;
        case MD_SPAN_CODE:   b.append_raw("{\\f1\\cf4 ");    break;
        case MD_SPAN_A: {
            auto* d = static_cast<MD_SPAN_A_DETAIL*>(detail);
            b.append_raw("{\\field{\\*\\fldinst HYPERLINK \"");
            for (MD_SIZE i = 0; i < d->href.size; ++i) {
                const char c = d->href.text[i];
                if (c == '\\' || c == '{' || c == '}' || c == '"') {
                    b.out += '\\';
                }
                b.out += c;
            }
            b.append_raw("\"}{\\fldrslt{\\cf2\\ul ");
            break;
        }
        default: break;
        }
        return 0;
    }

    int OnLeaveSpan(MD_SPANTYPE type, void* /*detail*/, void* ud) {
        auto& b = *static_cast<RtfBuilder*>(ud);
        switch (type) {
        case MD_SPAN_EM:
        case MD_SPAN_STRONG:
        case MD_SPAN_DEL:
        case MD_SPAN_CODE:
            b.append_raw("}");
            break;
        case MD_SPAN_A:
            b.append_raw("}}}");
            break;
        default: break;
        }
        return 0;
    }

    int OnText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* ud) {
        auto& b = *static_cast<RtfBuilder*>(ud);
        const std::string_view s{ text, size };
        switch (type) {
        case MD_TEXT_NORMAL:
        case MD_TEXT_ENTITY:
        case MD_TEXT_CODE:
            b.append_inline(s);
            break;
        case MD_TEXT_BR:       b.append_raw("\\line "); break;
        case MD_TEXT_SOFTBR:   b.append_raw(" "); break;
        case MD_TEXT_HTML:     break;
        case MD_TEXT_NULLCHAR: b.append_raw("?"); break;
        default: b.append_inline(s); break;
        }
        return 0;
    }

    std::string ConvertMarkdownToRtf(std::string_view md_utf8) {
        RtfBuilder b;
        b.out.reserve(md_utf8.size() * 2 + 256);

        b.append_raw(R"({\rtf1\ansi\ansicpg65001\deff0)");
        b.append_raw(R"({\fonttbl{\f0 Segoe UI;}{\f1 Cascadia Code;}})");
        b.append_raw(R"({\colortbl;\red0\green102\blue204;\red0\green102\blue204;)"
            R"(\red128\green128\blue128;\red40\green40\blue40;})");
        b.append_raw("\\fs22 \n");

        MD_PARSER parser{};
        parser.abi_version = 0;
        parser.flags = MD_FLAG_STRIKETHROUGH
            | MD_FLAG_PERMISSIVEAUTOLINKS
            | MD_FLAG_TASKLISTS;
        parser.enter_block = OnEnterBlock;
        parser.leave_block = OnLeaveBlock;
        parser.enter_span = OnEnterSpan;
        parser.leave_span = OnLeaveSpan;
        parser.text = OnText;

        md_parse(md_utf8.data(), static_cast<MD_SIZE>(md_utf8.size()),
            &parser, &b);

        b.append_raw("}");
        return std::move(b.out);
    }

} // namespace md_render

// ============================================================================
// Lister key forwarding (subclass)
// ============================================================================
namespace {

    bool IsListerKey(WPARAM vk) noexcept {
        switch (vk) {
        case VK_ESCAPE: case VK_F2: case 'N': case 'P':
            return true;
        default:
            return false;
        }
    }

    LRESULT CALLBACK RichEditSubclassProc(HWND hwnd, UINT msg,
        WPARAM wp, LPARAM lp,
        UINT_PTR, DWORD_PTR) {
        if (msg == WM_KEYDOWN && IsListerKey(wp)) {
            const bool isLetter = (wp == 'N' || wp == 'P');
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            if (!(isLetter && (ctrl || alt))) {
                HWND host = GetParent(hwnd);
                HWND lister = host ? GetParent(host) : nullptr;
                if (lister) {
                    PostMessageW(lister, msg, wp, lp);
                    return 0;
                }
            }
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

} // namespace

// ============================================================================
// RichEdit content loaders
// ============================================================================
namespace {

    struct RtfStreamCtx {
        std::span<const char> data;
        size_t pos = 0;
    };

    DWORD CALLBACK RtfStreamCallback(DWORD_PTR cookie, LPBYTE buf,
        LONG cb, LONG* pcb) noexcept {
        auto* c = reinterpret_cast<RtfStreamCtx*>(cookie);
        const size_t remaining = c->data.size() - c->pos;
        const size_t toCopy = (static_cast<size_t>(cb) < remaining)
            ? static_cast<size_t>(cb) : remaining;
        std::memcpy(buf, c->data.data() + c->pos, toCopy);
        c->pos += toCopy;
        *pcb = static_cast<LONG>(toCopy);
        return 0;
    }

    void LoadRtf(HWND richEdit, std::string_view rtf) {
        RtfStreamCtx ctx{ .data = std::span<const char>(rtf.data(), rtf.size()) };
        EDITSTREAM es{};
        es.dwCookie = reinterpret_cast<DWORD_PTR>(&ctx);
        es.pfnCallback = RtfStreamCallback;
        SendMessageW(richEdit, EM_STREAMIN, SF_RTF,
            reinterpret_cast<LPARAM>(&es));
    }

    void LoadPlainTextWide(HWND richEdit, std::wstring_view text) {
        SETTEXTEX st{};
        st.flags = ST_DEFAULT;
        st.codepage = 1200;
        std::wstring z(text);
        SendMessageW(richEdit, EM_SETTEXTEX,
            reinterpret_cast<WPARAM>(&st),
            reinterpret_cast<LPARAM>(z.c_str()));
    }

    void ApplyDefaultFont(HWND richEdit) {
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET;
        cf.yHeight = 11 * 20;
        cf.bCharSet = DEFAULT_CHARSET;
        lstrcpynW(cf.szFaceName, L"Segoe UI", LF_FACESIZE);
        SendMessageW(richEdit, EM_SETCHARFORMAT, SCF_DEFAULT,
            reinterpret_cast<LPARAM>(&cf));
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
    SendMessageW(richEdit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
    SendMessageW(richEdit, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);

    SetWindowSubclass(richEdit, RichEditSubclassProc, kSubclassId, 0);

    ApplyDefaultFont(richEdit);

    auto content = ReadFileToUtf8(FileToLoad);
    if (content) {
        const std::string rtf = md_render::ConvertMarkdownToRtf(*content);
        LoadRtf(richEdit, rtf);
    }
    else {
        LoadPlainTextWide(richEdit, DescribeError(content.error(), FileToLoad));
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