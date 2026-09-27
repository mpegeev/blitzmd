#include "core/settings_dialog.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <dwrite_1.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

#include "core/settings.h"
#include "core/util.h"
#include "core/viewer.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace mdv {
namespace {

enum ControlId : WORD {
    kIdTheme = 1001,
    kIdFont,
    kIdMono,
    kIdSize,
    kIdZoom,
    kIdWidth,
    kIdFullWidth,
    kIdWrap,
    kIdLineNumbers,
    kIdHighlight,
    kIdRemote,
    kIdEmoji,
    kIdMath,
    kIdAutoReload,
    kIdSmooth,
    kIdHardware,
    kIdMermaid,  // (the check boxes are kIdLineNumbers..kIdMermaid)
    kIdReset,
    kIdHeader = 1100,  // section headers: kIdHeader + n
};
constexpr WORD kIdLabel = 0xFFFF;  // IDC_STATIC
constexpr int kHeaders = 4;

// Window class atoms of the standard dialog controls.
constexpr WORD kButton = 0x0080;
constexpr WORD kStatic = 0x0082;
constexpr WORD kComboBox = 0x0085;

constexpr UINT_PTR kTimerApply = 1;
constexpr UINT kApplyDelayMs = 400;       // typing in a number field
constexpr UINT kMsgApply = WM_APP + 1;    // after CBN_SELCHANGE, once the edit shows the item
constexpr UINT kMsgFonts = WM_APP + 2;    // after a DPI change

// Dark scheme, as in Windows 11 dialogs.
constexpr COLORREF kDarkBack = RGB(32, 32, 32);
constexpr COLORREF kDarkField = RGB(51, 51, 51);  // DarkMode_CFD combo box face
constexpr COLORREF kDarkText = RGB(255, 255, 255);
constexpr COLORREF kDarkGray = RGB(135, 135, 135);

constexpr int kSizes[] = {12, 13, 14, 15, 16, 17, 18, 20, 22, 24};
constexpr int kZooms[] = {50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300};
constexpr int kWidths[] = {700, 800, 900, 1000, 1200, 1400, 1600};

// In-memory DLGTEMPLATEEX.
class DialogTemplate {
public:
    DialogTemplate(const wchar_t* title, short cx) {
        Word(1);
        Word(0xFFFF);  // DLGTEMPLATEEX
        Dword(0);      // help id
        Dword(0);      // extended style
        Dword(DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU);
        count_at_ = data_.size();
        Word(0);
        Word(0);
        Word(0);
        Word(static_cast<WORD>(cx));
        height_at_ = data_.size();
        Word(0);
        Word(0);  // no menu
        Word(0);  // default class
        String(title);
        Word(9);          // point size
        Word(FW_NORMAL);  // weight
        Word(DEFAULT_CHARSET << 8);  // italic = 0, charset
        String(L"Segoe UI");
    }

    void Add(WORD cls, const wchar_t* text, WORD id, DWORD style, short x, short y, short cx, short cy) {
        if (data_.size() % 2) Word(0);  // DWORD alignment
        Dword(0);
        Dword(0);
        Dword(style | WS_CHILD | WS_VISIBLE);
        Word(static_cast<WORD>(x));
        Word(static_cast<WORD>(y));
        Word(static_cast<WORD>(cx));
        Word(static_cast<WORD>(cy));
        Dword(id == kIdLabel ? 0xFFFFFFFFu : id);
        Word(0xFFFF);
        Word(cls);
        String(text);
        Word(0);  // no creation data
        ++data_[count_at_];
    }

    void SetHeight(short cy) { data_[height_at_] = static_cast<WORD>(cy); }
    const DLGTEMPLATE* Get() const { return reinterpret_cast<const DLGTEMPLATE*>(data_.data()); }

private:
    void Word(WORD w) { data_.push_back(w); }
    void Dword(DWORD d) {
        Word(LOWORD(d));
        Word(HIWORD(d));
    }
    void String(const wchar_t* s) {
        for (; *s; ++s) Word(static_cast<WORD>(*s));
        Word(0);
    }

