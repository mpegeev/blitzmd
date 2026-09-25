// Mermaid treemaps: a tree by indentation ("Section" / "Leaf": value), laid
// out as d3's squarified treemap (1000 by 400, biggest first, padded as
// Mermaid pads it): sections with their name and total in a header, leaves
// with their name and value as big as fits, in the theme's cScale colors
// handed out by name as Mermaid's ordinal scales do; classDef styles.
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Node {
    std::wstring text;
    uint32_t pos = 0, len = 0;
    double value = 0;       // a leaf's own, a section's sum
    bool leaf = false;
    int parent = -1, indent = 0;
    std::wstring cls;       // :::class
    std::vector<int> children;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"treemap-beta") && !c_.EatKeyword(L"treemap")) c_.Fail(Tr(L"Expected \"treemap-beta\"", L"Ожидалось «treemap-beta»"));
        nodes.push_back(Node{});  // the root
        nodes[0].indent = -1;
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            size_t line = c_.pos();
            while (line > 0 && src_.text[line - 1] != L'\n') --line;
            const int indent = static_cast<int>(c_.pos() - line);  // (a tab counts one, as in Mermaid)
            if (c_.EatKeyword(L"classDef")) {
                c_.SkipBlanks();
                const size_t s = c_.pos();
                while (IsWordChar(c_.Peek())) c_.Advance();
                const std::wstring name(src_.text.substr(s, c_.pos() - s));
                if (name.empty()) c_.Fail(Tr(L"Expected a class name", L"Ожидалось имя класса"));
                c_.SkipBlanks();
                classes[name].Apply(c_.LineRest());
                continue;
            }
            if (c_.EatKeyword(L"title")) {
                c_.SkipBlanks();
                const std::wstring_view t = c_.WholeLine();
                title = std::wstring(t);
                title_pos = static_cast<uint32_t>(t.data() - src_.text.data());
                title_len = static_cast<uint32_t>(t.size());
                continue;
            }
            if (c_.EatKeyword(L"accDescr")) {
                c_.SkipBlanks();
                if (c_.Eat(L'{')) {
                    while (!c_.AtEnd() && c_.Peek() != L'}') c_.Advance();
                    c_.Eat(L'}');
                } else {
                    c_.LineRest();
                }
                continue;
            }
            if (c_.EatKeyword(L"accTitle")) {
                c_.LineRest();
                continue;
            }
            Line(indent);
        }
        // Sums, children before parents.
        for (size_t i = nodes.size(); i-- > 0;) {
            if (nodes[i].leaf) continue;
            nodes[i].value = 0;
            for (int ch : nodes[i].children) nodes[i].value += nodes[static_cast<size_t>(ch)].value;
        }
    }

    std::vector<Node> nodes;
    std::map<std::wstring, Style> classes;
    std::wstring title;
    uint32_t title_pos = 0, title_len = 0;

