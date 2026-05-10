// MD Viewer for Total Commander — Stage 4 v2
//
// Hyperlink approach: emit \field HYPERLINK in RTF (RichEdit handles
// styling and CFE_LINK automatically), then on EN_LINK click extract the
// real URL via ITextRange2::GetURL (Text Object Model interface).
//
// No manual position tracking — that was an over-engineered detour.

#include <windows.h>
#include <richedit.h>
#include <richole.h>        // IRichEditOle (separate header from richedit.h)
#include <commctrl.h>
#include <shellapi.h>
#include <tom.h>            // ITextDocument2, ITextRange2

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
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

inline constexpr std::string_view kDetectString =
R"(EXT="MD" | EXT="MARKDOWN" | EXT="MDOWN" | EXT="MKD")";

inline constexpr uint64_t kMaxFileBytes = 50ull * 1024 * 1024;

inline constexpr LONG kPadLeft = 16;
inline constexpr LONG kPadRight = 16;
inline constexpr LONG kPadTop = 12;
inline constexpr LONG kPadBottom = 12;

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
// File reading: bytes → UTF-8
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
// Markdown → RTF
// ============================================================================
namespace md_render {

    struct ListContext {
        bool ordered;
        int next_number;
    };

    struct RtfBuilder {
        std::string out;
        std::vector<ListContext> list_stack;

        // Whether we've already emitted any block-level content. Used to suppress
        // \sb (space-before) on the very first heading at the top of the document.
        bool first_block_emitted = false;

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

        const bool is_first = !b.first_block_emitted;
        if (type != MD_BLOCK_DOC) b.first_block_emitted = true;

        switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_H: {
            auto* d = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            constexpr int sizes[] = { 0, 36, 32, 28, 26, 24, 22 };
            constexpr int sb_vals[] = { 0, 360, 320, 280, 240, 240, 240 };
            constexpr int sa_vals[] = { 0, 180, 160, 140, 120, 120, 120 };
            const unsigned level = (d->level >= 1 && d->level <= 6)
                ? static_cast<unsigned>(d->level) : 1u;
            // Suppress space-before for the very first block — otherwise the
            // first heading of a document floats too far from the top edge.
            const int sb = is_first ? 0 : sb_vals[level];
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                "\\pard\\sb%d\\sa%d {\\fs%d\\b ",
                sb, sa_vals[level], sizes[level]);
            b.append_raw(buf);
            break;
        }
        case MD_BLOCK_P:
            b.append_raw("\\pard\\sa120 ");
            break;
        case MD_BLOCK_QUOTE:
            b.append_raw("\\pard\\li360\\sa120\\sb120 {\\cf3\\i ");
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
            const int text_indent = level * 720;
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                "\\pard\\li%d\\fi-360\\tx%d ",
                text_indent, text_indent);
            b.append_raw(buf);
            if (!b.list_stack.empty() && b.list_stack.back().ordered) {
                const int n = b.list_stack.back().next_number++;
                char num[16];
                std::snprintf(num, sizeof(num), "%d.\\tab ", n);
                b.append_raw(num);
            }
            else {
                b.append_raw("\\bullet\\tab ");
            }
            break;
        }
        case MD_BLOCK_HR:
            b.append_raw("\\pard\\brdrb\\brdrs\\brdrw10\\sa180\\sb180\\par ");
            break;
        case MD_BLOCK_CODE:
            b.append_raw("\\pard\\sa120\\sb120 {\\f1\\fs18\\cf4 ");
            break;
        case MD_BLOCK_HTML:
            break;
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
        case MD_BLOCK_LI:    b.append_raw("\\par\n"); break;
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
            // \field HYPERLINK syntax — RichEdit applies CFE_LINK to the
            // \fldrslt content automatically and fires EN_LINK on click.
            // We extract the real URL on click via ITextRange2::GetURL.
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
        case MD_SPAN_IMG:
            b.append_raw("{\\cf3\\i [image: ");
            break;
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
            b.append_raw("}}}");  // close \cf2\ul, fldrslt, field
            break;
        case MD_SPAN_IMG:
            b.append_raw("]}");
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
// Lister key forwarding
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
// RichEdit content loaders + helpers
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

    void ApplyEditorPadding(HWND richEdit) {
        RECT r;
        GetClientRect(richEdit, &r);
        r.left += kPadLeft;
        r.top += kPadTop;
        r.right -= kPadRight;
        r.bottom -= kPadBottom;
        if (r.right < r.left)  r.right = r.left;
        if (r.bottom < r.top)  r.bottom = r.top;
        SendMessageW(richEdit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&r));
    }

    // Open the URL of the link at the given character range.
    // Uses ITextRange2::GetURL to extract the real URL from \field HYPERLINK.
    // Falls back to the visible text for autodetected URLs (where text == URL).
    void OpenLinkAtRange(HWND richEdit, CHARRANGE chrg) {
        IRichEditOle* pOle = nullptr;
        SendMessageW(richEdit, EM_GETOLEINTERFACE, 0,
            reinterpret_cast<LPARAM>(&pOle));
        if (!pOle) return;

        ITextDocument2* pDoc = nullptr;
        HRESULT hr = pOle->QueryInterface(__uuidof(ITextDocument2),
            reinterpret_cast<void**>(&pDoc));
        pOle->Release();

        bool opened = false;

        if (SUCCEEDED(hr) && pDoc) {
            ITextRange* pRange = nullptr;
            if (SUCCEEDED(pDoc->Range(chrg.cpMin, chrg.cpMax, &pRange)) && pRange) {
                ITextRange2* pRange2 = nullptr;
                if (SUCCEEDED(pRange->QueryInterface(__uuidof(ITextRange2),
                    reinterpret_cast<void**>(&pRange2)))) {
                    BSTR url = nullptr;
                    if (SUCCEEDED(pRange2->GetURL(&url)) && url && SysStringLen(url) > 0) {
                        ShellExecuteW(nullptr, L"open", url,
                            nullptr, nullptr, SW_SHOWNORMAL);
                        opened = true;
                    }
                    if (url) SysFreeString(url);
                    pRange2->Release();
                }
                pRange->Release();
            }
            pDoc->Release();
        }

        if (opened) return;

        // Fallback: extract visible text via EM_GETTEXTRANGE and use it as URL
        // (works for autodetected URLs where text IS the URL).
        const LONG len = chrg.cpMax - chrg.cpMin;
        if (len <= 0 || len > 4096) return;
        std::vector<wchar_t> buf(static_cast<size_t>(len) + 1);
        TEXTRANGEW tr{ chrg, buf.data() };
        SendMessageW(richEdit, EM_GETTEXTRANGE, 0,
            reinterpret_cast<LPARAM>(&tr));
        if (buf[0] != L'\0') {
            ShellExecuteW(nullptr, L"open", buf.data(),
                nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

} // namespace

// ============================================================================
// Host window
// ============================================================================
namespace {

    LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept {
        switch (msg) {
        case WM_SIZE: {
            HWND child = GetWindow(hwnd, GW_CHILD);
            if (child) {
                MoveWindow(child, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
                ApplyEditorPadding(child);
            }
            return 0;
        }
        case WM_NOTIFY: {
            auto* nmh = reinterpret_cast<NMHDR*>(lp);
            if (nmh->code == EN_LINK) {
                auto* enl = reinterpret_cast<ENLINK*>(lp);
                if (enl->msg == WM_LBUTTONUP) {
                    HWND rich = GetWindow(hwnd, GW_CHILD);
                    if (rich) OpenLinkAtRange(rich, enl->chrg);
                    return 1;
                }
            }
            break;
        }
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

    ApplyEditorPadding(richEdit);
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