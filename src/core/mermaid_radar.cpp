// Mermaid radar charts: axes out from the middle, rings (or polygons) for
// the scale, each curve a closed smooth shape through its values in the
// theme's cScale colors, a legend, a title; laid out as Mermaid's radar
// renderer does (600 by 600 inside margins of 50).
#include <algorithm>
#include <cmath>
#include <map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

struct Axis {
    std::wstring id;
    Text label;
};

struct Curve {
    Text label;
    std::vector<double> values;                     // by position
    std::vector<std::pair<std::wstring, double>> named;  // or by axis id
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"radar-beta") && !c_.EatKeyword(L"radar")) c_.Fail(Tr(L"Expected \"radar-beta\"", L"Ожидалось «radar-beta»"));
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        // Values named by axis: in the axes' order (missing: 0).
        for (Curve& c : curves) {
            if (c.named.empty()) continue;
            c.values.assign(axes.size(), 0);
            for (const auto& [id, v] : c.named) {
                for (size_t k = 0; k < axes.size(); ++k) {
                    if (axes[k].id == id) c.values[k] = v;
                }
            }
        }
    }

    Text title;
    std::vector<Axis> axes;
    std::vector<Curve> curves;
    bool has_max = false, has_min = false, polygon = false, legend = true;
    double max = 0, min = 0;
    int ticks = 5;

