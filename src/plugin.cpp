// MD Viewer for Total Commander — Stage 4.5
//
// Code block rectangular shading via NBSP-padding:
//  - Each line of a code block is padded with NBSP characters to match the
//    longest line in the block, plus a fixed right margin.
//  - Combined with \highlight5 (character-level background), this gives a
//    visually rectangular fill since Consolas is strictly monospaced — every
//    glyph (including box-drawing chars) is exactly one cell wide.
//  - Plus 2 NBSPs prefix on each line for left padding inside the box.
//  - No TOM/ITextPara2 usage — that API surface isn't in current SDKs.

#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM, GET_Y_LPARAM
#include <richedit.h>
#include <richole.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tom.h>
#include <ole2.h>      // OleInitialize, IRichEditOle, REOBJECT
#include <olectl.h>    // IPicture, OleLoadPicture

// GDI+ wants min/max as functions; we already define NOMINMAX, so pull in
// std versions before the GDI+ header.
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>   // GdiplusStartup, Gdiplus::Bitmap

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <memory>
#include <string_view>
#include <vector>

#include "md4c.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "gdiplus.lib")

inline constexpr std::string_view kDetectString =
R"(EXT="MD" | EXT="MARKDOWN" | EXT="MDOWN" | EXT="MKD")";

inline constexpr uint64_t kMaxFileBytes = 50ull * 1024 * 1024;

inline constexpr LONG kPadLeft = 16;
inline constexpr LONG kPadRight = 16;
inline constexpr LONG kPadTop = 12;
inline constexpr LONG kPadBottom = 12;

inline constexpr int kListLevelIndent = 480;

// Code block visual padding (in characters of monospaced font).
inline constexpr size_t kCodeLeftPadChars = 2;
inline constexpr size_t kCodeRightPadChars = 4;

namespace {
    HINSTANCE g_hInstance = nullptr;
    HMODULE   g_richEditModule = nullptr;
    bool      g_ole_initialized = false;
    ULONG_PTR g_gdiplus_token = 0;  // 0 = not initialized
    constexpr wchar_t kHostClass[] = L"MdViewerHostV1";
    constexpr UINT_PTR kSubclassId = 0xCAFE;

    template <size_t N>
    void CopyFixed(wchar_t(&dst)[N], std::wstring_view src) {
        const size_t n = (src.size() < N - 1) ? src.size() : N - 1;
        std::wmemcpy(dst, src.data(), n);
        dst[n] = L'\0';
    }
    inline void CopyFixed(char* dst, int dst_count, std::string_view src) {
        if (dst_count <= 0) return;
        const size_t cap = static_cast<size_t>(dst_count) - 1;
        const size_t n = (src.size() < cap) ? src.size() : cap;
        std::memcpy(dst, src.data(), n);
        dst[n] = '\0';
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hInstance = static_cast<HINSTANCE>(hModule);
        DisableThreadLibraryCalls(hModule);
        // NOTE: nothing else here. LoadLibrary, OleInitialize and
        // GdiplusStartup are forbidden under loader-lock by MSDN. We
        // do them lazily in EnsurePluginInitialized() on first
        // ListLoadW call instead.
        break;
    case DLL_PROCESS_DETACH:
        // FreeLibrary in DETACH is explicitly allowed by MSDN. We
        // don't call OleUninitialize / GdiplusShutdown here because
        // they may try to load other modules during teardown.
        if (g_richEditModule) FreeLibrary(g_richEditModule);
        break;
    }
    return TRUE;
}

namespace {

    // Lazy plugin-wide initialization. Called on first ListLoadW so we never
    // run LoadLibrary / OleInitialize / GdiplusStartup under loader-lock from
    // DllMain (forbidden by MSDN — risks deadlock when those calls trigger
    // dependent DLL loads).
    //
    // All steps are independent and best-effort: failure of any one doesn't
    // prevent the others. Subsequent calls are no-ops (guarded by static).
    //
    // MUST be called on TC's UI thread (which is where ListLoadW runs). OLE
    // is per-thread, so initializing here ties our STA to that thread.
    void EnsurePluginInitialized() {
        static const bool initialized = [] {
            // RichEdit 8.5 DLL — registers MSFTEDIT_CLASS window class.
            // Idempotent if already loaded (just bumps the module refcount).
            g_richEditModule = LoadLibraryW(L"msftedit.dll");

            // OLE for image embedding (IRichEditOle, OleCreateStaticFromData).
            // STA apartment ties to this thread, which is fine — all our
            // RichEdit interactions happen on the same UI thread.
            if (SUCCEEDED(OleInitialize(nullptr))) {
                g_ole_initialized = true;
            }

            // GDI+ for decoding PNG/JPG/GIF/BMP/TIFF/ICO.
            // OleLoadPicture officially only handles BMP/ICO/WMF/EMF —
            // GDI+ is the path for modern formats. We then hand the decoded
            // HBITMAP to OleCreateStaticFromData via our custom IDataObject.
            Gdiplus::GdiplusStartupInput startup;
            Gdiplus::GdiplusStartup(&g_gdiplus_token, &startup, nullptr);

            return true;
            }();
        (void)initialized;
    }

}  // namespace

// ============================================================================
// Theme
// ============================================================================
struct ThemeColors {
    COLORREF background;
    COLORREF text;
    COLORREF link;
    COLORREF quote;
    COLORREF code_text;        // text color in code BLOCKS
    COLORREF code_bg;          // background color in code BLOCKS
    COLORREF hr_line;          // horizontal rule color
    COLORREF inline_code_text; // text color for inline `code`
};

inline constexpr ThemeColors kLightTheme{
    .background = RGB(255, 255, 255),
    .text = RGB(0,   0,   0),
    .link = RGB(0, 102, 204),
    .quote = RGB(110, 110, 110),
    .code_text = RGB(40,  40,  40),
    .code_bg = RGB(232, 232, 234),
    .hr_line = RGB(195, 195, 195),
    .inline_code_text = RGB(199,  37,  78),  // brick red, GitHub-ish
};

inline constexpr ThemeColors kDarkTheme{
    .background = RGB(30,  30,  30),
    .text = RGB(212, 212, 212),
    .link = RGB(86, 156, 255),
    .quote = RGB(160, 160, 160),
    .code_text = RGB(206, 145, 120),
    .code_bg = RGB(50,  52,  60),
    .hr_line = RGB(85,  85,  85),
    .inline_code_text = RGB(236, 118, 137),  // softer for dark theme
};

namespace {

    bool IsSystemDarkMode() {
        DWORD value = 1;
        DWORD size = sizeof(value);
        const LSTATUS s = RegGetValueW(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",
            RRF_RT_REG_DWORD,
            nullptr, &value, &size);
        return (s == ERROR_SUCCESS) && (value == 0);
    }

    const ThemeColors& GetActiveTheme() {
        return IsSystemDarkMode() ? kDarkTheme : kLightTheme;
    }

} // namespace

// ============================================================================
// File reading
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
// ============================================================================
// Image embedding plan
//
// During markdown rendering (Pass 1), every MD_SPAN_IMG span is recorded
// in `image_refs` with a unique placeholder string that's also emitted to
// the RTF body. After EM_STREAMIN (Pass 2), the image_embed namespace
// walks the placeholders, finds each one in the loaded document via
// EM_FINDTEXTEXW, loads the actual image, and replaces the placeholder
// with an embedded OLE picture.
//
// Failure modes (missing file, web URL, unsupported format, etc.) leave
// the placeholder text in the document — never a crash, never a partial
// render.
// ============================================================================
struct ImageRef {
    std::wstring placeholder;  // unique string we can find in the document
    std::wstring src;          // raw src attribute from markdown
    std::wstring alt;          // alt text (for fallback display)
    bool is_inside_link = false; // emitted inside an \field HYPERLINK — need
    // hit-test mapping to make picture clickable
    std::wstring link_url;     // URL of the enclosing link (if is_inside_link)
};

namespace md_render {

    struct ConvertResult {
        std::string rtf;
        std::vector<ImageRef> image_refs;
    };

    struct ListContext {
        bool ordered;
        int next_number;
    };