    std::vector<WORD> data_;
    size_t count_at_ = 0, height_at_ = 0;
};

// Activation context with Common Controls 6 (manifest resource 3 in the DLL):
// themed controls whatever the host process uses.
HANDLE g_controls_context = INVALID_HANDLE_VALUE;  // once made (see ReleaseDialogContext)

HANDLE ControlsContext() {
    static const HANDLE ctx = [] {
        wchar_t module[MAX_PATH * 2] = {};
        GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), module, static_cast<DWORD>(std::size(module)));
        ACTCTXW ac{};
        ac.cbSize = sizeof ac;
        ac.dwFlags = ACTCTX_FLAG_HMODULE_VALID | ACTCTX_FLAG_RESOURCE_NAME_VALID;
        ac.lpSource = module;
        ac.hModule = reinterpret_cast<HMODULE>(&__ImageBase);
        ac.lpResourceName = MAKEINTRESOURCEW(3);
        g_controls_context = CreateActCtxW(&ac);
        return g_controls_context;
    }();
    return ctx;
}

bool HighContrast() {
    HIGHCONTRASTW hc{sizeof hc};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

bool LessNoCase(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; }

// Installed font families (DirectWrite names, as the layout uses them).
void EnumerateFonts(std::vector<std::wstring>& all, std::vector<std::wstring>& mono) {
    ComPtr<IDWriteFactory> dw;
    ComPtr<IDWriteFontCollection> fonts;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dw.GetAddressOf()))) ||
        FAILED(dw->GetSystemFontCollection(&fonts, FALSE))) {
        return;
    }
    const UINT32 count = fonts->GetFontFamilyCount();
    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IDWriteFontFamily> family;
        ComPtr<IDWriteLocalizedStrings> names;
        ComPtr<IDWriteFont> font;
        if (FAILED(fonts->GetFontFamily(i, &family)) || FAILED(family->GetFamilyNames(&names)) ||
            FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                DWRITE_FONT_STYLE_NORMAL, &font)) ||
            font->IsSymbolFont()) {
            continue;
        }
        UINT32 index = 0, length = 0;
        BOOL exists = FALSE;
        if (FAILED(names->FindLocaleName(L"en-us", &index, &exists)) || !exists) index = 0;
        if (FAILED(names->GetStringLength(index, &length)) || length == 0) continue;
        std::wstring name(length + 1, L'\0');
        if (FAILED(names->GetString(index, name.data(), length + 1))) continue;
        name.resize(length);
        ComPtr<IDWriteFont1> font1;
        if (SUCCEEDED(font.As(&font1)) && font1->IsMonospacedFont()) mono.push_back(name);
        all.push_back(std::move(name));
    }
    std::sort(all.begin(), all.end(), LessNoCase);
    std::sort(mono.begin(), mono.end(), LessNoCase);
}

class SettingsDialog {
public:
    explicit SettingsDialog(HWND viewer) : viewer_(viewer), original_(GlobalSettings()) {}
    ~SettingsDialog();
    void Run();
    void ChangedOutside(const std::function<void(Settings&)>& edit);
    HWND hwnd() const { return dlg_; }

private:
    static INT_PTR CALLBACK StaticProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp);
    INT_PTR Proc(UINT msg, WPARAM wp, LPARAM lp);
    static DialogTemplate BuildTemplate();
    void OnInit();
    void OnCommand(WORD id, WORD code);
    void Fill(const Settings& s);
    Settings Read() const;
    void Apply();
    void Restore();
    void UpdateLook();
    void SetDark(bool dark);
    void UpdateFonts();
    void Center();
    LRESULT DrawCheckbox(const NMCUSTOMDRAW& cd) const;
    void SelectFont(WORD id, const std::wstring& name, int first);
    void SetNumber(WORD id, int value);
    void ClearSelections() const;
    int ReadNumber(WORD id, int fallback, int lo, int hi) const;
    LRESULT Send(WORD id, UINT msg, WPARAM wp = 0, LPARAM lp = 0) const {
        return SendDlgItemMessageW(dlg_, id, msg, wp, lp);
    }

    HWND viewer_;
    HWND dlg_ = nullptr;
    Settings original_;
    int width_ = Settings{}.max_width;  // shown while "full width" is on
    bool filling_ = false;
    bool dark_ = false;
    bool themed_ = false;               // SetDark() ran once
    HFONT bold_ = nullptr;
    HBRUSH back_ = nullptr;
    HBRUSH field_ = nullptr;
    std::vector<std::wstring> fonts_, mono_;
};

