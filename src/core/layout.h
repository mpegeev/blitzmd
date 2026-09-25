// Block layout on top of DirectWrite.
//
// Layout is split in two parts:
//  * measuring leaves (paragraphs, headings, code) with IDWriteTextLayout,
//    which is the expensive part and is cached per leaf;
//  * a cheap positioning pass over the block tree that assigns boxes.
// Leaves near the viewport are always measured exactly; the rest may use
// height estimates that are refined incrementally (Refine) in idle time, so
// even multi-megabyte documents open instantly.
#pragma once

#include <d2d1.h>
#include <dwrite_3.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/diagram.h"
#include "core/document.h"
#include "core/theme.h"
#include "core/util.h"

namespace mdv {

class ImageStore;
class LayoutEngine;
class MathTypesetter;
struct MathBox;

constexpr float kDiagramRefSize = 16.f;  // diagrams are laid out once at this size and scaled

struct LayoutConfig {
    std::wstring font_family = L"Segoe UI";
    std::wstring mono_family;           // empty = auto (Cascadia Mono / Consolas)
    float font_size = 16.f;             // base font size in DIPs at 100% zoom
    float zoom = 1.f;
    float max_content_width = 900.f;    // DIPs at 100% zoom; 0 = unlimited
    bool wrap_code = true;
    bool line_numbers = false;          // line numbers in code blocks
    bool dark = false;
    float page_padding = -1.f;          // DIPs around the content; <0 = automatic
};

// Resolved typography of one leaf.
struct LeafStyle {
    float size = 16.f;
    float line_height = 1.5f;           // CSS line-height multiplier
    uint16_t weight = 400;
    bool italic = false;
    bool mono = false;
    bool nowrap = false;
    bool rtl = false;
    uint8_t color = kColorText;         // default brush
    Align align = Align::Default;
};

struct LeafState {
    ComPtr<IDWriteTextLayout> layout;
    LeafStyle style;
    float width = -1.f;                 // layout width the measurement is for
    float text_h = 0.f;                 // height of the text layout
    float nowrap_w = 0.f;               // natural single-line width (0 = unknown)
    float min_w = 0.f;                  // widest unbreakable run
    float first_baseline = 0.f;         // first line baseline, relative to text top
    float tx = 0.f, ty = 0.f;           // text origin in document coordinates
    float gutter = 0.f;                 // code blocks: width of the line-number column
    float overflow_w = 0.f;             // text wider than its column (a formula): scrolls
    bool exact = false;
    bool minmax_valid = false;
    uint32_t used = 0;                  // frame stamp for eviction
};

struct TableInfo {
    std::vector<float> col_x;           // column left edges relative to table x (size cols+1)
    float width = 0.f;                  // total table width
};

struct Metrics {
    float em = 16.f;
    float page_pad_x = 32.f;
    float page_pad_top = 24.f;
    float page_pad_bottom = 32.f;
    float block_gap = 16.f;
    float code_pad = 16.f;
    float code_size = 13.6f;
    float quote_bar = 4.f;
    float quote_pad = 16.f;
    float list_indent = 32.f;
    float li_gap = 4.f;
    float cell_pad_x = 13.f;
    float cell_pad_y = 6.f;
    float rule_h = 4.f;
    float radius = 6.f;
    float inline_code_pad = 5.4f;
    float kbd_pad = 5.f;
};

// A zoomable object as last drawn on screen (view coordinates, DIPs).
struct DrawnObject {
    Block* leaf = nullptr;
    InlineObject* object = nullptr;
    D2D1_RECT_F box{};      // its layout box
    D2D1_RECT_F visible{};  // the part of it not clipped away
};

// Everything inline objects need while being drawn.
struct DrawContext {
    ID2D1RenderTarget* rt = nullptr;
    ImageStore* images = nullptr;
    Palette* palette = nullptr;
    ID2D1Brush* default_brush = nullptr;
    // On screen (not printing or thumbnails): objects are drawn zoomed and
    // listed in `drawn`, clipped to `clip` (view coordinates).
    bool interactive = false;
    std::vector<DrawnObject>* drawn = nullptr;
    D2D1_RECT_F clip{};
    Block* leaf = nullptr;  // leaf being drawn
    // Diagrams: the theme, and search hits and the selection in the leaf
    // being drawn (its positions), shown on the labels they touch.
    bool dark = false;
    std::vector<DiagramHighlight> diagram_highlights;
    ID2D1Brush* highlight_brushes[3] = {};  // search hit, current hit, selection
};

class LayoutEngine {
public:
    LayoutEngine();
    ~LayoutEngine();
    LayoutEngine(const LayoutEngine&) = delete;
    LayoutEngine& operator=(const LayoutEngine&) = delete;

