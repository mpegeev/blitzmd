// Mermaid ishikawa (fishbone) diagrams (ishikawa-beta): the effect as a
// head at the spine's right end, causes as bones slanting up and down from
// the spine in turn, their sub-causes on bones off them (level and slanting
// in turn); laid out as Mermaid's ishikawa renderer does.
#include <algorithm>
#include <cmath>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Cause {
    std::wstring text;
    uint32_t pos = 0, len = 0;
    std::vector<int> children;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    // Lines by indentation: the first the effect, the least indented after
    // it its causes (Mermaid's levels).
    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"ishikawa-beta") && !c_.EatKeyword(L"ishikawa")) c_.Fail(Tr(L"Expected \"ishikawa-beta\"", L"Ожидалось «ishikawa-beta»"));
        struct Open {
            int level;
            int node;
        };
        std::vector<Open> stack;
        int base = -1;
        const std::wstring_view text = src_.text;
        // Lines, the first from the header's end (text there is a node too).
        for (size_t line = c_.pos(); line <= text.size();) {
            const size_t next = text.find(L'\n', line);
            const size_t end = next == std::wstring_view::npos ? text.size() : next;
            const size_t from = line;
            line = next == std::wstring_view::npos ? text.size() + 1 : next + 1;
            size_t s = from;
            while (s < end && IsSpace(text[s])) ++s;
            const std::wstring_view body = Trim(text.substr(s, end - s));
            if (body.empty() || body.starts_with(L"%%")) continue;
            c_.set_pos(s);
            if (body.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
            if (causes.size() >= kMaxNodes) c_.Fail(Tr(L"Too many causes", L"Слишком много причин"));
            const int index = static_cast<int>(causes.size());
            causes.push_back(Cause{std::wstring(body), static_cast<uint32_t>(body.data() - text.data()), static_cast<uint32_t>(body.size()), {}});
            if (index == 0) {
                stack.push_back({0, 0});
                continue;
            }
            // (Levels from the first cause's indent.)
            const int raw = static_cast<int>(s - from);
            if (base < 0) base = raw;
            const int level = std::max(raw - base + 1, 1);
            while (stack.size() > 1 && stack.back().level >= level) stack.pop_back();
            if (stack.size() > static_cast<size_t>(kMaxNesting)) c_.Fail(Tr(L"Causes nested too deep", L"Причины вложены слишком глубоко"));
            causes[static_cast<size_t>(stack.back().node)].children.push_back(index);
            stack.push_back({level, index});
        }
    }

    std::vector<Cause> causes;  // [0]: the effect

private:
    const Source& src_;
    Cursor c_;
};

using M = SceneBuilder::Measured;

constexpr float kFont = 16;
constexpr float kSpine = 250;           // Mermaid's SPINE_BASE_LENGTH
constexpr float kStub = 30, kBoneBase = 60, kBonePerChild = 5;
constexpr float kAngle = 82 * 3.14159265f / 180;
constexpr float kPadding = 20;

// Mermaid's wrapText: words into lines of at most `most` characters.
std::wstring Wrap(const std::wstring& text, size_t most) {
    if (text.size() <= most) return text;
    std::vector<std::wstring> lines;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && IsSpace(text[i])) ++i;
        size_t e = i;
        while (e < text.size() && !IsSpace(text[e])) ++e;
        if (e == i) break;
        const std::wstring word = text.substr(i, e - i);
        if (!lines.empty() && lines.back().size() + 1 + word.size() <= most) lines.back() += L" " + word;
        else lines.push_back(word);
        i = e;
    }
    std::wstring out;
    for (size_t k = 0; k < lines.size(); ++k) out += (k ? L"\n" : L"") + lines[k];
    return out;
}

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b) : p_(p), b_(b), cos_(std::cos(kAngle)), sin_(std::sin(kAngle)) {}

    std::shared_ptr<DiagramScene> Build() {
        const std::vector<Cause>& all = p_.causes;
        if (all.empty()) return b_.Finish(1, 1);  // (nothing, as Mermaid draws it)
        const std::vector<int>& causes = all[0].children;
        // Sides: causes in turn above and below; bones as long as their
        // share of what hangs off them (a third of the base at least).
        std::vector<int> upper, lower;
        for (size_t i = 0; i < causes.size(); ++i) (i % 2 == 0 ? upper : lower).push_back(causes[i]);
        const auto [upper_total, upper_max] = Stats(upper);
        const auto [lower_total, lower_max] = Stats(lower);
        float upper_len = kSpine, lower_len = kSpine;
        if (upper_total + lower_total > 0) {
            upper_len = std::max(kSpine * 0.3f, 2 * kSpine * static_cast<float>(upper_total) / static_cast<float>(upper_total + lower_total));
            lower_len = std::max(kSpine * 0.3f, 2 * kSpine * static_cast<float>(lower_total) / static_cast<float>(upper_total + lower_total));
        }
        upper_len = std::max(upper_len, static_cast<float>(upper_max) * kFont * 2);
        lower_len = std::max(lower_len, static_cast<float>(lower_max) * kFont * 2);
        const float spine_y = causes.empty() ? kSpine : std::max(upper_len, kSpine);
        // Pairs of causes along the spine, each pair left of the last's text.
        float spine_x = -20;
        for (size_t p = 0; p * 2 < causes.size(); ++p) {
            left_ = 1e30f;
            Bone(static_cast<size_t>(causes[p * 2]), spine_x, spine_y, -1, upper_len);
            if (p * 2 + 1 < causes.size()) Bone(static_cast<size_t>(causes[p * 2 + 1]), spine_x, spine_y, 1, lower_len);
            spine_x = left_;
        }
        if (!causes.empty()) Line(D2D1::Point2F(spine_x, spine_y), D2D1::Point2F(0, spine_y), 2.f, false);
        Head(spine_y);
        return b_.FinishFitted(kPadding);
    }