SettingsDialog* g_dialog = nullptr;

SettingsDialog::~SettingsDialog() {
    if (bold_) DeleteObject(bold_);
    if (back_) DeleteObject(back_);
    if (field_) DeleteObject(field_);
}

DialogTemplate SettingsDialog::BuildTemplate() {
    // Dialog units. Left column: appearance (labels, then their controls) and
    // behavior; right column: code blocks and content.
    constexpr short M = 7, Row = 16, Check = 13, Gap = 6;
    constexpr short LX = 14, LW = 80, CX = LX + LW + 4, CW = 136, NW = 44;  // left column ends at 234
    constexpr short RH = 248, RX = RH + 7, RW = 120, W = RX + RW + M;
    DialogTemplate t(Tr(L"mdview Settings", L"Настройки mdview"), W);
    int header = 0;
    auto section = [&](short x, short& y, const wchar_t* text) {
        t.Add(kStatic, text, static_cast<WORD>(kIdHeader + header++), SS_LEFT | SS_NOPREFIX, x, y, W - M - x, 10);
        y += 14;
    };
    auto label = [&](short y, const wchar_t* text) { t.Add(kStatic, text, kIdLabel, SS_LEFT, LX, y + 2, LW, 9); };
    auto list = [&](short x, short& y, short w, WORD id) {
        t.Add(kComboBox, L"", id, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, y, w, 160);
        y += Row;
    };
    auto number = [&](short y, WORD id) {
        t.Add(kComboBox, L"", id, CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, CX, y, NW, 140);
    };
    auto check = [&](short x, short& y, short w, WORD id, const wchar_t* text) {
        t.Add(kButton, text, id, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, w, 10);
        y += Check;
    };

    short y = M;
    section(M, y, Tr(L"Appearance", L"Внешний вид"));
    label(y, Tr(L"&Theme:", L"&Тема:"));
    list(CX, y, CW, kIdTheme);
    label(y, Tr(L"Text &font:", L"&Шрифт текста:"));
    list(CX, y, CW, kIdFont);
    label(y, Tr(L"&Monospace font:", L"&Моноширинный шрифт:"));
    list(CX, y, CW, kIdMono);
    label(y, Tr(L"Font si&ze:", L"&Размер шрифта:"));
    number(y, kIdSize);
    y += Row;
    label(y, Tr(L"Z&oom, %:", L"Масшта&б, %:"));
    number(y, kIdZoom);
    y += Row;
    label(y, Tr(L"Text &width:", L"Ширина т&екста:"));
    number(y, kIdWidth);
    constexpr short FX = CX + NW + 6;
    t.Add(kButton, Tr(L"Full win&dow width", L"На &всю ширину окна"), kIdFullWidth, BS_AUTOCHECKBOX | WS_TABSTOP, FX,
          y + 2, CX + CW - FX, 10);
    y += Row + Gap;
    section(M, y, Tr(L"Behavior", L"Поведение"));
    check(LX, y, CX + CW - LX, kIdAutoReload, Tr(L"Reload when the file &changes", L"&Обновлять при изменении файла"));
    check(LX, y, CX + CW - LX, kIdSmooth, Tr(L"Smooth scrollin&g", L"П&лавная прокрутка"));
    check(LX, y, CX + CW - LX, kIdHardware, Tr(L"Hardware &acceleration (GPU)", L"&Аппаратное ускорение (GPU)"));
    const short left_end = y;

    y = M;
    section(RH, y, Tr(L"Code blocks", L"Блоки кода"));
    t.Add(kStatic, Tr(L"Wrap &long lines:", L"&Перенос длинных строк:"), kIdLabel, SS_LEFT, RX, y, RW, 9);
    y += 11;
    list(RX, y, RW, kIdWrap);
    check(RX, y, RW, kIdLineNumbers, Tr(L"Line &numbers", L"&Номера строк"));
    check(RX, y, RW, kIdHighlight, Tr(L"Syntax &highlighting", L"По&дсветка синтаксиса"));
    y += Gap;
    section(RH, y, Tr(L"Content", L"Содержимое"));
    check(RX, y, RW, kIdRemote, Tr(L"&Images from the internet", L"&Изображения из интернета"));
    check(RX, y, RW, kIdEmoji, Tr(L"&Emoji codes like :smile:", L"&Эмодзи-коды вида :smile:"));
    check(RX, y, RW, kIdMath, Tr(L"Math formula&s: $…$", L"&Формулы: $…$"));
    check(RX, y, RW, kIdMermaid, Tr(L"Mermaid diagrams as &pictures", L"Диа&граммы Mermaid"));

    y = static_cast<short>(std::max(y, left_end) + 10);
    t.Add(kButton, Tr(L"&Reset", L"&Сбросить"), kIdReset, BS_PUSHBUTTON | WS_TABSTOP, M, y, 56, 14);
    t.Add(kButton, L"OK", IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP, W - M - 50 - 4 - 50, y, 50, 14);
    t.Add(kButton, Tr(L"Cancel", L"Отмена"), IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP, W - M - 50, y, 50, 14);
    t.SetHeight(static_cast<short>(y + 14 + M));
    return t;
}

