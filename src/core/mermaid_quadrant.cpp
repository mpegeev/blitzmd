// Mermaid quadrant charts: four quadrants with their names, points placed
// at [x, y] from 0 to 1 with their labels, the axes' names at their ends,
// a title, laid out as Mermaid's quadrantBuilder does (500 by 500).
#include <algorithm>
#include <cmath>
#include <map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
    bool markdown = false;
};

// Point styles: radius, color, stroke-color, stroke-width (-1: not set).
struct PointStyle {
    float radius = -1, stroke_width = -1;
    DgColor fill, stroke;
    void Merge(const PointStyle& o) {
        if (o.radius >= 0) radius = o.radius;
        if (o.stroke_width >= 0) stroke_width = o.stroke_width;
        if (o.fill.role != kDgNone) fill = o.fill;
        if (o.stroke.role != kDgNone) stroke = o.stroke;
    }
};

struct Point {
    Text text;
    double x = 0, y = 0;
    std::vector<std::wstring> classes;
    PointStyle style;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"quadrantChart")) c_.Fail(Tr(L"Expected \"quadrantChart\"", L"Ожидалось «quadrantChart»"));
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
    }

    Text title;
    Text x_left, x_right, y_bottom, y_top;
    Text quadrants[4];
    std::vector<Point> points;
    std::map<std::wstring, PointStyle> classes;

