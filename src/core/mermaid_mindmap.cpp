// Mermaid mindmaps: a tree by indentation, the root in the middle and its
// branches to the right and left, each branch in one of the theme's cScale
// colors, lines thinner the deeper they go. Mermaid lays mindmaps out with
// a force-directed layout; this is a tidy two-sided tree of the same nodes.
#include <algorithm>
#include <cmath>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

enum class Shape : uint8_t { Default, Rounded, Rect, Circle, Cloud, Bang, Hexagon };

struct Node {
    std::wstring text;
    uint32_t pos = 0, len = 0;
    bool markdown = false;
    Shape shape = Shape::Default;
    int parent = -1;
    int indent = 0;
    std::vector<int> children;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"mindmap")) c_.Fail(Tr(L"Expected \"mindmap\"", L"Ожидалось «mindmap»"));
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            // The indent: from the line's start.
            size_t line = c_.pos();
            while (line > 0 && src_.text[line - 1] != L'\n') --line;
            const int indent = static_cast<int>(c_.pos() - line);  // (a tab counts one, as in Mermaid)
            if (c_.EatStr(L"::icon(")) {  // an icon (not drawn)
                c_.SkipToLineEnd();
                continue;
            }
            if (c_.EatStr(L":::")) {      // classes (their CSS isn't known)
                c_.SkipToLineEnd();
                continue;
            }
            NodeLine(indent);
        }
        if (nodes.empty()) c_.Fail(Tr(L"Expected a root node", L"Ожидался корневой узел"));
    }

    std::vector<Node> nodes;

private:
    // id[text], id(text), id((text)), id)text(, id))text((, id{{text}}, or text.
    void NodeLine(int indent) {
        Node n;
        n.indent = indent;
        const size_t start = c_.pos();
        static constexpr struct {
            const wchar_t* open;
            const wchar_t* close;
            Shape shape;
        } kShapes[] = {{L"((", L"))", Shape::Circle}, {L"))", L"((", Shape::Bang},  {L"{{", L"}}", Shape::Hexagon},
                       {L"[", L"]", Shape::Rect},     {L"(", L")", Shape::Rounded}, {L")", L"(", Shape::Cloud}};
        // The id: up to a shape's opening.
        size_t k = start;
        const std::wstring_view text = src_.text;
        while (k < text.size() && text[k] != L'\n' && text[k] != L'[' && text[k] != L'(' && text[k] != L')' && text[k] != L'{') ++k;
        bool shaped = false;
        for (const auto& s : kShapes) {
            if (k >= text.size() || !text.substr(k).starts_with(s.open)) continue;
            // The text up to the matching close at the line's end (a
            // "quoted" one may go on over lines).
            size_t e = k + std::wcslen(s.open);
            while (e < text.size() && (text[e] == L' ' || text[e] == L'\t')) ++e;
            if (e < text.size() && text[e] == L'"') {
                // (Only a quote closed right before the shape's end.)
                const size_t q = text.find(L'"', e + 1);
                size_t after = q == std::wstring_view::npos ? q : q + 1;
                while (after < text.size() && (text[after] == L' ' || text[after] == L'\t')) ++after;
                if (after < text.size() && text.substr(after).starts_with(s.close)) e = q;
            }
            while (e < text.size() && text[e] != L'\n') ++e;
            const std::wstring_view line = Trim(text.substr(k, e - k));
            const std::wstring_view close = s.close;
            if (line.size() < std::wcslen(s.open) + close.size() || !line.ends_with(close)) continue;
            std::wstring_view inner = Trim(line.substr(std::wcslen(s.open), line.size() - std::wcslen(s.open) - close.size()));
            if (inner.size() >= 2 && inner.front() == L'"' && inner.back() == L'"') inner = inner.substr(1, inner.size() - 2);
            if (inner.size() >= 2 && inner.front() == L'`' && inner.back() == L'`') {
                inner = inner.substr(1, inner.size() - 2);
                n.markdown = true;
            }
            Set(&n, inner);
            n.shape = s.shape;
            c_.set_pos(e);
            shaped = true;
            break;
        }
        if (!shaped) {
            size_t e = start;
            while (e < text.size() && text[e] != L'\n') ++e;
            Set(&n, Trim(text.substr(start, e - start)));
            c_.set_pos(e);
        }
        // The parent: the nearest node indented less.
        const int index = static_cast<int>(nodes.size());
        if (!nodes.empty()) {
            int p = index - 1;
            while (p >= 0 && nodes[static_cast<size_t>(p)].indent >= indent) p = nodes[static_cast<size_t>(p)].parent;
            if (p < 0) {
                c_.set_pos(start);
                c_.Fail(Tr(L"There can be only one root", L"Корень может быть только один"));
            }
            n.parent = p;
            nodes[static_cast<size_t>(p)].children.push_back(index);
        }
        if (nodes.size() >= kMaxNodes) c_.Fail(Tr(L"Too many nodes", L"Слишком много узлов"));
        nodes.push_back(std::move(n));
    }

    void Set(Node* n, std::wstring_view s) {
        if (s.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        n->text = std::wstring(s);
        n->pos = static_cast<uint32_t>(s.data() - src_.text.data());
        n->len = static_cast<uint32_t>(s.size());
    }

    const Source& src_;
    Cursor c_;
};

