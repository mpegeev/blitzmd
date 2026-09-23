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
#include "core/images.h"
#include "core/layout.h"
#include "core/mathlayout.h"
#include "core/theme.h"
#include "core/offscreen.h"
#include "core/parser.h"
#include "core/print.h"
#include "core/svg.h"
#include "core/util.h"
#include "html_dump.h"
#include "spec_runner.h"

#pragma comment(lib, "psapi.lib")

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
