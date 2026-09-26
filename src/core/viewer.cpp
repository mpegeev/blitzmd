#include "core/viewer.h"

#include <d2d1.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwctype>

#include "core/images.h"
#include "core/layout.h"
#include "core/offscreen.h"
#include "core/parser.h"
#include "core/print.h"
#include "core/render.h"
#include "core/settings.h"
#include "core/settings_dialog.h"
#include "core/theme.h"
#include "core/zoom.h"

#pragma comment(lib, "dwmapi.lib")

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mdv {
namespace {

constexpr wchar_t kClassName[] = L"MdViewWlxWindow";
constexpr UINT_PTR kTimerRefine = 1;
constexpr UINT_PTR kTimerCopied = 2;
constexpr UINT_PTR kTimerAutoScroll = 3;
constexpr UINT_PTR kTimerRepeat = 4;
constexpr UINT_PTR kTimerGif = 5;
constexpr UINT_PTR kTimerReload = 6;    // debounce after a change on disk
constexpr UINT_PTR kTimerWatchPoll = 7; // network drives: notifications are unreliable
constexpr UINT kReloadDelayMs = 300;    // editors save in several steps
constexpr UINT kWatchPollMs = 2000;
constexpr uint64_t kMaxFileBytes = 32ull << 20;  // larger files: the beginning is shown

// Shared Direct2D factory for all viewer windows (all live on TC's UI thread).
struct Shared {
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dw;
    std::vector<Viewer*> viewers;
};
Shared& G() {
    static Shared g;
    return g;
}

bool IsWordChar(wchar_t c) {
    return c == L'_' || IsCharAlphaNumericW(c) || (c >= 0x0300 && c <= 0x036F);
}

bool IsMarkdownPath(std::wstring_view p) {
    const size_t dot = p.find_last_of(L'.');
    if (dot == std::wstring_view::npos) return false;
    const std::wstring_view ext = p.substr(dot + 1);
    for (const wchar_t* e : {L"md", L"markdown", L"mdown", L"mkd", L"mkdn", L"mdwn", L"mdtxt", L"mdtext", L"rmd"}) {
        if (EqualsNoCase(ext, e)) return true;
    }
    return false;
}

std::wstring_view ExtensionOf(std::wstring_view p) {
    const size_t dot = p.find_last_of(L'.');
    const size_t slash = p.find_last_of(L"\\/");
    if (dot == std::wstring_view::npos || (slash != std::wstring_view::npos && dot < slash)) return {};
    return p.substr(dot + 1);
}

// Files that run code when opened (confirmation required).
bool IsExecutablePath(std::wstring_view p) {
    const std::wstring_view ext = ExtensionOf(p);
    if (ext.empty()) return false;
    for (const wchar_t* e : {L"exe", L"com", L"bat", L"cmd", L"scr", L"pif", L"msi", L"msp", L"mst", L"ps1",
                             L"psm1", L"psd1", L"ps1xml", L"psc1", L"vb", L"vbs", L"vbe", L"js", L"jse", L"ws",
                             L"wsf", L"wsh", L"wsc", L"hta", L"cpl", L"lnk", L"reg", L"jar", L"application",
                             L"appref-ms", L"url", L"website", L"inf", L"ins", L"isp", L"sct", L"msc", L"dll",
                             L"ocx", L"sys", L"scf", L"shs", L"chm", L"hlp", L"gadget", L"xbap", L"xll",
                             L"settingcontent-ms", L"diagcab", L"library-ms", L"search-ms", L"appx",
                             L"appxbundle", L"msix", L"msixbundle", L"py", L"pyw", L"pyz", L"pl", L"rb", L"sh",
                             L"ahk", L"au3"}) {
        if (EqualsNoCase(ext, e)) return true;
    }
    return AssocIsDangerous((L"." + std::wstring(ext)).c_str()) != FALSE;
}

bool IsImagePath(std::wstring_view p) {
    const std::wstring_view ext = ExtensionOf(p);
    for (const wchar_t* e : {L"png", L"apng", L"jpg", L"jpeg", L"jpe", L"jfif", L"gif", L"bmp", L"dib", L"ico",
                             L"cur", L"tif", L"tiff", L"webp", L"svg", L"svgz", L"avif", L"heic", L"heif", L"jxr",
                             L"wdp", L"jxl", L"dds"}) {
        if (EqualsNoCase(ext, e)) return true;
    }
    return false;
}

// Opens exactly the file or folder `path` with its default verb. A path
// string won't do: without an extension ShellExecute looks for `path` plus
// each PATHEXT extension next to it and runs what it finds (a link to LICENSE
// would run LICENSE.cmd); an item ID list names the item itself.
bool OpenShellItem(HWND owner, const std::wstring& path) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr)) || !pidl) return false;
    const std::wstring dir = DirectoryOf(path);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_IDLIST;
    sei.hwnd = owner;
    sei.lpIDList = pidl;
    sei.lpDirectory = dir.empty() ? nullptr : dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    const bool ok = ShellExecuteExW(&sei) != FALSE;
    CoTaskMemFree(pidl);
    return ok;
}

// Posted by the file watch (a thread-pool wait) to the viewer window.
UINT FileChangedMessage() {
    static const UINT msg = RegisterWindowMessageW(L"MdViewWlxFileChanged");
    return msg;
}

// A poll of the file's stamp is done: wParam 1 if it changed, lParam the
// watch it belongs to (Viewer::watch_gen_).
UINT PollDoneMessage() {
    static const UINT msg = RegisterWindowMessageW(L"MdViewWlxPollDone");
    return msg;
}

// Polls a file on a network share: GetFileAttributesEx on the thread pool,
// since a share that stopped answering blocks it for the SMB timeout. The
// check owns everything it needs, so it may outlive the viewer.
struct PollCheck {
    HWND hwnd;
    std::wstring path;  // extended
    uint64_t time, size;
    uint32_t gen;
};

void CALLBACK PollCallback(PTP_CALLBACK_INSTANCE, void* context) {
    std::unique_ptr<PollCheck> c(static_cast<PollCheck*>(context));
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    bool changed = false;
    if (GetFileAttributesExW(c->path.c_str(), GetFileExInfoStandard, &fa)) {
        const uint64_t t = (static_cast<uint64_t>(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
        const uint64_t size = (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
        changed = t != c->time || size != c->size;
    }
    PostMessageW(c->hwnd, PollDoneMessage(), changed ? 1 : 0, static_cast<LPARAM>(c->gen));
}

// GetDpiForWindow exists since Windows 10 1607; resolved dynamically so the
// plugin still loads on older systems.
UINT WindowDpi(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static const Fn fn = reinterpret_cast<Fn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    UINT dpi = fn ? fn(hwnd) : 0;
    if (dpi == 0) {
        HDC dc = GetDC(hwnd);
        dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
        ReleaseDC(hwnd, dc);
    }
    return dpi ? dpi : 96;
}

enum MenuId : UINT {
    kMenuCopy = 100,
    kMenuCopyLink,
    kMenuCopyImage,
    kMenuCopyCode,
    kMenuSelectAll,
    kMenuZoomIn,
    kMenuZoomOut,
    kMenuZoomReset,
    kMenuThemeAuto,
    kMenuThemeLight,
    kMenuThemeDark,
    kMenuWrap,
    kMenuRemote,
    kMenuFullWidth,
    kMenuBack,
    kMenuOpenLink,
    kMenuOpenExternal,
    kMenuReload,
    kMenuLineNumbers,
    kMenuAutoReload,
    kMenuSettings,
    kMenuAbout,
    kMenuViewFull,
    kMenuResetView,
    kMenuCloseView,
    kMenuTocBase = 1000,
};

constexpr float kWheelZoomStep = 1.25f;   // per wheel notch, zooming images
constexpr float kButtonZoomStep = 1.5f;   // per click on a zoom button
constexpr float kFullViewUiEm = 16.f;     // size of the full-window view's buttons
constexpr DWORD kWheelGestureMs = 500;    // Ctrl+wheel events this close keep their target

// Buttons that can't act now (zoom out and reset at 1x, zoom in at the most):
// a bit per button of the toolbar.
unsigned ObjectButtonsOff(const InlineObject& o) {
    return (IsZoomed(o) ? 0u : 0b101u) | (o.zoom >= kMaxObjectZoom ? 0b10u : 0u);
}

unsigned FullViewButtonsOff(const FullView& f) {
    return (f.scale <= f.min_scale * 1.0001f ? 0b101u : 0u) | (f.scale >= f.max_scale * 0.9999f ? 0b10u : 0u);
}

}  // namespace

HostInfo& Host() {
    static HostInfo info;
    return info;
}

bool SystemUsesDarkTheme() {
    DWORD value = 1, size = sizeof value;
    const LSTATUS s = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return s == ERROR_SUCCESS && value == 0;
}

class Viewer {
public:
    Viewer() = default;
    ~Viewer();

    bool Create(HWND parent, int flags);
    bool Load(const std::wstring& path, int flags, bool keep_history = false);
    bool Search(const std::wstring& text, int flags);
    bool Command(int command, int parameter);
    bool Print(const wchar_t* printer, const RECT* margins);
    void SettingsChanged(const Settings& old);
    HWND hwnd() const { return hwnd_; }
    bool dark() const { return dark_; }

    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    struct Hit {
        Block* leaf = nullptr;
        uint32_t pos = 0;           // global text position
        bool on_text = false;
        int link = -1;
        int image = -1;
        Block* code = nullptr;      // code block under the point
        Block* summary = nullptr;   // <summary> under the point
        uint32_t cluster = 0;       // on_text: start of the character (cluster) under the point
        Block* wide = nullptr;      // horizontally scrollable block under the point
    };
    struct ScrollAnchor {
        Block* leaf = nullptr;
        float offset = 0;
    };
    struct HistoryEntry {
        std::wstring path;
        float scroll = 0;
    };

    LRESULT Proc(UINT msg, WPARAM wp, LPARAM lp);
    void OnPaint();
    void OnSize();
    void OnMouseWheel(int delta, bool horizontal, WORD keys, POINT screen);
    void OnKeyDown(WPARAM vk);
    void OnLButtonDown(int x, int y, bool dbl);
    void OnMouseMove(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnContextMenu(int x, int y);
    void OnTimer(UINT_PTR id);

    // Document / layout
    void ApplySettings(bool relayout);
    LayoutConfig MakeLayoutConfig() const;
    bool DarkTheme() const;
    void Relayout(bool keep_anchor = true);
    void EnsureLayout();
    void EnsureVisibleExact();
    ScrollAnchor CaptureAnchor() const;
    void RestoreAnchor(const ScrollAnchor& a);
    Block* LeafAtY(float y) const;
    float ViewWidth() const;
    float ViewHeight() const;
    float MaxScroll() const;
    float Scale() const { return dpi_ / 96.f; }

    // Rendering
    bool EnsureRenderTarget();
    void DiscardDeviceResources();
    void ScheduleGifFrame();
    void DrawOverlays();
    void Invalidate() { InvalidateRect(hwnd_, nullptr, FALSE); }
    void ResetInteraction();

    // Auto-reload: watch the file's directory, re-read after a change.
    void Reload();
    void StartWatch();
    void StopWatch();
    void OnFileChanged();
    bool FileStamp(uint64_t& write_time, uint64_t& size) const;
    static void CALLBACK WatchCallback(void* hwnd, BOOLEAN timed_out);

    // Scrolling
    void ScrollTo(float y, bool smooth);
    void ScrollBy(float dy, bool smooth) { ScrollTo((anim_ ? anim_to_ : scroll_y_) + dy, smooth); }
    // `target`: where to look instead, in the leaf's text layout.
    void ScrollIntoView(Block* leaf, uint32_t local_pos, bool center, const D2D1_RECT_F* target = nullptr);
    void UpdatePercent();
    float LineStep() const { return layout_.metrics().em * 2.5f; }

    // Hit testing & interaction
    Hit HitTest(float x, float y) const;  // document coordinates
    uint32_t LeafPosFromPoint(Block* leaf, float x, float y, bool* inside, uint32_t* run_link,
                              uint32_t* cluster = nullptr) const;
    TextRange WholeObjects(TextRange r) const;
    float ScrollXOf(const Block* b) const;
    int LinkAt(Block* leaf, uint32_t local) const;
    D2D1_RECT_F ScrollbarTrack() const;
    D2D1_RECT_F ScrollbarThumb() const;
    void SetHoverLink(Block* leaf, int link);
    // Whether viewer `v` still exists after a modal loop (a menu, a message
    // box, a dialog): Total Commander may close it meanwhile, deleting the
    // object. `hwnd` is its window read before the loop. Static, and only the
    // pointer value is compared: `v` may be gone.
    static bool Survived(HWND hwnd, const Viewer* v) { return ViewerFromHwnd(hwnd) == v; }
    void OpenLink(int link_index);
    void NavigateTo(const std::wstring& path, const std::wstring& fragment);
    bool JumpToAnchor(const std::wstring& fragment);
    void GoBack();
    void ToggleDetails(Block* summary);
    void SelectWordAt(const Hit& hit);
    void SelectLeaf(Block* leaf);
    void CopySelection();
    void CopyCode(Block* code);
    void SetZoom(int percent);
    void SetStatus(const std::wstring& text);
    void FlashStatus(const std::wstring& text);  // shown for a moment

    // Zoomable objects (images): inside their box, and in the full-window view.
    const DrawnObject* ObjectAt(float x, float y) const;  // view coordinates
    const DrawnObject* Drawn(const InlineObject* o) const;
    // The label of diagram `diagram` shown for source range [a, b) of it
    // (offsets inside its source), or nullptr.
    const DiagramScene::Text* LabelOver(int32_t diagram, uint32_t a, uint32_t b);
    ZoomToolbar ObjectToolbar(const DrawnObject& d) const;
    void UpdateObjectHover(float x, float y);
    void ZoomObjectBy(const DrawnObject& d, float factor, float x, float y);
    void ObjectButton(const DrawnObject& d, ZoomButton button);
    void ShowZoomStatus(float zoom, const InlineObject* object);
    bool OpenFullView(Block* leaf, const InlineObject* object);
    void CloseFullView();
    D2D1_RECT_F ViewRect() const { return D2D1::RectF(0, 0, ViewWidth(), ViewHeight()); }
    ZoomToolbar FullViewToolbar() const { return MakeZoomToolbar(ViewRect(), kFullViewUiEm, true); }
    void DrawFullView();
    void FullViewButton(ZoomButton button);
    void FullViewKey(WPARAM vk);
    void FullViewMenu(POINT screen);
    void HookKeys(bool on);
    static LRESULT CALLBACK KeyHook(int code, WPARAM wp, LPARAM lp);

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    int flags_ = 0;
    UINT dpi_ = 96;

    std::unique_ptr<Document> doc_;
    std::wstring path_;
    std::vector<HistoryEntry> history_;
    Palette palette_;
    ImageStore images_;
    LayoutEngine layout_;
    Renderer renderer_;
    ComPtr<ID2D1HwndRenderTarget> rt_;
    bool layout_dirty_ = true;
    float laid_out_width_ = 0;
    bool dark_ = false;
    int zoom_percent_ = 100;

    float scroll_y_ = 0;
    bool anim_ = false;
    float anim_from_ = 0, anim_to_ = 0;
    double anim_start_ = 0, anim_duration_ = 0;
    int last_percent_ = -1;
    float wheel_accum_ = 0;

    TextRange selection_;
    uint32_t anchor_ = 0;
    bool selecting_ = false;
    bool mouse_down_ = false;
    bool dragged_ = false;
    POINT down_pt_{};
    int down_link_ = -1;
    int down_image_ = -1;
    Block* down_summary_ = nullptr;
    int click_count_ = 0;
    DWORD last_click_time_ = 0;
    POINT last_click_pt_{};
    POINT last_mouse_{};

    int hover_link_ = -1;
    Block* hover_link_leaf_ = nullptr;
    Block* hover_block_ = nullptr;    // code block or wide table under the mouse
    bool copy_hover_ = false;
    Block* copied_block_ = nullptr;
    bool tracking_mouse_ = false;
    bool thumb_drag_ = false;
    float thumb_drag_offset_ = 0;
    bool scrollbar_hover_ = false;
    Block* hscroll_drag_ = nullptr;
    float hscroll_drag_start_x_ = 0, hscroll_drag_start_scroll_ = 0;
    int repeat_dir_ = 0;
    std::wstring status_;

    std::vector<DrawnObject> drawn_;          // zoomable objects of the last paint
    InlineObject* hover_object_ = nullptr;    // shows its buttons
    int hover_button_ = -1;
    InlineObject* pan_object_ = nullptr;      // zoomed object being dragged
    D2D1_RECT_F pan_box_{};
    float pan_x_ = 0, pan_y_ = 0;             // where the drag started (view DIPs)
    float pan_view_x_ = 0, pan_view_y_ = 0;   // and the object's view then
    FullView full_;                           // full-window view of one object
    bool full_pan_ = false;                   // dragging it
    bool full_backdrop_down_ = false;         // pressed beside it (closes on release)
    float full_last_x_ = 0, full_last_y_ = 0;
    bool key_hook_ = false;                   // KeyHook installed for it
    bool esc_held_ = false;                   // Esc closed the view, not released yet
    DWORD full_closed_at_ = 0;                // a double click there must not reach the page
    InlineObject* wheel_object_ = nullptr;    // Ctrl+wheel gesture: its target (null: the page)
    DWORD wheel_time_ = 0;

    uint32_t doc_gen_ = 0;             // incremented by every Load()
    bool printing_ = false;            // PrintDocument() is using the blocks
    bool reload_after_print_ = false;  // the file or the settings changed while printing
    bool created_ = false;             // CreateWindowEx succeeded: the window owns this object
    // A diagram's labels by the source they come from, sorted by start with
    // the farthest end so far (and its text), built once per scene.
    struct LabelIndex {
        std::shared_ptr<const DiagramScene> scene;
        std::vector<uint32_t> starts;
        std::vector<std::pair<uint32_t, uint32_t>> reach;
    };
    std::unordered_map<int32_t, LabelIndex> label_index_;

    HANDLE watch_change_ = INVALID_HANDLE_VALUE;  // FindFirstChangeNotification
    HANDLE watch_wait_ = nullptr;                 // thread-pool wait on it
    uint64_t file_time_ = 0, file_size_ = 0;      // stamp of the loaded file
    uint32_t watch_gen_ = 0;                      // incremented by StartWatch()
    bool poll_pending_ = false;                   // a PollCheck of this watch is out

    std::wstring search_text_;
    std::vector<TextRange> hits_;
    int current_hit_ = -1;
    std::wstring global_text_;        // document text for searching (lazy)
    std::wstring folded_text_;        // lowercase version of it
    bool folded_valid_ = false;
};

// ============================================================================
// Creation & loading
// ============================================================================

Viewer::~Viewer() {
    HookKeys(false);
    StopWatch();
    images_.Detach();
    layout_.SetDocument(nullptr);
    rt_.Reset();
    auto& list = G().viewers;
    const auto it = std::find(list.begin(), list.end(), this);
    if (it != list.end()) {
        list.erase(it);
        if (list.empty()) G().d2d.Reset();
    }
}

bool Viewer::Create(HWND parent, int flags) {
    static const bool registered = [] {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &Viewer::StaticProc;
        wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (RegisterClassExW(&wc)) return true;
        // Left behind by an earlier load of a module at this address: its
        // window procedure may be gone, so register ours instead.
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        UnregisterClassW(kClassName, wc.hInstance);
        return RegisterClassExW(&wc) != 0;
    }();
    if (!registered) return false;

    Shared& g = G();
    if (!g.d2d && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g.d2d.GetAddressOf()))) return false;
    if (!g.dw && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                            reinterpret_cast<IUnknown**>(g.dw.GetAddressOf())))) {
        return false;
    }
    g.viewers.push_back(this);

    flags_ = flags;
    parent_ = parent;
    RECT rc{};
    GetClientRect(parent, &rc);
    hwnd_ = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, rc.right, rc.bottom,
                            parent, nullptr, reinterpret_cast<HINSTANCE>(&__ImageBase), this);
    if (!hwnd_) return false;
    created_ = true;
    dpi_ = WindowDpi(hwnd_);
    layout_.Init(g.dw.Get(), &palette_, &images_);
    layout_.draw_context().interactive = true;
    layout_.draw_context().drawn = &drawn_;
    zoom_percent_ = GlobalSettings().zoom_percent;
    ApplySettings(false);
    return true;
}

