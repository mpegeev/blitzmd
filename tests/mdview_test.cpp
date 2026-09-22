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

#include "core/offscreen.h"
#include "core/parser.h"
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
        SendMessageW(hwnd, msg, wp, lp);
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