private:
    // "Name" or "Name": value (or , value), 'quoted' too; :::class after it.
    void Line(int indent) {
        Node n;
        n.indent = indent;
        const size_t at = c_.pos();
        const wchar_t quote = c_.Peek();
        if (quote != L'"' && quote != L'\'') c_.Fail(Tr(L"Expected a \"name\"", L"Ожидалось «имя»"));
        c_.Advance();
        const size_t s = c_.pos();
        while (!c_.AtEnd() && c_.Peek() != quote && c_.Peek() != L'\n') c_.Advance();
        if (!c_.Eat(quote)) c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        const std::wstring_view name = src_.text.substr(s, c_.pos() - 1 - s);
        if (name.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        n.text = std::wstring(name);
        n.pos = static_cast<uint32_t>(s);
        n.len = static_cast<uint32_t>(name.size());
        c_.SkipBlanks();
        if (!c_.Rest().starts_with(L":::") && (c_.Eat(L':') || c_.Eat(L','))) {
            c_.SkipBlanks();
            const size_t v = c_.pos();
            while ((c_.Peek() >= L'0' && c_.Peek() <= L'9') || c_.Peek() == L'.' || c_.Peek() == L',') c_.Advance();
            std::wstring digits(src_.text.substr(v, c_.pos() - v));
            digits.erase(std::remove(digits.begin(), digits.end(), L','), digits.end());
            wchar_t* end = nullptr;
            n.value = digits.empty() || digits.size() > 30 ? -1 : std::wcstod(digits.c_str(), &end);
            if (!(n.value >= 0) || !std::isfinite(n.value) || (end && *end)) c_.Fail(Tr(L"Expected a number", L"Ожидалось число"));
            n.leaf = true;
        }
        c_.SkipBlanks();
        if (c_.EatStr(L":::")) {
            const size_t k = c_.pos();
            while (IsWordChar(c_.Peek())) c_.Advance();
            n.cls = std::wstring(src_.text.substr(k, c_.pos() - k));
        }
        c_.SkipBlanks();
        if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        // The parent: the nearest section indented less.
        int p = static_cast<int>(nodes.size()) - 1;
        while (p > 0 && (nodes[static_cast<size_t>(p)].indent >= indent || nodes[static_cast<size_t>(p)].leaf)) p = nodes[static_cast<size_t>(p)].parent;
        if (nodes.size() > kMaxNodes) {
            c_.set_pos(at);
            c_.Fail(Tr(L"Too many items", L"Слишком много элементов"));
        }
        n.parent = p;
        nodes[static_cast<size_t>(p)].children.push_back(static_cast<int>(nodes.size()));
        nodes.push_back(std::move(n));
    }

    const Source& src_;
    Cursor c_;
};

using M = SceneBuilder::Measured;

constexpr float kW = 1000, kH = 400;     // Mermaid's nodeWidth and nodeHeight times 10
constexpr float kInner = 10;             // between siblings
constexpr float kSide = 10, kHeader = 25;  // a section's padding, and its header
constexpr float kTitle = 30;

// d3's squarified treemap: children (biggest first) in rows that keep their
// boxes nearest to the golden ratio.
void Squarify(const std::vector<Node>& nodes, const std::vector<int>& kids, D2D1_RECT_F r, std::vector<D2D1_RECT_F>* out) {
    double total = 0;
    for (int k : kids) total += nodes[static_cast<size_t>(k)].value;
    if (total <= 0 || kids.empty()) {
        for (int k : kids) (*out)[static_cast<size_t>(k)] = D2D1::RectF(r.left, r.top, r.left, r.top);
        return;
    }
    const double ratio = (1 + std::sqrt(5.0)) / 2;
    size_t i0 = 0;
    double value = total;
    float x0 = r.left, y0 = r.top, x1 = r.right, y1 = r.bottom;
    while (i0 < kids.size()) {
        const double dx = x1 - x0, dy = y1 - y0;
        if (!(dx > 0 && dy > 0) || !(value > 0)) {
            for (size_t k = i0; k < kids.size(); ++k) (*out)[static_cast<size_t>(kids[k])] = D2D1::RectF(x0, y0, x0, y0);
            return;
        }
        size_t i1 = i0;
        double sum = 0;
        do sum = nodes[static_cast<size_t>(kids[i1++])].value;
        while (sum == 0 && i1 < kids.size());
        double min_v = sum, max_v = sum;
        const double alpha = std::max(dy / dx, dx / dy) / (value * ratio);
        double beta = sum * sum * alpha;
        double min_ratio = std::max(max_v / beta, beta / min_v);
        for (; i1 < kids.size(); ++i1) {
            const double v = nodes[static_cast<size_t>(kids[i1])].value;
            sum += v;
            if (v < min_v) min_v = v;
            if (v > max_v) max_v = v;
            beta = sum * sum * alpha;
            const double new_ratio = std::max(max_v / beta, beta / min_v);
            if (new_ratio > min_ratio) {
                sum -= v;
                break;
            }
            min_ratio = new_ratio;
        }
        // Lay the row out: across the shorter side.
        const bool dice = dx < dy;
        if (dice) {
            const float h = value > 0 ? static_cast<float>(dy * sum / value) : 0.f;
            float x = x0;
            for (size_t k = i0; k < i1; ++k) {
                const float w = sum > 0 ? static_cast<float>(dx * nodes[static_cast<size_t>(kids[k])].value / sum) : 0.f;
                (*out)[static_cast<size_t>(kids[k])] = D2D1::RectF(x, y0, x + w, y0 + h);
                x += w;
            }
            y0 += h;
        } else {
            const float w = value > 0 ? static_cast<float>(dx * sum / value) : 0.f;
            float y = y0;
            for (size_t k = i0; k < i1; ++k) {
                const float h = sum > 0 ? static_cast<float>(dy * nodes[static_cast<size_t>(kids[k])].value / sum) : 0.f;
                (*out)[static_cast<size_t>(kids[k])] = D2D1::RectF(x0, y, x0 + w, y + h);
                y += h;
            }
            x0 += w;
        }
        value -= sum;
        i0 = i1;
    }
}

