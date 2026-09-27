// Printing (ListPrint): paginates the document at the printer's width and
// rasterizes each page with Direct2D in horizontal bands.
#pragma once

#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>

#include <memory>
#include <utility>
#include <vector>

#include "core/document.h"
#include "core/images.h"
#include "core/layout.h"
#include "core/render.h"
#include "core/theme.h"

namespace mdv {

// Lays out a document once and renders arbitrary horizontal bands of it.
class PageRenderer {
public:
    // `width` is the content width in DIPs; bands are `band_px` pixels tall
    // at `dpi`.
    bool Init(Document& doc, const LayoutConfig& config, float width, float dpi, UINT band_px);

    float doc_height() const { return layout_.doc_height(); }

    // Page breaks for pages of `page_h` DIPs, preferring line and block
    // boundaries. Returns [top, bottom) pairs in document coordinates.
    std::vector<std::pair<float, float>> Paginate(float page_h);

    // Renders document rows [y, y + height) (DIPs); rows at or below
    // `clip_bottom` are left blank. The result stays valid until the next call.
    IWICBitmap* RenderBand(float y, float height, float clip_bottom);

    UINT width_px() const { return width_px_; }
    UINT band_px() const { return band_px_; }
    float scale() const { return scale_; }

private:
    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dw_;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<IWICBitmap> bitmap_;
    ComPtr<ID2D1RenderTarget> rt_;
    Palette palette_;
    ImageStore images_;
    LayoutEngine layout_;
    Renderer renderer_;
    float width_ = 0, scale_ = 1;
    UINT width_px_ = 0, band_px_ = 0;
};

// Shows the print dialog and prints `doc`. The dialog starts with `printer`
// (the one chosen in Total Commander; null or empty, or one PrintDlg doesn't
// take: the default one). `margins` are in 0.1 mm (MM_LOMETRIC) as passed by
// Total Commander; may be null.
bool PrintDocument(HWND owner, Document& doc, const LayoutConfig& config, const std::wstring& title,
                   const wchar_t* printer, const RECT* margins);

// PRINTDLG's hDevNames naming `printer` (GlobalFree it), null for none.
HGLOBAL PrinterDevNames(const wchar_t* printer);

}  // namespace mdv
