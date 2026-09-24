// Mermaid XY charts (xychart-beta): bars and lines over a category or
// number axis, a number axis for the values (given or from the data), axis
// titles, a title, a legend for named plots; vertical or horizontal. Laid
// out as Mermaid's xychart orchestrator does (700 by 500).
#include <algorithm>
#include <charconv>
#include <cmath>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

struct Axis {
    Text title;
    std::vector<Text> categories;  // a category axis, else numbers
    bool range = false;            // min --> max given
    double min = 0, max = 0;
};

struct Plot {
    bool bar = false;
    Text name;
    std::vector<double> values;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"xychart-beta") && !c_.EatKeyword(L"xychart")) c_.Fail(Tr(L"Expected \"xychart-beta\"", L"Ожидалось «xychart-beta»"));
        c_.SkipBlanks();
        if (c_.EatKeyword(L"horizontal")) horizontal = true;
        else c_.EatKeyword(L"vertical");
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
    }

    bool horizontal = false;
    Text title;
    Axis x, y;
    std::vector<Plot> plots;

private:
    void Statement() {
        if (c_.EatKeyword(L"title")) {
            c_.SkipBlanks();
            title = c_.Peek() == L'"' ? Quoted() : Here(c_.LineRest());
            return;
        }
        if (c_.EatStr(L"x-axis")) return AxisStmt(&x, true);
        if (c_.EatStr(L"y-axis")) return AxisStmt(&y, false);
        for (const wchar_t* kw : {L"accTitle", L"accDescr"}) {
            if (c_.EatKeyword(kw)) {
                c_.LineRest();
                return;
            }
        }
        Plot p;
        if (c_.EatKeyword(L"bar")) p.bar = true;
        else if (!c_.EatKeyword(L"line")) c_.Fail(Tr(L"Expected bar, line or an axis", L"Ожидалось bar, line или ось"));
        c_.SkipBlanks();
        if (c_.Peek() == L'"') p.name = Quoted();
        else if (c_.Peek() != L'[') p.name = Word();
        c_.SkipBlanks();
        if (!c_.Eat(L'[')) c_.Fail(Tr(L"Expected '['", L"Ожидалась «[»"));
        for (;;) {
            c_.SkipBlanks();
            if (c_.Eat(L']')) break;
            if (!p.values.empty() && !c_.Eat(L',')) c_.Fail(Tr(L"Expected ',' or ']'", L"Ожидалась «,» или «]»"));
            c_.SkipBlanks();
            p.values.push_back(Number());
            if (++values_ > kMaxNodes) c_.Fail(Tr(L"Too many values", L"Слишком много значений"));
        }
        if (plots.size() >= kMaxEdges) c_.Fail(Tr(L"Too many plots", L"Слишком много графиков"));
        plots.push_back(std::move(p));
    }

    // x-axis ["title"] [a, "b c"] | x-axis [title] min --> max | y-axis title
    void AxisStmt(Axis* a, bool categories_ok) {
        c_.SkipBlanks();
        if (c_.Peek() == L'"') a->title = Quoted();
        else if (c_.Peek() != L'[' && !StartsNumber()) a->title = Word();
        c_.SkipBlanks();
        if (categories_ok && c_.Eat(L'[')) {
            for (;;) {
                c_.SkipBlanks();
                if (c_.Eat(L']')) break;
                if (!a->categories.empty() && !c_.Eat(L',')) c_.Fail(Tr(L"Expected ',' or ']'", L"Ожидалась «,» или «]»"));
                c_.SkipBlanks();
                if (c_.Peek() == L'"') {
                    a->categories.push_back(Quoted());
                } else {
                    const size_t start = c_.pos();
                    while (!c_.AtStatementEnd() && c_.Peek() != L',' && c_.Peek() != L']') c_.Advance();
                    a->categories.push_back(Here(Trim(src_.text.substr(start, c_.pos() - start))));
                }
                if (a->categories.size() > kMaxNodes) c_.Fail(Tr(L"Too many categories", L"Слишком много категорий"));
            }
            return;
        }
        if (StartsNumber()) {
            a->min = Number();
            c_.SkipBlanks();
            if (!c_.EatStr(L"-->")) c_.Fail(Tr(L"Expected \"-->\"", L"Ожидалось «-->»"));
            c_.SkipBlanks();
            a->max = Number();
            a->range = true;
        }
    }

    bool StartsNumber() const {
        const wchar_t ch = c_.Peek(), next = c_.Peek(1);
        return (ch >= L'0' && ch <= L'9') || ((ch == L'-' || ch == L'.') && ((next >= L'0' && next <= L'9') || next == L'.'));
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

    // A title of words, up to '[' or a number.
    Text Word() {
        const size_t start = c_.pos();
        while (!c_.AtStatementEnd() && c_.Peek() != L'[' && !(IsSpace(c_.Peek()) && IsNumberAhead())) c_.Advance();
        return Here(Trim(src_.text.substr(start, c_.pos() - start)));
    }
    bool IsNumberAhead() const {
        size_t k = 1;
        while (c_.Peek(k) == L' ' || c_.Peek(k) == L'\t') ++k;
        const wchar_t ch = c_.Peek(k);
        return (ch >= L'0' && ch <= L'9') || ch == L'-';
    }

    Text Quoted() {
        const size_t open = c_.pos();
        c_.Advance();
        const size_t start = c_.pos();
        while (!c_.AtEnd() && c_.Peek() != L'"' && c_.Peek() != L'\n') c_.Advance();
        if (c_.Peek() != L'"') {
            c_.set_pos(open);
            c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        }
        const Text t = Here(src_.text.substr(start, c_.pos() - start));
        c_.Advance();
        return t;
    }

    Text Here(std::wstring_view s) {
        if (s.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return Text{std::wstring(s), static_cast<uint32_t>(s.data() - src_.text.data()), static_cast<uint32_t>(s.size())};
    }

    const Source& src_;
    Cursor c_;
    size_t values_ = 0;
};

// ---------------------------------------------------------------------------
// Scales
// ---------------------------------------------------------------------------

// d3's ticks(start, stop, 10).
std::vector<double> NiceTicks(double start, double stop) {
    std::vector<double> out;
    if (!(stop > start) || !std::isfinite(stop - start)) return {start};
    const double step = (stop - start) / 10, power = std::floor(std::log10(step)), error = step / std::pow(10.0, power);
    const double factor = error >= std::sqrt(50.0) ? 10 : error >= std::sqrt(10.0) ? 5 : error >= std::sqrt(2.0) ? 2 : 1;
    if (power < 0) {
        const double inc = std::pow(10.0, -power) / factor;
        double i1 = std::round(start * inc), i2 = std::round(stop * inc);
        if (i1 / inc < start) ++i1;
        if (i2 / inc > stop) --i2;
        for (double i = i1; i <= i2 && out.size() < 100; ++i) out.push_back(i / inc);
    } else {
        const double inc = std::pow(10.0, power) * factor;
        double i1 = std::round(start / inc), i2 = std::round(stop / inc);
        if (i1 * inc < start) ++i1;
        if (i2 * inc > stop) --i2;
        for (double i = i1; i <= i2 && out.size() < 100; ++i) out.push_back(i * inc);
    }
    return out;
}

// A number as JavaScript writes it (3, 0.5, 1e+21).
std::wstring JsNumber(double v) {
    if (v == 0) return L"0";
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::scientific);
    const std::string sci(buf, r.ptr);
    const size_t e = sci.find('e');
    const bool neg = sci[0] == '-';
    std::string digits = sci.substr(neg ? 1 : 0, e - (neg ? 1 : 0));
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
    const int exp = std::atoi(sci.c_str() + e + 1);
    std::string out;
    if (exp >= -6 && exp < 21) {
        const size_t n = static_cast<size_t>(std::max(exp, 0)) + 1;
        if (exp >= 0) out = digits.size() > n ? digits.substr(0, n) + "." + digits.substr(n) : digits + std::string(n - digits.size(), '0');
        else out = "0." + std::string(static_cast<size_t>(-exp - 1), '0') + digits;
    } else {
        out = digits.substr(0, 1) + (digits.size() > 1 ? "." + digits.substr(1) : "") + (exp < 0 ? "e-" : "e+") + std::to_string(std::abs(exp));
    }
    if (neg) out.insert(out.begin(), '-');
    return std::wstring(out.begin(), out.end());
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

using M = SceneBuilder::Measured;

constexpr float kW = 700, kH = 500;
constexpr float kTick = 5, kLine = 2, kPad = 5;

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src) {}

    std::shared_ptr<DiagramScene> Build() {
        size_t n = 0;
        for (const Plot& pl : p_.plots) n = std::max(n, pl.values.size());
        const bool band = !p_.x.categories.empty();
        // The value axis: given, or from the data.
        double vmin = p_.y.min, vmax = p_.y.max;
        if (!p_.y.range) {
            vmin = 1e300;
            vmax = -1e300;
            for (const Plot& pl : p_.plots) {
                for (double v : pl.values) {
                    vmin = std::min(vmin, v);
                    vmax = std::max(vmax, v);
                }
            }
            if (vmin > vmax) vmin = vmax = 0;
        }
        if (vmax == vmin) {
            vmin -= 1;
            vmax += 1;
        }
        // The other axis: categories, a range, or 1 to n.
        double xmin = p_.x.range ? p_.x.min : 1, xmax = p_.x.range ? p_.x.max : static_cast<double>(std::max<size_t>(n, 1));
        if (xmax == xmin) xmax = xmin + 1;
        const std::vector<double> vticks = NiceTicks(std::min(vmin, vmax), std::max(vmin, vmax));
        const std::vector<double> xticks = band ? std::vector<double>{} : NiceTicks(std::min(xmin, xmax), std::max(xmin, xmax));
        // Labels.
        const float unit = b_.size();
        auto measure = [&](const std::wstring& s, float px) {
            RichText t;
            t.text = s;
            return b_.Measure(t, 0, false, px / unit);
        };
        std::vector<M> cat_labels, vlabels, xlabels;
        if (band) {
            for (const Text& c : p_.x.categories) cat_labels.push_back(b_.Measure(ParseLabel(c.raw, false), 0, false, 14 / unit));
        }
        for (double t : vticks) vlabels.push_back(measure(JsNumber(t), 14));
        for (double t : xticks) xlabels.push_back(measure(JsNumber(t), 14));
        const std::vector<M>& xl = band ? cat_labels : xlabels;
        auto widest = [](const std::vector<M>& v) {
            float w = 0, h = 0;
            for (const M& m : v) {
                w = std::max(w, m.w);
                h = std::max(h, m.h);
            }
            return std::make_pair(w, h);
        };
        const auto [xl_w, xl_h] = widest(xl);
        const auto [vl_w, vl_h] = widest(vlabels);
        const Text title = !p_.title.raw.empty() ? p_.title : Text{src_.title, src_.title_pos, src_.title_len};
        M title_m, xt, vt;
        if (!title.raw.empty()) title_m = b_.Measure(ParseLabel(title.raw, false), 0, false, 20 / unit);
        if (!p_.x.title.raw.empty()) xt = b_.Measure(ParseLabel(p_.x.title.raw, false), 0, false, 16 / unit);
        if (!p_.y.title.raw.empty()) vt = b_.Measure(ParseLabel(p_.y.title.raw, false), 0, false, 16 / unit);
        // A legend for named plots, on the right.
        std::vector<M> names;
        float legend_w = 0;
        for (const Plot& pl : p_.plots) {
            names.push_back(pl.name.raw.empty() ? M{} : b_.Measure(ParseLabel(pl.name.raw, false), 0, false, 14 / unit, false));
            if (names.back().layout) legend_w = std::max(legend_w, names.back().w + 36);
        }
        const float title_h = title_m.layout ? title_m.h + 20 : 0.f;
        const bool vertical = !p_.horizontal;
        // The category (x) axis's depth and the value axis's, then the plot.
        auto depth = [&](float label, bool has_title, const M& t) { return kLine + kTick + label + 2 * kPad + (has_title ? t.h + 2 * kPad : 0.f); };
        const float x_depth = depth(vertical ? xl_h : xl_w, xt.layout != nullptr, xt);
        const float v_depth = depth(vertical ? vl_w : vl_h, vt.layout != nullptr, vt);
        D2D1_RECT_F plot;
        if (vertical) plot = D2D1::RectF(v_depth, title_h, kW - legend_w, kH - x_depth);
        else plot = D2D1::RectF(x_depth, title_h + v_depth, kW - legend_w, kH);
        // Ranges along the axes, inset (as Mermaid's outer padding): the
        // value axis by half a label, the category axis to fit its bars.
        const bool bars = std::any_of(p_.plots.begin(), p_.plots.end(), [](const Plot& pl) { return pl.bar; });
        const float along = vertical ? plot.right - plot.left : plot.bottom - plot.top;
        const size_t count = band ? p_.x.categories.size() : xticks.size();
        float outer = std::min((vertical ? xl_w : xl_h) / 2, 0.2f * along);
        float tick_distance = count > 0 ? (along - 2 * outer) / static_cast<float>(count) : along;
        if (bars && 0.7f * tick_distance > 2 * outer) {
            outer = std::floor(0.7f * tick_distance / 2);
            tick_distance = count > 0 ? (along - 2 * outer) / static_cast<float>(count) : along;
        }
        const float bar_w = std::min(2 * outer, tick_distance) * 0.95f;
        const float v_inset = (vertical ? vl_h : vl_w) / 2;
        // Position along the category axis (pixels from its start).
        auto cat_at = [&](double i) {  // i: a category's index
            const size_t k = std::max<size_t>(p_.x.categories.size(), 1);
            return k == 1 ? along / 2 : outer + static_cast<float>(i) * (along - 2 * outer) / static_cast<float>(k - 1);
        };
        auto num_at = [&](double v) {
            return outer + static_cast<float>((v - xmin) / (xmax - xmin)) * (along - 2 * outer);
        };
        const float vlen = (vertical ? plot.bottom - plot.top : plot.right - plot.left) - 2 * v_inset;
        auto val_at = [&](double v) {  // from the axis's low end
            return v_inset + static_cast<float>((v - vmin) / (vmax - vmin)) * vlen;
        };
        // To scene points: `a` along the category axis, `v` along the values.
        auto point = [&](float a, float v) {
            return vertical ? D2D1::Point2F(plot.left + a, plot.bottom - v) : D2D1::Point2F(plot.left + v, plot.top + a);
        };
        const DgColor ink = Role(kDgText);
        // Plots, in the palette's colors in turn.
        for (size_t k = 0; k < p_.plots.size(); ++k) {
            const Plot& pl = p_.plots[k];
            const DgColor color = Role(static_cast<uint8_t>(kDgPlot + k % 10));
            const size_t shown = band ? std::min(pl.values.size(), p_.x.categories.size()) : pl.values.size();
            auto a_of = [&](size_t i) {
                if (band) return cat_at(static_cast<double>(i));
                const double step = pl.values.size() > 1 ? (xmax - xmin) / static_cast<double>(pl.values.size() - 1) : 0;
                return num_at(xmin + step * static_cast<double>(i));
            };
            if (pl.bar) {
                for (size_t i = 0; i < shown; ++i) {
                    const float a = a_of(i), v = std::max(0.f, val_at(pl.values[i]));
                    const D2D1_POINT_2F p0 = point(a - bar_w / 2, 0), p1 = point(a + bar_w / 2, v);
                    b_.Rect(D2D1::RectF(std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::max(p0.x, p1.x), std::max(p0.y, p1.y)), 0, color,
                            Role(kDgNone));
                }
            } else if (shown > 0) {
                b_.Begin(point(a_of(0), val_at(pl.values[0])));
                for (size_t i = 1; i < shown; ++i) b_.LineTo(point(a_of(i), val_at(pl.values[i])));
                b_.End(Role(kDgNone), color, 2.f);
            }
        }
        // The axes: lines, ticks, labels, titles.
        const DgColor none = Role(kDgNone);
        if (vertical) {
            b_.Begin(D2D1::Point2F(plot.left, plot.bottom + kLine / 2));
            b_.LineTo(D2D1::Point2F(plot.right, plot.bottom + kLine / 2));
            b_.MoveTo(D2D1::Point2F(plot.left - kLine / 2, plot.top));
            b_.LineTo(D2D1::Point2F(plot.left - kLine / 2, plot.bottom));
        } else {
            b_.Begin(D2D1::Point2F(plot.left - kLine / 2, plot.top));
            b_.LineTo(D2D1::Point2F(plot.left - kLine / 2, plot.bottom));
            b_.MoveTo(D2D1::Point2F(plot.left, plot.top - kLine / 2));
            b_.LineTo(D2D1::Point2F(plot.right, plot.top - kLine / 2));
        }
        b_.End(none, ink, kLine);
        // The category axis: below the plot (left of it, turned).
        for (size_t i = 0; i < xl.size(); ++i) {
            const float a = band ? cat_at(static_cast<double>(i)) : num_at(xticks[i]);
            const M& m = xl[i];
            const Text* src = band ? &p_.x.categories[i] : nullptr;
            if (vertical) {
                const float x = plot.left + a, y = plot.bottom + kLine;
                Tick(D2D1::Point2F(x, y), D2D1::Point2F(x, y + kTick), ink);
                b_.Text(m, D2D1::Point2F(x - m.w / 2, y + kTick + kPad), ink, src ? src->pos : 0, src ? src->len : 0);
            } else {
                const float x = plot.left - kLine, y = plot.top + a;
                Tick(D2D1::Point2F(x, y), D2D1::Point2F(x - kTick, y), ink);
                b_.Text(m, D2D1::Point2F(x - kTick - kPad - m.w, y - m.h / 2), ink, src ? src->pos : 0, src ? src->len : 0);
            }
        }
        for (size_t i = 0; i < vlabels.size(); ++i) {
            const float v = val_at(vticks[i]);
            const M& m = vlabels[i];
            if (vertical) {
                const float x = plot.left - kLine, y = plot.bottom - v;
                Tick(D2D1::Point2F(x, y), D2D1::Point2F(x - kTick, y), ink);
                b_.Text(m, D2D1::Point2F(x - kTick - kPad - m.w, y - m.h / 2), ink);
            } else {
                const float x = plot.left + v, y = plot.top - kLine;
                Tick(D2D1::Point2F(x, y), D2D1::Point2F(x, y - kTick), ink);
                b_.Text(m, D2D1::Point2F(x - m.w / 2, y - kTick - kPad - m.h), ink);
            }
        }
        if (xt.layout) {
            if (vertical) {
                b_.Text(xt, D2D1::Point2F((plot.left + plot.right) / 2 - xt.w / 2, kH - kPad - xt.h), ink, p_.x.title.pos, p_.x.title.len);
            } else {
                b_.TextUp(xt, D2D1::Point2F(kPad, (plot.top + plot.bottom) / 2 - xt.w / 2), ink, p_.x.title.pos, p_.x.title.len);
            }
        }
        if (vt.layout) {
            if (vertical) {
                b_.TextUp(vt, D2D1::Point2F(kPad, (plot.top + plot.bottom) / 2 - vt.w / 2), ink, p_.y.title.pos, p_.y.title.len);
            } else {
                b_.Text(vt, D2D1::Point2F((plot.left + plot.right) / 2 - vt.w / 2, title_h + kPad), ink, p_.y.title.pos, p_.y.title.len);
            }
        }
        if (title_m.layout) b_.Text(title_m, D2D1::Point2F(kW / 2 - title_m.w / 2, 10), ink, title.pos, title.len);
        // The legend: a line or a square in the plot's color, its name.
        float ly = (kH - static_cast<float>(std::count_if(names.begin(), names.end(), [](const M& m) { return m.layout != nullptr; })) * 22) / 2;
        for (size_t k = 0; k < names.size(); ++k) {
            if (!names[k].layout) continue;
            const DgColor color = Role(static_cast<uint8_t>(kDgPlot + k % 10));
            const float lx = kW - legend_w + 12, cy = ly + 11;
            if (p_.plots[k].bar) {
                b_.Rect(D2D1::RectF(lx, cy - 5, lx + 12, cy + 5), 0, color, Role(kDgNone));
            } else {
                b_.Begin(D2D1::Point2F(lx, cy));
                b_.LineTo(D2D1::Point2F(lx + 12, cy));
                b_.End(none, color, 2.f);
            }
            b_.Text(names[k], D2D1::Point2F(lx + 18, cy - names[k].h / 2), ink, p_.plots[k].name.pos, p_.plots[k].name.len);
            ly += 22;
        }
        return b_.Finish(kW, kH);
    }

private:
    void Tick(D2D1_POINT_2F a, D2D1_POINT_2F z, DgColor ink) {
        b_.Begin(a);
        b_.LineTo(z);
        b_.End(Role(kDgNone), ink, kLine);
    }

    const Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
};

}  // namespace

std::shared_ptr<DiagramScene> XyChart(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder, src).Build();
}

}  // namespace mdv::mermaid
