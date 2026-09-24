// Mermaid pie charts: slices clockwise from the top in the order given, in
// the theme's pie colors, their percentages on them, a legend beside (with
// the values under showData) and a title above.
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cwchar>
#include <unordered_set>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Slice {
    std::wstring label;         // unescaped
    uint32_t pos = 0, len = 0;  // the label in the source
    double value = 0;
    uint32_t value_pos = 0, value_len = 0;
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
            // (Up to the line's end or a %% comment, ';' and all.)
            const size_t start = c_.pos();
            while (!c_.AtEnd() && c_.Peek() != L'\n' && !(c_.Peek() == L'%' && c_.Peek(1) == L'%')) c_.Advance();
            const std::wstring_view t = Trim(src_.text.substr(start, c_.pos() - start));
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
        // "label" : value (or 'label', with \" and \' inside)
        const wchar_t quote = c_.Peek();
        if (quote != L'"' && quote != L'\'') c_.Fail(Tr(L"Expected a \"label\" : value", L"Ожидалось «\"подпись\" : значение»"));
        const size_t open = c_.pos();
        c_.Advance();
        const size_t start = c_.pos();
        Slice s;
        while (!c_.AtEnd() && c_.Peek() != quote && c_.Peek() != L'\n') {
            wchar_t ch = c_.Peek();
            if (ch == L'\\' && c_.Peek(1) != L'\n' && c_.Peek(1) != L'\0') {
                c_.Advance();
                ch = c_.Peek() == L'n' ? L'\n' : c_.Peek() == L't' ? L'\t' : c_.Peek();
            }
            s.label.push_back(ch);
            c_.Advance();
        }
        if (c_.Peek() != quote) {
            c_.set_pos(open);
            c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        }
        s.pos = static_cast<uint32_t>(start);
        s.len = static_cast<uint32_t>(c_.pos() - start);
        if (s.label.size() > kMaxLabel) c_.Fail(Tr(L"Label too long", L"Слишком длинная подпись"));
        c_.Advance();
        c_.SkipBlanks();
        if (!c_.Eat(L':')) c_.Fail(Tr(L"Expected ':'", L"Ожидалось «:»"));
        c_.SkipBlanks();
        // A number as Mermaid reads it: digits, a fraction.
        const size_t number = c_.pos();
        if (c_.Peek() == L'-') c_.Fail(Tr(L"A slice can't be negative", L"Доля не может быть отрицательной"));
        while (c_.Peek() >= L'0' && c_.Peek() <= L'9') c_.Advance();
        if (c_.Peek() == L'.') {
            c_.Advance();
            while (c_.Peek() >= L'0' && c_.Peek() <= L'9') c_.Advance();
        }
        const std::wstring digits(src_.text.substr(number, c_.pos() - number));
        if (digits.empty() || digits == L"." || digits.size() > 400) c_.Fail(Tr(L"Expected a number", L"Ожидалось число"));
        s.value = std::wcstod(digits.c_str(), nullptr);
        if (!std::isfinite(s.value)) c_.Fail(Tr(L"Expected a number", L"Ожидалось число"));
        s.value_pos = static_cast<uint32_t>(number);
        s.value_len = static_cast<uint32_t>(digits.size());
        // A label again: Mermaid keeps the first.
        if (!labels_.insert(s.label).second) return;
        if (slices.size() >= kMaxNodes) c_.Fail(Tr(L"Too many slices", L"Слишком много долей"));
        slices.push_back(std::move(s));
    }

    const Source& src_;
    Cursor c_;
    std::unordered_set<std::wstring> labels_;
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

// A value as JavaScript writes numbers: the shortest digits that read
// back, plainly from 1e-7 to 1e21, else as 1.5e+21.
std::wstring Number(double v) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::scientific);
    const std::string sci(buf, r.ptr);  // d[.ddd]e+XX
    const size_t e = sci.find('e');
    std::string digits = sci.substr(0, e);
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
    const int exp = std::atoi(sci.c_str() + e + 1);
    std::string out;
    if (exp >= -7 + 1 && exp < 21) {
        if (exp >= 0) {
            out = digits.size() > static_cast<size_t>(exp) + 1 ? digits.substr(0, static_cast<size_t>(exp) + 1) + "." + digits.substr(static_cast<size_t>(exp) + 1)
                                                            : digits + std::string(static_cast<size_t>(exp) + 1 - digits.size(), '0');
        } else {
            out = "0." + std::string(static_cast<size_t>(-exp - 1), '0') + digits;
        }
    } else {
        out = digits.substr(0, 1) + (digits.size() > 1 ? "." + digits.substr(1) : "") + (exp < 0 ? "e-" : "e+") + std::to_string(std::abs(exp));
    }
    return std::wstring(out.begin(), out.end());
}

