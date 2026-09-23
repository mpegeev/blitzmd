#include "core/mathlayout.h"

#include <dwrite_2.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/texmath.h"

namespace mdv {
namespace {

// MathConstants, in table order (OpenType MATH).
enum Constant : int {
    kScriptPercentScaleDown, kScriptScriptPercentScaleDown, kDelimitedSubFormulaMinHeight,
    kDisplayOperatorMinHeight, kMathLeading, kAxisHeight, kAccentBaseHeight, kFlattenedAccentBaseHeight,
    kSubscriptShiftDown, kSubscriptTopMax, kSubscriptBaselineDropMin, kSuperscriptShiftUp,
    kSuperscriptShiftUpCramped, kSuperscriptBottomMin, kSuperscriptBaselineDropMax, kSubSuperscriptGapMin,
    kSuperscriptBottomMaxWithSubscript, kSpaceAfterScript, kUpperLimitGapMin, kUpperLimitBaselineRiseMin,
    kLowerLimitGapMin, kLowerLimitBaselineDropMin, kStackTopShiftUp, kStackTopDisplayStyleShiftUp,
    kStackBottomShiftDown, kStackBottomDisplayStyleShiftDown, kStackGapMin, kStackDisplayStyleGapMin,
    kStretchStackTopShiftUp, kStretchStackBottomShiftDown, kStretchStackGapAboveMin, kStretchStackGapBelowMin,
    kFractionNumeratorShiftUp, kFractionNumeratorDisplayStyleShiftUp, kFractionDenominatorShiftDown,
    kFractionDenominatorDisplayStyleShiftDown, kFractionNumeratorGapMin, kFractionNumDisplayStyleGapMin,
    kFractionRuleThickness, kFractionDenominatorGapMin, kFractionDenomDisplayStyleGapMin,
    kSkewedFractionHorizontalGap, kSkewedFractionVerticalGap, kOverbarVerticalGap, kOverbarRuleThickness,
    kOverbarExtraAscender, kUnderbarVerticalGap, kUnderbarRuleThickness, kUnderbarExtraDescender,
    kRadicalVerticalGap, kRadicalDisplayStyleVerticalGap, kRadicalRuleThickness, kRadicalExtraAscender,
    kRadicalKernBeforeDegree, kRadicalKernAfterDegree, kRadicalDegreeBottomRaisePercent, kConstantCount
};

constexpr uint32_t kErrorColor = 0xFFD1242F;  // unknown commands (MathJax shows them red)
constexpr int kMaxAssemblyGlyphs = 1000;      // parts of one stretched glyph
constexpr int kFormulaAssemblyGlyphs = 4000;  // and of all of a formula's
constexpr int kMaxShapedTexts = 64;           // DirectWrite layouts of a formula (emoji, complex scripts)
constexpr float kFarDips = 1e7f;              // > any 20000-character formula at the reference size

// Big-endian reads with bounds checks (0 outside the table).
struct Table {
    std::vector<uint8_t> bytes;
    uint16_t U16(size_t off) const {
        return off + 2 <= bytes.size() ? static_cast<uint16_t>((bytes[off] << 8) | bytes[off + 1]) : 0;
    }
    int16_t S16(size_t off) const { return static_cast<int16_t>(U16(off)); }

    // Index of `glyph` in a Coverage table, -1 if absent.
    int Coverage(size_t off, uint16_t glyph) const {
        if (!off) return -1;
        const uint16_t format = U16(off), count = U16(off + 2);
        size_t lo = 0, hi = count;
        while (lo < hi) {
            const size_t mid = (lo + hi) / 2;
            if (format == 1) {
                const uint16_t g = U16(off + 4 + mid * 2);
                if (g == glyph) return static_cast<int>(mid);
                if (g < glyph) lo = mid + 1;
                else hi = mid;
            } else if (format == 2) {
                const size_t r = off + 4 + mid * 6;
                const uint16_t start = U16(r), end = U16(r + 2);
                if (glyph < start) hi = mid;
                else if (glyph > end) lo = mid + 1;
                else return U16(r + 4) + (glyph - start);
            } else {
                return -1;
            }
        }
        return -1;
    }
};

struct GlyphMetrics {
    float advance = 0, xmin = 0, xmax = 0, ymin = 0, ymax = 0;  // design units, y up
};

struct Part {
    uint16_t glyph;
    int start, end, full;
    bool extender;
};

// A text analysis source for one short string (font fallback lookups).
class OneString final : public IDWriteTextAnalysisSource {
public:
    explicit OneString(std::wstring text) : text_(std::move(text)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextAnalysisSource)) {
            *ppv = static_cast<IDWriteTextAnalysisSource*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }   // lives on the stack
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 pos, const WCHAR** text, UINT32* len) override {
        *text = pos < text_.size() ? text_.data() + pos : nullptr;
        *len = pos < text_.size() ? static_cast<UINT32>(text_.size() - pos) : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 pos, const WCHAR** text, UINT32* len) override {
        *text = pos > 0 && pos <= text_.size() ? text_.data() : nullptr;
        *len = pos > 0 && pos <= text_.size() ? pos : 0;
        return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 pos, UINT32* len, const WCHAR** locale) override {
        *locale = L"";
        *len = pos < text_.size() ? static_cast<UINT32>(text_.size() - pos) : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 pos, UINT32* len,
                                                    IDWriteNumberSubstitution** sub) override {
        *sub = nullptr;
        *len = pos < text_.size() ? static_cast<UINT32>(text_.size() - pos) : 0;
        return S_OK;
    }

private:
    std::wstring text_;
};

// A box under construction: glyphs and rules around the origin (left end
// of the baseline, y down), with TeX's width, height and depth.
struct Box {
    float w = 0, h = 0, d = 0;
    std::vector<MathBox::Glyph> glyphs;
    std::vector<MathBox::Rule> rules;
    std::vector<MathBox::Text> texts;

    // Puts `b` with its origin at (dx, dy) and grows to include it.
    void Add(const Box& b, float dx, float dy) {
        for (MathBox::Glyph g : b.glyphs) {
            g.x += dx;
            g.y += dy;
            glyphs.push_back(g);
        }
        for (MathBox::Rule r : b.rules) {
            r.x += dx;
            r.y += dy;
            rules.push_back(r);
        }
        for (MathBox::Text t : b.texts) {
            t.x += dx;
            t.y += dy;
            texts.push_back(std::move(t));
        }
        h = std::max(h, b.h - dy);
        d = std::max(d, b.d + dy);
    }
    void Shift(float dy) {  // moves the contents down
        for (auto& g : glyphs) g.y += dy;
        for (auto& r : rules) r.y += dy;
        for (auto& t : texts) t.y += dy;
        h -= dy;
        d += dy;
    }
};

struct Atom {
    Box box;
    MathClass cls = MathClass::Ord;
    float italic = 0;           // italic correction (in box.w unless ink_right is set)
    float ink_right = -1;       // big operator glyph: right edge of its ink
    bool is_char = false;       // one glyph of the math font (TeX's character nucleus)
    bool op = false;            // big operator or operator name
    uint8_t limits = 2;         // see MathNode::limits
    uint16_t glyph = 0;         // is_char: the glyph (accent attachment)
    float size = 0;             // is_char: its em size
};

// Display, text, script, scriptscript; cramped styles keep superscripts low.
struct Ctx {
    int style = 1;
    bool cramped = false;
    uint32_t color = 0;
};

Ctx ScriptCtx(Ctx c, bool cramped) {
    c.style = c.style <= 1 ? 2 : 3;
    c.cramped = c.cramped || cramped;
    return c;
}

Ctx FracCtx(Ctx c, bool denominator) {
    c.style = std::min(c.style + 1, 3);
    c.cramped = c.cramped || denominator;
    return c;
}

// TeX's spacing between atom classes (0 none, 1 thin, 2 medium, 3 thick);
// negative values only apply in display and text styles.
constexpr int8_t kSpacing[8][8] = {
    //         Ord  Op  Bin  Rel Open Close Punct Inner
    /* Ord */ {0, 1, -2, -3, 0, 0, 0, -1},
    /* Op  */ {1, 1, 0, -3, 0, 0, 0, -1},
    /* Bin */ {-2, -2, 0, 0, -2, 0, 0, -2},
    /* Rel */ {-3, -3, 0, 0, -3, 0, 0, -3},
    /* Open*/ {0, 0, 0, 0, 0, 0, 0, 0},
    /* Close*/ {0, 1, -2, -3, 0, 0, 0, -1},
    /* Punct*/ {-1, -1, 0, -1, -1, -1, -1, -1},
    /* Inner*/ {-1, 1, -2, -3, -1, 0, -1, -1},
};