    struct RtfBuilder {
        std::string out;
        std::vector<ListContext> list_stack;
        std::vector<bool> li_block_emitted_stack;
        bool first_block_emitted = false;

        // Code block state. While in_code_block, OnText writes UTF-8 here
        // (newlines preserved). FlushCodeBlock pads each line and emits to `out`.
        bool in_code_block = false;
        std::string code_buffer;

        // Table state. RTF tables need column positions defined at start of each
        // row (\trowd + \cellx N). We capture col_count on enter MD_BLOCK_TABLE
        // and emit precomputed column positions on every TR.
        struct TableCtx {
            int col_count = 0;
            std::vector<MD_ALIGN> col_aligns;  // per-column alignment from header
            int current_col = 0;               // current cell index within row
            bool in_thead = false;
            bool capturing_aligns = false;     // true during THEAD: collect aligns
        };
        std::vector<TableCtx> table_stack;

        // Collected image references. Populated by OnEnterSpan(MD_SPAN_IMG).
        // Each entry pairs a unique placeholder string emitted into the RTF
        // body with the original src/alt — Pass 2 (image_embed) uses these
        // to locate and replace placeholders with real embedded pictures.
        std::vector<ImageRef> image_refs;

        // While inside an MD_SPAN_IMG, OnText routes text into `image_alt_buffer`
        // (for the ImageRef's `alt` field) AND to the RTF body as the
        // human-visible placeholder text. After the span closes, the ref is
        // pushed onto image_refs with a unique placeholder wrapping the alt.
        bool in_image_span = false;
        std::wstring image_src_buffer;   // raw href from MD_SPAN_IMG_DETAIL
        std::wstring image_alt_buffer;   // accumulated text inside the span

        // Stack of URLs of currently open MD_SPAN_A hyperlinks. The depth gives
        // us the link_span_depth (was a separate counter); the top URL is used
        // to tag any MD_SPAN_IMG that closes inside (so picture clicks open
        // the wrapping link via WM_LBUTTONUP hit-test in image_clicks state).
        std::vector<std::wstring> link_url_stack;

        void append_raw(std::string_view s) { out.append(s); }

        int container_indent() const {
            return static_cast<int>(list_stack.size()) * kListLevelIndent;
        }

        bool inside_li_no_block_yet() const {
            return !li_block_emitted_stack.empty()
                && !li_block_emitted_stack.back();
        }

        void mark_li_block_emitted() {
            if (!li_block_emitted_stack.empty()) {
                li_block_emitted_stack.back() = true;
            }
        }

        // Escape one wide char and write to `out` (no NBSP routing).
        void write_escaped_char(wchar_t c) {
            switch (c) {
            case L'\\': append_raw("\\\\"); return;
            case L'{':  append_raw("\\{");  return;
            case L'}':  append_raw("\\}");  return;
            }
            if (c < 0x20) return;
            if (c < 0x80) { out += static_cast<char>(c); return; }
            char buf[16];
            const int n = std::snprintf(buf, sizeof(buf), "\\u%d?",
                static_cast<int>(static_cast<int16_t>(c)));
            out.append(buf, static_cast<size_t>(n));
        }

        void emit_utf16(char16_t c) {
            if (c == u'\n') { append_raw("\\line "); return; }
            if (c == u'\r') return;
            write_escaped_char(static_cast<wchar_t>(c));
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

        // Flush buffered code block: split into lines, find max width, pad each
        // line with NBSPs to that width + right margin, prepend left margin.
        void flush_code_block() {
            // Split UTF-8 buffer on '\n' into line views.
            std::vector<std::string_view> lines;
            size_t start = 0;
            for (size_t i = 0; i <= code_buffer.size(); ++i) {
                if (i == code_buffer.size() || code_buffer[i] == '\n') {
                    lines.emplace_back(code_buffer.data() + start, i - start);
                    start = i + 1;
                }
            }
            // Trim trailing empty lines (md4c often emits a final \n).
            while (!lines.empty() && lines.back().empty()) lines.pop_back();
            if (lines.empty()) {
                code_buffer.clear();
                return;
            }

            // Convert each line to UTF-16 once; track max char count.
            std::vector<std::wstring> wlines;
            wlines.reserve(lines.size());
            size_t max_chars = 0;
            for (auto line : lines) {
                std::wstring w;
                if (!line.empty()) {
                    const int wlen = MultiByteToWideChar(CP_UTF8, 0,
                        line.data(), static_cast<int>(line.size()),
                        nullptr, 0);
                    if (wlen > 0) {
                        w.resize(static_cast<size_t>(wlen));
                        MultiByteToWideChar(CP_UTF8, 0, line.data(),
                            static_cast<int>(line.size()),
                            w.data(), wlen);
                    }
                }
                if (w.size() > max_chars) max_chars = w.size();
                wlines.push_back(std::move(w));
            }

            const size_t target = max_chars + kCodeRightPadChars;

            // Emit each line: leading spaces, content, trailing spaces, \line.
            //
            // On the very first line we prepend a Zero-Width Joiner (U+200D).
            // RichEdit strips leading whitespace at the start of a paragraph
            // (but not after \line), which was eating our 2-space left pad on
            // line 1 only — causing the first line to be visually shorter than
            // the rest. ZWJ is a non-whitespace format character with zero
            // visual width, so it anchors the paragraph start without affecting
            // alignment, and subsequent space padding is preserved.
            //
            // Known limitation: lines containing box-drawing characters
            // (U+2500..U+257F) may render with a slightly narrower right edge
            // because RichEdit's font fallback gives those glyphs different
            // width than ASCII chars even in strictly-monospaced fonts. This
            // affects ASCII-art content but not regular source code.
            for (size_t i = 0; i < wlines.size(); ++i) {
                if (i == 0) {
                    append_raw("\\u8205?");  // ZWJ anchor
                }
                for (size_t j = 0; j < kCodeLeftPadChars; ++j) {
                    append_raw("\\u32?");
                }
                for (wchar_t c : wlines[i]) {
                    write_escaped_char(c);
                }
                for (size_t j = wlines[i].size(); j < target; ++j) {
                    append_raw("\\u32?");
                }
                if (i + 1 < wlines.size()) append_raw("\\line ");
            }
            code_buffer.clear();
        }
    };

    static int OnEnterBlock(MD_BLOCKTYPE type, void* detail, void* ud) {
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
            const int sb = is_first ? 0 : sb_vals[level];
            const int li = b.container_indent();
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "\\pard\\li%d\\sb%d\\sa%d {\\fs%d\\b ",
                li, sb, sa_vals[level], sizes[level]);
            b.append_raw(buf);
            break;
        }

        case MD_BLOCK_P: {
            if (!b.table_stack.empty()) {
                // Inside a table cell — don't emit \pard, would break \intbl.
                // The cell's TH/TD handler already opened the paragraph.
                break;
            }
            if (b.inside_li_no_block_yet()) {
                b.mark_li_block_emitted();
            }
            else {
                const int li = b.container_indent();
                char buf[80];
                std::snprintf(buf, sizeof(buf),
                    "\\pard\\li%d\\sa120\\sl276\\slmult1 ", li);
                b.append_raw(buf);
            }
            break;
        }