private:
    void Statement() {
        if (c_.EatKeyword(L"title")) {
            c_.SkipBlanks();
            title = Label(c_.WholeLine());
            return;
        }
        if (c_.EatKeyword(L"x-axis")) return Axis(&x_left, &x_right);
        if (c_.EatKeyword(L"y-axis")) return Axis(&y_bottom, &y_top);
        for (int q = 0; q < 4; ++q) {
            if (c_.EatKeyword(L"quadrant-" + std::to_wstring(q + 1))) {
                c_.SkipBlanks();
                quadrants[q] = Label(c_.LineRest());
                return;
            }
        }
        if (c_.EatKeyword(L"classDef")) {
            c_.SkipBlanks();
            const size_t start = c_.pos();
            while (!c_.AtStatementEnd() && !IsSpace(c_.Peek())) c_.Advance();
            const std::wstring name(src_.text.substr(start, c_.pos() - start));
            c_.SkipBlanks();
            classes[name].Merge(Styles(c_.LineRest()));
            return;
        }
        if (c_.EatKeyword(L"accDescr")) {
            c_.SkipBlanks();
            if (c_.Eat(L'{')) {
                while (!c_.AtEnd() && c_.Peek() != L'}') c_.Advance();
                c_.Eat(L'}');
            } else {
                c_.LineRest();
            }
            return;
        }
        if (c_.EatKeyword(L"accTitle")) {
            c_.LineRest();
            return;
        }
        // Name[:::class]: [x, y] [radius: 10, color: #f00, ...]
        Point p;
        const size_t start = c_.pos();
        if (c_.Peek() == L'"') {
            c_.Advance();
            while (!c_.AtEnd() && c_.Peek() != L'"' && c_.Peek() != L'\n') c_.Advance();
            if (!c_.Eat(L'"')) c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        } else {
            while (!c_.AtStatementEnd() && c_.Peek() != L':') c_.Advance();
        }
        p.text = Label(src_.text.substr(start, c_.pos() - start));
        while (c_.EatStr(L":::")) {
            const size_t s = c_.pos();
            while (!c_.AtEnd() && (IsWordChar(c_.Peek()) || c_.Peek() == L'-')) c_.Advance();
            p.classes.emplace_back(src_.text.substr(s, c_.pos() - s));
        }
        c_.SkipBlanks();
        if (p.text.raw.empty() || !c_.Eat(L':')) c_.Fail(Tr(L"Expected a point (Name: [x, y])", L"Ожидалась точка (Имя: [x, y])"));
        c_.SkipBlanks();
        if (!c_.Eat(L'[')) c_.Fail(Tr(L"Expected '['", L"Ожидалась «[»"));
        p.x = Number();
        c_.SkipBlanks();
        if (!c_.Eat(L',')) c_.Fail(Tr(L"Expected ','", L"Ожидалась «,»"));
        p.y = Number();
        c_.SkipBlanks();
        if (!c_.Eat(L']')) c_.Fail(Tr(L"Expected ']'", L"Ожидалась «]»"));
        c_.SkipBlanks();
        if (!c_.AtStatementEnd()) p.style = Styles(c_.LineRest());
        if (points.size() >= kMaxNodes) c_.Fail(Tr(L"Too many points", L"Слишком много точек"));
        points.push_back(std::move(p));
    }

    // x-axis Low --> High (or just Low; Low --> with nothing after shows
    // "Low ⟶", as in Mermaid)
    void Axis(Text* low, Text* high) {
        c_.SkipBlanks();
        const std::wstring_view rest = c_.LineRest();
        size_t arrow = std::wstring_view::npos, arrow_end = 0;
        for (size_t k = 0; k < rest.size(); ++k) {
            if (rest[k] == L'"') {  // (not in quoted text)
                const size_t q = rest.find(L'"', k + 1);
                if (q == std::wstring_view::npos) break;
                k = q;
                continue;
            }
            if (rest[k] != L'-') continue;
            size_t e = k;
            while (e < rest.size() && rest[e] == L'-') ++e;
            if (e - k >= 2 && e < rest.size() && rest[e] == L'>') {
                arrow = k;
                arrow_end = e + 1;
                break;
            }
            k = e;
        }
        *low = Label(rest.substr(0, arrow));
        if (arrow != std::wstring_view::npos) {
            *high = Label(rest.substr(arrow_end));
            if (high->raw.empty()) low->raw += L" \u27F6";
        }
    }

    // A value from 0 to 1 (Mermaid's lexer takes no other).
    double Number() {
        c_.SkipBlanks();
        const size_t start = c_.pos();
        while ((c_.Peek() >= L'0' && c_.Peek() <= L'9') || c_.Peek() == L'.') c_.Advance();
        const std::wstring s(src_.text.substr(start, c_.pos() - start));
        wchar_t* end = nullptr;
        const double v = s.empty() || s.size() > 30 ? -1 : std::wcstod(s.c_str(), &end);
        if (!(v >= 0 && v <= 1) || (end && *end)) {
            c_.set_pos(start);
            c_.Fail(Tr(L"Expected a value from 0 to 1", L"Ожидалось значение от 0 до 1"));
        }
        return v;
    }

    // radius: 10, color: #ff3300, stroke-color: #10f0f0, stroke-width: 5px
    static PointStyle Styles(std::wstring_view s) {
        PointStyle st;
        size_t i = 0;
        while (i < s.size()) {
            size_t e = s.find(L',', i);
            if (e == std::wstring_view::npos) e = s.size();
            const std::wstring_view item = s.substr(i, e - i);
            i = e + 1;
            const size_t colon = item.find(L':');
            if (colon == std::wstring_view::npos) continue;
            const std::wstring_view key = Trim(item.substr(0, colon)), value = Trim(item.substr(colon + 1));
            uint32_t argb = 0;
            if (key == L"radius") {
                const double r = std::wcstod(std::wstring(value).c_str(), nullptr);
                st.radius = static_cast<float>(r >= 0 && r <= 200 ? r : (r > 200 ? 200.0 : 0.0));
            } else if (key == L"stroke-width") {
                const double w = std::wcstod(std::wstring(value).c_str(), nullptr);
                st.stroke_width = static_cast<float>(w >= 0 && w <= 100 ? w : (w > 100 ? 100.0 : 0.0));
            } else if (key == L"color" && ParseCssColor(value, &argb)) {
                st.fill = Argb(argb);
            } else if (key == L"stroke-color" && ParseCssColor(value, &argb)) {
                st.stroke = Argb(argb);
            }
        }
        return st;
    }

    // Text, "quoted" or "`markdown`".
    Text Label(std::wstring_view s) {
        s = Trim(s);
        if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') s = s.substr(1, s.size() - 2);
        Text t;
        if (s.size() >= 2 && s.front() == L'`' && s.back() == L'`') {
            s = s.substr(1, s.size() - 2);
            t.markdown = true;
        }
        if (s.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        t.raw = std::wstring(s);
        t.pos = static_cast<uint32_t>(s.data() - src_.text.data());
        t.len = static_cast<uint32_t>(s.size());
        return t;
    }

    const Source& src_;
    Cursor c_;
};

// ---------------------------------------------------------------------------
// Drawing: Mermaid's quadrantChart defaults.
// ---------------------------------------------------------------------------

constexpr float kSize = 500;
constexpr float kPad = 5;                          // quadrantPadding
constexpr float kTitle = 20 + 2 * 10;              // titleFontSize + 2 titlePadding
constexpr float kAxis = 16 + 2 * 5;                // axis label font + 2 padding