// A color at an opacity (a theme role, or a style's explicit one).
DgColor Fade(DgColor c, float opacity) {
    if (c.role == kDgArgb) return Argb((c.argb & 0xFFFFFF) | (static_cast<uint32_t>(static_cast<float>(c.argb >> 24) * opacity + 0.5f) << 24));
    return RoleAt(c.role, static_cast<uint8_t>(255 * opacity + 0.5f));
}

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b, const Source& src)
        : p_(p), b_(b), src_(src), rect_(p.nodes.size()), color_(p.nodes.size(), -1), label_(p.nodes.size(), 0) {}

    std::shared_ptr<DiagramScene> Build() {
        const bool own = !p_.title.empty();
        const std::wstring& title = own ? p_.title : src_.title;
        const float top = title.empty() ? 0.f : kTitle;
        rect_[0] = D2D1::RectF(0, top, kW, top + kH);
        Position(0, 0);
        for (D2D1_RECT_F& r : rect_) r = D2D1::RectF(std::round(r.left), std::round(r.top), std::round(r.right), std::round(r.bottom));
        Colors();
        for (size_t i = 1; i < p_.nodes.size(); ++i) {
            if (!p_.nodes[i].leaf && !p_.nodes[i].children.empty()) Section(i);
        }
        Leaves();
        if (!title.empty()) {
            const M m = b_.Measure(ParseLabel(title, false), 0, false, 14 / b_.size());
            b_.Text(m, D2D1::Point2F(kW / 2 - m.w / 2, kTitle / 2 - m.h / 2), Role(kDgText), own ? p_.title_pos : src_.title_pos,
                    own ? p_.title_len : src_.title_len);
        }
        return b_.FinishFitted(8);
    }