void SettingsDialog::Run() {
    const DialogTemplate t = BuildTemplate();
    const HANDLE ctx = ControlsContext();
    ULONG_PTR cookie = 0;
    const bool activated = ctx != INVALID_HANDLE_VALUE && ActivateActCtx(ctx, &cookie);
    DialogBoxIndirectParamW(reinterpret_cast<HINSTANCE>(&__ImageBase), t.Get(), GetAncestor(viewer_, GA_ROOT),
                            &SettingsDialog::StaticProc, reinterpret_cast<LPARAM>(this));
    if (activated) DeactivateActCtx(0, cookie);
}

INT_PTR CALLBACK SettingsDialog::StaticProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INITDIALOG) SetWindowLongPtrW(dlg, DWLP_USER, lp);
    auto* self = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(dlg, DWLP_USER));
    if (!self) return FALSE;
    if (msg == WM_INITDIALOG) self->dlg_ = dlg;
    try {
        return self->Proc(msg, wp, lp);
    } catch (...) {
        return FALSE;  // never unwind through the dialog manager
    }
}

INT_PTR SettingsDialog::Proc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        OnInit();
        return TRUE;
    case WM_COMMAND:
        OnCommand(LOWORD(wp), HIWORD(wp));
        return TRUE;
    case WM_TIMER:
        if (wp == kTimerApply) {
            KillTimer(dlg_, kTimerApply);
            Apply();
        }
        return TRUE;
    case kMsgApply:
        Apply();
        return TRUE;
    case kMsgFonts:
        UpdateFonts();
        return TRUE;
    case WM_DPICHANGED:
        // The dialog manager rescales the controls and their font; the bold
        // headers are ours.
        PostMessageW(dlg_, kMsgFonts, 0, 0);
        return FALSE;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        UpdateLook();
        return FALSE;
    case WM_NOTIFY: {
        const auto* nm = reinterpret_cast<const NMHDR*>(lp);
        if (dark_ && nm->code == NM_CUSTOMDRAW &&
            (nm->idFrom == kIdFullWidth || (nm->idFrom >= kIdLineNumbers && nm->idFrom <= kIdMermaid))) {
            SetWindowLongPtrW(dlg_, DWLP_MSGRESULT, DrawCheckbox(*reinterpret_cast<const NMCUSTOMDRAW*>(lp)));
            return TRUE;
        }
        return FALSE;
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        if (!dark_) return FALSE;
        const HDC dc = reinterpret_cast<HDC>(wp);
        const bool field = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
        SetTextColor(dc, IsWindowEnabled(reinterpret_cast<HWND>(lp)) ? kDarkText : kDarkGray);
        SetBkColor(dc, field ? kDarkField : kDarkBack);
        return reinterpret_cast<INT_PTR>(field ? field_ : back_);
    }
    default:
        return FALSE;
    }
}

