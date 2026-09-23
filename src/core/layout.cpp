#include "core/layout.h"

#include <algorithm>
#include <cmath>

#include "core/highlight.h"
#include "core/images.h"
#include "core/mathlayout.h"

namespace mdv {
namespace {

constexpr float kHugeWidth = 100000.f;
constexpr wchar_t kObjectChar = 0xFFFC;
constexpr float kMathRefSize = 64.f;  // formulas are typeset once at this size and scaled

bool IsStrongRtl(wchar_t c) {
    return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF);
}

bool IsStrongLtr(wchar_t c) {
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= 0x00C0 && c <= 0x058F) ||
           (c >= 0x0900 && c <= 0x1FFF) || (c >= 0x3040 && c <= 0xD7FF);
}

// Paragraph direction from the first strong character (like dir="auto");
// the letters of a formula's TeX source don't count.
bool DetectRtl(const Block* b) {
    const std::wstring& t = b->text;
    for (size_t i = 0; i < t.size(); ++i) {
        for (const InlineObject& o : b->objects) {
            if (o.math >= 0 && o.pos == i) i += o.length;
        }
        if (i >= t.size()) break;
        if (IsStrongRtl(t[i])) return true;
        if (IsStrongLtr(t[i])) return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Inline objects
// ---------------------------------------------------------------------------

class InlineObjectBase : public IDWriteInlineObject {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteInlineObject)) {
            *ppv = static_cast<IDWriteInlineObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&ref_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }
    HRESULT STDMETHODCALLTYPE GetOverhangMetrics(DWRITE_OVERHANG_METRICS* o) override {
        *o = DWRITE_OVERHANG_METRICS{};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetBreakConditions(DWRITE_BREAK_CONDITION* before,
                                                 DWRITE_BREAK_CONDITION* after) override {
        *before = DWRITE_BREAK_CONDITION_NEUTRAL;
        *after = DWRITE_BREAK_CONDITION_NEUTRAL;
        return S_OK;
    }

protected:
    virtual ~InlineObjectBase() = default;

private:
    LONG ref_ = 1;
};

// An image sitting on the baseline, with the line's strut descent below it
// (the gap under inline images in CSS). Proportional line spacing scales
// every line by `spacing`, inline objects included, which would add leading
// above tall images (CSS doesn't); the reported size is pre-divided.
class ImageObject final : public InlineObjectBase {
public:
    ImageObject(LayoutEngine* engine, int index, float w, float h, float em, float spacing, float strut_descent)
        : engine_(engine), index_(index), w_(w), h_(h), em_(em), baseline_(h / std::max(spacing, 0.5f)),
          descent_(strut_descent) {}

    HRESULT STDMETHODCALLTYPE Draw(void*, IDWriteTextRenderer*, FLOAT x, FLOAT y, BOOL, BOOL,
                                   IUnknown*) override {
        DrawContext& dc = engine_->draw_context();
        if (dc.rt && dc.images && w_ > 0 && h_ > 0) {
            const float top = y + baseline_ - h_;  // y is the line baseline - baseline_
            dc.images->Draw(dc.rt, index_, D2D1::RectF(x, top, x + w_, top + h_), dc.palette,
                            engine_->dwrite(), em_);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS* m) override {
        m->width = w_;
        m->height = baseline_ + descent_;
        m->baseline = baseline_;
        m->supportsSideways = FALSE;
        return S_OK;
    }

private:
    LayoutEngine* engine_;
    int index_;
    float w_, h_, em_, baseline_, descent_;
};

// Superscript / subscript text: drawn as a small text layout shifted
// relative to the baseline without affecting line height (like GitHub's
// "position: relative; line-height: 0" CSS): it reports the metrics of the
// paragraph font (the CSS strut), so even a line holding nothing else keeps
// the height of a text line.
class TextBoxObject final : public InlineObjectBase {
public:
    TextBoxObject(LayoutEngine* engine, ComPtr<IDWriteTextLayout> layout, float shift, float strut_ascent,
                  float strut_descent)
        : engine_(engine), layout_(std::move(layout)), shift_(shift), strut_ascent_(strut_ascent),
          strut_descent_(strut_descent) {
        DWRITE_TEXT_METRICS tm{};
        layout_->GetMetrics(&tm);
        w_ = tm.widthIncludingTrailingWhitespace;
        baseline_ = FirstLineBaseline(layout_.Get());
    }
    HRESULT STDMETHODCALLTYPE Draw(void*, IDWriteTextRenderer*, FLOAT x, FLOAT y, BOOL, BOOL,
                                   IUnknown* effect) override {
        DrawContext& dc = engine_->draw_context();
        if (!dc.rt) return S_OK;
        ID2D1Brush* brush = dc.default_brush;
        ComPtr<ID2D1Brush> effect_brush;
        if (effect && SUCCEEDED(effect->QueryInterface(IID_PPV_ARGS(&effect_brush)))) {
            brush = effect_brush.Get();
        }
        if (brush) {
            // y is the line baseline - strut ascent.
            const float top = y + strut_ascent_ - baseline_ - shift_;
            dc.rt->DrawTextLayout(D2D1::Point2F(x, top), layout_.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS* m) override {
        m->width = w_;
        m->height = strut_ascent_ + strut_descent_;
        m->baseline = strut_ascent_;
        m->supportsSideways = FALSE;
        return S_OK;
    }

private:
    LayoutEngine* engine_;
    ComPtr<IDWriteTextLayout> layout_;
    float shift_, strut_ascent_, strut_descent_;
    float w_ = 0, baseline_ = 0;
};

// A flag emoji (flags.h): an SVG in a square the size of a color emoji
// glyph, centered where Segoe UI Emoji centers its glyphs. It reports the
// paragraph strut like text does, so the line keeps its height.
constexpr float kEmojiAdvance = 1.375f;  // em, as Segoe UI Emoji
constexpr float kEmojiBox = 1.f;         // em (a flag is as wide as a smiley)
constexpr float kEmojiCenter = 0.33f;    // em above the baseline

class EmojiObject final : public InlineObjectBase {
public:
    EmojiObject(LayoutEngine* engine, int index, float size, float strut_ascent, float strut_descent)
        : engine_(engine), index_(index), size_(size), ascent_(strut_ascent), descent_(strut_descent) {}

    HRESULT STDMETHODCALLTYPE Draw(void*, IDWriteTextRenderer*, FLOAT x, FLOAT y, BOOL, BOOL,
                                   IUnknown*) override {
        DrawContext& dc = engine_->draw_context();
        if (dc.rt && dc.images) {
            // y is the line baseline - strut ascent.
            const float box = size_ * kEmojiBox;
            const float left = x + (size_ * kEmojiAdvance - box) * 0.5f;
            const float top = y + ascent_ - size_ * kEmojiCenter - box * 0.5f;
            dc.images->Draw(dc.rt, index_, D2D1::RectF(left, top, left + box, top + box), dc.palette,
                            engine_->dwrite(), size_);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS* m) override {
        m->width = size_ * kEmojiAdvance;
        m->height = ascent_ + descent_;
        m->baseline = ascent_;
        m->supportsSideways = FALSE;
        return S_OK;
    }

private:
    LayoutEngine* engine_;
    int index_;
    float size_, ascent_, descent_;
};

// A formula (mathlayout.h) over its TeX source. It sits on the baseline like
// text and reports the paragraph strut, so only a formula taller than the
// text makes its line taller (the excess pre-divided by the line spacing,
// like images, so no extra leading appears).
class MathObject final : public InlineObjectBase {
public:
    MathObject(LayoutEngine* engine, std::shared_ptr<const MathBox> box, float scale, float raise, float strut_ascent,
               float strut_descent, float spacing)
        : engine_(engine), box_(std::move(box)), scale_(scale), raise_(raise) {
        const float k = 1.f / std::max(spacing, 0.5f);
        ascent_ = std::max(strut_ascent, (box_->ascent * scale_ + raise_) * k);
        descent_ = std::max(strut_descent, (box_->descent * scale_ - raise_) * k);
    }

    HRESULT STDMETHODCALLTYPE Draw(void*, IDWriteTextRenderer*, FLOAT x, FLOAT y, BOOL, BOOL,
                                   IUnknown* effect) override {
        DrawContext& dc = engine_->draw_context();
        MathTypesetter* math = engine_->math();
        if (!dc.rt || !math) return S_OK;
        ID2D1Brush* brush = dc.default_brush;
        ComPtr<ID2D1Brush> effect_brush;
        if (effect && SUCCEEDED(effect->QueryInterface(IID_PPV_ARGS(&effect_brush)))) brush = effect_brush.Get();
        math->Draw(dc.rt, *box_, D2D1::Point2F(x, y + ascent_ - raise_), scale_, brush);  // y: baseline - ascent
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS* m) override {
        m->width = box_->width * scale_;
        m->height = ascent_ + descent_;
        m->baseline = ascent_;
        m->supportsSideways = FALSE;
        return S_OK;
    }

private:
    LayoutEngine* engine_;
    std::shared_ptr<const MathBox> box_;
    float scale_, raise_, ascent_ = 0, descent_ = 0;
};

bool HasMath(const Block* b) {
    return std::any_of(b->objects.begin(), b->objects.end(), [](const InlineObject& o) { return o.math >= 0; });
}

// Font size of a run (inline code, <small>, <big>...).
float RunFontSize(float size, uint32_t f) {
    if (f & kSmall) size *= 0.85f;
    if (f & kBig) size *= 1.2f;
    if (f & (kCode | kKbd)) size *= (f & kKbd) ? 0.8f : 0.85f;
    return size;
}

std::wstring FormatKey(const std::wstring& family, float size, uint16_t weight, bool italic) {
    return family + L"|" + std::to_wstring(static_cast<int>(size * 100.f + 0.5f)) + L"|" +
           std::to_wstring(weight) + (italic ? L"|i" : L"|n");
}

float WeightFor(uint32_t flags, uint16_t base) {
    if (flags & kBold) return base >= 600 ? 700.f : 600.f;
    return base;
}

}  // namespace

float FirstLineBaseline(IDWriteTextLayout* layout) {
    UINT32 count = 0;
    layout->GetLineMetrics(nullptr, 0, &count);  // E_NOT_SUFFICIENT_BUFFER, sets count
    if (count == 0) return 0.f;
    DWRITE_LINE_METRICS local[4];
    std::vector<DWRITE_LINE_METRICS> big;
    DWRITE_LINE_METRICS* lines = local;
    if (count > 4) {
        big.resize(count);
        lines = big.data();
    }
    if (FAILED(layout->GetLineMetrics(lines, count, &count)) || count == 0) return 0.f;
    return lines[0].baseline;
}

bool FontFamilyExists(IDWriteFactory* dw, const wchar_t* family) {
    ComPtr<IDWriteFontCollection> coll;
    if (FAILED(dw->GetSystemFontCollection(&coll))) return false;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(coll->FindFamilyName(family, &index, &exists)) && exists;
}

LayoutEngine::LayoutEngine() = default;
LayoutEngine::~LayoutEngine() = default;

void LayoutEngine::Init(IDWriteFactory* dw, Palette* palette, ImageStore* images) {
    dw_ = dw;
    palette_ = palette;
    images_ = images;
    SetConfig(cfg_);
}

void LayoutEngine::SetDocument(Document* doc) {
    doc_ = doc;
    leaves_.clear();
    tables_.clear();
    math_boxes_.clear();
    if (doc_) {
        leaves_.resize(doc_->leaves.size());
        UpdateVisibility(doc_->root, true);
    }
    viewport_w_ = 0;
    doc_h_ = 0;
    refine_cursor_ = 0;
}

void LayoutEngine::SetConfig(const LayoutConfig& cfg) {
    cfg_ = cfg;
    if (cfg_.font_family.empty() || (dw_ && !FontFamilyExists(dw_.Get(), cfg_.font_family.c_str()))) {
        cfg_.font_family = L"Segoe UI";
    }
    mono_family_ = cfg_.mono_family;
    if (dw_ && (mono_family_.empty() || !FontFamilyExists(dw_.Get(), mono_family_.c_str()))) {
        mono_family_ = FontFamilyExists(dw_.Get(), L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
    }
    const float em = std::clamp(cfg_.font_size, 6.f, 72.f) * Zoom();
    m_.em = em;
    m_.block_gap = em;
    m_.code_pad = em;
    m_.code_size = em * 0.85f;
    m_.quote_bar = em * 0.25f;
    m_.quote_pad = em;
    m_.list_indent = em * 2.f;
    m_.li_gap = em * 0.25f;
    m_.cell_pad_x = em * 13.f / 16.f;
    m_.cell_pad_y = em * 6.f / 16.f;
    m_.rule_h = em * 0.25f;
    m_.radius = em * 6.f / 16.f;
    m_.inline_code_pad = m_.code_size * 0.4f;
    m_.kbd_pad = em * 5.f / 16.f;
    formats_.clear();
    labels_.clear();
    numbers_.clear();
    InvalidateMeasurements();
}

MathTypesetter* LayoutEngine::math() {
    if (!math_ && !math_failed_ && dw_) {
        math_ = std::make_unique<MathTypesetter>();
        if (!math_->Init(dw_.Get())) {
            math_.reset();
            math_failed_ = true;
        }
    }
    return math_.get();
}

std::shared_ptr<const MathBox> LayoutEngine::MathFormula(int32_t index, bool create) {
    if (!doc_ || index < 0 || static_cast<size_t>(index) >= doc_->maths.size()) return nullptr;
    auto it = math_boxes_.find(index);
    if (it != math_boxes_.end()) return it->second;
    MathTypesetter* m = create ? math() : nullptr;
    if (!m) return nullptr;
    if (math_boxes_.size() >= 50000) math_boxes_.clear();  // a bound, a few KB each
    const MathRef& ref = doc_->maths[static_cast<size_t>(index)];
    auto box = m->Layout(ref.tex, ref.display, kMathRefSize);
    math_boxes_.emplace(index, box);
    return box;
}

float LayoutEngine::MathSize(const std::wstring& family, float size) {
    // Like MathJax, math is scaled so its x-height matches the text's.
    MathTypesetter* m = math();
    return m ? size * std::clamp(MetricsOf(family).x_height / m->XHeight(), 0.85f, 1.3f) : size;
}

// ---------------------------------------------------------------------------
// Fonts & formats
// ---------------------------------------------------------------------------

IDWriteTextFormat* LayoutEngine::Format(const std::wstring& family, float size, uint16_t weight,
                                        bool italic) {
    const std::wstring key = FormatKey(family, size, weight, italic);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> fmt;
    if (FAILED(dw_->CreateTextFormat(family.c_str(), nullptr, static_cast<DWRITE_FONT_WEIGHT>(weight),
                                     italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, size, L"", &fmt))) {
        return nullptr;
    }
    IDWriteTextFormat* raw = fmt.Get();
    formats_.emplace(key, std::move(fmt));
    return raw;
}

const LayoutEngine::FamilyMetrics& LayoutEngine::MetricsOf(const std::wstring& family) {
    auto it = family_metrics_.find(family);
    if (it != family_metrics_.end()) return it->second;
    FamilyMetrics m;
    ComPtr<IDWriteFontCollection> coll;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(dw_->GetSystemFontCollection(&coll)) &&
        SUCCEEDED(coll->FindFamilyName(family.c_str(), &index, &exists)) && exists) {
        ComPtr<IDWriteFontFamily> fam;
        ComPtr<IDWriteFont> font;
        if (SUCCEEDED(coll->GetFontFamily(index, &fam)) &&
            SUCCEEDED(fam->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                DWRITE_FONT_STYLE_NORMAL, &font))) {
            DWRITE_FONT_METRICS fm{};
            font->GetMetrics(&fm);
            if (fm.designUnitsPerEm > 0) {
                const float upem = fm.designUnitsPerEm;
                m.ascent = fm.ascent / upem;
                m.descent = (fm.descent + fm.lineGap) / upem;
                if (fm.xHeight > 0) m.x_height = fm.xHeight / upem;
            }
        }
    }
    return family_metrics_.emplace(family, m).first->second;
}

float LayoutEngine::LineHeightFactor(const std::wstring& family, float css_line_height) {
    return css_line_height / std::max(MetricsOf(family).natural(), 0.5f);
}

float LayoutEngine::ApplyLineSpacing(IDWriteTextLayout* layout, const std::wstring& family, float size,
                                     float css_line_height) {
    ComPtr<IDWriteTextLayout3> l3;
    if (SUCCEEDED(layout->QueryInterface(IID_PPV_ARGS(&l3)))) {
        DWRITE_LINE_SPACING ls{};
        ls.method = DWRITE_LINE_SPACING_METHOD_PROPORTIONAL;
        ls.height = LineHeightFactor(family, css_line_height);
        ls.baseline = ls.height;
        ls.leadingBefore = 0.5f;
        ls.fontLineGapUsage = DWRITE_FONT_LINE_GAP_USAGE_ENABLED;
        l3->SetLineSpacing(&ls);
        return ls.height;
    }
    layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, size * css_line_height,
                           size * (css_line_height * 0.5f + 0.42f));
    return 1.f;
}

// ---------------------------------------------------------------------------
// Leaves
// ---------------------------------------------------------------------------

LeafState* LayoutEngine::State(const Block* leaf) {
    if (!leaf || leaf->leaf_index < 0 || static_cast<size_t>(leaf->leaf_index) >= leaves_.size()) {
        return nullptr;
    }
    return &leaves_[static_cast<size_t>(leaf->leaf_index)];
}

D2D1_POINT_2F LayoutEngine::TextOrigin(const Block* leaf) const {
    if (!leaf || leaf->leaf_index < 0 || static_cast<size_t>(leaf->leaf_index) >= leaves_.size()) {
        return D2D1::Point2F(0, 0);
    }
    const LeafState& s = leaves_[static_cast<size_t>(leaf->leaf_index)];
    return D2D1::Point2F(s.tx, s.ty);
}

LeafStyle LayoutEngine::ResolveStyle(const Block* b, const Ctx& ctx) const {
    LeafStyle st;
    const float em = m_.em * ctx.scale;
    st.color = ctx.color;
    st.align = b->align != Align::Default ? b->align : ctx.align;
    switch (b->type) {
    case BlockType::Heading: {
        static constexpr float kScale[6] = {2.f, 1.5f, 1.25f, 1.f, 0.875f, 0.85f};
        st.size = em * kScale[std::clamp<int>(b->level, 1, 6) - 1];
        st.weight = 600;
        st.line_height = 1.25f;
        if (b->level == 6) st.color = kColorMuted;
        break;
    }
    case BlockType::CodeBlock:
        st.mono = true;
        st.size = em * 0.85f;
        st.line_height = 1.45f;
        st.nowrap = !cfg_.wrap_code;
        st.align = Align::Left;
        if (ctx.color == kColorMuted) st.color = kColorText;
        break;
    default:
        st.size = em;
        st.line_height = 1.5f;
        break;
    }
    if (ctx.header_cell) st.weight = 600;
    if (b->base_flags & kBold) st.weight = 600;
    if (b->base_flags & kItalic) st.italic = true;
    st.rtl = b->type != BlockType::CodeBlock && DetectRtl(b);
    return st;
}

ComPtr<IDWriteTextLayout> LayoutEngine::BuildTextLayout(Block* b, const LeafStyle& st, float width) {
    const std::wstring& family = st.mono ? mono_family_ : cfg_.font_family;
    IDWriteTextFormat* fmt = Format(family, st.size, st.weight, st.italic);
    if (!fmt) return nullptr;
    const UINT32 len = static_cast<UINT32>(b->text.size());
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw_->CreateTextLayout(b->text.c_str(), len, fmt, std::max(width, 1.f), kHugeWidth, &layout))) {
        return nullptr;
    }
    if (st.nowrap) {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    } else if (FAILED(layout->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK))) {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }
    if (st.rtl) layout->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
    switch (st.align) {
    case Align::Center: layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); break;
    case Align::Right:
        layout->SetTextAlignment(st.rtl ? DWRITE_TEXT_ALIGNMENT_LEADING : DWRITE_TEXT_ALIGNMENT_TRAILING);
        break;
    case Align::Left:
        layout->SetTextAlignment(st.rtl ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
        break;
    default: break;
    }
    const float spacing = ApplyLineSpacing(layout.Get(), family, st.size, st.line_height);
    if (st.mono) layout->SetIncrementalTabStop(st.size * 0.6f * 4.f);

    ComPtr<IDWriteTextLayout1> l1;
    layout.As(&l1);
    const std::wstring& text = b->text;

    // Character formatting.
    for (const StyleRun& r : b->runs) {
        if (r.length == 0 || r.start >= len) continue;
        const DWRITE_TEXT_RANGE rg{r.start, std::min(r.length, len - r.start)};
        const uint32_t f = r.flags;
        if (f & kBold) layout->SetFontWeight(static_cast<DWRITE_FONT_WEIGHT>(WeightFor(f, st.weight)), rg);
        if (f & kItalic) layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, rg);
        if (f & kStrike) layout->SetStrikethrough(TRUE, rg);
        if (f & kUnderline) layout->SetUnderline(TRUE, rg);
        const float size = RunFontSize(st.size, f);
        if (f & (kCode | kKbd)) layout->SetFontFamilyName(mono_family_.c_str(), rg);
        if (f & kMath) layout->SetFontFamilyName(L"Cambria Math", rg);
        if (size != st.size) layout->SetFontSize(size, rg);
        uint8_t color = 0xFF;
        if (r.token) color = static_cast<uint8_t>(kColorSyntaxBase + r.token);
        if (f & kMuted) color = kColorMuted;
        if (f & kMark) color = kColorMarkText;
        if (r.link >= 0) color = kColorLink;
        if (color != 0xFF && palette_ && palette_->Brush(color)) {
            layout->SetDrawingEffect(palette_->Brush(color), rg);
        }
    }

    // Padding around inline code / kbd: extra spacing on the first and last
    // character of each maximal run.
    if (l1 && !st.mono) {
        size_t i = 0;
        const size_t nr = b->runs.size();
        while (i < nr) {
            const uint32_t kind = b->runs[i].flags & (kCode | kKbd);
            if (!kind) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j + 1 < nr && (b->runs[j + 1].flags & (kCode | kKbd)) == kind &&
                   b->runs[j + 1].start == b->runs[j].start + b->runs[j].length) {
                ++j;
            }
            const uint32_t s0 = b->runs[i].start;
            const uint32_t s1 = b->runs[j].start + b->runs[j].length;
            if (s1 > s0 && s1 <= len) {
                const float pad = (kind & kKbd) ? m_.kbd_pad : m_.inline_code_pad;
                const float gap = pad + 1.f;  // pad inside the pill + 1 DIP outside
                if (s1 - s0 == 1) {
                    l1->SetCharacterSpacing(gap, gap, 0, DWRITE_TEXT_RANGE{s0, 1});
                } else {
                    l1->SetCharacterSpacing(gap, 0, 0, DWRITE_TEXT_RANGE{s0, 1});
                    l1->SetCharacterSpacing(0, gap, 0, DWRITE_TEXT_RANGE{s1 - 1, 1});
                }
            }
            i = j + 1;
        }
    }

    // Superscript / subscript ranges become inline text boxes.
    {
        size_t i = 0;
        const size_t nr = b->runs.size();
        while (i < nr) {
            const uint32_t kind = b->runs[i].flags & (kSup | kSub);
            if (!kind || kind == (kSup | kSub) || (b->runs[i].flags & kMath)) {  // formulas raise themselves
                ++i;
                continue;
            }
            size_t j = i;
            while (j + 1 < nr && (b->runs[j + 1].flags & (kSup | kSub)) == kind && !(b->runs[j + 1].flags & kMath)) ++j;
            const uint32_t s0 = b->runs[i].start;
            const uint32_t s1 = std::min<uint32_t>(b->runs[j].start + b->runs[j].length, len);
            if (s1 > s0) {
                const float sub_size = st.size * 0.75f;
                IDWriteTextFormat* sf = Format(family, sub_size, st.weight, st.italic);
                ComPtr<IDWriteTextLayout> sl;
                if (sf && SUCCEEDED(dw_->CreateTextLayout(text.c_str() + s0, s1 - s0, sf, kHugeWidth,
                                                          kHugeWidth, &sl))) {
                    sl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                    for (size_t k = i; k <= j; ++k) {
                        const StyleRun& r = b->runs[k];
                        const uint32_t a = std::max(r.start, s0) - s0;
                        const uint32_t e = std::min(r.start + r.length, s1) - s0;
                        if (e <= a) continue;
                        const DWRITE_TEXT_RANGE rg{a, e - a};
                        if (r.flags & kBold) sl->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, rg);
                        if (r.flags & kItalic) sl->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, rg);
                        if (r.flags & (kCode | kKbd)) sl->SetFontFamilyName(mono_family_.c_str(), rg);
                        if (r.link >= 0 && palette_) sl->SetDrawingEffect(palette_->Brush(kColorLink), rg);
                    }
                    const float shift = kind == kSup ? sub_size * 0.5f : -sub_size * 0.25f;
                    const FamilyMetrics& fm = MetricsOf(family);
                    auto* obj = new TextBoxObject(this, std::move(sl), shift, st.size * fm.ascent, st.size * fm.descent);
                    layout->SetInlineObject(obj, DWRITE_TEXT_RANGE{s0, s1 - s0});
                    obj->Release();
                }
            }
            i = j + 1;
        }
    }

    // Images, and flag emoji and formulas over their characters.
    const float line_h = st.size * st.line_height;
    for (const InlineObject& o : b->objects) {
        if (o.pos >= len || o.length > len - o.pos) continue;
        uint32_t run_flags = 0;
        for (const StyleRun& r : b->runs) {
            if (r.start <= o.pos && o.pos < r.start + r.length) {
                run_flags = r.flags;
                break;
            }
        }
        auto run_size = [&] { return RunFontSize(st.size, run_flags); };
        if (o.math >= 0) {
            const std::shared_ptr<const MathBox> box = MathFormula(o.math);
            if (!box) continue;  // no math font: the source stays visible
            // In <sup>/<sub>: smaller, and raised or lowered like the text there.
            float size = run_size(), raise = 0;
            if ((run_flags & (kSup | kSub)) == kSup || (run_flags & (kSup | kSub)) == kSub) {
                size = st.size * 0.75f;
                raise = (run_flags & kSup) ? size * 0.5f : -size * 0.25f;
            }
            float scale = MathSize(family, size) / kMathRefSize;
            // Too wide for the line: shrink a little; beyond that it scrolls.
            if (width < kHugeWidth && width > 0 && box->width * scale > width) {
                scale = std::max(scale * 0.75f, width / box->width);
            }
            const FamilyMetrics& fm = MetricsOf(family);
            auto* obj = new MathObject(this, box, scale, raise, st.size * fm.ascent, st.size * fm.descent, spacing);
            layout->SetInlineObject(obj, DWRITE_TEXT_RANGE{o.pos, o.length});
            obj->Release();
            continue;
        }
        if (o.image < 0) continue;
        if (o.length > 1) {
            const FamilyMetrics& fm = MetricsOf(family);
            auto* obj = new EmojiObject(this, o.image, run_size(), st.size * fm.ascent, st.size * fm.descent);
            layout->SetInlineObject(obj, DWRITE_TEXT_RANGE{o.pos, o.length});
            obj->Release();
            continue;
        }
        if (text[o.pos] != kObjectChar) continue;
        D2D1_SIZE_F sz = D2D1::SizeF(line_h, line_h);
        if (images_) sz = images_->LayoutSize(o.image, width >= kHugeWidth ? 0.f : width, line_h, Zoom());
        auto* obj = new ImageObject(this, o.image, sz.width, sz.height, st.size, spacing,
                                    st.size * MetricsOf(family).descent);
        layout->SetInlineObject(obj, DWRITE_TEXT_RANGE{o.pos, 1});
        obj->Release();
    }
    if (width < kHugeWidth && !st.rtl && HasMath(b)) {
        // Wider than its column even after shrinking (a formula): it starts at
        // the column's start and scrolls, rather than overflowing both sides.
        // (Trailing spaces hang past a wrapped line's end: they don't count.)
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(layout->GetMetrics(&tm)) && tm.width > width + 0.5f) {
            layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }
    return layout;
}