bool IsCombining(uint32_t c) { return (c >= 0x0300 && c <= 0x036F) || (c >= 0x20D0 && c <= 0x20FF); }

bool IsPrime(const MathNode& k) {
    return k.kind == MathKind::Glyphs && !k.text.empty() && k.text[0] >= 0x2032 && k.text[0] <= 0x2034;
}

// Text in scripts that need shaping (joining, reordering, marks) or color
// (emoji): laid out with DirectWrite instead of glyph by glyph.
bool NeedsShaping(std::wstring_view s) {
    for (size_t k = 0; k < s.size(); ++k) {
        const wchar_t c = s[k];
        if ((c >= 0x0300 && c <= 0x036F) || (c >= 0x0590 && c <= 0x1FFF) || c == 0x200C || c == 0x200D ||
            (c >= 0xA800 && c <= 0xABFF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE00 && c <= 0xFE0F) ||
            (c >= 0xFE70 && c <= 0xFEFF)) {
            return true;
        }
        if (c >= 0xD83C && c <= 0xD83E) return true;  // U+1F000..U+1FBFF: emoji
    }
    return false;
}

}  // namespace

struct MathTypesetter::Impl {
    ComPtr<IDWriteFactory> dw;
    ComPtr<IDWriteFontFallback> fallback;
    ComPtr<IDWriteFontCollection> system;
    std::vector<ComPtr<IDWriteFontFace>> faces;  // 0: the math font
    std::vector<float> upem;
    Table math;
    int c[kConstantCount] = {};
    size_t italic_cov = 0, italic_values = 0, accent_cov = 0, accent_values = 0;
    size_t variants = 0, vert_cov = 0, horiz_cov = 0, vert_offsets = 0, horiz_offsets = 0;
    uint16_t vert_count = 0, horiz_count = 0, min_overlap = 0;
    float x_height = 0.466f;
    std::unordered_map<uint32_t, std::pair<uint8_t, uint16_t>> cmap;
    std::unordered_map<uint32_t, GlyphMetrics> metrics;

    bool Load(IDWriteFactory* factory);
    std::pair<uint8_t, uint16_t> Lookup(uint32_t cp);
    const GlyphMetrics& Metrics(uint8_t face, uint16_t glyph);
    float Italic(uint16_t glyph) const;   // design units
    bool TopAccent(uint16_t glyph, float* x) const;
    size_t Construction(uint16_t glyph, bool vertical) const;
};

bool MathTypesetter::Impl::Load(IDWriteFactory* factory) {
    dw = factory;
    UINT32 index = 0;
    BOOL exists = FALSE;
    ComPtr<IDWriteFontFamily> family;
    ComPtr<IDWriteFont> font;
    ComPtr<IDWriteFontFace> face;
    if (FAILED(dw->GetSystemFontCollection(&system)) || FAILED(system->FindFamilyName(L"Cambria Math", &index, &exists)) ||
        !exists || FAILED(system->GetFontFamily(index, &family)) ||
        FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, &font)) ||
        FAILED(font->CreateFontFace(&face))) {
        return false;
    }
    const void* data = nullptr;
    UINT32 size = 0;
    void* context = nullptr;
    exists = FALSE;
    if (FAILED(face->TryGetFontTable(DWRITE_MAKE_OPENTYPE_TAG('M', 'A', 'T', 'H'), &data, &size, &context, &exists)) ||
        !exists) {
        return false;
    }
    math.bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    face->ReleaseFontTable(context);

    DWRITE_FONT_METRICS fm{};
    face->GetMetrics(&fm);
    if (fm.designUnitsPerEm == 0) return false;
    faces.push_back(face);
    upem.push_back(fm.designUnitsPerEm);
    if (fm.xHeight > 0) x_height = static_cast<float>(fm.xHeight) / fm.designUnitsPerEm;

    const Table& t = math;
    const size_t constants = t.U16(4), glyph_info = t.U16(6);
    variants = t.U16(8);
    if (!constants || !variants) return false;
    c[0] = t.S16(constants);
    c[1] = t.S16(constants + 2);
    c[2] = t.U16(constants + 4);
    c[3] = t.U16(constants + 6);
    for (int k = 0; k < 51; ++k) c[4 + k] = t.S16(constants + 8 + static_cast<size_t>(k) * 4);
    c[kRadicalDegreeBottomRaisePercent] = t.S16(constants + 212);
    if (c[kScriptPercentScaleDown] <= 0) c[kScriptPercentScaleDown] = 70;
    if (c[kScriptScriptPercentScaleDown] <= 0) c[kScriptScriptPercentScaleDown] = 50;
    if (glyph_info) {
        if (const size_t italic = t.U16(glyph_info)) {
            italic_cov = glyph_info + italic + t.U16(glyph_info + italic);
            italic_values = glyph_info + italic + 4;
        }
        if (const size_t accent = t.U16(glyph_info + 2)) {
            accent_cov = glyph_info + accent + t.U16(glyph_info + accent);
            accent_values = glyph_info + accent + 4;
        }
    }
    min_overlap = t.U16(variants);
    vert_cov = t.U16(variants + 2) ? variants + t.U16(variants + 2) : 0;
    horiz_cov = t.U16(variants + 4) ? variants + t.U16(variants + 4) : 0;
    vert_count = t.U16(variants + 6);
    horiz_count = t.U16(variants + 8);
    vert_offsets = variants + 10;
    horiz_offsets = vert_offsets + static_cast<size_t>(vert_count) * 2;

    ComPtr<IDWriteFactory2> dw2;
    if (SUCCEEDED(dw.As(&dw2))) dw2->GetSystemFontFallback(&fallback);
    return true;
}

std::pair<uint8_t, uint16_t> MathTypesetter::Impl::Lookup(uint32_t cp) {
    auto it = cmap.find(cp);
    if (it != cmap.end()) return it->second;
    std::pair<uint8_t, uint16_t> r{uint8_t{0}, uint16_t{0}};
    UINT16 glyph = 0;
    faces[0]->GetGlyphIndices(&cp, 1, &glyph);
    if (glyph) {
        r = {uint8_t{0}, glyph};
    } else if (fallback) {
        // Not in the math font (CJK in \text, emoji...): the system fallback.
        std::wstring s;
        AppendCodepoint(s, cp);
        OneString source(s);
        UINT32 mapped = 0;
        ComPtr<IDWriteFont> font;
        FLOAT scale = 1;
        if (SUCCEEDED(fallback->MapCharacters(&source, 0, static_cast<UINT32>(s.size()), system.Get(), L"Cambria Math",
                                              DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, &mapped, &font, &scale)) &&
            font) {
            ComPtr<IDWriteFontFace> face;
            if (SUCCEEDED(font->CreateFontFace(&face))) {
                face->GetGlyphIndices(&cp, 1, &glyph);
                if (glyph) {
                    size_t k = 0;
                    while (k < faces.size() && faces[k].Get() != face.Get()) ++k;
                    if (k == faces.size() && k < 250) {
                        DWRITE_FONT_METRICS fm{};
                        face->GetMetrics(&fm);
                        faces.push_back(face);
                        upem.push_back(fm.designUnitsPerEm ? fm.designUnitsPerEm : 2048.f);
                    }
                    if (k < faces.size()) r = {static_cast<uint8_t>(k), glyph};
                }
            }
        }
    }
    cmap.emplace(cp, r);
    return r;
}

const GlyphMetrics& MathTypesetter::Impl::Metrics(uint8_t face, uint16_t glyph) {
    const uint32_t key = (static_cast<uint32_t>(face) << 16) | glyph;
    auto it = metrics.find(key);
    if (it != metrics.end()) return it->second;
    GlyphMetrics m;
    DWRITE_GLYPH_METRICS gm{};
    if (SUCCEEDED(faces[face]->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE))) {
        m.advance = static_cast<float>(gm.advanceWidth);
        m.xmin = static_cast<float>(gm.leftSideBearing);
        m.xmax = static_cast<float>(static_cast<int>(gm.advanceWidth) - gm.rightSideBearing);
        m.ymax = static_cast<float>(gm.verticalOriginY - gm.topSideBearing);
        m.ymin = static_cast<float>(gm.verticalOriginY - static_cast<int>(gm.advanceHeight) + gm.bottomSideBearing);
        if (m.xmax < m.xmin || m.ymax < m.ymin) m.xmin = m.xmax = m.ymin = m.ymax = 0;  // blank glyph
    }
    return metrics.emplace(key, m).first->second;
}