void SettingsDialog::OnInit() {
    back_ = CreateSolidBrush(kDarkBack);
    field_ = CreateSolidBrush(kDarkField);
    UpdateFonts();

    Send(kIdTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"As in Total Commander", L"Как в Total Commander")));
    Send(kIdTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"Light", L"Светлая")));
    Send(kIdTheme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"Dark", L"Тёмная")));
    Send(kIdWrap, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"As in Lister", L"Как в Lister")));
    Send(kIdWrap, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"Don't wrap", L"Не переносить")));
    Send(kIdWrap, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Tr(L"Wrap", L"Переносить")));

    EnumerateFonts(fonts_, mono_);
    for (const auto& f : fonts_) Send(kIdFont, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(f.c_str()));
    // Empty setting: the layout picks Cascadia Mono if installed, else Consolas.
    const bool cascadia = std::any_of(mono_.begin(), mono_.end(),
                                      [](const std::wstring& f) { return EqualsNoCase(f, L"Cascadia Mono"); });
    const std::wstring automatic = std::wstring(Tr(L"Automatic (", L"Автоматически (")) +
                                   (cascadia ? L"Cascadia Mono" : L"Consolas") + L")";
    Send(kIdMono, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(automatic.c_str()));
    for (const auto& f : mono_) Send(kIdMono, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(f.c_str()));

    auto presets = [this](WORD id, const auto& values) {
        for (int v : values) Send(id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(std::to_wstring(v).c_str()));
        Send(id, CB_LIMITTEXT, 5);
    };
    presets(kIdSize, kSizes);
    presets(kIdZoom, kZooms);
    presets(kIdWidth, kWidths);

    Fill(GlobalSettings());
    UpdateLook();
    Center();
}

void SettingsDialog::OnCommand(WORD id, WORD code) {
    if (id == IDOK) {
        KillTimer(dlg_, kTimerApply);
        Apply();
        SaveSettings();
        EndDialog(dlg_, IDOK);
        return;
    }
    if (id == IDCANCEL) {
        KillTimer(dlg_, kTimerApply);
        Restore();
        EndDialog(dlg_, IDCANCEL);
        return;
    }
    if (filling_) return;
    const bool number = id == kIdSize || id == kIdZoom || id == kIdWidth;
    if (id == kIdReset && code == BN_CLICKED) {
        Fill(Settings{});
        Apply();
    } else if (id == kIdFullWidth && code == BN_CLICKED) {
        EnableWindow(GetDlgItem(dlg_, kIdWidth), IsDlgButtonChecked(dlg_, kIdFullWidth) != BST_CHECKED);
        Apply();
    } else if (id >= kIdLineNumbers && id <= kIdMermaid && code == BN_CLICKED) {
        Apply();
    } else if (code == CBN_SELCHANGE) {
        PostMessageW(dlg_, kMsgApply, 0, 0);
    } else if (number && code == CBN_EDITCHANGE) {
        SetTimer(dlg_, kTimerApply, kApplyDelayMs, nullptr);
    } else if (number && code == CBN_KILLFOCUS) {
        // Show what was taken from the typed text (clamped, or the old value).
        KillTimer(dlg_, kTimerApply);
        Apply();
        const Settings& s = GlobalSettings();
        filling_ = true;
        SetNumber(id, id == kIdSize ? static_cast<int>(s.font_size) : id == kIdZoom ? s.zoom_percent : width_);
        filling_ = false;
    }
}

void SettingsDialog::SelectFont(WORD id, const std::wstring& name, int first) {
    LRESULT index = Send(id, CB_FINDSTRINGEXACT, static_cast<WPARAM>(first - 1), reinterpret_cast<LPARAM>(name.c_str()));
    if (index == CB_ERR || index < first) {
        // Not installed (or typed into mdview.ini by hand): keep it selectable.
        index = Send(id, CB_INSERTSTRING, static_cast<WPARAM>(first), reinterpret_cast<LPARAM>(name.c_str()));
    }
    Send(id, CB_SETCURSEL, static_cast<WPARAM>(index));
}