private:
    // Biggest first, as d3 sorts them.
    std::vector<int> Sorted(const std::vector<int>& kids) const {
        std::vector<int> s = kids;
        std::stable_sort(s.begin(), s.end(), [&](int a, int b) { return p_.nodes[static_cast<size_t>(a)].value > p_.nodes[static_cast<size_t>(b)].value; });
        return s;
    }

    bool Branch(size_t i) const { return !p_.nodes[i].children.empty(); }

    // d3's treemap positioning: a node inset by half the inner padding
    // (`inset`), its children tiled inside its own padding (a section's
    // header on top).
    void Position(size_t i, float inset) {
        D2D1_RECT_F r = rect_[i];
        r = D2D1::RectF(r.left + inset, r.top + inset, r.right - inset, r.bottom - inset);
        if (r.right < r.left) r.left = r.right = (r.left + r.right) / 2;
        if (r.bottom < r.top) r.top = r.bottom = (r.top + r.bottom) / 2;
        rect_[i] = r;
        if (!Branch(i)) return;
        const float p = kInner / 2;
        D2D1_RECT_F in = D2D1::RectF(r.left + kSide - p, r.top + kHeader + kSide - p, r.right - kSide + p, r.bottom - kSide + p);
        if (in.right < in.left) in.left = in.right = (in.left + in.right) / 2;
        if (in.bottom < in.top) in.top = in.bottom = (in.top + in.bottom) / 2;
        const std::vector<int> kids = Sorted(p_.nodes[i].children);
        std::vector<D2D1_RECT_F> out(p_.nodes.size());
        Squarify(p_.nodes, kids, in, &out);
        for (int k : kids) {
            rect_[static_cast<size_t>(k)] = out[static_cast<size_t>(k)];
            Position(static_cast<size_t>(k), p);
        }
    }

    // Mermaid's ordinal scales by name: fills (and borders) handed out to
    // sections breadth first, the diagram's own first (none), 13 in turn;
    // text colors to sections, then to leaves depth first, 12 in turn.
    void Colors() {
        std::unordered_map<std::wstring, int> fills, labels;
        auto fill = [&](const std::wstring& name) { return fills.emplace(name, static_cast<int>(fills.size())).first->second % 13 - 1; };
        auto label = [&](const std::wstring& name) { return labels.emplace(name, static_cast<int>(labels.size())).first->second % 12; };
        std::vector<size_t> queue{0};
        for (size_t q = 0; q < queue.size(); ++q) {
            const size_t i = queue[q];
            if (!Branch(i)) continue;
            color_[i] = i == 0 ? -1 : fill(p_.nodes[i].text);
            if (i == 0) fills.emplace(L"", 0);
            if (i != 0) label_[i] = label(p_.nodes[i].text);
            for (int k : Sorted(p_.nodes[i].children)) queue.push_back(static_cast<size_t>(k));
        }
        Depth(0, label);
    }

    template <typename F>
    void Depth(size_t i, F& label) {
        for (int k : Sorted(p_.nodes[i].children)) {
            const size_t c = static_cast<size_t>(k);
            if (Branch(c)) Depth(c, label);
            else label_[c] = label(p_.nodes[c].text);
        }
    }

    Style StyleOf(const Node& n) const {
        if (auto it = p_.classes.find(n.cls); !n.cls.empty() && it != p_.classes.end()) return it->second;
        return Style{};
    }

    // A section: see-through in its color, its border fainter; the name
    // (cut short to fit) and total in the header.
    void Section(size_t i) {
        const Node& n = p_.nodes[i];
        const D2D1_RECT_F r = rect_[i];
        const Style st = StyleOf(n);
        const int c = color_[i];
        const DgColor fill = st.fill.role != kDgNone ? st.fill : c >= 0 ? Role(static_cast<uint8_t>(kDgScale + c)) : Role(kDgNone);
        const DgColor stroke = st.stroke.role != kDgNone ? st.stroke : c >= 0 ? Role(static_cast<uint8_t>(kDgScalePeer + c)) : Role(kDgNone);
        b_.Rect(r, 0, fill.role != kDgNone ? Fade(fill, 0.6f) : fill, stroke.role != kDgNone ? Fade(stroke, 0.4f) : stroke,
                st.stroke_width >= 0 ? st.stroke_width : 2.f);
        const DgColor ink = st.color.role != kDgNone ? st.color : Role(static_cast<uint8_t>(kDgScaleText + label_[i]));
        const float unit = b_.size(), w = r.right - r.left;
        const bool value = n.value > 0;
        const float room = std::max(15.f, value ? w - 10 - 30 - 10 - 6 : w - 12);
        const RichText name = ParseLabel(n.text, false);
        M m = b_.Measure(name, 0, true, 12 / unit, false);
        // As Mermaid: shortened with "..." (or left out) till it fits; then
        // clipped to the section less 12 (here: shortened to that too).
        auto fit = [&](float limit) {
            if (!m.layout || m.w <= limit) return;
            auto cut = [&](size_t k) {
                RichText t;
                t.text = name.text.substr(0, k) + L"...";
                return b_.Measure(t, 0, true, 12 / unit, false);
            };
            size_t lo = 0, hi = name.text.size();
            while (lo + 1 < hi) {
                const size_t mid = (lo + hi) / 2;
                if (cut(mid).w <= limit) lo = mid;
                else hi = mid;
            }
            m = cut(lo);
            if (m.w > limit) m = M{};
        };
        fit(room);
        fit(w - 12 - 6);
        if (m.layout) b_.Text(m, D2D1::Point2F(r.left + 6, r.top + kHeader / 2 - m.h / 2), ink, n.pos, n.len);
        if (value) {
            RichText v;
            v.text = FormatValue(n.value);
            v.spans.push_back({0, static_cast<uint32_t>(v.text.size()), false, true});
            const M vm = b_.Measure(v, 0, false, 10 / unit, false);
            b_.Text(vm, D2D1::Point2F(r.right - 10 - vm.w, r.top + kHeader / 2 - vm.h / 2), ink);
        }
    }

    // Leaves: see-through in their section's color with its border; the
    // name as big as fits (from 38px, or 16 with many leaves) and the value
    // under it, sized and left out by Mermaid's rules.
    void Leaves() {
        std::vector<size_t> leaves;
        Collect(0, &leaves);
        const bool many = leaves.size() > 20;
        const float base = many ? 16.f : 38.f, base_value = many ? 14.f : 28.f, least = many ? 4.f : 8.f, least_value = many ? 4.f : 6.f;
        const float pad = many ? 2.f : 4.f, threshold = many ? 8.f : 10.f, spacing = many ? 1.f : 2.f;
        const float unit = b_.size();
        for (const size_t i : leaves) {
            const Node& n = p_.nodes[i];
            const D2D1_RECT_F r = rect_[i];
            const Style st = StyleOf(n);
            const int c = color_[static_cast<size_t>(n.parent)];
            const DgColor own = c >= 0 ? Role(static_cast<uint8_t>(kDgScale + c)) : Role(kDgNone);
            const DgColor fill = st.fill.role != kDgNone ? st.fill : own, stroke = st.stroke.role != kDgNone ? st.stroke : own;
            b_.Rect(r, 0, fill.role != kDgNone ? Fade(fill, 0.3f) : fill, stroke, st.stroke_width >= 0 ? st.stroke_width : 3.f);
            const float w = r.right - r.left, h = r.bottom - r.top, room_w = w - 2 * pad, room_h = h - 2 * pad;
            if (room_w < threshold || room_h < threshold) continue;
            const RichText name = ParseLabel(n.text, false);
            // (Widths scale with the size: one measure, then arithmetic.)
            const float width_at_base = b_.Measure(name, 0, false, base / unit).w;
            auto width_at = [&](float px) { return width_at_base * px / base; };
            auto value_px = [&](float px) { return std::max(least_value, std::min(base_value, std::round(px * 0.6f))); };
            float px = base;
            while (width_at(px) > room_w && px > least) --px;
            float vpx = value_px(px);
            while (px + spacing + vpx > room_h && px > least) {
                --px;
                vpx = value_px(px);
                if (vpx < least_value && px == least) break;
            }
            if (many ? (px < least || room_h < least) : (width_at(px) > room_w || px < least || room_h < px)) continue;
            const DgColor ink = st.color.role != kDgNone ? st.color : Role(static_cast<uint8_t>(kDgScaleText + label_[i]));
            const M m = b_.Measure(name, 0, false, px / unit);
            const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            b_.Text(m, D2D1::Point2F(cx - m.w / 2, cy - m.h / 2), ink, n.pos, n.len);
            if (n.value > 0) {
                RichText v;
                v.text = FormatValue(n.value);
                const M vm = b_.Measure(v, 0, false, vpx / unit);
                const float vtop = cy + px / 2 + spacing;
                if (vm.w <= room_w && vtop - r.top + vpx <= h - 4) {
                    b_.Text(vm, D2D1::Point2F(cx - vm.w / 2, vtop), ink);
                }
            }
        }
    }

    // Leaves depth first (d3's leaves()).
    void Collect(size_t i, std::vector<size_t>* out) const {
        for (int k : Sorted(p_.nodes[i].children)) {
            const size_t c = static_cast<size_t>(k);
            if (Branch(c)) Collect(c, out);
            else out->push_back(c);
        }
    }

    // Values as d3's format(","): 1,234.5
    static std::wstring FormatValue(double v) {
        wchar_t buf[64];
        std::swprintf(buf, 64, L"%.12g", v);
        std::wstring s(buf);
        if (s.find(L'e') != std::wstring::npos) return s;
        const size_t dot = s.find(L'.');
        const size_t int_end = dot == std::wstring::npos ? s.size() : dot;
        for (size_t k = int_end; k > 3; k -= 3) s.insert(k - 3, L",");
        return s;
    }

    const Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
    std::vector<D2D1_RECT_F> rect_;
    std::vector<int> color_;  // sections: cScale index, -1 none
    std::vector<int> label_;  // text color index
};

}  // namespace

std::shared_ptr<DiagramScene> Treemap(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder, src).Build();
}

}  // namespace mdv::mermaid