    void Init(IDWriteFactory* dw, Palette* palette, ImageStore* images);
    void SetDocument(Document* doc);
    void SetConfig(const LayoutConfig& cfg);
    const LayoutConfig& config() const { return cfg_; }
    const Metrics& metrics() const { return m_; }
    Document* document() const { return doc_; }
    IDWriteFactory* dwrite() const { return dw_.Get(); }

    // Positions every block for the given viewport width. Leaves overlapping
    // [exact_y0, exact_y1] are measured exactly; others may be estimated.
    void Layout(float viewport_w, float exact_y0, float exact_y1);
    float doc_height() const { return doc_h_; }
    float doc_width() const { return doc_w_; }
    float viewport_width() const { return viewport_w_; }
    bool laid_out() const { return viewport_w_ > 0; }

    // Measures estimated leaves for up to budget_ms. Returns true if any
    // leaf changed (a new Layout pass is needed).
    bool Refine(double budget_ms);
    bool HasEstimates() const { return estimated_count_ > 0; }

    // Text layout of a leaf at its current width (created on demand).
    IDWriteTextLayout* TextLayout(Block* leaf);
    LeafState* State(const Block* leaf);
    D2D1_POINT_2F TextOrigin(const Block* leaf) const;

    // Pixel metrics of a run of text for decorations (code pills etc.).
    void InvalidateTextLayouts();       // brushes changed: rebuild lazily
    void InvalidateMeasurements();      // sizes changed (images, zoom...)
    void InvalidateLeaf(Block* leaf);
    void EvictFar(float y0, float y1);  // releases text layouts far from view
    void NextFrame() { ++frame_; }

    const TableInfo* Table(const Block* table) const;

    // Title layouts for alerts and such (cached); `wrap` > 0 wraps them to
    // that width.
    IDWriteTextLayout* LabelLayout(const std::wstring& text, float size, uint16_t weight, float wrap = 0);
    // Code block line numbers: a number in the code font, with its baseline.
    IDWriteTextLayout* LineNumberLayout(int number, float size, float* baseline);
    // Gap between the line numbers and the code.
    float GutterGap(float size) const { return size * 1.25f; }
    // A code block's note (Block::note): muted text above the code, after a
    // warning sign and clear of the copy button, in a block `block_w` wide.
    IDWriteTextLayout* NoteLayout(const std::wstring& note, float size, float block_w);
    float NoteHeight(const std::wstring& note, float size, float block_w);

    DrawContext& draw_context() { return draw_ctx_; }
    ImageStore* images() const { return images_; }
    // Diagram `index` of the document laid out at kDiagramRefSize DIPs per
    // em (it scales linearly); nullptr if it can't be drawn.
    std::shared_ptr<const DiagramScene> Diagram(int32_t index);
    // DIPs per diagram unit at the document's text size.
    float DiagramScale() const { return m_.em / kDiagramRefSize; }
    // Formula typesetter (created on first use; nullptr without a math font).
    MathTypesetter* math();
    Palette* palette() const { return palette_; }

    // Details toggling: marks descendants visible/hidden.
    static void UpdateVisibility(Block* b, bool visible);

private:
    struct Ctx {
        float scale = 1.f;              // font scale (footnotes)
        uint8_t color = kColorText;
        Align align = Align::Default;
        int list_depth = 0;
        bool header_cell = false;
    };
    struct Margins {
        float top = 0, bottom = 0;
    };