float MathTypesetter::Impl::Italic(uint16_t glyph) const {
    const int k = math.Coverage(italic_cov, glyph);
    return k < 0 ? 0.f : static_cast<float>(math.S16(italic_values + static_cast<size_t>(k) * 4));
}

bool MathTypesetter::Impl::TopAccent(uint16_t glyph, float* x) const {
    const int k = math.Coverage(accent_cov, glyph);
    if (k < 0) return false;
    *x = static_cast<float>(math.S16(accent_values + static_cast<size_t>(k) * 4));
    return true;
}

size_t MathTypesetter::Impl::Construction(uint16_t glyph, bool vertical) const {
    const int k = math.Coverage(vertical ? vert_cov : horiz_cov, glyph);
    if (k < 0 || k >= (vertical ? vert_count : horiz_count)) return 0;
    const size_t off = math.U16((vertical ? vert_offsets : horiz_offsets) + static_cast<size_t>(k) * 2);
    return off ? variants + off : 0;
}

namespace {

// Lays out one formula.
class Typesetter {
public:
    Typesetter(MathTypesetter::Impl& f, float size) : f_(f), size_(size) {}

    Atom Run(const MathNode& root, bool display) {
        Ctx ctx;
        ctx.style = display ? 0 : 1;
        return List(root, ctx);
    }

private:
    float Size(const Ctx& c) const {
        if (c.style == 2) return size_ * f_.c[kScriptPercentScaleDown] / 100.f;
        if (c.style == 3) return size_ * f_.c[kScriptScriptPercentScaleDown] / 100.f;
        return size_;
    }
    // A MATH constant at the style's size, in DIPs.
    float C(Constant k, const Ctx& c) const { return f_.c[k] * Size(c) / f_.upem[0]; }
    float Mu(const Ctx& c) const { return Size(c) / 18.f; }

    Box GlyphBox(uint8_t face, uint16_t glyph, float size, uint32_t color) {
        const GlyphMetrics& m = f_.Metrics(face, glyph);
        const float s = size / f_.upem[face];
        Box b;
        b.w = m.advance * s;
        b.h = std::max(0.f, m.ymax * s);
        b.d = std::max(0.f, -m.ymin * s);
        MathBox::Glyph g;
        g.size = size;
        g.index = glyph;
        g.face = face;
        g.color = color;
        b.glyphs.push_back(g);
        return b;
    }

    // A delimiter or other stretchy glyph at least `target` DIPs long.
    Box Stretch(uint32_t cp, float target, bool vertical, float size, uint32_t color) {
        const auto [face, glyph] = f_.Lookup(cp);
        if (face != 0 || !glyph) return GlyphBox(face, glyph, size, color);
        const size_t con = f_.Construction(glyph, vertical);
        if (!con) return GlyphBox(face, glyph, size, color);
        const Table& t = f_.math;
        const float s = size / f_.upem[0];
        const uint16_t count = t.U16(con + 2);
        uint16_t best = glyph;
        for (uint16_t k = 0; k < count; ++k) {
            const uint16_t v = t.U16(con + 4 + static_cast<size_t>(k) * 4);
            best = v;
            if (t.U16(con + 6 + static_cast<size_t>(k) * 4) * s >= target) return GlyphBox(0, v, size, color);
        }
        const size_t assembly = t.U16(con) ? con + t.U16(con) : 0;
        if (!assembly) return GlyphBox(0, best, size, color);
        std::vector<Part> parts;
        const uint16_t n = std::min<uint16_t>(t.U16(assembly + 4), 16);
        for (uint16_t k = 0; k < n; ++k) {
            const size_t p = assembly + 6 + static_cast<size_t>(k) * 10;
            parts.push_back(Part{t.U16(p), t.U16(p + 2), t.U16(p + 4), t.U16(p + 6), (t.U16(p + 8) & 1) != 0});
        }
        return Assemble(parts, target / s, vertical, size, color);
    }

    // Glyph assembly: parts (bottom to top / left to right) with extenders
    // repeated until `target` (design units) is reached, overlapping at the
    // connectors.
    Box Assemble(const std::vector<Part>& parts, float target, bool vertical, float size, uint32_t color) {
        const float overlap = f_.min_overlap;
        float fixed = 0, ext = 0;
        int n_fixed = 0, n_ext = 0;
        for (const Part& p : parts) {
            (p.extender ? ext : fixed) += static_cast<float>(p.full);
            (p.extender ? n_ext : n_fixed)++;
        }
        int reps = 0;
        auto length = [&](int r) { return fixed + r * ext - (n_fixed + r * n_ext - 1) * overlap; };
        if (n_ext > 0 && length(0) < target) {
            const float per = ext - n_ext * overlap;  // what one more round of extenders adds
            if (per > 0) reps = static_cast<int>(std::min(std::ceil((target - length(0)) / per), 1e6f));
            const int budget = std::min(kMaxAssemblyGlyphs, assembly_budget_);
            reps = std::clamp(reps, 0, std::max(0, (budget - n_fixed) / n_ext));
        }
        assembly_budget_ = std::max(0, assembly_budget_ - (n_fixed + reps * n_ext));
        std::vector<Part> seq;
        for (const Part& p : parts) {
            if (!p.extender) seq.push_back(p);
            else for (int r = 0; r < reps; ++r) seq.push_back(p);
        }
        if (seq.empty()) return Box{};
        const size_t joints = seq.size() - 1;
        const float extra = joints ? std::max(0.f, length(reps) - target) / static_cast<float>(joints) : 0.f;
        const float s = size / f_.upem[0];
        Box b;
        float pos = 0;
        if (++group_ == 0) group_ = 1;
        for (size_t k = 0; k < seq.size(); ++k) {
            const GlyphMetrics& m = f_.Metrics(0, seq[k].glyph);
            MathBox::Glyph g;
            g.size = size;
            g.index = seq[k].glyph;
            g.group = group_;  // overlapping parts are filled as one shape
            g.color = color;
            if (vertical) {
                // Parts stack by their ink; y is measured up from the bottom.
                g.y = -(pos - m.ymin) * s;
                b.w = std::max(b.w, m.advance * s);
            } else {
                g.x = pos * s;
                b.h = std::max(b.h, m.ymax * s);
                b.d = std::max(b.d, -m.ymin * s);
            }
            b.glyphs.push_back(g);
            float step = static_cast<float>(seq[k].full);
            if (k < joints) {
                const float max_overlap = std::max(overlap, static_cast<float>(std::min(seq[k].end, seq[k + 1].start)));
                step -= std::min(overlap + extra, max_overlap);
            }
            pos += step;
        }
        if (vertical) b.h = pos * s;
        else b.w = pos * s;
        return b;
    }

    // Ink extent of a box's glyphs: top and bottom (y down, from its baseline).
    void Ink(const Box& b, float* top, float* bottom) {
        *top = 0;
        *bottom = 0;
        bool any = false;
        for (const MathBox::Glyph& g : b.glyphs) {
            const GlyphMetrics& m = f_.Metrics(g.face, g.index);
            const float s = g.size / f_.upem[g.face];
            const float t = g.y - m.ymax * s, btm = g.y - m.ymin * s;
            *top = any ? std::min(*top, t) : t;
            *bottom = any ? std::max(*bottom, btm) : btm;
            any = true;
        }
    }

    // Centers a box on the math axis (delimiters, big operators, arrays).
    void CenterOnAxis(Box& b, const Ctx& ctx) { b.Shift((b.h - b.d) / 2 - C(kAxisHeight, ctx)); }

    // A delimiter covering height h and depth d around the axis (\left, \right).
    Box Delimiter(wchar_t c, float h, float d, const Ctx& ctx) {
        if (!c) {
            Box b;
            b.w = 0.12f * Size(ctx);  // \nulldelimiterspace
            return b;
        }
        const float axis = C(kAxisHeight, ctx);
        const float delta = std::max(h - axis, d + axis);
        const float target = std::max(delta * 2.f * 0.901f, 2.f * delta - 0.3f * Size(ctx));
        Box b = Stretch(c, target, true, Size(ctx), ctx.color);
        CenterOnAxis(b, ctx);
        return b;
    }

