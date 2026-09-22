#include "core/theme.h"

#include "core/highlight.h"

namespace mdv {
namespace {

constexpr uint32_t Rgb(uint32_t rgb) { return 0xFF000000u | rgb; }
constexpr uint32_t Rgba(uint32_t rgb, uint32_t alpha) { return (alpha << 24) | rgb; }

void SetSyntax(Theme& t, TokenKind k, uint32_t argb) {
    t.argb[kColorSyntaxBase + static_cast<int>(k)] = argb;
}

Theme MakeLight() {
    Theme t;
    t.dark = false;
    t.argb[kColorText] = Rgb(0x1F2328);
    t.argb[kColorMuted] = Rgb(0x59636E);
    t.argb[kColorLink] = Rgb(0x0969DA);
    t.argb[kColorBackground] = Rgb(0xFFFFFF);
    t.argb[kColorCanvasSubtle] = Rgb(0xF6F8FA);
    t.argb[kColorInlineCodeBg] = Rgba(0x818B98, 0x1F);
    t.argb[kColorBorder] = Rgb(0xD1D9E0);
    t.argb[kColorBorderMuted] = Rgba(0xD1D9E0, 0xB3);
    t.argb[kColorRule] = Rgb(0xD1D9E0);
    t.argb[kColorMarkBg] = Rgb(0xFFF8C5);
    t.argb[kColorMarkText] = Rgb(0x1F2328);
    t.argb[kColorKbdBg] = Rgb(0xF6F8FA);
    t.argb[kColorKbdBorder] = Rgba(0xB7C0C9, 0xFF);
    t.argb[kColorSelection] = Rgba(0x0969DA, 0x40);
    t.argb[kColorSearch] = Rgba(0xFFD33D, 0x80);
    t.argb[kColorSearchCurrent] = Rgba(0xFF8C1A, 0xA0);
    t.argb[kColorCheckboxBg] = Rgb(0xFFFFFF);
    t.argb[kColorCheckboxChecked] = Rgb(0x0969DA);
    t.argb[kColorCheckmark] = Rgb(0xFFFFFF);
    t.argb[kColorScrollThumb] = Rgba(0x59636E, 0x60);
    t.argb[kColorScrollThumbHover] = Rgba(0x59636E, 0xA8);
    t.argb[kColorBubbleBg] = Rgba(0xF6F8FA, 0xF4);
    t.argb[kColorBubbleText] = Rgb(0x1F2328);
    t.argb[kColorButtonBg] = Rgba(0xF6F8FA, 0xF0);
    t.argb[kColorButtonHover] = Rgb(0xE6EAEF);
    t.argb[kColorAlertNote] = Rgb(0x0969DA);
    t.argb[kColorAlertTip] = Rgb(0x1A7F37);
    t.argb[kColorAlertImportant] = Rgb(0x8250DF);
    t.argb[kColorAlertWarning] = Rgb(0x9A6700);
    t.argb[kColorAlertCaution] = Rgb(0xD1242F);
    t.argb[kColorDiffInsertedBg] = Rgb(0xDAFBE1);
    t.argb[kColorDiffDeletedBg] = Rgb(0xFFEBE9);
    t.argb[kColorImagePlaceholder] = Rgba(0x818B98, 0x1A);
    // GitHub "prettylights" syntax colors.
    for (int i = 0; i < static_cast<int>(TokenKind::Count); ++i) t.argb[kColorSyntaxBase + i] = Rgb(0x1F2328);
    SetSyntax(t, TokenKind::Comment, Rgb(0x59636E));
    SetSyntax(t, TokenKind::Keyword, Rgb(0xCF222E));
    SetSyntax(t, TokenKind::String, Rgb(0x0A3069));
    SetSyntax(t, TokenKind::Number, Rgb(0x0550AE));
    SetSyntax(t, TokenKind::Constant, Rgb(0x0550AE));
    SetSyntax(t, TokenKind::Function, Rgb(0x6639BA));
    SetSyntax(t, TokenKind::Type, Rgb(0x953800));
    SetSyntax(t, TokenKind::Variable, Rgb(0x953800));
    SetSyntax(t, TokenKind::Tag, Rgb(0x116329));
    SetSyntax(t, TokenKind::Attribute, Rgb(0x0550AE));
    SetSyntax(t, TokenKind::Property, Rgb(0x0550AE));
    SetSyntax(t, TokenKind::Meta, Rgb(0x8250DF));
    SetSyntax(t, TokenKind::Regex, Rgb(0x116329));
    SetSyntax(t, TokenKind::Escape, Rgb(0x116329));
    SetSyntax(t, TokenKind::Inserted, Rgb(0x116329));
    SetSyntax(t, TokenKind::Deleted, Rgb(0x82071E));
    SetSyntax(t, TokenKind::DiffRange, Rgb(0x8250DF));
    SetSyntax(t, TokenKind::Heading, Rgb(0x0550AE));
    return t;
}

Theme MakeDark() {
    Theme t;
    t.dark = true;
    t.argb[kColorText] = Rgb(0xF0F6FC);
    t.argb[kColorMuted] = Rgb(0x9198A1);
    t.argb[kColorLink] = Rgb(0x4493F8);
    t.argb[kColorBackground] = Rgb(0x0D1117);
    t.argb[kColorCanvasSubtle] = Rgb(0x151B23);
    t.argb[kColorInlineCodeBg] = Rgba(0x656C76, 0x33);
    t.argb[kColorBorder] = Rgb(0x3D444D);
    t.argb[kColorBorderMuted] = Rgba(0x3D444D, 0xB3);
    t.argb[kColorRule] = Rgb(0x3D444D);
    t.argb[kColorMarkBg] = Rgba(0xBB8009, 0x40);
    t.argb[kColorMarkText] = Rgb(0xF0F6FC);
    t.argb[kColorKbdBg] = Rgb(0x151B23);
    t.argb[kColorKbdBorder] = Rgb(0x3D444D);
    t.argb[kColorSelection] = Rgba(0x4493F8, 0x55);
    t.argb[kColorSearch] = Rgba(0xBB8009, 0x80);
    t.argb[kColorSearchCurrent] = Rgba(0xF0883E, 0xB0);
    t.argb[kColorCheckboxBg] = Rgb(0x0D1117);
    t.argb[kColorCheckboxChecked] = Rgb(0x1F6FEB);
    t.argb[kColorCheckmark] = Rgb(0xFFFFFF);
    t.argb[kColorScrollThumb] = Rgba(0x9198A1, 0x50);
    t.argb[kColorScrollThumbHover] = Rgba(0x9198A1, 0x98);
    t.argb[kColorBubbleBg] = Rgba(0x151B23, 0xF4);
    t.argb[kColorBubbleText] = Rgb(0xF0F6FC);
    t.argb[kColorButtonBg] = Rgba(0x212830, 0xF0);
    t.argb[kColorButtonHover] = Rgb(0x2A313C);
    t.argb[kColorAlertNote] = Rgb(0x4493F8);
    t.argb[kColorAlertTip] = Rgb(0x3FB950);
    t.argb[kColorAlertImportant] = Rgb(0xAB7DF8);
    t.argb[kColorAlertWarning] = Rgb(0xD29922);
    t.argb[kColorAlertCaution] = Rgb(0xF85149);
    t.argb[kColorDiffInsertedBg] = Rgb(0x033A16);
    t.argb[kColorDiffDeletedBg] = Rgb(0x67060C);
    t.argb[kColorImagePlaceholder] = Rgba(0x656C76, 0x33);
    for (int i = 0; i < static_cast<int>(TokenKind::Count); ++i) t.argb[kColorSyntaxBase + i] = Rgb(0xF0F6FC);
    SetSyntax(t, TokenKind::Comment, Rgb(0x9198A1));
    SetSyntax(t, TokenKind::Keyword, Rgb(0xFF7B72));
    SetSyntax(t, TokenKind::String, Rgb(0xA5D6FF));
    SetSyntax(t, TokenKind::Number, Rgb(0x79C0FF));
    SetSyntax(t, TokenKind::Constant, Rgb(0x79C0FF));
    SetSyntax(t, TokenKind::Function, Rgb(0xD2A8FF));
    SetSyntax(t, TokenKind::Type, Rgb(0xFFA657));
    SetSyntax(t, TokenKind::Variable, Rgb(0xFFA657));
    SetSyntax(t, TokenKind::Tag, Rgb(0x7EE787));
    SetSyntax(t, TokenKind::Attribute, Rgb(0x79C0FF));
    SetSyntax(t, TokenKind::Property, Rgb(0x79C0FF));
    SetSyntax(t, TokenKind::Meta, Rgb(0xD2A8FF));
    SetSyntax(t, TokenKind::Regex, Rgb(0x7EE787));
    SetSyntax(t, TokenKind::Escape, Rgb(0x7EE787));
    SetSyntax(t, TokenKind::Inserted, Rgb(0xAFF5B4));
    SetSyntax(t, TokenKind::Deleted, Rgb(0xFFDCD7));
    SetSyntax(t, TokenKind::DiffRange, Rgb(0xD2A8FF));
    SetSyntax(t, TokenKind::Heading, Rgb(0x1F6FEB));
    return t;
}

}  // namespace

static_assert(static_cast<int>(TokenKind::Count) <= 20, "kColorCount reserves 20 syntax colors");

const Theme& GetTheme(bool dark) {
    static const Theme light = MakeLight();
    static const Theme dark_theme = MakeDark();
    return dark ? dark_theme : light;
}

bool Palette::Create(ID2D1RenderTarget* rt, const Theme& theme) {
    theme_ = &theme;
    for (int i = 0; i < kColorCount; ++i) {
        brushes_[i].Reset();
        if (FAILED(rt->CreateSolidColorBrush(theme.Color(static_cast<uint8_t>(i)), &brushes_[i]))) {
            return false;
        }
    }
    return true;
}

void Palette::Apply(const Theme& theme) {
    theme_ = &theme;
    for (int i = 0; i < kColorCount; ++i) {
        if (brushes_[i]) brushes_[i]->SetColor(theme.Color(static_cast<uint8_t>(i)));
    }
}

}  // namespace mdv
