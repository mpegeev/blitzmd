// Zooming images inside their box and in the full-window view: the geometry
// shared by drawing and the viewer's mouse handling, and the buttons shown
// over them.
#pragma once

#include <d2d1.h>

#include <array>

#include "core/document.h"

namespace mdv {

class IconCache;
class Palette;

constexpr float kMaxObjectZoom = 10.f;

// Where the content of an object laid out in `box` is drawn at its zoom.
D2D1_RECT_F ZoomedRect(const D2D1_RECT_F& box, const InlineObject& o);
// Multiplies the zoom by `factor` (clamped to 1..kMaxObjectZoom), keeping the
// content under (x, y) in place.
void ZoomObject(InlineObject& o, const D2D1_RECT_F& box, float factor, float x, float y);
// Shifts the view `from` by a drag of (dx, dy).
void PanObject(InlineObject& o, const D2D1_RECT_F& box, float from_x, float from_y, float dx, float dy);
void ResetObjectView(InlineObject& o);
inline bool IsZoomed(const InlineObject& o) { return o.zoom > 1.f; }

// The full-window view of one object: its content at `scale` times its
// natural size, with the content point (cx, cy) (fractions of its size) at
// the middle of the window unless it fits there.
struct FullView {
    Block* leaf = nullptr;
    const InlineObject* object = nullptr;
    float natural_w = 0, natural_h = 0;  // DIPs
    float min_scale = 1, max_scale = 1;
    float scale = 1;
    float cx = 0.5f, cy = 0.5f;

    bool active() const { return object != nullptr; }
    // Opens at the natural size, or smaller to fit `view`.
    void Open(Block* leaf, const InlineObject* object, float w, float h, const D2D1_RECT_F& view);
    void Close() { *this = FullView{}; }
    void Reset(const D2D1_RECT_F& view);
    void Fit(const D2D1_RECT_F& view);  // after the window changed size
    D2D1_RECT_F ContentRect(const D2D1_RECT_F& view) const;
    void ZoomAt(float factor, float x, float y, const D2D1_RECT_F& view);
    void PanBy(float dx, float dy, const D2D1_RECT_F& view);
    bool CanPan(const D2D1_RECT_F& view) const;

private:
    float FitScale(const D2D1_RECT_F& view) const;
    void Clamp(const D2D1_RECT_F& view);
};

// Buttons in the top right corner of a zoomable object or of the window.
enum class ZoomButton : uint8_t { ZoomOut, ZoomIn, Reset, Expand, Close };

struct ZoomToolbar {
    std::array<D2D1_RECT_F, 4> rects{};
    std::array<ZoomButton, 4> kinds{};
    int count = 0;  // 0: no room for it
    float em = 16;
};

// `area` is the visible part of the object (or the window).
ZoomToolbar MakeZoomToolbar(const D2D1_RECT_F& area, float em, bool full_window);
int ZoomButtonAt(const ZoomToolbar& t, float x, float y);  // -1: none
// `disabled`: bit per button index.
void DrawZoomToolbar(ID2D1RenderTarget* rt, IconCache& icons, const Palette& palette, const ZoomToolbar& t,
                     int hover, unsigned disabled);

}  // namespace mdv
