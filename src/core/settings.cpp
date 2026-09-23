#include "core/settings.h"

#include <windows.h>

#include <algorithm>

#include "core/util.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mdv {
namespace {

constexpr const wchar_t* kSection = L"mdview";

std::wstring& PathStorage() {
    static std::wstring path;
    return path;
}

int ReadInt(const wchar_t* key, int def) {
    return static_cast<int>(GetPrivateProfileIntW(kSection, key, def, SettingsPath().c_str()));
}

std::wstring ReadString(const wchar_t* key, const std::wstring& def) {
    wchar_t buf[256];
    GetPrivateProfileStringW(kSection, key, def.c_str(), buf, static_cast<DWORD>(std::size(buf)),
                             SettingsPath().c_str());
    return buf;
}

void WriteInt(const wchar_t* key, int v) {
    WritePrivateProfileStringW(kSection, key, std::to_wstring(v).c_str(), SettingsPath().c_str());
}

void WriteString(const wchar_t* key, const std::wstring& v) {
    WritePrivateProfileStringW(kSection, key, v.c_str(), SettingsPath().c_str());
}

}  // namespace

Settings& GlobalSettings() {
    static Settings settings;
    return settings;
}

void SetSettingsPath(const std::wstring& ini_path) {
    // Store our settings in our own file next to the ini TC suggests.
    PathStorage() = DirectoryOf(ini_path) + L"mdview.ini";
}

const std::wstring& SettingsPath() {
    std::wstring& p = PathStorage();
    if (p.empty()) {
        wchar_t module[MAX_PATH * 2] = {};
        GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), module, static_cast<DWORD>(std::size(module)));
        p = DirectoryOf(module) + L"mdview.ini";
    }
    return p;
}

void LoadSettings() {
    Settings& s = GlobalSettings();
    const Settings d;
    s.theme = static_cast<ThemeMode>(std::clamp(ReadInt(L"Theme", static_cast<int>(d.theme)), 0, 2));
    s.font_family = ReadString(L"Font", d.font_family);
    s.mono_family = ReadString(L"MonoFont", d.mono_family);
    s.font_size = static_cast<float>(std::clamp(ReadInt(L"FontSize", static_cast<int>(d.font_size)), 8, 40));
    s.zoom_percent = std::clamp(ReadInt(L"Zoom", d.zoom_percent), 30, 400);
    s.max_width = std::clamp(ReadInt(L"MaxWidth", d.max_width), 0, 10000);
    s.remote_images = ReadInt(L"RemoteImages", d.remote_images) != 0;
    s.smooth_scroll = ReadInt(L"SmoothScroll", d.smooth_scroll) != 0;
    s.emoji_shortcodes = ReadInt(L"EmojiShortcodes", d.emoji_shortcodes) != 0;
    s.math = ReadInt(L"Math", d.math) != 0;
    s.mermaid = ReadInt(L"Mermaid", d.mermaid) != 0;
    s.syntax_highlight = ReadInt(L"SyntaxHighlight", d.syntax_highlight) != 0;
    s.hardware = ReadInt(L"HardwareRendering", d.hardware) != 0;
    s.wrap_code = std::clamp(ReadInt(L"WrapCode", d.wrap_code), -1, 1);
    s.line_numbers = ReadInt(L"LineNumbers", d.line_numbers) != 0;
    s.auto_reload = ReadInt(L"AutoReload", d.auto_reload) != 0;
    if (s.font_family.empty()) s.font_family = d.font_family;
}

void SaveSettings() {
    const Settings& s = GlobalSettings();
    WriteInt(L"Theme", static_cast<int>(s.theme));
    WriteString(L"Font", s.font_family);
    WriteString(L"MonoFont", s.mono_family);
    WriteInt(L"FontSize", static_cast<int>(s.font_size));
    WriteInt(L"Zoom", s.zoom_percent);
    WriteInt(L"MaxWidth", s.max_width);
    WriteInt(L"RemoteImages", s.remote_images ? 1 : 0);
    WriteInt(L"SmoothScroll", s.smooth_scroll ? 1 : 0);
    WriteInt(L"EmojiShortcodes", s.emoji_shortcodes ? 1 : 0);
    WriteInt(L"Math", s.math ? 1 : 0);
    WriteInt(L"Mermaid", s.mermaid ? 1 : 0);
    WriteInt(L"SyntaxHighlight", s.syntax_highlight ? 1 : 0);
    WriteInt(L"HardwareRendering", s.hardware ? 1 : 0);
    WriteInt(L"WrapCode", s.wrap_code);
    WriteInt(L"LineNumbers", s.line_numbers ? 1 : 0);
    WriteInt(L"AutoReload", s.auto_reload ? 1 : 0);
}

}  // namespace mdv