void SettingsDialog::Fill(const Settings& s) {
    filling_ = true;
    Send(kIdTheme, CB_SETCURSEL, static_cast<WPARAM>(std::clamp(static_cast<int>(s.theme), 0, 2)));
    SelectFont(kIdFont, s.font_family, 0);
    if (s.mono_family.empty()) {
        Send(kIdMono, CB_SETCURSEL, 0);
    } else {
        SelectFont(kIdMono, s.mono_family, 1);
    }
    if (s.max_width > 0) width_ = s.max_width;
    SetNumber(kIdSize, static_cast<int>(s.font_size));
    SetNumber(kIdZoom, s.zoom_percent);
    SetNumber(kIdWidth, width_);
    CheckDlgButton(dlg_, kIdFullWidth, s.max_width == 0 ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(dlg_, kIdWidth), s.max_width != 0);
    Send(kIdWrap, CB_SETCURSEL, static_cast<WPARAM>(std::clamp(s.wrap_code, -1, 1) + 1));
    const std::pair<WORD, bool> checks[] = {
        {kIdLineNumbers, s.line_numbers}, {kIdHighlight, s.syntax_highlight}, {kIdRemote, s.remote_images},
        {kIdEmoji, s.emoji_shortcodes},   {kIdMath, s.math},                  {kIdAutoReload, s.auto_reload},
        {kIdSmooth, s.smooth_scroll},     {kIdHardware, s.hardware},          {kIdMermaid, s.mermaid},
    };
    for (const auto& [id, on] : checks) CheckDlgButton(dlg_, id, on ? BST_CHECKED : BST_UNCHECKED);
    filling_ = false;
}

void SettingsDialog::SetNumber(WORD id, int value) {
    SetDlgItemInt(dlg_, id, static_cast<UINT>(value), FALSE);
    ClearSelections();
}

// A combo box selects its text when set, resized or re-themed, and the
// unfocused field shows that selection.
void SettingsDialog::ClearSelections() const {
    const HWND focus = GetFocus();
    for (WORD id : {kIdSize, kIdZoom, kIdWidth}) {
        const HWND combo = GetDlgItem(dlg_, id);
        if (focus != combo && !IsChild(combo, focus)) Send(id, CB_SETEDITSEL, 0, MAKELPARAM(-1, 0));
    }
}

int SettingsDialog::ReadNumber(WORD id, int fallback, int lo, int hi) const {
    wchar_t text[32] = {};
    GetDlgItemTextW(dlg_, id, text, static_cast<int>(std::size(text)));
    wchar_t* end = nullptr;
    const long v = std::wcstol(text, &end, 10);
    if (end == text) return fallback;
    return static_cast<int>(std::clamp<long>(v, lo, hi));
}

Settings SettingsDialog::Read() const {
    Settings s = GlobalSettings();
    auto item = [this](WORD id, LRESULT index) {
        std::wstring text(static_cast<size_t>(std::max<LRESULT>(Send(id, CB_GETLBTEXTLEN, index), 0)) + 1, L'\0');
        text.resize(static_cast<size_t>(std::max<LRESULT>(
            Send(id, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(text.data())), 0)));
        return text;
    };
    const LRESULT theme = Send(kIdTheme, CB_GETCURSEL);
    if (theme >= 0 && theme <= 2) s.theme = static_cast<ThemeMode>(theme);
    const LRESULT font = Send(kIdFont, CB_GETCURSEL);
    if (font >= 0) s.font_family = item(kIdFont, font);
    if (s.font_family.empty()) s.font_family = Settings{}.font_family;
    const LRESULT mono = Send(kIdMono, CB_GETCURSEL);
    if (mono >= 0) s.mono_family = mono == 0 ? std::wstring() : item(kIdMono, mono);
    s.font_size = static_cast<float>(ReadNumber(kIdSize, static_cast<int>(s.font_size), 8, 40));
    s.zoom_percent = ReadNumber(kIdZoom, s.zoom_percent, 30, 400);
    s.max_width = IsDlgButtonChecked(dlg_, kIdFullWidth) == BST_CHECKED ? 0 : ReadNumber(kIdWidth, width_, 200, 10000);
    const LRESULT wrap = Send(kIdWrap, CB_GETCURSEL);
    if (wrap >= 0 && wrap <= 2) s.wrap_code = static_cast<int>(wrap) - 1;
    auto on = [this](WORD id) { return IsDlgButtonChecked(dlg_, id) == BST_CHECKED; };
    s.line_numbers = on(kIdLineNumbers);
    s.syntax_highlight = on(kIdHighlight);
    s.remote_images = on(kIdRemote);
    s.emoji_shortcodes = on(kIdEmoji);
    s.math = on(kIdMath);
    s.mermaid = on(kIdMermaid);
    s.auto_reload = on(kIdAutoReload);
    s.smooth_scroll = on(kIdSmooth);
    s.hardware = on(kIdHardware);
    return s;
}

