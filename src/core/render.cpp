#include "core/render.h"

#include <algorithm>
#include <cmath>

#include "core/highlight.h"
#include "core/images.h"

namespace mdv {
namespace {

const wchar_t* AlertLabel(AlertKind k) {
    switch (k) {
    case AlertKind::Tip: return L"Tip";
    case AlertKind::Important: return L"Important";
    case AlertKind::Warning: return L"Warning";
    case AlertKind::Caution: return L"Caution";
    default: return L"Note";
    }
}

Icon AlertIcon(AlertKind k) {
    switch (k) {
    case AlertKind::Tip: return Icon::LightBulb;
    case AlertKind::Important: return Icon::Report;
    case AlertKind::Warning: return Icon::Alert;
    case AlertKind::Caution: return Icon::Stop;
    default: return Icon::Info;
    }
}

float LabelBaseline(IDWriteTextLayout* tl) { return FirstLineBaseline(tl); }

float LabelWidth(IDWriteTextLayout* tl) {
    DWRITE_TEXT_METRICS tm{};
    tl->GetMetrics(&tm);
    return tm.widthIncludingTrailingWhitespace;
}

bool InFootnotes(const Block* b) {
    for (const Block* p = b; p; p = p->parent) {
        if (p->type == BlockType::FootnoteSection) return true;
    }
    return false;
}

}  // namespace

std::wstring ListMarkerText(int n, int depth) {
    if (depth == 1 && n > 0 && n < 4000) {
        static const wchar_t* kOnes[] = {L"", L"i", L"ii", L"iii", L"iv", L"v", L"vi", L"vii", L"viii", L"ix"};
        static const wchar_t* kTens[] = {L"", L"x", L"xx", L"xxx", L"xl", L"l", L"lx", L"lxx", L"lxxx", L"xc"};
        static const wchar_t* kHundreds[] = {L"", L"c", L"cc", L"ccc", L"cd", L"d", L"dc", L"dcc", L"dccc", L"cm"};
        std::wstring s(static_cast<size_t>(n / 1000), L'm');
        s += kHundreds[(n / 100) % 10];
        s += kTens[(n / 10) % 10];
        s += kOnes[n % 10];
        return s;
    }
    if (depth >= 2 && n > 0) {
        std::wstring s;
        int v = n;
        while (v > 0) {
            --v;
            s.insert(s.begin(), static_cast<wchar_t>(L'a' + v % 26));
            v /= 26;
        }
        return s;
    }
    return std::to_wstring(n);
}

D2D1_RECT_F Renderer::CopyButtonRect(const Block* code, const Metrics& m) {
    const float size = m.em * 1.9f;
    const float margin = m.em * 0.5f;
    const float right = code->x + code->w - margin;
    const float top = code->y + margin;
    return D2D1::RectF(right - size, top, right, top + size);
}

D2D1_RECT_F Renderer::HScrollbarRect(const Block* b, const Metrics& m, bool thumb) {
    const float h = std::max(4.f, m.em * 0.3f);
    const float bottom = b->y + b->h - 2.f;
    const D2D1_RECT_F track = D2D1::RectF(b->x + 2.f, bottom - h, b->x + b->w - 2.f, bottom);
    if (!thumb || b->content_w <= 0) return track;
    const float tw = track.right - track.left;
    const float ratio = b->w / b->content_w;
    const float thumb_w = std::max(tw * ratio, m.em * 2.f);
    const float range = b->content_w - b->w;
    const float pos = range > 0 ? b->scroll_x / range : 0.f;
    const float left = track.left + (tw - thumb_w) * pos;
    return D2D1::RectF(left, track.top, left + thumb_w, track.bottom);
}

ID2D1SolidColorBrush* Renderer::Brush(uint8_t role) const { return palette_->Brush(role); }

float Renderer::Snap(float v) const {
    const float s = st_ ? st_->dpi_scale : 1.f;
    return std::round(v * s) / s;
}

float Renderer::Hairline() const {
    const float s = st_ ? st_->dpi_scale : 1.f;
    return std::max(1.f, std::round(s)) / s;
}

void Renderer::Draw(ID2D1RenderTarget* rt, LayoutEngine& layout, const RenderState& st) {
    rt_ = rt;
    layout_ = &layout;
    palette_ = layout.palette();
    st_ = &st;
    Document* doc = layout.document();
    if (!doc || !palette_) return;
    view_top_ = st.scroll_y;
    view_bottom_ = st.scroll_y + st.view_h;
    layout.NextFrame();

    D2D1_MATRIX_3X2_F base;
    rt->GetTransform(&base);
    rt->SetTransform(D2D1::Matrix3x2F::Translation(0, -st.scroll_y) * base);
    DrawContext& dc = layout.draw_context();
    dc.rt = rt;
    dc.images = layout.images();
    dc.palette = palette_;
    dc.clip = D2D1::RectF(0, 0, st.view_w, st.view_h);
    clip_stack_.clear();
    DrawChildren(doc->root);
    dc.rt = nullptr;
    dc.leaf = nullptr;
    rt->SetTransform(base);
}

// An axis-aligned clip, also kept (in view coordinates) for inline objects
// that record where they are visible.
void Renderer::PushClip(const D2D1_RECT_F& r) {
    rt_->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
    DrawContext& dc = layout_->draw_context();
    clip_stack_.push_back(dc.clip);
    D2D1_MATRIX_3X2_F m;
    rt_->GetTransform(&m);
    dc.clip = D2D1::RectF(std::max(dc.clip.left, r.left * m._11 + m._31), std::max(dc.clip.top, r.top * m._22 + m._32),
                          std::min(dc.clip.right, r.right * m._11 + m._31),
                          std::min(dc.clip.bottom, r.bottom * m._22 + m._32));
}

void Renderer::PopClip() {
    rt_->PopAxisAlignedClip();
    if (!clip_stack_.empty()) {
        layout_->draw_context().clip = clip_stack_.back();
        clip_stack_.pop_back();
    }
}

void Renderer::DrawChildren(Block* b) {
    auto& ch = b->children;
    auto it = std::partition_point(ch.begin(), ch.end(),
                                   [this](const Block* c) { return c->y + c->h < view_top_; });
    for (; it != ch.end(); ++it) {
        Block* c = *it;
        if (c->y > view_bottom_) break;
        if (c->visible) DrawBlock(c);
    }
}

// A leaf wider than its column (a long formula) scrolls horizontally.
void Renderer::DrawLeafScrolled(Block* b) {
    if (b->content_w <= 0) {
        DrawLeaf(b);
        return;
    }
    PushClip(D2D1::RectF(b->x, b->y, b->x + b->w, b->y + b->h));
    D2D1_MATRIX_3X2_F old;
    rt_->GetTransform(&old);
    rt_->SetTransform(D2D1::Matrix3x2F::Translation(-b->scroll_x, 0) * old);
    DrawLeaf(b);
    rt_->SetTransform(old);
    PopClip();
    DrawHScrollbar(b);
}

void Renderer::DrawBlock(Block* b) {
    const Metrics& m = layout_->metrics();
    switch (b->type) {
    case BlockType::Paragraph:
        DrawLeafScrolled(b);
        break;
    case BlockType::Summary: {
        const LeafState* s = layout_->State(b);
        const bool open = b->parent && b->parent->open;
        if (s) {
            const float em = s->style.size;
            const float baseline = s->exact ? s->ty + s->first_baseline : s->ty + em * 1.165f;
            const float sz = em * 0.55f;
            const float cy = baseline - em * 0.33f;
            DrawDisclosure(rt_, D2D1::RectF(b->x + em * 0.25f, cy - sz * 0.5f, b->x + em * 0.25f + sz, cy + sz * 0.5f),
                           open, Brush(s->style.color));
        }
        DrawLeafScrolled(b);
        break;
    }
    case BlockType::Heading:
        DrawLeafScrolled(b);
        if (b->level <= 2) {
            const float y = Snap(b->y + b->h - Hairline());
            rt_->FillRectangle(D2D1::RectF(b->x, y, b->x + b->w, y + Hairline()), Brush(kColorBorderMuted));
        }
        break;
    case BlockType::CodeBlock:
        DrawCodeBlock(b);
        break;
    case BlockType::Quote: {
        const float x = Snap(b->x);
        rt_->FillRectangle(D2D1::RectF(x, b->y, Snap(x + m.quote_bar), b->y + b->h), Brush(kColorBorder));
        DrawChildren(b);
        break;
    }
    case BlockType::ListItem:
    case BlockType::FootnoteDef:
        DrawListItem(b);
        break;
    case BlockType::Table:
        DrawTable(b);
        break;
    case BlockType::Rule: {
        const float y = Snap(b->y);
        rt_->FillRectangle(D2D1::RectF(b->x, y, b->x + b->w, Snap(b->y + b->h)), Brush(kColorRule));
        break;
    }
    case BlockType::Alert:
        DrawAlert(b);
        break;
    case BlockType::FootnoteSection: {
        const float y = Snap(b->y);
        rt_->FillRectangle(D2D1::RectF(b->x, y, b->x + b->w, y + Hairline()), Brush(kColorBorder));
        DrawChildren(b);
        break;
    }
    default:
        DrawChildren(b);
        break;
    }
}

void Renderer::FillRanges(IDWriteTextLayout* tl, D2D1_POINT_2F o, uint32_t start, uint32_t len,
                          ID2D1Brush* brush, float inset_y) {
    if (len == 0) return;
    UINT32 count = 0;
    HRESULT hr = tl->HitTestTextRange(start, len, o.x, o.y, nullptr, 0, &count);
    if (hr != E_NOT_SUFFICIENT_BUFFER && FAILED(hr)) return;
    if (count == 0) return;
    hit_buf_.resize(count);
    if (FAILED(tl->HitTestTextRange(start, len, o.x, o.y, hit_buf_.data(), count, &count))) return;
    for (UINT32 i = 0; i < count; ++i) {
        const auto& h = hit_buf_[i];
        rt_->FillRectangle(D2D1::RectF(h.left, h.top + inset_y, h.left + h.width, h.top + h.height - inset_y),
                           brush);
    }
}

void Renderer::DrawInlineBackgrounds(Block* b, IDWriteTextLayout* tl, D2D1_POINT_2F o) {
    const Metrics& m = layout_->metrics();
    const LeafState* s = layout_->State(b);
    const float size = s ? s->style.size : m.em;
    const size_t nr = b->runs.size();

    if (b->type == BlockType::CodeBlock) {
        // Full-width backgrounds for diff lines.
        for (const StyleRun& r : b->runs) {
            const auto kind = static_cast<TokenKind>(r.token);
            if (kind != TokenKind::Inserted && kind != TokenKind::Deleted) continue;
            UINT32 count = 0;
            tl->HitTestTextRange(r.start, r.length, o.x, o.y, nullptr, 0, &count);
            if (!count) continue;
            hit_buf_.resize(count);
            if (FAILED(tl->HitTestTextRange(r.start, r.length, o.x, o.y, hit_buf_.data(), count, &count))) continue;
            const float right = b->x + std::max(b->w, b->content_w);
            for (UINT32 i = 0; i < count; ++i) {
                const auto& h = hit_buf_[i];
                rt_->FillRectangle(D2D1::RectF(b->x, h.top, right, h.top + h.height),
                                   Brush(kind == TokenKind::Inserted ? kColorDiffInsertedBg : kColorDiffDeletedBg));
            }
        }
        return;
    }

    size_t i = 0;
    while (i < nr) {
        const uint32_t kind = b->runs[i].flags & (kCode | kKbd | kMark);
        if (!kind) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j + 1 < nr && (b->runs[j + 1].flags & (kCode | kKbd | kMark)) == kind) ++j;
        const uint32_t s0 = b->runs[i].start;
        const uint32_t s1 = b->runs[j].start + b->runs[j].length;
        i = j + 1;
        UINT32 count = 0;
        tl->HitTestTextRange(s0, s1 - s0, o.x, o.y, nullptr, 0, &count);
        if (!count) continue;
        hit_buf_.resize(count);
        if (FAILED(tl->HitTestTextRange(s0, s1 - s0, o.x, o.y, hit_buf_.data(), count, &count))) continue;
        // One pill per line: an inline object (a flag emoji) splits the range
        // into adjacent pieces; its box keeps the height of the text's.
        UINT32 merged = 0;
        for (UINT32 k = 0; k < count; ++k) {
            const auto& h = hit_buf_[k];
            if (merged > 0) {
                auto& prev = hit_buf_[merged - 1];
                const bool same_line = std::fabs(prev.top + prev.height - (h.top + h.height)) < 1.f;
                if (same_line && std::fabs(prev.left + prev.width - h.left) < 1.f) {
                    prev.width = h.left + h.width - prev.left;
                    if (!prev.isText && h.isText) {
                        prev.top = h.top;
                        prev.height = h.height;
                        prev.isText = TRUE;
                    }
                    continue;
                }
            }
            hit_buf_[merged++] = h;
        }
        count = merged;
        for (UINT32 k = 0; k < count; ++k) {
            const auto& h = hit_buf_[k];
            float left = h.left, right = h.left + h.width;
            if (kind & (kCode | kKbd)) {
                if (k == 0) left += 1.f;
                if (k + 1 == count) right -= 1.f;
                const float cs = size * ((kind & kKbd) ? 0.8f : 0.85f);
                const float ph = std::min(h.height, cs * ((kind & kKbd) ? 1.6f : 1.45f));
                const float cy = h.top + h.height * 0.5f + size * 0.09f;
                const D2D1_RECT_F r = D2D1::RectF(left, cy - ph * 0.5f, right, cy + ph * 0.5f);
                const float radius = std::min(m.radius, ph * 0.3f);
                if (kind & kKbd) {
                    const D2D1_ROUNDED_RECT shadow{D2D1::RectF(r.left, r.top, r.right, r.bottom + 1.f), radius, radius};
                    rt_->FillRoundedRectangle(shadow, Brush(kColorKbdBorder));
                    const D2D1_ROUNDED_RECT face{r, radius, radius};
                    rt_->FillRoundedRectangle(face, Brush(kColorKbdBg));
                    const D2D1_ROUNDED_RECT edge{D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f),
                                                 radius, radius};
                    rt_->DrawRoundedRectangle(edge, Brush(kColorKbdBorder), 1.f);
                } else {
                    rt_->FillRoundedRectangle(D2D1_ROUNDED_RECT{r, radius, radius}, Brush(kColorInlineCodeBg));
                }
            } else {
                const float inset = h.height * 0.08f;
                rt_->FillRectangle(D2D1::RectF(left, h.top + inset, right, h.top + h.height - inset),
                                   Brush(kColorMarkBg));
            }
        }
    }
}