std::shared_ptr<DiagramScene> Build(const Parser& p, SceneBuilder& b, const Source& src) {
    using M = SceneBuilder::Measured;
    const float unit = b.size();
    auto measure = [&](const Text& t, float px) { return b.Measure(ParseLabel(t.raw, t.markdown), 0, false, px / unit); };
    const bool own = !p.title.raw.empty();
    const Text title = own ? p.title : Text{src.title, src.title_pos, src.title_len, false};
    const bool show_title = !title.raw.empty();
    const bool show_x = !p.x_left.raw.empty() || !p.x_right.raw.empty();
    const bool show_y = !p.y_bottom.raw.empty() || !p.y_top.raw.empty();
    const bool x_top = p.points.empty();  // (Mermaid moves it down when there are points)
    const float title_space = show_title ? kTitle : 0.f;
    const float x_space = show_x ? kAxis : 0.f, y_space = show_y ? kAxis : 0.f;
    const float left = kPad + y_space, top = kPad + (x_top ? x_space : 0.f) + title_space;
    const float w = kSize - 2 * kPad - y_space, h = kSize - 2 * kPad - x_space - title_space;
    const float hw = w / 2, hh = h / 2;
    // Quadrants: 1 top right, 2 top left, 3 bottom left, 4 bottom right.
    const D2D1_RECT_F q[4] = {D2D1::RectF(left + hw, top, left + w, top + hh), D2D1::RectF(left, top, left + hw, top + hh),
                              D2D1::RectF(left, top + hh, left + hw, top + h), D2D1::RectF(left + hw, top + hh, left + w, top + h)};
    for (int i = 0; i < 4; ++i) b.Rect(q[i], 0, Role(static_cast<uint8_t>(kDgQuadrant + i)), Role(kDgNone));
    const DgColor border = Role(kDgQuadrantBorder);
    b.Begin(D2D1::Point2F(left + hw, top));
    b.LineTo(D2D1::Point2F(left + hw, top + h));
    b.MoveTo(D2D1::Point2F(left, top + hh));
    b.LineTo(D2D1::Point2F(left + w, top + hh));
    b.End(Role(kDgNone), border, 1.f);
    b.Rect(D2D1::RectF(left, top, left + w, top + h), 0, Role(kDgNone), border, 2.f);
    // Quadrant names: at the top of each with points, else in the middle.
    for (int i = 0; i < 4; ++i) {
        if (p.quadrants[i].raw.empty()) continue;
        const M m = measure(p.quadrants[i], 16);
        const float cx = (q[i].left + q[i].right) / 2;
        const float y = p.points.empty() ? (q[i].top + q[i].bottom) / 2 - m.h / 2 : q[i].top + 5;
        b.Text(m, D2D1::Point2F(cx - m.w / 2, y), Role(kDgText), p.quadrants[i].pos, p.quadrants[i].len);
    }
    // Axis names: in the middle of each half when both ends are named, else
    // from the start.
    const float x_y = x_top ? title_space + 5 : top + h + kPad + 3;
    float bottom = kSize;
    auto x_label = [&](const Text& t, float x0, bool middle) {
        if (t.raw.empty()) return;
        const M m = measure(t, 16);
        b.Text(m, D2D1::Point2F(middle ? x0 - m.w / 2 : x0, x_y), Role(kDgText), t.pos, t.len);
        bottom = std::max(bottom, x_y + m.h + 2);
    };
    const bool x_mid = !p.x_right.raw.empty(), y_mid = !p.y_top.raw.empty();
    x_label(p.x_left, left + (x_mid ? hw / 2 : 0.f), x_mid);
    x_label(p.x_right, left + hw + (x_mid ? hw / 2 : 0.f), x_mid);
    auto y_label = [&](const Text& t, float y0, bool middle) {
        if (t.raw.empty()) return;
        const M m = measure(t, 16);
        // Turned to read upwards: m.h wide, m.w high.
        b.TextUp(m, D2D1::Point2F(5, middle ? y0 - m.w / 2 : y0 - m.w), Role(kDgText), t.pos, t.len);
    };
    y_label(p.y_bottom, top + h - (y_mid ? hh / 2 : 0.f), y_mid);
    y_label(p.y_top, top + hh - (y_mid ? hh / 2 : 0.f), y_mid);
    if (show_title) {
        const M m = measure(title, 20);
        b.Text(m, D2D1::Point2F(kSize / 2 - m.w / 2, 10), Role(kDgText), title.pos, title.len);
    }
    // Points, their names under them. (Mermaid's default point color is an
    // invalid one that shows black: the theme's text color here.)
    for (const Point& pt : p.points) {
        PointStyle st;
        for (const std::wstring& c : pt.classes) {
            if (auto it = p.classes.find(c); it != p.classes.end()) st.Merge(it->second);
        }
        st.Merge(pt.style);
        const float x = left + static_cast<float>(pt.x) * w, y = top + h - static_cast<float>(pt.y) * h;
        const float r = st.radius >= 0 ? st.radius : 5.f;
        const DgColor fill = st.fill.role != kDgNone ? st.fill : Role(kDgText);
        const float sw = st.stroke_width > 0 ? st.stroke_width : 0.f;
        b.Ellipse(D2D1::RectF(x - r, y - r, x + r, y + r), fill, sw > 0 ? (st.stroke.role != kDgNone ? st.stroke : fill) : Role(kDgNone), sw);
        const M m = measure(pt.text, 12);
        b.Text(m, D2D1::Point2F(x - m.w / 2, y + 5), Role(kDgText), pt.text.pos, pt.text.len);
        bottom = std::max(bottom, y + 5 + m.h);
    }
    return b.Finish(kSize, bottom);
}

}  // namespace

std::shared_ptr<DiagramScene> QuadrantChart(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Build(p, *builder, src);
}

}  // namespace mdv::mermaid
