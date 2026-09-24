// Mermaid pie charts: slices clockwise from the top in the order given, in
// the theme's pie colors, their percentages on them, a legend beside (with
// the values under showData) and a title above.
#include <algorithm>
#include <cmath>
#include <cwchar>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Slice {
    std::wstring label;
    uint32_t pos = 0, len = 0;  // the label in the source
    double value = 0;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"pie")) c_.Fail(Tr(L"Expected \"pie\"", L"Ожидалось «pie»"));
        c_.SkipBlanks();
        if (c_.EatKeyword(L"showData")) show_data = true;
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
    }

    bool show_data = false;
    std::wstring title;
    uint32_t title_pos = 0, title_len = 0;
    std::vector<Slice> slices;

private:
    void Statement() {
        if (c_.EatKeyword(L"showData")) {
            show_data = true;
            return;
        }
        if (c_.EatKeyword(L"title")) {
            c_.SkipBlanks();
            const std::wstring_view t = c_.LineRest();
            title = std::wstring(t);
            title_pos = static_cast<uint32_t>(t.data() - src_.text.data());
            title_len = static_cast<uint32_t>(t.size());
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
        // "label" : value
        if (c_.Peek() != L'"') c_.Fail(Tr(L"Expected a \"label\" : value", L"Ожидалось «\"подпись\" : значение»"));
        const size_t open = c_.pos();
        c_.Advance();
        const size_t start = c_.pos();
        while (!c_.AtEnd() && c_.Peek() != L'"' && c_.Peek() != L'\n') c_.Advance();
        if (c_.Peek() != L'"') {
            c_.set_pos(open);
            c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        }
        Slice s;
        s.label = std::wstring(src_.text.substr(start, c_.pos() - start));
        s.pos = static_cast<uint32_t>(start);
        s.len = static_cast<uint32_t>(s.label.size());
        if (s.label.size() > kMaxLabel) c_.Fail(Tr(L"Label too long", L"Слишком длинная подпись"));
        c_.Advance();
        c_.SkipBlanks();
        if (!c_.Eat(L':')) c_.Fail(Tr(L"Expected ':'", L"Ожидалось «:»"));
        c_.SkipBlanks();
        const std::wstring number(c_.LineRest());
        wchar_t* end = nullptr;
        s.value = std::wcstod(number.c_str(), &end);
        if (number.empty() || !end || *end || !std::isfinite(s.value)) c_.Fail(Tr(L"Expected a number", L"Ожидалось число"));
        if (s.value < 0) c_.Fail(Tr(L"A slice can't be negative", L"Доля не может быть отрицательной"));
        if (slices.size() >= kMaxNodes) c_.Fail(Tr(L"Too many slices", L"Слишком много долей"));
        slices.push_back(std::move(s));
    }

    const Source& src_;
    Cursor c_;
};

constexpr float kRadius = 150;
constexpr float kMargin = 20;
constexpr float kLegendBox = 18, kLegendGap = 4;
constexpr float kPi = 3.14159265f;

// A slice: the center, the arc from a0 to a1 (radians, clockwise from the
// top) in cubic pieces of at most a quarter turn.
void AddSlice(SceneBuilder& b, D2D1_POINT_2F c, float r, float a0, float a1) {
    auto at = [&](float a) { return D2D1::Point2F(c.x + r * std::sin(a), c.y - r * std::cos(a)); };
    const bool whole = a1 - a0 >= 2 * kPi - 1e-4f;
    if (whole) {
        b.Begin(at(a0));
    } else {
        b.Begin(c);
        b.LineTo(at(a0));
    }
    const int pieces = std::max(1, static_cast<int>(std::ceil((a1 - a0) / (kPi / 2))));
    const float step = (a1 - a0) / static_cast<float>(pieces);
    const float k = 4.f / 3.f * std::tan(step / 4) * r;
    for (int i = 0; i < pieces; ++i) {
        const float s = a0 + step * static_cast<float>(i), e = s + step;
        const D2D1_POINT_2F p0 = at(s), p1 = at(e);
        // Tangents, clockwise.
        b.CubicTo(D2D1::Point2F(p0.x + k * std::cos(s), p0.y + k * std::sin(s)),
                  D2D1::Point2F(p1.x - k * std::cos(e), p1.y - k * std::sin(e)), p1);
    }
    b.Close();
}

std::wstring Number(double v) {
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%.2f", v);
    std::wstring s(buf);
    while (!s.empty() && s.back() == L'0') s.pop_back();
    if (!s.empty() && s.back() == L'.') s.pop_back();
    return s;
}