// ---------------------------------------------------------------------------
// Layout and drawing
// ---------------------------------------------------------------------------

using M = SceneBuilder::Measured;

constexpr float kMargin = 16;
constexpr float kWrap = 200;             // Mermaid's maxNodeWidth
constexpr float kPadX = 16, kPadY = 8;
constexpr float kGapX = 44, kGapY = 14;  // between a node and its children, between siblings
constexpr float kPi = 3.14159265f;

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b) : p_(p), b_(b) {}

    std::shared_ptr<DiagramScene> Build() {
        const size_t n = p_.nodes.size();
        text_.resize(n);
        w_.resize(n);
        h_.resize(n);
        section_.assign(n, -1);
        depth_.assign(n, 0);
        for (size_t i = 0; i < n; ++i) {
            const Node& nd = p_.nodes[i];
            // (Every mindmap label is markdown in Mermaid 11.)
            text_[i] = b_.Measure(ParseLabel(nd.text, true), kWrap, false, 1.f);
            const float tw = text_[i].w, th = text_[i].h;
            switch (nd.shape) {
            case Shape::Circle: w_[i] = h_[i] = std::max(tw, th) + 2 * kPadY + 8; break;
            case Shape::Hexagon: w_[i] = tw + 2 * kPadX + th; h_[i] = th + 2 * kPadY + 4; break;
            case Shape::Cloud:
            case Shape::Bang: w_[i] = tw + 2 * kPadX + 24; h_[i] = th + 2 * kPadY + 24; break;
            default: w_[i] = tw + 2 * kPadX; h_[i] = th + 2 * kPadY; break;
            }
            if (nd.parent >= 0) {
                depth_[i] = depth_[static_cast<size_t>(nd.parent)] + 1;
                section_[i] = depth_[i] == 1 ? static_cast<int>(Child(i) % 11) : section_[static_cast<size_t>(nd.parent)];
            }
        }
        // Subtree heights, then the root's branches to the side with less.
        span_.assign(n, 0.f);
        for (size_t i = n; i-- > 0;) {
            float sum = 0;
            for (int c : p_.nodes[i].children) sum += span_[static_cast<size_t>(c)] + kGapY;
            span_[i] = std::max(h_[i], sum - (p_.nodes[i].children.empty() ? 0.f : kGapY));
        }
        std::vector<int> right, left;
        float rh = 0, lh = 0;
        for (int c : p_.nodes[0].children) {
            const float s = span_[static_cast<size_t>(c)] + kGapY;
            if (rh <= lh) {
                right.push_back(c);
                rh += s;
            } else {
                left.push_back(c);
                lh += s;
            }
        }
        x_.assign(n, 0.f);
        y_.assign(n, 0.f);
        Place(right, 0, w_[0] / 2, 1.f);
        Place(left, 0, w_[0] / 2, -1.f);
        // Bounds.
        float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
        for (size_t i = 0; i < n; ++i) {
            x0 = std::min(x0, x_[i] - w_[i] / 2);
            x1 = std::max(x1, x_[i] + w_[i] / 2);
            y0 = std::min(y0, y_[i] - h_[i] / 2);
            y1 = std::max(y1, y_[i] + h_[i] / 2);
        }
        const float ox = kMargin - x0, oy = kMargin - y0;
        for (size_t i = 0; i < n; ++i) {
            x_[i] += ox;
            y_[i] += oy;
        }
        // Lines first (the nodes cover their ends), thinner deeper down.
        for (size_t i = 1; i < n; ++i) {
            const size_t par = static_cast<size_t>(p_.nodes[i].parent);
            b_.Begin(D2D1::Point2F(x_[par], y_[par]));
            const float mx = (x_[par] + x_[i]) / 2;
            b_.CubicTo(D2D1::Point2F(mx, y_[par]), D2D1::Point2F(mx, y_[i]), D2D1::Point2F(x_[i], y_[i]));
            const float width = std::max(3.f, 17.f - 3.f * static_cast<float>(depth_[i] + 1));
            b_.End(Role(kDgNone), Role(Scale(kDgScale, i)), width);
        }
        for (size_t i = 0; i < n; ++i) DrawNode(i);
        return b_.Finish(x1 - x0 + 2 * kMargin, y1 - y0 + 2 * kMargin);
    }

