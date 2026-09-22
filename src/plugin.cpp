// mdview — GitHub Flavored Markdown viewer plugin (WLX) for Total Commander.
//
// This file only contains the Lister plugin entry points; everything else
// lives in src/core. Every export catches all exceptions: nothing may unwind
// into Total Commander.

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "core/offscreen.h"
#include "core/parser.h"
#include "core/settings.h"
#include "core/util.h"
#include "core/viewer.h"

using namespace mdv;

namespace {

constexpr char kDetectString[] =
    R"(EXT="MD" | EXT="MARKDOWN" | EXT="MDOWN" | EXT="MKD" | EXT="MKDN" | EXT="MDWN" | EXT="MDTXT" | EXT="MDTEXT" | EXT="RMD")";

struct ListDefaultParamStruct {
    int size;
    DWORD PluginInterfaceVersionLow;
    DWORD PluginInterfaceVersionHi;
    char DefaultIniName[MAX_PATH];
};

void EnsureSettings() {
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        LoadSettings();
    }
}

std::wstring AnsiToWide(const char* s) {
    if (!s) return {};
    const int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    return w;
}

HBITMAP PreviewBitmap(const std::wstring& path, int width, int height, const char* buf, int buflen) {
    if (width <= 0 || height <= 0) return nullptr;
    // A thumbnail only shows the beginning of the document: parse up to 64 KB.
    std::string bytes;
    if (!ReadFileBytes(path.c_str(), bytes, 64ull << 20)) {
        if (!buf || buflen <= 0) return nullptr;
        bytes.assign(buf, static_cast<size_t>(buflen));
    }
    if (bytes.size() > 65536) {
        bytes.resize(65536);
        const size_t nl = bytes.rfind('\n');
        if (nl != std::string::npos && nl > 32768) bytes.resize(nl + 1);
    }
    const std::string utf8 = DecodeTextToUtf8(bytes);
    ParseOptions opt;
    auto doc = ParseMarkdown(utf8, path, opt);

    // Render a "page" at a readable width and let TC scale it down.
    OffscreenOptions o;
    o.width = 640.f;
    o.layout.font_size = 16.f;
    o.layout.max_content_width = 0;
    o.layout.dark = GlobalSettings().theme == ThemeMode::Dark;
    o.allow_remote_images = false;
    const float aspect = static_cast<float>(height) / static_cast<float>(width);
    o.max_height = o.width * std::max(aspect, 0.5f);
    const float scale = std::min(1.f, static_cast<float>(width) / o.width);
    o.scale = scale;
    ComPtr<IWICBitmap> bmp = RenderToBitmap(*doc, o);
    return bmp ? BitmapToHBitmap(bmp.Get()) : nullptr;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}

extern "C" {

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    if (!DetectString || maxlen <= 0) return;
    const size_t n = std::min(std::strlen(kDetectString), static_cast<size_t>(maxlen - 1));
    std::memcpy(DetectString, kDetectString, n);
    DetectString[n] = '\0';
}

void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps) {
    try {
        if (!dps) return;
        Host().interface_hi = dps->PluginInterfaceVersionHi;
        Host().interface_lo = dps->PluginInterfaceVersionLow;
        const std::wstring ini = AnsiToWide(dps->DefaultIniName);
        if (!ini.empty()) SetSettingsPath(ini);
        LoadSettings();
    } catch (...) {
    }
}

HWND __stdcall ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags) {
    try {
        EnsureSettings();
        if (!FileToLoad) return nullptr;
        return CreateViewer(ParentWin, FileToLoad, ShowFlags);
    } catch (...) {
        return nullptr;
    }
}

HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags) {
    try {
        const std::wstring w = AnsiToWide(FileToLoad);
        return ListLoadW(ParentWin, const_cast<WCHAR*>(w.c_str()), ShowFlags);
    } catch (...) {
        return nullptr;
    }
}

int __stdcall ListLoadNextW(HWND /*ParentWin*/, HWND PluginWin, WCHAR* FileToLoad, int ShowFlags) {
    try {
        if (!FileToLoad) return 1;
        return ViewerLoadFile(PluginWin, FileToLoad, ShowFlags) ? 0 : 1;
    } catch (...) {
        return 1;
    }
}

int __stdcall ListLoadNext(HWND ParentWin, HWND PluginWin, char* FileToLoad, int ShowFlags) {
    try {
        const std::wstring w = AnsiToWide(FileToLoad);
        return ListLoadNextW(ParentWin, PluginWin, const_cast<WCHAR*>(w.c_str()), ShowFlags);
    } catch (...) {
        return 1;
    }
}

void __stdcall ListCloseWindow(HWND ListWin) {
    if (ListWin && IsWindow(ListWin)) DestroyWindow(ListWin);
}

int __stdcall ListSearchTextW(HWND ListWin, WCHAR* SearchString, int SearchParameter) {
    try {
        return ViewerSearch(ListWin, SearchString, SearchParameter) ? 0 : 1;
    } catch (...) {
        return 1;
    }
}

int __stdcall ListSearchText(HWND ListWin, char* SearchString, int SearchParameter) {
    try {
        const std::wstring w = AnsiToWide(SearchString);
        return ListSearchTextW(ListWin, const_cast<WCHAR*>(w.c_str()), SearchParameter);
    } catch (...) {
        return 1;
    }
}

int __stdcall ListSendCommand(HWND ListWin, int Command, int Parameter) {
    try {
        return ViewerCommand(ListWin, Command, Parameter) ? 0 : 1;
    } catch (...) {
        return 1;
    }
}

int __stdcall ListPrintW(HWND ListWin, WCHAR* /*FileToPrint*/, WCHAR* DefPrinter, int /*PrintFlags*/,
                         RECT* Margins) {
    try {
        return ViewerPrint(ListWin, DefPrinter, Margins) ? 0 : 1;
    } catch (...) {
        return 1;
    }
}

int __stdcall ListPrint(HWND ListWin, char* FileToPrint, char* DefPrinter, int PrintFlags, RECT* Margins) {
    try {
        const std::wstring file = AnsiToWide(FileToPrint);
        const std::wstring printer = AnsiToWide(DefPrinter);
        return ListPrintW(ListWin, const_cast<WCHAR*>(file.c_str()),
                          DefPrinter ? const_cast<WCHAR*>(printer.c_str()) : nullptr, PrintFlags, Margins);
    } catch (...) {
        return 1;
    }
}

HBITMAP __stdcall ListGetPreviewBitmapW(WCHAR* FileToLoad, int width, int height, char* contentbuf,
                                        int contentbuflen) {
    // Called on a background thread: initialize COM for WIC/Direct2D.
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HBITMAP result = nullptr;
    try {
        EnsureSettings();
        if (FileToLoad) result = PreviewBitmap(FileToLoad, width, height, contentbuf, contentbuflen);
    } catch (...) {
        result = nullptr;
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return result;
}

HBITMAP __stdcall ListGetPreviewBitmap(char* FileToLoad, int width, int height, char* contentbuf,
                                       int contentbuflen) {
    try {
        const std::wstring w = AnsiToWide(FileToLoad);
        return ListGetPreviewBitmapW(const_cast<WCHAR*>(w.c_str()), width, height, contentbuf, contentbuflen);
    } catch (...) {
        return nullptr;
    }
}

}  // extern "C"