private:
    void Statement() {
        if (c_.EatKeyword(L"title")) {
            c_.SkipBlanks();
            title = Here(c_.LineRest());
            return;
        }
        if (c_.EatKeyword(L"axis")) {
            for (;;) {
                c_.SkipBlanks();
                Axis a;
                a.label = Id(&a.id);
                if (axes.size() >= 500) c_.Fail(Tr(L"Too many axes", L"Слишком много осей"));
                axes.push_back(std::move(a));
                c_.SkipBlanks();
                if (!c_.Eat(L',')) return;
            }
        }
        if (c_.EatKeyword(L"curve")) {
            for (;;) {
                c_.SkipBlanks();
                Curve cv;
                std::wstring id;
                cv.label = Id(&id);
                c_.SkipBlanks();
                if (!c_.Eat(L'{')) c_.Fail(Tr(L"Expected '{'", L"Ожидалась «{»"));
                for (;;) {
                    c_.SkipBlanks();
                    if (c_.Eat(L'}')) break;
                    if ((!cv.values.empty() || !cv.named.empty()) && !c_.Eat(L',')) c_.Fail(Tr(L"Expected ',' or '}'", L"Ожидалась «,» или «}»"));
                    c_.SkipBlanks();
                    if (IsWordChar(c_.Peek()) && !(c_.Peek() >= L'0' && c_.Peek() <= L'9')) {
                        std::wstring axis;
                        Id(&axis);
                        c_.SkipBlanks();
                        if (!c_.Eat(L':')) c_.Fail(Tr(L"Expected ':'", L"Ожидалось «:»"));
                        c_.SkipBlanks();
                        cv.named.push_back({axis, Number()});
                    } else {
                        cv.values.push_back(Number());
                    }
                    if (cv.values.size() + cv.named.size() > 500) c_.Fail(Tr(L"Too many values", L"Слишком много значений"));
                }
                if (curves.size() >= 100) c_.Fail(Tr(L"Too many curves", L"Слишком много кривых"));
                curves.push_back(std::move(cv));
                c_.SkipBlanks();
                if (!c_.Eat(L',')) return;
            }
        }
        if (c_.EatKeyword(L"max")) {
            c_.SkipBlanks();
            max = Number();
            has_max = true;
            return;
        }
        if (c_.EatKeyword(L"min")) {
            c_.SkipBlanks();
            min = Number();
            has_min = true;
            return;
        }
        if (c_.EatKeyword(L"ticks")) {
            c_.SkipBlanks();
            ticks = static_cast<int>(std::clamp(Number(), 1.0, 50.0));
            return;
        }
        if (c_.EatKeyword(L"graticule")) {
            c_.SkipBlanks();
            if (c_.EatKeyword(L"polygon")) polygon = true;
            else if (!c_.EatKeyword(L"circle")) c_.Fail(Tr(L"Expected circle or polygon", L"Ожидалось circle или polygon"));
            return;
        }
        if (c_.EatKeyword(L"showLegend")) {
            c_.SkipBlanks();
            if (c_.EatKeyword(L"false")) legend = false;
            else if (!c_.EatKeyword(L"true")) c_.Fail(Tr(L"Expected true or false", L"Ожидалось true или false"));
            return;
        }
        for (const wchar_t* kw : {L"accTitle", L"accDescr"}) {
            if (c_.EatKeyword(kw)) {
                c_.LineRest();
                return;
            }
        }
        c_.Fail(Tr(L"Expected axis, curve, max, min, ticks, graticule or showLegend", L"Ожидалось axis, curve, max, min, ticks, graticule или showLegend"));
    }

    // id, id["Label"] (the label: the id itself when there is none).
    Text Id(std::wstring* id) {
        const size_t start = c_.pos();
        while (IsWordChar(c_.Peek()) || c_.Peek() == L'-') c_.Advance();
        if (c_.pos() == start) c_.Fail(Tr(L"Expected a name", L"Ожидалось имя"));
        const Text name = Here(src_.text.substr(start, c_.pos() - start));
        *id = name.raw;
        if (!c_.Eat(L'[')) return name;
        c_.SkipBlanks();
        Text label;
        if (c_.Eat(L'"')) {
            const size_t s = c_.pos();
            while (!c_.AtEnd() && c_.Peek() != L'"' && c_.Peek() != L'\n') c_.Advance();
            if (!c_.Eat(L'"')) c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
            label = Here(src_.text.substr(s, c_.pos() - 1 - s));
        } else {
            const size_t s = c_.pos();
            while (!c_.AtStatementEnd() && c_.Peek() != L']') c_.Advance();
            label = Here(Trim(src_.text.substr(s, c_.pos() - s)));
        }
        c_.SkipBlanks();
        if (!c_.Eat(L']')) c_.Fail(Tr(L"Expected ']'", L"Ожидалась «]»"));
        return label;
    }

    double Number() {
        const size_t start = c_.pos();
        if (c_.Peek() == L'-' || c_.Peek() == L'+') c_.Advance();
        while ((c_.Peek() >= L'0' && c_.Peek() <= L'9') || c_.Peek() == L'.') c_.Advance();
        const std::wstring s(src_.text.substr(start, c_.pos() - start));
        wchar_t* end = nullptr;
        const double v = s.size() > 40 ? NAN : std::wcstod(s.c_str(), &end);
        if (s.empty() || !end || *end || !std::isfinite(v) || std::abs(v) > 1e15) {
            c_.set_pos(start);
            c_.Fail(Tr(L"Expected a number", L"Ожидалось число"));
        }
        return v;
    }

    Text Here(std::wstring_view s) {
        if (s.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return Text{std::wstring(s), static_cast<uint32_t>(s.data() - src_.text.data()), static_cast<uint32_t>(s.size())};
    }

    const Source& src_;
    Cursor c_;
};

using M = SceneBuilder::Measured;

constexpr float kSize = 600, kMargin = 50, kPi = 3.14159265f;

