// Mermaid treemaps: a tree by indentation ("Section" / "Leaf": value), laid
// out as d3's squarified treemap, biggest first: sections with their name
// and total in a header, leaves with a big name and their value, each
// section in the next of the theme's cScale colors.
#include <algorithm>
#include <cmath>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Node {
    std::wstring text;
    uint32_t pos = 0, len = 0;
    double value = 0;       // a leaf's own, a section's sum
    bool leaf = false;
    int parent = -1, indent = 0;
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
            int indent = 0;
            for (size_t k = line; k < c_.pos(); ++k) indent += src_.text[k] == L'\t' ? 4 : 1;
            if (c_.EatKeyword(L"classDef") || c_.EatKeyword(L"title") || c_.EatKeyword(L"accTitle") || c_.EatKeyword(L"accDescr")) {
                c_.LineRest();
                continue;
            }
            Line(indent);
        }
        // Sums, children before parents.
        for (size_t i = nodes.size(); i-- > 1;) {
            if (nodes[i].leaf) continue;
            nodes[i].value = 0;
            for (int ch : nodes[i].children) nodes[i].value += nodes[static_cast<size_t>(ch)].value;
        }
        nodes[0].value = 0;
        for (int ch : nodes[0].children) nodes[0].value += nodes[static_cast<size_t>(ch)].value;
    }

    std::vector<Node> nodes;

private:
    // "Name" or "Name": value (:::class after it).
    void Line(int indent) {
        Node n;
        n.indent = indent;
        const size_t at = c_.pos();
        if (!c_.Eat(L'"')) c_.Fail(Tr(L"Expected a \"name\"", L"Ожидалось «имя»"));
        const size_t s = c_.pos();
        while (!c_.AtEnd() && c_.Peek() != L'"' && c_.Peek() != L'\n') c_.Advance();
        if (!c_.Eat(L'"')) c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        const std::wstring_view name = src_.text.substr(s, c_.pos() - 1 - s);
        if (name.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        n.text = std::wstring(name);
        n.pos = static_cast<uint32_t>(s);
        n.len = static_cast<uint32_t>(name.size());
        c_.SkipBlanks();
        if (c_.Eat(L':')) {
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
        while (c_.EatStr(L":::")) {
            while (IsWordChar(c_.Peek()) || c_.Peek() == L'-') c_.Advance();
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

constexpr float kW = 960, kH = 360;
constexpr float kPad = 10, kHeader = 25;

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

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b) : p_(p), b_(b), rect_(p.nodes.size()), color_(p.nodes.size(), 0) {}

    std::shared_ptr<DiagramScene> Build() {
        Lay(0, D2D1::RectF(0, 0, kW, kH));
        int next = 0;
        Color(0, &next);
        for (size_t i = 1; i < p_.nodes.size(); ++i) Draw(i);
        return b_.FinishFitted(8);
    }

private:
    // Biggest first, as d3 sorts them.
    std::vector<int> Sorted(const std::vector<int>& kids) const {
        std::vector<int> s = kids;
        std::stable_sort(s.begin(), s.end(), [&](int a, int b) { return p_.nodes[static_cast<size_t>(a)].value > p_.nodes[static_cast<size_t>(b)].value; });
        return s;
    }

    void Lay(size_t i, D2D1_RECT_F r) {
        rect_[i] = r;
        const Node& n = p_.nodes[i];
        if (n.children.empty()) return;
        // Inside a section: its header on top, padding around.
        D2D1_RECT_F in = i == 0 ? r : D2D1::RectF(r.left + kPad, r.top + kHeader + 4, r.right - kPad, r.bottom - kPad);
        if (in.right < in.left) in.right = in.left;
        if (in.bottom < in.top) in.bottom = in.top;
        const std::vector<int> kids = Sorted(n.children);
        std::vector<D2D1_RECT_F> out(p_.nodes.size());
        Squarify(p_.nodes, kids, in, &out);
        for (int k : kids) {
            // (Padding between siblings.)
            D2D1_RECT_F cr = out[static_cast<size_t>(k)];
            if (i != 0 || kids.size() > 1) cr = D2D1::RectF(cr.left + kPad / 2, cr.top + kPad / 2, std::max(cr.left + kPad / 2, cr.right - kPad / 2),
                                                            std::max(cr.top + kPad / 2, cr.bottom - kPad / 2));
            Lay(static_cast<size_t>(k), cr);
        }
    }

    // Sections take the colors in turn (biggest first, depth first); leaves
    // their section's.
    void Color(size_t i, int* next) {
        for (int k : Sorted(p_.nodes[i].children)) {
            const size_t c = static_cast<size_t>(k);
            color_[c] = p_.nodes[c].leaf ? color_[i] : (*next)++ % 12;
            if (!p_.nodes[c].leaf) Color(c, next);
        }
    }

    void Draw(size_t i) {
        const Node& n = p_.nodes[i];
        const D2D1_RECT_F r = rect_[i];
        if (r.right - r.left < 1 || r.bottom - r.top < 1) return;
        const uint8_t role = static_cast<uint8_t>(kDgScale + color_[i]);
        const float unit = b_.size();
        std::wstring value = FormatValue(n.value);
        if (!n.leaf) {
            b_.Rect(r, 0, RoleAt(role, 150), Role(role), 1.f);
            RichText v;
            v.text = value;
            const M vm = b_.Measure(v, 0, false, 11 / unit, false);
            const M name = b_.Measure(ParseLabel(n.text, false), std::max(r.right - r.left - vm.w - 16, 1.f), true, 12 / unit, false);
            b_.Text(name, D2D1::Point2F(r.left + 6, r.top + 4), Role(static_cast<uint8_t>(kDgScaleText + color_[i])), n.pos, n.len);
            if (r.right - r.left > name.w + vm.w + 16) b_.Text(vm, D2D1::Point2F(r.right - 6 - vm.w, r.top + 5), Role(static_cast<uint8_t>(kDgScaleText + color_[i])));
            return;
        }
        b_.Rect(r, 0, RoleAt(role, 200), RoleAt(role, 255), 2.f);
        // The name as big as fits (up to 38px), the value under it.
        const float w = r.right - r.left, h = r.bottom - r.top;
        float px = 38;
        M name;
        for (;;) {
            name = b_.Measure(ParseLabel(n.text, false), 0, false, px / unit);
            if ((name.w <= w - 8 && name.h * 1.6f <= h) || px <= 8) break;
            px = std::max(8.f, px * 0.85f);
        }
        RichText v;
        v.text = value;
        const M vm = b_.Measure(v, 0, false, std::max(px * 0.55f, 8.f) / unit);
        const float total = name.h + vm.h;
        const DgColor ink = Role(static_cast<uint8_t>(kDgScaleText + color_[i]));
        if (name.w <= w && total <= h + 2) {
            b_.Text(name, D2D1::Point2F((r.left + r.right) / 2 - name.w / 2, (r.top + r.bottom) / 2 - total / 2), ink, n.pos, n.len);
            b_.Text(vm, D2D1::Point2F((r.left + r.right) / 2 - vm.w / 2, (r.top + r.bottom) / 2 - total / 2 + name.h), ink);
        } else if (name.w <= w && name.h <= h) {
            b_.Text(name, D2D1::Point2F((r.left + r.right) / 2 - name.w / 2, (r.top + r.bottom) / 2 - name.h / 2), ink, n.pos, n.len);
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
    std::vector<D2D1_RECT_F> rect_;
    std::vector<int> color_;
};

}  // namespace

std::shared_ptr<DiagramScene> Treemap(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder).Build();
}

}  // namespace mdv::mermaid
