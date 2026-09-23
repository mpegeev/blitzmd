// Draws a laid-out document with Direct2D.
#pragma once

#include <d2d1.h>

#include <vector>

#include "core/icons.h"
#include "core/layout.h"

namespace mdv {

struct TextRange {
    uint32_t start = 0;  // global text positions
    uint32_t end = 0;
};

struct RenderState {
    float scroll_y = 0;
    float view_w = 0;
    float view_h = 0;
    float dpi_scale = 1;
    TextRange selection;
    bool focused = true;
    const std::vector<TextRange>* hits = nullptr;   // search matches, sorted
    int current_hit = -1;
    const Block* hover_block = nullptr;             // code block under the mouse
    bool copy_button_hover = false;
    const Block* copied_block = nullptr;            // shows a check mark briefly
};

class Renderer {
public:
    // Draws the part of the document visible in `st` (document coordinates
    // are translated by -scroll_y on top of the target's current transform).
    void Draw(ID2D1RenderTarget* rt, LayoutEngine& layout, const RenderState& st);

    // Geometry shared with hit testing.
    static D2D1_RECT_F CopyButtonRect(const Block* code, const Metrics& m);
    static D2D1_RECT_F HScrollbarRect(const Block* b, const Metrics& m, bool thumb);

    IconCache& icons() { return icons_; }

private:
    void DrawBlock(Block* b);
    void DrawLeafScrolled(Block* b);
    void DrawChildren(Block* b);
    void DrawLeaf(Block* b);
    void DrawCodeBlock(Block* b);
    void DrawTable(Block* b);
    void DrawListItem(Block* item);
    void DrawAlert(Block* b);
    void DrawHScrollbar(Block* b);
    void DrawLineNumbers(Block* b, const LeafState& s);
    void FillRanges(IDWriteTextLayout* tl, D2D1_POINT_2F origin, uint32_t start, uint32_t len,
                    ID2D1Brush* brush, float inset_y = 0);
    void DrawInlineBackgrounds(Block* b, IDWriteTextLayout* tl, D2D1_POINT_2F origin);
    float Snap(float v) const;
    float Hairline() const;
    ID2D1SolidColorBrush* Brush(uint8_t role) const;

    ID2D1RenderTarget* rt_ = nullptr;
    LayoutEngine* layout_ = nullptr;
    Palette* palette_ = nullptr;
    const RenderState* st_ = nullptr;
    float view_top_ = 0, view_bottom_ = 0;
    IconCache icons_;
    std::vector<DWRITE_HIT_TEST_METRICS> hit_buf_;
    std::vector<DWRITE_LINE_METRICS> line_buf_;
};

// Number formatting for ordered list markers at a nesting depth
// (decimal, lower-roman, lower-alpha like GitHub).
std::wstring ListMarkerText(int number, int ordered_depth);

}  // namespace mdv
