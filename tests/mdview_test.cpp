// mdview_test — developer tool for the mdview Lister plugin.
//
//   mdview_test spec <spec.txt> [-v] [-n N]     run CommonMark/GFM spec examples
//   mdview_test dump <file.md>                  print the parsed document as HTML
//   mdview_test render <file.md> <out.png> [--width W] [--scale S] [--dark]
//                      [--zoom Z] [--scroll Y] [--height H] [--remote] [--nowrap]
//                                               render a document (region) to PNG
//   mdview_test bench <file.md> [--width W]     parse + layout timing
#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "core/texmath.h"
#include "core/offscreen.h"
#include "core/parser.h"
#include "core/print.h"
#include "core/util.h"
#include "html_dump.h"
#include "spec_runner.h"

#pragma comment(lib, "psapi.lib")

using namespace mdv;

namespace {

int Usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  mdview_test spec <spec.txt> [-v] [-n N]\n"
                 "  mdview_test dump <file.md>\n"
                 "  mdview_test render <file.md> <out.png> [--width W] [--scale S] [--dark] [--zoom Z]\n"
                 "              [--scroll Y] [--height H] [--remote] [--nowrap]\n"
                 "  mdview_test bench <file.md> [--width W]\n");
    return 2;
}

bool LoadMarkdown(const char* path, std::string& utf8) {
    std::string bytes;
    if (!ReadFileBytes(Utf8ToWide(path).c_str(), bytes, 256u << 20)) {
        std::fprintf(stderr, "cannot read %s\n", path);
        return false;
    }
    utf8 = DecodeTextToUtf8(bytes);
    return true;
}

std::wstring FullPath(const char* p) {
    const std::wstring w = Utf8ToWide(p);
    wchar_t buf[2048];
    const DWORD n = GetFullPathNameW(w.c_str(), 2048, buf, nullptr);
    return n > 0 && n < 2048 ? std::wstring(buf, n) : w;
}