void Renderer::DrawLeaf(Block* b) {
    LeafState* s = layout_->State(b);
    if (!s) return;
    IDWriteTextLayout* tl = layout_->TextLayout(b);
    if (!tl) return;
    const D2D1_POINT_2F o = D2D1::Point2F(s->tx, s->ty);
    DrawInlineBackgrounds(b, tl, o);

    const uint32_t a = b->text_start;
    const uint32_t e = b->text_end();
    // A formula or flag is drawn in one piece: a range touching it covers all of it.
    auto whole = [b](uint32_t& s0, uint32_t& s1) {
        for (const InlineObject& o : b->objects) {
            if (o.length > 1 && s0 < o.pos + o.length && s1 > o.pos) {
                s0 = std::min(s0, o.pos);
                s1 = std::max(s1, o.pos + o.length);
            }
        }
    };
    if (st_->hits && !st_->hits->empty()) {
        const auto& hits = *st_->hits;
        auto it = std::lower_bound(hits.begin(), hits.end(), a,
                                   [](const TextRange& r, uint32_t pos) { return r.end <= pos; });
        for (; it != hits.end() && it->start < e; ++it) {
            uint32_t s0 = std::max(it->start, a) - a;
            uint32_t s1 = std::min(it->end, e) - a;
            whole(s0, s1);
            const bool current = st_->current_hit >= 0 && &*it == &hits[static_cast<size_t>(st_->current_hit)];
            FillRanges(tl, o, s0, s1 - s0, Brush(current ? kColorSearchCurrent : kColorSearch));
        }
    }
    const TextRange& sel = st_->selection;
    if (sel.end > sel.start && sel.start < e && sel.end > a) {
        uint32_t s0 = std::max(sel.start, a) - a;
        uint32_t s1 = std::min(sel.end, e) - a;
        whole(s0, s1);
        FillRanges(tl, o, s0, s1 - s0, Brush(kColorSelection));
    }

    DrawContext& dc = layout_->draw_context();
    dc.default_brush = Brush(s->style.color);
    dc.leaf = b;
    rt_->DrawTextLayout(o, tl, dc.default_brush, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);

    // Selected images get a tinted overlay (the selection is behind them).
    if (sel.end > sel.start && !b->objects.empty()) {
        for (const InlineObject& obj : b->objects) {
            if (obj.image < 0) continue;  // formulas are text: the selection is behind them
            const uint32_t pos = a + obj.pos;
            if (pos >= sel.start && pos < sel.end) FillRanges(tl, o, obj.pos, obj.length, Brush(kColorSelection));
        }
    }
}