    Atom Node(const MathNode& n, Ctx ctx) {
        if (n.color) ctx.color = n.color;
        if (n.error) ctx.color = kErrorColor;
        switch (n.kind) {
        case MathKind::List: {
            Atom a = List(n, ctx);
            a.cls = n.cls;
            if (n.cls == MathClass::Op) {
                a.op = true;
                a.limits = n.limits;
            }
            return a;
        }
        case MathKind::Glyphs: return Glyphs(n, ctx);
        case MathKind::Text: return Text(n, ctx);
        case MathKind::Scripts: return Scripts(n, ctx);
        case MathKind::Frac: return Frac(n, ctx);
        case MathKind::Radical: return Radical(n, ctx);
        case MathKind::Accent: return Accent(n, ctx);
        case MathKind::Over:
        case MathKind::Under: return OverUnder(n, ctx);
        case MathKind::Overline: return Overline(n, ctx);
        case MathKind::Fenced: return Fenced(n, ctx);
        case MathKind::Delim: return Delim(n, ctx);
        case MathKind::Array: return Array(n, ctx);
        case MathKind::Arrow: return Arrow(n, ctx);
        case MathKind::Boxed: return Boxed(n, ctx);
        case MathKind::Phantom: return Phantom(n, ctx);
        default: return Atom{};
        }
    }