private:
    // Descendants: in all, and the most of one cause.
    std::pair<int, int> Stats(const std::vector<int>& side) const {
        int total = 0, most = 0;
        for (const int c : side) {
            const int d = Count(static_cast<size_t>(c));
            total += d;
            most = std::max(most, d);
        }
        return {total, most};
    }

    int Count(size_t i) const {
        int n = 0;
        for (const int c : p_.causes[i].children) n += 1 + Count(static_cast<size_t>(c));
        return n;
    }

    // Text as Mermaid's writes it: lines 1.05 of the size apart, aligned.
    M Lines(const std::wstring& raw, bool bold, float px, DWRITE_TEXT_ALIGNMENT align) {
        RichText t;
        t.text = raw;
        for (const wchar_t* br : {L"<br/>", L"<br />", L"<br>"}) {
            for (size_t k = t.text.find(br); k != std::wstring::npos; k = t.text.find(br, k)) t.text.replace(k, std::wcslen(br), L"\n");
        }
        M m = b_.Measure(t, 0, bold, px / b_.size(), false);
        if (m.layout) {
            const float lh = px * 1.05f;
            m.layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lh, lh * 0.8f);
            m.layout->SetTextAlignment(align);
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(m.layout->GetMetrics(&tm))) m.h = tm.height;
        }
        return m;
    }

    // Its box as the browser's (a line's box 1.15 of the size).
    static float BoxHeight(const M& m, float px) {
        const float lh = px * 1.05f;
        return m.h - lh + px * 1.15f;
    }

    void Place(const M& m, float left, float top, uint32_t pos, uint32_t len) {
        b_.Text(m, D2D1::Point2F(left, top), Role(kDgText), pos, len);
        left_ = std::min(left_, left);
    }

    // A line, an arrowhead at its start pointing into it (Mermaid's markers).
    void Line(D2D1_POINT_2F a, D2D1_POINT_2F z, float width, bool arrow) {
        b_.Begin(a);
        b_.LineTo(z);
        b_.End(Role(kDgNone), Role(kDgLine), width);
        if (!arrow) return;
        const float dx = z.x - a.x, dy = z.y - a.y, len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0) return;
        // (6 stroke widths long and wide, as a marker scaled by the stroke.)
        const float ux = dx / len, uy = dy / len, s = 6 * width;
        b_.Polygon({a, D2D1::Point2F(a.x + ux * s - uy * s / 2, a.y + uy * s + ux * s / 2), D2D1::Point2F(a.x + ux * s + uy * s / 2, a.y + uy * s - ux * s / 2)},
                   Role(kDgLine), Role(kDgNone));
    }

    // The effect: a head at the spine's end, its name inside.
    void Head(float y) {
        const Cause& e = p_.causes[0];
        const size_t most = std::max<size_t>(6, static_cast<size_t>(110 / (kFont * 0.6f)));
        const M m = Lines(Wrap(e.text, most), true, 14, DWRITE_TEXT_ALIGNMENT_CENTER);
        const float w = std::max(60.f, m.w + 6), h = std::max(40.f, BoxHeight(m, 14) * 2 + 40);
        // M 0 -h/2 L 0 h/2 Q 2.4w 0 0 -h/2 Z
        const float k = 2.f / 3;
        b_.Begin(D2D1::Point2F(0, y - h / 2));
        b_.LineTo(D2D1::Point2F(0, y + h / 2));
        b_.CubicTo(D2D1::Point2F(2.4f * w * k, y + h / 2 * (1 - k)), D2D1::Point2F(2.4f * w * k, y - h / 2 * (1 - k)), D2D1::Point2F(0, y - h / 2));
        b_.Close();
        b_.End(Role(kDgNodeFill), Role(kDgLine), 2.f);
        b_.Text(m, D2D1::Point2F((w - m.w) / 2 + 3, y - m.h / 2), Role(kDgText), e.pos, e.len);
    }

    // A cause's bone from the spine (up: dir -1), its label boxed at the
    // end, its sub-causes on bones along it.
    void Bone(size_t cause, float x0, float y0, float dir, float length) {
        const Cause& c = p_.causes[cause];
        const float line_len = length * (c.children.empty() ? 0.2f : 1.f);
        const float dx = -cos_ * line_len, dy = sin_ * line_len * dir;
        const float x1 = x0 + dx, y1 = y0 + dy;
        Line(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1), 2.f, true);
        // The label, centred 11 past the bone's end, in a box 20 wider.
        const M m = Lines(c.text, false, kFont, DWRITE_TEXT_ALIGNMENT_CENTER);
        const float bh = BoxHeight(m, kFont), cy = y1 + 11 * dir;
        const D2D1_RECT_F box = D2D1::RectF(x1 - m.w / 2 - 20, cy - bh / 2 - 2, x1 + m.w / 2 + 20, cy + bh / 2 + 2);
        b_.Rect(box, 0, Role(kDgNodeFill), Role(kDgLine), 2.f);
        Place(m, x1 - m.w / 2, cy - m.h / 2, c.pos, c.len);
        if (c.children.empty()) return;
        // Sub-causes, depth first, in Mermaid's order down the bone: a
        // level one before its own, a slanting one after them.
        struct Entry {
            size_t node;
            int depth, parent, children;
        };
        std::vector<Entry> entries;
        std::vector<size_t> order;  // entries by their place down the bone
        auto walk = [&](auto&& self, const std::vector<int>& kids, int parent, int depth) -> void {
            std::vector<int> list = kids;
            if (dir < 0) std::reverse(list.begin(), list.end());
            for (const int k : list) {
                const size_t idx = entries.size();
                const std::vector<int>& gc = p_.causes[static_cast<size_t>(k)].children;
                entries.push_back({static_cast<size_t>(k), depth, parent, static_cast<int>(gc.size())});
                if (depth % 2 == 0) {
                    order.push_back(idx);
                    if (!gc.empty()) self(self, gc, static_cast<int>(idx), depth + 1);
                } else {
                    if (!gc.empty()) self(self, gc, static_cast<int>(idx), depth + 1);
                    order.push_back(idx);
                }
            }
        };
        walk(walk, c.children, -1, 2);
        std::vector<float> ys(entries.size());
        for (size_t slot = 0; slot < order.size(); ++slot) {
            ys[order[slot]] = y0 + dy * static_cast<float>(slot + 1) / static_cast<float>(entries.size() + 1);
        }
        struct Bn {
            float x0, y0, x1, y1;
            int children, drawn;
        };
        std::vector<Bn> bones(entries.size());
        Bn root{x0, y0, x1, y1, static_cast<int>(c.children.size()), 0};
        const float diag_x = -cos_, diag_y = sin_ * dir;
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            const float y = ys[i];
            Bn& par = e.parent < 0 ? root : bones[static_cast<size_t>(e.parent)];
            const Cause& n = p_.causes[e.node];
            float bx0 = 0, by0 = 0, bx1 = 0;
            if (e.depth % 2 == 0) {
                // Level, off the parent at its height, the label left of it.
                const float span = par.y1 - par.y0;
                bx0 = par.x0 + (par.x1 - par.x0) * (span != 0 ? (y - par.y0) / span : 0.5f);
                by0 = y;
                bx1 = bx0 - (e.children > 0 ? kBoneBase + static_cast<float>(e.children) * kBonePerChild : kStub);
                Line(D2D1::Point2F(bx0, y), D2D1::Point2F(bx1, y), 1.f, true);
                const M t = Lines(Wrap(n.text, 15), false, kFont, DWRITE_TEXT_ALIGNMENT_TRAILING);
                Place(t, bx1 - t.w, y - t.h / 2, n.pos, n.len);
            } else {
                // Slanting, from along the parent; the label above (below) its end.
                const int k = par.drawn++;
                bx0 = par.x0 + (par.x1 - par.x0) * static_cast<float>(par.children - k) / static_cast<float>(par.children + 1);
                by0 = par.y0;
                bx1 = bx0 + diag_x * ((y - by0) / diag_y);
                Line(D2D1::Point2F(bx0, by0), D2D1::Point2F(bx1, y), 1.f, true);
                const std::wstring text = Wrap(n.text, 15);
                const M t = Lines(text, false, kFont, DWRITE_TEXT_ALIGNMENT_TRAILING);
                // Its first line where Mermaid's is: on the line's end (above
                // it up, hanging from it down), the rest after it.
                const float lh = kFont * 1.05f;
                const float first = y - static_cast<float>(std::count(text.begin(), text.end(), L'\n')) * lh / 2;
                Place(t, bx1 - t.w, dir < 0 ? first - lh * 0.8f : first, n.pos, n.len);
            }
            if (e.children > 0) bones[i] = {bx0, by0, bx1, y, e.children, 0};
        }
    }

    const Parser& p_;
    SceneBuilder& b_;
    const float cos_, sin_;
    float left_ = 1e30f;  // the leftmost text of the bones drawn
};

}  // namespace

std::shared_ptr<DiagramScene> IshikawaDiagram(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder).Build();
}

}  // namespace mdv::mermaid
