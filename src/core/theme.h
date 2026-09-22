// Colors (GitHub Primer light/dark) and brush palette.
#pragma once

#include <d2d1.h>

#include <cstdint>

#include "core/document.h"
#include "core/util.h"

namespace mdv {

// Brush roles used as DirectWrite drawing effects and for decorations.
enum ColorRole : uint8_t {
    kColorText,
    kColorMuted,
    kColorLink,
    kColorBackground,
    kColorCanvasSubtle,      // code blocks, even table rows
    kColorInlineCodeBg,
    kColorBorder,
    kColorBorderMuted,
    kColorRule,
    kColorMarkBg,
    kColorMarkText,
    kColorKbdBg,
    kColorKbdBorder,
    kColorSelection,
    kColorSearch,
    kColorSearchCurrent,
    kColorCheckboxBg,
    kColorCheckboxChecked,
    kColorCheckmark,
    kColorScrollThumb,
    kColorScrollThumbHover,
    kColorBubbleBg,
    kColorBubbleText,
    kColorButtonBg,
    kColorButtonHover,
    kColorAlertNote,
    kColorAlertTip,
    kColorAlertImportant,
    kColorAlertWarning,
    kColorAlertCaution,
    kColorDiffInsertedBg,
    kColorDiffDeletedBg,
    kColorImagePlaceholder,
    kColorSyntaxBase,        // + TokenKind
    kColorCount = kColorSyntaxBase + 20,
};

struct Theme {
    bool dark = false;
    uint32_t argb[kColorCount] = {};

    D2D1_COLOR_F Color(uint8_t role) const {
        const uint32_t c = argb[role];
        return D2D1::ColorF(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f,
                            ((c >> 24) & 0xFF) / 255.f);
    }
    COLORREF Rgb(uint8_t role) const {
        const uint32_t c = argb[role];
        return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
    }
};

const Theme& GetTheme(bool dark);

inline uint8_t AlertColor(AlertKind k) {
    switch (k) {
    case AlertKind::Tip: return kColorAlertTip;
    case AlertKind::Important: return kColorAlertImportant;
    case AlertKind::Warning: return kColorAlertWarning;
    case AlertKind::Caution: return kColorAlertCaution;
    default: return kColorAlertNote;
    }
}

// Solid color brushes for every role, created for one render target. The
// brush objects keep their identity when the theme changes (only their color
// is updated), so text layouts referencing them stay valid.
class Palette {
public:
    bool Create(ID2D1RenderTarget* rt, const Theme& theme);
    void Apply(const Theme& theme);
    ID2D1SolidColorBrush* Brush(uint8_t role) const { return brushes_[role].Get(); }
    const Theme& theme() const { return *theme_; }

private:
    ComPtr<ID2D1SolidColorBrush> brushes_[kColorCount];
    const Theme* theme_ = nullptr;
};

}  // namespace mdv