    // A horizontal list: atoms with TeX's spacing between them.
    Atom List(const MathNode& n, Ctx ctx, bool lead_ord = false) {
        if (n.color) ctx.color = n.color;
        struct Item {
            Atom atom;
            float space = 0;
            bool is_space = false;
            int style = 1;  // a style switch only affects what follows it
        };
        std::vector<Item> items;
        items.reserve(n.kids.size());
        for (const MathNode& k : n.kids) {
            if (k.kind == MathKind::Style) {
                ctx.style = std::clamp(static_cast<int>(k.value), 0, 3);
                continue;
            }
            Item it;
            it.style = ctx.style;
            if (k.kind == MathKind::Space) {
                it.is_space = true;
                it.space = std::isfinite(k.value) ? k.value * Mu(ctx) : 0.f;
            } else {
                it.atom = Node(k, ctx);
            }
            items.push_back(std::move(it));
        }
        // TeX rules 5 and 6: a binary operator without operands is ordinary.
        int prev = lead_ord ? -2 : -1;  // -2: a leading {} (aligned cells)
        auto cls_of = [&](int i) { return i == -2 ? MathClass::Ord : items[static_cast<size_t>(i)].atom.cls; };
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[static_cast<size_t>(i)].is_space) continue;
            MathClass& cls = items[static_cast<size_t>(i)].atom.cls;
            if (cls == MathClass::Bin) {
                if (prev == -1) {
                    cls = MathClass::Ord;
                } else {
                    const MathClass p = cls_of(prev);
                    if (p == MathClass::Bin || p == MathClass::Op || p == MathClass::Rel || p == MathClass::Open ||
                        p == MathClass::Punct) {
                        cls = MathClass::Ord;
                    }
                }
            } else if ((cls == MathClass::Rel || cls == MathClass::Close || cls == MathClass::Punct) && prev >= 0 &&
                       items[static_cast<size_t>(prev)].atom.cls == MathClass::Bin) {
                items[static_cast<size_t>(prev)].atom.cls = MathClass::Ord;
            }
            prev = i;
        }
        if (prev >= 0 && items[static_cast<size_t>(prev)].atom.cls == MathClass::Bin) {
            items[static_cast<size_t>(prev)].atom.cls = MathClass::Ord;
        }
        Atom out;
        float x = 0;
        int last = lead_ord ? -2 : -1;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            Item& it = items[static_cast<size_t>(i)];
            if (it.is_space) {
                x += it.space;
                continue;
            }
            if (last != -1) {
                const int s = kSpacing[static_cast<int>(cls_of(last))][static_cast<int>(it.atom.cls)];
                if (s > 0 || (s < 0 && it.style <= 1)) {
                    static const float kMu[] = {0, 3, 4, 5};
                    Ctx at = ctx;
                    at.style = it.style;
                    x += kMu[std::abs(s)] * Mu(at);
                }
            }
            out.box.Add(it.atom.box, x, 0);
            x += it.atom.box.w;
            last = i;
        }
        out.box.w = x;
        // A list of one character keeps its character-ness (scripts, accents).
        if (items.size() == 1 && !items[0].is_space) {
            const Atom& only = items[0].atom;
            out.is_char = only.is_char;
            out.italic = only.italic;
            out.glyph = only.glyph;
            out.size = only.size;
            out.cls = only.cls;
            out.op = only.op;
            out.limits = only.limits;
        }
        return out;
    }

    Atom Glyphs(const MathNode& n, const Ctx& ctx) {
        Atom a;
        a.cls = n.cls;
        const float size = Size(ctx);
        // Emoji: in color, through DirectWrite.
        if (!n.large && std::any_of(n.text.begin(), n.text.end(), [](wchar_t c) { return c >= 0xD83C && c <= 0xD83E; }) &&
            ShapedText(n.text, size, ctx.color, &a.box)) {
            return a;
        }
        std::vector<uint32_t> cps;
        for (size_t k = 0; k < n.text.size(); ++k) {
            uint32_t cp = n.text[k];
            if (cp >= 0xD800 && cp <= 0xDBFF && k + 1 < n.text.size()) cp = 0x10000 + ((cp - 0xD800) << 10) + (n.text[++k] - 0xDC00);
            cps.push_back(cp);
        }
        float x = 0, prev_x = 0;
        const GlyphMetrics* prev = nullptr;
        float prev_s = 0;
        for (uint32_t cp : cps) {
            auto [face, glyph] = f_.Lookup(cp);
            if (IsCombining(cp) && prev) {
                // A combining mark (\not's slash) is centered over the glyph before it.
                const GlyphMetrics& mm = f_.Metrics(face, glyph);
                const float ms = size / f_.upem[face];
                const float dx = prev_x + (prev->xmin + prev->xmax) / 2 * prev_s - (mm.xmin + mm.xmax) / 2 * ms;
                a.box.Add(GlyphBox(face, glyph, size, ctx.color), dx, 0);
                continue;
            }
            if (n.large && ctx.style == 0 && face == 0) {
                // Display style: the first variant at least DisplayOperatorMinHeight
                // tall; integrals about twice the text size, as in TeX.
                const bool integral = (cp >= 0x222B && cp <= 0x2233) || (cp >= 0x2A0B && cp <= 0x2A1C);
                const float min_height = integral ? std::max(static_cast<float>(f_.c[kDisplayOperatorMinHeight]), 2.1f * f_.upem[0])
                                                  : static_cast<float>(f_.c[kDisplayOperatorMinHeight]);
                if (const size_t con = f_.Construction(glyph, true)) {
                    const Table& t = f_.math;
                    for (uint16_t k = 0, count = t.U16(con + 2); k < count; ++k) {
                        glyph = t.U16(con + 4 + static_cast<size_t>(k) * 4);
                        if (t.U16(con + 6 + static_cast<size_t>(k) * 4) >= min_height) break;
                    }
                }
            }
            Box g = GlyphBox(face, glyph, size, ctx.color);
            a.box.Add(g, x, 0);
            prev_x = x;
            prev = &f_.Metrics(face, glyph);
            prev_s = size / f_.upem[face];
            x += g.w;
            a.glyph = glyph;
            a.is_char = face == 0 && cps.size() == 1 && !IsCombining(cp);
        }
        a.box.w = x;
        a.size = size;
        if (a.is_char) a.italic = f_.Italic(a.glyph) * size / f_.upem[0];
        if (n.large) {
            // The operator's advance covers its ink already: scripts attach to
            // the ink (below), the italic correction only offsets them.
            a.op = true;
            a.limits = n.limits;
            if (a.is_char) a.ink_right = f_.Metrics(0, a.glyph).xmax * size / f_.upem[0];
            CenterOnAxis(a.box, ctx);
        } else {
            a.box.w += a.italic;
        }
        return a;
    }

    // Upright text (\text, operator names): glyph by glyph, spaces kept.
    Atom Text(const MathNode& n, const Ctx& ctx) {
        Atom a;
        a.cls = n.cls;
        const float size = Size(ctx);
        if (NeedsShaping(n.text) && ShapedText(n.text, size, ctx.color, &a.box)) {
            if (n.cls == MathClass::Op) {
                a.op = true;
                a.limits = n.limits;
            }
            return a;
        }
        float x = 0;
        for (size_t k = 0; k < n.text.size(); ++k) {
            uint32_t cp = n.text[k];
            if (cp >= 0xD800 && cp <= 0xDBFF && k + 1 < n.text.size()) cp = 0x10000 + ((cp - 0xD800) << 10) + (n.text[++k] - 0xDC00);
            if (cp == 0x00A0) cp = ' ';
            const auto [face, glyph] = f_.Lookup(cp);
            Box g = GlyphBox(face, glyph, size, ctx.color);
            a.box.Add(g, x, 0);
            x += g.w;
        }
        a.box.w = x;
        if (n.cls == MathClass::Op) {
            a.op = true;
            a.limits = n.limits;
        }
        return a;
    }

    // Text laid out by DirectWrite (shaping, bidi, color fonts); its box is
    // the ink around the baseline. Past a formula's budget, false: the text
    // goes glyph by glyph (each layout costs a draw call).
    bool ShapedText(const std::wstring& text, float size, uint32_t color, Box* box) {
        if (shaped_budget_ <= 0) return false;
        ComPtr<IDWriteTextFormat> format;
        ComPtr<IDWriteTextLayout> layout;
        constexpr float kFar = 10000.f;
        if (FAILED(f_.dw->CreateTextFormat(L"Cambria Math", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, size, L"", &format)) ||
            FAILED(f_.dw->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), kFar, kFar,
                                           &layout))) {
            return false;
        }
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TEXT_METRICS tm{};
        DWRITE_OVERHANG_METRICS om{};
        DWRITE_LINE_METRICS line{};
        UINT32 lines = 0;
        if (FAILED(layout->GetMetrics(&tm)) || FAILED(layout->GetOverhangMetrics(&om)) ||
            FAILED(layout->GetLineMetrics(&line, 1, &lines)) || lines == 0) {
            return false;
        }
        const float baseline = line.baseline;
        box->w = tm.widthIncludingTrailingWhitespace;
        box->h = std::max(0.f, baseline + om.top);             // ink above the layout top: om.top
        box->d = std::max(0.f, kFar + om.bottom - baseline);  // ink below its bottom: om.bottom
        MathBox::Text t;
        t.y = -baseline;
        t.ink = D2D1::RectF(-om.left, -om.top, kFar + om.right, kFar + om.bottom);
        t.color = color;
        t.layout = std::move(layout);
        box->texts.push_back(std::move(t));
        --shaped_budget_;
        return true;
    }

    Atom Scripts(const MathNode& n, const Ctx& ctx) {
        Atom base = Node(n.kids[0], ctx);
        const MathNode empty;
        const MathNode& sub_node = n.kids.size() > 1 ? n.kids[1] : empty;
        const MathNode& sup_node = n.kids.size() > 2 ? n.kids[2] : empty;
        const bool limits = base.op && (base.limits == 1 || (base.limits == 0 && ctx.style == 0));
        // Primes (x', x^\prime) are drawn at the base's size right after it
        // (U+2032 sits high already); the rest of the superscript follows.
        // After a taller base (a group, an operator) they are an ordinary
        // superscript, as in TeX.
        size_t primes = 0;
        while (!limits && base.is_char && !base.op && primes < sup_node.kids.size() && IsPrime(sup_node.kids[primes])) {
            ++primes;
        }
        MathNode prime_list, sup_rest;
        prime_list.color = sup_rest.color = sup_node.color;
        prime_list.kids.assign(sup_node.kids.begin(), sup_node.kids.begin() + static_cast<ptrdiff_t>(primes));
        sup_rest.kids.assign(sup_node.kids.begin() + static_cast<ptrdiff_t>(primes), sup_node.kids.end());
        const bool has_sub = !sub_node.kids.empty();
        const bool has_sup = !sup_rest.kids.empty();
        if (!has_sub && !has_sup && !primes) return base;
        const Ctx sup_ctx = ScriptCtx(ctx, false), sub_ctx = ScriptCtx(ctx, true);
        Atom sup, sub;
        if (has_sup) sup = List(sup_rest, sup_ctx);
        if (has_sub) sub = List(sub_node, sub_ctx);
        const float italic = base.italic;
        const float base_w = base.ink_right >= 0 ? base.box.w : base.box.w - italic;
        Atom out;
        out.cls = base.cls;
        if (limits) {
            // Rule 13a: limits centered above and below.
            const float w = std::max({base_w, sup.box.w, sub.box.w});
            out.box.Add(base.box, (w - base_w) / 2, 0);
            if (has_sup) {
                const float rise = std::max(C(kUpperLimitBaselineRiseMin, ctx), C(kUpperLimitGapMin, ctx) + sup.box.d);
                out.box.Add(sup.box, (w - sup.box.w) / 2 + italic / 2, -(base.box.h + rise));
            }
            if (has_sub) {
                const float drop = std::max(C(kLowerLimitBaselineDropMin, ctx), C(kLowerLimitGapMin, ctx) + sub.box.h);
                out.box.Add(sub.box, (w - sub.box.w) / 2 - italic / 2, base.box.d + drop);
            }
            out.box.w = w;
            return out;
        }
        // Rule 18: superscript and subscript shifts (a big operator is a box).
        float u = 0, v = 0;
        if (!base.is_char || base.op) {
            u = base.box.h - C(kSuperscriptBaselineDropMax, ctx);
            v = base.box.d + C(kSubscriptBaselineDropMin, ctx);
        }
        if (has_sup) {
            u = std::max({u, C(ctx.cramped ? kSuperscriptShiftUpCramped : kSuperscriptShiftUp, ctx),
                          sup.box.d + C(kSuperscriptBottomMin, ctx)});
        }
        if (has_sub) {
            v = std::max(v, C(kSubscriptShiftDown, ctx));
            if (!has_sup) v = std::max(v, sub.box.h - C(kSubscriptTopMax, ctx));
        }
        if (has_sup && has_sub) {
            const float gap = (u - sup.box.d) - (sub.box.h - v);
            const float min_gap = C(kSubSuperscriptGapMin, ctx);
            if (gap < min_gap) {
                v += min_gap - gap;
                const float psi = C(kSuperscriptBottomMaxWithSubscript, ctx) - (u - sup.box.d);
                if (psi > 0) {
                    u += psi;
                    v -= psi;
                }
            }
        }
        out.box.Add(base.box, 0, 0);
        float w = base.box.w;
        float sup_x = base_w + italic, sub_x = base_w;
        if (base.ink_right >= 0) {  // integrals: at the top hook, the bottom one further left
            sup_x = base.ink_right + (italic > 0 ? 0.05f * Size(ctx) : 0.f);
            sub_x = std::max(0.f, base.ink_right - italic);
        }
        if (primes) {
            const Atom p = List(prime_list, ctx);
            out.box.Add(p.box, sup_x, 0);
            sup_x += p.box.w;
            w = std::max(w, sup_x);
        }
        if (has_sup) {
            out.box.Add(sup.box, sup_x, -u);
            w = std::max(w, sup_x + sup.box.w);
        }
        if (has_sub) {
            out.box.Add(sub.box, sub_x, v);
            w = std::max(w, sub_x + sub.box.w);
        }
        out.box.w = w + (has_sup || has_sub ? C(kSpaceAfterScript, ctx) : 0.f);
        return out;
    }

    Atom Frac(const MathNode& n, Ctx ctx) {
        if (n.style >= 0) ctx.style = n.style;
        const bool display = ctx.style == 0;
        const Atom num = List(n.kids[0], FracCtx(ctx, false));
        const Atom den = List(n.kids[1], FracCtx(ctx, true));
        const float t = n.value < 0 ? C(kFractionRuleThickness, ctx) : 0.f;
        const float axis = C(kAxisHeight, ctx);
        float u, v;
        if (t > 0) {
            u = C(display ? kFractionNumeratorDisplayStyleShiftUp : kFractionNumeratorShiftUp, ctx);
            v = C(display ? kFractionDenominatorDisplayStyleShiftDown : kFractionDenominatorShiftDown, ctx);
            const float gap_num = C(display ? kFractionNumDisplayStyleGapMin : kFractionNumeratorGapMin, ctx);
            const float gap_den = C(display ? kFractionDenomDisplayStyleGapMin : kFractionDenominatorGapMin, ctx);
            u += std::max(0.f, gap_num - ((u - num.box.d) - (axis + t / 2)));
            v += std::max(0.f, gap_den - ((axis - t / 2) - (den.box.h - v)));
        } else {
            u = C(display ? kStackTopDisplayStyleShiftUp : kStackTopShiftUp, ctx);
            v = C(display ? kStackBottomDisplayStyleShiftDown : kStackBottomShiftDown, ctx);
            const float gap = C(display ? kStackDisplayStyleGapMin : kStackGapMin, ctx);
            const float clearance = (u - num.box.d) - (den.box.h - v);
            if (clearance < gap) {
                u += (gap - clearance) / 2;
                v += (gap - clearance) / 2;
            }
        }
        const float pad = t > 0 ? 0.12f * Size(ctx) : 0.f;  // \nulldelimiterspace
        const float inner = std::max(num.box.w, den.box.w);
        Atom a;
        a.cls = MathClass::Inner;
        a.box.Add(num.box, pad + (inner - num.box.w) / 2, -u);
        a.box.Add(den.box, pad + (inner - den.box.w) / 2, v);
        if (t > 0) {
            a.box.rules.push_back(MathBox::Rule{pad, -(axis + t / 2), inner, t, ctx.color});
            a.box.h = std::max(a.box.h, axis + t / 2);
            a.box.d = std::max(a.box.d, t / 2 - axis);
        }
        a.box.w = inner + 2 * pad;
        if (n.left || n.right) {  // \binom
            Box l = Delimiter(n.left, a.box.h, a.box.d, ctx);
            Box r = Delimiter(n.right, a.box.h, a.box.d, ctx);
            Box all;
            all.Add(l, 0, 0);
            all.Add(a.box, l.w, 0);
            all.Add(r, l.w + a.box.w, 0);
            all.w = l.w + a.box.w + r.w;
            a.box = std::move(all);
        }
        return a;
    }

    Atom Radical(const MathNode& n, const Ctx& ctx) {
        Ctx inner_ctx = ctx;
        inner_ctx.cramped = true;
        const Atom body = List(n.kids[0], inner_ctx);
        const bool display = ctx.style == 0;
        float gap = C(display ? kRadicalDisplayStyleVerticalGap : kRadicalVerticalGap, ctx);
        const float t = C(kRadicalRuleThickness, ctx);
        const float target = body.box.h + body.box.d + gap + t;
        Box sign = Stretch(0x221A, target, true, Size(ctx), ctx.color);
        const float total = sign.h + sign.d;
        if (total > target) gap += (total - target) / 2;  // center the radicand
        const float top = body.box.h + gap + t;           // rule top = sign top
        const float sign_dy = sign.h - top;               // moves the sign so its top is there
        Atom a;
        float x = 0;
        if (n.kids.size() > 1 && !n.kids[1].kids.empty()) {
            Ctx dctx = ctx;
            dctx.style = 3;
            const Atom degree = List(n.kids[1], dctx);
            const float raise = f_.c[kRadicalDegreeBottomRaisePercent] / 100.f * total;
            const float bottom = sign_dy + sign.d;  // bottom of the sign (y down)
            x = std::max(0.f, C(kRadicalKernBeforeDegree, ctx));
            a.box.Add(degree.box, x, bottom - raise);
            x += degree.box.w + C(kRadicalKernAfterDegree, ctx);
            x = std::max(x, 0.f);
        }
        a.box.Add(sign, x, sign_dy);
        x += sign.w;
        a.box.Add(body.box, x, 0);
        a.box.rules.push_back(MathBox::Rule{x, -top, body.box.w, t, ctx.color});
        a.box.w = x + body.box.w;
        a.box.h = std::max(a.box.h, top + C(kRadicalExtraAscender, ctx));
        return a;
    }

    Atom Accent(const MathNode& n, const Ctx& ctx) {
        Ctx base_ctx = ctx;
        base_ctx.cramped = true;
        Atom base = Node(n.kids[0], base_ctx);
        const uint32_t accent = n.text.empty() ? 0x0302 : n.text[0];
        const float size = Size(ctx);
        const float s = size / f_.upem[0];
        const auto [acc_face, acc_glyph] = f_.Lookup(accent);
        Box acc = n.stretchy ? Stretch(accent, base.box.w - base.italic, false, size, ctx.color)
                             : GlyphBox(acc_face, acc_glyph, size, ctx.color);
        // Horizontal: attachment points (MATH table) or centers.
        float base_x = (base.box.w - base.italic) / 2;
        float attach = 0;
        if (base.is_char && f_.TopAccent(base.glyph, &attach)) base_x = attach * base.size / f_.upem[0];
        float acc_x = acc.w / 2;
        if (acc.glyphs.size() == 1) {
            const MathBox::Glyph& ag = acc.glyphs[0];
            if (ag.face == 0 && f_.TopAccent(ag.index, &attach)) {
                acc_x = attach * s;
            } else {
                const GlyphMetrics& am = f_.Metrics(ag.face, ag.index);
                acc_x = (am.xmin + am.xmax) / 2 * size / f_.upem[ag.face];
            }
        }
        Atom a;
        a.cls = MathClass::Ord;
        a.box.Add(base.box, 0, 0);
        if (IsCombining(accent)) {
            // Combining accents sit right over a base as tall as AccentBaseHeight.
            const float lift = std::max(0.f, base.box.h - C(kAccentBaseHeight, ctx));
            a.box.Add(acc, base_x - acc_x, -lift);
        } else {
            const float gap = C(kOverbarVerticalGap, ctx) * 0.5f;
            float top = 0, bottom = 0;
            Ink(acc, &top, &bottom);
            a.box.Add(acc, base_x - acc_x, -(base.box.h + gap + bottom));
        }
        a.box.w = base.box.w;
        a.italic = base.italic;
        return a;
    }

    // \overbrace, \underbrace (stretchy `text`) or \overset, \underset (a label).
    Atom OverUnder(const MathNode& n, const Ctx& ctx) {
        const bool over = n.kind == MathKind::Over;
        const Atom base = Node(n.kids[0], ctx);
        Box extra;
        float gap = 0;
        if (!n.text.empty()) {
            // TeX puts 3pt between a brace and its base.
            extra = Stretch(n.text[0], base.box.w, false, Size(ctx), ctx.color);
            gap = std::max(C(over ? kStretchStackGapAboveMin : kStretchStackGapBelowMin, ctx), 0.3f * Size(ctx));
        } else if (n.kids.size() > 1) {
            extra = List(n.kids[1], ScriptCtx(ctx, !over)).box;
            gap = over ? std::max(C(kUpperLimitGapMin, ctx), C(kUpperLimitBaselineRiseMin, ctx) - extra.d)
                       : std::max(C(kLowerLimitGapMin, ctx), C(kLowerLimitBaselineDropMin, ctx) - extra.h);
        }
        const float w = std::max(base.box.w, extra.w);
        Atom a;
        a.cls = n.cls;
        a.op = n.cls == MathClass::Op;
        a.limits = n.limits;
        a.box.Add(base.box, (w - base.box.w) / 2, 0);
        float top = -extra.h, bottom = extra.d;
        if (!n.text.empty()) Ink(extra, &top, &bottom);  // a brace may float above its baseline
        if (over) a.box.Add(extra, (w - extra.w) / 2, -(base.box.h + gap + bottom));
        else a.box.Add(extra, (w - extra.w) / 2, base.box.d + gap - top);
        a.box.w = w;
        return a;
    }

    Atom Overline(const MathNode& n, const Ctx& ctx) {
        Ctx base_ctx = ctx;
        if (n.value == 0) base_ctx.cramped = true;
        const Atom base = List(n.kids[0], base_ctx);
        Atom a;
        a.box = base.box;
        if (n.value == 0) {
            const float t = C(kOverbarRuleThickness, ctx), gap = C(kOverbarVerticalGap, ctx);
            a.box.rules.push_back(MathBox::Rule{0, -(base.box.h + gap + t), base.box.w, t, ctx.color});
            a.box.h = base.box.h + gap + t + C(kOverbarExtraAscender, ctx);
        } else {
            const float t = C(kUnderbarRuleThickness, ctx), gap = C(kUnderbarVerticalGap, ctx);
            a.box.rules.push_back(MathBox::Rule{0, base.box.d + gap, base.box.w, t, ctx.color});
            a.box.d = base.box.d + gap + t + C(kUnderbarExtraDescender, ctx);
        }
        return a;
    }

    Atom Fenced(const MathNode& n, const Ctx& ctx) {
        std::vector<Atom> parts;
        float h = 0, d = 0;
        for (const MathNode& k : n.kids) {
            parts.push_back(List(k, ctx));
            h = std::max(h, parts.back().box.h);
            d = std::max(d, parts.back().box.d);
        }
        Atom a;
        a.cls = MathClass::Inner;
        float x = 0;
        for (size_t k = 0; k <= parts.size(); ++k) {
            const wchar_t c = k < n.text.size() ? n.text[k] : 0;
            Box delim = Delimiter(c, h, d, ctx);
            a.box.Add(delim, x, 0);
            x += delim.w;
            if (k < parts.size()) {
                a.box.Add(parts[k].box, x, 0);
                x += parts[k].box.w;
            }
        }
        a.box.w = x;
        return a;
    }

    Atom Delim(const MathNode& n, const Ctx& ctx) {
        Atom a;
        a.cls = n.cls;
        const wchar_t c = n.text.empty() ? 0 : n.text[0];
        if (!c) {
            a.box.w = 0.12f * Size(ctx);
            return a;
        }
        a.box = Stretch(c, n.value * Size(ctx), true, Size(ctx), ctx.color);
        CenterOnAxis(a.box, ctx);
        return a;
    }

    Atom Array(const MathNode& n, const Ctx& ctx) {
        Ctx cell = ctx;
        float strut_h = 0.8f, strut_d = 0.3f, row_gap = 0.5f, col_gap = 1.f;  // em
        switch (n.env) {
        case MathEnv::Aligned:
        case MathEnv::Gathered:
            cell.style = 0;
            row_gap = 0.25f;
            break;
        case MathEnv::Cases:
            cell.style = n.style == 0 ? 0 : std::max(ctx.style, 1);
            row_gap = 0.2f;
            break;
        case MathEnv::Small:
            cell.style = std::max(ctx.style, 2);
            strut_h = 0.6f;
            strut_d = 0.2f;
            row_gap = 0.1f;
            col_gap = 0.5f;
            break;
        default:
            cell.style = std::max(ctx.style, 1);
            break;
        }
        const float em = Size(cell);
        size_t cols = 0;
        for (const MathNode& row : n.kids) cols = std::max(cols, row.kids.size());
        std::vector<std::vector<Atom>> cells(n.kids.size());
        std::vector<float> col_w(cols, 0.f), row_h(n.kids.size(), strut_h * em), row_d(n.kids.size(), strut_d * em);
        for (size_t r = 0; r < n.kids.size(); ++r) {
            for (size_t c = 0; c < n.kids[r].kids.size(); ++c) {
                const bool lead = n.env == MathEnv::Aligned && c % 2 == 1;
                cells[r].push_back(List(n.kids[r].kids[c], cell, lead));
                const Box& b = cells[r].back().box;
                col_w[c] = std::max(col_w[c], b.w);
                row_h[r] = std::max(row_h[r], b.h);
                row_d[r] = std::max(row_d[r], b.d);
            }
        }
        std::vector<float> col_x(cols, 0.f);
        float x = 0;
        for (size_t c = 0; c < cols; ++c) {
            if (c > 0) {
                if (n.env == MathEnv::Aligned) x += c % 2 == 0 ? 2.f * em : 0.f;
                else x += col_gap * em;
            }
            col_x[c] = x;
            x += col_w[c];
        }
        Atom a;
        a.cls = n.cls;
        float y = 0;  // baseline of each row, from the top
        std::vector<float> baselines;
        for (size_t r = 0; r < n.kids.size(); ++r) {
            y += (r ? row_gap * em : 0.f) + row_h[r];
            baselines.push_back(y);
            y += row_d[r];
        }
        const float total = y;
        const float axis = C(kAxisHeight, ctx);
        const float top = total / 2 + axis;  // array top above the baseline
        for (size_t r = 0; r < cells.size(); ++r) {
            for (size_t c = 0; c < cells[r].size(); ++c) {
                wchar_t align = n.text.empty() ? L'c' : n.text[std::min(c, n.text.size() - 1)];
                if (n.env == MathEnv::Aligned) align = c % 2 == 0 ? L'r' : L'l';
                const Box& b = cells[r][c].box;
                const float free = col_w[c] - b.w;
                const float dx = align == L'l' ? 0.f : align == L'r' ? free : free / 2;
                a.box.Add(b, col_x[c] + dx, baselines[r] - top);
            }
        }
        a.box.w = x;
        a.box.h = std::max(a.box.h, top);
        a.box.d = std::max(a.box.d, total - top);
        if (n.left || n.right) {
            Box l = Delimiter(n.left, a.box.h, a.box.d, ctx);
            Box rr = Delimiter(n.right, a.box.h, a.box.d, ctx);
            Box all;
            all.Add(l, 0, 0);
            all.Add(a.box, l.w, 0);
            all.Add(rr, l.w + a.box.w, 0);
            all.w = l.w + a.box.w + rr.w;
            a.box = std::move(all);
        }
        return a;
    }

    Atom Arrow(const MathNode& n, const Ctx& ctx) {
        const Ctx label_ctx = ScriptCtx(ctx, false);
        const Atom above = List(n.kids[0], label_ctx);
        const Atom below = n.kids.size() > 1 ? List(n.kids[1], ScriptCtx(ctx, true)) : Atom{};
        const float pad = 0.3f * Size(ctx);
        const float want = std::max({above.box.w, below.box.w}) + 2 * pad;
        Box arrow = Stretch(n.text.empty() ? 0x2192 : n.text[0], std::max(want, 1.2f * Size(ctx)), false, Size(ctx),
                            ctx.color);
        const float w = std::max({arrow.w, above.box.w, below.box.w});
        const float gap = 0.1f * Size(ctx);
        Atom a;
        a.cls = MathClass::Rel;
        a.box.Add(arrow, (w - arrow.w) / 2, 0);
        if (!n.kids[0].kids.empty()) a.box.Add(above.box, (w - above.box.w) / 2, -(arrow.h + gap + above.box.d));
        if (n.kids.size() > 1 && !n.kids[1].kids.empty()) {
            a.box.Add(below.box, (w - below.box.w) / 2, arrow.d + gap + below.box.h);
        }
        a.box.w = w;
        return a;
    }

    Atom Boxed(const MathNode& n, const Ctx& ctx) {
        const Atom inner = List(n.kids[0], ctx);
        const float t = C(kFractionRuleThickness, ctx), pad = 0.25f * Size(ctx);
        Atom a;
        a.box.Add(inner.box, pad + t, 0);
        a.box.w = inner.box.w + 2 * (pad + t);
        a.box.h = inner.box.h + pad + t;
        a.box.d = inner.box.d + pad + t;
        const float top = -a.box.h, height = a.box.h + a.box.d;
        a.box.rules.push_back(MathBox::Rule{0, top, a.box.w, t, ctx.color});
        a.box.rules.push_back(MathBox::Rule{0, a.box.d - t, a.box.w, t, ctx.color});
        a.box.rules.push_back(MathBox::Rule{0, top, t, height, ctx.color});
        a.box.rules.push_back(MathBox::Rule{a.box.w - t, top, t, height, ctx.color});
        return a;
    }

    Atom Phantom(const MathNode& n, const Ctx& ctx) {
        const Atom inner = List(n.kids[0], ctx);
        Atom a;
        a.box.w = n.value == 2 ? 0.f : inner.box.w;
        a.box.h = n.value == 1 ? 0.f : inner.box.h;
        a.box.d = n.value == 1 ? 0.f : inner.box.d;
        return a;
    }

    MathTypesetter::Impl& f_;
    float size_;
    uint16_t group_ = 0;  // last id of a stretched glyph's parts
    int assembly_budget_ = kFormulaAssemblyGlyphs;
    int shaped_budget_ = kMaxShapedTexts;
};

}  // namespace

