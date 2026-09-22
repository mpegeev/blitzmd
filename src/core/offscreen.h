// Rendering documents into bitmaps: thumbnails (ListGetPreviewBitmap),
// printing and the test tool's PNG output.
#pragma once

#include <wincodec.h>

#include <string>

#include "core/document.h"
#include "core/layout.h"
#include "core/util.h"

namespace mdv {

struct OffscreenOptions {
    LayoutConfig layout;           // fonts, zoom, dark theme...
    float width = 900.f;           // viewport width in DIPs
    float scale = 1.f;             // pixels per DIP
    float scroll_y = 0.f;          // first document row to render (DIPs)
    float max_height = 16000.f;    // DIPs; the document is cropped beyond this
    bool allow_remote_images = false;
    bool transparent = false;
};

struct OffscreenStats {
    float doc_height = 0;          // DIPs
    double parse_ms = 0, layout_ms = 0, render_ms = 0;
};

// Renders `doc` into a new 32bpp PBGRA WIC bitmap.
ComPtr<IWICBitmap> RenderToBitmap(Document& doc, const OffscreenOptions& opt, OffscreenStats* stats = nullptr);

bool SaveBitmapPng(IWICBitmap* bitmap, const std::wstring& path);

// Converts a WIC bitmap to a GDI DIB section (caller owns the HBITMAP).
HBITMAP BitmapToHBitmap(IWICBitmap* bitmap);

}  // namespace mdv