float LayoutEngine::EstimateLeaf(const Block* b, const LeafState& s, float width) {
    const LeafStyle& st = s.style;
    const float line_h = st.size * st.line_height;
    const std::wstring& t = b->text;
    if (b->type == BlockType::CodeBlock) {
        if (st.nowrap) {
            const size_t lines = std::count(t.begin(), t.end(), L'\n') + 1;
            return static_cast<float>(lines) * line_h;
        }
        const float per_line = std::max(1.f, std::floor(width / (st.size * 0.6f)));
        float lines = 0;
        size_t col = 0;
        for (wchar_t c : t) {
            if (c == L'\n') {
                lines += std::max(1.f, std::ceil(col / per_line));
                col = 0;
            } else {
                col += (c == L'\t') ? 4 : 1;
            }
        }
        lines += std::max(1.f, std::ceil(col / per_line));
        return lines * line_h;
    }
    // Formulas count with their typeset size where known: display ones are
    // typeset for it (they change the height most), inline ones not yet
    // typeset are guessed from their source.
    float formulas_w = 0, extra_h = 0, units = 0, breaks = 0;
    size_t next = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        const wchar_t c = t[i];
        if (next < b->objects.size() && b->objects[next].pos == i) {
            const InlineObject& o = b->objects[next++];
            if (o.math >= 0) {
                const bool display = doc_->maths[static_cast<size_t>(o.math)].display;
                if (const auto box = MathFormula(o.math, display)) {
                    const float k = MathSize(st.mono ? mono_family_ : cfg_.font_family, st.size) / kMathRefSize;
                    formulas_w += box->width * k;
                    extra_h += std::max(0.f, (box->ascent + box->descent) * k - line_h);
                } else {
                    formulas_w += 0.3f * st.size * static_cast<float>(o.length);
                }
                i += o.length - 1;
                continue;
            }
        }
        if (c == 0x2028) breaks += 1.f;
        units += (c >= 0x1100) ? 1.f : 0.52f;
    }
    const float total = s.nowrap_w > 0 ? s.nowrap_w : units * st.size + formulas_w;
    const float lines = std::max(1.f, std::ceil(total / std::max(width, 1.f) * 1.04f)) + breaks;
    return lines * line_h + extra_h;
}

