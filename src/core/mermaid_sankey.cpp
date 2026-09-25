// Mermaid sankey diagrams: flows (CSV rows source,target,value) between
// nodes in columns, laid out as d3-sankey does for Mermaid (justified,
// 600 by 400), nodes in Tableau10 colors, the flows as bands shading from
// the source's color to the target's, names and values beside the nodes.
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct SNode {
    std::wstring name;
    uint32_t pos = 0, len = 0;          // first named
    std::vector<size_t> in, out;        // links
    double value = 0;
    int depth = 0, layer = 0;
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};

struct SLink {
    size_t source = 0, target = 0, index = 0;
    double value = 0, width = 0, y0 = 0, y1 = 0;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"sankey-beta") && !c_.EatKeyword(L"sankey")) c_.Fail(Tr(L"Expected \"sankey-beta\"", L"Ожидалось «sankey-beta»"));
        for (;;) {
            // Rows: blank lines and %% comments between them.
            while (!c_.AtEnd() && IsSpace(c_.Peek())) c_.Advance();
            if (c_.Rest().starts_with(L"%%")) {
                c_.SkipToLineEnd();
                continue;
            }
            if (c_.AtEnd()) break;
            const size_t row = c_.pos();
            const auto [a, a_pos, a_len] = Field();
            if (!c_.Eat(L',')) c_.Fail(Tr(L"Expected source,target,value", L"Ожидалось источник,цель,значение"));
            const auto [b, b_pos, b_len] = Field();
            if (!c_.Eat(L',')) c_.Fail(Tr(L"Expected source,target,value", L"Ожидалось источник,цель,значение"));
            const double value = ParseFloat(Field().text);
            if (!(value >= 0) || !std::isfinite(value)) {
                c_.set_pos(row);
                c_.Fail(Tr(L"Expected a number", L"Ожидалось число"));
            }
            SLink l;
            l.source = Node(a, a_pos, a_len);
            l.target = Node(b, b_pos, b_len);
            l.value = value;
            l.index = links.size();
            if (links.size() >= kMaxNodes) c_.Fail(Tr(L"Too many flows", L"Слишком много потоков"));
            links.push_back(l);
            while (!c_.AtEnd() && c_.Peek() != L'\n') {
                if (!IsSpace(c_.Peek())) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
                c_.Advance();
            }
        }
        // (Values adding up past a double's range would make no layout.)
        double total = 0;
        for (const SLink& l : links) total += l.value;
        if (!std::isfinite(total)) Fail(kNoLine, Tr(L"The values are too large", L"Значения слишком большие"));
    }

    std::vector<SNode> nodes;
    std::vector<SLink> links;

