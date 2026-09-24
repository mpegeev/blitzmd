// mdview_test — developer tool for the mdview Lister plugin.
//
//   mdview_test spec <spec.txt> [-v] [-n N] [--strict]
//                                               run CommonMark/GFM spec examples
//                                               (--strict: every mismatch fails)
//   mdview_test dump <file.md>                  print the parsed document as HTML
//   mdview_test render <file.md> <out.png> [--width W] [--scale S] [--dark]
//                      [--zoom Z] [--scroll Y] [--height H] [--remote] [--nowrap]
//                                               render a document (region) to PNG
//   mdview_test bench <file.md> [--width W]     parse + layout timing
//   mdview_test settings <dll> <file.md> <out-prefix>
//                                               settings dialog: live preview, Cancel, OK
#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <shellapi.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/texmath.h"
#include "core/flags.h"
#include "core/graphlayout.h"
#include "core/images.h"
#include "core/layout.h"
#include "core/mathlayout.h"
#include "core/mermaid.h"
#include "core/theme.h"
#include "core/offscreen.h"
#include "core/parser.h"
#include "core/print.h"
#include "core/svg.h"
#include "core/util.h"
#include "html_dump.h"
#include "spec_runner.h"

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace mdv;

namespace {

int Usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  mdview_test spec <spec.txt> [-v] [-n N] [--strict]\n"
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

// Captures a window (Direct2D content and the dark title bar included) to PNG
// and returns the color of the pixel at (x, y) of the capture.
COLORREF CaptureWindow(HWND hwnd, const std::string& path, bool client_only, int x = 8, int y = 8) {
    RECT rc{};
    if (client_only) GetClientRect(hwnd, &rc);
    else GetWindowRect(hwnd, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT | (client_only ? PW_CLIENTONLY : 0));
    const COLORREF pixel = GetPixel(mem, x, y);
    SelectObject(mem, old);
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> wbmp;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateBitmapFromHBITMAP(bmp, nullptr, WICBitmapIgnoreAlpha, &wbmp))) {
        SaveBitmapPng(wbmp.Get(), Utf8ToWide(path));
    }
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return pixel;
}

bool IsDarkPixel(COLORREF c) { return GetRValue(c) + GetGValue(c) + GetBValue(c) < 3 * 80; }

double WorkingSetMb() {
    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
    return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
}

// Records where a text layout puts its inline objects (nothing is drawn).
class ObjectRecorder final : public IDWriteTextRenderer {
public:
    struct Placed {
        float y = 0;  // top of the object's box
        DWRITE_INLINE_OBJECT_METRICS metrics{};
        DWRITE_OVERHANG_METRICS overhang{};
    };
    std::vector<Placed> placed;

    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** p) override {
        *p = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* disabled) override {
        *disabled = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* m) override {
        *m = DWRITE_MATRIX{1, 0, 0, 1, 0, 0};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* p) override {
        *p = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT, FLOAT, DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN*,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT y, IDWriteInlineObject* obj, BOOL, BOOL,
                                               IUnknown*) override {
        Placed p;
        p.y = y;
        obj->GetMetrics(&p.metrics);
        obj->GetOverhangMetrics(&p.overhang);
        placed.push_back(p);
        return S_OK;
    }
};

}  // namespace