float LayoutEngine::MeasureLeaf(Block* b, float width, bool exact) {
    LeafState& s = *State(b);
    if (s.exact && s.width == width) return s.text_h;
    if (!exact) {
        s.exact = false;
        s.width = width;
        s.text_h = EstimateLeaf(b, s, width);
        return s.text_h;
    }
    if (s.layout && b->objects.empty()) {
        s.layout->SetMaxWidth(std::max(width, 1.f));
    } else {
        s.layout = BuildTextLayout(b, s.style, width);
    }
    if (!s.layout) {
        s.exact = true;
        s.width = width;
        s.text_h = s.style.size * s.style.line_height;
        return s.text_h;
    }
    DWRITE_TEXT_METRICS tm{};
    s.layout->GetMetrics(&tm);
    s.overflow_w = width < kHugeWidth && !s.style.rtl && tm.width > width + 0.5f && HasMath(b) ? tm.width : 0.f;
    s.text_h = tm.height;
    s.width = width;
    s.exact = true;
    s.used = frame_;
    if (s.style.nowrap || tm.lineCount <= 1) {
        s.nowrap_w = tm.widthIncludingTrailingWhitespace;
    } else if (s.nowrap_w <= 0) {
        s.nowrap_w = width * (static_cast<float>(tm.lineCount) - 0.5f);
    }
    s.first_baseline = FirstLineBaseline(s.layout.Get());
    return s.text_h;
}