private:
    // As JavaScript's parseFloat: the number at the start (10.5kg: 10.5;
    // 0x10: 0), else NaN.
    static double ParseFloat(std::wstring_view s) {
        s = Trim(s);
        size_t k = 0;
        if (k < s.size() && (s[k] == L'+' || s[k] == L'-')) ++k;
        const size_t digits = k;
        while (k < s.size() && s[k] >= L'0' && s[k] <= L'9') ++k;
        bool any = k > digits;
        if (k < s.size() && s[k] == L'.') {
            const size_t f = ++k;
            while (k < s.size() && s[k] >= L'0' && s[k] <= L'9') ++k;
            any = any || k > f;
        }
        if (!any) return NAN;
        if (k < s.size() && (s[k] == L'e' || s[k] == L'E')) {
            size_t e = k + 1;
            if (e < s.size() && (s[e] == L'+' || s[e] == L'-')) ++e;
            const size_t d = e;
            while (e < s.size() && s[e] >= L'0' && s[e] <= L'9') ++e;
            if (e > d) k = e;
        }
        if (k > 400) return NAN;
        return std::wcstod(std::wstring(s.substr(0, k)).c_str(), nullptr);
    }

    // A CSV field: text, or "quoted" with "" for a quote; trimmed, as
    // Mermaid trims them.
    struct Cell {
        std::wstring text;
        size_t pos, len;  // in the source
    };
    Cell Field() {
        while (c_.Peek() == L' ' || c_.Peek() == L'\t') c_.Advance();
        std::wstring out;
        size_t start = c_.pos(), len = 0;
        if (c_.Eat(L'"')) {
            start = c_.pos();
            for (;;) {
                if (c_.AtEnd()) c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
                if (c_.Peek() == L'"') {
                    if (c_.Peek(1) == L'"') {
                        out.push_back(L'"');
                        c_.Advance(2);
                        continue;
                    }
                    len = c_.pos() - start;
                    c_.Advance();
                    break;
                }
                out.push_back(c_.Peek());
                c_.Advance();
            }
            // (Trimmed: spaces are themselves in the source too.)
            const std::wstring_view t = Trim(out);
            const size_t lead = static_cast<size_t>(t.data() - out.data()), trail = out.size() - lead - t.size();
            start += lead;
            len -= lead + trail;
            out = std::wstring(t);
        } else {
            while (!c_.AtEnd() && c_.Peek() != L',' && c_.Peek() != L'\n' && c_.Peek() != L'\r') {
                out.push_back(c_.Peek());
                c_.Advance();
            }
            while (!out.empty() && (out.back() == L' ' || out.back() == L'\t')) out.pop_back();
            len = out.size();
        }
        if (out.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return {out, start, len};
    }

    size_t Node(const std::wstring& name, size_t pos, size_t len) {
        if (auto it = ids_.find(name); it != ids_.end()) return it->second;
        if (nodes.size() >= kMaxNodes) c_.Fail(Tr(L"Too many nodes", L"Слишком много узлов"));
        SNode n;
        n.name = name;
        n.pos = static_cast<uint32_t>(pos);
        n.len = static_cast<uint32_t>(len);
        ids_.emplace(name, nodes.size());
        nodes.push_back(std::move(n));
        return nodes.size() - 1;
    }

    const Source& src_;
    Cursor c_;
    std::unordered_map<std::wstring, size_t> ids_;
};

// ---------------------------------------------------------------------------
// d3-sankey
// ---------------------------------------------------------------------------

class Sankey {
public:
    Sankey(std::vector<SNode>& n, std::vector<SLink>& l, double w, double h, double node_w, double pad)
        : nodes_(n), links_(l), x1_(w), y1_(h), dx_(node_w), dy_(pad) {}

    bool Run() {
        for (size_t k = 0; k < links_.size(); ++k) {
            nodes_[links_[k].source].out.push_back(k);
            nodes_[links_[k].target].in.push_back(k);
        }
        for (SNode& n : nodes_) {
            double in = 0, out = 0;
            for (size_t k : n.in) in += links_[k].value;
            for (size_t k : n.out) out += links_[k].value;
            n.value = std::max(in, out);
        }
        if (!Depths()) return false;
        Breadths();
        LinkBreadths();
        return true;
    }

private:
    // Depths from the sources (a flow around in a circle: none).
    bool Depths() {
        std::vector<size_t> current(nodes_.size());
        for (size_t i = 0; i < current.size(); ++i) current[i] = i;
        int x = 0;
        std::vector<char> in_next(nodes_.size());
        while (!current.empty()) {
            std::vector<size_t> next;
            std::fill(in_next.begin(), in_next.end(), static_cast<char>(0));
            for (size_t i : current) {
                nodes_[i].depth = x;
                for (size_t k : nodes_[i].out) {
                    const size_t t = links_[k].target;
                    if (!in_next[t]) {
                        in_next[t] = 1;
                        next.push_back(t);
                    }
                }
            }
            if (++x > static_cast<int>(nodes_.size())) return false;
            current = std::move(next);
        }
        return true;
    }

    // Columns, justified: nodes without outflows in the last.
    std::vector<std::vector<size_t>> Layers() {
        int x = 0;
        for (const SNode& n : nodes_) x = std::max(x, n.depth);
        ++x;
        const double kx = x > 1 ? (x1_ - dx_) / (x - 1) : 0;
        std::vector<std::vector<size_t>> columns(static_cast<size_t>(x));
        for (size_t i = 0; i < nodes_.size(); ++i) {
            SNode& n = nodes_[i];
            const int align = n.out.empty() ? x - 1 : n.depth;
            n.layer = std::clamp(align, 0, x - 1);
            n.x0 = n.layer * kx;
            n.x1 = n.x0 + dx_;
            columns[static_cast<size_t>(n.layer)].push_back(i);
        }
        return columns;
    }