        case MD_BLOCK_QUOTE: {
            if (b.inside_li_no_block_yet()) {
                b.mark_li_block_emitted();
                b.append_raw("\\par");
            }
            const int li = b.container_indent() + 360;
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                "\\pard\\li%d\\sa120\\sb120\\sl276\\slmult1 {\\cf3\\i ", li);
            b.append_raw(buf);
            break;
        }

        case MD_BLOCK_UL:
            if (b.inside_li_no_block_yet()) {
                b.mark_li_block_emitted();
                b.append_raw("\\par");
            }
            b.list_stack.push_back({ false, 0 });
            break;

        case MD_BLOCK_OL: {
            auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
            if (b.inside_li_no_block_yet()) {
                b.mark_li_block_emitted();
                b.append_raw("\\par");
            }
            b.list_stack.push_back({ true, static_cast<int>(d->start) });
            break;
        }

        case MD_BLOCK_LI: {
            b.li_block_emitted_stack.push_back(false);
            const int level = static_cast<int>(b.list_stack.size());
            const int text_indent = level * kListLevelIndent;
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "\\pard\\li%d\\fi-360\\tx%d\\sl276\\slmult1 ",
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

        case MD_BLOCK_HR: {
            if (b.inside_li_no_block_yet()) {
                b.mark_li_block_emitted();
                b.append_raw("\\par");
            }
            const int li = b.container_indent();
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "\\pard\\li%d\\brdrb\\brdrs\\brdrw10\\brdrcf6"
                "\\sa180\\sb180\\par ", li);
            b.append_raw(buf);
            break;
        }

        case MD_BLOCK_CODE: {
            if (b.inside_li_no_block_yet()) {
                b.mark_li_block_emitted();
                b.append_raw("\\par");
            }
            // Align code blocks with list item text (no extra indent).
            // Inside a list this gives consistent visual indent for both UL
            // (bullet) and OL (number) since both have text starting at
            // level * kListLevelIndent.
            const int li = b.container_indent();
            // Code blocks use tight line spacing (single, not 1.15).
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                "\\pard\\li%d\\sa120\\sb120\\sl240\\slmult1 "
                "{\\f1\\fs18\\cf4\\highlight5 ", li);
            b.append_raw(buf);
            b.in_code_block = true;
            b.code_buffer.clear();
            break;
        }

        case MD_BLOCK_HTML:
            break;

            // ---- Tables (MD_FLAG_TABLES) -----------------------------------
        case MD_BLOCK_TABLE: {
            if (b.inside_li_no_block_yet()) {
                b.mark_li_block_emitted();
                b.append_raw("\\par");
            }
            auto* d = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
            RtfBuilder::TableCtx ctx;
            ctx.col_count = static_cast<int>(d->col_count);
            ctx.col_aligns.assign(ctx.col_count, MD_ALIGN_DEFAULT);
            b.table_stack.push_back(std::move(ctx));
            // Small vertical spacing before the table.
            b.append_raw("\\pard\\sa120\\sb120\n");
            break;
        }
        case MD_BLOCK_THEAD: {
            if (!b.table_stack.empty()) {
                b.table_stack.back().in_thead = true;
                b.table_stack.back().capturing_aligns = true;
            }
            break;
        }
        case MD_BLOCK_TBODY: {
            if (!b.table_stack.empty()) {
                b.table_stack.back().in_thead = false;
                b.table_stack.back().capturing_aligns = false;
            }
            break;
        }
        case MD_BLOCK_TR: {
            if (b.table_stack.empty()) break;
            auto& t = b.table_stack.back();
            t.current_col = 0;
            // \trowd starts a row; \cellxN gives the right-edge x-coord of
            // each cell in twips. We distribute columns evenly across a
            // fixed total width (8000 twips ≈ 5.5 inches).
            // \trgaph108 = half-gap between cells (54 twips on each side).
            b.append_raw("\\trowd\\trgaph108\\trleft0");
            constexpr int kTableTotalWidth = 8000;
            const int per_col = (t.col_count > 0)
                ? (kTableTotalWidth / t.col_count) : kTableTotalWidth;
            for (int i = 0; i < t.col_count; ++i) {
                // Each cell gets borders on all four sides + position.
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                    "\\clbrdrt\\brdrs\\brdrw10\\brdrcf6"
                    "\\clbrdrl\\brdrs\\brdrw10\\brdrcf6"
                    "\\clbrdrb\\brdrs\\brdrw10\\brdrcf6"
                    "\\clbrdrr\\brdrs\\brdrw10\\brdrcf6"
                    "\\cellx%d", per_col * (i + 1));
                b.append_raw(buf);
            }
            b.append_raw("\n");
            break;
        }
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            if (b.table_stack.empty()) break;
            auto& t = b.table_stack.back();
            auto* d = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
            // First TH row defines per-column alignment.
            if (t.capturing_aligns && t.current_col < t.col_count) {
                t.col_aligns[t.current_col] = d->align;
            }
            // Cell paragraph. \intbl marks the paragraph as belonging to the
            // current table cell; without it RichEdit shows raw text.
            b.append_raw("\\pard\\intbl");
            // Alignment: prefer the column's header-derived align; fallback
            // to the cell's own align (some markdown variants set per-cell).
            const MD_ALIGN align = (t.current_col < t.col_count
                && t.col_aligns[t.current_col] != MD_ALIGN_DEFAULT)
                ? t.col_aligns[t.current_col]
                : d->align;
            switch (align) {
            case MD_ALIGN_CENTER: b.append_raw("\\qc"); break;
            case MD_ALIGN_RIGHT:  b.append_raw("\\qr"); break;
            default:              b.append_raw("\\ql"); break;
            }
            // Header cells get bold.
            if (type == MD_BLOCK_TH) b.append_raw("\\b");
            b.append_raw(" ");
            break;
        }

        default: break;
        }
        return 0;
    }

    static int OnLeaveBlock(MD_BLOCKTYPE type, void* /*detail*/, void* ud) {
        auto& b = *static_cast<RtfBuilder*>(ud);
        switch (type) {
        case MD_BLOCK_H:     b.append_raw("}\\par\n"); break;
        case MD_BLOCK_P:
            if (!b.table_stack.empty()) break;  // P inside cell — no \par
            b.append_raw("\\par\n");
            break;
        case MD_BLOCK_QUOTE: b.append_raw("}\\par\n"); break;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            if (!b.list_stack.empty()) b.list_stack.pop_back();
            break;
        case MD_BLOCK_LI: {
            const bool emitted = b.li_block_emitted_stack.empty()
                ? true
                : b.li_block_emitted_stack.back();
            if (!b.li_block_emitted_stack.empty()) {
                b.li_block_emitted_stack.pop_back();
            }
            if (!emitted) b.append_raw("\\par\n");
            break;
        }
        case MD_BLOCK_CODE:
            b.in_code_block = false;
            b.flush_code_block();
            b.append_raw("}\\par\n");
            break;

            // ---- Tables ---------------------------------------------------
        case MD_BLOCK_TABLE:
            if (!b.table_stack.empty()) b.table_stack.pop_back();
            b.append_raw("\\pard\\par\n");
            break;
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break;
        case MD_BLOCK_TR:
            b.append_raw("\\row\n");
            break;
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
            // \b applied above must be terminated; \cell finalizes the cell.
            // We end the bold group implicitly by switching paragraphs;
            // but to be safe in case the cell content opened spans, end
            // any leftover formatting via \plain before \cell.
            if (type == MD_BLOCK_TH) b.append_raw("\\b0");
            b.append_raw("\\cell\n");
            if (!b.table_stack.empty()) b.table_stack.back().current_col++;
            break;
        default: break;
        }
        return 0;
    }

    static int OnEnterSpan(MD_SPANTYPE type, void* detail, void* ud) {
        auto& b = *static_cast<RtfBuilder*>(ud);
        switch (type) {
        case MD_SPAN_EM:     b.append_raw("{\\i ");          break;
        case MD_SPAN_STRONG: b.append_raw("{\\b ");          break;
        case MD_SPAN_DEL:    b.append_raw("{\\strike ");     break;
        case MD_SPAN_CODE:
            // Inline code: monospaced font + brick-red color.
            // No border (RichEdit doesn't render \chbrdr) and no highlight
            // (it follows line height and visually merges on adjacent lines).
            // Font shift + accent color is enough to make inline code stand
            // out without decorations — same approach as Notion's default.
            b.append_raw("{\\f1\\cf7 ");
            break;
        case MD_SPAN_A: {
            auto* d = static_cast<MD_SPAN_A_DETAIL*>(detail);
            // Convert href to wstring for later use by nested IMG span.
            std::wstring url;
            if (d->href.size > 0) {
                const int wlen = MultiByteToWideChar(CP_UTF8, 0, d->href.text,
                    static_cast<int>(d->href.size), nullptr, 0);
                if (wlen > 0) {
                    url.resize(static_cast<size_t>(wlen));
                    MultiByteToWideChar(CP_UTF8, 0, d->href.text,
                        static_cast<int>(d->href.size),
                        url.data(), wlen);
                }
            }
            b.link_url_stack.push_back(std::move(url));
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
        case MD_SPAN_IMG: {
            auto* d = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
            // Capture src as wide string (md4c gives UTF-8).
            b.image_src_buffer.clear();
            if (d->src.size > 0) {
                const int wlen = MultiByteToWideChar(CP_UTF8, 0, d->src.text,
                    static_cast<int>(d->src.size), nullptr, 0);
                if (wlen > 0) {
                    b.image_src_buffer.resize(static_cast<size_t>(wlen));
                    MultiByteToWideChar(CP_UTF8, 0, d->src.text,
                        static_cast<int>(d->src.size),
                        b.image_src_buffer.data(), wlen);
                }
            }
            b.image_alt_buffer.clear();
            b.in_image_span = true;
            // We don't emit anything to `out` yet — placeholder is built
            // in OnLeaveSpan once we have the complete alt text.
            break;
        }
        default: break;
        }
        return 0;
    }

    static int OnLeaveSpan(MD_SPANTYPE type, void* /*detail*/, void* ud) {
        auto& b = *static_cast<RtfBuilder*>(ud);
        switch (type) {
        case MD_SPAN_EM:
        case MD_SPAN_STRONG:
        case MD_SPAN_DEL:
            b.append_raw("}");
            break;
        case MD_SPAN_CODE:
            b.append_raw("}");
            break;
        case MD_SPAN_A:
            b.append_raw("}}}");
            if (!b.link_url_stack.empty()) b.link_url_stack.pop_back();
            break;
        case MD_SPAN_IMG: {
            b.in_image_span = false;
            // Build a UNIQUE placeholder string that we can locate later
            // via EM_FINDTEXTEXW. Format:
            //   U+2063 IDX:N U+2063 [image: alt]
            // The leading/trailing U+2063 (Invisible Separator) chars are
            // anchor markers — zero-width, valid Unicode, virtually
            // guaranteed not to occur in user content. The numeric N is
            // unique per image; the [image: alt] tail is the human-visible
            // fallback text the user sees if the picture fails to load.
            const size_t idx = b.image_refs.size();
            wchar_t header[64];
            std::swprintf(header, _countof(header), L"\u2063IMG%zu\u2063", idx);

            ImageRef ref;
            ref.placeholder = std::wstring(header)
                + L"[image: "
                + (b.image_alt_buffer.empty()
                    ? std::wstring(L"image")
                    : b.image_alt_buffer)
                + L"]";
            ref.src = b.image_src_buffer;
            ref.alt = b.image_alt_buffer;
            ref.is_inside_link = !b.link_url_stack.empty();
            if (ref.is_inside_link) {
                ref.link_url = b.link_url_stack.back();
            }

            // Emit the placeholder text into RTF, styled like the old
            // [image: ...] fallback (gray italic). U+2063 needs the \uN?
            // escape; the rest goes through the normal text path.
            b.append_raw("{\\cf3\\i ");
            for (wchar_t c : ref.placeholder) {
                b.emit_utf16(static_cast<char16_t>(c));
            }
            b.append_raw("}");

            b.image_refs.push_back(std::move(ref));
            break;
        }
        default: break;
        }
        return 0;
    }

    static int OnText(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* ud) {
        auto& b = *static_cast<RtfBuilder*>(ud);
        const std::string_view s{ text, size };

        // While inside an MD_SPAN_IMG, text belongs to the alt-text and is
        // accumulated into image_alt_buffer instead of being emitted to RTF.
        // The placeholder containing this alt will be emitted in OnLeaveSpan.
        if (b.in_image_span) {
            if (type == MD_TEXT_NORMAL || type == MD_TEXT_ENTITY
                || type == MD_TEXT_CODE) {
                const int wlen = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                    static_cast<int>(s.size()), nullptr, 0);
                if (wlen > 0) {
                    const size_t old = b.image_alt_buffer.size();
                    b.image_alt_buffer.resize(old + static_cast<size_t>(wlen));
                    MultiByteToWideChar(CP_UTF8, 0, s.data(),
                        static_cast<int>(s.size()),
                        b.image_alt_buffer.data() + old, wlen);
                }
            }
            return 0;
        }

        // Inside a code block, buffer raw UTF-8 (preserve newlines for line split).
        if (b.in_code_block) {
            switch (type) {
            case MD_TEXT_BR:
            case MD_TEXT_SOFTBR:
                b.code_buffer += '\n';
                break;
            case MD_TEXT_HTML:
            case MD_TEXT_NULLCHAR:
                break;
            default:
                b.code_buffer.append(s);
                break;
            }
            return 0;
        }

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

    static std::string ColorEntry(COLORREF c) {
        return std::format("\\red{}\\green{}\\blue{};",
            GetRValue(c), GetGValue(c), GetBValue(c));
    }

    static ConvertResult ConvertMarkdownToRtf(std::string_view md_utf8,
        const ThemeColors& theme) {
        RtfBuilder b;
        b.out.reserve(md_utf8.size() * 2 + 512);

        b.append_raw(R"({\rtf1\ansi\ansicpg65001\deff0)");
        b.append_raw(R"({\fonttbl{\f0 Segoe UI;}{\f1 Cascadia Mono;}})");
        b.append_raw("{\\colortbl;");
        b.append_raw(ColorEntry(theme.text));              // 1
        b.append_raw(ColorEntry(theme.link));              // 2
        b.append_raw(ColorEntry(theme.quote));             // 3
        b.append_raw(ColorEntry(theme.code_text));         // 4
        b.append_raw(ColorEntry(theme.code_bg));           // 5
        b.append_raw(ColorEntry(theme.hr_line));           // 6
        b.append_raw(ColorEntry(theme.inline_code_text));  // 7
        b.append_raw("}");
        // Document-level defaults. Line spacing is set per-paragraph (in each
        // \pard block), not here — \pard resets paragraph properties, including
        // \sl, so a top-level \sl is silently overridden by every block.
        b.append_raw("\\fs22\\cf1 \n");

        MD_PARSER parser{};
        parser.abi_version = 0;
        parser.flags = MD_FLAG_STRIKETHROUGH
            | MD_FLAG_PERMISSIVEAUTOLINKS
            | MD_FLAG_TASKLISTS
            | MD_FLAG_HARD_SOFT_BREAKS
            | MD_FLAG_TABLES;
        parser.enter_block = OnEnterBlock;
        parser.leave_block = OnLeaveBlock;
        parser.enter_span = OnEnterSpan;
        parser.leave_span = OnLeaveSpan;
        parser.text = OnText;

        const int parse_result = md_parse(
            md_utf8.data(), static_cast<MD_SIZE>(md_utf8.size()),
            &parser, &b);
#ifndef NDEBUG
        if (parse_result != 0) {
            OutputDebugStringW(L"[mdview] md_parse returned non-zero "
                L"(parse may be incomplete)\n");
        }
#else
        (void)parse_result;
#endif

        b.append_raw("}");
        return ConvertResult{
            .rtf = std::move(b.out),
            .image_refs = std::move(b.image_refs),
        };
    }

} // namespace md_render

