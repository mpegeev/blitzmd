// Mermaid timelines: time periods in a row (in a column with "timeline TD"),
// each with its events stacked below (beside), grouped in sections, in the
// theme's cScale colors: a period and its events in one color, the events a
// little brighter; a thick arrow along the time.
#include <algorithm>
#include <cmath>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

struct Period {
    Text text;
    std::vector<Text> events;
    int section = -1;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    // As Mermaid's lexer reads it: a period is text up to ':' or '#', an
    // event ": text" up to the next ": ", a section's name ends at ':' too.
    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"timeline")) c_.Fail(Tr(L"Expected \"timeline\"", L"Ожидалось «timeline»"));
        c_.SkipBlanks();
        if (c_.EatKeyword(L"TD") || c_.EatKeyword(L"TB")) vertical = true;
        else c_.EatKeyword(L"LR");
        for (;;) {
            Skip();
            if (c_.AtEnd()) break;
            if (TitleAhead()) {
                c_.SkipBlanks();
                title = Here(c_.WholeLine());
            } else if (c_.EatKeyword(L"accTitle")) {
                c_.WholeLine();
            } else if (c_.EatKeyword(L"accDescr")) {
                c_.SkipBlanks();
                if (c_.Eat(L'{')) {
                    while (!c_.AtEnd() && c_.Peek() != L'}') c_.Advance();
                    c_.Eat(L'}');
                } else {
                    c_.WholeLine();
                }
            } else if (c_.EatKeyword(L"section")) {
                c_.SkipBlanks();
                if (sections.size() >= kMaxNodes) c_.Fail(Tr(L"Too many sections", L"Слишком много разделов"));
                sections.push_back(Here(Until(false, true)));
            } else if (c_.Peek() == L':') {
                c_.Advance();
                if (!IsSpace(c_.Peek()) && !c_.AtEnd()) c_.Fail(Tr(L"Expected a space after ':'", L"Ожидался пробел после «:»"));
                const Text event = Here(Until(true));
                if (!periods.empty() && !event.raw.empty()) {
                    if (++events_ > kMaxNodes) c_.Fail(Tr(L"Too many events", L"Слишком много событий"));
                    periods.back().events.push_back(event);
                }
            } else {
                Period p;
                p.text = Here(Until(false));
                p.section = static_cast<int>(sections.size()) - 1;
                if (p.text.raw.empty()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
                if (periods.size() >= kMaxEdges) c_.Fail(Tr(L"Too many periods", L"Слишком много периодов"));
                periods.push_back(std::move(p));
            }
        }
    }

    bool vertical = false;
    Text title;
    std::vector<Text> sections;
    std::vector<Period> periods;

private:
    // "title" then a blank and text (else "title: x" is a period, as in
    // Mermaid's lexer).
    bool TitleAhead() {
        const size_t at = c_.pos();
        if (c_.EatKeyword(L"title") && (c_.Peek() == L' ' || c_.Peek() == L'\t') && c_.Peek(1) != L'\n' && c_.Peek(1) != L'\0') return true;
        c_.set_pos(at);
        return false;
    }

    // Blanks, line ends, %% and # comments.
    void Skip() {
        for (;;) {
            c_.SkipStatementGap();
            if (c_.Peek() != L'#') return;
            c_.SkipToLineEnd();
        }
    }

    // Up to the line's end or ':' (an event: ':' then a space; a period up
    // to '#' too).
    std::wstring_view Until(bool event, bool section = false) {
        const size_t start = c_.pos();
        while (!c_.AtEnd() && c_.Peek() != L'\n') {
            if (c_.Peek() == L':' && (!event || IsSpace(c_.Peek(1)))) break;
            if (!event && !section && c_.Peek() == L'#') break;
            c_.Advance();
        }
        return Trim(src_.text.substr(start, c_.pos() - start));
    }

    Text Here(std::wstring_view s) {
        if (s.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return Text{std::wstring(s), static_cast<uint32_t>(s.data() - src_.text.data()), static_cast<uint32_t>(s.size())};
    }

    const Source& src_;
    Cursor c_;
    size_t events_ = 0;
};

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