void Renderer::DrawCodeBlock(Block* b) {
    const Metrics& m = layout_->metrics();
    const D2D1_RECT_F box = D2D1::RectF(b->x, b->y, b->x + b->w, b->y + b->h);
    rt_->FillRoundedRectangle(D2D1_ROUNDED_RECT{box, m.radius, m.radius}, Brush(kColorCanvasSubtle));
    // With line numbers the code scrolls (horizontally) right of their column.
    const LeafState* s = layout_->State(b);
    const float gutter = s ? s->gutter : 0.f;
    D2D1_RECT_F code_clip = box;
    if (gutter > 0) code_clip.left = s->tx - layout_->GutterGap(s->style.size) * 0.5f;
    PushClip(code_clip);
    D2D1_MATRIX_3X2_F old;
    rt_->GetTransform(&old);
    if (b->scroll_x > 0) rt_->SetTransform(D2D1::Matrix3x2F::Translation(-b->scroll_x, 0) * old);
    DrawLeaf(b);
    rt_->SetTransform(old);
    PopClip();
    if (gutter > 0) DrawLineNumbers(b, *s);
    if (b->content_w > 0) DrawHScrollbar(b);

    if (st_->hover_block == b || st_->copied_block == b) {
        const D2D1_RECT_F r = CopyButtonRect(b, m);
        const float radius = m.radius;
        rt_->FillRoundedRectangle(D2D1_ROUNDED_RECT{r, radius, radius},
                                  Brush(st_->copy_button_hover ? kColorButtonHover : kColorButtonBg));
        const D2D1_ROUNDED_RECT edge{D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f),
                                     radius, radius};
        rt_->DrawRoundedRectangle(edge, Brush(kColorBorder), 1.f);
        const float isz = m.em;
        const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
        const D2D1_RECT_F ir = D2D1::RectF(cx - isz * 0.5f, cy - isz * 0.5f, cx + isz * 0.5f, cy + isz * 0.5f);
        if (st_->copied_block == b) DrawIcon(rt_, icons_, Icon::Check, ir, Brush(kColorAlertTip));
        else DrawIcon(rt_, icons_, Icon::Copy, ir, Brush(kColorMuted));
    }
}