MathTypesetter::MathTypesetter() : impl_(std::make_unique<Impl>()) {}
MathTypesetter::~MathTypesetter() = default;

bool MathTypesetter::Init(IDWriteFactory* dw) { return impl_->Load(dw); }

float MathTypesetter::XHeight() const { return impl_->x_height; }

std::shared_ptr<const MathBox> MathTypesetter::Layout(std::wstring_view tex, bool display, float size) {
    auto out = std::make_shared<MathBox>();
    if (impl_->faces.empty() || !(size > 0)) return out;
    const MathNode root = ParseTex(tex);
    Typesetter ts(*impl_, size);
    Atom a = ts.Run(root, display);
    // Nothing non-finite or negative reaches the text layout.
    auto finite = [](float v) { return std::isfinite(v) && std::fabs(v) < kFarDips; };
    auto extent = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.f, kFarDips) : 0.f; };
    out->width = extent(a.box.w);
    out->ascent = extent(a.box.h);
    out->descent = extent(a.box.d);
    for (const MathBox::Glyph& g : a.box.glyphs) {
        if (finite(g.x) && finite(g.y)) out->glyphs.push_back(g);
    }
    for (const MathBox::Rule& r : a.box.rules) {
        if (finite(r.x) && finite(r.y) && finite(r.w) && finite(r.h)) out->rules.push_back(r);
    }
    for (MathBox::Text& t : a.box.texts) {
        if (finite(t.x) && finite(t.y)) out->texts.push_back(std::move(t));
    }
    return out;
}