bool Viewer::DarkTheme() const {
    switch (GlobalSettings().theme) {
    case ThemeMode::Light: return false;
    case ThemeMode::Dark: return true;
    default:
        if (Host().tc_reports_dark_mode()) return (flags_ & lcp_darkmode) != 0;
        return SystemUsesDarkTheme();
    }
}

LayoutConfig Viewer::MakeLayoutConfig() const {
    const Settings& s = GlobalSettings();
    LayoutConfig cfg;
    cfg.font_family = s.font_family;
    cfg.mono_family = s.mono_family;
    cfg.font_size = s.font_size;
    cfg.zoom = static_cast<float>(zoom_percent_) / 100.f;
    cfg.max_content_width = static_cast<float>(s.max_width);
    cfg.wrap_code = s.wrap_code < 0 ? (flags_ & lcp_wraptext) != 0 : s.wrap_code == 1;
    cfg.line_numbers = s.line_numbers;
    cfg.dark = dark_;
    return cfg;
}

void Viewer::ApplySettings(bool relayout) {
    const bool dark = DarkTheme();
    const bool theme_changed = dark != dark_;
    dark_ = dark;
    if (theme_changed && rt_) palette_.Apply(GetTheme(dark_));
    if (theme_changed) images_.SetDark(dark_);
    if (theme_changed && full_.active()) CloseFullView();  // <picture> may switch to another image
    const LayoutConfig cfg = MakeLayoutConfig();
    const LayoutConfig& old = layout_.config();
    const bool layout_changed = cfg.font_family != old.font_family || cfg.mono_family != old.mono_family ||
                                cfg.font_size != old.font_size || cfg.zoom != old.zoom ||
                                cfg.max_content_width != old.max_content_width || cfg.wrap_code != old.wrap_code ||
                                cfg.line_numbers != old.line_numbers ||
                                !layout_.laid_out();
    if (layout_changed) {
        const ScrollAnchor a = CaptureAnchor();
        layout_.SetConfig(cfg);
        layout_dirty_ = true;
        if (relayout && doc_) {
            Relayout(false);
            RestoreAnchor(a);
        }
    }
    Invalidate();
}

// Settings changed (dialog or menu, possibly in another viewer window).
void Viewer::SettingsChanged(const Settings& old) {
    const Settings& s = GlobalSettings();
    if (s.zoom_percent != old.zoom_percent) zoom_percent_ = s.zoom_percent;
    if (s.hardware != old.hardware) DiscardDeviceResources();
    if (s.auto_reload != old.auto_reload) StartWatch();
    const bool reparse = s.remote_images != old.remote_images || s.emoji_shortcodes != old.emoji_shortcodes ||
                         s.math != old.math || s.mermaid != old.mermaid || s.syntax_highlight != old.syntax_highlight;
    if (reparse && !path_.empty()) {
        Reload();  // (after printing, if it is on)
    } else {
        ApplySettings(true);
    }
}

// Drops every mouse interaction in progress (they point into the document).
void Viewer::ResetInteraction() {
    mouse_down_ = false;
    selecting_ = false;
    dragged_ = false;
    thumb_drag_ = false;
    hscroll_drag_ = nullptr;
    down_link_ = -1;
    down_image_ = -1;
    down_summary_ = nullptr;
    click_count_ = 0;
    repeat_dir_ = 0;
    copy_hover_ = false;
    drawn_.clear();
    hover_object_ = nullptr;
    hover_button_ = -1;
    pan_object_ = nullptr;
    wheel_object_ = nullptr;
    full_.Close();
    esc_held_ = false;
    HookKeys(false);
    full_pan_ = false;
    full_backdrop_down_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    KillTimer(hwnd_, kTimerAutoScroll);
    KillTimer(hwnd_, kTimerRepeat);
    KillTimer(hwnd_, kTimerCopied);
}

bool Viewer::Load(const std::wstring& path, int flags, bool keep_history) {
    if (printing_) return false;  // the document is in use
    std::string bytes;
    std::string utf8;
    uint64_t total = 0;
    if (ReadFilePrefix(path.c_str(), bytes, kMaxFileBytes, &total)) {
        if (total > bytes.size()) {
            // Huge file: show the beginning (cut at a line break) with a note.
            const size_t nl = bytes.rfind('\n');
            if (nl != std::string::npos && nl > bytes.size() / 2) bytes.resize(nl + 1);
        }
        utf8 = DecodeTextToUtf8(bytes);
        if (total > bytes.size()) {
            const std::wstring note = std::wstring(L"> [!WARNING]\n> ") +
                                      Tr(L"The file is too large; only the first ", L"Файл слишком большой, показаны первые ") +
                                      std::to_wstring(bytes.size() >> 20) + Tr(L" MB of ", L" МБ из ") +
                                      std::to_wstring(total >> 20) + Tr(L" MB are shown.\n\n", L" МБ.\n\n");
            utf8 = WideToUtf8(note) + utf8;
        }
    } else {
        utf8 = WideToUtf8(std::wstring(L"# ") + Tr(L"Cannot open file", L"Не удалось открыть файл") + L"\n\n`" +
                          path + L"`\n");
    }
    bytes.clear();
    bytes.shrink_to_fit();

    const Settings& s = GlobalSettings();
    ParseOptions opt;
    opt.emoji_shortcodes = s.emoji_shortcodes;
    opt.math = s.math;
    opt.mermaid = s.mermaid;
    opt.highlight_code = s.syntax_highlight;
    auto doc = ParseMarkdown(utf8, path, opt);
    utf8.clear();
    utf8.shrink_to_fit();

    if (!keep_history) history_.clear();
    flags_ = flags;
    images_.Detach();
    layout_.SetDocument(nullptr);
    ResetInteraction();  // before the old blocks go away
    ++doc_gen_;
    doc_ = std::move(doc);
    path_ = path;
    selection_ = {};
    anchor_ = 0;
    hits_.clear();
    current_hit_ = -1;
    label_index_.clear();
    folded_valid_ = false;
    global_text_.clear();
    global_text_.shrink_to_fit();
    folded_text_.clear();
    folded_text_.shrink_to_fit();
    hover_link_ = -1;
    hover_link_leaf_ = nullptr;
    hover_block_ = nullptr;
    copied_block_ = nullptr;
    status_.clear();
    scroll_y_ = 0;
    anim_ = false;
    last_percent_ = -1;

    dark_ = DarkTheme();
    if (rt_) palette_.Apply(GetTheme(dark_));
    images_.Attach(doc_.get(), hwnd_, dark_, s.remote_images);
    images_.LoadAll();
    layout_.SetConfig(MakeLayoutConfig());
    layout_.SetDocument(doc_.get());
    layout_dirty_ = true;
    Relayout(false);
    UpdatePercent();
    Invalidate();
    StartWatch();
    return true;
}

// ============================================================================
// Auto-reload
// ============================================================================