private:
    // Which of its parent's children node i is.
    size_t Child(size_t i) const {
        const auto& siblings = p_.nodes[static_cast<size_t>(p_.nodes[i].parent)].children;
        return static_cast<size_t>(std::find(siblings.begin(), siblings.end(), static_cast<int>(i)) - siblings.begin());
    }

    // A branch's color: Mermaid's section-n uses cScale n+1.
    uint8_t Scale(uint8_t base, size_t i) const { return static_cast<uint8_t>(base + (section_[i] + 1) % 12); }

    // Children stacked beside their parent (at `edge` from its center),
    // centered on it; their children beside them in turn.
    void Place(const std::vector<int>& kids, size_t parent, float edge, float side) {
        if (kids.empty()) return;
        float total = -kGapY;
        for (int c : kids) total += span_[static_cast<size_t>(c)] + kGapY;
        float y = y_[parent] - total / 2;
        for (int c : kids) {
            const size_t k = static_cast<size_t>(c);
            x_[k] = x_[parent] + side * (edge + kGapX + w_[k] / 2);
            y_[k] = y + span_[k] / 2;
            y += span_[k] + kGapY;
            Place(p_.nodes[k].children, k, w_[k] / 2, side);
        }
    }

    void DrawNode(size_t i) {
        const Node& nd = p_.nodes[i];
        const bool root = i == 0;
        const DgColor fill = root ? Role(kDgGit) : Role(Scale(kDgScale, i));
        const DgColor text = root ? Role(kDgGitText) : Role(Scale(kDgScaleText, i));
        const float cx = x_[i], cy = y_[i], w = w_[i], h = h_[i];
        const D2D1_RECT_F r = D2D1::RectF(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
        switch (nd.shape) {
        case Shape::Default:
            if (root) {
                b_.Rect(r, 5, fill, Role(kDgNone));
            } else {
                // Rounded at the top, a line in the inverse color along the bottom.
                const float rd = 5, k = rd * 0.45f;
                b_.Begin(D2D1::Point2F(r.left, r.bottom));
                b_.LineTo(D2D1::Point2F(r.left, r.top + rd));
                b_.CubicTo(D2D1::Point2F(r.left, r.top + k), D2D1::Point2F(r.left + k, r.top), D2D1::Point2F(r.left + rd, r.top));
                b_.LineTo(D2D1::Point2F(r.right - rd, r.top));
                b_.CubicTo(D2D1::Point2F(r.right - k, r.top), D2D1::Point2F(r.right, r.top + k), D2D1::Point2F(r.right, r.top + rd));
                b_.LineTo(D2D1::Point2F(r.right, r.bottom));
                b_.Close();
                b_.End(fill, Role(kDgNone));
                b_.Begin(D2D1::Point2F(r.left, r.bottom));
                b_.LineTo(D2D1::Point2F(r.right, r.bottom));
                b_.End(Role(kDgNone), Role(Scale(kDgScaleInv, i)), 3.f);
            }
            break;
        case Shape::Rect: b_.Rect(r, 0, fill, Role(kDgNone)); break;
        case Shape::Rounded: b_.Rect(r, 8, fill, Role(kDgNone)); break;
        case Shape::Circle: b_.Ellipse(r, fill, Role(kDgNone)); break;
        case Shape::Hexagon: {
            const float d = h / 2;
            b_.Polygon({{r.left, cy}, {r.left + d, r.top}, {r.right - d, r.top}, {r.right, cy}, {r.right - d, r.bottom}, {r.left + d, r.bottom}},
                       fill, Role(kDgNone));
            break;
        }
        case Shape::Cloud: Bumps(r, fill, false); break;
        case Shape::Bang: Bumps(r, fill, true); break;
        }
        const M& m = text_[i];
        b_.Text(m, D2D1::Point2F(cx - m.w / 2, cy - m.h / 2), text, nd.pos, nd.len);
    }

    // A cloud (bumps out round an ellipse) or a bang (spikes out).
    void Bumps(const D2D1_RECT_F& r, DgColor fill, bool spikes) {
        const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, rx = (r.right - r.left) / 2, ry = (r.bottom - r.top) / 2;
        const int count = std::clamp(static_cast<int>((rx + ry) / 12), 8, 24);
        auto at = [&](float a, float k) { return D2D1::Point2F(cx + rx * k * std::cos(a), cy + ry * k * std::sin(a)); };
        b_.Begin(at(0, 0.88f));
        for (int j = 0; j < count; ++j) {
            const float a0 = 2 * kPi * static_cast<float>(j) / static_cast<float>(count);
            const float a1 = 2 * kPi * static_cast<float>(j + 1) / static_cast<float>(count);
            const float am = (a0 + a1) / 2;
            if (spikes) {
                b_.LineTo(at(am, 1.f));
                b_.LineTo(at(a1, 0.8f));
            } else {
                b_.CubicTo(at(a0 + (a1 - a0) * 0.1f, 1.08f), at(a1 - (a1 - a0) * 0.1f, 1.08f), at(a1, 0.88f));
            }
        }
        b_.Close();
        b_.End(fill, Role(kDgNone));
    }

    const Parser& p_;
    SceneBuilder& b_;
    std::vector<M> text_;
    std::vector<float> w_, h_, x_, y_, span_;
    std::vector<int> section_, depth_;
};

}  // namespace

std::shared_ptr<DiagramScene> Mindmap(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder).Build();
}

}  // namespace mdv::mermaid