    float LayoutBlock(Block* b, float x, float y, float w, const Ctx& ctx);
    float LayoutChildren(Block* parent, float x, float y, float w, const Ctx& ctx, size_t first = 0);
    float LayoutLeaf(Block* b, float x, float y, float w, const Ctx& ctx);
    float LayoutList(Block* b, float x, float y, float w, const Ctx& ctx);
    float LayoutTable(Block* b, float x, float y, float w, const Ctx& ctx);
    float LayoutAlert(Block* b, float x, float y, float w, const Ctx& ctx);
    float LayoutDetails(Block* b, float x, float y, float w, const Ctx& ctx);
    Margins BlockMargins(const Block* b) const;
    float CollapsedMargin(const Block* b, bool top) const;
    void Translate(Block* b, float dx, float dy);
    // A hidden block and everything in it sit at (x, y) with no height, so
    // blocks and leaves stay sorted by position (drawing and hit tests
    // search them by binary search).
    void Park(Block* b, float x, float y, float w);

    LeafStyle ResolveStyle(const Block* b, const Ctx& ctx) const;
    float MeasureLeaf(Block* b, float width, bool exact);
    float EstimateLeaf(const Block* b, const LeafState& s, float width);
    void MinMaxWidth(Block* b, const Ctx& ctx, float& min_w, float& max_w, bool estimate);
    ComPtr<IDWriteTextLayout> BuildTextLayout(Block* b, const LeafStyle& st, float width);
    IDWriteTextFormat* Format(const std::wstring& family, float size, uint16_t weight, bool italic);
    // Ascent, descent (+ line gap) and their sum per em of a font family.
    struct FamilyMetrics {
        float ascent = 0.92f, descent = 0.41f;
        float x_height = 0.5f;
        float natural() const { return ascent + descent; }
    };
    const FamilyMetrics& MetricsOf(const std::wstring& family);
    float LineHeightFactor(const std::wstring& family, float css_line_height);
    float Zoom() const { return std::clamp(cfg_.zoom, 0.25f, 5.f); }
    // Returns the factor by which lines (and inline objects) get scaled.
    float ApplyLineSpacing(IDWriteTextLayout* layout, const std::wstring& family, float size,
                          float css_line_height);
    const std::wstring& MonoFamily() const { return mono_family_; }
    float FirstBaseline(Block* b) const;

    ComPtr<IDWriteFactory> dw_;
    Palette* palette_ = nullptr;
    ImageStore* images_ = nullptr;
    Document* doc_ = nullptr;
    LayoutConfig cfg_;
    Metrics m_;
    std::wstring mono_family_ = L"Consolas";
    std::vector<LeafState> leaves_;
    std::unordered_map<const Block*, TableInfo> tables_;
    std::unordered_map<std::wstring, ComPtr<IDWriteTextFormat>> formats_;
    std::unordered_map<std::wstring, FamilyMetrics> family_metrics_;
    std::unordered_map<std::wstring, ComPtr<IDWriteTextLayout>> labels_;
    struct NumberLabel {
        ComPtr<IDWriteTextLayout> layout;
        float baseline = 0;
    };
    std::unordered_map<uint64_t, NumberLabel> numbers_;   // (size, number) -> label
    float GutterWidth(const Block* b, const LeafStyle& st);
    // Formula `index` of the document at kMathRefSize DIPs per em (it scales
    // linearly); nullptr without a math font, or if not typeset yet and
    // `create` is false.
    std::shared_ptr<const MathBox> MathFormula(int32_t index, bool create = true);
    // Math font size next to text of `size` in `family`: x-heights match.
    float MathSize(const std::wstring& family, float size);
    std::unique_ptr<MathTypesetter> math_;
    bool math_failed_ = false;
    std::unordered_map<int32_t, std::shared_ptr<const MathBox>> math_boxes_;
    std::unordered_map<int32_t, std::shared_ptr<const DiagramScene>> diagrams_;
    float viewport_w_ = 0.f;
    float doc_h_ = 0.f;
    float doc_w_ = 0.f;
    float exact_y0_ = 0.f, exact_y1_ = 0.f;
    size_t estimated_count_ = 0;
    size_t refine_cursor_ = 0;
    uint32_t frame_ = 1;
    DrawContext draw_ctx_;
};

bool FontFamilyExists(IDWriteFactory* dw, const wchar_t* family);

// Baseline of the first line of a text layout (0 if it has no lines).
float FirstLineBaseline(IDWriteTextLayout* layout);

}  // namespace mdv