// ============================================================================
// Per-window state — owned by host window via GWLP_USERDATA.
// ============================================================================
//
// Holds the table of clickable images: each entry maps a document character
// position (where an OLE picture sits) to the URL of the enclosing markdown
// link. Populated by image_embed::EmbedAll after successful InsertObject.
//
// Looked up on WM_LBUTTONUP via EM_CHARFROMPOS to make picture-links work
// (text CFE_LINK doesn't apply to OLE objects, so we hit-test ourselves).

struct ImageClickTarget {
    LONG cp;            // character position of the OLE image in the document
    std::wstring url;   // URL to open on click
};

struct WindowState {
    std::vector<ImageClickTarget> image_clicks;
};

namespace {

    // Get the WindowState attached to the host window (the parent of the
    // RichEdit). Returns nullptr if not yet attached.
    WindowState* GetWindowState(HWND host) {
        return reinterpret_cast<WindowState*>(
            GetWindowLongPtrW(host, GWLP_USERDATA));
    }

    // Find a clickable image at the given document character position.
    // Checks both `cp` and `cp - 1` because EM_CHARFROMPOS returns the position
    // AFTER an OLE object when the click lands on the object itself (positions
    // in RichEdit are between chars; the object is one char wide, so a click
    // inside the object's box reports cp = object's position + 1).
    const ImageClickTarget* FindImageClickAt(const WindowState* state, LONG cp) {
        if (!state) return nullptr;
        for (const auto& t : state->image_clicks) {
            if (t.cp == cp || t.cp == cp - 1) return &t;
        }
        return nullptr;
    }

}  // namespace


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
        // Key forwarding to TC's Lister window.
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

        // Image-link clicking: text hyperlinks use EN_LINK, but CFE_LINK doesn't
        // apply to OLE objects, so picture-links need manual hit testing.
        // On WM_LBUTTONUP: check if the click hit a registered image-link
        // position; if so, ShellExecute the URL and swallow the message.
        if (msg == WM_LBUTTONUP) {
            HWND host = GetParent(hwnd);
            WindowState* state = GetWindowState(host);
            if (state && !state->image_clicks.empty()) {
                POINTL pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                const LONG cp = static_cast<LONG>(SendMessageW(
                    hwnd, EM_CHARFROMPOS, 0,
                    reinterpret_cast<LPARAM>(&pt)));
                if (const auto* target = FindImageClickAt(state, cp)) {
                    ShellExecuteW(nullptr, L"open", target->url.c_str(),
                        nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
            }
        }

        // Cursor feedback: WM_SETCURSOR fires when the mouse moves over the
        // RichEdit. If the cursor is currently over an image-link, show the
        // hand cursor instead of the default beam/arrow.
        if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
            HWND host = GetParent(hwnd);
            WindowState* state = GetWindowState(host);
            if (state && !state->image_clicks.empty()) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                POINTL ptl{ pt.x, pt.y };
                const LONG cp = static_cast<LONG>(SendMessageW(
                    hwnd, EM_CHARFROMPOS, 0,
                    reinterpret_cast<LPARAM>(&ptl)));
                if (FindImageClickAt(state, cp)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;  // we handled cursor selection
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
        // Defensive: in valid usage pos <= data.size() always (we only
        // increment by toCopy which is <= remaining), but a corrupted cookie
        // or programming error could underflow `remaining` if pos > size().
        if (c->pos >= c->data.size()) {
            *pcb = 0;
            return 0;
        }
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
#ifndef NDEBUG
        if (es.dwError != 0) {
            wchar_t buf[96];
            std::swprintf(buf, _countof(buf),
                L"[mdview] EM_STREAMIN dwError=0x%08lX (RTF parse issue)\n",
                es.dwError);
            OutputDebugStringW(buf);
        }
#endif
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

    void ApplyDefaultFontAndColor(HWND richEdit, const ThemeColors& theme) {
        CHARFORMAT2W cf{};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET | CFM_COLOR;
        cf.yHeight = 11 * 20;
        cf.bCharSet = DEFAULT_CHARSET;
        cf.crTextColor = theme.text;
        CopyFixed(cf.szFaceName, L"Segoe UI");
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
        case WM_DESTROY: {
            // Free per-window state attached via GWLP_USERDATA.
            WindowState* state = GetWindowState(hwnd);
            if (state) {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                delete state;
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
// Image embedding (OLE) — replaces [image:N] placeholders with real pictures
// ============================================================================
//
// Pass 2 of the rendering pipeline. Called after EM_STREAMIN with the list
// of image references collected by md_render during Pass 1.
//
// All functions in this namespace are FAIL-SAFE: if any step fails (file
// missing, web URL, decode error, etc.), the placeholder text is replaced
// with a clean "[image: alt]" fallback and rendering continues. Never
// throws, never crashes, never leaves partial state.
//
// Pipeline:
//   1. ResolvePath: src → absolute path (or "" for web URLs / data: URIs)
//   2. IsLoadable: exists, regular file, size ≤ 5MB, supported extension
//   3. LoadAsHBitmap: GDI+ decodes file → HBITMAP
//   4. FindAndSelectPlaceholder: EM_FINDTEXTEXW finds unique anchor in doc
//   5. InsertHBitmapAt: wraps HBITMAP in IDataObject, embeds via
//      OleCreateStaticFromData + IRichEditOle::InsertObject
//   6. After all inserts: walk IRichEditOle::GetObject to get authoritative
//      cp positions, record (cp, url) for picture-link hit testing.
//
namespace image_embed {

    // URL-decode %XX sequences in-place into a wide string.
    // Markdown src attributes are URL-encoded for safety (spaces -> %20 etc.),
    // but Windows file APIs want raw paths. Quietly skips malformed sequences.
    static std::wstring UrlDecode(std::wstring_view in) {
        std::wstring out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == L'%' && i + 2 < in.size()) {
                auto hex = [](wchar_t c) -> int {
                    if (c >= L'0' && c <= L'9') return c - L'0';
                    if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
                    if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
                    return -1;
                    };
                const int h1 = hex(in[i + 1]);
                const int h2 = hex(in[i + 2]);
                if (h1 >= 0 && h2 >= 0) {
                    // %XX is a single UTF-8 byte. We collect a run of %XX into
                    // a UTF-8 buffer and convert it to UTF-16 once.
                    std::string utf8;
                    while (i < in.size() && in[i] == L'%' && i + 2 < in.size()) {
                        const int a = hex(in[i + 1]);
                        const int b = hex(in[i + 2]);
                        if (a < 0 || b < 0) break;
                        utf8.push_back(static_cast<char>((a << 4) | b));
                        i += 3;
                    }
                    --i;  // outer loop will ++i; we've consumed up to one past last %XX
                    const int wlen = MultiByteToWideChar(CP_UTF8, 0,
                        utf8.data(), static_cast<int>(utf8.size()),
                        nullptr, 0);
                    if (wlen > 0) {
                        const size_t old = out.size();
                        out.resize(old + static_cast<size_t>(wlen));
                        MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                            static_cast<int>(utf8.size()),
                            out.data() + old, wlen);
                    }
                    continue;
                }
            }
            out.push_back(in[i]);
        }
        return out;
    }

    // True if src is a URL scheme we cannot handle (web, ftp, data:).
    // We do NOT treat file:// as resolvable to keep the implementation simple
    // — markdown rarely uses file:// URIs for local images.
    static bool IsExternalUrl(std::wstring_view src) {
        static constexpr std::wstring_view prefixes[] = {
            L"http://", L"https://", L"ftp://", L"ftps://", L"file://", L"data:",
        };
        for (auto p : prefixes) {
            if (src.size() >= p.size()) {
                // Case-insensitive prefix match.
                bool match = true;
                for (size_t i = 0; i < p.size(); ++i) {
                    wchar_t a = src[i];
                    wchar_t b = p[i];
                    if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a + 32);
                    if (a != b) { match = false; break; }
                }
                if (match) return true;
            }
        }
        return false;
    }

    // Step 3: resolve `src` relative to the markdown file's directory.
    // Returns absolute path on success, empty string on web URLs / data: URIs
    // or any other un-resolvable input.
    static std::wstring ResolvePath(const std::wstring& md_file_dir,
        const std::wstring& src) {
        if (src.empty()) return {};
        if (IsExternalUrl(src)) return {};

        // URL-decode %20 etc. before handing to Win32 file APIs.
        const std::wstring decoded = UrlDecode(src);

        // Forward slashes are common in markdown — normalize to backslashes
        // for Windows file APIs (they accept both, but GetFullPathName is
        // happier with backslashes for the result).
        std::wstring normalized;
        normalized.reserve(decoded.size());
        for (wchar_t c : decoded) {
            normalized.push_back(c == L'/' ? L'\\' : c);
        }

        // Check for absolute path: "X:\..." or "\\server\share\..."
        const bool is_drive_abs =
            normalized.size() >= 3
            && ((normalized[0] >= L'A' && normalized[0] <= L'Z') ||
                (normalized[0] >= L'a' && normalized[0] <= L'z'))
            && normalized[1] == L':'
            && normalized[2] == L'\\';
        const bool is_unc =
            normalized.size() >= 2
            && normalized[0] == L'\\' && normalized[1] == L'\\';

        std::wstring full_input = (is_drive_abs || is_unc)
            ? normalized
            : (md_file_dir + normalized);

        // GetFullPathNameW resolves "..", "." and normalizes separators.
        wchar_t resolved[MAX_PATH * 2] = {};
        const DWORD len = GetFullPathNameW(full_input.c_str(),
            _countof(resolved), resolved, nullptr);
        if (len == 0 || len >= _countof(resolved)) return {};
        return std::wstring(resolved, len);
    }

    // Max image file size we'll try to load (bytes). Larger files = placeholder.
    inline constexpr uint64_t kMaxImageBytes = 5ull * 1024 * 1024;

    // Image extensions supported by OleLoadPicture (matches our whitelist).
    static bool HasSupportedExt(std::wstring_view path) {
        static constexpr std::wstring_view exts[] = {
            L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".ico",
        };
        // Find last '.' after last separator.
        size_t dot = path.size();
        for (size_t i = path.size(); i > 0; --i) {
            const wchar_t c = path[i - 1];
            if (c == L'.') { dot = i - 1; break; }
            if (c == L'\\' || c == L'/') break;
        }
        if (dot == path.size()) return false;
        const std::wstring_view ext = path.substr(dot);
        for (auto e : exts) {
            if (ext.size() != e.size()) continue;
            bool match = true;
            for (size_t i = 0; i < e.size(); ++i) {
                wchar_t a = ext[i];
                if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a + 32);
                if (a != e[i]) { match = false; break; }
            }
            if (match) return true;
        }
        return false;
    }

    // Step 3: returns true if the path points to a file we can actually load.
    // Checks: exists, regular file, size <= kMaxImageBytes, supported extension.
    static bool IsLoadable(const std::wstring& path) {
        if (path.empty()) return false;
        if (!HasSupportedExt(path)) return false;

        WIN32_FILE_ATTRIBUTE_DATA fi{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fi)) {
            return false;
        }
        if (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return false;

        const uint64_t size = (static_cast<uint64_t>(fi.nFileSizeHigh) << 32)
            | fi.nFileSizeLow;
        if (size == 0 || size > kMaxImageBytes) return false;

        return true;
    }

    // Step 4: load a picture file into an IPicture COM object.
    // Returns nullptr on failure. Caller releases the picture.
    //
    // Implementation:
    //   1. ReadFile into a memory buffer.
    //   2. Wrap the buffer in a moveable HGLOBAL.
    //   3. CreateStreamOnHGlobal to get an IStream.
    //   4. OleLoadPicture parses the stream's contents (auto-detects PNG/JPG/
    //      BMP/GIF/ICO from magic bytes — no need to tell it the format).
    //   5. The stream takes ownership of the HGLOBAL when fDeleteOnRelease=TRUE;
    //      releasing the stream frees the buffer.
    //
    // Notes on lifetime: IPicture keeps an internal HBITMAP/HENHMETAFILE. The
    // caller owns this IPicture* and must Release() it. On Step 5 we'll pass
    // the picture to IRichEditOle::InsertObject, which retains its own ref —
    // after that we Release the local pointer.
    // Step 4 (revised): load a picture file and return an HBITMAP. Caller
    // must DeleteObject() the result when done.
    //
    // We use GDI+ for decoding (handles PNG/JPG/GIF/BMP/TIFF/ICO).
    // The HBITMAP is then handed to a custom IDataObject and embedded into
    // the RichEdit document via OleCreateStaticFromData.
    static HBITMAP LoadAsHBitmap(const std::wstring& path,
        UINT* outWidth, UINT* outHeight) {
        if (!g_gdiplus_token) return nullptr;

        Gdiplus::Bitmap bitmap(path.c_str());
        if (bitmap.GetLastStatus() != Gdiplus::Ok) return nullptr;

        const UINT w = bitmap.GetWidth();
        const UINT h = bitmap.GetHeight();
        if (outWidth)  *outWidth = w;
        if (outHeight) *outHeight = h;

        HBITMAP hbm = nullptr;
        // Background color matters only for images with alpha. White matches
        // the light theme; on dark theme transparent PNGs may look off
        // (documented limitation — we don't auto-invert images).
        const Gdiplus::Status s =
            const_cast<Gdiplus::Bitmap&>(bitmap).GetHBITMAP(
                Gdiplus::Color(255, 255, 255), &hbm);
        if (s != Gdiplus::Ok) return nullptr;
        return hbm;
    }

    // Compute available content width inside the RichEdit, in HIMETRIC units
    // (0.01 mm). Used to scale down oversized images. Returns 0 on failure.
    //
    // HIMETRIC = pixels * 2540 / DPI.
    static LONG GetAvailableWidthHimetric(HWND richEdit) {
        RECT formatRect{};
        SendMessageW(richEdit, EM_GETRECT, 0,
            reinterpret_cast<LPARAM>(&formatRect));
        const LONG width_px = formatRect.right - formatRect.left;
        if (width_px <= 0) return 0;

        HDC hdc = GetDC(richEdit);
        if (!hdc) return 0;
        const int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(richEdit, hdc);
        if (dpi <= 0) return 0;

        return MulDiv(width_px, 2540, dpi);
    }

    // Minimal IDataObject exposing a single CF_BITMAP — just enough for
    // OleCreateStaticFromData to make a static OLE picture out of an HBITMAP.
    //
    // We implement only what the call chain actually exercises:
    //   - GetData (CF_BITMAP)
    //   - QueryGetData (CF_BITMAP)
    //   - GetDataHere — stub, not called by OleCreateStaticFromData in practice
    //   - EnumFormatEtc — returns a single FORMATETC
    // Other methods return E_NOTIMPL. This is sufficient based on common
    // recipes used across RichEdit samples for inserting bitmaps.
    class BitmapDataObject : public IDataObject {
    public:
        explicit BitmapDataObject(HBITMAP bmp) : m_bitmap(bmp) {}
        virtual ~BitmapDataObject() = default;

        // IUnknown
        STDMETHODIMP QueryInterface(REFIID iid, void** ppv) override {
            if (!ppv) return E_POINTER;
            if (iid == IID_IUnknown || iid == IID_IDataObject) {
                *ppv = static_cast<IDataObject*>(this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        STDMETHODIMP_(ULONG) AddRef() override {
            return InterlockedIncrement(&m_ref);
        }
        STDMETHODIMP_(ULONG) Release() override {
            const LONG r = InterlockedDecrement(&m_ref);
            if (r == 0) delete this;
            return r;
        }

        // IDataObject
        STDMETHODIMP GetData(FORMATETC* pFE, STGMEDIUM* pMed) override {
            if (!pFE || !pMed) return E_POINTER;
            if (pFE->cfFormat != CF_BITMAP || !(pFE->tymed & TYMED_GDI)) {
                return DV_E_FORMATETC;
            }
            pMed->tymed = TYMED_GDI;
            pMed->hBitmap = m_bitmap;
            pMed->pUnkForRelease = nullptr;
            return S_OK;
        }
        STDMETHODIMP QueryGetData(FORMATETC* pFE) override {
            if (!pFE) return E_POINTER;
            return (pFE->cfFormat == CF_BITMAP && (pFE->tymed & TYMED_GDI))
                ? S_OK : DV_E_FORMATETC;
        }
        STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        STDMETHODIMP GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
        STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
        STDMETHODIMP EnumFormatEtc(DWORD dir, IEnumFORMATETC** ppEnum) override {
            if (dir != DATADIR_GET || !ppEnum) return E_NOTIMPL;
            // SHCreateStdEnumFmtEtc would be cleaner but pulls in shell32 dep.
            // RichEdit doesn't actually call this during InsertObject — we
            // return E_NOTIMPL and that's fine in practice.
            return E_NOTIMPL;
        }
        STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
        STDMETHODIMP DUnadvise(DWORD) override { return E_NOTIMPL; }
        STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override { return E_NOTIMPL; }

    private:
        LONG m_ref = 1;
        HBITMAP m_bitmap;
    };

    // Find the placeholder text in the document and select it. Returns the
    // CHARRANGE of the found text, or {-1,-1} if not found.
    static CHARRANGE FindAndSelectPlaceholder(HWND richEdit,
        const std::wstring& placeholder) {
        FINDTEXTEXW ft{};
        ft.chrg.cpMin = 0;
        ft.chrg.cpMax = -1;
        ft.lpstrText = placeholder.c_str();
        const LONG pos = static_cast<LONG>(SendMessageW(
            richEdit, EM_FINDTEXTEXW, /*FR_DOWN=*/1,
            reinterpret_cast<LPARAM>(&ft)));
        if (pos < 0) return { -1, -1 };

        // Select the found range so InsertObject replaces it.
        SendMessageW(richEdit, EM_EXSETSEL, 0,
            reinterpret_cast<LPARAM>(&ft.chrgText));
        return ft.chrgText;
    }

    // Insert an OLE static picture object made from an HBITMAP into the
    // RichEdit at the current selection. Replaces the selection content.
    // Returns true on success. On any failure, returns false and the document
    // is left unchanged (the placeholder stays visible to the user).
    //
    // Sizing: `sizeWidth`/`sizeHeight` are the desired display size in HIMETRIC
    // units. If the picture is wider than `maxWidthHimetric`, both dimensions
    // are scaled down proportionally.
    static bool InsertHBitmapAt(HWND richEdit,
        HBITMAP hbm, UINT pxW, UINT pxH,
        LONG maxWidthHimetric) {
        // Get IRichEditOle for InsertObject.
        IRichEditOle* pRichOle = nullptr;
        SendMessageW(richEdit, EM_GETOLEINTERFACE, 0,
            reinterpret_cast<LPARAM>(&pRichOle));
        if (!pRichOle) return false;

        // Wrap the HBITMAP in our minimal IDataObject.
        BitmapDataObject* pData = new BitmapDataObject(hbm);

        // Create an IStorage in memory to hold the OLE object's persisted state.
        // OleCreateStaticFromData needs a real storage to write into.
        ILockBytes* pLockBytes = nullptr;
        HRESULT hr = CreateILockBytesOnHGlobal(nullptr, TRUE, &pLockBytes);
        if (FAILED(hr) || !pLockBytes) {
            pData->Release();
            pRichOle->Release();
            return false;
        }

        IStorage* pStorage = nullptr;
        hr = StgCreateDocfileOnILockBytes(pLockBytes,
            STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_READWRITE,
            0, &pStorage);
        pLockBytes->Release();
        if (FAILED(hr) || !pStorage) {
            pData->Release();
            pRichOle->Release();
            return false;
        }

        // Get the IOleClientSite from the RichEdit.
        IOleClientSite* pClientSite = nullptr;
        if (FAILED(pRichOle->GetClientSite(&pClientSite)) || !pClientSite) {
            pStorage->Release();
            pData->Release();
            pRichOle->Release();
            return false;
        }

        // Create a static OLE object from our DataObject. Static = no live
        // application backing it, just the bitmap data. This is exactly what
        // we want for a viewer (no editing).
        //
        // OleCreateStaticFromData with OLERENDER_FORMAT requires a non-null
        // pFormatEtc telling it WHICH format to extract. We want CF_BITMAP
        // via TYMED_GDI — matches what our BitmapDataObject exposes.
        FORMATETC fmt{};
        fmt.cfFormat = CF_BITMAP;
        fmt.ptd = nullptr;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_GDI;

        IOleObject* pOleObject = nullptr;
        hr = OleCreateStaticFromData(pData, IID_IOleObject, OLERENDER_FORMAT,
            &fmt, pClientSite, pStorage,
            reinterpret_cast<void**>(&pOleObject));
        pData->Release();
        if (FAILED(hr) || !pOleObject) {
            pClientSite->Release();
            pStorage->Release();
            pRichOle->Release();
            return false;
        }

        // Build the REOBJECT for InsertObject.
        REOBJECT reo{};
        reo.cbStruct = sizeof(reo);
        reo.cp = REO_CP_SELECTION;        // insert at current selection
        reo.clsid = CLSID_NULL;
        pOleObject->GetUserClassID(&reo.clsid);
        reo.poleobj = pOleObject;
        reo.pstg = pStorage;
        reo.polesite = pClientSite;

        // Native size in HIMETRIC. 1 HIMETRIC = 0.01 mm; 1 inch = 2540 HIMETRIC.
        // At 96 DPI: 1 px = 26.46 HIMETRIC.
        LONG natWidth = MulDiv(static_cast<LONG>(pxW), 2540, 96);
        LONG natHeight = MulDiv(static_cast<LONG>(pxH), 2540, 96);

        // Scale down if wider than available width.
        if (maxWidthHimetric > 0 && natWidth > maxWidthHimetric) {
            natHeight = MulDiv(natHeight, maxWidthHimetric, natWidth);
            natWidth = maxWidthHimetric;
        }
        reo.sizel.cx = natWidth;
        reo.sizel.cy = natHeight;
        reo.dvaspect = DVASPECT_CONTENT;
        reo.dwFlags = REO_RESIZABLE;

        hr = pRichOle->InsertObject(&reo);

        pOleObject->Release();
        pClientSite->Release();
        pStorage->Release();
        pRichOle->Release();
        return SUCCEEDED(hr);
    }

    // Replace a placeholder in the document with plain `[image: alt]` text.
    // Used when an image couldn't be loaded (web URL, missing file, decode
    // failure) — strips our internal anchor markers so the user sees a clean
    // fallback instead of "⁣IMG3⁣[image: alt]".
    static void ReplacePlaceholderWithFallback(HWND richEdit,
        const std::wstring& placeholder,
        const std::wstring& alt) {
        FINDTEXTEXW ft{};
        ft.chrg.cpMin = 0;
        ft.chrg.cpMax = -1;
        ft.lpstrText = placeholder.c_str();
        const LONG pos = static_cast<LONG>(SendMessageW(
            richEdit, EM_FINDTEXTEXW, /*FR_DOWN=*/1,
            reinterpret_cast<LPARAM>(&ft)));
        if (pos < 0) return;

        SendMessageW(richEdit, EM_EXSETSEL, 0,
            reinterpret_cast<LPARAM>(&ft.chrgText));

        const std::wstring clean = L"[image: "
            + (alt.empty() ? std::wstring(L"image") : alt)
            + L"]";
        SendMessageW(richEdit, EM_REPLACESEL, FALSE,
            reinterpret_cast<LPARAM>(clean.c_str()));
    }

    // Entry point: walk image_refs, try to load and embed each one.
    // Failures are silent (clean [image: alt] fallback remains in document).
    // On success for is_inside_link refs, records (cp, url) into out_clicks
    // so the host window can hit-test mouse events later.
    //
    // Strategy notes:
    //   - We collect a list of "embedded link URLs" in insertion order during
    //     the main loop. These are URLs for picture-links that successfully
    //     embedded.
    //   - After the loop, we re-query the actual document positions of all
    //     OLE objects via IRichEditOle::GetObject. RichEdit's REOBJECT.cp is
    //     authoritative — using found.cpMin captured during insertion would
    //     be wrong because later inserts/placeholder replacements shift earlier
    //     positions.
    //   - We match recorded URLs to OLE objects by insertion order: the i-th
    //     successfully-embedded picture (counting all inserts, not just links)
    //     corresponds to the i-th REOBJECT returned by GetObject. We use a
    //     parallel array of "link URL or empty string" indexed by insert order.
    static void EmbedAll(HWND richEdit,
        const std::wstring& md_file_dir,
        const std::vector<ImageRef>& refs,
        std::vector<ImageClickTarget>& out_clicks) {
        // Skip entirely if OLE wasn't initialized (very unusual — TC always
        // uses STA on its UI thread). Without OLE, IRichEditOle::InsertObject
        // would just fail per-image; the early-return is purely a fast path.
        // Placeholders remain visible — fallback message would be nicer but
        // adds complexity we don't need for a path that practically never hits.
        if (!g_ole_initialized) return;

        const LONG maxWidthHimetric = GetAvailableWidthHimetric(richEdit);

        // URL of the i-th successfully-embedded picture, in insertion order.
        // Empty string means "not a link". After all inserts, we map index → cp
        // via IRichEditOle::GetObject and emit (cp, url) for non-empty entries.
        std::vector<std::wstring> insert_order_urls;

        for (size_t i = 0; i < refs.size(); ++i) {
            const std::wstring path = ResolvePath(md_file_dir, refs[i].src);
            if (!IsLoadable(path)) {
                ReplacePlaceholderWithFallback(richEdit, refs[i].placeholder,
                    refs[i].alt);
                continue;
            }

            UINT pxW = 0, pxH = 0;
            HBITMAP hbm = LoadAsHBitmap(path, &pxW, &pxH);
            if (!hbm) {
                ReplacePlaceholderWithFallback(richEdit, refs[i].placeholder,
                    refs[i].alt);
                continue;
            }

            const CHARRANGE found = FindAndSelectPlaceholder(richEdit,
                refs[i].placeholder);
            if (found.cpMin < 0) {
                // Placeholder not in document — unusual (could happen if RTF
                // parse truncated). The Replace call below will also no-op
                // since it uses the same finder, but kept for symmetry.
                DeleteObject(hbm);
                ReplacePlaceholderWithFallback(richEdit, refs[i].placeholder,
                    refs[i].alt);
                continue;
            }

            if (!InsertHBitmapAt(richEdit, hbm, pxW, pxH, maxWidthHimetric)) {
                CHARRANGE clear{ 0, 0 };
                SendMessageW(richEdit, EM_EXSETSEL, 0,
                    reinterpret_cast<LPARAM>(&clear));
                DeleteObject(hbm);
                ReplacePlaceholderWithFallback(richEdit, refs[i].placeholder,
                    refs[i].alt);
                continue;
            }

            // Release the original HBITMAP first — RichEdit copied it during
            // InsertObject, so the original is no longer needed. Doing this
            // BEFORE push_back (which can theoretically throw bad_alloc) avoids
            // a leak in the bad_alloc path.
            DeleteObject(hbm);

            // Successfully embedded. Record URL (or empty) at this insertion
            // index — actual cp positions are resolved at the end.
            insert_order_urls.push_back(
                refs[i].is_inside_link ? refs[i].link_url : std::wstring());
        }

        // Reset selection to start so user doesn't see the last image highlighted.
        CHARRANGE clear{ 0, 0 };
        SendMessageW(richEdit, EM_EXSETSEL, 0,
            reinterpret_cast<LPARAM>(&clear));

        // Resolve final document positions of inserted pictures via the RichEdit
        // OLE object table — these are stable and accurate regardless of how
        // other inserts shifted things during the loop.
        if (insert_order_urls.empty()) return;

        IRichEditOle* pRichOle = nullptr;
        SendMessageW(richEdit, EM_GETOLEINTERFACE, 0,
            reinterpret_cast<LPARAM>(&pRichOle));
        if (!pRichOle) return;

        const LONG count = pRichOle->GetObjectCount();
        // Walk all OLE objects in document order. RichEdit returns them sorted
        // by cp ascending, which matches our insertion order (we always find
        // placeholders front-to-back). Pair i-th object with i-th recorded URL.
        const LONG limit = (static_cast<LONG>(insert_order_urls.size()) < count)
            ? static_cast<LONG>(insert_order_urls.size()) : count;
        for (LONG i = 0; i < limit; ++i) {
            REOBJECT reo{};
            reo.cbStruct = sizeof(reo);
            if (FAILED(pRichOle->GetObject(i, &reo, REO_GETOBJ_NO_INTERFACES))) {
                continue;
            }
            if (!insert_order_urls[i].empty()) {
                out_clicks.push_back({ reo.cp, insert_order_urls[i] });
            }
        }
        pRichOle->Release();
    }

}  // namespace image_embed



extern "C" __declspec(dllexport)
void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    CopyFixed(DetectString, maxlen, kDetectString);
}

extern "C" __declspec(dllexport)
HWND __stdcall ListLoadW(HWND ParentWin, wchar_t* FileToLoad, int /*ShowFlags*/) {
    EnsurePluginInitialized();
    EnsureHostClassRegistered();

    const ThemeColors& theme = GetActiveTheme();

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

    SendMessageW(richEdit, EM_SETBKGNDCOLOR, 0, theme.background);
    SendMessageW(richEdit, EM_AUTOURLDETECT, AURL_ENABLEURL, 0);
    SendMessageW(richEdit, EM_SETEVENTMASK, 0, ENM_LINK);
    SendMessageW(richEdit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
    SendMessageW(richEdit, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);

    SetWindowSubclass(richEdit, RichEditSubclassProc, kSubclassId, 0);
    ApplyDefaultFontAndColor(richEdit, theme);

    // Everything below is wrapped in try/catch because we cross the WLX C
    // ABI back to TC — any uncaught exception there is undefined behavior.
    // On bad_alloc (huge files, OOM), we fall back to showing a plain
    // error message and return a valid host window so TC can still close
    // it through ListCloseWindow.
    try {
        auto content = ReadFileToUtf8(FileToLoad);
        if (content) {
            const md_render::ConvertResult result =
                md_render::ConvertMarkdownToRtf(*content, theme);

#ifndef NDEBUG
            // Debug: dump the generated RTF to %TEMP%\mdview-debug.rtf so we
            // can inspect what we send to RichEdit. Only in Debug builds.
            {
                wchar_t tempPath[MAX_PATH] = {};
                GetTempPathW(MAX_PATH, tempPath);
                std::wstring dumpPath = std::wstring(tempPath) + L"mdview-debug.rtf";
                HANDLE hf = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hf != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    WriteFile(hf, result.rtf.data(),
                        static_cast<DWORD>(result.rtf.size()),
                        &written, nullptr);
                    CloseHandle(hf);
                }
            }
#endif

            LoadRtf(richEdit, result.rtf);

            // Pass 2: replace image placeholders with embedded pictures.
            // WindowState is owned by unique_ptr until we successfully
            // attach it to the host via SetWindowLongPtrW — only then we
            // release(). This way exceptions during EmbedAll won't leak
            // the state.
            if (!result.image_refs.empty()) {
                wchar_t mdDir[MAX_PATH * 2] = {};
                const DWORD dirLen = GetFullPathNameW(
                    FileToLoad, _countof(mdDir), mdDir, nullptr);
                if (dirLen > 0 && dirLen < _countof(mdDir)) {
                    if (wchar_t* lastSlash = wcsrchr(mdDir, L'\\')) {
                        *(lastSlash + 1) = L'\0';
                    }
                    auto state = std::make_unique<WindowState>();
                    image_embed::EmbedAll(richEdit, mdDir, result.image_refs,
                        state->image_clicks);
                    SetWindowLongPtrW(host, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(state.release()));
                }
                // If GetFullPathNameW failed, skip image embedding entirely
                // — placeholders will remain in the document as styled
                // [image: alt] text. This is acceptable degradation.
            }
        }
        else {
            LoadPlainTextWide(richEdit, DescribeError(content.error(), FileToLoad));
        }
    }
    catch (const std::exception& /*e*/) {
        // OOM or any other std exception. Show a simple message and bail.
        // Note: even LoadPlainTextWide can throw (it std::wstring-copies);
        // use a static buffer to be exception-free here.
        static constexpr wchar_t kFallback[] =
            L"Error: out of memory or internal error while rendering file.";
        SETTEXTEX st{};
        st.flags = ST_DEFAULT;
        st.codepage = 1200;
        SendMessageW(richEdit, EM_SETTEXTEX,
            reinterpret_cast<WPARAM>(&st),
            reinterpret_cast<LPARAM>(kFallback));
    }
    catch (...) {
        // Anything else — same fallback. Don't propagate across C ABI.
        static constexpr wchar_t kFallback[] =
            L"Error: unknown exception while rendering file.";
        SETTEXTEX st{};
        st.flags = ST_DEFAULT;
        st.codepage = 1200;
        SendMessageW(richEdit, EM_SETTEXTEX,
            reinterpret_cast<WPARAM>(&st),
            reinterpret_cast<LPARAM>(kFallback));
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