IDWriteTextLayout* LayoutEngine::TextLayout(Block* leaf) {
    LeafState* s = State(leaf);
    if (!s) return nullptr;
    s->used = frame_;
    if (!s->layout) {
        const float w = s->width > 0 ? s->width : 100.f;
        s->layout = BuildTextLayout(leaf, s->style, w);
    } else if (s->layout->GetMaxWidth() != s->width && s->width > 0) {
        s->layout->SetMaxWidth(s->width);
    }
    return s->layout.Get();
}

void LayoutEngine::InvalidateTextLayouts() {
    for (LeafState& s : leaves_) s.layout.Reset();
    labels_.clear();
    numbers_.clear();
}

void LayoutEngine::InvalidateMeasurements() {
    for (LeafState& s : leaves_) {
        s.layout.Reset();
        s.exact = false;
        s.minmax_valid = false;
        s.nowrap_w = 0;
        s.width = -1;
    }
    tables_.clear();
    refine_cursor_ = 0;
}

void LayoutEngine::InvalidateLeaf(Block* leaf) {
    if (LeafState* s = State(leaf)) {
        s->layout.Reset();
        s->exact = false;
        s->minmax_valid = false;
        s->width = -1;
    }
}

void LayoutEngine::EvictFar(float y0, float y1) {
    if (!doc_) return;
    const float margin = std::max(1000.f, (y1 - y0) * 2.f);
    for (size_t i = 0; i < leaves_.size(); ++i) {
        LeafState& s = leaves_[i];
        if (!s.layout) continue;
        const Block* b = doc_->leaves[i];
        if (b->y + b->h < y0 - margin || b->y > y1 + margin) {
            // Table cells keep their layouts (min/max widths are needed).
            if (!s.minmax_valid) s.layout.Reset();
        }
    }
}