bool Viewer::FileStamp(uint64_t& write_time, uint64_t& size) const {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (path_.empty() || !GetFileAttributesExW(ExtendedPath(path_).c_str(), GetFileExInfoStandard, &fa)) return false;
    write_time = (static_cast<uint64_t>(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
    size = (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
    return true;
}

// Thread-pool wait: only tells the window; everything else happens there.
void CALLBACK Viewer::WatchCallback(void* hwnd, BOOLEAN /*timed_out*/) {
    PostMessageW(static_cast<HWND>(hwnd), FileChangedMessage(), 0, 0);
}

void Viewer::StartWatch() {
    StopWatch();
    ++watch_gen_;  // (a poll still out belongs to the previous watch)
    poll_pending_ = false;
    if (path_.empty() || !GlobalSettings().auto_reload) return;
    FileStamp(file_time_, file_size_);
    const std::wstring dir = DirectoryOf(path_);
    watch_change_ = FindFirstChangeNotificationW(
        ExtendedPath(dir).c_str(), FALSE,
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE);
    if (watch_change_ != INVALID_HANDLE_VALUE &&
        !RegisterWaitForSingleObject(&watch_wait_, watch_change_, &Viewer::WatchCallback, hwnd_, INFINITE,
                                     WT_EXECUTEONLYONCE)) {
        watch_wait_ = nullptr;
    }
    // Change notifications from network shares are unreliable: also poll.
    const bool unc = dir.size() > 2 && dir[0] == L'\\' && dir[1] == L'\\';
    const std::wstring root = dir.substr(0, 3);
    const bool remote = unc || (dir.size() >= 3 && dir[1] == L':' && GetDriveTypeW(root.c_str()) == DRIVE_REMOTE);
    if (remote || !watch_wait_) SetTimer(hwnd_, kTimerWatchPoll, kWatchPollMs, nullptr);
}

void Viewer::StopWatch() {
    if (watch_wait_) {
        UnregisterWaitEx(watch_wait_, INVALID_HANDLE_VALUE);  // waits for a running callback
        watch_wait_ = nullptr;
    }
    if (watch_change_ != INVALID_HANDLE_VALUE) {
        FindCloseChangeNotification(watch_change_);
        watch_change_ = INVALID_HANDLE_VALUE;
    }
    if (hwnd_) {
        KillTimer(hwnd_, kTimerWatchPoll);
        KillTimer(hwnd_, kTimerReload);
    }
}

// Something changed in the directory: re-arm, then re-check the file after
// things settle down.
void Viewer::OnFileChanged() {
    if (watch_wait_) {
        UnregisterWaitEx(watch_wait_, INVALID_HANDLE_VALUE);
        watch_wait_ = nullptr;
    }
    if (watch_change_ != INVALID_HANDLE_VALUE) {
        if (FindNextChangeNotification(watch_change_) &&
            !RegisterWaitForSingleObject(&watch_wait_, watch_change_, &Viewer::WatchCallback, hwnd_, INFINITE,
                                         WT_EXECUTEONLYONCE)) {
            watch_wait_ = nullptr;
        }
    }
    SetTimer(hwnd_, kTimerReload, kReloadDelayMs, nullptr);
}

// Re-reads the file, keeping the reading position on the same paragraph.
void Viewer::Reload() {
    if (path_.empty()) return;
    if (printing_) {  // the document is in use: once it's printed
        reload_after_print_ = true;
        return;
    }
    const Block* leaf = doc_ ? LeafAtY(scroll_y_) : nullptr;
    const int32_t leaf_index = leaf ? leaf->leaf_index : -1;
    const float offset = leaf ? scroll_y_ - leaf->y : 0.f;
    const float y = scroll_y_;
    if (!Load(path_, flags_, true)) return;
    if (leaf_index >= 0 && static_cast<size_t>(leaf_index) < doc_->leaves.size()) {
        scroll_y_ = std::clamp(doc_->leaves[static_cast<size_t>(leaf_index)]->y + offset, 0.f, MaxScroll());
        Relayout(true);  // exact layout around it, anchored to that paragraph
    } else {
        scroll_y_ = std::clamp(y, 0.f, MaxScroll());
        Relayout(true);
    }
    UpdatePercent();
    Invalidate();
}

// ============================================================================
// Layout
// ============================================================================

float Viewer::ViewWidth() const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    return static_cast<float>(rc.right) / Scale();
}

float Viewer::ViewHeight() const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    return static_cast<float>(rc.bottom) / Scale();
}

float Viewer::MaxScroll() const { return std::max(0.f, layout_.doc_height() - ViewHeight()); }

Block* Viewer::LeafAtY(float y) const {
    if (!doc_) return nullptr;
    const auto& leaves = doc_->leaves;
    auto it = std::partition_point(leaves.begin(), leaves.end(), [y](const Block* b) { return b->y + b->h <= y; });
    // partition_point assumes monotonic order; hidden leaves sit where their
    // hidden block starts, with no height (LayoutEngine::Park), which keeps
    // the sequence sorted.
    while (it != leaves.end() && !(*it)->visible) ++it;
    return it == leaves.end() ? nullptr : *it;
}

Viewer::ScrollAnchor Viewer::CaptureAnchor() const {
    ScrollAnchor a;
    if (!doc_ || !layout_.laid_out()) return a;
    a.leaf = LeafAtY(scroll_y_);
    if (a.leaf) a.offset = scroll_y_ - a.leaf->y;
    return a;
}

void Viewer::RestoreAnchor(const ScrollAnchor& a) {
    if (a.leaf && a.leaf->visible) {
        scroll_y_ = a.leaf->y + a.offset;
    }
    scroll_y_ = std::clamp(scroll_y_, 0.f, MaxScroll());
    if (anim_) anim_to_ = std::clamp(anim_to_, 0.f, MaxScroll());
}

void Viewer::Relayout(bool keep_anchor) {
    if (!doc_ || printing_) return;
    const ScrollAnchor a = keep_anchor ? CaptureAnchor() : ScrollAnchor{};
    const float vh = ViewHeight();
    const float margin = vh;
    layout_.Layout(ViewWidth(), scroll_y_ - margin, scroll_y_ + vh + margin);
    if (keep_anchor) RestoreAnchor(a);
    // Positions may have shifted: make sure what is on screen is exact.
    for (int pass = 0; pass < 3; ++pass) {
        bool estimated = false;
        for (Block* leaf = LeafAtY(scroll_y_); leaf; ) {
            if (leaf->y > scroll_y_ + vh) break;
            const LeafState* s = layout_.State(leaf);
            if (leaf->visible && s && !s->exact) {
                estimated = true;
                break;
            }
            const int next = leaf->leaf_index + 1;
            leaf = next < static_cast<int>(doc_->leaves.size()) ? doc_->leaves[static_cast<size_t>(next)] : nullptr;
        }
        if (!estimated) break;
        const ScrollAnchor b = CaptureAnchor();
        layout_.Layout(ViewWidth(), scroll_y_ - margin, scroll_y_ + vh + margin);
        RestoreAnchor(b);
    }
    laid_out_width_ = ViewWidth();
    layout_dirty_ = false;
    if (layout_.HasEstimates()) SetTimer(hwnd_, kTimerRefine, 15, nullptr);
    UpdatePercent();
}

void Viewer::EnsureLayout() {
    if (!doc_) return;
    if (layout_dirty_ || std::fabs(laid_out_width_ - ViewWidth()) > 0.01f) Relayout(layout_.laid_out());
}

void Viewer::EnsureVisibleExact() {
    if (!doc_ || !layout_.HasEstimates()) return;
    const float vh = ViewHeight();
    for (Block* leaf = LeafAtY(scroll_y_); leaf;) {
        if (leaf->y > scroll_y_ + vh) break;
        const LeafState* s = layout_.State(leaf);
        if (leaf->visible && s && !s->exact) {
            Relayout(true);
            return;
        }
        const int next = leaf->leaf_index + 1;
        leaf = next < static_cast<int>(doc_->leaves.size()) ? doc_->leaves[static_cast<size_t>(next)] : nullptr;
    }
}

// ============================================================================
// Rendering
// ============================================================================

bool Viewer::EnsureRenderTarget() {
    if (rt_) return true;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float dpi = static_cast<float>(dpi_);
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        GlobalSettings().hardware ? D2D1_RENDER_TARGET_TYPE_DEFAULT : D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
    const D2D1_HWND_RENDER_TARGET_PROPERTIES hprops = D2D1::HwndRenderTargetProperties(
        hwnd_, D2D1::SizeU(std::max<LONG>(rc.right, 1), std::max<LONG>(rc.bottom, 1)), D2D1_PRESENT_OPTIONS_NONE);
    if (FAILED(G().d2d->CreateHwndRenderTarget(props, hprops, &rt_))) {
        // Fall back to software rendering.
        const D2D1_RENDER_TARGET_PROPERTIES sw = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            dpi, dpi);
        if (FAILED(G().d2d->CreateHwndRenderTarget(sw, hprops, &rt_))) return false;
    }
    if (!palette_.Create(rt_.Get(), GetTheme(dark_))) {
        rt_.Reset();
        return false;
    }
    layout_.InvalidateTextLayouts();
    images_.ReleaseDeviceResources();
    return true;
}

void Viewer::DiscardDeviceResources() {
    rt_.Reset();
    layout_.InvalidateTextLayouts();
    images_.ReleaseDeviceResources();
}

void Viewer::OnPaint() {
    PAINTSTRUCT ps;
    BeginPaint(hwnd_, &ps);
    EndPaint(hwnd_, &ps);
    if (printing_) return;  // the last frame stays on screen
    if (!EnsureRenderTarget()) return;
    EnsureLayout();
    if (anim_) {
        const double t = (NowMs() - anim_start_) / anim_duration_;
        if (t >= 1.0) {
            scroll_y_ = anim_to_;
            anim_ = false;
        } else {
            const double e = 1.0 - std::pow(1.0 - t, 3.0);  // ease-out cubic
            scroll_y_ = anim_from_ + static_cast<float>((anim_to_ - anim_from_) * e);
        }
        UpdatePercent();
    }
    EnsureVisibleExact();

    const Theme& theme = GetTheme(dark_);
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->Clear(theme.Color(kColorBackground));
    RenderState st;
    st.scroll_y = scroll_y_;
    st.view_w = ViewWidth();
    st.view_h = ViewHeight();
    st.dpi_scale = Scale();
    st.selection = selection_;
    st.focused = GetFocus() == hwnd_;
    st.hits = &hits_;
    st.current_hit = current_hit_;
    st.hover_block = hover_block_;
    st.copy_button_hover = copy_hover_;
    st.copied_block = copied_block_;
    try {
        images_.BeginPaint();
        if (full_.active()) {
            DrawFullView();
        } else {
            drawn_.clear();
            if (doc_) renderer_.Draw(rt_.Get(), layout_, st);
        }
        DrawOverlays();
    } catch (...) {
        // Out of memory: the frame is shown as far as it got, and EndDraw
        // must follow BeginDraw regardless or the target stops drawing.
    }
    const HRESULT hr = rt_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        Invalidate();
        return;
    }
    ScheduleGifFrame();
    images_.TrimMemory();
    if (anim_) {
        // Keep animating at the display's refresh rate.
        DwmFlush();
        Invalidate();
    }
    layout_.EvictFar(scroll_y_, scroll_y_ + ViewHeight());
    // Scrolled under a resting mouse: what it is over now.
    if (tracking_mouse_ && !mouse_down_ && !full_.active()) {
        const float x = last_mouse_.x / Scale();
        UpdateObjectHover(MaxScroll() > 0.5f && x >= ViewWidth() - 16.f ? -1.f : x, last_mouse_.y / Scale());
    }
}

// Animated GIFs visible in the last paint get their next frame on time;
// nothing runs while none is on screen.
void Viewer::ScheduleGifFrame() {
    const double due = images_.NextAnimationDue();
    if (due <= 0) {
        KillTimer(hwnd_, kTimerGif);
        return;
    }
    SetTimer(hwnd_, kTimerGif, static_cast<UINT>(std::clamp(due - NowMs(), 10.0, 10000.0)), nullptr);
}

D2D1_RECT_F Viewer::ScrollbarTrack() const {
    const float w = ViewWidth(), h = ViewHeight();
    const float width = (scrollbar_hover_ || thumb_drag_) ? 12.f : 8.f;
    return D2D1::RectF(w - width - 2.f, 2.f, w - 2.f, h - 2.f);
}

D2D1_RECT_F Viewer::ScrollbarThumb() const {
    const D2D1_RECT_F track = ScrollbarTrack();
    const float th = track.bottom - track.top;
    const float doc_h = std::max(layout_.doc_height(), 1.f);
    const float vh = ViewHeight();
    const float size = std::clamp(th * vh / doc_h, std::min(28.f, th), th);  // lo <= hi even when tiny
    const float range = MaxScroll();
    const float pos = range > 0 ? scroll_y_ / range : 0.f;
    const float top = track.top + (th - size) * pos;
    return D2D1::RectF(track.left, top, track.right, top + size);
}

const DiagramScene::Text* Viewer::LabelOver(int32_t diagram, uint32_t a, uint32_t b) {
    const std::shared_ptr<const DiagramScene> scene = layout_.Diagram(diagram);
    if (!scene) return nullptr;
    LabelIndex& ix = label_index_[diagram];
    if (ix.scene != scene) {  // (built once: a search or a repaint asks for every match)
        ix.scene = scene;
        std::vector<uint32_t> order;
        for (uint32_t i = 0; i < scene->texts.size(); ++i) {
            if (scene->texts[i].src_length > 0) order.push_back(i);
        }
        std::sort(order.begin(), order.end(),
                  [&](uint32_t x, uint32_t y) { return scene->texts[x].src_start < scene->texts[y].src_start; });
        ix.starts.clear();
        ix.reach.clear();
        for (const uint32_t i : order) {
            const DiagramScene::Text& t = scene->texts[i];
            const uint32_t end = t.src_start + t.src_length;
            ix.starts.push_back(t.src_start);
            ix.reach.push_back(ix.reach.empty() || end > ix.reach.back().first ? std::make_pair(end, i) : ix.reach.back());
        }
    }
    // Of the labels starting before b, the one reaching farthest past a.
    const size_t j = static_cast<size_t>(std::lower_bound(ix.starts.begin(), ix.starts.end(), b) - ix.starts.begin());
    if (j == 0 || ix.reach[j - 1].first <= a) return nullptr;
    return &scene->texts[ix.reach[j - 1].second];
}