// Live preview: every open viewer follows the dialog.
void SettingsDialog::Apply() {
    const Settings s = Read();
    if (s.max_width > 0) width_ = s.max_width;
    if (s == GlobalSettings()) return;
    const Settings old = GlobalSettings();
    GlobalSettings() = s;
    ApplySettingsToViewers(old);
    UpdateLook();
}

void SettingsDialog::Restore() {
    if (GlobalSettings() == original_) return;
    const Settings old = GlobalSettings();
    GlobalSettings() = original_;
    ApplySettingsToViewers(old);
}

// A setting changed from a viewer's menu (in GlobalSettings() already):
// Cancel keeps it, and only it is saved, not the dialog's own changes.
void SettingsDialog::ChangedOutside(const std::function<void(Settings&)>& edit) {
    KillTimer(dlg_, kTimerApply);
    edit(original_);
    SaveSettings(original_);
    Fill(GlobalSettings());
    UpdateLook();
}

// The dialog is dark when its viewer is.
void SettingsDialog::UpdateLook() {
    bool dark;
    if (IsWindow(viewer_)) {
        dark = ViewerIsDark(viewer_);
    } else {
        dark = GlobalSettings().theme == ThemeMode::Dark ||
               (GlobalSettings().theme == ThemeMode::Auto && SystemUsesDarkTheme());
    }
    SetDark(dark && !HighContrast());
}