float LayoutEngine::FirstBaseline(Block* b) const {
    Block* cur = b;
    while (cur) {
        if (!cur->visible) break;
        if (cur->IsLeaf()) {
            const LeafState& s = leaves_[static_cast<size_t>(cur->leaf_index)];
            if (s.exact) return s.ty + s.first_baseline;
            const float lh = s.style.size * s.style.line_height;
            return s.ty + lh * 0.5f + s.style.size * 0.415f;
        }
        if (cur->children.empty() || cur->type == BlockType::Table || cur->type == BlockType::Rule) break;
        cur = cur->children.front();
    }
    return b->y + m_.em * 1.165f;
}

// ---------------------------------------------------------------------------
// Positioning
// ---------------------------------------------------------------------------

void LayoutEngine::UpdateVisibility(Block* b, bool visible) {
    b->visible = visible;
    if (b->type == BlockType::Details) {
        for (size_t i = 0; i < b->children.size(); ++i) {
            Block* c = b->children[i];
            const bool is_summary = i == 0 && c->type == BlockType::Summary;
            UpdateVisibility(c, visible && (is_summary || b->open));
        }
        return;
    }
    for (Block* c : b->children) UpdateVisibility(c, visible);
}

LayoutEngine::Margins LayoutEngine::BlockMargins(const Block* b) const {
    const float gap = m_.block_gap;
    switch (b->type) {
    case BlockType::Paragraph:
        return b->anonymous ? Margins{} : Margins{0, gap};
    case BlockType::Heading:
        return Margins{m_.em * 1.5f, gap};
    case BlockType::CodeBlock:
    case BlockType::Quote:
    case BlockType::Table:
    case BlockType::Details:
    case BlockType::Alert:
        return Margins{0, gap};
    case BlockType::List:
        if (b->parent && (b->parent->type == BlockType::ListItem || b->parent->type == BlockType::FootnoteDef)) {
            return Margins{};
        }
        return Margins{0, gap};
    case BlockType::Rule:
        return Margins{m_.em * 1.5f, m_.em * 1.5f};
    case BlockType::FootnoteSection:
        return Margins{m_.em * 2.f, 0};
    case BlockType::Div:
        if (b->indent > 0) return Margins{0, gap};
        return Margins{CollapsedMargin(b, true), CollapsedMargin(b, false)};
    default:
        return Margins{};
    }
}

// Margins collapse through a <div> without padding or borders: follow the
// chain of first (last) visible children. Iterative, as nesting is unbounded.
float LayoutEngine::CollapsedMargin(const Block* b, bool top) const {
    while (b->type == BlockType::Div && b->indent <= 0) {
        const Block* next = nullptr;
        if (top) {
            for (const Block* c : b->children) {
                if (c->visible) {
                    next = c;
                    break;
                }
            }
        } else {
            for (auto it = b->children.rbegin(); it != b->children.rend(); ++it) {
                if ((*it)->visible) {
                    next = *it;
                    break;
                }
            }
        }
        if (!next) return 0;
        b = next;
    }
    const Margins m = BlockMargins(b);
    return top ? m.top : m.bottom;
}

void LayoutEngine::Translate(Block* b, float dx, float dy) {
    b->x += dx;
    b->y += dy;
    b->marker_y += dy;
    if (b->IsLeaf()) {
        if (LeafState* s = State(b)) {
            s->tx += dx;
            s->ty += dy;
        }
    }
    for (Block* c : b->children) Translate(c, dx, dy);
}

void LayoutEngine::Layout(float viewport_w, float exact_y0, float exact_y1) {
    if (!doc_) return;
    viewport_w_ = std::max(viewport_w, 1.f);
    exact_y0_ = exact_y0;
    exact_y1_ = exact_y1;
    estimated_count_ = 0;

    const float vw = viewport_w_;
    if (cfg_.page_padding >= 0) {
        m_.page_pad_x = m_.page_pad_top = m_.page_pad_bottom = cfg_.page_padding;
    } else {
        m_.page_pad_x = std::clamp(vw * 0.04f, 8.f, 32.f);
        m_.page_pad_top = std::clamp(vw * 0.03f, 8.f, 24.f);
        m_.page_pad_bottom = m_.page_pad_top + 8.f;
    }
    float cw = vw - 2.f * m_.page_pad_x;
    if (cfg_.max_content_width > 0) cw = std::min(cw, cfg_.max_content_width * cfg_.zoom);
    cw = std::max(cw, 40.f);
    const float x0 = std::max(m_.page_pad_x, (vw - cw) * 0.5f);

    Block* root = doc_->root;
    root->x = x0;
    root->y = m_.page_pad_top;
    root->w = cw;
    const float h = LayoutChildren(root, x0, m_.page_pad_top, cw, Ctx{});
    root->h = h;
    doc_h_ = m_.page_pad_top + h + m_.page_pad_bottom;
    doc_w_ = vw;
}

float LayoutEngine::LayoutChildren(Block* parent, float x, float y, float w, const Ctx& ctx, size_t first) {
    float cursor = y;
    float prev_bottom = 0;
    bool first_visible = true;
    for (size_t i = first; i < parent->children.size(); ++i) {
        Block* c = parent->children[i];
        if (!c->visible) {
            c->x = x;
            c->y = cursor;
            c->w = w;
            c->h = 0;
            continue;
        }
        const Margins m = BlockMargins(c);
        if (!first_visible) cursor += std::max(prev_bottom, m.top);
        cursor += LayoutBlock(c, x, cursor, w, ctx);
        prev_bottom = m.bottom;
        first_visible = false;
    }
    return cursor - y;
}