// Numbers of the logical lines of a code block (wrapped continuations get
// none). They are drawn, not part of the text: selection, search and copying
// only ever see the code.
void Renderer::DrawLineNumbers(Block* b, const LeafState& s) {
    IDWriteTextLayout* tl = layout_->TextLayout(b);
    if (!tl) return;
    const float gap = layout_->GutterGap(s.style.size);
    const float right = s.tx - gap;
    const float sep = std::floor(s.tx - gap * 0.5f) + 0.5f;
    rt_->DrawLine(D2D1::Point2F(sep, b->y), D2D1::Point2F(sep, b->y + b->h), Brush(kColorBorderMuted), 1.f);

    // Walk the visual lines: a number goes on each one that starts a line of
    // code, on that line's own baseline; only the lines in view are drawn.
    UINT32 count = 0;
    tl->GetLineMetrics(nullptr, 0, &count);
    if (count == 0) return;
    line_buf_.resize(count);
    if (FAILED(tl->GetLineMetrics(line_buf_.data(), count, &count))) return;
    const float top = st_->scroll_y, bottom = st_->scroll_y + st_->view_h;
    ID2D1Brush* brush = Brush(kColorMuted);
    float y = s.ty;
    int number = 1;
    bool starts_line = true;
    for (UINT32 i = 0; i < count && y <= bottom; ++i) {
        const DWRITE_LINE_METRICS& lm = line_buf_[i];
        if (starts_line && y + lm.height >= top) {
            float baseline = 0;
            if (IDWriteTextLayout* nl = layout_->LineNumberLayout(number, s.style.size, &baseline)) {
                DWRITE_TEXT_METRICS tm{};
                nl->GetMetrics(&tm);
                rt_->DrawTextLayout(D2D1::Point2F(right - tm.widthIncludingTrailingWhitespace, y + lm.baseline - baseline),
                                    nl, brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
        }
        starts_line = lm.newlineLength > 0;
        if (starts_line) ++number;
        y += lm.height;
    }
}

void Renderer::DrawHScrollbar(Block* b) {
    const Metrics& m = layout_->metrics();
    const D2D1_RECT_F thumb = HScrollbarRect(b, m, true);
    const float r = (thumb.bottom - thumb.top) * 0.5f;
    rt_->FillRoundedRectangle(D2D1_ROUNDED_RECT{thumb, r, r},
                              Brush(st_->hover_block == b ? kColorScrollThumbHover : kColorScrollThumb));
}

void Renderer::DrawListItem(Block* item) {
    const Metrics& m = layout_->metrics();
    const bool footnote = item->type == BlockType::FootnoteDef;
    const float scale = (footnote || InFootnotes(item)) ? 0.75f : 1.f;
    const float em = m.em * scale;
    const float baseline = item->marker_y;
    uint8_t color = kColorText;
    for (const Block* p = item->parent; p; p = p->parent) {
        if (p->type == BlockType::Quote || p->type == BlockType::FootnoteSection) {
            color = kColorMuted;
            break;
        }
    }
    const Block* list = item->parent;
    int depth = 0;
    for (const Block* p = list ? list->parent : nullptr; p; p = p->parent) {
        if (p->type == BlockType::List) ++depth;
    }

    // Right-to-left lists have their markers on the right side.
    const bool rtl = list && list->type == BlockType::List && list->rtl;
    const float right = item->x + item->w;
    if (footnote || (list && list->type == BlockType::List && list->ordered && item->task < 0)) {
        const std::wstring num = footnote ? std::to_wstring(item->number) : ListMarkerText(item->number, depth);
        const std::wstring text = rtl ? L"." + num : num + L".";
        if (IDWriteTextLayout* tl = layout_->LabelLayout(text, em, 400)) {
            const float w = LabelWidth(tl);
            const float x = rtl ? right + em * 0.35f : item->x - em * 0.35f - w;
            rt_->DrawTextLayout(D2D1::Point2F(x, baseline - LabelBaseline(tl)), tl, Brush(color),
                                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        }
    } else if (item->task >= 0) {
        const float sz = em * 0.875f;
        const float x = rtl ? right + em * 1.45f - sz : item->x - em * 1.45f;
        const float cy = baseline - em * 0.34f;
        const D2D1_RECT_F r = D2D1::RectF(Snap(x), Snap(cy - sz * 0.5f), Snap(x + sz), Snap(cy + sz * 0.5f));
        DrawCheckbox(rt_, icons_, r, item->task == 1, Brush(kColorMuted), Brush(kColorCheckboxBg),
                     Brush(kColorCheckboxChecked), Brush(kColorCheckmark));
    } else if (list && list->type == BlockType::List) {
        const float cx = rtl ? right + em * 0.95f : item->x - em * 0.95f;
        const float cy = baseline - em * 0.3f;
        if (depth == 0) {
            const float r = em * 0.17f;
            rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), Brush(color));
        } else if (depth == 1) {
            const float r = em * 0.16f;
            rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), Brush(color), std::max(1.f, em * 0.07f));
        } else {
            const float h = em * 0.15f;
            rt_->FillRectangle(D2D1::RectF(cx - h, cy - h, cx + h, cy + h), Brush(color));
        }
    }
    DrawChildren(item);
}