using M = SceneBuilder::Measured;

constexpr float kMargin = 20;
constexpr float kWrap = 150;             // text in a box wraps at this
constexpr float kScale = 0.875f;
constexpr float kLineGap = 50;           // from the arrow's start to the first box

// A box as Mermaid draws timeline nodes: rounded at the top, square at the
// bottom, a thicker line in the inverse color along the bottom.
void AddNode(SceneBuilder& b, const D2D1_RECT_F& r, int color, DgColor fill) {
    const float rd = std::min(5.f, (r.bottom - r.top) / 2), k = rd * 0.45f;
    b.Begin(D2D1::Point2F(r.left, r.bottom));
    b.LineTo(D2D1::Point2F(r.left, r.top + rd));
    b.CubicTo(D2D1::Point2F(r.left, r.top + k), D2D1::Point2F(r.left + k, r.top), D2D1::Point2F(r.left + rd, r.top));
    b.LineTo(D2D1::Point2F(r.right - rd, r.top));
    b.CubicTo(D2D1::Point2F(r.right - k, r.top), D2D1::Point2F(r.right, r.top + k), D2D1::Point2F(r.right, r.top + rd));
    b.LineTo(D2D1::Point2F(r.right, r.bottom));
    b.Close();
    b.End(fill, Role(kDgNone));
    b.Begin(D2D1::Point2F(r.left, r.bottom));
    b.LineTo(D2D1::Point2F(r.right, r.bottom));
    b.End(Role(kDgNone), Role(static_cast<uint8_t>(kDgScaleInv + color % 12)), 3.f);
}

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src) {}

    std::shared_ptr<DiagramScene> Build() {
        const bool own = !p_.title.raw.empty();
        const std::wstring& title_text = own ? p_.title.raw : src_.title;
        if (!title_text.empty()) title_ = b_.Measure(ParseLabel(title_text, false), 0, true, 1.75f, false);
        // Colors: with sections, a section's; else each period its own.
        for (size_t i = 0; i < p_.periods.size(); ++i) {
            const int s = p_.periods[i].section;
            colors_.push_back(s >= 0 ? s : static_cast<int>(i));
        }
        top_ = kMargin + (title_.layout ? title_.h + 20 : 0.f);
        Place();
        return p_.vertical ? Vertical() : Horizontal();
    }