float LayoutEngine::LayoutBlock(Block* b, float x, float y, float w, const Ctx& in) {
    b->x = x;
    b->y = y;
    b->w = w;
    Ctx ctx = in;
    if (!b->IsLeaf() && b->align != Align::Default && b->type != BlockType::TableCell) ctx.align = b->align;
    float h = 0;
    switch (b->type) {
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::CodeBlock:
    case BlockType::Summary:
        return LayoutLeaf(b, x, y, w, ctx);
    case BlockType::Quote: {
        const float inset = m_.quote_bar + m_.quote_pad;
        Ctx c = ctx;
        c.color = kColorMuted;
        h = LayoutChildren(b, x + inset, y, std::max(w - inset - m_.quote_pad, 20.f), c);
        break;
    }
    case BlockType::List:
        h = LayoutList(b, x, y, w, ctx);
        break;
    case BlockType::ListItem:
    case BlockType::FootnoteDef:
        h = LayoutChildren(b, x, y, w, ctx);
        if (b->children.empty()) h = m_.em * ctx.scale * 1.5f;
        b->marker_y = b->children.empty() ? y + m_.em * ctx.scale * 1.165f : FirstBaseline(b);
        break;
    case BlockType::Table:
        h = LayoutTable(b, x, y, w, ctx);
        break;
    case BlockType::Rule:
        h = m_.rule_h;
        break;
    case BlockType::Alert:
        h = LayoutAlert(b, x, y, w, ctx);
        break;
    case BlockType::Details:
        h = LayoutDetails(b, x, y, w, ctx);
        break;
    case BlockType::Div: {
        const float indent = b->indent * m_.em * ctx.scale;
        h = LayoutChildren(b, x + indent, y, std::max(w - indent, 20.f), ctx);
        break;
    }
    case BlockType::FootnoteSection: {
        Ctx c = ctx;
        c.scale = 0.75f;
        c.color = kColorMuted;
        const float top = 1.f + m_.em * 0.75f;
        const float indent = m_.em;
        float cursor = y + top;
        bool first = true;
        for (Block* def : b->children) {
            if (!first) cursor += m_.li_gap;
            cursor += LayoutBlock(def, x + indent, cursor, std::max(w - indent, 20.f), c);
            first = false;
        }
        h = cursor - y;
        break;
    }
    default:
        h = LayoutChildren(b, x, y, w, ctx);
        break;
    }
    b->h = h;
    return h;
}

float LayoutEngine::LayoutLeaf(Block* b, float x, float y, float w, const Ctx& ctx) {
    LeafState& s = *State(b);
    const LeafStyle st = ResolveStyle(b, ctx);
    if (st.size != s.style.size || st.weight != s.style.weight || st.italic != s.style.italic ||
        st.mono != s.style.mono || st.nowrap != s.style.nowrap || st.align != s.style.align ||
        st.rtl != s.style.rtl || st.line_height != s.style.line_height) {
        s.layout.Reset();
        s.exact = false;
        s.minmax_valid = false;
    }
    s.style = st;

    float pad_l = 0, pad_t = 0, pad_r = 0, pad_b = 0;
    if (b->type == BlockType::CodeBlock) {
        pad_l = pad_t = pad_r = pad_b = m_.code_pad * ctx.scale;
        s.gutter = GutterWidth(b, st);
        pad_l += s.gutter;
    } else if (b->type == BlockType::Heading && b->level <= 2) {
        pad_b = st.size * 0.3f + 1.f;
    } else if (b->type == BlockType::Summary) {
        pad_l = m_.em * ctx.scale * 1.25f;
    }
    const float tw = std::max(w - pad_l - pad_r, 1.f);

    float th;
    if (s.exact && s.width == tw) {
        th = s.text_h;
    } else {
        const float est = EstimateLeaf(b, s, tw);
        const bool is_near = y < exact_y1_ && y + est + pad_t + pad_b > exact_y0_;
        // Images can't be estimated (their sizes vary too much): measured right away.
        const bool images = std::any_of(b->objects.begin(), b->objects.end(),
                                        [](const InlineObject& o) { return o.image >= 0 && o.length == 1; });
        th = MeasureLeaf(b, tw, is_near || images || s.layout != nullptr);
    }
    if (!s.exact) ++estimated_count_;
    // A formula too wide for its column even after shrinking: the leaf
    // scrolls horizontally like code, with room for the scrollbar below.
    const bool wide_text = b->type != BlockType::CodeBlock && s.exact && s.overflow_w > tw + 0.5f;
    if (wide_text) pad_b += std::max(4.f, m_.em * 0.3f) + 6.f;
    s.tx = x + pad_l;
    s.ty = y + pad_t;
    b->h = th + pad_t + pad_b;
    b->content_w = 0;
    if (b->type == BlockType::CodeBlock && st.nowrap && s.exact) {
        const float cw = s.nowrap_w + pad_l + pad_r;
        if (cw > w + 0.5f) b->content_w = cw;
    }
    if (wide_text) b->content_w = s.overflow_w + pad_l + pad_r;
    if (b->content_w <= 0) b->scroll_x = 0;
    else b->scroll_x = std::clamp(b->scroll_x, 0.f, std::max(0.f, b->content_w - w));
    return b->h;
}

float LayoutEngine::LayoutList(Block* b, float x, float y, float w, const Ctx& ctx) {
    Ctx c = ctx;
    c.list_depth++;
    const float indent = m_.list_indent * ctx.scale;
    const float gap = b->tight ? m_.li_gap * ctx.scale : m_.block_gap * ctx.scale;
    // dir="auto": the list direction comes from its first strong character.
    b->rtl = false;
    for (const Block* p = b; p && !p->children.empty();) {
        p = p->children.front();
        if (p->IsLeaf()) {
            b->rtl = DetectRtl(p);
            break;
        }
    }
    const float item_x = b->rtl ? x : x + indent;
    float cursor = y;
    bool first = true;
    for (Block* item : b->children) {
        if (!item->visible) {
            item->y = cursor;
            item->h = 0;
            continue;
        }
        if (!first) cursor += gap;
        cursor += LayoutBlock(item, item_x, cursor, std::max(w - indent, 20.f), c);
        first = false;
    }
    return cursor - y;
}

float LayoutEngine::LayoutAlert(Block* b, float x, float y, float w, const Ctx& ctx) {
    const float bar = m_.quote_bar;
    const float px = m_.em;
    const float py = m_.em * 0.5f;
    const float title_h = m_.em * 1.5f;
    const float gap = m_.em * 0.25f;
    const float inner_x = x + bar + px;
    const float inner_w = std::max(w - bar - 2.f * px, 20.f);
    const float ch = LayoutChildren(b, inner_x, y + py + title_h + gap, inner_w, ctx);
    return py + title_h + (ch > 0 ? gap + ch : 0) + py;
}

float LayoutEngine::LayoutDetails(Block* b, float x, float y, float w, const Ctx& ctx) {
    float h = 0;
    size_t first = 0;
    if (!b->children.empty() && b->children[0]->type == BlockType::Summary) {
        h = LayoutBlock(b->children[0], x, y, w, ctx);
        first = 1;
    }
    if (b->open) {
        const float body = LayoutChildren(b, x, y + h + m_.em * 0.5f, w, ctx, first);
        if (body > 0) h += m_.em * 0.5f + body;
    }
    return h;
}