double WorkingSetMb() {
    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
    return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc < 3) return Usage();
    const std::string cmd = argv[1];
    if (cmd == "spec") {
        bool verbose = false;
        int only = 0;
        for (int i = 3; i < argc; ++i) {
            if (!std::strcmp(argv[i], "-v")) verbose = true;
            else if (!std::strcmp(argv[i], "-n") && i + 1 < argc) only = std::atoi(argv[++i]);
        }
        return test::RunSpec(argv[2], verbose, only);
    }
    if (cmd == "dump") {
        std::string md;
        if (!LoadMarkdown(argv[2], md)) return 1;
        auto doc = ParseMarkdown(md, FullPath(argv[2]));
        std::fputs(test::DumpHtml(*doc).c_str(), stdout);
        return 0;
    }
    if (cmd == "render" && argc >= 4) {
        OffscreenOptions o;
        o.width = 900.f;
        for (int i = 4; i < argc; ++i) {
            const std::string a = argv[i];
            auto next = [&](float def) { return i + 1 < argc ? static_cast<float>(std::atof(argv[++i])) : def; };
            if (a == "--width") o.width = next(900.f);
            else if (a == "--scale") o.scale = next(1.f);
            else if (a == "--dark") o.layout.dark = true;
            else if (a == "--zoom") o.layout.zoom = next(1.f);
            else if (a == "--scroll") o.scroll_y = next(0.f);
            else if (a == "--height") o.max_height = next(16000.f);
            else if (a == "--remote") o.allow_remote_images = true;
            else if (a == "--nowrap") o.layout.wrap_code = false;
            else if (a == "--maxwidth") o.layout.max_content_width = next(900.f);
        }
        std::string md;
        if (!LoadMarkdown(argv[2], md)) return 1;
        const double t0 = NowMs();
        auto doc = ParseMarkdown(md, FullPath(argv[2]));
        const double t1 = NowMs();
        OffscreenStats stats;
        ComPtr<IWICBitmap> bmp = RenderToBitmap(*doc, o, &stats);
        if (!bmp) {
            std::fprintf(stderr, "render failed\n");
            return 1;
        }
        if (!SaveBitmapPng(bmp.Get(), Utf8ToWide(argv[3]))) {
            std::fprintf(stderr, "cannot write %s\n", argv[3]);
            return 1;
        }
        std::printf("parse %.1f ms, layout %.1f ms, render %.1f ms, doc height %.0f DIP\n", t1 - t0,
                    stats.layout_ms, stats.render_ms, stats.doc_height);
        return 0;
    }
    if (cmd == "snap") {
        // snap <out.png> [window class] : captures a top-level window (default:
        // Total Commander's Lister) including Direct2D content.
        const std::wstring cls = argc >= 4 ? Utf8ToWide(argv[3]) : L"TLister";
        HWND hwnd = FindWindowW(cls.c_str(), nullptr);
        if (!hwnd) {
            std::fprintf(stderr, "window not found\n");
            return 1;
        }
        RECT rc{};
        GetWindowRect(hwnd, &rc);
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(mem, dib);
        PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT);
        SelectObject(mem, old);
        ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        ComPtr<IWICBitmap> bmp;
        bool ok = false;
        if (wic && SUCCEEDED(wic->CreateBitmapFromHBITMAP(dib, nullptr, WICBitmapIgnoreAlpha, &bmp))) {
            ok = SaveBitmapPng(bmp.Get(), Utf8ToWide(argv[2]));
        }
        DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        std::printf("%s %dx%d\n", ok ? "saved" : "failed", w, h);
        return ok ? 0 : 1;
    }
    if (cmd == "post") {
        // post <class> <msg> <wparam> [lparam]: posts a message to a window
        // found by class (e.g. to drive the viewer: WM_KEYDOWN=256).
        HWND hwnd = FindWindowExW(nullptr, nullptr, Utf8ToWide(argv[2]).c_str(), nullptr);
        if (!hwnd) {
            // Child window: search inside the Lister.
            HWND lister = FindWindowW(L"TLister", nullptr);
            if (lister) hwnd = FindWindowExW(lister, nullptr, Utf8ToWide(argv[2]).c_str(), nullptr);
        }
        if (!hwnd || argc < 5) {
            std::fprintf(stderr, "window not found\n");
            return 1;
        }
        const UINT msg = static_cast<UINT>(std::strtoul(argv[3], nullptr, 0));
        const WPARAM wp = static_cast<WPARAM>(std::strtoull(argv[4], nullptr, 0));
        const LPARAM lp = argc >= 6 ? static_cast<LPARAM>(std::strtoll(argv[5], nullptr, 0)) : 0;
        if (argc >= 7 && !std::strcmp(argv[6], "--async")) PostMessageW(hwnd, msg, wp, lp);
        else SendMessageW(hwnd, msg, wp, lp);
        return 0;
    }
    if (cmd == "plugin" && argc >= 5) {
        // plugin <mdview.wlx64> <file.md> <out.png> [search text] [--dark]
        // Drives the real plugin DLL like Total Commander: ListSetDefaultParams,
        // ListLoadW, ListSearchTextW, ListSendCommand, ListGetPreviewBitmapW.
        HMODULE dll = LoadLibraryW(Utf8ToWide(argv[2]).c_str());
        if (!dll) {
            std::fprintf(stderr, "cannot load plugin\n");
            return 1;
        }
        struct DefaultParams {
            int size;
            DWORD lo, hi;
            char ini[MAX_PATH];
        };
        using SetParamsFn = void(__stdcall*)(DefaultParams*);
        using LoadFn = HWND(__stdcall*)(HWND, const wchar_t*, int);
        using SearchFn = int(__stdcall*)(HWND, const wchar_t*, int);
        using CommandFn = int(__stdcall*)(HWND, int, int);
        using CloseFn = void(__stdcall*)(HWND);
        using PreviewFn = HBITMAP(__stdcall*)(const wchar_t*, int, int, char*, int);
        auto set_params = reinterpret_cast<SetParamsFn>(GetProcAddress(dll, "ListSetDefaultParams"));
        auto load = reinterpret_cast<LoadFn>(GetProcAddress(dll, "ListLoadW"));
        auto search = reinterpret_cast<SearchFn>(GetProcAddress(dll, "ListSearchTextW"));
        auto command = reinterpret_cast<CommandFn>(GetProcAddress(dll, "ListSendCommand"));
        auto close = reinterpret_cast<CloseFn>(GetProcAddress(dll, "ListCloseWindow"));
        auto preview = reinterpret_cast<PreviewFn>(GetProcAddress(dll, "ListGetPreviewBitmapW"));
        if (!set_params || !load || !search || !command || !close || !preview) {
            std::fprintf(stderr, "missing exports\n");
            return 1;
        }
        bool dark = false;
        const char* query = nullptr;
        int hover_x = -1, hover_y = -1;
        for (int i = 5; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--dark")) dark = true;
            else if (!std::strcmp(argv[i], "--hover") && i + 2 < argc) {
                hover_x = std::atoi(argv[++i]);
                hover_y = std::atoi(argv[++i]);
            } else query = argv[i];
        }
        DefaultParams dp{sizeof(DefaultParams), 13, 2, {}};
        const std::string ini = WideToUtf8(DirectoryOf(FullPath(argv[4]))) + "lsplugin.ini";
        strncpy_s(dp.ini, ini.c_str(), _TRUNCATE);
        set_params(&dp);

        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MdViewTestHost";
        RegisterClassW(&wc);
        HWND host = CreateWindowExW(0, wc.lpszClassName, L"mdview test host", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                    40, 40, 1000, 800, nullptr, nullptr, wc.hInstance, nullptr);
        auto pump = [](int ms) {
            const double end = NowMs() + ms;
            MSG msg;
            while (NowMs() < end) {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                Sleep(5);
            }
        };
        pump(100);
        const double m0 = WorkingSetMb();
        const double t0 = NowMs();
        HWND viewer = load(host, FullPath(argv[3]).c_str(), dark ? 128 : 0);
        const double t1 = NowMs();
        if (!viewer) {
            std::fprintf(stderr, "ListLoadW failed\n");
            return 1;
        }
        UpdateWindow(viewer);
        const double t2 = NowMs();
        std::printf("ListLoadW %.1f ms, first paint %.1f ms, working set +%.1f MB\n", t1 - t0, t2 - t1,
                    WorkingSetMb() - m0);
        pump(700);
        {
            // Scrolling cost: 30 x PageDown with a synchronous repaint each.
            const double s0 = NowMs();
            for (int i = 0; i < 30; ++i) {
                SendMessageW(viewer, WM_KEYDOWN, VK_NEXT, 0);
                RedrawWindow(viewer, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            }
            const double s1 = NowMs();
            std::printf("scroll: %.2f ms per page (incl. present)\n", (s1 - s0) / 30.0);
            SendMessageW(viewer, WM_KEYDOWN, VK_HOME, 0);
            pump(300);
        }
        if (query) {
            const int r = search(viewer, Utf8ToWide(query).c_str(), 1);
            std::printf("ListSearchTextW(\"%s\") = %d\n", query, r);
            const int r2 = search(viewer, Utf8ToWide(query).c_str(), 0);
            std::printf("find next = %d\n", r2);
            pump(300);
        }
        std::printf("ListSendCommand(lc_selectall) = %d, lc_setpercent(0) = %d\n", command(viewer, 3, 0),
                    command(viewer, 4, 0));
        if (query) search(viewer, Utf8ToWide(query).c_str(), 1);
        pump(300);
        if (hover_x >= 0) {
            // Hover: deliver the move and repaint synchronously before the
            // WM_MOUSELEAVE queued by TrackMouseEvent is processed.
            SendMessageW(viewer, WM_MOUSEMOVE, 0, MAKELPARAM(hover_x, hover_y));
            RedrawWindow(viewer, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        }
        // Capture the host window (including Direct2D content).
        RECT rc{};
        GetClientRect(host, &rc);
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        HBITMAP bmp = CreateCompatibleBitmap(screen, rc.right, rc.bottom);
        HGDIOBJ old = SelectObject(mem, bmp);
        PrintWindow(host, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
        SelectObject(mem, old);
        ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        ComPtr<IWICBitmap> wbmp;
        if (wic && SUCCEEDED(wic->CreateBitmapFromHBITMAP(bmp, nullptr, WICBitmapIgnoreAlpha, &wbmp))) {
            SaveBitmapPng(wbmp.Get(), Utf8ToWide(argv[4]));
        }
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        close(viewer);
        DestroyWindow(host);
        // Thumbnail.
        const double t3 = NowMs();
        HBITMAP thumb = preview(FullPath(argv[3]).c_str(), 256, 256, nullptr, 0);
        const double t4 = NowMs();
        if (thumb) {
            BITMAP bm{};
            GetObjectW(thumb, sizeof bm, &bm);
            std::printf("ListGetPreviewBitmapW: %ldx%ld in %.1f ms\n", bm.bmWidth, bm.bmHeight, t4 - t3);
            ComPtr<IWICBitmap> tb;
            if (wic && SUCCEEDED(wic->CreateBitmapFromHBITMAP(thumb, nullptr, WICBitmapIgnoreAlpha, &tb))) {
                std::string tp = argv[4];
                tp.insert(tp.size() - 4, "-thumb");
                SaveBitmapPng(tb.Get(), Utf8ToWide(tp));
            }
            DeleteObject(thumb);
        } else {
            std::printf("ListGetPreviewBitmapW failed\n");
        }
        FreeLibrary(dll);
        std::printf("plugin OK\n");
        return 0;
    }
    if (cmd == "text") {
        // text <file.md>: the plain text "Select all + Copy" puts on the clipboard.
        std::string md;
        if (!LoadMarkdown(argv[2], md)) return 1;
        auto doc = ParseMarkdown(md, FullPath(argv[2]));
        std::fputs(WideToUtf8(doc->PlainText(0, doc->text_length)).c_str(), stdout);
        return 0;
    }
    if (cmd == "clip") {
        // clip get: prints the Unicode text on the clipboard.
        if (!OpenClipboard(nullptr)) {
            std::printf("cannot open clipboard (%lu)\n", GetLastError());
            return 1;
        }
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            if (const auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
                std::printf("%s\n", WideToUtf8(p).c_str());
                GlobalUnlock(h);
            }
        } else {
            std::printf("(no text on the clipboard)\n");
        }
        CloseClipboard();
        return 0;
    }
    if (cmd == "pages" && argc >= 4) {
        // pages <file.md> <out-prefix>: paginates like printing (A4, 15 mm
        // margins, 96 dpi) and writes one PNG per page.
        std::string md;
        if (!LoadMarkdown(argv[2], md)) return 1;
        auto doc = ParseMarkdown(md, FullPath(argv[2]));
        LayoutConfig cfg;
        cfg.zoom = 0.85f;
        cfg.max_content_width = 0;
        cfg.page_padding = 0;
        const float width = (210.f - 30.f) / 25.4f * 96.f;
        const float page_h = (297.f - 30.f) / 25.4f * 96.f - 32.f;
        PageRenderer pr;
        if (!pr.Init(*doc, cfg, width, 96.f, static_cast<UINT>(page_h) + 1)) return 1;
        const auto pages = pr.Paginate(page_h);
        std::printf("%zu pages\n", pages.size());
        for (size_t p = 0; p < pages.size() && p < 50; ++p) {
            IWICBitmap* bmp = pr.RenderBand(pages[p].first, page_h, pages[p].second);
            const std::string out = std::string(argv[3]) + std::to_string(p + 1) + ".png";
            SaveBitmapPng(bmp, Utf8ToWide(out));
        }
        return 0;
    }
    if (cmd == "tex") {
        // tex "<TeX source>": shows the Unicode conversion used for math.
        std::vector<MathPiece> pieces;
        TexToUnicode(Utf8ToWide(argv[2]), true, pieces);
        for (const auto& p : pieces) {
            std::string t = WideToUtf8(p.text);
            for (char& c : t) {
                if (c == '\n') c = '|';
            }
            std::printf("[%s%s]%s\n", (p.flags & kSup) ? "sup" : "", (p.flags & kSub) ? "sub" : "", t.c_str());
        }
        return 0;
    }
    if (cmd == "fuzz") {
        // fuzz <iterations> [seed]: random markdown built from hostile
        // snippets through parse + layout + render at random widths.
        const int iterations = std::atoi(argv[2]);
        uint32_t seed = argc >= 4 ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10)) : 12345u;
        auto rnd = [&seed]() {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return seed;
        };
        static const char* kPieces[] = {
            "# ", "## ", "###### ", "\n", "\n\n", "  \n", "> ", ">> ", "- ", "* ", "+ ", "1. ", "2) ",
            "- [ ] ", "- [x] ", "    ", "\t", "```", "```cpp\n", "~~~", "`", "``", "*", "**", "_", "__", "~",
            "~~", "[", "]", "(", ")", "![", "](", "<", ">", "</", "<div align=\"center\">", "</div>",
            "<details>", "<summary>", "</summary>", "</details>", "<table>", "<tr>", "<td>", "</td>",
            "</tr>", "</table>", "<th colspan=\"3\">", "<img src=\"x.png\" width=\"50%\">",
            "<img src=\"http://example.invalid/a.svg\">", "<br>", "<hr>", "<p align=\"right\">", "</p>",
            "<kbd>", "</kbd>", "<sup>", "</sup>", "<sub>", "<pre>", "</pre>", "<a href=\"#x\">", "</a>",
            "<a name=\"x\">", "<picture><source media=\"(prefers-color-scheme: dark)\" srcset=\"d.png\">",
            "</picture>", "<!--", "-->", "<script>", "</script>", "&amp;", "&#x1F600;", "&#0;", "&bogus;",
            "|", "| a | b |\n", "|---|---|\n", "|:-:|--:|\n", "---\n", "***\n", "===\n", "[^1]", "[^1]: note\n",
            "> [!NOTE]\n", "> [!WARNING]\n", ":rocket:", ":+1:", "$x^2$", "$$\\sum$$", "www.example.com",
            "https://example.com/a_(b)", "foo@bar.com", "\\", "\\*", "word ", "longwordlongwordlongword",
            "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 ", "\xE6\x97\xA5\xE6\x9C\xAC ",
            "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 ", "\xF0\x9F\x9A\x80", "\xE2\x80\xA8", "\xEF\xBF\xBC",
            "\xC3", "\xFF", "\xED\xA0\x80", "\r\n", "\r", "[link]: /url\n", "[link]", "<https://a.b>",
        };
        const size_t n_pieces = sizeof(kPieces) / sizeof(kPieces[0]);
        double total = 0;
        for (int it = 0; it < iterations; ++it) {
            std::string md;
            const int len = static_cast<int>(rnd() % 300);
            for (int k = 0; k < len; ++k) md += kPieces[rnd() % n_pieces];
            const double t0 = NowMs();
            auto doc = ParseMarkdown(md, L"C:\\fuzz\\doc.md");
            OffscreenOptions o;
            o.width = static_cast<float>(40 + rnd() % 1400);
            o.max_height = 2000.f;
            o.scroll_y = static_cast<float>(rnd() % 400);
            o.layout.zoom = 0.5f + static_cast<float>(rnd() % 20) / 10.f;
            o.layout.dark = (rnd() & 1) != 0;
            o.layout.wrap_code = (rnd() & 1) != 0;
            o.layout.max_content_width = (rnd() & 1) ? 0.f : 900.f;
            ComPtr<IWICBitmap> bmp = RenderToBitmap(*doc, o);
            total += NowMs() - t0;
            if (!bmp) {
                std::printf("iteration %d: render failed\n", it);
            }
            if ((it + 1) % 200 == 0) std::printf("%d iterations, avg %.2f ms\n", it + 1, total / (it + 1));
        }
        std::printf("fuzz OK: %d iterations\n", iterations);
        return 0;
    }
    if (cmd == "bench") {
        std::string md;
        if (!LoadMarkdown(argv[2], md)) return 1;
        float width = 900.f;
        for (int i = 3; i + 1 < argc; ++i) {
            if (!std::strcmp(argv[i], "--width")) width = static_cast<float>(std::atof(argv[i + 1]));
        }
        const double m0 = WorkingSetMb();
        const double t0 = NowMs();
        auto doc = ParseMarkdown(md, FullPath(argv[2]));
        const double t1 = NowMs();
        std::printf("input %.2f MB, %zu leaves, %zu blocks\n", md.size() / 1048576.0, doc->leaves.size(),
                    doc->storage.size());
        std::printf("parse: %.1f ms (working set +%.1f MB)\n", t1 - t0, WorkingSetMb() - m0);
        OffscreenOptions o;
        o.width = width;
        o.max_height = 1200.f;
        OffscreenStats stats;
        const double t2 = NowMs();
        ComPtr<IWICBitmap> bmp = RenderToBitmap(*doc, o, &stats);
        const double t3 = NowMs();
        std::printf("first screen: layout %.1f ms, render %.1f ms, total %.1f ms, doc height %.0f\n",
                    stats.layout_ms, stats.render_ms, t3 - t2, stats.doc_height);
        std::printf("working set: %.1f MB\n", WorkingSetMb());
        return 0;
    }
    return Usage();
}