void Renderer::DrawTable(Block* b) {
    const TableInfo* ti = layout_->Table(b);
    std::vector<Block*> rows;
    for (Block* c : b->children) {
        if (c->type == BlockType::TableRow) rows.push_back(c);
        else if (c->visible && c->y <= view_bottom_ && c->y + c->h >= view_top_) DrawBlock(c);
    }
    if (!ti || rows.empty()) return;

    const bool scroll = b->content_w > 0;
    D2D1_MATRIX_3X2_F old;
    rt_->GetTransform(&old);
    if (scroll) {
        PushClip(D2D1::RectF(b->x, b->y, b->x + b->w, b->y + b->h));
        rt_->SetTransform(D2D1::Matrix3x2F::Translation(-b->scroll_x, 0) * old);
    }
    const float hair = Hairline();
    const float tx = rows.front()->x;
    const float tw = ti->width;
    int body_index = 0;
    for (Block* row : rows) {
        if (!row->header) {
            ++body_index;
            if (body_index % 2 == 0 && row->y <= view_bottom_ && row->y + row->h >= view_top_) {
                rt_->FillRectangle(D2D1::RectF(tx, row->y, tx + tw, row->y + row->h), Brush(kColorCanvasSubtle));
            }
        }
    }
    rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    ID2D1Brush* border = Brush(kColorBorder);
    for (Block* row : rows) {
        if (row->y - 1.f > view_bottom_) break;
        if (row->y + row->h + 1.f < view_top_) continue;
        for (Block* cell : row->children) {
            if (!cell->visible) continue;
            const float l = Snap(cell->x - hair), t = Snap(cell->y - hair);
            const float r = Snap(cell->x + cell->w), btm = Snap(cell->y + cell->h);
            rt_->FillRectangle(D2D1::RectF(l, t, r + hair, t + hair), border);
            rt_->FillRectangle(D2D1::RectF(l, btm, r + hair, btm + hair), border);
            rt_->FillRectangle(D2D1::RectF(l, t, l + hair, btm + hair), border);
            rt_->FillRectangle(D2D1::RectF(r, t, r + hair, btm + hair), border);
        }
    }
    rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (Block* row : rows) {
        if (row->y > view_bottom_) break;
        if (row->y + row->h < view_top_) continue;
        for (Block* cell : row->children) {
            if (cell->visible) DrawChildren(cell);
        }
    }
    if (scroll) {
        rt_->SetTransform(old);
        PopClip();
        DrawHScrollbar(b);
    }
}

void Renderer::DrawAlert(Block* b) {
    const Metrics& m = layout_->metrics();
    ID2D1SolidColorBrush* color = Brush(AlertColor(b->alert));
    const float x = Snap(b->x);
    rt_->FillRectangle(D2D1::RectF(x, b->y, Snap(x + m.quote_bar), b->y + b->h), color);
    const float em = m.em;
    const float title_top = b->y + em * 0.5f;
    const float title_h = em * 1.5f;
    const float ix = b->x + m.quote_bar + em;
    const float iy = title_top + (title_h - em) * 0.5f;
    DrawIcon(rt_, icons_, AlertIcon(b->alert), D2D1::RectF(ix, iy, ix + em, iy + em), color);
    if (IDWriteTextLayout* tl = layout_->LabelLayout(AlertLabel(b->alert), em, 600)) {
        DWRITE_TEXT_METRICS tm{};
        tl->GetMetrics(&tm);
        rt_->DrawTextLayout(D2D1::Point2F(ix + em * 1.5f, title_top + (title_h - tm.height) * 0.5f), tl, color,
                            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
    DrawChildren(b);
}

}  // namespace mdv
