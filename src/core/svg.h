// SVG images: shapes are rendered by Direct2D's SVG engine (Windows 10
// 1703+); <text> elements, which Direct2D ignores, are drawn by us with
// DirectWrite so badges (shields.io etc.) and diagrams keep their labels.
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
    void ReleaseDeviceResources() {
        doc_.Reset();
        doc_rt_ = nullptr;
        doc_failed_ = false;
    }

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

    void ParseStructure();

    std::string source_;
    float width_ = 300, height_ = 150;
    float vb_x_ = 0, vb_y_ = 0, vb_w_ = 0, vb_h_ = 0;
    std::vector<Text> texts_;

    ID2D1RenderTarget* doc_rt_ = nullptr;
    ComPtr<ID2D1SvgDocument> doc_;
    bool doc_failed_ = false;
};

}  // namespace mdv
