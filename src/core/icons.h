// Vector icons (GitHub Octicons, MIT license) drawn with Direct2D.
#pragma once

#include <d2d1.h>

#include <string_view>

#include "core/util.h"

namespace mdv {

enum class Icon : uint8_t {
    Info,       // alert: note
    LightBulb,  // alert: tip
    Report,     // alert: important
    Alert,      // alert: warning
    Stop,       // alert: caution
    Copy,
    Check,
    Image,      // image placeholder
    Link,
    ZoomIn,     // zoom buttons over images and diagrams
    ZoomOut,
    ScreenFull,
    Sync,       // reset the zoom
    X,          // close the full-window view
};

// Parsed icon geometries, per Direct2D factory. Not thread-safe: use one
// cache per renderer.
class IconCache {
public:
    ID2D1PathGeometry* Get(ID2D1Factory* factory, Icon icon);

private:
    ID2D1Factory* factory_ = nullptr;
    ComPtr<ID2D1Factory> factory_ref_;
    ComPtr<ID2D1PathGeometry> geo_[16];
};

// Draws a 16x16-unit icon scaled into `rect`.
void DrawIcon(ID2D1RenderTarget* rt, IconCache& cache, Icon icon, const D2D1_RECT_F& rect, ID2D1Brush* brush);

// Builds a path geometry from SVG path data ("M0 0L1 1Z", arcs supported).
ComPtr<ID2D1PathGeometry> ParseSvgPath(ID2D1Factory* factory, std::wstring_view d,
                                        D2D1_FILL_MODE fill = D2D1_FILL_MODE_WINDING);

// Disclosure triangle for <details>: pointing right (closed) or down (open).
void DrawDisclosure(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect, bool open, ID2D1Brush* brush);

// Task list checkbox.
void DrawCheckbox(ID2D1RenderTarget* rt, IconCache& cache, const D2D1_RECT_F& rect, bool checked,
                  ID2D1Brush* border,
                  ID2D1Brush* fill, ID2D1Brush* check_fill, ID2D1Brush* checkmark);

}  // namespace mdv