void MathTypesetter::Draw(ID2D1RenderTarget* rt, const MathBox& box, D2D1_POINT_2F origin, float scale,
                          ID2D1Brush* brush) const {
    if (!rt || !brush) return;
    ComPtr<ID2D1SolidColorBrush> colored;
    auto brush_for = [&](uint32_t color) -> ID2D1Brush* {
        if (!color) return brush;
        const D2D1_COLOR_F c = D2D1::ColorF(color & 0xFFFFFF, static_cast<float>(color >> 24) / 255.f);
        if (!colored && FAILED(rt->CreateSolidColorBrush(c, &colored))) return brush;
        colored->SetColor(c);
        return colored.Get();
    };
    D2D1_MATRIX_3X2_F world;
    rt->GetTransform(&world);
    // What of the formula (its own coordinates) is on the target: the rest
    // isn't drawn.
    const bool aligned = world._12 == 0 && world._21 == 0 && world._11 > 0 && world._22 > 0 && scale > 0;
    const D2D1_SIZE_F target = rt->GetSize();
    const D2D1_RECT_F visible = aligned ? D2D1::RectF(((-world._31) / world._11 - origin.x) / scale,
                                                      ((-world._32) / world._22 - origin.y) / scale,
                                                      ((target.width - world._31) / world._11 - origin.x) / scale,
                                                      ((target.height - world._32) / world._22 - origin.y) / scale)
                                        : D2D1::RectF(-1e30f, -1e30f, 1e30f, 1e30f);
    auto shown = [&visible](float x0, float y0, float x1, float y1) {
        return x1 >= visible.left && x0 <= visible.right && y1 >= visible.top && y0 <= visible.bottom;
    };
    ComPtr<ID2D1Factory> factory;
    rt->GetFactory(&factory);
    if (box.shapes_factory.Get() != factory.Get()) {
        box.shapes.clear();
        box.shapes_factory = factory;
    }

    // Rules on whole device pixels (after the scroll transform), with their
    // thickness rounded on its own: bars of equal weight stay equal and crisp.
    float dpi_x = 96, dpi_y = 96;
    rt->GetDpi(&dpi_x, &dpi_y);
    const float px = dpi_y / 96.f * world._22;  // device pixels per DIP, vertically
    const bool snap = world._12 == 0 && world._21 == 0 && px > 0;
    for (const MathBox::Rule& r : box.rules) {
        if (!shown(r.x, r.y, r.x + r.w, r.y + r.h)) continue;
        float top = origin.y + r.y * scale, height = r.h * scale;
        if (snap) {
            const float dev_top = std::round((top * world._22 + world._32) * dpi_y / 96.f);
            top = (dev_top * 96.f / dpi_y - world._32) / world._22;
            height = std::max(1.f, std::round(height * px)) / px;
        }
        rt->FillRectangle(D2D1::RectF(origin.x + r.x * scale, top, origin.x + (r.x + r.w) * scale, top + height),
                          brush_for(r.color));
    }

    // Glyphs: runs of one face, size and color; the parts of a stretched
    // glyph as one filled shape, so their overlaps aren't blended twice.
    std::vector<UINT16> indices;
    std::vector<FLOAT> advances;
    std::vector<DWRITE_GLYPH_OFFSET> offsets;
    const auto& glyphs = box.glyphs;
    for (size_t k = 0; k < glyphs.size();) {
        const MathBox::Glyph& g0 = glyphs[k];
        size_t e = k + 1;
        if (g0.group) {
            while (e < glyphs.size() && glyphs[e].group == g0.group && glyphs[e].face == g0.face) ++e;
        } else {
            while (e < glyphs.size() && !glyphs[e].group && glyphs[e].face == g0.face && glyphs[e].size == g0.size &&
                   glyphs[e].color == g0.color) {
                ++e;
            }
        }
        // Rough bounds of the run (a glyph stays within a few ems of its origin).
        float x0 = g0.x, x1 = g0.x, y0 = g0.y, y1 = g0.y, em = 0;
        for (size_t i = k; i < e; ++i) {
            x0 = std::min(x0, glyphs[i].x);
            x1 = std::max(x1, glyphs[i].x);
            y0 = std::min(y0, glyphs[i].y);
            y1 = std::max(y1, glyphs[i].y);
            em = std::max(em, glyphs[i].size);
        }
        if (g0.face >= impl_->faces.size() || !shown(x0 - 2 * em, y0 - 4 * em, x1 + 4 * em, y1 + 3 * em)) {
            k = e;
            continue;
        }
        const D2D1_POINT_2F at = D2D1::Point2F(origin.x + g0.x * scale, origin.y + g0.y * scale);
        IDWriteFontFace* face = impl_->faces[g0.face].Get();
        auto collect = [&](float k_scale) {
            indices.clear();
            advances.assign(e - k, 0.f);
            offsets.clear();
            for (size_t i = k; i < e; ++i) {
                indices.push_back(glyphs[i].index);
                offsets.push_back(DWRITE_GLYPH_OFFSET{(glyphs[i].x - g0.x) * k_scale, -(glyphs[i].y - g0.y) * k_scale});
            }
        };
        bool drawn = false;
        if (g0.group && e - k > 1 && factory) {
            // Built once at the formula's own size, then scaled.
            ComPtr<ID2D1PathGeometry>& shape = box.shapes[g0.group];
            if (!shape) {
                ComPtr<ID2D1PathGeometry> built;
                ComPtr<ID2D1GeometrySink> sink;
                collect(1.f);
                if (SUCCEEDED(factory->CreatePathGeometry(&built)) && SUCCEEDED(built->Open(&sink))) {
                    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
                    const bool ok = SUCCEEDED(face->GetGlyphRunOutline(g0.size, indices.data(), advances.data(),
                                                                       offsets.data(), static_cast<UINT32>(indices.size()),
                                                                       FALSE, FALSE, sink.Get()));
                    if (SUCCEEDED(sink->Close()) && ok) shape = built;
                }
            }
            if (shape) {
                rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(at.x, at.y) * world);
                rt->FillGeometry(shape.Get(), brush_for(g0.color));
                rt->SetTransform(world);
                drawn = true;
            }
        }
        if (!drawn) {
            collect(scale);
            DWRITE_GLYPH_RUN run{};
            run.fontFace = face;
            run.fontEmSize = g0.size * scale;
            run.glyphCount = static_cast<UINT32>(indices.size());
            run.glyphIndices = indices.data();
            run.glyphAdvances = advances.data();
            run.glyphOffsets = offsets.data();
            rt->DrawGlyphRun(at, &run, brush_for(g0.color));
        }
        k = e;
    }

    // Shaped text, laid out at the formula's own size.
    for (const MathBox::Text& t : box.texts) {
        const float m = (t.ink.bottom - t.ink.top) * 0.5f;  // color layers may reach past the outline
        if (!t.layout || !shown(t.x + t.ink.left - m, t.y + t.ink.top - m, t.x + t.ink.right + m, t.y + t.ink.bottom + m)) {
            continue;
        }
        rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
                         D2D1::Matrix3x2F::Translation(origin.x + t.x * scale, origin.y + t.y * scale) * world);
        rt->DrawTextLayout(D2D1::Point2F(0, 0), t.layout.Get(), brush_for(t.color), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        rt->SetTransform(world);
    }
}

}  // namespace mdv