std::shared_ptr<DiagramScene> Build(const Parser& p, SceneBuilder& b, const Source& src) {
    const float unit = b.size();
    const float radius = kSize / 2;
    const D2D1_POINT_2F c = D2D1::Point2F(kMargin + radius, kMargin + radius);
    const size_t n = p.axes.size();
    double max = p.max, min = p.has_min ? p.min : 0;
    if (!p.has_max) {
        max = min;
        for (const Curve& cv : p.curves) {
            for (double v : cv.values) max = std::max(max, v);
        }
    }
    if (max <= min) max = min + 1;
    auto angle = [&](size_t k) { return 2 * kPi * static_cast<float>(k) / static_cast<float>(std::max<size_t>(n, 1)) - kPi / 2; };
    auto at = [&](size_t k, float r) { return D2D1::Point2F(c.x + r * std::cos(angle(k)), c.y + r * std::sin(angle(k))); };
    // The scale: rings (or polygons), each lightly filled.
    const DgColor ring = Argb(0xFFDEDEDE), ring_fill = Argb(0x4DDEDEDE);
    for (int t = p.ticks; t >= 1; --t) {
        const float r = radius * static_cast<float>(t) / static_cast<float>(p.ticks);
        if (p.polygon && n >= 3) {
            std::vector<D2D1_POINT_2F> pts;
            for (size_t k = 0; k < n; ++k) pts.push_back(at(k, r));
            b.Polygon(pts, ring_fill, ring, 1.f);
        } else {
            b.Ellipse(D2D1::RectF(c.x - r, c.y - r, c.x + r, c.y + r), ring_fill, ring, 1.f);
        }
    }
    // Axes and their names.
    for (size_t k = 0; k < n; ++k) {
        b.Begin(c);
        b.LineTo(at(k, radius));
        b.End(Role(kDgNone), Role(kDgLine), 2.f);
        const M m = b.Measure(ParseLabel(p.axes[k].label.raw, false), 0, false, 12 / unit);
        const D2D1_POINT_2F q = at(k, radius * 1.05f);
        const float ca = std::cos(angle(k));
        const float x = std::abs(ca) < 0.1f ? q.x - m.w / 2 : ca > 0 ? q.x : q.x - m.w;
        b.Text(m, D2D1::Point2F(x, q.y - m.h / 2), Role(kDgText), p.axes[k].label.pos, p.axes[k].label.len);
    }
    // Curves: closed cardinal splines (d3's, tension 0.17), see-through.
    for (size_t i = 0; i < p.curves.size() && n > 0; ++i) {
        const Curve& cv = p.curves[i];
        std::vector<D2D1_POINT_2F> pts;
        for (size_t k = 0; k < n; ++k) {
            const double v = k < cv.values.size() ? cv.values[k] : 0;
            const float r = radius * static_cast<float>(std::clamp((v - min) / (max - min), 0.0, 1.0));
            pts.push_back(at(k, r));
        }
        const DgColor color = Role(static_cast<uint8_t>(kDgScale + i % 12));
        const float k = (1 - 0.17f) / 6;
        const size_t m = pts.size();
        b.Begin(pts[0]);
        for (size_t j = 0; j < m; ++j) {
            const D2D1_POINT_2F p0 = pts[(j + m - 1) % m], p1 = pts[j], p2 = pts[(j + 1) % m], p3 = pts[(j + 2) % m];
            b.CubicTo(D2D1::Point2F(p1.x + (p2.x - p0.x) * k, p1.y + (p2.y - p0.y) * k), D2D1::Point2F(p2.x - (p3.x - p1.x) * k, p2.y - (p3.y - p1.y) * k),
                      p2);
        }
        b.Close();
        b.End(RoleAt(static_cast<uint8_t>(kDgScale + i % 12), 128), color, 2.f);
    }
    // The legend, top right.
    if (p.legend) {
        float y = kMargin + 20;
        for (size_t i = 0; i < p.curves.size(); ++i) {
            const uint8_t role = static_cast<uint8_t>(kDgScale + i % 12);
            const float x = kMargin + kSize * 0.95f;
            b.Rect(D2D1::RectF(x, y, x + 12, y + 12), 0, RoleAt(role, 128), Role(role), 1.f);
            const Text& label = p.curves[i].label;
            const M m = b.Measure(ParseLabel(label.raw, false), 0, false, 12 / unit, false);
            b.Text(m, D2D1::Point2F(x + 18, y + 6 - m.h / 2), Role(kDgText), label.pos, label.len);
            y += 20;
        }
    }
    const Text title = !p.title.raw.empty() ? p.title : Text{src.title, src.title_pos, src.title_len};
    if (!title.raw.empty()) {
        const M m = b.Measure(ParseLabel(title.raw, false), 0, false, 1.f);
        b.Text(m, D2D1::Point2F(c.x - m.w / 2, 4), Role(kDgText), title.pos, title.len);
    }
    return b.FinishFitted(4);
}

}  // namespace

std::shared_ptr<DiagramScene> RadarChart(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Build(p, *builder, src);
}

}  // namespace mdv::mermaid