    void Breadths() {
        std::vector<std::vector<size_t>> columns = Layers();
        size_t longest = 1;
        for (const auto& c : columns) longest = std::max(longest, c.size());
        py_ = longest > 1 ? std::min(dy_, y1_ / static_cast<double>(longest - 1)) : dy_;
        // Initial: stacked and spread, scaled to the fullest column.
        double ky = 1e300;
        for (const auto& c : columns) {
            double sum = 0;
            for (size_t i : c) sum += nodes_[i].value;
            if (sum > 0) ky = std::min(ky, (y1_ - static_cast<double>(c.size() - 1) * py_) / sum);
        }
        if (!(ky < 1e299) || ky < 0) ky = 0;
        for (const auto& c : columns) {
            double y = 0;
            for (size_t i : c) {
                SNode& n = nodes_[i];
                n.y0 = y;
                n.y1 = y + n.value * ky;
                y = n.y1 + py_;
                for (size_t k : n.out) links_[k].width = links_[k].value * ky;
            }
            y = (y1_ - y + py_) / static_cast<double>(c.size() + 1);
            for (size_t j = 0; j < c.size(); ++j) {
                nodes_[c[j]].y0 += y * static_cast<double>(j + 1);
                nodes_[c[j]].y1 += y * static_cast<double>(j + 1);
            }
        }
        for (size_t i = 0; i < nodes_.size(); ++i) ReorderLinks(i);
        const int iterations = 6;
        for (int i = 0; i < iterations; ++i) {
            const double alpha = std::pow(0.99, i), beta = std::max(1 - alpha, static_cast<double>(i + 1) / iterations);
            RelaxRightToLeft(columns, alpha, beta);
            RelaxLeftToRight(columns, alpha, beta);
        }
    }

    void RelaxLeftToRight(std::vector<std::vector<size_t>>& columns, double alpha, double beta) {
        for (size_t i = 1; i < columns.size(); ++i) {
            for (size_t t : columns[i]) {
                SNode& target = nodes_[t];
                double y = 0, w = 0;
                for (size_t k : target.in) {
                    const SNode& source = nodes_[links_[k].source];
                    const double v = links_[k].value * (target.layer - source.layer);
                    y += TargetTop(links_[k].source, t) * v;
                    w += v;
                }
                if (!(w > 0)) continue;
                const double d = (y / w - target.y0) * alpha;
                target.y0 += d;
                target.y1 += d;
                ReorderNodeLinks(t);
            }
            SortColumn(&columns[i]);
            ResolveCollisions(columns[i], beta);
        }
    }

    void RelaxRightToLeft(std::vector<std::vector<size_t>>& columns, double alpha, double beta) {
        for (size_t i = columns.size() >= 2 ? columns.size() - 2 : 0; columns.size() >= 2; --i) {
            for (size_t s : columns[i]) {
                SNode& source = nodes_[s];
                double y = 0, w = 0;
                for (size_t k : source.out) {
                    const SNode& target = nodes_[links_[k].target];
                    const double v = links_[k].value * (target.layer - source.layer);
                    y += SourceTop(s, links_[k].target) * v;
                    w += v;
                }
                if (!(w > 0)) continue;
                const double d = (y / w - source.y0) * alpha;
                source.y0 += d;
                source.y1 += d;
                ReorderNodeLinks(s);
            }
            SortColumn(&columns[i]);
            ResolveCollisions(columns[i], beta);
            if (i == 0) break;
        }
    }

    void SortColumn(std::vector<size_t>* c) {
        std::stable_sort(c->begin(), c->end(), [&](size_t a, size_t b) { return nodes_[a].y0 < nodes_[b].y0; });
    }