std::shared_ptr<DiagramScene> Build(const Parser& p, SceneBuilder& b, const Source& src) {
    using M = SceneBuilder::Measured;
    double sum = 0;
    for (const Slice& s : p.slices) sum += s.value;
    // Slices under 1% are left out (the others fill the circle), as Mermaid
    // does; percentages are of the whole.
    std::vector<size_t> shown;
    double shown_sum = 0;
    for (size_t i = 0; i < p.slices.size(); ++i) {
        if (sum > 0 && p.slices[i].value / sum * 100 >= 1) {
            shown.push_back(i);
            shown_sum += p.slices[i].value;
        }
    }
    // The title: the pie's own, or the front matter's (25px, as Mermaid's).
    const bool own = !p.title.empty();
    const std::wstring& title_text = own ? p.title : src.title;
    M title;
    if (!title_text.empty()) title = b.Measure(ParseLabel(title_text, false), 0, false, 1.5625f);
    // Legend: a colored square, the label, its value under showData.
    std::vector<M> legend, values;
    float legend_w = 0;
    for (const Slice& s : p.slices) {
        legend.push_back(b.Measure(ParseLabel(s.label, false), 0, false, 1.0625f, false));
        RichText v;
        if (p.show_data) v.text = L" [" + Number(s.value) + L"]";
        values.push_back(p.show_data ? b.Measure(v, 0, false, 1.0625f, false) : M{});
        legend_w = std::max(legend_w, legend.back().w + values.back().w);
    }
    const float row = std::max(kLegendBox, legend.empty() ? 0.f : legend.front().h) + kLegendGap;
    const float legend_h = row * static_cast<float>(legend.size());
    const float top = kMargin + (title.layout ? title.h + 12 : 0.f);
    const float pie_h = 2 * kRadius + 4;
    const float body_h = std::max(pie_h, legend_h);
    const D2D1_POINT_2F c = D2D1::Point2F(kMargin + std::max(0.f, title.w / 2 - kRadius) + kRadius + 2, top + body_h / 2);
    const float legend_x = c.x + kRadius + 36;
    const float width = std::max(legend.empty() ? c.x + kRadius + 2 : legend_x + kLegendBox + 6 + legend_w, c.x + title.w / 2) + kMargin;
    const float height = top + body_h + kMargin;
    // (Centered over the pie, as in Mermaid.)
    if (title.layout) {
        b.Text(title, D2D1::Point2F(c.x - title.w / 2, kMargin), Role(kDgText), own ? p.title_pos : src.title_pos,
               own ? p.title_len : src.title_len);
    }
    const DgColor black = Argb(0xFF000000);
    if (shown_sum > 0) {
        float a = 0;
        for (size_t i : shown) {
            const float a1 = a + static_cast<float>(p.slices[i].value / shown_sum) * 2 * kPi;
            AddSlice(b, c, kRadius, a, std::min(a1, 2 * kPi));
            b.End(Role(static_cast<uint8_t>(kDgCategory + i % 12)), black, 2.f);
            a = a1;
        }
        // The percentages on them, three quarters out.
        a = 0;
        for (size_t i : shown) {
            const float a1 = a + static_cast<float>(p.slices[i].value / shown_sum) * 2 * kPi;
            const float mid = (a + a1) / 2;
            a = a1;
            RichText t;
            t.text = std::to_wstring(std::lround(p.slices[i].value / sum * 100)) + L"%";
            const M m = b.Measure(t, 0, false, 1.0625f);
            const D2D1_POINT_2F at = D2D1::Point2F(c.x + 0.75f * kRadius * std::sin(mid), c.y - 0.75f * kRadius * std::cos(mid));
            b.Text(m, D2D1::Point2F(at.x - m.w / 2, at.y - m.h / 2), Role(kDgText));
        }
    }
    // The rim.
    b.Ellipse(D2D1::RectF(c.x - kRadius - 1, c.y - kRadius - 1, c.x + kRadius + 1, c.y + kRadius + 1), Role(kDgNone), black, 2.f);
    // The legend, beside the middle of the pie, in the slices' colors at
    // full strength.
    float y = c.y - legend_h / 2;
    for (size_t i = 0; i < legend.size(); ++i) {
        const DgColor color = Role(static_cast<uint8_t>(kDgCategorySolid + i % 12));
        b.Rect(D2D1::RectF(legend_x, y, legend_x + kLegendBox, y + kLegendBox), 0, color, color);
        const M& m = legend[i];
        const Slice& s = p.slices[i];
        const float tx = legend_x + kLegendBox + 6, ty = y + (kLegendBox - m.h) / 2;
        b.Text(m, D2D1::Point2F(tx, ty), Role(kDgText), s.pos, s.len);
        if (values[i].layout) b.Text(values[i], D2D1::Point2F(tx + m.w, ty), Role(kDgText), s.value_pos, s.value_len);
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
