// SVG images: shapes are rendered by Direct2D's SVG engine (Windows 10
// 1703+); <text> elements, which Direct2D ignores, are drawn by us with
// DirectWrite so badges (shields.io etc.) and diagrams keep their labels,
// and so are embedded <image>s (data: URIs only: badge logos).
#pragma once

#include <d2d1_3.h>
#include <dwrite.h>

#include <memory>
#include <string>
#include <vector>

#include "core/util.h"

namespace mdv {

class SvgImage {
public:
    // Returns nullptr if `bytes` doesn't look like an SVG document.
    static std::shared_ptr<SvgImage> Parse(std::string bytes);

    float width() const { return width_; }
    float height() const { return height_; }

    // Draws the image scaled into `rect`. Must be called on the thread that
    // owns `rt`; device resources are cached for the last render target.
    void Draw(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect, IDWriteFactory* dw);
    // Drops the cached document (the render target is going away; a new one
    // may get the same address).
    void ReleaseDeviceResources();
    // Memory it holds, roughly: the source, Direct2D's document of it once
    // drawn, raster <image>s.
    size_t MemoryBytes() const;

private:
    struct Text {
        std::wstring text;
        std::wstring family;
        float x = 0, y = 0;
        float size = 16;
        float text_length = 0;   // textLength attribute (0 = none)
        uint16_t weight = 400;
        bool italic = false;
        int anchor = 0;          // 0 start, 1 middle, 2 end
        uint32_t argb = 0xFF000000;
        D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
    };

    struct Image {
        float x = 0, y = 0, w = 0, h = 0;  // 0 size = intrinsic
        D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
        std::shared_ptr<SvgImage> svg;     // nested SVG, or
        UINT px_w = 0, px_h = 0;           // raster pixels (32bpp PBGRA)
        std::vector<uint8_t> pixels;
        ComPtr<ID2D1Bitmap> bitmap;        // device resource for doc_rt_
    };

    // `raster_left`: bytes of decoded raster <image>s the whole tree (this
    // document and those nested in it) may still take.
    static std::shared_ptr<SvgImage> Parse(std::string bytes, int depth, size_t* raster_left);
    void ParseStructure();
    void AddImage(Image im, std::wstring_view href);
    // The drawing itself, into `rt` (`dc` is it).
    void Render(ID2D1RenderTarget* rt, ID2D1DeviceContext5* dc, const D2D1_RECT_F& rect, IDWriteFactory* dw);
    // Draws from raster_ if it is (or now becomes) the one for this size.
    bool DrawRaster(ID2D1RenderTarget* rt, const D2D1_RECT_F& rect, IDWriteFactory* dw);

    std::string source_;
    int depth_ = 0;  // nesting level of this document inside <image>s
    size_t* raster_left_ = nullptr;  // while parsing: Parse's `raster_left`
    float width_ = 300, height_ = 150;
    float vb_x_ = 0, vb_y_ = 0, vb_w_ = 0, vb_h_ = 0;
    std::vector<Text> texts_;
    std::vector<Image> images_;
    size_t elements_ = 0;  // start tags

    ID2D1RenderTarget* doc_rt_ = nullptr;
    ComPtr<ID2D1SvgDocument> doc_;
    bool doc_failed_ = false;
    // Direct2D draws a big drawing (thousands of elements) anew each time,
    // some 0.1 s, and a document may show it many times each paint. Asked
    // for at one device size twice in a row, it is drawn from a bitmap of
    // itself at that size.
    ComPtr<ID2D1Bitmap> raster_;
    D2D1_SIZE_U raster_px_{}, asked_px_{};
    bool raster_failed_ = false;
};

}  // namespace mdv