int main(int argc, char** ansi_argv) {
    // Arguments as UTF-8 (the ANSI ones lose emoji and most scripts).
    std::vector<std::string> args;
    std::vector<char*> utf8_argv;
    int wargc = 0;
    if (LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc)) {
        for (int i = 0; i < wargc; ++i) args.push_back(WideToUtf8(wargv[i]));
        LocalFree(wargv);
    }
    for (std::string& a : args) utf8_argv.push_back(a.data());
    utf8_argv.push_back(nullptr);
    char** argv = wargc == argc ? utf8_argv.data() : ansi_argv;
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc < 3) return Usage();
    const std::string cmd = argv[1];
    if (cmd == "spec") {
        bool verbose = false, strict = false;
        int only = 0;
        for (int i = 3; i < argc; ++i) {
            if (!std::strcmp(argv[i], "-v")) verbose = true;
            else if (!std::strcmp(argv[i], "--strict")) strict = true;
            else if (!std::strcmp(argv[i], "-n") && i + 1 < argc) only = std::atoi(argv[++i]);
        }
        return test::RunSpec(argv[2], verbose, only, strict);
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
            else if (a == "--lines") o.layout.line_numbers = true;
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
        // A "0x..." argument is a window handle.
        const std::wstring cls = argc >= 4 ? Utf8ToWide(argv[3]) : L"TLister";
        HWND hwnd = cls.starts_with(L"0x") ? reinterpret_cast<HWND>(static_cast<uintptr_t>(std::wcstoull(cls.c_str(), nullptr, 16)))
                                           : FindWindowW(cls.c_str(), nullptr);
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
        // plugin <mdview.wlx64> <file.md> <out.png> [search text] [--dark] [--hover X Y] [--idle]
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
        bool dark = false, idle = false;
        const char* query = nullptr;
        int hover_x = -1, hover_y = -1;
        for (int i = 5; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--dark")) dark = true;
            else if (!std::strcmp(argv[i], "--idle")) idle = true;
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
            pump(500);
            std::printf("scroll: %.2f ms per page (incl. present), working set +%.1f MB after\n", (s1 - s0) / 30.0,
                        WorkingSetMb() - m0);
            SendMessageW(viewer, WM_KEYDOWN, VK_HOME, 0);
            pump(300);
        }
        ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        // Captures the host window (including Direct2D content).
        auto capture = [&](const std::string& path) {
            RECT rc{};
            GetClientRect(host, &rc);
            HDC screen = GetDC(nullptr);
            HDC mem = CreateCompatibleDC(screen);
            HBITMAP bmp = CreateCompatibleBitmap(screen, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            PrintWindow(host, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
            SelectObject(mem, old);
            ComPtr<IWICBitmap> wbmp;
            if (wic && SUCCEEDED(wic->CreateBitmapFromHBITMAP(bmp, nullptr, WICBitmapIgnoreAlpha, &wbmp))) {
                SaveBitmapPng(wbmp.Get(), Utf8ToWide(path));
            }
            DeleteObject(bmp);
            DeleteDC(mem);
            ReleaseDC(nullptr, screen);
        };
        auto suffixed = [&](const char* suffix) {
            std::string tp = argv[4];
            tp.insert(tp.size() - 4, suffix);
            return tp;
        };
        if (idle) {
            // CPU spent while nothing happens except (visible) animations.
            auto cpu_ms = [] {
                FILETIME c, e, k, u;
                GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
                auto ms = [](const FILETIME& f) {
                    return (static_cast<double>(f.dwHighDateTime) * 4294967296.0 + f.dwLowDateTime) / 10000.0;
                };
                return ms(k) + ms(u);
            };
            capture(suffixed("-idle0"));
            const double c0 = cpu_ms();
            pump(3000);
            const double c1 = cpu_ms();
            capture(suffixed("-idle1"));
            for (int i = 2; i < 5; ++i) {
                pump(110);
                capture(suffixed(("-idle" + std::to_string(i)).c_str()));
            }
            std::printf("idle at top: %.1f ms CPU in 3 s\n", c1 - c0);
            SendMessageW(viewer, WM_KEYDOWN, VK_END, 0);
            pump(300);
            const double r0 = NowMs();
            for (int i = 0; i < 20; ++i) RedrawWindow(viewer, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            std::printf("full repaint: %.2f ms\n", (NowMs() - r0) / 20);
            const double c2 = cpu_ms();
            pump(3000);
            std::printf("idle at end: %.1f ms CPU in 3 s\n", cpu_ms() - c2);
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
        capture(argv[4]);
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
                SaveBitmapPng(tb.Get(), Utf8ToWide(suffixed("-thumb")));
            }
            DeleteObject(thumb);
        } else {
            std::printf("ListGetPreviewBitmapW failed\n");
        }
        FreeLibrary(dll);
        std::printf("plugin OK\n");
        return 0;
    }
    if (cmd == "zoom" && argc >= 5) {
        // zoom <mdview.wlx64> <file.md> <out-prefix>: zooms the first image of
        // the document the way the mouse does (Ctrl+wheel, drag, double click,
        // its buttons, the full-window view) and checks what is on screen.
        HMODULE dll = LoadLibraryW(Utf8ToWide(argv[2]).c_str());
        using SetParamsFn = void(__stdcall*)(void*);
        using LoadFn = HWND(__stdcall*)(HWND, const wchar_t*, int);
        using CloseFn = void(__stdcall*)(HWND);
        auto set_params = dll ? reinterpret_cast<SetParamsFn>(GetProcAddress(dll, "ListSetDefaultParams")) : nullptr;
        auto load = dll ? reinterpret_cast<LoadFn>(GetProcAddress(dll, "ListLoadW")) : nullptr;
        auto close = dll ? reinterpret_cast<CloseFn>(GetProcAddress(dll, "ListCloseWindow")) : nullptr;
        if (!set_params || !load || !close) {
            std::fprintf(stderr, "cannot load plugin\n");
            return 1;
        }
        const std::string prefix = argv[4];
        const std::wstring dir = DirectoryOf(FullPath(argv[4]));
        CreateDirectoryW(dir.c_str(), nullptr);
        DeleteFileW((dir + L"mdview.ini").c_str());
        struct {
            int size;
            DWORD lo, hi;
            char ini[MAX_PATH];
        } dp{static_cast<int>(sizeof(dp)), 13, 2, {}};
        strncpy_s(dp.ini, WideToUtf8(dir + L"lsplugin.ini").c_str(), _TRUNCATE);
        set_params(&dp);
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MdViewZoomHost";
        RegisterClassW(&wc);
        HWND host = CreateWindowExW(0, wc.lpszClassName, L"zoom test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 1000,
                                    760, nullptr, nullptr, wc.hInstance, nullptr);
        HWND viewer = load(host, FullPath(argv[3]).c_str(), 0);
        if (!viewer) {
            std::fprintf(stderr, "ListLoadW failed\n");
            return 1;
        }
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
        pump(600);  // images decode asynchronously
        struct Shot {
            int w = 0, h = 0;
            std::vector<uint32_t> px;
            uint32_t at(int x, int y) const { return px[static_cast<size_t>(y) * w + x] & 0xFFFFFF; }
        };
        ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        auto grab = [&](const char* name) {
            RedrawWindow(viewer, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            // The capture shows what DWM composed: wait for the new frame.
            DwmFlush();
            DwmFlush();
            Sleep(30);
            RECT rc{};
            GetClientRect(viewer, &rc);
            Shot s;
            s.w = rc.right;
            s.h = rc.bottom;
            HDC screen = GetDC(nullptr);
            HDC mem = CreateCompatibleDC(screen);
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = s.w;
            bi.bmiHeader.biHeight = -s.h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            void* bits = nullptr;
            HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            HGDIOBJ old = SelectObject(mem, dib);
            PrintWindow(viewer, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
            SelectObject(mem, old);
            s.px.assign(static_cast<const uint32_t*>(bits), static_cast<const uint32_t*>(bits) + s.w * s.h);
            ComPtr<IWICBitmap> bmp;
            if (wic && SUCCEEDED(wic->CreateBitmapFromHBITMAP(dib, nullptr, WICBitmapIgnoreAlpha, &bmp))) {
                SaveBitmapPng(bmp.Get(), Utf8ToWide(prefix + name + ".png"));
            }
            DeleteObject(dib);
            DeleteDC(mem);
            ReleaseDC(nullptr, screen);
            return s;
        };
        int failures = 0;
        auto expect = [&failures](bool ok, const char* what) {
            std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
            if (!ok) ++failures;
        };
        // Pixels that differ between two shots inside [x0,x1)x[y0,y1).
        auto diff = [](const Shot& a, const Shot& b, int x0, int y0, int x1, int y1) {
            int n = 0;
            for (int y = std::max(y0, 0); y < std::min({y1, a.h, b.h}); ++y) {
                for (int x = std::max(x0, 0); x < std::min({x1, a.w, b.w}); ++x) n += a.at(x, y) != b.at(x, y);
            }
            return n;
        };
        auto send = [&](UINT msg, WPARAM wp, int x, int y) { SendMessageW(viewer, msg, wp, MAKELPARAM(x, y)); };
        auto away = [&] { send(WM_MOUSEMOVE, 0, 2, 2); };

        const Shot s0 = grab("0-initial");
        // The image: the first rows that aren't page background (the document
        // starts with it), left of the scrollbar.
        const uint32_t bg = s0.at(s0.w / 2, 2);
        int l = s0.w, t = -1, r = 0, b = 0;
        for (int y = 0; y < s0.h; ++y) {
            bool any = false;
            for (int x = 0; x < s0.w - 30; ++x) {
                if (s0.at(x, y) == bg) continue;
                any = true;
                l = std::min(l, x);
                r = std::max(r, x + 1);
            }
            if (any && t < 0) t = y;
            if (!any && t >= 0) {
                b = y;
                break;
            }
        }
        std::printf("image at %d,%d..%d,%d\n", l, t, r, b);
        expect(r - l > 100 && b - t > 100, "the image is on screen");
        const int cx = (l + r) / 2, cy = (t + b) / 2;
        const float scale = static_cast<float>(GetDpiForWindow(viewer)) / 96.f;

        send(WM_MOUSEMOVE, 0, cx, cy);
        const Shot s1 = grab("1-hover");
        const int bar = static_cast<int>(60 * scale);  // the buttons, top right
        expect(diff(s0, s1, r - static_cast<int>(150 * scale), t, r, t + bar) > 200, "hover shows the zoom buttons");
        expect(diff(s0, s1, l, t + bar, r, b) == 0, "hover changes nothing else");

        POINT sp{cx, cy};
        ClientToScreen(viewer, &sp);
        SendMessageW(viewer, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA * 3), MAKELPARAM(sp.x, sp.y));
        away();
        const Shot s2 = grab("2-ctrl-wheel");
        expect(diff(s0, s2, l, t, r, b) > (r - l) * (b - t) / 4, "Ctrl+wheel zooms the image");
        expect(diff(s0, s2, 0, 0, s0.w, t - 2) == 0 && diff(s0, s2, 0, b + 2, s0.w - 30, s0.h) <= 4,
               "... inside its box: the rest of the page stays");

        send(WM_LBUTTONDOWN, MK_LBUTTON, cx, cy);
        send(WM_MOUSEMOVE, MK_LBUTTON, cx - 120, cy - 80);
        send(WM_LBUTTONUP, 0, cx - 120, cy - 80);
        away();
        const Shot s3 = grab("3-dragged");
        expect(diff(s2, s3, l, t, r, b) > (r - l) * (b - t) / 4, "dragging moves the zoomed image");
        expect(diff(s0, s3, 0, 0, s0.w, t - 2) == 0, "... and doesn't select or scroll");

        send(WM_LBUTTONDOWN, MK_LBUTTON, cx, cy);
        send(WM_LBUTTONUP, 0, cx, cy);
        send(WM_LBUTTONDBLCLK, MK_LBUTTON, cx, cy);
        send(WM_LBUTTONUP, 0, cx, cy);
        away();
        const Shot s4 = grab("4-double-click");
        expect(diff(s0, s4, 0, 0, s0.w - 30, s0.h) == 0, "double click restores the image");
        {
            int dl = s0.w, dt = s0.h, dr = 0, db = 0, n = 0;
            for (int y = 0; y < s0.h; ++y) {
                for (int x = 0; x < s0.w - 30; ++x) {
                    if (s0.at(x, y) == s4.at(x, y)) continue;
                    ++n;
                    dl = std::min(dl, x);
                    dt = std::min(dt, y);
                    dr = std::max(dr, x);
                    db = std::max(db, y);
                }
            }
            if (n) std::printf("  %d pixels differ in %d,%d..%d,%d\n", n, dl, dt, dr, db);
        }

        // Expand: the last of the buttons.
        send(WM_MOUSEMOVE, 0, cx, cy);
        grab("5-hover");
        const int em = static_cast<int>(16 * scale);
        const int bx = r - em / 2 - em * 7 / 8, by = t + em / 2 + em * 7 / 8;
        send(WM_MOUSEMOVE, 0, bx, by);
        send(WM_LBUTTONDOWN, MK_LBUTTON, bx, by);
        send(WM_LBUTTONUP, 0, bx, by);
        // The second click of a double click on the button lands beside the
        // image in the view: it must stay open.
        send(WM_LBUTTONDBLCLK, MK_LBUTTON, bx, by);
        send(WM_LBUTTONUP, 0, bx, by);
        send(WM_MOUSEMOVE, 0, s0.w / 2, s0.h - 8);
        const Shot s6 = grab("6-full-window");
        expect(diff(s0, s6, 0, 0, s0.w, s0.h) > s0.w * s0.h / 10, "the expand button opens the full-window view");
        const uint32_t corner = s6.at(4, s6.h - 4);
        expect(corner == bg, "... on the page background (and a double click doesn't close it)");
        {
            // Its buttons, top right of the window: the hovered one lights up
            // and stays so through repaints.
            const int fx = s6.w - em / 2 - em * 7 / 8, fy = em / 2 + em * 7 / 8;
            send(WM_MOUSEMOVE, 0, fx - em * 4, fy);  // two buttons left of x: zoom in (1.75 + 0.25 em apart)
            grab("6b-hover");
            const Shot h2 = grab("6c-hover-again");
            expect(diff(s6, h2, s6.w - em * 9, 0, s6.w, em * 3) > 20, "a hovered button of the view stays lit");
        }

        POINT fc{s6.w / 2, s6.h / 2};
        ClientToScreen(viewer, &fc);
        SendMessageW(viewer, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA * 4), MAKELPARAM(fc.x, fc.y));
        const Shot s7 = grab("7-full-wheel");
        expect(diff(s6, s7, 0, 0, s6.w, s6.h) > s6.w * s6.h / 10, "the wheel zooms the full-window view");

        // Posted, like a key press: through the message loop (Total Commander's
        // Lister would take Esc there, so the viewer hooks it first).
        PostMessageW(viewer, WM_KEYDOWN, VK_ESCAPE, 0x00010001);
        pump(100);
        away();
        const Shot s8 = grab("8-closed");
        expect(diff(s0, s8, 0, 0, s0.w - 30, s0.h) == 0, "Esc closes it: the page is back as it was");

        // Ctrl+wheel beside the image still zooms the document.
        POINT tp{l / 2 + 4, b + static_cast<int>(40 * scale)};
        ClientToScreen(viewer, &tp);
        SendMessageW(viewer, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), MAKELPARAM(tp.x, tp.y));
        const Shot s9 = grab("9-page-zoom");
        expect(diff(s0, s9, 0, 0, s0.w - 30, s0.h) > s0.w * s0.h / 20, "Ctrl+wheel beside it zooms the page");
        // A new gesture: Ctrl+wheel down over the image at 1x zooms the page
        // (back to 100%), not the image.
        Sleep(600);
        POINT ip{cx, cy};
        ClientToScreen(viewer, &ip);
        SendMessageW(viewer, WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, static_cast<WORD>(-WHEEL_DELTA)), MAKELPARAM(ip.x, ip.y));
        away();
        const Shot s10 = grab("10-page-zoom-back");
        expect(diff(s0, s10, 0, 0, s0.w - 30, s0.h - static_cast<int>(60 * scale)) == 0,
               "Ctrl+wheel down over an unzoomed image zooms the page out");

        close(viewer);
        DestroyWindow(host);
        FreeLibrary(dll);
        std::printf(failures ? "zoom FAILED\n" : "zoom OK\n");
        return failures ? 1 : 0;
    }
    if (cmd == "settings" && argc >= 5) {
        // settings <mdview.wlx64> <file.md> <out-prefix>: opens the settings
        // dialog the way the context menu does, captures it, changes options
        // (live preview in the viewer), cancels, then changes and saves them.
        HMODULE dll = LoadLibraryW(Utf8ToWide(argv[2]).c_str());
        using SetParamsFn = void(__stdcall*)(void*);
        using LoadFn = HWND(__stdcall*)(HWND, const wchar_t*, int);
        using CloseFn = void(__stdcall*)(HWND);
        auto set_params = dll ? reinterpret_cast<SetParamsFn>(GetProcAddress(dll, "ListSetDefaultParams")) : nullptr;
        auto load = dll ? reinterpret_cast<LoadFn>(GetProcAddress(dll, "ListLoadW")) : nullptr;
        auto close = dll ? reinterpret_cast<CloseFn>(GetProcAddress(dll, "ListCloseWindow")) : nullptr;
        if (!set_params || !load || !close) {
            std::fprintf(stderr, "cannot load plugin\n");
            return 1;
        }
        const std::string prefix = argv[4];
        const std::wstring dir = DirectoryOf(FullPath(argv[4]));
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring ini = dir + L"mdview.ini";
        DeleteFileW(ini.c_str());
        struct {
            int size;
            DWORD lo, hi;
            char ini[MAX_PATH];
        } dp{static_cast<int>(sizeof(dp)), 13, 2, {}};
        strncpy_s(dp.ini, WideToUtf8(dir + L"lsplugin.ini").c_str(), _TRUNCATE);
        set_params(&dp);

        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MdViewSettingsHost";
        RegisterClassW(&wc);
        HWND host = CreateWindowExW(0, wc.lpszClassName, L"settings test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40,
                                    1000, 760, nullptr, nullptr, wc.hInstance, nullptr);
        HWND viewer = load(host, FullPath(argv[3]).c_str(), 0);
        if (!viewer) {
            std::fprintf(stderr, "ListLoadW failed\n");
            return 1;
        }
        const UINT open_settings = RegisterWindowMessageW(L"MdViewWlxOpenSettings");
        const DWORD ui_thread = GetCurrentThreadId();
        std::atomic<bool> done{false};
        int failures = 0;
        auto expect = [&failures](bool ok, const char* what) {
            std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
            if (!ok) ++failures;
        };
        // The dialog runs a modal loop on the UI thread: drive it from here.
        std::thread driver([&] {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // WIC for the captures
            auto find_dialog = [&]() -> HWND {
                for (int i = 0; i < 100; ++i) {
                    HWND found = nullptr;
                    EnumThreadWindows(
                        ui_thread,
                        [](HWND h, LPARAM lp) {
                            wchar_t cls[16] = {};
                            GetClassNameW(h, cls, 16);
                            if (wcscmp(cls, L"#32770") == 0 && IsWindowVisible(h)) {
                                *reinterpret_cast<HWND*>(lp) = h;
                                return FALSE;
                            }
                            return TRUE;
                        },
                        reinterpret_cast<LPARAM>(&found));
                    if (found) return found;
                    Sleep(50);
                }
                return nullptr;
            };
            auto command = [](HWND dlg, int id, int code) {
                SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(id, code), reinterpret_cast<LPARAM>(GetDlgItem(dlg, id)));
            };
            auto set_check = [&](HWND dlg, int id, bool on) {
                SendDlgItemMessageW(dlg, id, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
                command(dlg, id, BN_CLICKED);
            };
            auto select = [&](HWND dlg, int id, int index) {
                SendDlgItemMessageW(dlg, id, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
                command(dlg, id, CBN_SELCHANGE);
            };
            auto type = [&](HWND dlg, int id, const wchar_t* text) {
                SetDlgItemTextW(dlg, id, text);
                command(dlg, id, CBN_EDITCHANGE);
            };
            auto closed = [](HWND dlg) {
                for (int i = 0; i < 100 && IsWindow(dlg); ++i) Sleep(50);
                return !IsWindow(dlg);
            };
            auto ini_value = [&](const wchar_t* key) {
                return static_cast<int>(GetPrivateProfileIntW(L"mdview", key, -999, ini.c_str()));
            };
            PostMessageW(viewer, open_settings, 0, 0);
            HWND dlg = find_dialog();
            expect(dlg != nullptr, "dialog opens");
            if (dlg) {
                Sleep(300);
                wchar_t title[64] = {};
                GetWindowTextW(dlg, title, 64);
                RECT rc{};
                GetWindowRect(dlg, &rc);
                std::printf("dialog \"%s\" %ldx%ld\n", WideToUtf8(title).c_str(), rc.right - rc.left, rc.bottom - rc.top);
                DWORD sel_start = 0, sel_end = 0;
                SendDlgItemMessageW(dlg, 1005, CB_GETEDITSEL, reinterpret_cast<WPARAM>(&sel_start),
                                    reinterpret_cast<LPARAM>(&sel_end));
                expect(sel_start == sel_end, "unfocused number fields show no selection");
                expect(!IsDarkPixel(CaptureWindow(dlg, prefix + "-dialog.png", false, 20, 60)), "light dialog");
                expect(!IsDarkPixel(CaptureWindow(host, prefix + "-viewer0.png", true)), "light viewer");
                // Live preview: dark theme, 20 px text, no line numbers.
                select(dlg, 1001, 2);
                type(dlg, 1004, L"20");
                set_check(dlg, 1009, false);
                Sleep(900);
                expect(IsDarkPixel(CaptureWindow(host, prefix + "-viewer1.png", true)), "viewer turns dark at once");
                expect(IsDarkPixel(CaptureWindow(dlg, prefix + "-dialog-dark.png", false, 20, 60)), "dialog turns dark");
                expect(GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES, "nothing saved before OK");
                PostMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
                expect(closed(dlg), "Cancel closes");
                Sleep(300);
                expect(!IsDarkPixel(CaptureWindow(host, prefix + "-viewer2.png", true)), "Cancel restores the theme");
                expect(GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES, "Cancel saves nothing");
            }
            PostMessageW(viewer, open_settings, 0, 0);
            dlg = find_dialog();
            expect(dlg != nullptr, "dialog opens again");
            if (dlg) {
                type(dlg, 1004, L"18");
                select(dlg, 1005, 7);         // 125 %
                set_check(dlg, 1007, true);   // full width
                set_check(dlg, 1011, false);  // no remote images
                Sleep(600);
                CaptureWindow(host, prefix + "-viewer3.png", true);
                PostMessageW(dlg, WM_COMMAND, IDOK, 0);
                expect(closed(dlg), "OK closes");
                Sleep(200);
                expect(ini_value(L"FontSize") == 18 && ini_value(L"Zoom") == 125 && ini_value(L"MaxWidth") == 0 &&
                           ini_value(L"RemoteImages") == 0 && ini_value(L"Theme") == 0 &&
                           ini_value(L"LineNumbers") == 1,
                       "OK saves the new values");
            }
            CoUninitialize();
            done = true;
        });
        const double end = NowMs() + 30000;
        MSG msg;
        while (!done && NowMs() < end) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(5);
        }
        if (!done) {
            std::printf("FAIL: timeout\n");
            std::fflush(stdout);
            TerminateProcess(GetCurrentProcess(), 1);
        }
        driver.join();
        close(viewer);
        DestroyWindow(host);
        FreeLibrary(dll);
        std::printf(failures ? "settings FAILED\n" : "settings OK\n");
        return failures ? 1 : 0;
    }
    if (cmd == "reload" && argc >= 4) {
        // reload <mdview.wlx64> <dir>: auto-reload check. Opens <dir>\reload-test.md,
        // saves it the way editors do (temp file + rename), then searches for
        // the new text; also checks that AutoReload=0 turns it off.
        HMODULE dll = LoadLibraryW(Utf8ToWide(argv[2]).c_str());
        using SetParamsFn = void(__stdcall*)(void*);
        using LoadFn = HWND(__stdcall*)(HWND, const wchar_t*, int);
        using SearchFn = int(__stdcall*)(HWND, const wchar_t*, int);
        using CloseFn = void(__stdcall*)(HWND);
        auto load = dll ? reinterpret_cast<LoadFn>(GetProcAddress(dll, "ListLoadW")) : nullptr;
        auto search = dll ? reinterpret_cast<SearchFn>(GetProcAddress(dll, "ListSearchTextW")) : nullptr;
        auto close = dll ? reinterpret_cast<CloseFn>(GetProcAddress(dll, "ListCloseWindow")) : nullptr;
        auto set_params = dll ? reinterpret_cast<SetParamsFn>(GetProcAddress(dll, "ListSetDefaultParams")) : nullptr;
        if (!load || !search || !close || !set_params) {
            std::fprintf(stderr, "cannot load plugin\n");
            return 1;
        }
        const std::wstring dir = FullPath(argv[3]);
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring file = dir + L"\\reload-test.md";
        const std::wstring ini = dir + L"\\mdview.ini";
        struct {
            int size;
            DWORD lo, hi;
            char ini[MAX_PATH];
        } dp{static_cast<int>(sizeof(dp)), 13, 2, {}};
        strncpy_s(dp.ini, WideToUtf8(dir + L"\\lsplugin.ini").c_str(), _TRUNCATE);
        auto write = [](const std::wstring& path, const std::string& text) {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD n = 0;
            WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &n, nullptr);
            CloseHandle(h);
        };
        auto save_like_editor = [&](const std::string& text) {
            const std::wstring tmp = file + L".tmp~";
            write(tmp, text);
            DeleteFileW(file.c_str());
            MoveFileW(tmp.c_str(), file.c_str());
        };
        auto pump = [](int ms) {
            const double end = NowMs() + ms;
            MSG msg;
            while (NowMs() < end) {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
                Sleep(5);
            }
        };
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MdViewReloadHost";
        RegisterClassW(&wc);
        HWND host = CreateWindowExW(0, wc.lpszClassName, L"reload test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40,
                                    800, 600, nullptr, nullptr, wc.hInstance, nullptr);
        int failures = 0;
        for (int enabled = 1; enabled >= 0; --enabled) {
            write(ini, enabled ? "[mdview]\r\nAutoReload=1\r\n" : "[mdview]\r\nAutoReload=0\r\n");
            set_params(&dp);  // settings load once per process: first round only
            write(file, "# Reload test\n\nfirst version\n");
            HWND v = load(host, file.c_str(), 0);
            pump(300);
            const std::string marker = enabled ? "second-version-marker" : "third-version-marker";
            save_like_editor("# Reload test\n\n" + marker + "\n");
            pump(1500);
            const bool found = search(v, Utf8ToWide(marker).c_str(), 1) == 0;
            std::printf("AutoReload=%d: new text %s\n", enabled, found ? "shown" : "not shown");
            if (found != (enabled == 1)) ++failures;
            close(v);
            if (enabled) {
                // Settings are cached per process: flip the switch as the menu would.
                FreeLibrary(dll);
                dll = LoadLibraryW(Utf8ToWide(argv[2]).c_str());
                load = reinterpret_cast<LoadFn>(GetProcAddress(dll, "ListLoadW"));
                search = reinterpret_cast<SearchFn>(GetProcAddress(dll, "ListSearchTextW"));
                close = reinterpret_cast<CloseFn>(GetProcAddress(dll, "ListCloseWindow"));
                set_params = reinterpret_cast<SetParamsFn>(GetProcAddress(dll, "ListSetDefaultParams"));
            }
        }
        DestroyWindow(host);
        FreeLibrary(dll);
        std::printf(failures ? "reload FAILED\n" : "reload OK\n");
        return failures ? 1 : 0;
    }
    if (cmd == "flags") {
        // flags <file.md>: flag emoji detection, the embedded Twemoji set, and
        // the flags of a document.
        int failures = 0;
        {
            std::string md;
            if (!LoadMarkdown(argv[2], md)) return 1;
            auto doc = ParseMarkdown(md, FullPath(argv[2]));
            size_t objects = 0;
            for (const Block* leaf : doc->leaves) {
                for (const InlineObject& o : leaf->objects) objects += o.length > 1 ? 1 : 0;
            }
            std::printf("%s: %zu flags\n", argv[2], objects);
            if (objects == 0) ++failures;
        }
        struct Case {
            const char* md;
            const char* keys;  // expected flags, space separated
        };
        const Case cases[] = {
            {"\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", "us"},
            {"a\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8 b \xF0\x9F\x87\xB7\xF0\x9F\x87\xBA.", "us ru"},
            {"\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xB7", "us"},              // odd count
            {"\xF0\x9F\x87\xA6\xF0\x9F\x87\xA6", ""},                                // AA: no country
            {"\xF0\x9F\x87\xA6\xF0\x9F\x87\xA6\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", "us"},  // pairs from the start
            {"\xF0\x9F\x87\xA6\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", "au"},              // A U | S
            {"\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xA5\xF3\xA0\x81\xAE\xF3\xA0\x81\xA7"
             "\xF3\xA0\x81\xBF", "gbeng"},
            {"\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xA5\xF3\xA0\x81\xAE\xF3\xA0\x81\xA7",
             ""},  // no cancel tag
            {"\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xB8\xF3\xA0\x81\xB9\xF3\xA0\x81\xBA"
             "\xF3\xA0\x81\xBF", ""},  // gbxyz: unknown
            {"x<sup>\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8</sup>", ""},
            {"`\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8`", "us"},
            {":ru: and :jp:", "ru jp"},
            {"\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", "us us"},
        };
        for (const Case& c : cases) {
            auto doc = ParseMarkdown(c.md, L"C:\\flags.md");
            std::string got;
            size_t flag_images = 0;
            for (const ImageRef& im : doc->images) flag_images += im.flag ? 1 : 0;
            for (const Block* leaf : doc->leaves) {
                for (const InlineObject& o : leaf->objects) {
                    if (o.image < 0 || !doc->images[static_cast<size_t>(o.image)].flag) continue;
                    if (!got.empty()) got += ' ';
                    got += WideToUtf8(doc->images[static_cast<size_t>(o.image)].src);
                    if (leaf->text.compare(o.pos, o.length, doc->images[static_cast<size_t>(o.image)].alt) != 0) {
                        got += "(wrong range)";
                    }
                }
            }
            const std::wstring text = doc->PlainText(0, doc->text_length);
            const bool text_kept = text.find(L'\xFFFC') == std::wstring::npos;
            const bool deduped = flag_images <= 2;
            const bool ok = got == c.keys && text_kept && deduped;
            if (!ok) {
                ++failures;
                std::printf("FAIL: %s -> \"%s\" (expected \"%s\")\n", c.md, got.c_str(), c.keys);
            }
        }
        // The whole set: every region code that exists unpacks and parses.
        std::vector<std::string> keys;
        for (char a = 'a'; a <= 'z'; ++a) {
            for (char b = 'a'; b <= 'z'; ++b) keys.push_back(std::string{a, b});
        }
        keys.insert(keys.end(), {"gbeng", "gbsct", "gbwls", "gbzzz", ""});
        const double t0 = NowMs();
        std::vector<std::string> svgs = FlagSvgs(keys);
        const double t1 = NowMs();
        size_t found = 0, parsed = 0, bytes = 0;
        for (std::string& svg : svgs) {
            if (svg.empty()) continue;
            ++found;
            bytes += svg.size();
            if (SvgImage::Parse(std::move(svg))) ++parsed;
        }
        std::printf("%zu flags (%zu KB of SVG) unpacked in %.1f ms, %zu parse\n", found, bytes >> 10, t1 - t0, parsed);
        if (found != 262 || parsed != found) ++failures;
        std::printf(failures ? "flags FAILED\n" : "flags OK\n");
        return failures ? 1 : 0;
    }
    if (cmd == "math") {
        // math <file.md> [iterations]: TeX parsing, layout of the file's
        // formulas, hostile input and a TeX fuzzer (no crash, bounded time).
        int failures = 0;
        struct Case {
            const char* tex;
            const char* dump;
        };
        const Case cases[] = {
            {"x^2_i", "list(scripts(g:𝑥,list(g:𝑖),list(g:2)))"},
            {"\\frac12", "list(frac(list(g:1),list(g:2)))"},
            {"a-b", "list(g:𝑎,g:−,g:𝑏)"},
            {"\\sum_{i=1}^n", "list(scripts(g:∑,list(g:𝑖,g:=,g:1),list(g:𝑛)))"},
            {"\\left( x \\middle| y \\right.", "list(fenced:(|.(list(g:𝑥),list(g:𝑦)))"},
            {"\\begin{pmatrix} 1 & 2 \\\\ 3 & 4 \\end{pmatrix}",
             "list(array:c[()](list(list(g:1),list(g:2)),list(list(g:3),list(g:4))))"},
            {"\\begin{cases} 1 & x \\\\ 0 & y \\end{cases}",
             "list(array:ll[{.](list(list(g:1),list(g:𝑥)),list(list(g:0),list(g:𝑦))))"},
            {"\\text{if } x", "list(list(text:if ),g:𝑥)"},
            {"\\color{red} x + \\textcolor{blue}{y}", "list(g:𝑥#ff0000,g:+#ff0000,g:𝑦#0000ff)"},
            {"\\foo{x}", "list(text:\\foo!,list(g:𝑥))"},
            {"f'(x)", "list(scripts(g:𝑓,list,list(g:′)),g:(,g:𝑥,g:))"},
            {"x'^2", "list(scripts(g:𝑥,list,list(g:′,g:2)))"},
            {"\\sum_{\\substack{i=1\\\\j=2}}",
             "list(scripts(g:∑,list(array:c(list(list(g:𝑖,g:=,g:1)),list(list(g:𝑗,g:=,g:2)))),list))"},
            {"\\operatorname{\\mathbb{E}}", "list(list(g:𝔼))"},
            {"\\boldsymbol{\\Sigma\\mu}", "list(g:𝚺,g:𝝁)"},
            {"\\begin{matrix} a \\\\ [0,1) \\end{matrix} + z",
             "list(array:c(list(list(g:𝑎)),list(list(g:[,g:0,g:,,g:1,g:)))),g:+,g:𝑧)"},
            {"a \\\\[4pt] b", "list(array:c(list(list(g:𝑎)),list(list(g:𝑏))))"},
            {"\\not x \\not}", "list(g:𝑥̸,g:/)"},
            {"\\hspace{nan}x", "list(space:0,g:𝑥)"},
            {"\\xrightarrow[\\{]{f} y", "list(arrow:→(list(g:𝑓),list(g:{)),g:𝑦)"},
            {"\\not= \\not\\in", "list(g:≠,g:∉)"},
            {"{a \\over b}", "list(list(frac(list(g:𝑎),list(g:𝑏))))"},
            {"\\sqrt[3]{x}", "list(sqrt(list(g:𝑥),list(g:3)))"},
            {"\\overbrace{x}^{n}", "list(scripts(over:⏞(list(g:𝑥),list),list,list(g:𝑛)))"},
            {"\\mathbb{R}\\mathbf{v}\\boldsymbol{\\alpha}", "list(g:ℝ,g:𝐯,g:𝜶)"},
            {"a \\\\ b", "list(array:c(list(list(g:𝑎)),list(list(g:𝑏))))"},
            {"\\binom{n}{k}", "list(frac[()](list(g:𝑛),list(g:𝑘)))"},
            {"\\big( \\Bigr]", "list(delim:(,delim:])"},
            {"\\alpha\\Gamma\\partial", "list(g:𝛼,g:Γ,g:𝜕)"},
            {"\\lim_{x\\to 0}", "list(scripts(text:lim,list(g:𝑥,g:→,g:0),list))"},
        };
        for (const Case& c : cases) {
            const std::string got = WideToUtf8(DumpMath(ParseTex(Utf8ToWide(c.tex))));
            if (got != c.dump) {
                std::printf("FAIL: %s\n  got      %s\n  expected %s\n", c.tex, got.c_str(), c.dump);
                ++failures;
            }
        }
        ComPtr<IDWriteFactory> dw;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dw.GetAddressOf()));
        MathTypesetter ts;
        if (!dw || !ts.Init(dw.Get())) {
            std::printf("math FAILED: no math font\n");
            return 1;
        }
        auto finite = [](float v) { return std::isfinite(v) && std::fabs(v) < 1e7f; };
        auto sane = [&](const MathBox& b) {
            if (!finite(b.width) || !finite(b.ascent) || !finite(b.descent) || b.width < 0) return false;
            for (const auto& g : b.glyphs) {
                if (!finite(g.x) || !finite(g.y) || !(g.size > 0)) return false;
            }
            for (const auto& r : b.rules) {
                if (!finite(r.x) || !finite(r.y) || !finite(r.w) || !finite(r.h)) return false;
            }
            return true;
        };
        // Every formula of the file, in both styles.
        std::string md;
        if (!LoadMarkdown(argv[2], md)) return 1;
        auto doc = ParseMarkdown(md, FullPath(argv[2]));
        for (const MathRef& m : doc->maths) {
            for (bool display : {false, true}) {
                auto box = ts.Layout(m.tex, display, 16.f);
                if (!sane(*box) || box->width <= 0 || box->glyphs.empty()) {
                    std::printf("FAIL: layout of %s\n", WideToUtf8(m.tex).c_str());
                    ++failures;
                }
            }
        }
        std::printf("%zu formulas of %s laid out\n", doc->maths.size(), argv[2]);
        {
            // Only a formula too wide for its column makes a leaf scroll
            // horizontally: never plain text, centered images or tables.
            const std::string wide_md =
                "A plain paragraph of ordinary words that wraps at many different widths, "
                "with trailing spaces at its line ends and nothing else in it at all.\n\n"
                "<p align=\"center\"><img src=\"img/badge-build-passing.svg\"> <img src=\"img/badge-coverage.svg\"> "
                "<img src=\"img/badge-build-passing.svg\"> <img src=\"img/badge-coverage.svg\"></p>\n\n"
                "| a | b |\n|---|---|\n| cell text that wraps | \xF0\x9F\x87\xBA\xF0\x9F\x87\xB8 flag and more words |\n\n"
                "Inline $x^2 + y^2$ math in a sentence that also wraps around at some widths.\n\n"
                "$$\\sum_{k=1}^{n} a_k + b_k + c_k + d_k + e_k + f_k + g_k + h_k + i_k + j_k + k_k + l_k + m_k$$\n";
            auto wdoc = ParseMarkdown(wide_md, FullPath("samples/gfm/x.md"));
            Palette palette;
            ImageStore images;
            images.Attach(wdoc.get(), nullptr, false, false);
            images.LoadAll();
            LayoutEngine le;
            le.Init(dw.Get(), &palette, &images);
            LayoutConfig cfg;
            cfg.max_content_width = 0;
            le.SetConfig(cfg);
            le.SetDocument(wdoc.get());
            int wide_formula = 0;
            for (int w = 280; w <= 960; w += 3) {
                le.Layout(static_cast<float>(w), 0, 1e6f);
                for (const Block* leaf : wdoc->leaves) {
                    const bool display = !leaf->objects.empty() && leaf->objects[0].math >= 0 &&
                                         wdoc->maths[static_cast<size_t>(leaf->objects[0].math)].display;
                    if (leaf->content_w > 0 && !display) {
                        std::printf("FAIL: \"%s...\" scrolls at width %d\n", WideToUtf8(leaf->text.substr(0, 20)).c_str(), w);
                        ++failures;
                    }
                    if (leaf->content_w > 0 && display) ++wide_formula;
                }
            }
            if (!wide_formula) {
                std::printf("FAIL: the long display formula never scrolls\n");
                ++failures;
            }
        }
        {
            // However tall, a formula stays inside its line (so its leaf and
            // the space before the next one): in a paragraph, in a heading
            // (tighter lines), in <sup>, on wrapped lines.
            std::string rows;
            for (int i = 0; i < 60; ++i) rows += "a_{" + std::to_string(i) + "} & b \\\\ ";
            const std::string tall_md =
                "$$\\begin{pmatrix}" + rows + "\\end{pmatrix}$$\n\n# Heading $\\begin{pmatrix}" + rows.substr(0, 150) +
                "\\end{pmatrix}$\n\nText $\\frac{\\frac{a}{b}}{\\frac{c}{d}}$ and <sup>$\\sqrt{\\frac{x}{y}}$</sup> "
                "text wrapping at some widths $\\left(\\begin{matrix}1\\\\2\\\\3\\end{matrix}\\right)$ more words "
                "$\\int_0^1 x\\,dx$ and a few more words after it.\n";
            auto tdoc = ParseMarkdown(tall_md, FullPath("samples/gfm/x.md"));
            Palette palette;
            ImageStore images;
            images.Attach(tdoc.get(), nullptr, false, false);
            LayoutEngine le;
            le.Init(dw.Get(), &palette, &images);
            LayoutConfig cfg;
            cfg.max_content_width = 0;
            le.SetConfig(cfg);
            le.SetDocument(tdoc.get());
            size_t checked = 0;
            for (int w : {300, 520, 800}) {
                le.Layout(static_cast<float>(w), 0, 1e6f);
                for (const Block* leaf : tdoc->leaves) {
                    LeafState* st = le.State(leaf);
                    if (!st || !st->layout) continue;
                    UINT32 count = 0;
                    st->layout->GetLineMetrics(nullptr, 0, &count);
                    std::vector<DWRITE_LINE_METRICS> lines(count);
                    if (!count || FAILED(st->layout->GetLineMetrics(lines.data(), count, &count))) continue;
                    ObjectRecorder rec;
                    st->layout->Draw(nullptr, &rec, 0, 0);
                    for (const ObjectRecorder::Placed& p : rec.placed) {
                        // The line holding the object's baseline.
                        const float baseline = p.y + p.metrics.baseline;
                        float top = 0, line_bottom = 0;
                        for (const DWRITE_LINE_METRICS& lm : lines) {
                            line_bottom = top + lm.height;
                            if (baseline <= line_bottom) break;
                            top = line_bottom;
                        }
                        const float ink_top = p.y - p.overhang.top;
                        const float ink_bottom = p.y + p.metrics.height + p.overhang.bottom;
                        ++checked;
                        if (ink_top < top - 0.5f || ink_bottom > line_bottom + 0.5f) {
                            std::printf("FAIL: formula ink %.1f..%.1f outside its line %.1f..%.1f (width %d)\n", ink_top,
                                        ink_bottom, top, line_bottom, w);
                            ++failures;
                        }
                    }
                }
            }
            if (checked < 6) {
                std::printf("FAIL: only %zu formulas placed\n", checked);
                ++failures;
            }
        }
        // Hostile input: deep nesting, huge and malformed sources.
        auto repeat = [](const wchar_t* s, int n) {
            std::wstring r;
            for (int i = 0; i < n; ++i) r += s;
            return r;
        };
        const std::wstring hostile[] = {
            std::wstring(20000, L'{'), repeat(L"\\sqrt", 5000), repeat(L"x^", 5000), repeat(L"\\left(", 3000),
            repeat(L"\\begin{matrix}", 500), repeat(L"\\frac", 5000), repeat(L"\\text{", 2000),
            repeat(L"\\textbf{", 3000), std::wstring(30000, L'x'), repeat(L"&\\\\", 3000), repeat(L"\\overbrace{", 1000),
            repeat(L"\\mathbb{", 1000), repeat(L"\\text{$", 1000), repeat(L"{\\over", 2000), repeat(L"\\sqrt[", 2000),
            repeat(L"\\substack{", 500), repeat(L"\\xrightarrow[", 500), repeat(L"\\qquad", 3000),
            L"\\hspace{1e30em}\\kern-1e30mu", L"\\color{#zzz}x\\color[rgb]{9,9}y", L"\\big", L"\\left", L"\\right)",
            L"\\end{x}", L"}}}}", L"\\\\", L"^_^_", L"\\frac{", L"\\begin{array}{", L"\\operatorname*", L"\\not",
            L"\\", L"\\text{\\", L"\\sqrt[\\frac]{}", L"\\begin{pmatrix}\\left( & \\right) \\\\ } \\end{pmatrix}",
            repeat(L"\\not", 5000) + L"=", repeat(L"\\not{", 3000), L"x \\hspace{nan} y \\kern-3em z",
            L"\\color[rgb]{nan,0,0} x \\kern-30em", L"\\sqrt{\\begin{matrix}" + repeat(L"x\\\\", 60) + L"\\end{matrix}}",
            L"\\text{\u0645\u0631\u062d\u0628\u0627 \U0001F600 \u05e9\u05dc\u05d5\u05dd}", repeat(L"x'", 3000),
        };
        for (const std::wstring& h : hostile) {
            const double t0 = NowMs();
            auto box = ts.Layout(h, true, 16.f);
            const double ms = NowMs() - t0;
            if (!sane(*box) || ms > 3000) {
                std::printf("FAIL: hostile input %s... (%.0f ms)\n", WideToUtf8(h.substr(0, 30)).c_str(), ms);
                ++failures;
            }
        }
        {
            // Emoji are DirectWrite layouts, a draw call each: a bounded number
            // per formula, and only those on screen drawn.
            auto box = ts.Layout(repeat(L"\U0001F600", 9000), false, 16.f);
            if (box->texts.size() > 64 || box->glyphs.size() < 8000) {
                std::printf("FAIL: 9000 emoji: %zu layouts, %zu glyphs\n", box->texts.size(), box->glyphs.size());
                ++failures;
            }
            // As fast as a formula of the emoji that fit on screen (color
            // emoji are slow to draw on some runners: timed against them).
            auto render_ms = [](int count) {
                std::string text = "$";
                for (int i = 0; i < count; ++i) text += "\xF0\x9F\x98\x80";
                auto d = ParseMarkdown(text + "$\n", FullPath("samples/gfm/x.md"));
                OffscreenOptions opt;
                opt.width = 800;
                opt.max_height = 600;
                double best = 1e9;
                for (int i = 0; i < 3; ++i) {
                    OffscreenStats stats;
                    RenderToBitmap(*d, opt, &stats);
                    best = std::min(best, stats.render_ms);
                }
                return best;
            };
            const double shown = render_ms(40), all = render_ms(3000);
            if (all > 2 * shown + 50) {
                std::printf("FAIL: a formula of 3000 emoji renders in %.0f ms (of 40: %.0f ms)\n", all, shown);
                ++failures;
            }
        }
        // Fuzz: random formulas from TeX fragments.
        static const wchar_t* kPieces[] = {
            L"x", L"2", L"+", L"-", L"=", L"^", L"_", L"{", L"}", L"\\frac", L"\\sqrt", L"[", L"]", L"\\left(",
            L"\\right)", L"\\middle|", L"\\begin{pmatrix}", L"\\end{pmatrix}", L"\\begin{cases}", L"\\end{cases}",
            L"\\begin{aligned}", L"\\end{aligned}", L"&", L"\\\\", L"\\sum", L"\\int", L"\\limits", L"\\text{a b}",
            L"\\mathbb{R}", L"\\hat", L"\\widehat", L"\\overbrace", L"\\underbrace", L"\\overset", L"\\color{red}",
            L"\\textcolor{blue}", L"\\boxed", L"\\phantom", L"\\not", L"\\big(", L"\\Bigg]", L"\\alpha", L"\\foo",
            L"'", L"\\,", L"\\quad", L"\\over", L"\\binom", L"\\xrightarrow", L"\\operatorname", L"\\displaystyle",
            L"\\scriptstyle", L" ", L"\\mathrm{d}", L"\\lim", L"\\to", L"\\infty", L"\\vec", L"\\dot", L"\\{", L"\\}",
            L"\\|", L"$", L"%", L"#", L"\\substack{a\\\\b}", L"\\hspace{2em}", L"\\mathstrut", L"\U0001D465",
            L"日本", L"\\text{$x$}", L"\\overline", L"\\underline", L"\\bmod", L"\\pmod", L"\\left.",
            L"\\right\\}", L"\\begin{array}{lcr}", L"\\end{array}", L"\\tag{1}", L"\\iff", L"\\prod", L"\\oint",
        };
        uint32_t seed = 4242;
        auto rnd = [&seed] {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return seed;
        };
        const int iterations = argc >= 4 ? std::atoi(argv[3]) : 3000;
        const double f0 = NowMs();
        for (int it = 0; it < iterations; ++it) {
            std::wstring tex;
            const int n = 1 + static_cast<int>(rnd() % 60);
            for (int k = 0; k < n; ++k) tex += kPieces[rnd() % std::size(kPieces)];
            auto box = ts.Layout(tex, (rnd() & 1) != 0, 8.f + static_cast<float>(rnd() % 40));
            if (!sane(*box)) {
                std::printf("FAIL: fuzz %s\n", WideToUtf8(tex).c_str());
                ++failures;
            }
        }
        std::printf("%d random formulas in %.0f ms\n", iterations, NowMs() - f0);
        std::printf(failures ? "math FAILED\n" : "math OK\n");
        return failures ? 1 : 0;
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
    if (cmd == "mermaid") {
        // mermaid <file.md> [fuzz iterations]: every diagram of the file lays
        // out, parse checks, hostile input stays bounded, random sources
        // never crash.
        int failures = 0;
        ComPtr<IDWriteFactory> dwf;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwf.GetAddressOf()));
        auto sane = [](const DiagramScene& s) {
            auto finite = [](float v) { return std::isfinite(v) && std::fabs(v) < 1e7f; };
            if (!finite(s.width) || !finite(s.height) || s.width <= 0 || s.height <= 0) return false;
            for (const D2D1_POINT_2F& p : s.points) {
                if (!finite(p.x) || !finite(p.y)) return false;
            }
            for (const auto& sh : s.shapes) {
                if (!finite(sh.box.left) || !finite(sh.box.top) || !finite(sh.box.right) || !finite(sh.box.bottom)) return false;
            }
            for (const auto& t : s.texts) {
                if (!finite(t.origin.x) || !finite(t.origin.y)) return false;
            }
            return true;
        };
        auto layout = [&](const std::wstring& src, std::wstring* error) {
            return CheckMermaid(src, error) ? LayoutMermaid(src, dwf.Get(), L"Segoe UI", 16.f, error) : nullptr;
        };
        // Every diagram of the file.
        std::string md;
        if (!LoadMarkdown(argv[2], md)) return 1;
        auto doc = ParseMarkdown(md, FullPath(argv[2]));
        for (const DiagramRef& d : doc->diagrams) {
            std::wstring error;
            const auto scene = layout(d.source, &error);
            if (!scene || !sane(*scene)) {
                std::printf("FAIL: diagram %s: %s\n", WideToUtf8(d.source.substr(0, 40)).c_str(), WideToUtf8(error).c_str());
                ++failures;
            }
        }
        std::printf("%zu diagrams of %s laid out\n", doc->diagrams.size(), argv[2]);
        // Parsing: what is accepted, and errors with their line.
        struct Case {
            const wchar_t* src;
            bool ok;
            const wchar_t* error;  // part of the message
        };
        const Case cases[] = {
            {L"flowchart TD\n  A-->B", true, nullptr},
            {L"graph LR;A-->B;B-->C", true, nullptr},
            {L"flowchart\nA --> B", true, nullptr},
            {L"---\ntitle: T\n---\nflowchart TD\nA-->B", true, nullptr},
            {L"%%{init: {'theme':'dark'}}%%\nflowchart TD\nA-->B", true, nullptr},
            {L"flowchart TD\n%% a comment\nA-->B", true, nullptr},
            {L"flowchart TD\nnode-1 --> node-2", true, nullptr},
            {L"flowchart TD\nA@{ shape: cyl, label: \"DB\" } --> B", true, nullptr},
            {L"flowchart TD\nA -- text --> B -. dotted .-> C == thick ==> D", true, nullptr},
            {L"flowchart TD\nA[\"a ] b\"] --> B", true, nullptr},
            {L"flowchart TD\nsubgraph S [Title]\nA\nend\nS --> B", true, nullptr},
            {L"flowchart TD\nEnd((End)) --> Start", true, nullptr},  // keywords are lowercase
            {L"flowchart TD\nClass --> Object\nStyle --> Click", true, nullptr},
            {L"flowchart TD\nA e1@--> B\ne1@{ animate: true }", true, nullptr},
            {L"flowchart TD\nA e-1@ --> B\ne-1@{ label: \"a } b\" }\nB -->|x| B\nB -->|y| B", true, nullptr},
            {L"flowchart TD\nA e1@--> B\ne1@{ animate: true\nB --> C", false, L"Line 3"},
            {L"flowchart TD\nA--oB\nC---xD", true, nullptr},
            {L"flowchart TD\nx@{ shape: fork } --> y@{ shape: flag } --> z@{ shape: sm-circ }", true, nullptr},
            {L"flowchart TD\nstyle A,B fill:#f00\nclass Nothing c", true, nullptr},
            {L"flowchart XY\nA --> B", false, L"Line 1"},
            {L"flowchart TD\nA[\"open\nB --> C\nC --> D", false, L"Line 2"},
            {L"flowchart TD\nA@{ shape: rect\nB --> C", false, L"Line 2"},
            {L"flowchart TD\nA[unclosed --> B", false, L"2"},
            {L"flowchart TD\nA --> B\nend", false, L"3"},
            {L"flowchart TD\nA -> B", false, L"2"},
            {L"sequenceDiagram\nA->>B: hi\nB-->>A: ok", true, nullptr},
            {L"sequenceDiagram\nparticipant A as Alice\nactor B\nA->>+B: x\nB-->>-A: y\nNote over A,B: n\nloop L\nA-)B: z\nend", true, nullptr},
            {L"sequenceDiagram\nbox Aqua G\nparticipant A\nend\nautonumber\ncreate participant C\nA->>C: c\ndestroy C\nC-xA: d", true, nullptr},
            {L"sequenceDiagram\nalt a\nA->>B: x\nelse b\nA->>B: y\nend\ncritical c\nA->B: z\noption o\nA-->B: w\nend", true, nullptr},
            {L"sequenceDiagram\nparticipant API-Gateway\nAPI-Gateway->>Auth-Service: validate\nAuth-Service-XAPI-Gateway: no", true, nullptr},
            {L"sequenceDiagram\nparticipant John Doe\nparticipant Jane Roe as JR\nJohn Doe->>JR: hi\nparticipant Q@{ \"type\": \"queue\" } as Queue", true, nullptr},
            {L"sequenceDiagram\nA->>B: I #9829; you#59; and #quot;them#quot;", true, nullptr},
            {L"sequenceDiagram\naccDescr {\nlong\ndescription\n}\npar_over x\nA->>B: wrap:a long text\nend", true, nullptr},
            {L"sequenceDiagram\nparticipant SaaS as Software as a Service\nA->>SaaS: x\nloop wrap: a title\nA->>SaaS: y\nend", true, nullptr},
            {L"sequenceDiagram\nA->>B->>C: x", false, L"Line 2"},
            {L"sequenceDiagram\nloop x\nA->>B: y", false, L"end"},
            {L"sequenceDiagram\nA=>B: x", false, L"2"},
            {L"sequenceDiagram\nNote above A: x", false, L"2"},
            {L"sequenceDiagram\nelse x", false, L"2"},
            {L"classDiagram\nAnimal <|-- Duck\nAnimal : +int age\nAnimal: +isMammal() bool$", true, nullptr},
            {L"classDiagram\nclass Square~Shape~{\n  int id\n  List~int~ position\n  getPoints() List~int~\n}", true, nullptr},
            {L"classDiagram\nA \"1\" --> \"*\" B : has\nC ..|> D\nE --* F\nG o-- H\nI ()-- J\nK <|--|> L", true, nullptr},
            {L"classDiagram\nnamespace N {\nclass A\nclass B\n}\nnote for A \"x\"\nnote \"y\"\n<<interface>> A", true, nullptr},
            {L"classDiagram\ndirection LR\nclass Note\nClass --> Note\nclassDef c fill:#f9f\ncssClass \"Note\" c\nstyle Class fill:#fff", true, nullptr},
            {L"classDiagram\nclass `Odd name!`\nclass A[\"A label\"]", true, nullptr},
            {L"classDiagram\nA..B\nclassK..>classL\nclass Shape <<interface>> {\n+draw()\n}\nnamespace N { class Z }", true, nullptr},
            {L"classDiagram\nA : +foo ( int x )$ int\nnote for Ghost \"alone\"\nstyle Ghost fill:#f00", true, nullptr},
            {L"classDiagram\nclass A {\n+x", false, L"Line 2"},
            {L"classDiagram\nA <-> B", false, L"Line 2"},
            {L"classDiagram\n}", false, L"Line 2"},
            {L"stateDiagram-v2\n[*] --> Still\nStill --> [*]\nStill --> Moving : go\nMoving --> Still", true, nullptr},
            {L"stateDiagram-v2\nstate Active {\n[*] --> A\nA --> B\n--\n[*] --> C\n}\n[*] --> Active\nActive --> [*]", true, nullptr},
            {L"stateDiagram\nstate c <<choice>>\nstate f <<fork>>\nstate j <<join>>\n[*] --> c\nc --> f : x\nf --> A\nf --> B\nA --> j\nB --> j\nj --> [*]", true, nullptr},
            {L"stateDiagram-v2\nstate \"A long name\" as s2\ns2 : more\ns2 : and more\nnote right of s2 : a note\nnote left of s2\nline one\nline two\nend note", true, nullptr},
            {L"stateDiagram-v2\ndirection LR\nclassDef bad fill:#f00\nA --> B:::bad\nclass A bad\nstyle B stroke:#00f\nA --> A : again", true, nullptr},
            {L"stateDiagram-v2\nstate \"Waiting\" as Wait\n[*] --> Main\nstate Main {\n[*] --> Wait\nWait --> [*]\n}\nMain --> [*]", true, nullptr},
            {L"stateDiagram-v2\n[*] --> Show\nShow --> Hide : toggle\nHide --> Show : toggle\nhide empty description\nscale 350 width", true, nullptr},
            {L"stateDiagram-v2\nclassDef c fill:#f00\nA --> [*]:::c\n[*]:::c --> A\nclass B c\nstyle B fill:#0f0\nB --> A\naccTitle: t", true, nullptr},
            {L"stateDiagram-v2\nstate C {\nX --> Y\n}\nC --> C : again\nC --> X : in\nY --> C : out", true, nullptr},
            {L"stateDiagram-v2\nstate A\n{\nB --> C\n-- %% next\nD --> E\n} A --> F\nstate G { H --> I }\nnote \"floating\" as N1\nstate f [[fork]]\n# a comment", true, nullptr},
            {L"stateDiagram-v2\nstate A {\nB --> C\n}\nstate A {\nD --> E\n}\nnote left of B extra\nmore\nend note", true, nullptr},
            {L"stateDiagram-v2\nstate A {\nB\n}\nstate B {\nA\n}", true, nullptr},
            {L"stateDiagram-v2\nstate A {\nB --> C\n-- D --> E\n}\nA --> F # a comment\nnote left of F\ntext\nend note here", true, nullptr},
            {L"stateDiagram-v2\nstate f [[fork\nA", false, L"Line 2"},
            {L"stateDiagram-v2\naccDescr {\nnever closed", false, L"Line 2"},
            {L"stateDiagram-v2\nstate A {\nB --> C", false, L"Line 2"},
            {L"stateDiagram-v2\n}", false, L"Line 2"},
            {L"stateDiagram-v2\n--", false, L"Line 2"},
            {L"stateDiagram-v2\nnote right of A\ntext", false, L"Line 2"},
            {L"stateDiagram-v2\nA -> B", false, L"Line 2"},
            {L"erDiagram\nCUSTOMER ||--o{ ORDER : places\nORDER ||--|{ LINE-ITEM : contains\nCUSTOMER }|..|{ DELIVERY-ADDRESS : uses", true, nullptr},
            {L"erDiagram\nCAR {\nstring registrationNumber PK\nstring(99) firstName \"Only 99\"\nstring[] parts\nint id PK, FK\n}", true, nullptr},
            {L"erDiagram\nMANUFACTURER only one to zero or more CAR : makes\nPERSON many(0) optionally to 0+ NAMED-DRIVER : is", true, nullptr},
            {L"erDiagram\np[Person] ||--o| a[\"Customer Account\"] : has\n\"A name\" |o--o| \"A name\" : \"self\"\ndirection LR", true, nullptr},
            {L"erDiagram\nsubgraph title1\nCUSTOMER {\nstring name\n}\nend\nsubgraph grp [\"Nice title\"]\ndirection LR\nCAR ||--o{ NAMED-DRIVER : allows\nsubgraph inner\nENGINE\nend\nend\nCUSTOMER ||--o{ CAR : owns\ngrp ||--|| CUSTOMER : links\nCUSTOMER }o--o{ title1 : in\ngrp |o--o| grp : self", true, nullptr},
            {L"erDiagram\nA one to many B : x\nC o|--|o D : y\nE |o.-o| F : z\nE |o-.o| F : z2\npublic.users ||--o{ public.orders : places\nI |O--O{ J : v\nx*y ||--|| a-b- : t", true, nullptr},
            {L"erDiagram\nclassDef c1 fill:#f00\nclassDef c2 stroke:#00f\nclass A,B c1,c2\nA:::c1,c2 ||--|| B : x\nA { string a int b PK,FK \"note\" }\nB {\n`some type` `some name` PK\n}", true, nullptr},
            {L"erDiagram\nsubgraph \"Customer Domain\"\nCUSTOMER ||--o{ ORDER : places\nend\n\"Customer Domain\" ||--o{ INVOICE : has\nsubgraph outer\nsubgraph inner\nA\nend\nA\nB\nend\nsubgraph Empty\nend\nA ||--o{ Empty : lost\nstyle outer fill:#f96\nclass inner c\nC { string x } C ||--|| A : r\nE |o--o| E : a\nE |o--o| E : b\nE |o--o| E : c", true, nullptr},
            {L"erDiagram\nsubgraph S\nA ||--|| B : x", false, L"Line 2"},
            {L"erDiagram\nA {\n`unclosed type\n}", false, L"Line 3"},
            {L"erDiagram\nA ||--o{ B", false, L"Line 2"},
            {L"erDiagram\nA ||-o{ B : x", false, L"Line 2"},
            {L"erDiagram\nA {\nstring", false, L"Line 3"},
            {L"erDiagram\nA {\nstring x", false, L"Line 2"},
            {L"classDiagram\nnote for A x", false, L"Line 2"},
            {L"gitGraph\ncommit", false, L"gitGraph"},
            {L"", false, nullptr},
            {L"not a diagram", false, nullptr},
        };
        for (const Case& c : cases) {
            std::wstring error;
            const bool ok = CheckMermaid(c.src, &error);
            if (ok != c.ok || (!ok && c.error && error.find(c.error) == std::wstring::npos)) {
                std::printf("FAIL: \"%s\" -> %s \"%s\"\n", WideToUtf8(c.src).c_str(), ok ? "ok" : "error",
                            WideToUtf8(error).c_str());
                ++failures;
            }
        }
        // Hostile input: bounded time, no crash.
        auto repeat = [](const std::wstring& s, int n) {
            std::wstring r;
            for (int i = 0; i < n; ++i) r += s;
            return r;
        };
        // Up to the limits (500 links, 24 levels of subgraphs) and past them.
        std::wstring chain = L"flowchart TD\n", dense = L"flowchart LR\n", nested = L"flowchart TD\n", fan = L"flowchart TD\n";
        std::wstring bomb = L"flowchart TD\n", deep = L"flowchart TD\n", boxes = L"flowchart LR\n";
        for (int i = 0; i < 500; ++i) chain += L"n" + std::to_wstring(i) + L" --> n" + std::to_wstring(i + 1) + L"\n";
        for (int i = 0; i < 60; ++i) {
            for (int j = 0; j < 60; ++j) {
                if ((i * 7 + j * 3) % 8 == 0) dense += L"a" + std::to_wstring(i) + L" --> b" + std::to_wstring(j) + L"\n";
            }
        }
        for (int i = 0; i < 40; ++i) nested += L"subgraph s" + std::to_wstring(i) + L"\nx" + std::to_wstring(i) + L" --> y" + std::to_wstring(i) + L"\n";
        nested += repeat(L"end\n", 40);
        for (int i = 0; i < 500; ++i) fan += L"root --> leaf" + std::to_wstring(i) + L"\n";
        // Long links down a chain plus links from its start to every node:
        // bends grow with the square of the size.
        for (int i = 0; i < 249; ++i) bomb += L"a" + std::to_wstring(i) + L" ----------> a" + std::to_wstring(i + 1) + L"\n";
        for (int k = 2; k < 252; ++k) bomb += L"a0 --> a" + std::to_wstring(k) + L"\n";
        // Every level of nesting linked out to the top.
        for (int i = 0; i < 24; ++i) deep += L"subgraph d" + std::to_wstring(i) + L"\nn" + std::to_wstring(i) + L" --> top\n";
        deep += repeat(L"end\n", 24);
        // Many boxes linked across each other.
        for (int i = 0; i < 60; ++i) {
            boxes += L"subgraph b" + std::to_wstring(i) + L"\np" + std::to_wstring(i) + L" --> q" + std::to_wstring(i) + L"\nend\n";
            boxes += L"q" + std::to_wstring(i) + L" --> p" + std::to_wstring((i * 7 + 3) % 60) + L"\n";
        }
        const std::wstring hostile[] = {
            chain, dense, nested, fan, bomb, deep, boxes,
            L"flowchart TD\nA[" + std::wstring(3900, L'x') + L"] --> B",
            L"flowchart TD\n" + repeat(L"A --> A\n", 500),
            L"flowchart TD\n" + repeat(L"A & B & C & D & E --> F & G & H & I & J\n", 20),
            L"flowchart TD\n" + repeat(L"subgraph X\n", 30) + L"A --> B\n" + repeat(L"end\n", 30),
            L"flowchart TD\n" + repeat(L"classDef c fill:#f00\n", 2000),
            L"flowchart TD\nA" + repeat(L" --> A", 500),
            L"flowchart TD\n" + repeat(L"A[\"a\"]:::x:::y:::z --- B\n", 500),
            std::wstring(L"flowchart TD\n") + std::wstring(70000, L'A'),
            L"sequenceDiagram\n" + repeat(L"A->>B: message\nB-->>A: reply\n", 1500),
            [&] {
                std::wstring s = L"sequenceDiagram\n";
                for (int i = 0; i < 1500; ++i) s += L"P" + std::to_wstring(i) + L"->>P" + std::to_wstring(i + 1) + L": m\n";
                return s;
            }(),
            L"sequenceDiagram\n" + repeat(L"loop x\n", 24) + L"A->>B: y\n" + repeat(L"end\n", 24),
            L"sequenceDiagram\n" + repeat(L"A->>+B: x\n", 1000) + repeat(L"B-->>-A: y\n", 1000),
            L"sequenceDiagram\nA->>B: " + std::wstring(3900, L'x'),
            L"sequenceDiagram\n" + repeat(L"Note over A,B: " + std::wstring(200, L'n') + L"\n", 500),
            [&] {  // a chain of classes, and links from the first to all: too many bends
                std::wstring s = L"classDiagram\n";
                for (int i = 0; i < 249; ++i) s += L"C" + std::to_wstring(i) + L" <|-- C" + std::to_wstring(i + 1) + L"\n";
                for (int k = 2; k < 252; ++k) s += L"C0 --> C" + std::to_wstring(k) + L"\n";
                return s;
            }(),
            [&] {  // many classes linked across namespaces
                std::wstring s = L"classDiagram\n";
                for (int i = 0; i < 40; ++i) {
                    s += L"namespace N" + std::to_wstring(i) + L" {\nclass A" + std::to_wstring(i) + L"\nclass B" + std::to_wstring(i) + L"\n}\n";
                    s += L"A" + std::to_wstring(i) + L" --> B" + std::to_wstring((i * 7 + 3) % 40) + L" : x\n";
                }
                return s;
            }(),
            L"classDiagram\nclass A {\n" + repeat(L"+method(List~List~int~~ x) Map~K~$\n", 1500) + L"}",
            L"classDiagram\n" + repeat(L"namespace N {\n", 30) + L"class A\n" + repeat(L"}\n", 30),
            L"classDiagram\n" + repeat(L"A --> A : self\n", 400),
            L"classDiagram\nA : " + std::wstring(3900, L'~'),  // (no recursion per ~)
            L"classDiagram\nclass A\n" + repeat(L"note for A \"n\"\n", 600),
            L"classDiagram\n" + repeat(L"note \"" + std::wstring(300, L'n') + L"\"\n", 100),
            [&] {  // a chain of states, and transitions from the first to all: too many bends
                std::wstring s = L"stateDiagram-v2\n";
                for (int i = 0; i < 249; ++i) s += L"S" + std::to_wstring(i) + L" --> S" + std::to_wstring(i + 1) + L"\n";
                for (int k = 2; k < 252; ++k) s += L"S0 --> S" + std::to_wstring(k) + L"\n";
                return s;
            }(),
            [&] {  // composites inside composites, each with regions, linked across
                std::wstring s = L"stateDiagram-v2\n";
                for (int i = 0; i < 20; ++i) s += L"state C" + std::to_wstring(i) + L" {\n[*] --> X" + std::to_wstring(i) + L"\n--\n[*] --> Y" + std::to_wstring(i) + L"\n";
                s += repeat(L"}\n", 20);
                for (int i = 0; i < 19; ++i) s += L"X" + std::to_wstring(i) + L" --> C" + std::to_wstring(i + 1) + L"\n";
                return s;
            }(),
            [&] {  // nested too deep
                std::wstring s = L"stateDiagram-v2\n";
                for (int i = 0; i < 30; ++i) s += L"state N" + std::to_wstring(i) + L" {\n";
                return s + L"B\n" + repeat(L"}\n", 30);
            }(),
            L"stateDiagram-v2\n" + repeat(L"A --> A : again\n", 400),
            L"stateDiagram-v2\n" + repeat(L"note right of A : " + std::wstring(200, L'n') + L"\n", 150),
            L"stateDiagram-v2\nstate X {\n" + repeat(L"[*] --> A\n--\n", 450) + L"}",
            [&] {  // composites named inside one another: nested deeper than written
                std::wstring s = L"stateDiagram-v2\n";
                for (int i = 0; i < 60; ++i) s += L"state C" + std::to_wstring(i) + L" {\nC" + std::to_wstring(i + 1) + L" --> X" + std::to_wstring(i) + L"\n}\n";
                return s;
            }(),
            [&] {  // states moved from composite to composite
                std::wstring s = L"stateDiagram-v2\n";
                for (int i = 0; i < 200; ++i) s += L"state C" + std::to_wstring(i) + L" {\nA --> B\nB --> C" + std::to_wstring((i + 1) % 200) + L"\n}\n";
                return s;
            }(),
            [&] {  // a chain of entities, and relationships from the first to all
                std::wstring s = L"erDiagram\n";
                for (int i = 0; i < 249; ++i) s += L"E" + std::to_wstring(i) + L" ||--o{ E" + std::to_wstring(i + 1) + L" : r\n";
                for (int k = 2; k < 252; ++k) s += L"E0 ||--|{ E" + std::to_wstring(k) + L" : f\n";
                return s;
            }(),
            L"erDiagram\nA {\n" + repeat(L"string(255) name PK, FK, UK \"comment\"\n", 1200) + L"}",
            L"erDiagram\n" + repeat(L"A |o..o| A : \"again\"\n", 300),
            L"erDiagram\n" + repeat(L"A ||--|| B : x\n", 2100),
            L"erDiagram\n" + repeat(L"subgraph S\n", 30) + L"A\n" + repeat(L"end\n", 30),
            [&] {  // many subgraphs, related as boxes, to their members and to themselves
                std::wstring s = L"erDiagram\n";
                for (int i = 0; i < 60; ++i) {
                    const std::wstring n = std::to_wstring(i);
                    s += L"subgraph G" + n + L"\nE" + n + L" ||--o{ F" + n + L" : r\nend\n";
                    s += L"G" + n + L" }o--|| G" + std::to_wstring((i * 7 + 3) % 60) + L" : g\nG" + n + L" |o--o| E" + n +
                         L" : in\nG" + n + L" ||--|| G" + n + L" : self\n";
                }
                return s;
            }(),
        };
        for (const std::wstring& h : hostile) {
            const double t0 = NowMs();
            std::wstring error;
            const auto scene = layout(h, &error);
            const double ms = NowMs() - t0;
            if ((scene && !sane(*scene)) || ms > 5000) {
                std::printf("FAIL: hostile input %s... (%.0f ms)\n", WideToUtf8(h.substr(0, 40)).c_str(), ms);
                ++failures;
            } else {
                std::printf("  hostile %.0f ms %s\n", ms, scene ? "drawn" : WideToUtf8(error).c_str());
            }
        }
        // Fuzz: random flowcharts from fragments.
        static const wchar_t* kPieces[] = {
            L"A", L"B", L"C", L"node-1", L" ", L"\n", L";", L"-->", L"---", L"-.->", L"==>", L"~~~", L"--o", L"--x",
            L"<-->", L"-- label -->", L"-->|txt|", L"[", L"]", L"(", L")", L"((", L"))", L"{", L"}", L"{{", L"}}",
            L"[(", L")]", L"([", L"])", L"[[", L"]]", L">", L"[/", L"/]", L"[\\", L"\\]", L"\"", L"`", L"&",
            L":::cls", L"subgraph S", L"subgraph T [Title]", L"end", L"direction LR", L"classDef cls fill:#f9f",
            L"class A cls", L"style A fill:#0f0", L"linkStyle 0 stroke:red", L"%% c", L"<br>", L"**b**", L"#quot;",
            L"@{ shape: diam }", L"привет", L"日本", L"😀", L"click A href", L"---\ntitle: x\n---\n",
        };
        uint32_t seed = 77;
        auto rnd = [&seed] {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return seed;
        };
        const int iterations = argc >= 4 ? std::atoi(argv[3]) : 2000;
        const double f0 = NowMs();
        int drawn = 0;
        for (int it = 0; it < iterations; ++it) {
            std::wstring src = (rnd() & 1) ? L"flowchart TD\n" : L"graph LR\n";
            const int n = 1 + static_cast<int>(rnd() % 50);
            for (int k = 0; k < n; ++k) src += kPieces[rnd() % std::size(kPieces)];
            std::wstring error;
            const auto scene = layout(src, &error);
            if (scene) ++drawn;
            if (scene && !sane(*scene)) {
                std::printf("FAIL: fuzz %s\n", WideToUtf8(src).c_str());
                ++failures;
            }
        }
        std::printf("%d random flowcharts in %.0f ms (%d drawn)\n", iterations, NowMs() - f0, drawn);
        // Well-formed random flowcharts: every part of the layout runs.
        static const wchar_t* kShapes[][2] = {{L"[", L"]"}, {L"(", L")"}, {L"([", L"])"}, {L"[[", L"]]"}, {L"[(", L")]"},
                                              {L"((", L"))"}, {L"{", L"}"}, {L"{{", L"}}"}, {L"[/", L"/]"}, {L">", L"]"}};
        static const wchar_t* kLinks[] = {L"-->", L"---", L"-.->", L"==>", L"--o", L"--x", L"<-->", L"~~~", L"--->"};
        static const wchar_t* kDirs[] = {L"TD", L"LR", L"BT", L"RL"};
        const double g0 = NowMs();
        for (int it = 0; it < iterations / 4; ++it) {
            std::wstring src = std::wstring(L"flowchart ") + kDirs[rnd() % 4] + L"\n";
            const int nodes = 1 + static_cast<int>(rnd() % 30);
            int open = 0;
            for (int k = 0; k < nodes * 2; ++k) {
                const int r = static_cast<int>(rnd() % 10);
                if (r == 0 && open < 5) {
                    src += L"subgraph G" + std::to_wstring(rnd() % 8) + L"\n";
                    ++open;
                    if (rnd() % 3 == 0) src += std::wstring(L"direction ") + kDirs[rnd() % 4] + L"\n";
                } else if (r == 1 && open > 0) {
                    src += L"end\n";
                    --open;
                } else {
                    const auto& s = kShapes[rnd() % std::size(kShapes)];
                    src += L"n" + std::to_wstring(rnd() % nodes) + s[0] + L"lbl " + std::to_wstring(k) + s[1] + L" " +
                           kLinks[rnd() % std::size(kLinks)] + ((rnd() & 3) == 0 ? L"|edge|" : L"") + L" n" +
                           std::to_wstring(rnd() % nodes) + ((rnd() & 7) == 0 ? L":::c" : L"") + L"\n";
                }
            }
            while (open-- > 0) src += L"end\n";
            if (rnd() & 1) src += L"classDef c fill:#fde,stroke:#a33\nlinkStyle 0 stroke:#08f\n";
            std::wstring error;
            const auto scene = layout(src, &error);
            if (!scene || !sane(*scene)) {
                std::printf("FAIL: generated %s: %s\n", WideToUtf8(src.substr(0, 200)).c_str(), WideToUtf8(error).c_str());
                ++failures;
            }
        }
        std::printf("%d generated flowcharts in %.0f ms\n", iterations / 4, NowMs() - g0);
        // Well-formed random sequence diagrams.
        static const wchar_t* kArrows[] = {L"->>", L"-->>", L"->", L"-->", L"-x", L"--x", L"-)", L"--)", L"<<->>"};
        static const wchar_t* kBlocks[] = {L"loop", L"alt", L"opt", L"par", L"critical", L"break", L"rect rgb(200,220,255)"};
        const double q0 = NowMs();
        for (int it = 0; it < iterations / 4; ++it) {
            std::wstring src = L"sequenceDiagram\n";
            if (rnd() & 1) src += L"autonumber\n";
            const int people = 1 + static_cast<int>(rnd() % 6);
            std::vector<int> open;
            int active = 0;
            for (int k = 0; k < 40; ++k) {
                const std::wstring a = L"P" + std::to_wstring(rnd() % people), b = L"P" + std::to_wstring(rnd() % people);
                switch (rnd() % 12) {
                case 0:
                    if (open.size() < 6) {
                        const int kind = static_cast<int>(rnd() % std::size(kBlocks));
                        src += std::wstring(kBlocks[kind]) + L" cond\n";
                        open.push_back(kind);
                    }
                    break;
                case 1:
                    if (!open.empty()) {
                        src += L"end\n";
                        open.pop_back();
                    }
                    break;
                case 2:
                    if (!open.empty() && open.back() == 1) src += L"else other\n";
                    break;
                case 3: src += L"Note " + std::wstring(rnd() & 1 ? L"left of " : L"over ") + a + L": note\n"; break;
                case 4: src += L"Note over " + a + L"," + b + L": wide note\n"; break;
                case 5:
                    src += a + L"->>+" + b + L": start\n";
                    ++active;
                    break;
                case 6:
                    if (active > 0) {
                        src += a + L"-->>-" + b + L": done\n";
                        --active;
                    }
                    break;
                default: src += a + kArrows[rnd() % std::size(kArrows)] + b + L": message " + std::to_wstring(k) + L"\n"; break;
                }
            }
            while (!open.empty()) {
                src += L"end\n";
                open.pop_back();
            }
            std::wstring error;
            const auto scene = layout(src, &error);
            if (!scene || !sane(*scene)) {
                std::printf("FAIL: generated %s: %s\n", WideToUtf8(src.substr(0, 200)).c_str(), WideToUtf8(error).c_str());
                ++failures;
            }
        }
        std::printf("%d generated sequence diagrams in %.0f ms\n", iterations / 4, NowMs() - q0);
        // Well-formed random class diagrams.
        static const wchar_t* kRelations[] = {L"<|--", L"*--", L"o--", L"-->", L"--", L"..>", L"..|>", L"..",
                                              L"--|>", L"--*", L"--o", L"<--", L"()--", L"<|--|>"};
        static const wchar_t* kMembers[] = {L"+int id", L"-String name$", L"#List~int~ items", L"~helper()",
                                            L"+run(int a, List~T~ b) bool*", L"getAll() List~List~int~~$", L"<<interface>>"};
        const double k0 = NowMs();
        for (int it = 0; it < iterations / 4; ++it) {
            std::wstring src = std::wstring(L"classDiagram\n") + ((rnd() & 3) == 0 ? L"direction LR\n" : L"");
            const int count = 1 + static_cast<int>(rnd() % 12);
            int open = 0;
            for (int k = 0; k < count * 2; ++k) {
                const std::wstring a = L"C" + std::to_wstring(rnd() % count), b = L"C" + std::to_wstring(rnd() % count);
                switch (rnd() % 9) {
                case 0:
                    if (open < 3) {
                        src += L"namespace NS" + std::to_wstring(k) + L" {\n";
                        ++open;
                    }
                    break;
                case 1:
                    if (open > 0) {
                        src += L"}\n";
                        --open;
                    }
                    break;
                case 2:
                    src += L"class " + a + ((rnd() & 1) ? L"~T~" : L"") + L" {\n";
                    for (int m = static_cast<int>(rnd() % 4); m > 0; --m) src += std::wstring(kMembers[rnd() % std::size(kMembers)]) + L"\n";
                    src += L"}\n";
                    break;
                case 3: src += a + L" : " + kMembers[rnd() % std::size(kMembers)] + L"\n"; break;
                case 4: src += L"note for " + a + L" \"note " + std::to_wstring(k) + L"\"\n"; break;
                default:
                    src += a + ((rnd() & 3) == 0 ? L" \"1\" " : L" ") + kRelations[rnd() % std::size(kRelations)] +
                           ((rnd() & 3) == 0 ? L" \"*\" " : L" ") + b + ((rnd() & 1) ? L" : label" : L"") + L"\n";
                    break;
                }
            }
            while (open-- > 0) src += L"}\n";
            std::wstring error;
            const auto scene = layout(src, &error);
            if (!scene || !sane(*scene)) {
                std::printf("FAIL: generated %s: %s\n", WideToUtf8(src.substr(0, 200)).c_str(), WideToUtf8(error).c_str());
                ++failures;
            }
        }
        std::printf("%d generated class diagrams in %.0f ms\n", iterations / 4, NowMs() - k0);
        // Well-formed random state diagrams.
        const double s0 = NowMs();
        for (int it = 0; it < iterations / 4; ++it) {
            std::wstring src = std::wstring(L"stateDiagram-v2\n") + ((rnd() & 3) == 0 ? L"direction LR\n" : L"");
            const int count = 1 + static_cast<int>(rnd() % 10);
            int open = 0;
            std::vector<bool> regions;  // per open composite: may split
            for (int k = 0; k < count * 3; ++k) {
                const std::wstring a = L"S" + std::to_wstring(rnd() % count), b = L"S" + std::to_wstring(rnd() % count);
                switch (rnd() % 10) {
                case 0:
                    if (open < 4) {
                        src += L"state C" + std::to_wstring(k) + L" {\n";
                        ++open;
                    }
                    break;
                case 1:
                    if (open > 0) {
                        src += L"}\n";
                        --open;
                    }
                    break;
                case 2:
                    if (open > 0) src += L"--\n";
                    break;
                case 3: src += L"[*] --> " + a + L"\n"; break;
                case 4: src += a + L" --> [*]\n"; break;
                case 5: src += L"note " + std::wstring(rnd() & 1 ? L"left" : L"right") + L" of " + a + L" : note\n"; break;
                case 6: src += L"state x" + std::to_wstring(k) + (rnd() & 1 ? L" <<choice>>\n" : L" <<fork>>\n") + a + L" --> x" + std::to_wstring(k) + L"\nx" + std::to_wstring(k) + L" --> " + b + L"\n"; break;
                case 7: src += a + L" : a description\n"; break;
                default: src += a + L" --> " + b + ((rnd() & 1) ? L" : event" : L"") + L"\n"; break;
                }
            }
            while (open-- > 0) src += L"}\n";
            std::wstring error;
            const auto scene = layout(src, &error);
            if (!scene || !sane(*scene)) {
                std::printf("FAIL: generated %s: %s\n", WideToUtf8(src.substr(0, 200)).c_str(), WideToUtf8(error).c_str());
                ++failures;
            }
        }
        std::printf("%d generated state diagrams in %.0f ms\n", iterations / 4, NowMs() - s0);
        // Well-formed random ER diagrams.
        static const wchar_t* kCards[][2] = {{L"|o", L"o|"}, {L"||", L"||"}, {L"}o", L"o{"}, {L"}|", L"|{"}};
        const double e0 = NowMs();
        for (int it = 0; it < iterations / 4; ++it) {
            std::wstring src = std::wstring(L"erDiagram\n") + ((rnd() & 3) == 0 ? L"direction LR\n" : L"");
            const int count = 1 + static_cast<int>(rnd() % 10);
            for (int k = 0; k < count * 2; ++k) {
                const std::wstring a = L"E" + std::to_wstring(rnd() % count), b = L"E" + std::to_wstring(rnd() % count);
                if (rnd() % 4 == 0) {
                    src += a + L" {\n";
                    for (int m = static_cast<int>(rnd() % 5); m > 0; --m) {
                        src += std::wstring(rnd() & 1 ? L"string" : L"int(10)") + L" f" + std::to_wstring(m) +
                               (rnd() & 1 ? L" PK" : L"") + (rnd() & 1 ? L" \"note\"" : L"") + L"\n";
                    }
                    src += L"}\n";
                } else if (rnd() % 6 == 0) {
                    src += L"subgraph G" + std::to_wstring(k % 3) + L"\n" + a + L" ||--|{ " + b + L" : in\nend\n";
                } else if (rnd() % 8 == 0) {
                    src += L"G" + std::to_wstring(rnd() % 3) + L" |o--o{ " + b + L" : box\n";
                } else {
                    src += a + L" " + kCards[rnd() % 4][0] + (rnd() & 1 ? L"--" : L"..") + kCards[rnd() % 4][1] + L" " + b + L" : rel\n";
                }
            }
            std::wstring error;
            const auto scene = layout(src, &error);
            if (!scene || !sane(*scene)) {
                std::printf("FAIL: generated %s: %s\n", WideToUtf8(src.substr(0, 200)).c_str(), WideToUtf8(error).c_str());
                ++failures;
            }
        }
        std::printf("%d generated ER diagrams in %.0f ms\n", iterations / 4, NowMs() - e0);
        // The layered layout on random clustered graphs: members inside their
        // box, nothing else in it, boxes apart unless nested.
        const double c0 = NowMs();
        for (int it = 0; it < iterations; ++it) {
            LayeredGraph g;
            g.dir = static_cast<RankDir>(rnd() % 4);
            const int nc = static_cast<int>(rnd() % 5);
            for (int c = 0; c < nc; ++c) {
                LayeredGraph::Cluster cl;
                cl.parent = c > 0 && rnd() % 3 == 0 ? static_cast<int>(rnd() % static_cast<uint32_t>(c)) : -1;
                cl.title_w = rnd() % 2 ? static_cast<float>(40 + rnd() % 200) : 0.f;
                cl.title_h = cl.title_w > 0 ? 20.f : 0.f;
                cl.share_ranks = cl.title_w <= 0 && cl.parent >= 0 && rnd() % 2;  // a concurrent region
                if (rnd() % 6 == 0) cl.after = 50;  // a composite state's loops
                g.clusters.push_back(cl);
            }
            const int n = 2 + static_cast<int>(rnd() % 12);
            for (int i = 0; i < n; ++i) {
                LayeredGraph::Node nd;
                nd.w = static_cast<float>(30 + rnd() % 80);
                nd.h = static_cast<float>(20 + rnd() % 40);
                nd.cluster = nc > 0 && rnd() % 2 ? static_cast<int>(rnd() % static_cast<uint32_t>(nc)) : -1;
                if (rnd() % 8 == 0) nd.after = 40;
                g.nodes.push_back(nd);
            }
            for (int i = static_cast<int>(rnd() % static_cast<uint32_t>(2 * n)); i > 0; --i) {
                LayeredGraph::Edge e;
                e.from = static_cast<int>(rnd() % static_cast<uint32_t>(n));
                e.to = static_cast<int>(rnd() % static_cast<uint32_t>(n));
                if (e.from == e.to) continue;
                if (rnd() % 4 == 0) {
                    e.label_w = 40;
                    e.label_h = 18;
                }
                // Now and then a box as an end (a subgraph or composite state).
                if (rnd() % 4 == 0) e.from_cluster = g.nodes[static_cast<size_t>(e.from)].cluster;
                if (rnd() % 4 == 0) e.to_cluster = g.nodes[static_cast<size_t>(e.to)].cluster;
                g.edges.push_back(e);
            }
            const LayeredLayout l = LayoutLayered(g);
            auto in = [&](int v, int c) {
                for (int k = g.nodes[static_cast<size_t>(v)].cluster; k >= 0; k = g.clusters[static_cast<size_t>(k)].parent) {
                    if (k == c) return true;
                }
                return false;
            };
            auto inside = [&](int a, int b) {  // box b inside box a
                for (int k = b; k >= 0; k = g.clusters[static_cast<size_t>(k)].parent) {
                    if (k == a) return true;
                }
                return false;
            };
            auto meet = [](const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
                return a.left < b.right - 0.5f && b.left < a.right - 0.5f && a.top < b.bottom - 0.5f && b.top < a.bottom - 0.5f;
            };
            auto empty = [](const D2D1_RECT_F& r) { return r.right <= r.left || r.bottom <= r.top; };
            std::string bad;
            for (int c = 0; c < nc && bad.empty(); ++c) {
                const D2D1_RECT_F& box = l.clusters[static_cast<size_t>(c)];
                if (empty(box)) continue;
                for (int v = 0; v < n && bad.empty(); ++v) {
                    const D2D1_POINT_2F p = l.centers[static_cast<size_t>(v)];
                    const float hw = g.nodes[static_cast<size_t>(v)].w / 2, hh = g.nodes[static_cast<size_t>(v)].h / 2;
                    const D2D1_RECT_F r = D2D1::RectF(p.x - hw, p.y - hh, p.x + hw, p.y + hh);
                    if (in(v, c)) {
                        if (r.left < box.left - 0.5f || r.right > box.right + 0.5f || r.top < box.top - 0.5f || r.bottom > box.bottom + 0.5f) {
                            bad = "a member outside its box";
                        }
                    } else if (meet(r, box)) {
                        bad = "a node inside another box";
                    }
                }
                for (int d = c + 1; d < nc && bad.empty(); ++d) {
                    const D2D1_RECT_F& other = l.clusters[static_cast<size_t>(d)];
                    if (!empty(other) && !inside(c, d) && !inside(d, c) && meet(box, other)) bad = "boxes overlap";
                }
                // The room beside a box: nothing else in it.
                const float room = g.clusters[static_cast<size_t>(c)].after;
                if (room > 0 && bad.empty()) {
                    const bool side = g.dir == RankDir::LR || g.dir == RankDir::RL;
                    const D2D1_RECT_F r = side ? D2D1::RectF(box.left, box.bottom, box.right, box.bottom + room)
                                               : D2D1::RectF(box.right, box.top, box.right + room, box.bottom);
                    for (int v = 0; v < n && bad.empty(); ++v) {
                        const D2D1_POINT_2F p = l.centers[static_cast<size_t>(v)];
                        const float hw = g.nodes[static_cast<size_t>(v)].w / 2, hh = g.nodes[static_cast<size_t>(v)].h / 2;
                        if (meet(r, D2D1::RectF(p.x - hw, p.y - hh, p.x + hw, p.y + hh))) bad = "a node beside a box, in its room";
                    }
                    for (int d = 0; d < nc && bad.empty(); ++d) {
                        const D2D1_RECT_F& other = l.clusters[static_cast<size_t>(d)];
                        if (d != c && !empty(other) && !inside(d, c) && meet(r, other)) bad = "a box beside a box, in its room";
                    }
                }
            }
            if (!bad.empty()) {
                std::printf("FAIL: layout of random graph %d (dir %d, %d nodes, %d clusters): %s\n", it, static_cast<int>(g.dir), n, nc,
                            bad.c_str());
                ++failures;
            }
        }
        std::printf("%d random clustered layouts checked in %.0f ms\n", iterations, NowMs() - c0);
        std::printf(failures ? "mermaid FAILED\n" : "mermaid OK\n");
        return failures ? 1 : 0;
    }
    if (cmd == "mmd" && argc >= 4) {
        // mmd <diagram.mmd> <out.png> [--dark] [--scale S]: lays out a Mermaid
        // diagram and renders it alone.
        bool dark = false;
        float scale = 1.5f;
        for (int i = 4; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--dark")) dark = true;
            else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = static_cast<float>(std::atof(argv[++i]));
        }
        std::string bytes;
        if (!ReadFileBytes(Utf8ToWide(argv[2]).c_str(), bytes, 1 << 20)) {
            std::fprintf(stderr, "cannot read %s\n", argv[2]);
            return 1;
        }
        const std::wstring source = Utf8ToWide(DecodeTextToUtf8(bytes));
        ComPtr<IDWriteFactory> dwf;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwf.GetAddressOf()));
        std::wstring error;
        const double t0 = NowMs();
        const bool ok = CheckMermaid(source, &error);
        const double t1 = NowMs();
        auto scene = ok ? LayoutMermaid(source, dwf.Get(), L"Segoe UI", 16.f, &error) : nullptr;
        const double t2 = NowMs();
        if (!scene) {
            std::printf("error: %s\n", WideToUtf8(error).c_str());
            return 1;
        }
        std::printf("check %.1f ms, layout %.1f ms, %.0fx%.0f, %zu shapes, %zu texts\n", t1 - t0, t2 - t1, scene->width,
                    scene->height, scene->shapes.size(), scene->texts.size());
        ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        ComPtr<ID2D1Factory> d2d;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
        const UINT w = static_cast<UINT>(std::ceil(scene->width * scale)), h = static_cast<UINT>(std::ceil(scene->height * scale));
        ComPtr<IWICBitmap> bmp;
        ComPtr<ID2D1RenderTarget> rt;
        if (!wic || !d2d || FAILED(wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp)) ||
            FAILED(d2d->CreateWicBitmapRenderTarget(
                bmp.Get(),
                D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                             96.f * scale, 96.f * scale),
                &rt))) {
            std::fprintf(stderr, "cannot create the bitmap\n");
            return 1;
        }
        const uint32_t page = DiagramRoleArgb(kDgPage, dark);
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(page & 0xFFFFFF));
        DrawDiagram(rt.Get(), *scene, D2D1::RectF(0, 0, scene->width, scene->height), dark, nullptr, nullptr);
        rt->EndDraw();
        std::printf("draw %.1f ms\n", NowMs() - t2);
        return SaveBitmapPng(bmp.Get(), Utf8ToWide(argv[3])) ? 0 : 1;
    }
    if (cmd == "tex") {
        // tex "<TeX source>": the parsed formula tree.
        std::printf("%s\n", WideToUtf8(DumpMath(ParseTex(Utf8ToWide(argv[2])))).c_str());
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
            // TeX inside $...$ (typeset by mathlayout).
            "$\\frac{a}{b}$", "$$\\sum_{i=1}^n$$", "\\left(", "\\right)", "\\begin{pmatrix}",
            "\\end{pmatrix}", "&", "\\\\", "\\sqrt[", "^{", "_{", "\\text{", "\\color{red}", "```math\n",
            // Regional indicators (flags pair them up) and tag characters.
            "\xF0\x9F\x87\xBA", "\xF0\x9F\x87\xB8", "\xF0\x9F\x87\xA6", "\xF0\x9F\x8F\xB4",
            "\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2", "\xF3\xA0\x81\xBF", ":us:",
            // Mermaid diagrams (and their pieces).
            "```mermaid\nflowchart TD\nA-->B\n```\n", "```mermaid\ngraph LR\nA[x] -- y --> B((z))\n```\n",
            "```mermaid\n", "flowchart TD\n", "A --> B\n", "subgraph S\n", "end\n", "classDef c fill:#f00\n",
            "```mermaid\nsequenceDiagram\nA->>+B: hi\nB-->>-A: ok\n```\n", "sequenceDiagram\n", "A->>B: m\n",
            "loop x\n", "Note over A,B: n\n", "autonumber\n",
        };
        const size_t n_pieces = sizeof(kPieces) / sizeof(kPieces[0]);
        // YAML front matter fragments (a third of the documents start with one).
        static const char* kYaml[] = {
            "key: ", "k2:", "value", "\n", "  ", "    ", "- ", "-", "[", "]", "{", "}", ", ", "'", "\"", "\\\"",
            "|", "|-\n", ">", ">+\n", "# c", ":", ": ", "&a ", "*a", "!tag ", "\t", "  - k: v\n", "...\n",
            "---\n", "\xD0\x9A\xD0\xBB\xD1\x8E\xD1\x87: ", "<b>&amp;", "```", "\r\n", "a: [b, {c: d}]\n",
        };
        const size_t n_yaml = sizeof(kYaml) / sizeof(kYaml[0]);
        double total = 0;
        for (int it = 0; it < iterations; ++it) {
            std::string md;
            if (rnd() % 3 == 0) {
                md = "---\nkey: v\n";
                const int ylen = static_cast<int>(rnd() % 60);
                for (int k = 0; k < ylen; ++k) md += kYaml[rnd() % n_yaml];
                md += "\n---\n";
            }
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
            o.layout.line_numbers = (rnd() & 1) != 0;
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