    void ResolveCollisions(const std::vector<size_t>& c, double alpha) {
        if (c.empty()) return;
        const size_t i = c.size() >> 1;
        const SNode& subject = nodes_[c[i]];
        BottomToTop(c, subject.y0 - py_, static_cast<long long>(i) - 1, alpha);
        TopToBottom(c, subject.y1 + py_, i + 1, alpha);
        BottomToTop(c, y1_, static_cast<long long>(c.size()) - 1, alpha);
        TopToBottom(c, 0, 0, alpha);
    }

    void TopToBottom(const std::vector<size_t>& c, double y, size_t i, double alpha) {
        for (; i < c.size(); ++i) {
            SNode& n = nodes_[c[i]];
            const double d = (y - n.y0) * alpha;
            if (d > 1e-6) {
                n.y0 += d;
                n.y1 += d;
            }
            y = n.y1 + py_;
        }
    }

    void BottomToTop(const std::vector<size_t>& c, double y, long long i, double alpha) {
        for (; i >= 0; --i) {
            SNode& n = nodes_[c[static_cast<size_t>(i)]];
            const double d = (n.y1 - y) * alpha;
            if (d > 1e-6) {
                n.y0 -= d;
                n.y1 -= d;
            }
            y = n.y0 - py_;
        }
    }

    void SortOut(size_t n) {
        std::vector<size_t>& v = nodes_[n].out;
        std::stable_sort(v.begin(), v.end(), [&](size_t a, size_t b) {
            const double d = nodes_[links_[a].target].y0 - nodes_[links_[b].target].y0;
            return d != 0 ? d < 0 : links_[a].index < links_[b].index;
        });
    }
    void SortIn(size_t n) {
        std::vector<size_t>& v = nodes_[n].in;
        std::stable_sort(v.begin(), v.end(), [&](size_t a, size_t b) {
            const double d = nodes_[links_[a].source].y0 - nodes_[links_[b].source].y0;
            return d != 0 ? d < 0 : links_[a].index < links_[b].index;
        });
    }
    void ReorderLinks(size_t n) {
        SortOut(n);
        SortIn(n);
    }
    void ReorderNodeLinks(size_t n) {
        for (size_t k : nodes_[n].in) SortOut(links_[k].source);
        for (size_t k : nodes_[n].out) SortIn(links_[k].target);
    }

    double TargetTop(size_t s, size_t t) const {
        const SNode& source = nodes_[s];
        double y = source.y0 - static_cast<double>(source.out.size() - 1) * py_ / 2;
        for (size_t k : source.out) {
            if (links_[k].target == t) break;
            y += links_[k].width + py_;
        }
        for (size_t k : nodes_[t].in) {
            if (links_[k].source == s) break;
            y -= links_[k].width;
        }
        return y;
    }

    double SourceTop(size_t s, size_t t) const {
        const SNode& target = nodes_[t];
        double y = target.y0 - static_cast<double>(target.in.size() - 1) * py_ / 2;
        for (size_t k : target.in) {
            if (links_[k].source == s) break;
            y += links_[k].width + py_;
        }
        for (size_t k : nodes_[s].out) {
            if (links_[k].target == t) break;
            y -= links_[k].width;
        }
        return y;
    }

    void LinkBreadths() {
        for (SNode& n : nodes_) {
            double y0 = n.y0, y1 = n.y0;
            for (size_t k : n.out) {
                links_[k].y0 = y0 + links_[k].width / 2;
                y0 += links_[k].width;
            }
            for (size_t k : n.in) {
                links_[k].y1 = y1 + links_[k].width / 2;
                y1 += links_[k].width;
            }
        }
    }

    std::vector<SNode>& nodes_;
    std::vector<SLink>& links_;
    double x1_, y1_, dx_, dy_, py_ = 0;
};

// d3's schemeTableau10.
constexpr uint32_t kTableau[] = {0xFF4E79A7, 0xFFF28E2C, 0xFFE15759, 0xFF76B7B2, 0xFF59A14F,
                                 0xFFEDC949, 0xFFAF7AA1, 0xFFFF9DA7, 0xFF9C755F, 0xFFBAB0AB};

// A value as Mermaid writes it: rounded to hundredths, as JavaScript prints.
std::wstring Value(double v) { return JsNumber(std::round(v * 100) / 100); }

