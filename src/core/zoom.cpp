#include "core/zoom.h"

#include <algorithm>

#include "core/icons.h"
#include "core/theme.h"

namespace mdv {
namespace {

void ClampView(InlineObject& o) {
    if (!(o.zoom > 1.0001f)) {
        ResetObjectView(o);
        return;
    }
    o.zoom = std::min(o.zoom, kMaxObjectZoom);
    const float max = 1.f - 1.f / o.zoom;
    o.view_x = std::clamp(o.view_x, 0.f, max);
    o.view_y = std::clamp(o.view_y, 0.f, max);
}

}  // namespace

D2D1_RECT_F ZoomedRect(const D2D1_RECT_F& box, const InlineObject& o) {
    const float cw = (box.right - box.left) * o.zoom, ch = (box.bottom - box.top) * o.zoom;
    const float left = box.left - o.view_x * cw, top = box.top - o.view_y * ch;
    return D2D1::RectF(left, top, left + cw, top + ch);
}

void ZoomObject(InlineObject& o, const D2D1_RECT_F& box, float factor, float x, float y) {
    const float w = box.right - box.left, h = box.bottom - box.top;
    if (!(w > 0 && h > 0 && factor > 0)) return;
    const float z = std::clamp(o.zoom * factor, 1.f, kMaxObjectZoom);
    const float fx = o.view_x + (x - box.left) / (w * o.zoom);  // content under the point
    const float fy = o.view_y + (y - box.top) / (h * o.zoom);
    o.zoom = z;
    o.view_x = fx - (x - box.left) / (w * z);
    o.view_y = fy - (y - box.top) / (h * z);
    ClampView(o);
}

void PanObject(InlineObject& o, const D2D1_RECT_F& box, float from_x, float from_y, float dx, float dy) {
    const float w = box.right - box.left, h = box.bottom - box.top;
    if (!(w > 0 && h > 0)) return;
    o.view_x = from_x - dx / (w * o.zoom);
    o.view_y = from_y - dy / (h * o.zoom);
    ClampView(o);
}

void ResetObjectView(InlineObject& o) {
    o.zoom = 1.f;
    o.view_x = o.view_y = 0.f;
}

// ---------------------------------------------------------------------------
// Full-window view
// ---------------------------------------------------------------------------

namespace {
constexpr float kFullViewMargin = 24.f;
}

float FullView::FitScale(const D2D1_RECT_F& view) const {
    const float vw = view.right - view.left - 2 * kFullViewMargin;
    const float vh = view.bottom - view.top - 2 * kFullViewMargin;
    if (!(natural_w > 0 && natural_h > 0)) return 1.f;
    return std::max(std::min(vw / natural_w, vh / natural_h), 0.01f);
}

void FullView::Open(Block* l, const InlineObject* o, float w, float h, const D2D1_RECT_F& view) {
    leaf = l;
    object = o;
    natural_w = std::max(w, 1.f);
    natural_h = std::max(h, 1.f);
    Reset(view);
}

void FullView::Reset(const D2D1_RECT_F& view) {
    scale = std::min(1.f, FitScale(view));
    min_scale = scale;
    max_scale = std::max(8.f, scale * 16.f);
    cx = cy = 0.5f;
}

void FullView::Fit(const D2D1_RECT_F& view) {
    min_scale = std::min(1.f, FitScale(view));
    max_scale = std::max(8.f, min_scale * 16.f);
    scale = std::clamp(scale, min_scale, max_scale);
    Clamp(view);
}

D2D1_RECT_F FullView::ContentRect(const D2D1_RECT_F& view) const {
    const float vw = view.right - view.left, vh = view.bottom - view.top;
    const float w = natural_w * scale, h = natural_h * scale;
    const float left = w <= vw ? view.left + (vw - w) * 0.5f : view.left + vw * 0.5f - cx * w;
    const float top = h <= vh ? view.top + (vh - h) * 0.5f : view.top + vh * 0.5f - cy * h;
    return D2D1::RectF(left, top, left + w, top + h);
}

void FullView::ZoomAt(float factor, float x, float y, const D2D1_RECT_F& view) {
    if (!(factor > 0)) return;
    const D2D1_RECT_F r = ContentRect(view);
    const float fx = std::clamp((x - r.left) / (r.right - r.left), 0.f, 1.f);
    const float fy = std::clamp((y - r.top) / (r.bottom - r.top), 0.f, 1.f);
    scale = std::clamp(scale * factor, min_scale, max_scale);
    const float w = natural_w * scale, h = natural_h * scale;
    // The point under the cursor stays there.
    cx = ((view.left + view.right) * 0.5f - (x - fx * w)) / w;
    cy = ((view.top + view.bottom) * 0.5f - (y - fy * h)) / h;
    Clamp(view);
}

void FullView::PanBy(float dx, float dy, const D2D1_RECT_F& view) {
    cx -= dx / (natural_w * scale);
    cy -= dy / (natural_h * scale);
    Clamp(view);
}

bool FullView::CanPan(const D2D1_RECT_F& view) const {
    return natural_w * scale > view.right - view.left + 0.5f || natural_h * scale > view.bottom - view.top + 0.5f;
}

void FullView::Clamp(const D2D1_RECT_F& view) {
    const float w = natural_w * scale, h = natural_h * scale;
    const float hw = (view.right - view.left) * 0.5f, hh = (view.bottom - view.top) * 0.5f;
    cx = w > 2 * hw ? std::clamp(cx, hw / w, 1.f - hw / w) : 0.5f;
    cy = h > 2 * hh ? std::clamp(cy, hh / h, 1.f - hh / h) : 0.5f;
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

ZoomToolbar MakeZoomToolbar(const D2D1_RECT_F& area, float em, bool full_window) {
    ZoomToolbar t;
    t.em = em;
    const float size = em * 1.75f, gap = em * 0.25f, margin = em * 0.5f;
    const float width = 4 * size + 3 * gap;
    if (area.right - area.left < width + 2 * margin || area.bottom - area.top < size + 2 * margin) return t;
    t.kinds = {ZoomButton::ZoomOut, ZoomButton::ZoomIn, ZoomButton::Reset,
               full_window ? ZoomButton::Close : ZoomButton::Expand};
    float left = area.right - margin - width;
    const float top = area.top + margin;
    for (int i = 0; i < 4; ++i) {
        t.rects[static_cast<size_t>(i)] = D2D1::RectF(left, top, left + size, top + size);
        left += size + gap;
    }
    t.count = 4;
    return t;
}

int ZoomButtonAt(const ZoomToolbar& t, float x, float y) {
    for (int i = 0; i < t.count; ++i) {
        const D2D1_RECT_F& r = t.rects[static_cast<size_t>(i)];
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
    }
    return -1;
}

void DrawZoomToolbar(ID2D1RenderTarget* rt, IconCache& icons, const Palette& palette, const ZoomToolbar& t,
                     int hover, unsigned disabled) {
    const float radius = t.em * 0.375f;
    for (int i = 0; i < t.count; ++i) {
        const D2D1_RECT_F& r = t.rects[static_cast<size_t>(i)];
        const bool off = (disabled >> i) & 1u;
        const bool hot = hover == i && !off;
        rt->FillRoundedRectangle(D2D1_ROUNDED_RECT{r, radius, radius},
                                 palette.Brush(hot ? kColorButtonHover : kColorButtonBg));
        const D2D1_ROUNDED_RECT edge{D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), radius,
                                     radius};
        rt->DrawRoundedRectangle(edge, palette.Brush(kColorBorder), 1.f);
        Icon icon = Icon::ZoomIn;
        switch (t.kinds[static_cast<size_t>(i)]) {
        case ZoomButton::ZoomOut: icon = Icon::ZoomOut; break;
        case ZoomButton::ZoomIn: icon = Icon::ZoomIn; break;
        case ZoomButton::Reset: icon = Icon::Sync; break;
        case ZoomButton::Expand: icon = Icon::ScreenFull; break;
        case ZoomButton::Close: icon = Icon::X; break;
        }
        const float isz = t.em;
        const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
        DrawIcon(rt, icons, icon, D2D1::RectF(cx - isz * 0.5f, cy - isz * 0.5f, cx + isz * 0.5f, cy + isz * 0.5f),
                 palette.Brush(off ? kColorBorder : (hot ? kColorText : kColorMuted)));
    }
}

}  // namespace mdv