void LayoutEngine::MinMaxWidth(Block* b, const Ctx& ctx, float& min_w, float& max_w, bool estimate) {
    min_w = max_w = 0;
    if (!b->visible) return;
    if (b->IsLeaf() && estimate) {
        LeafState& s = *State(b);
        if (s.minmax_valid) {
            min_w = s.min_w;
            max_w = s.nowrap_w;
        } else {
            // Rough widths from character counts; no DirectWrite work.
            const LeafStyle st = ResolveStyle(b, ctx);
            const float cw = st.mono ? st.size * 0.6f : st.size * 0.52f;
            float line = 0, word = 0, longest = 0, widest = 0;
            size_t next = 0;  // objects are in text order
            const std::wstring& family = st.mono ? MonoFamily() : cfg_.font_family;
            for (size_t i = 0; i < b->text.size(); ++i) {
                while (next < b->objects.size() && b->objects[next].pos < i) ++next;
                if (next < b->objects.size() && b->objects[next].pos == i && b->objects[next].math >= 0) {
                    // A formula counts with its typeset width, not its source.
                    const auto box = MathFormula(b->objects[next].math);
                    if (box) {
                        const float fw = box->width * MathSize(family, st.size) / kMathRefSize;
                        word += fw;
                        line += fw;
                        i += b->objects[next].length - 1;
                        continue;
                    }
                }
                const wchar_t c = b->text[i];
                const float adv = (c >= 0x1100 && !st.mono) ? st.size : cw;
                if (c == L'\n' || c == 0x2028) {
                    widest = std::max(widest, line);
                    longest = std::max(longest, word);
                    line = word = 0;
                } else if (c == L' ' || c == L'\t') {
                    longest = std::max(longest, word);
                    word = 0;
                    line += adv;
                } else {
                    word += adv;
                    line += adv;
                }
            }
            widest = std::max(widest, line);
            longest = std::max(longest, word);
            for (const InlineObject& o : b->objects) {
                if (images_ && o.image >= 0 && o.length == 1) {  // flag emoji were measured as text
                    const float iw = images_->LayoutSize(o.image, 0, st.size * st.line_height, Zoom()).width;
                    longest = std::max(longest, iw);
                    widest += iw;
                }
            }
            min_w = longest;
            max_w = widest;
        }
        if (b->type == BlockType::CodeBlock) {
            const float gutter = cfg_.line_numbers ? GutterWidth(b, ResolveStyle(b, ctx)) : 0.f;
            min_w = std::min(min_w + 2.f * m_.code_pad + gutter, m_.em * 12.f);
            max_w += 2.f * m_.code_pad + gutter;
        }
        return;
    }
    if (b->IsLeaf()) {
        LeafState& s = *State(b);
        const LeafStyle st = ResolveStyle(b, ctx);
        const bool style_same = st.size == s.style.size && st.weight == s.style.weight &&
                                st.mono == s.style.mono && st.align == s.style.align;
        if (!(s.minmax_valid && style_same)) {
            s.style = st;
            s.layout = BuildTextLayout(b, st, kHugeWidth);
            s.exact = false;
            if (s.layout) {
                DWRITE_TEXT_METRICS tm{};
                s.layout->GetMetrics(&tm);
                s.nowrap_w = tm.widthIncludingTrailingWhitespace;
                FLOAT mw = 0;
                if (st.nowrap) mw = s.nowrap_w;
                else if (FAILED(s.layout->DetermineMinWidth(&mw))) mw = s.nowrap_w;
                s.min_w = std::min(mw, s.nowrap_w);
            }
            s.minmax_valid = true;
        }
        float pad = 0;
        if (b->type == BlockType::CodeBlock) pad = 2.f * m_.code_pad * ctx.scale + GutterWidth(b, st);
        if (b->type == BlockType::Summary) pad = m_.em * ctx.scale * 1.25f;
        // +1: a layout exactly as wide as its text may still wrap (rounding).
        min_w = s.min_w + pad + 1.f;
        max_w = s.nowrap_w + pad + 1.f;
        if (b->type == BlockType::CodeBlock && !s.style.nowrap) min_w = std::min(min_w, m_.em * 12.f);
        return;
    }
    float inset = 0;
    Ctx c = ctx;
    switch (b->type) {
    case BlockType::Quote:
        inset = m_.quote_bar + 2.f * m_.quote_pad;
        c.color = kColorMuted;
        break;
    case BlockType::List:
        inset = m_.list_indent * ctx.scale;
        break;
    case BlockType::Alert:
        inset = m_.quote_bar + 2.f * m_.em;
        break;
    case BlockType::Div:
        inset = b->indent * m_.em;
        break;
    case BlockType::Rule:
        return;
    case BlockType::Table: {
        // Nested table: column widths are the widest cell of each column (as
        // LayoutTable computes them), plus padding and the grid lines.
        std::vector<float> cmin, cmax;
        for (Block* row : b->children) {
            if (row->type != BlockType::TableRow) continue;
            size_t col = 0;
            for (Block* cell : row->children) {
                Ctx cc = c;
                cc.header_cell = cell->header || row->header;
                cc.align = cell->align;
                float mn, mx;
                MinMaxWidth(cell, cc, mn, mx, estimate);
                const size_t span = static_cast<size_t>(std::max(1, cell->colspan));
                if (cmin.size() < col + span) {
                    cmin.resize(col + span, 0.f);
                    cmax.resize(col + span, 0.f);
                }
                for (size_t k = col; k < col + span; ++k) {
                    cmin[k] = std::max(cmin[k], mn / static_cast<float>(span));
                    cmax[k] = std::max(cmax[k], mx / static_cast<float>(span));
                }
                col += span;
            }
        }
        const float per_col = 2.f * m_.cell_pad_x * ctx.scale + 1.f;
        for (size_t k = 0; k < cmin.size(); ++k) {
            min_w += cmin[k] + per_col;
            max_w += cmax[k] + per_col;
        }
        if (!cmin.empty()) {
            min_w += 1.f;
            max_w += 1.f;
        }
        return;
    }
    default:
        break;
    }
    for (Block* child : b->children) {
        float cmin, cmax;
        MinMaxWidth(child, c, cmin, cmax, estimate);
        min_w = std::max(min_w, cmin);
        max_w = std::max(max_w, cmax);
    }
    min_w += inset;
    max_w += inset;
}