void Viewer::DrawOverlays() {
    // Zoom buttons over the image under the mouse.
    if (!full_.active() && hover_object_) {
        if (const DrawnObject* d = Drawn(hover_object_)) {
            const ZoomToolbar t = ObjectToolbar(*d);
            rt_->PushAxisAlignedClip(d->visible, D2D1_ANTIALIAS_MODE_ALIASED);
            DrawZoomToolbar(rt_.Get(), renderer_.icons(), palette_, t, hover_button_, ObjectButtonsOff(*d->object));
            rt_->PopAxisAlignedClip();
        }
    }
    // Vertical scrollbar (only when the document is taller than the view).
    if (!full_.active() && MaxScroll() > 0.5f) {
        const D2D1_RECT_F thumb = ScrollbarThumb();
        const float r = (thumb.right - thumb.left) * 0.5f;
        if (!hits_.empty() && doc_) {
            // Search matches as ticks on the track.
            const D2D1_RECT_F track = ScrollbarTrack();
            const float doc_h = std::max(layout_.doc_height(), 1.f);
            const float th = track.bottom - track.top;
            float last_y = -10.f;
            for (size_t i = 0; i < hits_.size(); ++i) {
                const Block* leaf = doc_->LeafAt(hits_[i].start);
                if (!leaf || !leaf->visible) continue;
                float at = leaf->y;
                // In a diagram: where its label is, down the diagram.
                const uint32_t h0 = hits_[i].start - leaf->text_start, h1 = hits_[i].end - leaf->text_start;
                for (const InlineObject& o : leaf->objects) {
                    if (o.diagram < 0 || h1 <= o.pos || h0 >= o.pos + o.length) continue;
                    const auto scene = layout_.Diagram(o.diagram);
                    if (!scene || scene->height <= 0) break;
                    if (const DiagramScene::Text* t = LabelOver(o.diagram, h0 > o.pos ? h0 - o.pos : 0, h1 - o.pos)) {
                        at += leaf->h * std::clamp(t->origin.y / scene->height, 0.f, 1.f);
                    }
                    break;
                }
                const float y = track.top + th * at / doc_h;
                if (y - last_y < 2.f && static_cast<int>(i) != current_hit_) continue;
                last_y = y;
                rt_->FillRectangle(D2D1::RectF(track.left - 2.f, y, track.right, y + 2.f),
                                   palette_.Brush(static_cast<int>(i) == current_hit_ ? kColorSearchCurrent
                                                                                       : kColorAlertWarning));
            }
        }
        rt_->FillRoundedRectangle(D2D1_ROUNDED_RECT{thumb, r, r},
                                  palette_.Brush((scrollbar_hover_ || thumb_drag_) ? kColorScrollThumbHover
                                                                                    : kColorScrollThumb));
    }
    // Link status bubble.
    if (!status_.empty()) {
        const float em = 12.f;
        IDWriteTextLayout* tl = layout_.LabelLayout(status_, em, 400);
        if (tl) {
            DWRITE_TEXT_METRICS tm{};
            tl->GetMetrics(&tm);
            const float vw = ViewWidth(), vh = ViewHeight();
            const float w = std::min(tm.widthIncludingTrailingWhitespace + 16.f, vw * 0.7f);
            const float h = tm.height + 8.f;
            float top = vh - h - 4.f;
            const float left = 4.f;
            if (last_mouse_.y / Scale() > top - 8.f && last_mouse_.x / Scale() < left + w + 8.f) top = 4.f;
            const D2D1_RECT_F r = D2D1::RectF(left, top, left + w, top + h);
            rt_->FillRoundedRectangle(D2D1_ROUNDED_RECT{r, 4.f, 4.f}, palette_.Brush(kColorBubbleBg));
            rt_->DrawRoundedRectangle(D2D1_ROUNDED_RECT{D2D1::RectF(r.left + .5f, r.top + .5f, r.right - .5f, r.bottom - .5f),
                                                        4.f, 4.f},
                                      palette_.Brush(kColorBorder), 1.f);
            rt_->PushAxisAlignedClip(D2D1::RectF(r.left + 8.f, r.top, r.right - 8.f, r.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
            rt_->DrawTextLayout(D2D1::Point2F(r.left + 8.f, r.top + 4.f), tl, palette_.Brush(kColorBubbleText));
            rt_->PopAxisAlignedClip();
        }
    }
}

// ============================================================================
// Scrolling
// ============================================================================

void Viewer::ScrollTo(float y, bool smooth) {
    y = std::clamp(y, 0.f, MaxScroll());
    if (smooth && GlobalSettings().smooth_scroll) {
        anim_from_ = scroll_y_;
        anim_to_ = y;
        anim_start_ = NowMs();
        anim_duration_ = 140.0;
        anim_ = std::fabs(anim_to_ - anim_from_) > 0.5f;
        if (!anim_) scroll_y_ = y;
    } else {
        anim_ = false;
        scroll_y_ = y;
    }
    UpdatePercent();
    Invalidate();
}

void Viewer::ScrollIntoView(Block* leaf, uint32_t local_pos, bool center, const D2D1_RECT_F* target) {
    if (!leaf) return;
    // Make sure the target is laid out exactly before computing positions.
    Relayout(true);
    IDWriteTextLayout* tl = layout_.TextLayout(leaf);
    float y = leaf->y, h = leaf->h;
    if (target) {
        y = layout_.TextOrigin(leaf).y + target->top;
        h = target->bottom - target->top;
    } else if (tl) {
        DWRITE_HIT_TEST_METRICS m{};
        FLOAT px = 0, py = 0;
        if (SUCCEEDED(tl->HitTestTextPosition(local_pos, FALSE, &px, &py, &m))) {
            const D2D1_POINT_2F o = layout_.TextOrigin(leaf);
            y = o.y + m.top;
            h = m.height;
        }
    }
    const float vh = ViewHeight();
    if (center) {
        if (y < scroll_y_ + vh * 0.15f || y + h > scroll_y_ + vh * 0.85f) ScrollTo(y - vh * 0.35f, false);
    } else {
        if (y < scroll_y_) ScrollTo(y - 8.f, false);
        else if (y + h > scroll_y_ + vh) ScrollTo(y + h - vh + 8.f, false);
    }
}

void Viewer::UpdatePercent() {
    if (!parent_) return;
    const float max = MaxScroll();
    const int pct = max > 0 ? static_cast<int>(std::lround(100.f * scroll_y_ / max)) : 100;
    if (pct == last_percent_) return;
    last_percent_ = pct;
    PostMessageW(parent_, WM_COMMAND, MAKEWPARAM(pct, itm_percent), reinterpret_cast<LPARAM>(hwnd_));
}

void Viewer::OnMouseWheel(int delta, bool horizontal, WORD keys, POINT screen) {
    const bool ctrl = (keys & MK_CONTROL) || (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (keys & MK_SHIFT) || (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    POINT cp = screen;
    ScreenToClient(hwnd_, &cp);
    const float mx = cp.x / Scale(), my = cp.y / Scale();
    const float factor = std::pow(kWheelZoomStep, static_cast<float>(delta) / WHEEL_DELTA);
    if (full_.active()) {
        // Full-window view: the wheel zooms (no Ctrl needed).
        if (!horizontal) {
            full_.ZoomAt(factor, mx, my, ViewRect());
            ShowZoomStatus(full_.scale, full_.object);
            Invalidate();
        }
        return;
    }
    if (ctrl && !horizontal) {
        // Over an image it zooms inside its box, elsewhere the page does. A
        // gesture keeps its target (a pinch zooming the page out doesn't get
        // stuck on an image sliding under the pointer), and zooming out an
        // image at 1x zooms the page.
        const DWORD now = GetTickCount();
        if (now - wheel_time_ > kWheelGestureMs) {
            const DrawnObject* d = ObjectAt(mx, my);
            wheel_object_ = d && !(delta < 0 && !IsZoomed(*d->object)) ? d->object : nullptr;
        }
        wheel_time_ = now;
        if (wheel_object_) {
            if (const DrawnObject* d = Drawn(wheel_object_)) {
                ZoomObjectBy(*d, factor, mx, my);
                return;
            }
            wheel_object_ = nullptr;
        }
        wheel_accum_ += static_cast<float>(delta);
        while (wheel_accum_ >= WHEEL_DELTA) {
            SetZoom(zoom_percent_ + 10);
            wheel_accum_ -= WHEEL_DELTA;
        }
        while (wheel_accum_ <= -WHEEL_DELTA) {
            SetZoom(zoom_percent_ - 10);
            wheel_accum_ += WHEEL_DELTA;
        }
        return;
    }
    if (horizontal || shift) {
        const Hit hit = HitTest(mx, my + scroll_y_);
        if (hit.wide) {
            const float step = static_cast<float>(horizontal ? delta : -delta) / WHEEL_DELTA * layout_.metrics().em * 4.f;
            hit.wide->scroll_x = std::clamp(hit.wide->scroll_x + step, 0.f, std::max(0.f, hit.wide->content_w - hit.wide->w));
            Invalidate();
        }
        return;
    }
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    float amount;
    if (lines == WHEEL_PAGESCROLL) {
        amount = ViewHeight() - LineStep();
    } else {
        amount = static_cast<float>(lines) * layout_.metrics().em * 2.f;
    }
    ScrollBy(-static_cast<float>(delta) / WHEEL_DELTA * amount, std::abs(delta) >= WHEEL_DELTA);
}

void Viewer::OnKeyDown(WPARAM vk) {
    if (full_.active()) {
        FullViewKey(vk);
        return;
    }
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const float page = std::max(ViewHeight() - LineStep() * 1.5f, LineStep());
    switch (vk) {
    case VK_DOWN: ScrollBy(LineStep(), true); break;
    case VK_UP: ScrollBy(-LineStep(), true); break;
    case VK_NEXT: ScrollBy(page, true); break;
    case VK_PRIOR: ScrollBy(-page, true); break;
    case VK_SPACE: ScrollBy(shift ? -page : page, true); break;
    case VK_HOME: ScrollTo(0, true); break;
    case VK_END: ScrollTo(MaxScroll(), true); break;
    case VK_BACK: GoBack(); break;
    case VK_LEFT: if (alt) GoBack(); break;
    case VK_F5: Reload(); break;
    case 'A': if (ctrl) Command(lc_selectall, 0); break;
    case 'C': case VK_INSERT: if (ctrl) CopySelection(); break;
    case VK_OEM_PLUS: case VK_ADD: if (ctrl) SetZoom(zoom_percent_ + 10); break;
    case VK_OEM_MINUS: case VK_SUBTRACT: if (ctrl) SetZoom(zoom_percent_ - 10); break;
    case '0': case VK_NUMPAD0: if (ctrl) SetZoom(100); break;
    default: break;
    }
}

// ============================================================================
// Hit testing
// ============================================================================

float Viewer::ScrollXOf(const Block* b) const {
    float sx = 0;
    for (const Block* p = b; p; p = p->parent) {
        if (p->content_w > 0) sx += p->scroll_x;
    }
    return sx;
}

uint32_t Viewer::LeafPosFromPoint(Block* leaf, float x, float y, bool* inside, uint32_t* run_link,
                                  uint32_t* cluster) const {
    auto& self = const_cast<Viewer&>(*this);
    IDWriteTextLayout* tl = self.layout_.TextLayout(leaf);
    if (inside) *inside = false;
    if (run_link) *run_link = static_cast<uint32_t>(-1);
    if (!tl) return leaf->text_start;
    const D2D1_POINT_2F o = layout_.TextOrigin(leaf);
    BOOL trailing = FALSE, in = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    tl->HitTestPoint(x + ScrollXOf(leaf) - o.x, y - o.y, &trailing, &in, &m);
    if (inside) *inside = in != FALSE;
    uint32_t pos = m.textPosition + (trailing ? m.length : 0);
    pos = std::min<uint32_t>(pos, static_cast<uint32_t>(leaf->text.size()));
    if (cluster) *cluster = leaf->text_start + std::min<uint32_t>(m.textPosition, static_cast<uint32_t>(leaf->text.size()));
    if (run_link && in) {
        const int l = LinkAt(leaf, m.textPosition);
        *run_link = static_cast<uint32_t>(l);
    }
    return leaf->text_start + pos;
}

int Viewer::LinkAt(Block* leaf, uint32_t local) const {
    for (const StyleRun& r : leaf->runs) {
        if (local >= r.start && local < r.start + r.length) return r.link;
    }
    return -1;
}

Viewer::Hit Viewer::HitTest(float x, float y) const {
    Hit hit;
    if (!doc_) return hit;
    // Find the deepest visible block containing the point.
    Block* b = doc_->root;
    float scroll_x = 0;  // accumulated horizontal scroll of wide ancestors
    for (;;) {
        Block* next = nullptr;
        if (b->content_w > 0) scroll_x += b->scroll_x;
        const float lx = x + scroll_x;
        for (Block* c : b->children) {
            if (!c->visible || c->h <= 0) continue;
            if (y >= c->y && y < c->y + c->h) {
                if (b->type == BlockType::TableRow && !(lx >= c->x && lx < c->x + c->w)) continue;
                if (c->type == BlockType::TableCell && !(lx >= c->x && lx < c->x + c->w)) continue;
                next = c;
                break;
            }
            if (c->y > y) break;
        }
        if (!next) break;
        b = next;
        if (b->type == BlockType::CodeBlock) hit.code = b;
        if (b->type == BlockType::Summary) hit.summary = b;
        if (b->content_w > 0) hit.wide = b;
        if (b->IsLeaf()) break;
    }
    Block* leaf = b->IsLeaf() ? b : nullptr;
    if (!leaf) {
        // Between blocks: nearest leaf by vertical distance.
        Block* after = LeafAtY(y);
        Block* before = nullptr;
        if (after && after->leaf_index > 0) {
            for (int i = after->leaf_index - 1; i >= 0; --i) {
                Block* c = doc_->leaves[static_cast<size_t>(i)];
                if (c->visible) {
                    before = c;
                    break;
                }
            }
        }
        if (after && y >= after->y) leaf = after;
        else if (before && after) leaf = (y - (before->y + before->h) < after->y - y) ? before : after;
        else leaf = after ? after : before;
        for (auto it = doc_->leaves.rbegin(); !leaf && it != doc_->leaves.rend(); ++it) {
            if ((*it)->visible) leaf = *it;  // below the end: the last visible leaf
        }
    }
    if (!leaf) return hit;
    hit.leaf = leaf;
    bool inside = false;
    uint32_t link = static_cast<uint32_t>(-1);
    hit.pos = LeafPosFromPoint(leaf, x, y, &inside, &link, &hit.cluster);
    hit.on_text = inside && b == leaf;
    if (hit.on_text) {
        hit.link = static_cast<int>(link);
        const uint32_t local = hit.pos - leaf->text_start;
        for (const InlineObject& o : leaf->objects) {
            if (o.length != 1 || o.image < 0) continue;  // flag emoji and formulas behave as text
            if (o.pos == local || o.pos + 1 == local) {
                hit.image = o.image;
                if (o.pos + 1 == local) {
                    // Trailing half of the object: the link is on the object itself.
                    hit.link = LinkAt(leaf, o.pos);
                }
            }
        }
    }
    return hit;
}

// ============================================================================
// Mouse
// ============================================================================

void Viewer::SetStatus(const std::wstring& text) {
    if (text == status_) return;
    status_ = text;
    Invalidate();
}

void Viewer::SetHoverLink(Block* leaf, int link) {
    if (link == hover_link_ && leaf == hover_link_leaf_) return;
    // Underline the hovered link in its text layout.
    auto set_underline = [this](Block* lf, int ln, bool on) {
        if (!lf || ln < 0) return;
        IDWriteTextLayout* tl = layout_.TextLayout(lf);
        if (!tl) return;
        for (const StyleRun& r : lf->runs) {
            if (r.link == ln && !(r.flags & kUnderline)) tl->SetUnderline(on, DWRITE_TEXT_RANGE{r.start, r.length});
        }
    };
    set_underline(hover_link_leaf_, hover_link_, false);
    hover_link_ = link;
    hover_link_leaf_ = link >= 0 ? leaf : nullptr;
    set_underline(hover_link_leaf_, hover_link_, true);
    if (link >= 0 && doc_ && static_cast<size_t>(link) < doc_->links.size()) {
        const Link& l = doc_->links[static_cast<size_t>(link)];
        std::wstring text = l.url;
        if (!l.title.empty() && l.url.rfind(L"#fn", 0) != 0) text = l.title + L"  —  " + l.url;
        SetStatus(text);
    } else {
        SetStatus(L"");
    }
    Invalidate();
}

void Viewer::OnLButtonDown(int px, int py, bool dbl) {
    SetFocus(hwnd_);
    const float x = px / Scale(), y = py / Scale();
    const float dy = y + scroll_y_;
    mouse_down_ = true;
    dragged_ = false;
    down_pt_ = POINT{px, py};
    down_link_ = -1;
    down_image_ = -1;
    down_summary_ = nullptr;
    SetCapture(hwnd_);

    if (full_.active()) {
        const ZoomToolbar t = FullViewToolbar();
        const int b = ZoomButtonAt(t, x, y);
        mouse_down_ = false;  // not a click on the page
        if (b >= 0) {
            // (A double click on the image's button that opened the view may
            // land on its x in a narrow window: the view stays.)
            if (!(dbl && t.kinds[static_cast<size_t>(b)] == ZoomButton::Close)) FullViewButton(t.kinds[static_cast<size_t>(b)]);
            return;
        }
        mouse_down_ = true;
        const D2D1_RECT_F r = full_.ContentRect(ViewRect());
        const bool on = x >= r.left && x < r.right && y >= r.top && y < r.bottom;
        if (on && dbl) {
            FullViewButton(ZoomButton::Reset);
        } else if (on) {
            full_pan_ = true;
            full_last_x_ = x;
            full_last_y_ = y;
        } else if (!dbl) {
            // A click beside the image closes the view (the second click of
            // a double click on the button that opened it doesn't).
            full_backdrop_down_ = true;
        }
        return;
    }
    if (dbl && GetTickCount() - full_closed_at_ < GetDoubleClickTime()) {
        mouse_down_ = false;  // the second click on the button that closed the view
        return;
    }

    // Scrollbar.
    if (MaxScroll() > 0.5f) {
        const D2D1_RECT_F track = ScrollbarTrack();
        if (x >= track.left - 4.f) {
            const D2D1_RECT_F thumb = ScrollbarThumb();
            if (y >= thumb.top && y <= thumb.bottom) {
                thumb_drag_ = true;
                thumb_drag_offset_ = y - thumb.top;
            } else {
                repeat_dir_ = y < thumb.top ? -1 : 1;
                ScrollBy(repeat_dir_ * (ViewHeight() - LineStep()), true);
                SetTimer(hwnd_, kTimerRepeat, 300, nullptr);
            }
            Invalidate();
            return;
        }
    }
    // Zoom buttons over an image, and dragging a zoomed one around.
    if (const DrawnObject* found = ObjectAt(x, y)) {
        const DrawnObject d = *found;
        if (d.object == hover_object_) {
            const ZoomToolbar t = ObjectToolbar(d);
            const int b = ZoomButtonAt(t, x, y);
            if (b >= 0) {
                mouse_down_ = false;  // the release isn't a click on the image
                ObjectButton(d, t.kinds[static_cast<size_t>(b)]);
                return;
            }
        }
        if (IsZoomed(*d.object)) {
            if (dbl) {
                mouse_down_ = false;
                ObjectButton(d, ZoomButton::Reset);
                return;
            }
            pan_object_ = d.object;
            pan_box_ = d.box;
            pan_x_ = x;
            pan_y_ = y;
            pan_view_x_ = d.object->view_x;
            pan_view_y_ = d.object->view_y;
            return;
        }
    }
    const Hit hit = HitTest(x, dy);
    // Copy button of a code block.
    if (hit.code && hover_block_ == hit.code) {
        const D2D1_RECT_F r = Renderer::CopyButtonRect(hit.code, layout_.metrics());
        if (x >= r.left && x <= r.right && dy >= r.top && dy <= r.bottom) {
            CopyCode(hit.code);
            return;
        }
    }
    // Horizontal scrollbar of a wide block.
    if (hit.wide) {
        const D2D1_RECT_F track = Renderer::HScrollbarRect(hit.wide, layout_.metrics(), false);
        if (dy >= track.top - 4.f && dy <= track.bottom + 2.f) {
            hscroll_drag_ = hit.wide;
            hscroll_drag_start_x_ = x;
            hscroll_drag_start_scroll_ = hit.wide->scroll_x;
            return;
        }
    }
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    // Click counting for triple clicks.
    const DWORD now = GetTickCount();
    if (dbl || (now - last_click_time_ < GetDoubleClickTime() && std::abs(px - last_click_pt_.x) < 4 &&
                std::abs(py - last_click_pt_.y) < 4)) {
        ++click_count_;
    } else {
        click_count_ = 1;
    }
    last_click_time_ = now;
    last_click_pt_ = POINT{px, py};

    if (click_count_ >= 3 && hit.leaf) {
        SelectLeaf(hit.leaf);
        return;
    }
    if (click_count_ == 2 && hit.leaf) {
        SelectWordAt(hit);
        return;
    }
    down_link_ = hit.link;
    down_summary_ = hit.summary;
    down_image_ = hit.image;
    if (shift && selection_.end >= selection_.start && hit.leaf) {
        selection_ = TextRange{std::min(anchor_, hit.pos), std::max(anchor_, hit.pos)};
    } else if (hit.leaf) {
        anchor_ = hit.pos;
        selection_ = TextRange{hit.pos, hit.pos};
    }
    selecting_ = hit.leaf != nullptr;
    Invalidate();
}

void Viewer::OnMouseMove(int px, int py) {
    last_mouse_ = POINT{px, py};
    if (!tracking_mouse_) {
        TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        tracking_mouse_ = true;
    }
    const float x = px / Scale(), y = py / Scale();
    const float dy = y + scroll_y_;
    if (full_.active()) {
        if (full_pan_) {
            full_.PanBy(x - full_last_x_, y - full_last_y_, ViewRect());
            full_last_x_ = x;
            full_last_y_ = y;
            Invalidate();
            return;
        }
        if (mouse_down_ && (std::abs(px - down_pt_.x) > 3 || std::abs(py - down_pt_.y) > 3)) dragged_ = true;
        const int b = mouse_down_ ? -1 : ZoomButtonAt(FullViewToolbar(), x, y);
        if (b != hover_button_) {
            hover_button_ = b;
            Invalidate();
        }
        return;
    }
    if (pan_object_) {
        PanObject(*pan_object_, pan_box_, pan_view_x_, pan_view_y_, x - pan_x_, y - pan_y_);
        Invalidate();
        return;
    }
    if (thumb_drag_) {
        const D2D1_RECT_F track = ScrollbarTrack();
        const D2D1_RECT_F thumb = ScrollbarThumb();
        const float th = track.bottom - track.top - (thumb.bottom - thumb.top);
        const float pos = th > 0 ? (y - thumb_drag_offset_ - track.top) / th : 0.f;
        ScrollTo(pos * MaxScroll(), false);
        return;
    }
    if (hscroll_drag_) {
        Block* b = hscroll_drag_;
        const float track_w = b->w - 4.f;
        const float ratio = b->content_w / std::max(track_w, 1.f);
        b->scroll_x = std::clamp(hscroll_drag_start_scroll_ + (x - hscroll_drag_start_x_) * ratio, 0.f,
                                 std::max(0.f, b->content_w - b->w));
        Invalidate();
        return;
    }
    if (mouse_down_ && selecting_) {
        if (std::abs(px - down_pt_.x) > 3 || std::abs(py - down_pt_.y) > 3) dragged_ = true;
        if (dragged_) {
            const Hit hit = HitTest(x, dy);
            if (hit.leaf) {
                selection_ = TextRange{std::min(anchor_, hit.pos), std::max(anchor_, hit.pos)};
                hits_.clear();
                current_hit_ = -1;
            }
            const float vh = ViewHeight();
            if (y < 0 || y > vh) SetTimer(hwnd_, kTimerAutoScroll, 30, nullptr);
            else KillTimer(hwnd_, kTimerAutoScroll);
            Invalidate();
        }
        return;
    }
    // Hover state.
    const bool over_bar = MaxScroll() > 0.5f && x >= ViewWidth() - 16.f;
    if (over_bar != scrollbar_hover_) {
        scrollbar_hover_ = over_bar;
        Invalidate();
    }
    const Hit hit = over_bar ? Hit{} : HitTest(x, dy);
    Block* hb = hit.code ? hit.code : hit.wide;
    bool copy_hover = false;
    if (hit.code) {
        const D2D1_RECT_F r = Renderer::CopyButtonRect(hit.code, layout_.metrics());
        copy_hover = x >= r.left && x <= r.right && dy >= r.top && dy <= r.bottom;
    }
    if (hb != hover_block_ || copy_hover != copy_hover_) {
        hover_block_ = hb;
        copy_hover_ = copy_hover;
        Invalidate();
    }
    UpdateObjectHover(over_bar ? -1.f : x, y);
    SetHoverLink(hit.leaf, copy_hover || hover_button_ >= 0 ? -1 : hit.link);
}

void Viewer::OnLButtonUp(int px, int py) {
    if (full_.active() || pan_object_) {
        const bool close = full_backdrop_down_ && mouse_down_ && !dragged_;
        mouse_down_ = false;
        full_pan_ = false;
        full_backdrop_down_ = false;
        pan_object_ = nullptr;
        ReleaseCapture();
        if (close) CloseFullView();
        return;
    }
    // Evaluate the click before ReleaseCapture(): it synchronously sends
    // WM_CAPTURECHANGED, which resets the mouse state.
    const bool was_click = mouse_down_ && !dragged_;
    const bool was_drag = thumb_drag_ || hscroll_drag_ != nullptr;
    mouse_down_ = false;
    selecting_ = false;
    thumb_drag_ = false;
    hscroll_drag_ = nullptr;
    ReleaseCapture();
    KillTimer(hwnd_, kTimerAutoScroll);
    KillTimer(hwnd_, kTimerRepeat);
    repeat_dir_ = 0;
    if (was_drag) {
        Invalidate();
        return;
    }
    // The press state is cleared first: opening a link may run a modal loop
    // (a message box, ShellExecute's own prompts) in which Total Commander
    // can close the viewer, so nothing may touch `this` after it.
    const int link = down_link_, image = down_image_;
    Block* const summary = down_summary_;
    down_link_ = -1;
    down_summary_ = nullptr;
    down_image_ = -1;
    if (!was_click) return;
    const Hit hit = HitTest(px / Scale(), py / Scale() + scroll_y_);
    if (link >= 0 && hit.link == link && click_count_ == 1) {
        selection_ = {};
        OpenLink(link);
    } else if (summary && hit.summary == summary && click_count_ == 1) {
        selection_ = {};
        ToggleDetails(summary);
    } else if (image >= 0 && hit.image == image && hit.link < 0 && click_count_ == 1) {
        // Like GitHub: clicking a picture that isn't a link opens the image.
        // Local files only with an image extension: a file that also decodes
        // as a picture may still be something else to the shell.
        const ImageEntry* e = images_.Entry(image);
        if (e && !e->url.empty() && !StartsWithNoCase(e->url, L"data:") && e->state == ImageState::Ready &&
            (e->remote || IsImagePath(e->url))) {
            selection_ = {};
            const std::wstring url = e->url;  // not ours to keep if the viewer goes meanwhile
            ShellExecuteW(hwnd_, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
}

void Viewer::SelectWordAt(const Hit& hit) {
    Block* leaf = hit.leaf;
    const std::wstring& t = leaf->text;
    uint32_t p = hit.pos - leaf->text_start;
    if (p >= t.size() && p > 0) --p;
    if (t.empty()) return;
    uint32_t a = p, b = p;
    // On a formula or flag (either half of it): all of it.
    const uint32_t at = hit.on_text && hit.cluster >= leaf->text_start ? hit.cluster - leaf->text_start : p;
    for (const InlineObject& o : leaf->objects) {
        if (o.length > 1 && at >= o.pos && at < o.pos + o.length) {
            anchor_ = leaf->text_start + o.pos;
            selection_ = TextRange{leaf->text_start + o.pos, leaf->text_start + o.pos + o.length};
            selecting_ = false;
            Invalidate();
            return;
        }
    }
    if (IsWordChar(t[p])) {
        while (a > 0 && IsWordChar(t[a - 1])) --a;
        while (b < t.size() && IsWordChar(t[b])) ++b;
    } else {
        b = p + 1;
    }
    anchor_ = leaf->text_start + a;
    selection_ = TextRange{leaf->text_start + a, leaf->text_start + b};
    selecting_ = false;
    Invalidate();
}

void Viewer::SelectLeaf(Block* leaf) {
    anchor_ = leaf->text_start;
    selection_ = TextRange{leaf->text_start, leaf->text_end()};
    selecting_ = false;
    Invalidate();
}

void Viewer::ToggleDetails(Block* summary) {
    Block* details = summary->parent;
    if (!details || details->type != BlockType::Details) return;
    details->open = !details->open;
    LayoutEngine::UpdateVisibility(details, details->visible);
    const float y_before = summary->y - scroll_y_;
    Relayout(false);
    scroll_y_ = std::clamp(summary->y - y_before, 0.f, MaxScroll());
    Relayout(true);
    Invalidate();
}

// ============================================================================
// Links & navigation
// ============================================================================

void Viewer::OpenLink(int link_index) {
    if (!doc_ || link_index < 0 || static_cast<size_t>(link_index) >= doc_->links.size()) return;
    const std::wstring url(Trim(doc_->links[static_cast<size_t>(link_index)].url));
    if (url.empty()) return;
    if (url[0] == L'#') {
        JumpToAnchor(url.substr(1));
        return;
    }
    if (StartsWithNoCase(url, L"http://") || StartsWithNoCase(url, L"https://") ||
        StartsWithNoCase(url, L"mailto:") || StartsWithNoCase(url, L"ftp://") || StartsWithNoCase(url, L"tel:") ||
        StartsWithNoCase(url, L"xmpp:")) {
        ShellExecuteW(hwnd_, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (StartsWithNoCase(url, L"www.")) {
        ShellExecuteW(hwnd_, L"open", (L"http://" + url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    // Local file (relative to the document) with an optional #fragment.
    std::wstring target = url;
    std::wstring fragment;
    const size_t hash = target.find(L'#');
    if (hash != std::wstring::npos) {
        fragment = target.substr(hash + 1);
        target.resize(hash);
    }
    bool remote = false;
    const std::wstring path = ResolveResourceUrl(doc_->base_dir, target, &remote);
    if (path.empty() || remote || StartsWithNoCase(path, L"data:")) return;  // javascript: etc.
    NavigateTo(path, fragment);
}

void Viewer::NavigateTo(const std::wstring& path, const std::wstring& fragment) {
    // A share on another server than the document's: touching the path makes
    // Windows log on there with the user's account (the server gets an NTLM
    // response), so the user decides first. Device paths never.
    const std::wstring server = ShareServer(path);
    if (server == L"?") {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (!server.empty() && server != ShareServer(doc_ ? doc_->base_dir : std::wstring())) {
        const std::wstring msg = std::wstring(Tr(L"The link points to a network location:\n\n",
                                                 L"Ссылка ведёт на сетевой ресурс:\n\n")) +
                                 path + Tr(L"\n\nOpen it?", L"\n\nОткрыть?");
        const HWND self = hwnd_;
        if (MessageBoxW(self, msg.c_str(), L"mdview", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        if (!Survived(self, this)) return;
    }
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        MessageBeep(MB_ICONWARNING);
        SetStatus(std::wstring(Tr(L"File not found: ", L"Файл не найден: ")) + path);
        return;
    }
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY) && IsMarkdownPath(path)) {
        history_.push_back(HistoryEntry{path_, scroll_y_});
        std::vector<HistoryEntry> saved = std::move(history_);
        Load(path, flags_, true);
        history_ = std::move(saved);
        if (!fragment.empty()) JumpToAnchor(fragment);
        return;
    }
    // A document must not be able to start programs with a single click.
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY) && IsExecutablePath(path)) {
        const std::wstring msg = std::wstring(Tr(L"The link points to a program or script:\n\n",
                                                 L"Ссылка указывает на программу или скрипт:\n\n")) +
                                 path + Tr(L"\n\nRun it?", L"\n\nЗапустить?");
        const HWND self = hwnd_;
        if (MessageBoxW(self, msg.c_str(), L"mdview", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        if (!Survived(self, this)) return;
    }
    if (!OpenShellItem(hwnd_, path)) MessageBeep(MB_ICONWARNING);
}

void Viewer::GoBack() {
    if (history_.empty()) return;
    HistoryEntry e = history_.back();
    history_.pop_back();
    std::vector<HistoryEntry> saved = std::move(history_);
    Load(e.path, flags_, true);
    history_ = std::move(saved);
    ScrollTo(e.scroll, false);
}

bool Viewer::JumpToAnchor(const std::wstring& fragment) {
    if (!doc_) return false;
    if (fragment.empty() || EqualsNoCase(fragment, L"top")) {
        ScrollTo(0, true);
        return true;
    }
    const Anchor* a = doc_->FindAnchor(fragment);
    if (!a || !a->block) {
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    // Open collapsed <details> around the target.
    bool changed = false;
    for (Block* p = a->block->parent; p; p = p->parent) {
        if (p->type == BlockType::Details && !p->open) {
            p->open = true;
            changed = true;
        }
    }
    if (changed) {
        LayoutEngine::UpdateVisibility(doc_->root, true);
        Relayout(false);
    }
    // Lay out around the target exactly, then scroll to it.
    Block* target = a->block;
    scroll_y_ = std::clamp(target->y - 8.f, 0.f, MaxScroll());
    Relayout(false);
    float y = target->y;
    if (target->IsLeaf() && a->pos > 0) {
        if (IDWriteTextLayout* tl = layout_.TextLayout(target)) {
            DWRITE_HIT_TEST_METRICS m{};
            FLOAT px, py;
            if (SUCCEEDED(tl->HitTestTextPosition(a->pos, FALSE, &px, &py, &m))) y = layout_.TextOrigin(target).y + m.top;
        }
    }
    ScrollTo(y - layout_.metrics().em * 0.5f, false);
    Relayout(true);
    Invalidate();
    return true;
}

// ============================================================================
// Selection & clipboard
// ============================================================================

void Viewer::CopySelection() {
    if (!doc_ || selection_.end <= selection_.start) return;
    const TextRange r = WholeObjects(selection_);  // a search hit inside a formula copies all of it
    CopyTextToClipboard(hwnd_, doc_->PlainText(r.start, r.end));
}

// `r` grown to cover the formulas and flags it touches (they are drawn,
// and so selected, as one piece). Diagrams show a search hit on its label,
// so that is what gets copied.
TextRange Viewer::WholeObjects(TextRange r) const {
    for (const uint32_t p : {r.start, r.end > r.start ? r.end - 1 : r.start}) {
        const Block* leaf = doc_ ? doc_->LeafAt(p) : nullptr;
        if (!leaf || p < leaf->text_start) continue;
        const uint32_t local = p - leaf->text_start;
        for (const InlineObject& o : leaf->objects) {
            if (o.length > 1 && o.diagram < 0 && local >= o.pos && local < o.pos + o.length) {
                r.start = std::min(r.start, leaf->text_start + o.pos);
                r.end = std::max(r.end, leaf->text_start + o.pos + o.length);
            }
        }
    }
    return r;
}

void Viewer::CopyCode(Block* code) {
    std::wstring text;
    for (wchar_t c : code->text) {
        if (c == L'\n') text += L"\r\n";
        else text.push_back(c);
    }
    CopyTextToClipboard(hwnd_, text);
    copied_block_ = code;
    SetTimer(hwnd_, kTimerCopied, 1500, nullptr);
    Invalidate();
}

void Viewer::SetZoom(int percent) {
    percent = std::clamp(percent, 30, 400);
    if (percent == zoom_percent_) return;
    zoom_percent_ = percent;
    GlobalSettings().zoom_percent = percent;
    SaveSettings();
    ApplySettings(true);
    RefreshSettingsDialog();
    FlashStatus(std::wstring(Tr(L"Zoom: ", L"Масштаб: ")) + std::to_wstring(percent) + L"%");
}

void Viewer::FlashStatus(const std::wstring& text) {
    SetStatus(text);
    SetTimer(hwnd_, kTimerCopied, 1200, nullptr);
}

// ============================================================================
// Zoomable objects
// ============================================================================

const DrawnObject* Viewer::ObjectAt(float x, float y) const {
    for (auto it = drawn_.rbegin(); it != drawn_.rend(); ++it) {  // topmost first
        const D2D1_RECT_F& r = it->visible;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return &*it;
    }
    return nullptr;
}

const DrawnObject* Viewer::Drawn(const InlineObject* o) const {
    for (const DrawnObject& d : drawn_) {
        if (d.object == o) return &d;
    }
    return nullptr;
}

ZoomToolbar Viewer::ObjectToolbar(const DrawnObject& d) const {
    return MakeZoomToolbar(d.visible, std::clamp(layout_.metrics().em, 12.f, 22.f), false);
}

// Which object (and which of its buttons) the mouse is over.
void Viewer::UpdateObjectHover(float x, float y) {
    InlineObject* object = nullptr;
    int button = -1;
    if (!full_.active()) {
        if (const DrawnObject* d = ObjectAt(x, y)) {
            object = d->object;
            button = ZoomButtonAt(ObjectToolbar(*d), x, y);
        }
    }
    if (object != hover_object_ || button != hover_button_) {
        hover_object_ = object;
        hover_button_ = button;
        Invalidate();
    }
}

void Viewer::ShowZoomStatus(float zoom, const InlineObject* object) {
    const bool diagram = object && object->diagram >= 0;
    FlashStatus(std::wstring(diagram ? Tr(L"Diagram: ", L"Диаграмма: ") : Tr(L"Image: ", L"Изображение: ")) +
                std::to_wstring(std::lround(zoom * 100.f)) + L"%");
}

void Viewer::ZoomObjectBy(const DrawnObject& d, float factor, float x, float y) {
    ZoomObject(*d.object, d.box, factor, x, y);
    if (pan_object_ == d.object) {  // zoomed while dragging: drag on from here
        pan_x_ = static_cast<float>(last_mouse_.x) / Scale();
        pan_y_ = static_cast<float>(last_mouse_.y) / Scale();
        pan_view_x_ = d.object->view_x;
        pan_view_y_ = d.object->view_y;
    }
    ShowZoomStatus(d.object->zoom, d.object);
    Invalidate();
}

void Viewer::ObjectButton(const DrawnObject& d, ZoomButton button) {
    const float cx = (d.visible.left + d.visible.right) * 0.5f, cy = (d.visible.top + d.visible.bottom) * 0.5f;
    const unsigned off = ObjectButtonsOff(*d.object);
    if ((button == ZoomButton::ZoomOut && (off & 1u)) || (button == ZoomButton::ZoomIn && (off & 2u)) ||
        (button == ZoomButton::Reset && (off & 4u))) {
        return;
    }
    switch (button) {
    case ZoomButton::ZoomIn: ZoomObjectBy(d, kButtonZoomStep, cx, cy); break;
    case ZoomButton::ZoomOut: ZoomObjectBy(d, 1.f / kButtonZoomStep, cx, cy); break;
    case ZoomButton::Reset:
        ResetObjectView(*d.object);
        ShowZoomStatus(1.f, d.object);
        Invalidate();
        break;
    case ZoomButton::Expand: OpenFullView(d.leaf, d.object); break;
    case ZoomButton::Close: break;
    }
}

bool Viewer::OpenFullView(Block* leaf, const InlineObject* object) {
    if (!object) return false;
    float w = 0, h = 0;  // natural size: an image's pixels, a diagram at the text's size
    if (object->diagram >= 0) {
        const auto scene = layout_.Diagram(object->diagram);
        if (!scene) return false;
        w = scene->width * layout_.DiagramScale();
        h = scene->height * layout_.DiagramScale();
    } else {
        const ImageEntry* e = object->image >= 0 ? images_.Entry(object->image) : nullptr;
        if (!e || e->state != ImageState::Ready || e->px_w == 0 || e->px_h == 0) return false;
        w = static_cast<float>(e->px_w);
        h = static_cast<float>(e->px_h);
    }
    pan_object_ = nullptr;
    hover_object_ = nullptr;
    hover_button_ = -1;
    hover_block_ = nullptr;
    copy_hover_ = false;
    SetHoverLink(nullptr, -1);
    full_.Open(leaf, object, w, h, ViewRect());
    HookKeys(true);
    SetStatus(L"");
    Invalidate();
    return true;
}

void Viewer::CloseFullView() {
    full_.Close();
    if (!esc_held_) HookKeys(false);  // else once Esc is released (KeyHook)
    full_closed_at_ = GetTickCount();
    full_pan_ = false;
    full_backdrop_down_ = false;
    mouse_down_ = false;  // the press that closed it is not a click on the document
    hover_button_ = -1;
    SetStatus(L"");
    Invalidate();
}

void Viewer::DrawFullView() {
    const D2D1_RECT_F view = ViewRect();
    if (full_.object && full_.object->diagram >= 0) {
        if (const auto scene = layout_.Diagram(full_.object->diagram)) {
            DrawDiagram(rt_.Get(), *scene, full_.ContentRect(view), dark_, nullptr, nullptr);
        }
    } else if (full_.object && full_.object->image >= 0) {
        images_.Draw(rt_.Get(), full_.object->image, full_.ContentRect(view), &palette_, G().dw.Get(), kFullViewUiEm);
    }
    DrawZoomToolbar(rt_.Get(), renderer_.icons(), palette_, FullViewToolbar(), hover_button_, FullViewButtonsOff(full_));
}

void Viewer::FullViewButton(ZoomButton button) {
    const D2D1_RECT_F v = ViewRect();
    const float cx = (v.left + v.right) * 0.5f, cy = (v.top + v.bottom) * 0.5f;
    const unsigned off = FullViewButtonsOff(full_);
    if ((button == ZoomButton::ZoomOut && (off & 1u)) || (button == ZoomButton::ZoomIn && (off & 2u)) ||
        (button == ZoomButton::Reset && (off & 4u))) {
        return;
    }
    switch (button) {
    case ZoomButton::ZoomIn: full_.ZoomAt(kButtonZoomStep, cx, cy, v); break;
    case ZoomButton::ZoomOut: full_.ZoomAt(1.f / kButtonZoomStep, cx, cy, v); break;
    case ZoomButton::Reset: full_.Reset(v); break;
    case ZoomButton::Expand:
    case ZoomButton::Close: CloseFullView(); return;
    }
    ShowZoomStatus(full_.scale, full_.object);
    Invalidate();
}

void Viewer::FullViewKey(WPARAM vk) {
    const D2D1_RECT_F v = ViewRect();
    const float step = std::min(v.right - v.left, v.bottom - v.top) * 0.1f;
    switch (vk) {
    case VK_ESCAPE: case VK_BACK: case VK_RETURN: CloseFullView(); break;
    case VK_OEM_PLUS: case VK_ADD: FullViewButton(ZoomButton::ZoomIn); break;
    case VK_OEM_MINUS: case VK_SUBTRACT: FullViewButton(ZoomButton::ZoomOut); break;
    case '0': case VK_NUMPAD0: FullViewButton(ZoomButton::Reset); break;
    case VK_LEFT: full_.PanBy(step, 0, v); Invalidate(); break;
    case VK_RIGHT: full_.PanBy(-step, 0, v); Invalidate(); break;
    case VK_UP: full_.PanBy(0, step, v); Invalidate(); break;
    case VK_DOWN: full_.PanBy(0, -step, v); Invalidate(); break;
    default: break;
    }
}

void Viewer::FullViewMenu(POINT pt) {
    if (pt.x == -1 && pt.y == -1) {
        pt = POINT{20, 20};
        ClientToScreen(hwnd_, &pt);
    }
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (full_.scale <= full_.min_scale * 1.0001f ? MF_GRAYED : 0), kMenuResetView,
                Tr(L"&Reset zoom\t0", L"&Сбросить масштаб\t0"));
    AppendMenuW(menu, MF_STRING, kMenuCopyImage, Tr(L"Copy &image address", L"Копировать адрес &изображения"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuCloseView, Tr(L"&Close\tEsc", L"&Закрыть\tEsc"));
    const uint32_t gen = doc_gen_;
    const HWND self = hwnd_;
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, self, nullptr);
    DestroyMenu(menu);
    if (!Survived(self, this) || gen != doc_gen_ || !full_.active()) return;
    switch (cmd) {
    case kMenuResetView: FullViewButton(ZoomButton::Reset); break;
    case kMenuCopyImage: CopyTextToClipboard(hwnd_, images_.ResolvedUrl(full_.object->image)); break;
    case kMenuCloseView: CloseFullView(); break;
    default: break;
    }
}

// ============================================================================
// Search (ListSearchText)
// ============================================================================

bool Viewer::Search(const std::wstring& text, int flags) {
    if (!doc_ || text.empty()) return false;
    if (full_.active()) CloseFullView();
    const bool match_case = (flags & lcs_matchcase) != 0;
    const bool whole = (flags & lcs_wholewords) != 0;
    const bool backwards = (flags & lcs_backwards) != 0;
    if (!folded_valid_) {
        global_text_ = doc_->GlobalText();
        folded_text_ = ToLower(global_text_);
        if (folded_text_.size() != global_text_.size()) folded_text_ = global_text_;  // length-changing folds
        folded_valid_ = true;
    }
    const std::wstring& global = global_text_;
    const std::wstring& hay = match_case ? global : folded_text_;
    const std::wstring needle = match_case ? text : ToLower(text);
    if (needle.size() > hay.size()) return false;

    auto is_match = [&](size_t pos) {
        if (hay.compare(pos, needle.size(), needle) != 0) return false;
        if (!whole) return true;
        const bool left_ok = pos == 0 || !IsWordChar(global[pos - 1]);
        const size_t end = pos + needle.size();
        const bool right_ok = end >= global.size() || !IsWordChar(global[end]);
        return left_ok && right_ok;
    };
    // Of a diagram only the labels show: a match elsewhere in its source
    // (the type, node ids, arrows, styles) would be found but never seen.
    auto shown = [&](size_t pos) {
        Block* leaf = doc_->LeafAt(static_cast<uint32_t>(pos));
        if (!leaf || leaf->objects.empty()) return true;
        const uint32_t h0 = static_cast<uint32_t>(pos) - leaf->text_start, h1 = h0 + static_cast<uint32_t>(needle.size());
        for (const InlineObject& o : leaf->objects) {
            if (o.diagram < 0 || h1 <= o.pos || h0 >= o.pos + o.length) continue;
            if (!layout_.Diagram(o.diagram)) return true;  // its source shows
            return LabelOver(o.diagram, h0 > o.pos ? h0 - o.pos : 0, h1 - o.pos) != nullptr;
        }
        return true;
    };

    // All matches (for highlighting), capped.
    // hits_ is also dropped by a new document or a mouse selection.
    const bool new_query = text != search_text_ || (flags & lcs_findfirst) || hits_.empty();
    if (new_query) {
        hits_.clear();
        for (size_t pos = hay.find(needle); pos != std::wstring::npos && hits_.size() < 20000;
             pos = hay.find(needle, pos + 1)) {
            if (is_match(pos) && shown(pos)) hits_.push_back(TextRange{static_cast<uint32_t>(pos), static_cast<uint32_t>(pos + needle.size())});
        }
        search_text_ = text;
    }
    if (hits_.empty()) {
        current_hit_ = -1;
        Invalidate();
        return false;
    }

    // Starting point.
    uint32_t from;
    if (backwards && selection_.end <= selection_.start) {
        // Nothing selected (just opened, or a click): back from the bottom
        // of the view, as forward starts at its top.
        Block* last = LeafAtY(scroll_y_ + ViewHeight());
        from = last ? last->text_end() : doc_->text_length;
    } else if (flags & lcs_findfirst) {
        Block* first = LeafAtY(scroll_y_);
        from = first ? first->text_start : 0;
        if (backwards) from = selection_.start;
    } else {
        from = backwards ? selection_.start : (selection_.end > selection_.start ? selection_.start + 1 : selection_.end);
    }
    int found = -1;
    if (!backwards) {
        auto it = std::lower_bound(hits_.begin(), hits_.end(), from,
                                   [](const TextRange& r, uint32_t p) { return r.start < p; });
        if (it != hits_.end()) found = static_cast<int>(it - hits_.begin());
    } else {
        auto it = std::lower_bound(hits_.begin(), hits_.end(), from,
                                   [](const TextRange& r, uint32_t p) { return r.start < p; });
        if (it != hits_.begin()) found = static_cast<int>(it - hits_.begin()) - 1;
    }
    if (found < 0) {
        current_hit_ = -1;
        Invalidate();
        return false;
    }
    current_hit_ = found;
    const TextRange hit = hits_[static_cast<size_t>(found)];
    selection_ = hit;
    anchor_ = hit.start;
    Block* leaf = doc_->LeafAt(hit.start);
    if (leaf) {
        bool opened = false;
        for (Block* p = leaf->parent; p; p = p->parent) {
            if (p->type == BlockType::Details && !p->open) {
                p->open = true;
                opened = true;
            }
        }
        if (opened) {
            LayoutEngine::UpdateVisibility(doc_->root, true);
            Relayout(true);
        }
        Relayout(true);  // exact positions for what follows
        // Where the hit shows, in the leaf's text layout: its text, or the
        // diagram label it falls in (a zoomed diagram moves to show it).
        const uint32_t h0 = hit.start - leaf->text_start, h1 = hit.end - leaf->text_start;
        IDWriteTextLayout* tl = layout_.TextLayout(leaf);
        D2D1_RECT_F target{};
        bool have_target = false;
        DWRITE_HIT_TEST_METRICS m{};
        FLOAT px = 0, py = 0;
        if (tl && SUCCEEDED(tl->HitTestTextPosition(h0, FALSE, &px, &py, &m))) {
            target = D2D1::RectF(px, m.top, px, m.top + m.height);
            have_target = true;
        }
        for (InlineObject& o : leaf->objects) {
            if (o.diagram < 0 || h1 <= o.pos || h0 >= o.pos + o.length) continue;
            const auto scene = layout_.Diagram(o.diagram);
            if (!scene || scene->width <= 0) continue;
            for (const DiagramScene::Text& t : scene->texts) {
                if (t.src_length == 0 || h0 >= o.pos + t.src_start + t.src_length || h1 <= o.pos + t.src_start) continue;
                if (IsZoomed(o)) {
                    const float fx = (t.origin.x + t.w / 2) / scene->width, fy = (t.origin.y + t.h / 2) / scene->height;
                    o.view_x = std::clamp(fx - 0.5f / o.zoom, 0.f, 1.f - 1.f / o.zoom);
                    o.view_y = std::clamp(fy - 0.5f / o.zoom, 0.f, 1.f - 1.f / o.zoom);
                }
                // Where DiagramObject::Draw puts it: standing on its line's baseline.
                if (tl && SUCCEEDED(tl->HitTestTextPosition(o.pos, FALSE, &px, &py, &m))) {
                    float base = m.top + m.height;
                    UINT32 count = 0;
                    tl->GetLineMetrics(nullptr, 0, &count);
                    std::vector<DWRITE_LINE_METRICS> lines(count);
                    if (count > 0 && SUCCEEDED(tl->GetLineMetrics(lines.data(), count, &count))) {
                        float top = 0;
                        uint32_t start = 0;
                        for (const DWRITE_LINE_METRICS& l : lines) {
                            if (o.pos < start + l.length) {
                                base = top + l.baseline;
                                break;
                            }
                            start += l.length;
                            top += l.height;
                        }
                    }
                    const float h = scene->height * m.width / scene->width;
                    const D2D1_RECT_F box = D2D1::RectF(m.left, base - h, m.left + m.width, base);
                    const D2D1_RECT_F drawn = IsZoomed(o) ? ZoomedRect(box, o) : box;
                    const float s = (drawn.right - drawn.left) / scene->width;
                    target = D2D1::RectF(std::clamp(drawn.left + t.origin.x * s, box.left, box.right),
                                         std::clamp(drawn.top + t.origin.y * s, box.top, box.bottom),
                                         std::clamp(drawn.left + (t.origin.x + t.w) * s, box.left, box.right),
                                         std::clamp(drawn.top + (t.origin.y + t.h) * s, box.top, box.bottom));
                    have_target = true;
                }
                break;
            }
        }
        // Horizontal scroll inside wide code blocks, tables and paragraphs.
        for (Block* p = leaf; p && have_target; p = p->parent) {
            if (p->content_w > 0) {
                const float x = layout_.TextOrigin(leaf).x + target.left - p->x;
                const float x1 = layout_.TextOrigin(leaf).x + target.right - p->x;
                if (x < p->scroll_x || x1 > p->scroll_x + p->w - 40.f) {
                    p->scroll_x = std::clamp(x - p->w * 0.3f, 0.f, std::max(0.f, p->content_w - p->w));
                }
            }
        }
        ScrollIntoView(leaf, h0, true, have_target ? &target : nullptr);
    }
    Invalidate();
    return true;
}

// ============================================================================
// Commands from Total Commander
// ============================================================================

bool Viewer::Command(int command, int parameter) {
    switch (command) {
    case lc_copy:
        CopySelection();
        return true;
    case lc_selectall:
        if (doc_ && !doc_->leaves.empty()) {
            selection_ = TextRange{0, doc_->text_length};
            anchor_ = 0;
            Invalidate();
        }
        return true;
    case lc_setpercent:
        ScrollTo(MaxScroll() * static_cast<float>(std::clamp(parameter, 0, 100)) / 100.f, false);
        return true;
    case lc_newparams:
        flags_ = parameter;
        ApplySettings(true);
        return true;
    default:
        return false;
    }
}

// ============================================================================
// Context menu
// ============================================================================

void Viewer::OnContextMenu(int sx, int sy) {
    POINT pt{sx, sy};
    if (sx == -1 && sy == -1) {
        pt = POINT{20, 20};
        ClientToScreen(hwnd_, &pt);
    }
    POINT cp = pt;
    ScreenToClient(hwnd_, &cp);
    const Hit hit = HitTest(cp.x / Scale(), cp.y / Scale() + scroll_y_);
    const Settings& s = GlobalSettings();

    HMENU menu = CreatePopupMenu();
    HMENU theme = CreatePopupMenu();
    const bool has_sel = selection_.end > selection_.start;
    AppendMenuW(menu, MF_STRING | (has_sel ? 0 : MF_GRAYED), kMenuCopy, Tr(L"&Copy\tCtrl+C", L"&Копировать\tCtrl+C"));
    if (hit.link >= 0) {
        AppendMenuW(menu, MF_STRING, kMenuOpenLink, Tr(L"&Open link", L"&Открыть ссылку"));
        AppendMenuW(menu, MF_STRING, kMenuCopyLink, Tr(L"Copy &link address", L"Копировать &адрес ссылки"));
    }
    if (hit.image >= 0) AppendMenuW(menu, MF_STRING, kMenuCopyImage, Tr(L"Copy &image address", L"Копировать адрес &изображения"));
    // The image under the mouse: full-window view, and its zoom.
    InlineObject* menu_object = nullptr;
    Block* menu_leaf = nullptr;
    if (const DrawnObject* d = ObjectAt(cp.x / Scale(), cp.y / Scale())) {
        menu_object = d->object;
        menu_leaf = d->leaf;
    } else if (hit.image >= 0 && hit.leaf) {
        for (InlineObject& o : hit.leaf->objects) {
            if (o.image == hit.image && o.length == 1) {
                menu_object = &o;
                menu_leaf = hit.leaf;
                break;
            }
        }
    }
    if (menu_object) {
        const bool diagram = menu_object->diagram >= 0;
        const ImageEntry* e = diagram ? nullptr : images_.Entry(menu_object->image);
        const bool ready = diagram || (e && e->state == ImageState::Ready && e->px_w && e->px_h);
        AppendMenuW(menu, MF_STRING | (ready ? 0 : MF_GRAYED), kMenuViewFull,
                    diagram ? Tr(L"View diagram in the &whole window", L"Показать диаграмму на всё &окно")
                            : Tr(L"View image in the &whole window", L"Показать изображение на всё &окно"));
        if (IsZoomed(*menu_object)) {
            AppendMenuW(menu, MF_STRING, kMenuResetView,
                        diagram ? Tr(L"Reset diagram &zoom", L"Сбросить &масштаб диаграммы")
                                : Tr(L"Reset image &zoom", L"Сбросить &масштаб изображения"));
        }
    }
    if (hit.code) AppendMenuW(menu, MF_STRING, kMenuCopyCode, Tr(L"Copy co&de block", L"Копировать &блок кода"));
    AppendMenuW(menu, MF_STRING, kMenuSelectAll, Tr(L"Select &all\tCtrl+A", L"Выделить &всё\tCtrl+A"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // Table of contents: jump to a heading.
    std::vector<Block*> toc;
    if (doc_) {
        int min_level = 6;
        for (Block* leaf : doc_->leaves) {
            if (leaf->type == BlockType::Heading) min_level = std::min<int>(min_level, leaf->level);
        }
        for (Block* leaf : doc_->leaves) {
            if (leaf->type == BlockType::Heading && leaf->level <= min_level + 2 && toc.size() < 60) toc.push_back(leaf);
        }
        if (toc.size() > 1) {
            HMENU contents = CreatePopupMenu();
            for (size_t i = 0; i < toc.size(); ++i) {
                std::wstring label(static_cast<size_t>((toc[i]->level - min_level) * 4), L' ');
                const Block* h = toc[i];
                for (size_t k = 0; k < h->text.size(); ++k) {
                    const InlineObject* math = nullptr;
                    for (const InlineObject& o : h->objects) {
                        if (o.math >= 0 && o.pos == k && static_cast<size_t>(o.math) < doc_->maths.size()) math = &o;
                    }
                    if (math) {  // a formula: its TeX without the $ delimiters
                        for (wchar_t m : Trim(doc_->maths[static_cast<size_t>(math->math)].tex)) {
                            if (m == L'&') label += L"&&";
                            else label.push_back(IsSpace(m) ? L' ' : m);
                            if (label.size() > 90) break;
                        }
                        if (label.size() > 90) {
                            label += L"…";
                            break;
                        }
                        k += math->length - 1;
                        continue;
                    }
                    const wchar_t c = h->text[k];
                    if (c == 0xFFFC || c == 0x200B) continue;
                    if (c == L'&') label += L"&&";
                    else label.push_back(c == 0x2028 ? L' ' : c);
                    if (label.size() > 90) {
                        label += L"…";
                        break;
                    }
                }
                AppendMenuW(contents, MF_STRING, kMenuTocBase + static_cast<UINT>(i), label.c_str());
            }
            AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(contents), Tr(L"C&ontents", L"&Оглавление"));
        }
    }
    if (!history_.empty()) AppendMenuW(menu, MF_STRING, kMenuBack, Tr(L"&Back\tBackspace", L"&Назад\tBackspace"));
    AppendMenuW(menu, MF_STRING, kMenuZoomIn, Tr(L"Zoom &in\tCtrl++", L"&Увеличить\tCtrl++"));
    AppendMenuW(menu, MF_STRING, kMenuZoomOut, Tr(L"Zoom &out\tCtrl+-", L"У&меньшить\tCtrl+-"));
    AppendMenuW(menu, MF_STRING | (zoom_percent_ == 100 ? MF_GRAYED : 0), kMenuZoomReset,
                Tr(L"&Reset zoom\tCtrl+0", L"&Сбросить масштаб\tCtrl+0"));
    AppendMenuW(theme, MF_STRING | (s.theme == ThemeMode::Auto ? MF_CHECKED : 0), kMenuThemeAuto,
                Tr(L"&Automatic", L"&Автоматически"));
    AppendMenuW(theme, MF_STRING | (s.theme == ThemeMode::Light ? MF_CHECKED : 0), kMenuThemeLight, Tr(L"&Light", L"&Светлая"));
    AppendMenuW(theme, MF_STRING | (s.theme == ThemeMode::Dark ? MF_CHECKED : 0), kMenuThemeDark, Tr(L"&Dark", L"&Тёмная"));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(theme), Tr(L"&Theme", L"&Тема"));
    AppendMenuW(menu, MF_STRING | (layout_.config().wrap_code ? MF_CHECKED : 0), kMenuWrap,
                Tr(L"&Wrap long code lines", L"&Переносить длинные строки кода"));
    AppendMenuW(menu, MF_STRING | (s.line_numbers ? MF_CHECKED : 0), kMenuLineNumbers,
                Tr(L"Line &numbers in code", L"&Номера строк в коде"));
    AppendMenuW(menu, MF_STRING | (s.max_width == 0 ? MF_CHECKED : 0), kMenuFullWidth,
                Tr(L"&Full-width text", L"Текст на всю &ширину"));
    AppendMenuW(menu, MF_STRING | (s.remote_images ? MF_CHECKED : 0), kMenuRemote,
                Tr(L"Load &remote images", L"Загружать &изображения из сети"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuReload, Tr(L"Re&load\tF5", L"Пере&загрузить\tF5"));
    AppendMenuW(menu, MF_STRING | (s.auto_reload ? MF_CHECKED : 0), kMenuAutoReload,
                Tr(L"Reload when the file &changes", L"Обновлять при &изменении файла"));
    AppendMenuW(menu, MF_STRING, kMenuOpenExternal, Tr(L"Open in default &editor", L"Открыть в &редакторе"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSettings, Tr(L"&Settings…", L"Н&астройки…"));
    AppendMenuW(menu, MF_STRING, kMenuAbout, L"mdview " MDVIEW_VERSION L"…");

    const uint32_t gen = doc_gen_;
    const HWND self = hwnd_;
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, self, nullptr);
    DestroyMenu(menu);
    if (!Survived(self, this)) return;  // closed meanwhile
    if (gen != doc_gen_) return;  // reloaded meanwhile: `hit` and `toc` are stale
    // Menu switches apply to every open viewer, like the settings dialog.
    auto change = [](auto&& edit) {
        const Settings old = GlobalSettings();
        edit(GlobalSettings());
        SaveSettings();
        ApplySettingsToViewers(old);
        RefreshSettingsDialog();
    };
    if (cmd >= kMenuTocBase && cmd < kMenuTocBase + toc.size()) {
        Block* heading = toc[cmd - kMenuTocBase];
        // Collapsed <details> around the heading are opened first.
        bool opened = false;
        for (Block* p = heading->parent; p; p = p->parent) {
            if (p->type == BlockType::Details && !p->open) {
                p->open = true;
                opened = true;
            }
        }
        if (opened) {
            LayoutEngine::UpdateVisibility(doc_->root, true);
            Relayout(true);
        }
        scroll_y_ = std::clamp(heading->y - layout_.metrics().em, 0.f, MaxScroll());
        Relayout(false);
        ScrollTo(heading->y - layout_.metrics().em, false);
        Relayout(true);
        return;
    }
    switch (cmd) {
    case kMenuCopy: CopySelection(); break;
    case kMenuOpenLink: OpenLink(hit.link); break;
    case kMenuCopyLink:
        if (doc_ && hit.link >= 0) CopyTextToClipboard(hwnd_, doc_->links[static_cast<size_t>(hit.link)].url);
        break;
    case kMenuCopyImage: CopyTextToClipboard(hwnd_, images_.ResolvedUrl(hit.image)); break;
    case kMenuViewFull: OpenFullView(menu_leaf, menu_object); break;
    case kMenuResetView:
        if (menu_object) {
            ResetObjectView(*menu_object);
            Invalidate();
        }
        break;
    case kMenuCopyCode: if (hit.code) CopyCode(hit.code); break;
    case kMenuSelectAll: Command(lc_selectall, 0); break;
    case kMenuBack: GoBack(); break;
    case kMenuZoomIn: SetZoom(zoom_percent_ + 10); break;
    case kMenuZoomOut: SetZoom(zoom_percent_ - 10); break;
    case kMenuZoomReset: SetZoom(100); break;
    case kMenuThemeAuto: change([](Settings& ws) { ws.theme = ThemeMode::Auto; }); break;
    case kMenuThemeLight: change([](Settings& ws) { ws.theme = ThemeMode::Light; }); break;
    case kMenuThemeDark: change([](Settings& ws) { ws.theme = ThemeMode::Dark; }); break;
    case kMenuWrap: {
        const bool wrap = layout_.config().wrap_code;
        change([wrap](Settings& ws) { ws.wrap_code = wrap ? 0 : 1; });
        break;
    }
    case kMenuLineNumbers: change([](Settings& ws) { ws.line_numbers = !ws.line_numbers; }); break;
    case kMenuFullWidth:
        change([](Settings& ws) { ws.max_width = ws.max_width == 0 ? Settings{}.max_width : 0; });
        break;
    case kMenuRemote: change([](Settings& ws) { ws.remote_images = !ws.remote_images; }); break;
    case kMenuReload: Reload(); break;
    case kMenuAutoReload: change([](Settings& ws) { ws.auto_reload = !ws.auto_reload; }); break;
    case kMenuOpenExternal:
        if (!path_.empty()) {
            const std::wstring path = path_;  // ShellExecute may outlive the viewer (see OnLButtonUp)
            ShellExecuteW(hwnd_, L"edit", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
    case kMenuSettings: ShowSettingsDialog(hwnd_); break;  // the viewer may be gone afterwards
    case kMenuAbout: {
        const std::wstring text = std::wstring(L"mdview " MDVIEW_VERSION L"\n\n") +
                                  Tr(L"GitHub Flavored Markdown viewer for Total Commander.",
                                     L"Просмотр GitHub Flavored Markdown для Total Commander.") +
                                  L"\nDirect2D + DirectWrite, md4c.\n\n" + Tr(L"Settings: ", L"Настройки: ") +
                                  SettingsPath();
        MessageBoxW(hwnd_, text.c_str(), L"mdview", MB_OK | MB_ICONINFORMATION);
        break;
    }
    default: break;
    }
}

// ============================================================================
// Timers & window procedure
// ============================================================================

void Viewer::OnTimer(UINT_PTR id) {
    switch (id) {
    case kTimerRefine: {
        if (!doc_ || !layout_.HasEstimates()) {
            KillTimer(hwnd_, kTimerRefine);
            return;
        }
        if (mouse_down_ || anim_ || printing_) return;  // don't disturb interaction
        if (layout_.Refine(6.0)) {
            const float old_max = MaxScroll();
            Relayout(true);
            if (std::fabs(old_max - MaxScroll()) > 0.5f) Invalidate();
        }
        if (!layout_.HasEstimates()) KillTimer(hwnd_, kTimerRefine);
        break;
    }
    case kTimerCopied:
        KillTimer(hwnd_, kTimerCopied);
        copied_block_ = nullptr;
        if (hover_link_ < 0) SetStatus(L"");
        Invalidate();
        break;
    case kTimerAutoScroll: {
        const float y = last_mouse_.y / Scale();
        const float vh = ViewHeight();
        const float d = y < 0 ? y : (y > vh ? y - vh : 0.f);
        if (d == 0) {
            KillTimer(hwnd_, kTimerAutoScroll);
            return;
        }
        ScrollTo(scroll_y_ + d * 0.5f, false);
        OnMouseMove(last_mouse_.x, last_mouse_.y);
        break;
    }
    case kTimerReload: {
        KillTimer(hwnd_, kTimerReload);
        uint64_t t = 0, size = 0;
        // Missing for a moment (saved via rename)? The next change retries.
        if (FileStamp(t, size) && (t != file_time_ || size != file_size_)) Reload();
        break;
    }
    case kTimerWatchPoll: {
        // Checked on the thread pool, one check at a time (PollCallback).
        if (poll_pending_ || path_.empty()) break;
        auto* check = new PollCheck{hwnd_, ExtendedPath(path_), file_time_, file_size_, watch_gen_};
        TP_CALLBACK_ENVIRON env;
        InitializeThreadpoolEnvironment(&env);
        SetThreadpoolCallbackLibrary(&env, reinterpret_cast<HMODULE>(&__ImageBase));  // stays loaded meanwhile
        SetThreadpoolCallbackRunsLong(&env);
        if (TrySubmitThreadpoolCallback(&PollCallback, check, &env)) poll_pending_ = true;
        else delete check;
        DestroyThreadpoolEnvironment(&env);
        break;
    }
    case kTimerGif:
        KillTimer(hwnd_, kTimerGif);
        if (images_.AdvanceAnimations(NowMs())) {
            Invalidate();
        } else {
            ScheduleGifFrame();
        }
        break;
    case kTimerRepeat: {
        SetTimer(hwnd_, kTimerRepeat, 60, nullptr);
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd_, &pt);
        const D2D1_RECT_F thumb = ScrollbarThumb();
        const float y = pt.y / Scale();
        if ((repeat_dir_ < 0 && y < thumb.top) || (repeat_dir_ > 0 && y > thumb.bottom)) {
            ScrollBy(repeat_dir_ * (ViewHeight() - LineStep()), false);
        }
        break;
    }
    default: break;
    }
}

LRESULT CALLBACK Viewer::StaticProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Viewer* self = reinterpret_cast<Viewer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<Viewer*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        const LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
        // (A window that failed to be created leaves its viewer to CreateViewer.)
        if (self->created_) delete self;
        else self->hwnd_ = nullptr;
        return r;
    }
    try {
        return self->Proc(msg, wp, lp);
    } catch (...) {
        // Never let an exception cross into Total Commander.
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// Total Commander's Lister acts on some keys (Esc closes it, digits switch
// the view mode) before the plugin window sees them. While a full-window view
// is open, a hook on the UI thread takes those keys first.
namespace {
HHOOK g_key_hook = nullptr;
int g_key_hook_users = 0;
}  // namespace

LRESULT CALLBACK Viewer::KeyHook(int code, WPARAM wp, LPARAM lp) {
    GUITHREADINFO gui{sizeof gui};
    const bool menu = GetGUIThreadInfo(GetCurrentThreadId(), &gui) &&
                      (gui.flags & (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_SYSTEMMENUMODE));
    if (code == HC_ACTION && wp == PM_REMOVE && !menu) {  // a menu takes its own keys
        MSG* m = reinterpret_cast<MSG*>(lp);
        const bool esc = m->wParam == VK_ESCAPE;
        if (m->message == WM_KEYDOWN && (esc || m->wParam == '0' || m->wParam == VK_NUMPAD0)) {
            const bool repeat = (m->lParam & (1 << 30)) != 0;
            for (Viewer* v : G().viewers) {
                if (!(m->hwnd == v->hwnd_ || GetFocus() == v->hwnd_)) continue;
                if (v->full_.active()) {
                    const WPARAM vk = m->wParam;
                    m->message = WM_NULL;  // not for the Lister
                    if (esc) v->esc_held_ = true;
                    try {
                        v->FullViewKey(vk);
                    } catch (...) {
                    }
                } else if (esc && v->esc_held_) {
                    // Esc closed the view: its auto-repeat must not close the
                    // Lister as well; a new press does.
                    if (repeat) {
                        m->message = WM_NULL;
                    } else {
                        v->esc_held_ = false;
                        v->HookKeys(false);
                    }
                }
                break;
            }
        } else if (m->message == WM_KEYUP && esc) {
            for (Viewer* v : G().viewers) {
                if (!v->esc_held_) continue;
                v->esc_held_ = false;
                if (!v->full_.active()) v->HookKeys(false);
            }
        }
    }
    return CallNextHookEx(g_key_hook, code, wp, lp);
}

void Viewer::HookKeys(bool on) {
    if (on == key_hook_) return;
    key_hook_ = on;
    if (on) {
        if (g_key_hook_users++ == 0) {
            g_key_hook = SetWindowsHookExW(WH_GETMESSAGE, &Viewer::KeyHook, nullptr, GetCurrentThreadId());
        }
    } else if (--g_key_hook_users == 0 && g_key_hook) {
        UnhookWindowsHookEx(g_key_hook);
        g_key_hook = nullptr;
    }
}

LRESULT Viewer::Proc(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == OpenSettingsMessage()) {
        ShowSettingsDialog(hwnd_);  // the viewer may be gone afterwards
        return 0;
    }
    if (msg == FileChangedMessage()) {
        if (watch_change_ != INVALID_HANDLE_VALUE || watch_wait_) OnFileChanged();
        return 0;
    }
    if (msg == PollDoneMessage()) {
        if (static_cast<uint32_t>(lp) == watch_gen_) {
            poll_pending_ = false;
            if (wp) SetTimer(hwnd_, kTimerReload, kReloadDelayMs, nullptr);
        }
        return 0;
    }
    if (msg == ImageLoadedMessage()) {
        if (images_.CompletePending()) {
            layout_.InvalidateMeasurements();
            Relayout(true);
        }
        Invalidate();
        return 0;
    }
    switch (msg) {
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (rt_) {
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            rt_->Resize(D2D1::SizeU(std::max<LONG>(rc.right, 1), std::max<LONG>(rc.bottom, 1)));
        }
        if (doc_ && layout_.laid_out() && std::fabs(laid_out_width_ - ViewWidth()) > 0.01f) {
            Relayout(true);
        } else {
            scroll_y_ = std::clamp(scroll_y_, 0.f, MaxScroll());
        }
        if (full_.active()) full_.Fit(ViewRect());
        UpdatePercent();
        Invalidate();
        return 0;
    case WM_DPICHANGED_AFTERPARENT: {
        const ScrollAnchor a = CaptureAnchor();
        dpi_ = WindowDpi(hwnd_);
        if (rt_) rt_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        Relayout(false);
        RestoreAnchor(a);
        Invalidate();
        return 0;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp), msg == WM_MOUSEHWHEEL, GET_KEYSTATE_WPARAM(wp),
                     POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (msg == WM_SYSKEYDOWN && wp != VK_LEFT) break;
        OnKeyDown(wp);
        return 0;
    case WM_LBUTTONDOWN:
        OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), false);
        return 0;
    case WM_LBUTTONDBLCLK:
        OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), true);
        return 0;
    case WM_LBUTTONUP:
        OnLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSELEAVE:
        tracking_mouse_ = false;
        if (!mouse_down_) {
            hover_block_ = nullptr;
            copy_hover_ = false;
            scrollbar_hover_ = false;
            hover_object_ = nullptr;
            hover_button_ = -1;
            SetHoverLink(nullptr, -1);
            Invalidate();
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lp) != hwnd_) {
            mouse_down_ = false;
            selecting_ = false;
            thumb_drag_ = false;
            hscroll_drag_ = nullptr;
            pan_object_ = nullptr;
            full_pan_ = false;
            full_backdrop_down_ = false;
            KillTimer(hwnd_, kTimerAutoScroll);
            KillTimer(hwnd_, kTimerRepeat);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            const float x = pt.x / Scale(), y = pt.y / Scale();
            LPCWSTR cursor = IDC_ARROW;
            if (full_.active()) {
                const D2D1_RECT_F r = full_.ContentRect(ViewRect());
                const int b = ZoomButtonAt(FullViewToolbar(), x, y);
                if (b >= 0 && !((FullViewButtonsOff(full_) >> b) & 1u)) cursor = IDC_HAND;
                else if (b < 0 && full_.CanPan(ViewRect()) && x >= r.left && x < r.right && y >= r.top && y < r.bottom) cursor = IDC_SIZEALL;
            } else if (!(MaxScroll() > 0.5f && x >= ViewWidth() - 16.f)) {
                const DrawnObject* d = ObjectAt(x, y);
                const Hit hit = HitTest(x, y + scroll_y_);
                if (d && d->object == hover_object_ && hover_button_ >= 0) {
                    if (!((ObjectButtonsOff(*d->object) >> hover_button_) & 1u)) cursor = IDC_HAND;
                }
                else if (d && IsZoomed(*d->object)) cursor = IDC_SIZEALL;
                else if (d && d->object->diagram >= 0) cursor = IDC_ARROW;  // a picture, not text
                else if (hit.link >= 0 || hit.summary || copy_hover_ || (hit.image >= 0 && hit.on_text)) cursor = IDC_HAND;
                else if (hit.on_text) cursor = IDC_IBEAM;
            }
            SetCursor(LoadCursorW(nullptr, cursor));
            return TRUE;
        }
        break;
    case WM_CONTEXTMENU:
        if (full_.active()) {
            FullViewMenu(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            return 0;
        }
        OnContextMenu(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_COPY:
        CopySelection();
        return 0;
    case WM_TIMER:
        OnTimer(wp);
        return 0;
    case WM_SETFOCUS:
        if (parent_) PostMessageW(parent_, WM_COMMAND, MAKEWPARAM(0, itm_focus), reinterpret_cast<LPARAM>(hwnd_));
        Invalidate();
        return 0;
    case WM_KILLFOCUS:
        Invalidate();
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETTINGCHANGE:
        if (GlobalSettings().theme == ThemeMode::Auto && !Host().tc_reports_dark_mode()) ApplySettings(true);
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

bool Viewer::Print(const wchar_t* /*printer*/, const RECT* margins) {
    if (!doc_ || printing_) return false;
    LayoutConfig cfg = MakeLayoutConfig();
    cfg.dark = false;             // paper is white
    cfg.zoom = 0.85f;
    cfg.max_content_width = 0;
    cfg.wrap_code = true;         // nothing can scroll on paper
    cfg.page_padding = 0;
    const size_t slash = path_.find_last_of(L"\\/");
    const std::wstring title = slash == std::wstring::npos ? path_ : path_.substr(slash + 1);
    const ScrollAnchor anchor = CaptureAnchor();
    // The print layout borrows the document's block boxes; the dialogs' modal
    // loops must not paint or re-lay-out the viewer meanwhile. They may also
    // close it (PrintDocument gives up then).
    bool ok = false;
    const HWND self = hwnd_;
    printing_ = true;
    try {
        ok = PrintDocument(self, *doc_, cfg, title, margins);
    } catch (...) {
        ok = false;
    }
    if (!Survived(self, this)) return ok;
    printing_ = false;
    if (reload_after_print_) {
        reload_after_print_ = false;
        Reload();
        return ok;
    }
    Relayout(false);
    RestoreAnchor(anchor);
    Relayout(true);
    Invalidate();
    return ok;
}

// ============================================================================
// Public API
// ============================================================================

HWND CreateViewer(HWND parent, const wchar_t* path, int show_flags) {
    auto viewer = std::make_unique<Viewer>();
    if (!viewer->Create(parent, show_flags)) {
        if (HWND h = viewer->hwnd()) {
            DestroyWindow(h);  // WM_NCDESTROY deletes the viewer
            viewer.release();
        }
        return nullptr;
    }
    Viewer* v = viewer.release();  // owned by the window from now on
    HWND hwnd = v->hwnd();
    try {
        v->Load(path, show_flags);
    } catch (...) {
        DestroyWindow(hwnd);  // TC gets no window: leave none behind
        throw;
    }
    return hwnd;
}

bool ViewerIsDark(HWND hwnd) {
    const Viewer* v = ViewerFromHwnd(hwnd);
    return v && v->dark();
}

void ApplySettingsToViewers(const Settings& old) {
    PublishSettings();  // every change of GlobalSettings() comes here
    // A viewer may reload its document; none is created or destroyed meanwhile,
    // but iterate over a copy anyway.
    const std::vector<Viewer*> viewers = G().viewers;
    for (Viewer* v : viewers) {
        if (std::find(G().viewers.begin(), G().viewers.end(), v) != G().viewers.end()) v->SettingsChanged(old);
    }
}

UINT OpenSettingsMessage() {
    static const UINT msg = RegisterWindowMessageW(L"MdViewWlxOpenSettings");
    return msg;
}

void UnregisterViewerClass() { UnregisterClassW(kClassName, reinterpret_cast<HINSTANCE>(&__ImageBase)); }

Viewer* ViewerFromHwnd(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return nullptr;
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    if (wcscmp(cls, kClassName) != 0) return nullptr;
    return reinterpret_cast<Viewer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

bool ViewerLoadFile(HWND hwnd, const wchar_t* path, int show_flags) {
    Viewer* v = ViewerFromHwnd(hwnd);
    return v && v->Load(path, show_flags);
}

bool ViewerSearch(HWND hwnd, const wchar_t* text, int flags) {
    Viewer* v = ViewerFromHwnd(hwnd);
    return v && v->Search(text ? text : L"", flags);
}

bool ViewerCommand(HWND hwnd, int command, int parameter) {
    Viewer* v = ViewerFromHwnd(hwnd);
    return v && v->Command(command, parameter);
}

bool ViewerPrint(HWND hwnd, const wchar_t* printer, const RECT* margins) {
    Viewer* v = ViewerFromHwnd(hwnd);
    return v && v->Print(printer, margins);
}

}  // namespace mdv
