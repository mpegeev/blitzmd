// Plugin settings, stored in mdview.ini next to Total Commander's plugin ini
// (see ListSetDefaultParams). All values have sensible defaults; the file is
// only written when the user changes something from the context menu.
#pragma once

#include <string>

namespace mdv {

enum class ThemeMode : int { Auto = 0, Light = 1, Dark = 2 };

struct Settings {
    ThemeMode theme = ThemeMode::Auto;   // Auto: follow Total Commander (or Windows)
    std::wstring font_family = L"Segoe UI";
    std::wstring mono_family;            // empty: Cascadia Mono, else Consolas
    float font_size = 16.f;              // DIPs at 100% zoom (GitHub uses 16px)
    int zoom_percent = 100;
    int max_width = 900;                 // content column in DIPs; 0 = full width
    bool remote_images = true;           // download http(s) images (badges...)
    bool smooth_scroll = true;
    bool emoji_shortcodes = true;
    bool math = true;
    bool syntax_highlight = true;
    bool hardware = true;                // GPU rendering (Direct2D hardware RT)
    int wrap_code = -1;                  // -1: follow Lister's wrap option, 0/1 forced
};

// Process-wide settings instance.
Settings& GlobalSettings();

// Location of the ini file (set from ListSetDefaultParams, else next to the DLL).
void SetSettingsPath(const std::wstring& ini_path);
const std::wstring& SettingsPath();

void LoadSettings();
void SaveSettings();

}  // namespace mdv