void SettingsDialog::SetDark(bool dark) {
    if (themed_ && dark == dark_) return;
    themed_ = true;
    dark_ = dark;
    // DWMWA_USE_IMMERSIVE_DARK_MODE: 20 since Windows 10 20H1, 19 before.
    const BOOL value = dark;
    if (SystemDllLoads(L"dwmapi.dll") && FAILED(DwmSetWindowAttribute(dlg_, 20, &value, sizeof value))) {
        DwmSetWindowAttribute(dlg_, 19, &value, sizeof value);
    }
    for (HWND child = GetWindow(dlg_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        wchar_t cls[32] = {};
        GetClassNameW(child, cls, static_cast<int>(std::size(cls)));
        if (EqualsNoCase(cls, L"Button")) {
            SetWindowTheme(child, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
        } else if (EqualsNoCase(cls, L"ComboBox")) {
            SetWindowTheme(child, dark ? L"DarkMode_CFD" : nullptr, nullptr);
            COMBOBOXINFO info{sizeof info};
            if (GetComboBoxInfo(child, &info)) {
                if (info.hwndItem && info.hwndItem != child) SetWindowTheme(info.hwndItem, dark ? L"DarkMode_CFD" : nullptr, nullptr);
                if (info.hwndList) SetWindowTheme(info.hwndList, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
            }
        }
    }
    ClearSelections();
    RedrawWindow(dlg_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

// Section headers: semibold version of the dialog font (at its current DPI).
void SettingsDialog::UpdateFonts() {
    const auto base = reinterpret_cast<HFONT>(SendMessageW(dlg_, WM_GETFONT, 0, 0));
    LOGFONTW lf{};
    if (!base || !GetObjectW(base, sizeof lf, &lf)) return;
    lf.lfWeight = FW_SEMIBOLD;
    const HFONT bold = CreateFontIndirectW(&lf);
    if (!bold) return;
    for (int i = 0; i < kHeaders; ++i) Send(static_cast<WORD>(kIdHeader + i), WM_SETFONT, reinterpret_cast<WPARAM>(bold), TRUE);
    if (bold_) DeleteObject(bold_);
    bold_ = bold;
}

// Over the viewer, inside the monitor's work area.
void SettingsDialog::Center() {
    RECT anchor{}, rc{};
    if (!GetWindowRect(IsWindow(viewer_) ? viewer_ : GetParent(dlg_), &anchor) || !GetWindowRect(dlg_, &rc)) return;
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int x = (anchor.left + anchor.right - w) / 2;
    int y = (anchor.top + anchor.bottom - h) / 2;
    MONITORINFO mi{sizeof mi};
    if (GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi)) {
        x = std::clamp(x, static_cast<int>(mi.rcWork.left), std::max<int>(mi.rcWork.left, mi.rcWork.right - w));
        y = std::clamp(y, static_cast<int>(mi.rcWork.top), std::max<int>(mi.rcWork.top, mi.rcWork.bottom - h));
    }
    SetWindowPos(dlg_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Checkboxes in the dark scheme: the themed control draws its label black,
// so draw it ourselves (dark glyph from the DarkMode_Explorer theme).
LRESULT SettingsDialog::DrawCheckbox(const NMCUSTOMDRAW& cd) const {
    if (cd.dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    const HWND button = cd.hdr.hwndFrom;
    const HDC dc = cd.hdc;
    RECT rc = cd.rc;
    FillRect(dc, &rc, back_);

    const bool checked = SendMessageW(button, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool disabled = (cd.uItemState & CDIS_DISABLED) || !IsWindowEnabled(button);
    int state = checked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL;
    if (disabled) state += 3;
    else if (cd.uItemState & CDIS_SELECTED) state += 2;
    else if (cd.uItemState & CDIS_HOT) state += 1;

    const HTHEME theme = OpenThemeData(button, L"Button");
    SIZE box{13, 13};
    if (theme) GetThemePartSize(theme, dc, BP_CHECKBOX, state, nullptr, TS_DRAW, &box);
    RECT glyph{rc.left, rc.top + (rc.bottom - rc.top - box.cy) / 2, 0, 0};
    glyph.right = glyph.left + box.cx;
    glyph.bottom = glyph.top + box.cy;
    if (theme) {
        DrawThemeBackground(theme, dc, BP_CHECKBOX, state, &glyph, nullptr);
        CloseThemeData(theme);
    } else {
        DrawFrameControl(dc, &glyph, DFC_BUTTON, DFCS_BUTTONCHECK | (checked ? DFCS_CHECKED : 0));
    }

    wchar_t text[128] = {};
    GetWindowTextW(button, text, static_cast<int>(std::size(text)));
    const HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabled ? kDarkGray : kDarkText);
    const LRESULT ui = SendMessageW(button, WM_QUERYUISTATE, 0, 0);
    const UINT format = DT_SINGLELINE | DT_VCENTER | DT_LEFT | ((ui & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0);
    RECT tr = rc;
    tr.left = glyph.right + box.cx / 3 + 1;
    DrawTextW(dc, text, -1, &tr, format);
    if ((cd.uItemState & CDIS_FOCUS) && !(ui & UISF_HIDEFOCUS)) {
        RECT fr = tr;
        DrawTextW(dc, text, -1, &fr, format | DT_CALCRECT);
        const LONG h = fr.bottom - fr.top;
        fr.top = tr.top + (tr.bottom - tr.top - h) / 2;
        fr.bottom = fr.top + h;
        InflateRect(&fr, 1, 1);
        SetTextColor(dc, kDarkText);
        DrawFocusRect(dc, &fr);
    }
    SelectObject(dc, old);
    return CDRF_SKIPDEFAULT;
}

}  // namespace

void ShowSettingsDialog(HWND viewer) {
    if (g_dialog) {
        SetForegroundWindow(g_dialog->hwnd());
        return;
    }
    SettingsDialog dialog(viewer);
    g_dialog = &dialog;
    struct Reset {
        ~Reset() { g_dialog = nullptr; }
    } reset;
    dialog.Run();
}

void SaveSettingChange(const std::function<void(Settings&)>& edit) {
    if (g_dialog && g_dialog->hwnd()) {
        g_dialog->ChangedOutside(edit);
    } else {
        SaveSettings();
    }
}

void ReleaseDialogContext() {
    if (g_controls_context != INVALID_HANDLE_VALUE) ReleaseActCtx(g_controls_context);
    g_controls_context = INVALID_HANDLE_VALUE;
}

}  // namespace mdv