std::shared_ptr<DiagramScene> Build(const Parser& p, SceneBuilder& b, const Source& src) {
    using M = SceneBuilder::Measured;
    double sum = 0;
    for (const Slice& s : p.slices) sum += s.value;
    // The title: the pie's own, or the front matter's (25px, as Mermaid's).
    const bool own = !p.title.empty();
    const std::wstring& title_text = own ? p.title : src.title;
    M title;
    if (!title_text.empty()) title = b.Measure(ParseLabel(title_text, false), 0, false, 1.5625f);
    // Legend: a colored square and the label (with its value under showData).
    std::vector<M> legend;
    float legend_w = 0;
    for (const Slice& s : p.slices) {
        RichText t = ParseLabel(s.label, false);
        if (p.show_data) t.text += L" [" + Number(s.value) + L"]";
        legend.push_back(b.Measure(t, 0, false, 1.0625f, false));
        legend_w = std::max(legend_w, legend.back().w);
    }
    const float row = std::max(kLegendBox, legend.empty() ? 0.f : legend.front().h) + kLegendGap;
    const float legend_h = row * static_cast<float>(legend.size());
    const float top = kMargin + (title.layout ? title.h + 12 : 0.f);
    const float pie_h = 2 * kRadius + 4;
    const float body_h = std::max(pie_h, legend_h);
    const D2D1_POINT_2F c = D2D1::Point2F(kMargin + kRadius + 2, top + body_h / 2);
    const float legend_x = c.x + kRadius + 36;
    const float width = std::max(legend.empty() ? c.x + kRadius + 2 : legend_x + kLegendBox + 6 + legend_w, title.w) + kMargin;
    const float height = top + body_h + kMargin;
    if (title.layout) {
        b.Text(title, D2D1::Point2F((width - title.w) / 2, kMargin), Role(kDgText), own ? p.title_pos : src.title_pos,
               own ? p.title_len : src.title_len);
    }
    const DgColor black = Argb(0xFF000000);
    // Slices; those that round to 0% are left out, as Mermaid does.
    if (sum > 0) {
        float a = 0;
        for (size_t i = 0; i < p.slices.size(); ++i) {
            const double share = p.slices[i].value / sum;
            if (std::lround(share * 100) == 0) continue;
            const float a1 = a + static_cast<float>(share) * 2 * kPi;
            AddSlice(b, c, kRadius, a, std::min(a1, 2 * kPi));
            b.End(Role(static_cast<uint8_t>(kDgCategory + i % 12)), black, 2.f);
            a = a1;
        }
        // The percentages on them, three quarters out.
        a = 0;
        for (size_t i = 0; i < p.slices.size(); ++i) {
            const double share = p.slices[i].value / sum;
            const long pct = std::lround(share * 100);
            const float a1 = a + static_cast<float>(share) * 2 * kPi;
            if (pct == 0) continue;
            const float mid = (a + a1) / 2;
            a = a1;
            RichText t;
            t.text = std::to_wstring(pct) + L"%";
            const M m = b.Measure(t, 0, false, 1.0625f);
            const D2D1_POINT_2F at = D2D1::Point2F(c.x + 0.75f * kRadius * std::sin(mid), c.y - 0.75f * kRadius * std::cos(mid));
            b.Text(m, D2D1::Point2F(at.x - m.w / 2, at.y - m.h / 2), Role(kDgText));
        }
    }
    // The rim.
    b.Ellipse(D2D1::RectF(c.x - kRadius - 1, c.y - kRadius - 1, c.x + kRadius + 1, c.y + kRadius + 1), Role(kDgNone), black, 2.f);
    // The legend, beside the middle of the pie.
    float y = c.y - legend_h / 2;
    for (size_t i = 0; i < legend.size(); ++i) {
        const DgColor color = Role(static_cast<uint8_t>(kDgCategory + i % 12));
        b.Rect(D2D1::RectF(legend_x, y, legend_x + kLegendBox, y + kLegendBox), 0, color, color);
        const M& m = legend[i];
        b.Text(m, D2D1::Point2F(legend_x + kLegendBox + 6, y + (kLegendBox - m.h) / 2), Role(kDgText), p.slices[i].pos, p.slices[i].len);
        y += row;
    }
    return b.Finish(width, height);
}

}  // namespace

std::shared_ptr<DiagramScene> PieChart(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Build(p, *builder, src);
}

}  // namespace mdv::mermaid