private:
    // The order things go in: with sections, each one and its periods (an
    // empty one taking a period's place; periods before the first section
    // left out), as Mermaid does; else the periods.
    void Place() {
        const size_t n = p_.periods.size();
        slot_.assign(n, -1);
        int next = 0;
        size_t i = 0;
        if (p_.sections.empty()) {
            for (; i < n; ++i) {
                slot_[i] = next++;
                order_.push_back(static_cast<int>(i));
            }
        } else {
            while (i < n && p_.periods[i].section < 0) ++i;
            for (size_t s = 0; s < p_.sections.size(); ++s) {
                const int first = next;
                order_.push_back(-1 - static_cast<int>(s));
                for (; i < n && p_.periods[i].section == static_cast<int>(s); ++i) {
                    slot_[i] = next++;
                    order_.push_back(static_cast<int>(i));
                }
                next = std::max(next, first + 1);
                spans_.push_back({first, next - first});
            }
        }
        slots_ = next;
    }

    M Measure(const Text& t, float wrap = kWrap) { return b_.Measure(ParseLabel(t.raw, false), wrap, false, kScale); }

    void Box(const D2D1_RECT_F& r, int color, bool event, const M& m, const Text& t, bool top_text) {
        AddNode(b_, r, color, Role(static_cast<uint8_t>((event ? kDgScaleBright : kDgScale) + color % 12)));
        const float y = top_text ? r.top + 16 : (r.top + r.bottom) / 2 - m.h / 2;
        b_.Text(m, D2D1::Point2F((r.left + r.right) / 2 - m.w / 2, y), Role(static_cast<uint8_t>(kDgScaleText + color % 12)), t.pos, t.len);
    }

    void Title(float width) {
        if (!title_.layout) return;
        const bool own = !p_.title.raw.empty();
        b_.Text(title_, D2D1::Point2F(std::max(kMargin, (width - title_.w) / 2), kMargin), Role(kDgText),
                own ? p_.title.pos : src_.title_pos, own ? p_.title.len : src_.title_len);
    }

    // Periods side by side under their sections, events stacked below the
    // arrow, a dashed line down through each period's events.
    std::shared_ptr<DiagramScene> Horizontal() {
        constexpr float kW = 190, kStep = 200;  // a box, and the next one's start
        constexpr float kEventGap = 12;
        const float x0 = kMargin + kLineGap;
        std::vector<M> period_text, section_text;
        std::vector<std::vector<M>> event_text;
        float period_h = 0, section_h = 0;
        for (size_t i = 0; i < p_.periods.size(); ++i) {
            period_text.emplace_back();
            event_text.emplace_back();
            if (slot_[i] < 0) continue;
            const Period& p = p_.periods[i];
            period_text.back() = Measure(p.text);
            period_h = std::max(period_h, period_text.back().h);
            for (const Text& e : p.events) event_text.back().push_back(Measure(e));
        }
        for (size_t s = 0; s < p_.sections.size(); ++s) {
            section_text.push_back(Measure(p_.sections[s], kStep * static_cast<float>(spans_[s].second) - 50));
            section_h = std::max(section_h, section_text.back().h);
        }
        period_h += 47;
        section_h += 47;
        const bool sections = !p_.sections.empty();
        const float section_y = top_, period_y = top_ + (sections ? section_h + 52 : 0.f);
        const float line_y = period_y + period_h + 52, event_y = line_y + 82;
        float events_bottom = event_y;
        for (const auto& events : event_text) {
            float y = event_y;
            for (const M& m : events) y += m.h + 29 + kEventGap;
            events_bottom = std::max(events_bottom, y - kEventGap);
        }
        const float dash_end = events_bottom + 66;
        const float right = x0 + static_cast<float>(std::max(slots_, 1)) * kStep - (kStep - kW);
        const float width = std::max(right + 60 + kMargin, title_.w + 2 * kMargin);
        const float height = dash_end + 10 + kMargin;
        Title(width);
        // Sections over their periods.
        for (size_t s = 0; s < spans_.size(); ++s) {
            const auto [first, count] = spans_[s];
            const D2D1_RECT_F r = D2D1::RectF(x0 + static_cast<float>(first) * kStep, section_y,
                                              x0 + static_cast<float>(first + count - 1) * kStep + kW, section_y + section_h);
            Box(r, static_cast<int>(s), false, section_text[s], p_.sections[s], true);
        }
        // The arrow along the time.
        const DgColor line = Role(kDgLine);
        b_.Begin(D2D1::Point2F(kMargin, line_y));
        b_.LineTo(D2D1::Point2F(right + 50, line_y));
        b_.End(Role(kDgNone), line, 4.f);
        AddMarker(b_, Marker::Arrow, D2D1::Point2F(right + 60, line_y), D2D1::Point2F(kMargin, line_y), line, 2.f);
        for (size_t i = 0; i < p_.periods.size(); ++i) {
            if (slot_[i] < 0) continue;
            const Period& p = p_.periods[i];
            const float x = x0 + static_cast<float>(slot_[i]) * kStep, cx = x + kW / 2;
            // The dashed line down, with an arrowhead at its end.
            b_.Begin(D2D1::Point2F(cx, period_y + period_h));
            b_.LineTo(D2D1::Point2F(cx, dash_end - 6));
            b_.End(Role(kDgNone), line, 2.f, 1);
            AddMarker(b_, Marker::Arrow, D2D1::Point2F(cx, dash_end), D2D1::Point2F(cx, period_y), line, 1.f);
            Box(D2D1::RectF(x, period_y, x + kW, period_y + period_h), colors_[i], false, period_text[i], p.text, true);
            float y = event_y;
            for (size_t k = 0; k < p.events.size(); ++k) {
                const M& m = event_text[i][k];
                Box(D2D1::RectF(x, y, x + kW, y + m.h + 29), colors_[i], true, m, p.events[k], false);
                y += m.h + 29 + kEventGap;
            }
        }
        return b_.Finish(width, height);
    }

    // The same turned: periods down the left, their events to the right of
    // the arrow, sections as bands across.
    std::shared_ptr<DiagramScene> Vertical() {
        constexpr float kPeriodW = 210, kEventW = 310, kGap = 40;
        const float left = kMargin, line_x = left + kPeriodW + kGap / 2, events_x = left + kPeriodW + kGap;
        const float right = events_x + kEventW;
        const DgColor line = Role(kDgLine);
        float y = top_ + 10;
        struct Row {
            float y, h;
        };
        std::vector<std::pair<size_t, Row>> rows;
        // First where everything goes, then the arrow under it all, then the boxes.
        std::vector<std::pair<D2D1_RECT_F, int>> section_boxes;
        for (const int item : order_) {
            if (item < 0) {
                const int section = -1 - item;
                const M m = Measure(p_.sections[static_cast<size_t>(section)], 0);
                section_boxes.push_back({D2D1::RectF(left, y, right, y + m.h + 20), section});
                y += m.h + 20 + 24;
                continue;
            }
            const size_t i = static_cast<size_t>(item);
            const Period& p = p_.periods[i];
            float events_h = 0;
            for (const Text& e : p.events) events_h += Measure(e, kEventW - 40).h + 20 + 8;
            const float h = std::max(Measure(p.text, kPeriodW - 40).h + 20, events_h - 8);
            rows.push_back({i, Row{y, h}});
            y += h + 30;
        }
        const float bottom = y;
        const float width = std::max(right + kMargin, title_.w + 2 * kMargin), height = bottom + 16 + kMargin;
        Title(width);
        b_.Begin(D2D1::Point2F(line_x, top_));
        b_.LineTo(D2D1::Point2F(line_x, bottom));
        b_.End(Role(kDgNone), line, 4.f);
        AddMarker(b_, Marker::Arrow, D2D1::Point2F(line_x, bottom + 10), D2D1::Point2F(line_x, top_), line, 2.f);
        for (const auto& [r, s] : section_boxes) {
            Box(r, s, false, Measure(p_.sections[static_cast<size_t>(s)], 0), p_.sections[static_cast<size_t>(s)], false);
        }
        for (const auto& [i, row] : rows) {
            const Period& p = p_.periods[i];
            const M pm = Measure(p.text, kPeriodW - 40);
            Box(D2D1::RectF(left, row.y, left + kPeriodW, row.y + pm.h + 20), colors_[i], false, pm, p.text, false);
            float ey = row.y;
            for (const Text& e : p.events) {
                const M m = Measure(e, kEventW - 40);
                const float cy = ey + (m.h + 20) / 2;
                b_.Begin(D2D1::Point2F(line_x + 4, cy));
                b_.LineTo(D2D1::Point2F(events_x, cy));
                b_.End(Role(kDgNone), line, 2.f, 1);
                Box(D2D1::RectF(events_x, ey, right, ey + m.h + 20), colors_[i], true, m, e, false);
                ey += m.h + 20 + 8;
            }
        }
        return b_.Finish(width, height);
    }

    const Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
    M title_;
    std::vector<int> colors_;  // per period
    std::vector<int> slot_;    // per period: its place in the row, or -1 if not drawn
    std::vector<int> order_;   // periods (>= 0) and sections (-1 - index) in order
    std::vector<std::pair<int, int>> spans_;  // per section: its first place and how many
    int slots_ = 0;
    float top_ = 0;
};

}  // namespace

std::shared_ptr<DiagramScene> Timeline(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder, src).Build();
}

}  // namespace mdv::mermaid