constexpr float kW = 600, kH = 400;

std::shared_ptr<DiagramScene> Build(Parser& p, SceneBuilder& b) {
    Sankey layout(p.nodes, p.links, kW, kH, 10, 25);
    if (!layout.Run()) Fail(kNoLine, Tr(L"The flows go round in a circle", L"Потоки идут по кругу"));
    // Flows: bands along d3's horizontal link curve, shaded source to target.
    for (const SLink& l : p.links) {
        const SNode& s = p.nodes[l.source];
        const SNode& t = p.nodes[l.target];
        const float x0 = static_cast<float>(s.x1), x1 = static_cast<float>(t.x0), xm = (x0 + x1) / 2;
        const float y0 = static_cast<float>(l.y0), y1 = static_cast<float>(l.y1), w = static_cast<float>(std::max(l.width, 1.0)) / 2;
        const uint32_t ca = kTableau[l.source % 10], cb = kTableau[l.target % 10];
        auto curve = [&](float u) {  // the link's middle at u in [0, 1]
            const float v = 1 - u;
            return D2D1::Point2F(v * v * v * x0 + 3 * v * v * u * xm + 3 * v * u * u * xm + u * u * u * x1,
                                 v * v * v * y0 + 3 * v * v * u * y0 + 3 * v * u * u * y1 + u * u * u * y1);
        };
        // The band: its top edge along, its bottom edge back.
        constexpr int kSteps = 24;
        std::vector<D2D1_POINT_2F> band;
        for (int j = 0; j <= kSteps; ++j) {
            const D2D1_POINT_2F c = curve(static_cast<float>(j) / kSteps);
            band.push_back(D2D1::Point2F(c.x, c.y - w));
        }
        for (int j = kSteps; j >= 0; --j) {
            const D2D1_POINT_2F c = curve(static_cast<float>(j) / kSteps);
            band.push_back(D2D1::Point2F(c.x, c.y + w));
        }
        b.Polygon(band, Argb((ca & 0xFFFFFF) | 0x80000000), Role(kDgNone));
        b.ShadeLast(Argb((cb & 0xFFFFFF) | 0x80000000), D2D1::Point2F(x0, 0), D2D1::Point2F(x1, 0));
    }
    // Nodes and their names with values: right of those in the left half,
    // left of the others.
    for (size_t i = 0; i < p.nodes.size(); ++i) {
        const SNode& n = p.nodes[i];
        const D2D1_RECT_F r = D2D1::RectF(static_cast<float>(n.x0), static_cast<float>(n.y0), static_cast<float>(n.x1),
                                          static_cast<float>(std::max(n.y1, n.y0 + 1)));
        b.Rect(r, 0, Argb(kTableau[i % 10]), Role(kDgNone));
        RichText t;
        t.text = n.name + L" " + Value(n.value);
        const SceneBuilder::Measured m = b.Measure(t, 0, false, 0.875f, false);
        // (The text's baseline at the node's middle, as Mermaid's.)
        float baseline = m.h * 0.75f;
        DWRITE_LINE_METRICS lm{};
        UINT32 lines = 0;
        if (m.layout && SUCCEEDED(m.layout->GetLineMetrics(&lm, 1, &lines)) && lines > 0) baseline = lm.baseline;
        const float cy = (r.top + r.bottom) / 2 - baseline;
        if (n.x0 < kW / 2) b.Text(m, D2D1::Point2F(r.right + 6, cy), Role(kDgText), n.pos, n.len);
        else b.Text(m, D2D1::Point2F(r.left - 6 - m.w, cy), Role(kDgText), n.pos, n.len);
    }
    return b.FinishFitted(4);
}

}  // namespace

std::shared_ptr<DiagramScene> SankeyDiagram(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) {
        Sankey layout(p.nodes, p.links, kW, kH, 10, 25);
        if (!layout.Run()) Fail(kNoLine, Tr(L"The flows go round in a circle", L"Потоки идут по кругу"));
        return nullptr;
    }
    return Build(p, *builder);
}

}  // namespace mdv::mermaid