float LayoutEngine::LayoutTable(Block* b, float x, float y, float w, const Ctx& ctx) {
    TableInfo& ti = tables_[b];
    float cursor = y;
    std::vector<Block*> rows;
    for (Block* c : b->children) {
        if (c->type == BlockType::TableRow) {
            rows.push_back(c);
        } else if (c->visible) {
            cursor += LayoutBlock(c, x, cursor, w, ctx);
        }
    }
    if (rows.empty()) {
        ti.col_x.clear();
        ti.width = 0;
        return cursor - y;
    }
    const float border = 1.f;
    const float padx = m_.cell_pad_x * ctx.scale;
    const float pady = m_.cell_pad_y * ctx.scale;
    // Tables far from the viewport get estimated column widths (no text
    // layouts); they are measured exactly once they come near the view.
    const float guess = static_cast<float>(rows.size()) * (m_.em * ctx.scale * 1.5f + 2.f * pady + border) * 1.5f;
    const bool estimate = !(cursor < exact_y1_ && cursor + guess > exact_y0_);

    size_t ncols = 0;
    for (Block* row : rows) {
        size_t n = 0;
        for (Block* cell : row->children) n += static_cast<size_t>(std::max(1, cell->colspan));
        ncols = std::max(ncols, n);
    }
    ncols = std::max<size_t>(ncols, 1);
    std::vector<float> cmin(ncols, 0.f), cmax(ncols, 0.f);
    struct Span {
        size_t col, span;
        float mn, mx;
    };
    std::vector<Span> spans;
    for (Block* row : rows) {
        size_t col = 0;
        for (Block* cell : row->children) {
            Ctx c = ctx;
            c.header_cell = cell->header || row->header;
            c.align = cell->align;
            float mn, mx;
            MinMaxWidth(cell, c, mn, mx, estimate);
            mn += 2.f * padx;
            mx += 2.f * padx;
            const size_t span = std::min<size_t>(static_cast<size_t>(std::max(1, cell->colspan)), ncols - col);
            if (span <= 1) {
                cmin[col] = std::max(cmin[col], mn);
                cmax[col] = std::max(cmax[col], mx);
            } else {
                spans.push_back({col, span, mn, mx});
            }
            col += span;
            if (col >= ncols) break;
        }
    }
    for (const Span& s : spans) {
        float smin = 0, smax = 0;
        for (size_t k = s.col; k < s.col + s.span; ++k) {
            smin += cmin[k];
            smax += cmax[k];
        }
        const float inner = static_cast<float>(s.span - 1) * border;
        if (s.mn > smin + inner) {
            const float add = (s.mn - smin - inner) / static_cast<float>(s.span);
            for (size_t k = s.col; k < s.col + s.span; ++k) cmin[k] += add;
        }
        if (s.mx > smax + inner) {
            const float add = (s.mx - smax - inner) / static_cast<float>(s.span);
            for (size_t k = s.col; k < s.col + s.span; ++k) cmax[k] += add;
        }
    }
    for (size_t k = 0; k < ncols; ++k) {
        cmin[k] = std::max(cmin[k], 2.f * padx + 4.f);
        cmax[k] = std::max(cmax[k], cmin[k]);
    }
    const float avail = w - static_cast<float>(ncols + 1) * border;
    float summin = 0, summax = 0;
    for (size_t k = 0; k < ncols; ++k) {
        summin += cmin[k];
        summax += cmax[k];
    }
    std::vector<float> width(ncols);
    if (summax <= avail) {
        width = cmax;
    } else if (summin < avail && summax > summin) {
        const float t = (avail - summin) / (summax - summin);
        for (size_t k = 0; k < ncols; ++k) width[k] = cmin[k] + (cmax[k] - cmin[k]) * t;
    } else {
        width = cmin;
    }
    ti.col_x.assign(ncols + 1, 0.f);
    ti.col_x[0] = border;
    for (size_t k = 0; k < ncols; ++k) ti.col_x[k + 1] = ti.col_x[k] + width[k] + border;
    const float tw = ti.col_x[ncols];
    ti.width = tw;

    float tx = x;
    const Align align = b->align != Align::Default ? b->align : ctx.align;
    if (tw < w) {
        if (align == Align::Center) tx = x + (w - tw) * 0.5f;
        else if (align == Align::Right) tx = x + (w - tw);
    }
    b->content_w = tw > w + 0.5f ? tw : 0.f;
    if (b->content_w <= 0) b->scroll_x = 0;
    else b->scroll_x = std::clamp(b->scroll_x, 0.f, std::max(0.f, b->content_w - w));

    float ry = cursor + border;
    const float table_top = cursor;
    for (Block* row : rows) {
        row->x = tx;
        row->y = ry;
        row->w = tw;
        float row_h = 0;
        size_t col = 0;
        std::vector<std::pair<Block*, float>> cells;
        for (Block* cell : row->children) {
            if (col >= ncols) {
                cell->visible = false;
                continue;
            }
            const size_t span = std::min<size_t>(static_cast<size_t>(std::max(1, cell->colspan)), ncols - col);
            const float cx = tx + ti.col_x[col];
            const float cw = ti.col_x[col + span] - border - ti.col_x[col];
            Ctx c = ctx;
            c.header_cell = cell->header || row->header;
            c.align = cell->align;
            cell->x = cx;
            cell->y = ry;
            cell->w = cw;
            const float content = LayoutChildren(cell, cx + padx, ry + pady, std::max(cw - 2.f * padx, 1.f), c);
            const float ch = content + 2.f * pady;
            row_h = std::max(row_h, ch);
            cells.emplace_back(cell, ch);
            col += span;
        }
        row_h = std::max(row_h, m_.em * ctx.scale * 1.5f + 2.f * pady);
        for (auto& [cell, ch] : cells) {
            // Vertically centered unless valign says otherwise (HTML tables).
            const float extra = row_h - ch;
            const float shift = cell->valign == 1 ? 0.f : cell->valign == 2 ? extra : extra * 0.5f;
            if (shift > 0.5f) {
                for (Block* child : cell->children) Translate(child, 0, shift);
            }
            cell->h = row_h;
        }
        row->h = row_h;
        ry += row_h + border;
    }
    (void)table_top;
    // Room for the horizontal scrollbar under an over-wide table.
    if (b->content_w > 0) ry += std::max(4.f, m_.em * 0.3f) + 6.f;
    return ry - y;
}

bool LayoutEngine::Refine(double budget_ms) {
    if (!doc_ || estimated_count_ == 0) return false;
    const double t0 = NowMs();
    bool changed = false;
    const size_t n = doc_->leaves.size();
    size_t visited = 0;
    while (visited < n) {
        const size_t i = refine_cursor_ % n;
        ++refine_cursor_;
        ++visited;
        LeafState& s = leaves_[i];
        Block* b = doc_->leaves[i];
        if (!b->visible || s.exact || s.width <= 0) continue;
        MeasureLeaf(b, s.width, true);
        const bool is_near = b->y < exact_y1_ + 2000.f && b->y + b->h > exact_y0_ - 2000.f;
        if (!is_near) s.layout.Reset();
        changed = true;
        if ((visited & 7) == 0 && NowMs() - t0 > budget_ms) break;
    }
    return changed;
}

const TableInfo* LayoutEngine::Table(const Block* table) const {
    auto it = tables_.find(table);
    return it == tables_.end() ? nullptr : &it->second;
}

// Line numbers of multi-line code blocks: the widest number plus a gap.
float LayoutEngine::GutterWidth(const Block* b, const LeafStyle& st) {
    if (!cfg_.line_numbers || b->type != BlockType::CodeBlock) return 0.f;
    const int lines = 1 + static_cast<int>(std::count(b->text.begin(), b->text.end(), L'\n'));
    if (lines < 2) return 0.f;
    float width = 0;
    if (IDWriteTextLayout* tl = LineNumberLayout(lines, st.size, nullptr)) {
        DWRITE_TEXT_METRICS tm{};
        tl->GetMetrics(&tm);
        width = tm.widthIncludingTrailingWhitespace;
    }
    return width + GutterGap(st.size);
}

IDWriteTextLayout* LayoutEngine::LineNumberLayout(int number, float size, float* baseline) {
    const uint64_t key = (static_cast<uint64_t>(size * 100.f + 0.5f) << 32) | static_cast<uint32_t>(number);
    auto it = numbers_.find(key);
    if (it == numbers_.end()) {
        if (numbers_.size() >= 4096) numbers_.clear();
        IDWriteTextFormat* fmt = Format(mono_family_, size, 400, false);
        if (!fmt) return nullptr;
        const std::wstring text = std::to_wstring(number);
        NumberLabel label;
        if (FAILED(dw_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt, kHugeWidth,
                                         kHugeWidth, &label.layout))) {
            return nullptr;
        }
        label.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        label.baseline = FirstLineBaseline(label.layout.Get());
        it = numbers_.emplace(key, std::move(label)).first;
    }
    if (baseline) *baseline = it->second.baseline;
    return it->second.layout.Get();
}

IDWriteTextLayout* LayoutEngine::LabelLayout(const std::wstring& text, float size, uint16_t weight) {
    const std::wstring key = text + L"\x1F" + std::to_wstring(static_cast<int>(size * 100)) + L"\x1F" +
                             std::to_wstring(weight);
    auto it = labels_.find(key);
    if (it != labels_.end()) return it->second.Get();
    // Distinct labels (list numbers, status texts) are unbounded: start over
    // now and then. Callers use the result right away, before the next call.
    if (labels_.size() >= 2048) labels_.clear();
    IDWriteTextFormat* fmt = Format(cfg_.font_family, size, weight, false);
    if (!fmt) return nullptr;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), fmt, kHugeWidth,
                                     kHugeWidth, &layout))) {
        return nullptr;
    }
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    IDWriteTextLayout* raw = layout.Get();
    labels_.emplace(key, std::move(layout));
    return raw;
}

}  // namespace mdv
