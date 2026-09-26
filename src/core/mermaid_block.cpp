// Mermaid block diagrams: blocks in a grid ("columns N"), spanning columns
// (id:2), empty cells (space), nested blocks with their own grid, block
// arrows, the flowchart's node shapes, straight edges between blocks with
// labels, styles.
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

enum class Shape : uint8_t {
    Rect, Round, Stadium, Subroutine, Cylinder, Circle, DoubleCircle, Asymmetric, Diamond, Hexagon,
    Parallelogram, ParallelogramAlt, Trapezoid, TrapezoidAlt, Arrow, Space, Group
};

struct Block {
    std::wstring id;
    std::wstring label;
    uint32_t pos = 0, len = 0;
    Shape shape = Shape::Rect;
    int span = 1;
    int columns = -1;                   // a group's (-1: auto, all in a row)
    std::vector<int> children;
    std::wstring arrow;                 // a block arrow's direction
    std::vector<std::wstring> classes;
    Style style;
};

struct Edge {
    int from = -1, to = -1;
    Marker start = Marker::None, end = Marker::Arrow;
    bool dotted = false, thick = false;
    std::wstring label;
    uint32_t pos = 0, len = 0;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"block-beta") && !c_.EatKeyword(L"block")) c_.Fail(Tr(L"Expected \"block-beta\"", L"Ожидалось «block-beta»"));
        blocks.push_back(Block{});  // the diagram: a group
        blocks[0].shape = Shape::Group;
        std::vector<std::pair<int, size_t>> open{{0, 0}};
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            const size_t at = c_.pos();
            if (c_.EatKeyword(L"end")) {
                if (open.size() == 1) c_.Fail(Tr(L"\"end\" without \"block\"", L"«end» без «block»"));
                open.pop_back();
                continue;
            }
            if (c_.EatKeyword(L"columns")) {
                c_.SkipBlanks();
                if (c_.EatKeyword(L"auto")) {
                    blocks[static_cast<size_t>(open.back().first)].columns = -1;
                } else {
                    const int n = static_cast<int>(Number());
                    if (n < 1 || n > 100) c_.Fail(Tr(L"Expected 1 to 100 columns", L"Ожидалось от 1 до 100 колонок"));
                    blocks[static_cast<size_t>(open.back().first)].columns = n;
                }
                continue;
            }
            if (c_.EatKeyword(L"classDef")) {
                c_.SkipBlanks();
                const std::vector<std::wstring> names = Names();
                c_.SkipBlanks();
                Style st;
                st.Apply(c_.LineRest());
                for (const std::wstring& n : names) class_defs[n].Merge(st);
                continue;
            }
            if (c_.EatKeyword(L"class")) {
                c_.SkipBlanks();
                const std::vector<std::wstring> ids = Names();
                c_.SkipBlanks();
                const std::vector<std::wstring> names = Names();
                ChargeClasses(class_chars_, ids.size(), ClassChars(names));
                for (const std::wstring& id : ids) {
                    if (auto it = ids_.find(id); it != ids_.end()) {
                        auto& c = blocks[static_cast<size_t>(it->second)].classes;
                        c.insert(c.end(), names.begin(), names.end());
                    }
                }
                continue;
            }
            if (c_.EatKeyword(L"style")) {
                c_.SkipBlanks();
                const std::vector<std::wstring> ids = Names();
                c_.SkipBlanks();
                Style st;
                st.Apply(c_.LineRest());
                for (const std::wstring& id : ids) {
                    if (auto it = ids_.find(id); it != ids_.end()) blocks[static_cast<size_t>(it->second)].style.Merge(st);
                }
                continue;
            }
            if (c_.EatKeyword(L"block")) {
                // block[:id][:span] ... end
                Block g;
                g.shape = Shape::Group;
                if (c_.Eat(L':')) {
                    const size_t s = c_.pos();
                    while (IsWordChar(c_.Peek()) || c_.Peek() == L'-') c_.Advance();
                    g.id = std::wstring(src_.text.substr(s, c_.pos() - s));
                    if (c_.Eat(L':')) g.span = static_cast<int>(std::clamp(Number(), 1.0, 100.0));
                }
                if (open.size() > static_cast<size_t>(kMaxNesting)) c_.Fail(Tr(L"Blocks nested too deep", L"Слишком глубокая вложенность блоков"));
                const int index = Add(std::move(g), open.back().first);
                open.push_back({index, at});
                continue;
            }
            // Blocks on a line, or edges between them.
            for (;;) {
                c_.SkipBlanks();
                if (c_.AtStatementEnd()) break;
                const int a = BlockRef(open.back().first);
                c_.SkipBlanks();
                while (a >= 0) {
                    Edge e;
                    if (!EdgeOp(&e)) break;
                    c_.SkipBlanks();
                    const int b = BlockRef(open.back().first);
                    if (b < 0) c_.Fail(Tr(L"Expected a block", L"Ожидался блок"));
                    e.from = a;
                    e.to = b;
                    if (edges.size() >= kMaxLines) c_.Fail(TooMany(L"edges", L"связей", kMaxLines));
                    edges.push_back(e);
                    c_.SkipBlanks();
                    break;
                }
            }
        }
        if (open.size() > 1) {
            c_.set_pos(open.back().second);
            c_.Fail(Tr(L"\"block\" without \"end\"", L"«block» без «end»"));
        }
    }

    std::vector<Block> blocks;
    std::vector<Edge> edges;
    std::map<std::wstring, Style> class_defs;

private:
    int Add(Block b, int parent) {
        if (blocks.size() >= kMaxNodes) c_.Fail(Tr(L"Too many blocks", L"Слишком много блоков"));
        const int index = static_cast<int>(blocks.size());
        if (!b.id.empty()) ids_.emplace(b.id, index);
        blocks.push_back(std::move(b));
        blocks[static_cast<size_t>(parent)].children.push_back(index);
        return index;
    }

    // id, id["label"] and the other shapes, id:span, space[:n], an arrow
    // block id<["label"]>(dir); a block named again refers to it.
    int BlockRef(int parent) {
        if (c_.EatKeyword(L"space")) {
            Block b;
            b.shape = Shape::Space;
            if (c_.Eat(L':')) b.span = static_cast<int>(std::clamp(Number(), 1.0, 100.0));
            Add(std::move(b), parent);
            return -1;
        }
        const size_t s = c_.pos();
        while (IsWordChar(c_.Peek()) || (c_.Peek() == L'-' && IsWordChar(c_.Peek(1))) || c_.Peek() == L'.') c_.Advance();
        if (c_.pos() == s) c_.Fail(Tr(L"Expected a block", L"Ожидался блок"));
        const std::wstring id(src_.text.substr(s, c_.pos() - s));
        Block b;
        b.id = id;
        b.label = id;
        b.pos = static_cast<uint32_t>(s);
        b.len = static_cast<uint32_t>(id.size());
        const bool shaped = Shaped(&b);
        if (c_.Peek() == L':' && !c_.Rest().starts_with(L":::")) {
            c_.Advance();
            b.span = static_cast<int>(std::clamp(Number(), 1.0, 100.0));
        }
        while (c_.EatStr(L":::")) {
            const size_t k = c_.pos();
            while (IsWordChar(c_.Peek()) || c_.Peek() == L'-') c_.Advance();
            b.classes.emplace_back(src_.text.substr(k, c_.pos() - k));
        }
        if (auto it = ids_.find(id); it != ids_.end()) {
            Block& old = blocks[static_cast<size_t>(it->second)];
            if (shaped) {
                old.label = b.label;
                old.pos = b.pos;
                old.len = b.len;
                old.shape = b.shape;
            }
            old.classes.insert(old.classes.end(), b.classes.begin(), b.classes.end());
            return it->second;
        }
        return Add(std::move(b), parent);
    }

    // The shape and label after an id.
    bool Shaped(Block* b) {
        static constexpr struct {
            const wchar_t* open;
            const wchar_t* close;
            Shape shape;
        } kShapes[] = {
            {L"(((", L")))", Shape::DoubleCircle}, {L"((", L"))", Shape::Circle},     {L"([", L"])", Shape::Stadium},
            {L"[[", L"]]", Shape::Subroutine},     {L"[(", L")]", Shape::Cylinder},   {L"{{", L"}}", Shape::Hexagon},
            {L"[/", L"/]", Shape::Parallelogram},  {L"[\\", L"\\]", Shape::ParallelogramAlt},
            {L"[/", L"\\]", Shape::Trapezoid},     {L"[\\", L"/]", Shape::TrapezoidAlt},
            {L"<[", L"]>", Shape::Arrow},          {L"[", L"]", Shape::Rect},         {L"(", L")", Shape::Round},
            {L">", L"]", Shape::Asymmetric},       {L"{", L"}", Shape::Diamond},
        };
        for (const auto& s : kShapes) {
            const std::wstring_view rest = c_.Rest();
            if (!rest.starts_with(s.open)) continue;
            // The label: "quoted" or up to the closing.
            const std::wstring_view after = rest.substr(std::wcslen(s.open));
            std::wstring_view label;
            size_t used;
            if (!after.empty() && after.front() == L'"') {
                const size_t q = after.find(L'"', 1);
                if (q == std::wstring_view::npos) continue;
                label = after.substr(1, q - 1);
                if (!after.substr(q + 1).starts_with(s.close)) continue;
                used = q + 1;
            } else {
                const size_t e = after.find(s.close);
                const size_t nl = after.find(L'\n');
                if (e == std::wstring_view::npos || (nl != std::wstring_view::npos && nl < e)) continue;
                label = after.substr(0, e);
                used = e;
            }
            if (label.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
            b->label = std::wstring(label);
            b->pos = static_cast<uint32_t>(label.data() - src_.text.data());
            b->len = static_cast<uint32_t>(label.size());
            b->shape = s.shape;
            c_.Advance(std::wcslen(s.open) + used + std::wcslen(s.close));
            if (s.shape == Shape::Arrow && c_.Eat(L'(')) {
                const size_t k = c_.pos();
                while (IsWordChar(c_.Peek()) || c_.Peek() == L',' || c_.Peek() == L' ') c_.Advance();
                b->arrow = ToLower(Trim(src_.text.substr(k, c_.pos() - k)));
                if (!c_.Eat(L')')) c_.Fail(Tr(L"Expected ')'", L"Ожидалась «)»"));
            }
            return true;
        }
        return false;
    }

    // -->, ---, -.->, ==>, <-->, -- "label" -->, -->|label|
    // (`labelled`: the rest after a label, which takes no second one.)
    bool EdgeOp(Edge* e, bool labelled = false) {
        const size_t at = c_.pos();
        if (c_.Eat(L'<')) e->start = Marker::Arrow;
        std::wstring_view r = c_.Rest();
        size_t n = 0;
        while (n < r.size() && (r[n] == L'-' || r[n] == L'=' || r[n] == L'.')) ++n;
        if (n < 2) {
            c_.set_pos(at);
            return false;
        }
        const std::wstring_view body = r.substr(0, n);
        e->dotted = body.find(L'.') != std::wstring_view::npos;
        e->thick = body.find(L'=') != std::wstring_view::npos;
        c_.Advance(n);
        if (c_.Eat(L'>')) {
            e->end = Marker::Arrow;
        } else if (c_.Peek() == L'x' || c_.Peek() == L'o') {
            e->end = c_.Peek() == L'x' ? Marker::Cross : Marker::Circle;
            c_.Advance();
        } else {
            // "-- label -->" or "---".
            c_.SkipBlanks();
            if (c_.Peek() == L'"') {
                if (labelled) c_.Fail(Tr(L"Expected the rest of the edge", L"Ожидалось продолжение связи"));
                c_.Advance();
                const size_t s = c_.pos();
                while (!c_.AtEnd() && c_.Peek() != L'"' && c_.Peek() != L'\n') c_.Advance();
                if (!c_.Eat(L'"')) c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
                e->label = std::wstring(src_.text.substr(s, c_.pos() - 1 - s));
                e->pos = static_cast<uint32_t>(s);
                e->len = static_cast<uint32_t>(e->label.size());
                c_.SkipBlanks();
                Edge rest;
                if (!EdgeOp(&rest, true)) c_.Fail(Tr(L"Expected the rest of the edge", L"Ожидалось продолжение связи"));
                e->end = rest.end;
                e->dotted = e->dotted || rest.dotted;
                e->thick = e->thick || rest.thick;
            } else {
                e->end = Marker::None;
            }
        }
        c_.SkipBlanks();
        if (c_.Eat(L'|')) {
            const size_t s = c_.pos();
            while (!c_.AtEnd() && c_.Peek() != L'|' && c_.Peek() != L'\n') c_.Advance();
            if (!c_.Eat(L'|')) c_.Fail(Tr(L"Expected '|'", L"Ожидалась «|»"));
            e->label = std::wstring(Trim(src_.text.substr(s, c_.pos() - 1 - s)));
            e->pos = static_cast<uint32_t>(s);
            e->len = static_cast<uint32_t>(c_.pos() - 1 - s);
        }
        return true;
    }

    double Number() {
        const size_t s = c_.pos();
        while (c_.Peek() >= L'0' && c_.Peek() <= L'9' && c_.pos() - s < 6) c_.Advance();
        if (c_.pos() == s) c_.Fail(Tr(L"Expected a number", L"Ожидалось число"));
        return std::wcstod(std::wstring(src_.text.substr(s, c_.pos() - s)).c_str(), nullptr);
    }

    std::vector<std::wstring> Names() {
        std::vector<std::wstring> out;
        std::wstring cur;
        while (!c_.AtStatementEnd() && !IsSpace(c_.Peek())) {
            const wchar_t ch = c_.Peek();
            c_.Advance();
            if (ch == L',') {
                if (!cur.empty()) out.push_back(std::move(cur));
                cur.clear();
                c_.SkipBlanks();
            } else {
                cur.push_back(ch);
            }
        }
        if (!cur.empty()) out.push_back(std::move(cur));
        if (out.empty()) c_.Fail(Tr(L"Expected a name", L"Ожидалось имя"));
        return out;
    }

    const Source& src_;
    Cursor c_;
    std::unordered_map<std::wstring, int> ids_;
    size_t class_chars_ = 0;  // ChargeClasses
};

// ---------------------------------------------------------------------------
// Layout: siblings share a cell size (the biggest one's), columns take the
// cells left to right.
// ---------------------------------------------------------------------------

using M = SceneBuilder::Measured;

constexpr float kPad = 16, kGap = 8, kGroupPad = 8;
constexpr float kMaxSize = 100000;

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b) : p_(p), b_(b), n_(p.blocks.size()) {}

    std::shared_ptr<DiagramScene> Build() {
        text_.resize(n_);
        w_.assign(n_, 0);
        h_.assign(n_, 0);
        rect_.assign(n_, D2D1::RectF(0, 0, 0, 0));
        Measure(0);
        // (Nested blocks double in size each level, as in Mermaid: past this, no.)
        if (w_[0] > kMaxSize || h_[0] > kMaxSize) TooLarge();
        Place(0, 0, 0);
        for (size_t i = 1; i < n_; ++i) {
            if (p_.blocks[i].shape == Shape::Group) DrawBlock(i);
        }
        for (const Edge& e : p_.edges) DrawEdge(e);
        for (size_t i = 1; i < n_; ++i) {
            if (p_.blocks[i].shape != Shape::Group && p_.blocks[i].shape != Shape::Space) DrawBlock(i);
        }
        for (const Edge& e : p_.edges) DrawEdgeLabel(e);
        return b_.FinishFitted(8);
    }

private:
    static bool Keeps(Shape s) {
        return s == Shape::Circle || s == Shape::DoubleCircle || s == Shape::Diamond || s == Shape::Arrow || s == Shape::Stadium;
    }

    int Columns(const Block& g) const {
        if (g.columns > 0) return g.columns;
        int total = 0;
        for (int c : g.children) total += p_.blocks[static_cast<size_t>(c)].span;
        return std::max(total, 1);
    }

    // Natural sizes, children first; a group's from its grid.
    void Measure(size_t i) {
        const Block& bl = p_.blocks[i];
        if (bl.shape == Shape::Group) {
            for (int c : bl.children) Measure(static_cast<size_t>(c));
            Cells(i);
            const int cols = Columns(bl);
            const float pad = i == 0 ? 0.f : kGroupPad;
            w_[i] = static_cast<float>(cols) * cell_w_[i] + static_cast<float>(cols - 1) * kGap + 2 * pad;
            h_[i] = static_cast<float>(rows_[i]) * cell_h_[i] + static_cast<float>(std::max(rows_[i] - 1, 0)) * kGap + 2 * pad;
            return;
        }
        if (bl.shape == Shape::Space) return;
        text_[i] = b_.Measure(ParseLabel(bl.label, false), 0, false, 1.f);
        const float tw = text_[i].w, th = text_[i].h;
        switch (bl.shape) {
        case Shape::Circle:
        case Shape::DoubleCircle: w_[i] = h_[i] = std::max(tw, th) + kPad + (bl.shape == Shape::DoubleCircle ? 8.f : 0.f); break;
        case Shape::Diamond: w_[i] = h_[i] = tw + th + kPad; break;
        case Shape::Hexagon: w_[i] = tw + th + 2 * kPad; h_[i] = th + 2 * kPad; break;
        case Shape::Arrow: {
            // Mermaid's, its padding 8; a label of spaces measured as nothing
            // and &nbsp; as its width, as the browser does.
            float aw = tw, ah = th;
            const RichText label = ParseLabel(bl.label, false);
            if (std::all_of(label.text.begin(), label.text.end(), [](wchar_t ch) { return ch == L' '; })) {
                aw = ah = 0;
            } else if (text_[i].layout) {
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(text_[i].layout->GetMetrics(&tm))) aw = tm.widthIncludingTrailingWhitespace;
            }
            h_[i] = ah + 16;
            w_[i] = aw + h_[i] + 8;
            break;
        }
        default: w_[i] = tw + 2 * kPad + 8; h_[i] = th + 2 * kPad; break;
        }
    }

    // A group's cell size (its children's biggest, per column of a span)
    // and rows.
    void Cells(size_t g) {
        const Block& bl = p_.blocks[g];
        const int cols = Columns(bl);
        float cw = 0, ch = 0;
        int col = 0, rows = bl.children.empty() ? 0 : 1;
        for (int c : bl.children) {
            const Block& k = p_.blocks[static_cast<size_t>(c)];
            const int span = std::min(k.span, cols);
            if (col + span > cols) {
                col = 0;
                ++rows;
            }
            col += span;
            cw = std::max(cw, (w_[static_cast<size_t>(c)] - static_cast<float>(span - 1) * kGap) / static_cast<float>(span));
            ch = std::max(ch, h_[static_cast<size_t>(c)]);
        }
        cell_w_[g] = cw;
        cell_h_[g] = ch;
        rows_[g] = rows;
    }

    void Place(size_t g, float x, float y) {
        const Block& bl = p_.blocks[g];
        const float pad = g == 0 ? 0.f : kGroupPad;
        const int cols = Columns(bl);
        int col = 0, row = 0;
        for (int c : bl.children) {
            const size_t k = static_cast<size_t>(c);
            const Block& child = p_.blocks[k];
            const int span = std::min(child.span, cols);
            if (col + span > cols) {
                col = 0;
                ++row;
            }
            const float cx = x + pad + static_cast<float>(col) * (cell_w_[g] + kGap);
            const float cy = y + pad + static_cast<float>(row) * (cell_h_[g] + kGap);
            const float w = static_cast<float>(span) * cell_w_[g] + static_cast<float>(span - 1) * kGap, h = cell_h_[g];
            if (child.shape == Shape::Group) {
                // Its grid stretched over its cells.
                rect_[k] = D2D1::RectF(cx, cy, cx + w, cy + h);
                const float extra_w = (w - w_[k]) / static_cast<float>(Columns(child));
                const float extra_h = rows_[k] > 0 ? (h - h_[k]) / static_cast<float>(rows_[k]) : 0.f;
                cell_w_[k] += std::max(extra_w, 0.f);
                cell_h_[k] += std::max(extra_h, 0.f);
                Place(k, cx, cy);
            } else if (Keeps(child.shape)) {
                // (A block arrow over columns takes their width, as in Mermaid.)
                const float bw = child.shape == Shape::Arrow && span > 1 ? std::max(w, w_[k]) : w_[k];
                rect_[k] = D2D1::RectF(cx + w / 2 - bw / 2, cy + h / 2 - h_[k] / 2, cx + w / 2 + bw / 2, cy + h / 2 + h_[k] / 2);
            } else {
                rect_[k] = D2D1::RectF(cx, cy, cx + w, cy + h);
            }
            col += span;
        }
    }

    Style StyleOf(const Block& bl) const {
        Style st;
        if (auto d = p_.class_defs.find(L"default"); d != p_.class_defs.end()) st.Merge(d->second);
        for (const std::wstring& c : bl.classes) {
            if (auto d = p_.class_defs.find(c); d != p_.class_defs.end()) st.Merge(d->second);
        }
        st.Merge(bl.style);
        return st;
    }

    void DrawBlock(size_t i) {
        const Block& bl = p_.blocks[i];
        const D2D1_RECT_F r = rect_[i];
        const Style st = StyleOf(bl);
        const bool group = bl.shape == Shape::Group;
        const DgColor fill = st.fill.role != kDgNone ? st.fill : Role(group ? kDgClusterFill : kDgNodeFill);
        const DgColor stroke = st.stroke.role != kDgNone ? st.stroke : Role(group ? kDgClusterStroke : kDgNodeStroke);
        const float sw = st.stroke_width >= 0 ? st.stroke_width : 1.f;
        const uint8_t dash = st.dash == 1 ? 1 : 0;
        const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, w = r.right - r.left, h = r.bottom - r.top;
        switch (bl.shape) {
        case Shape::Group: b_.Rect(r, 0, fill, stroke, 0.5f, dash); return;
        case Shape::Round: b_.Rect(r, 5, fill, stroke, sw, dash); break;
        case Shape::Stadium: b_.Rect(r, h / 2, fill, stroke, sw, dash); break;
        case Shape::Circle: b_.Ellipse(r, fill, stroke, sw, dash); break;
        case Shape::DoubleCircle:
            b_.Ellipse(r, fill, stroke, sw, dash);
            b_.Ellipse(D2D1::RectF(r.left + 4, r.top + 4, r.right - 4, r.bottom - 4), Role(kDgNone), stroke, sw, dash);
            break;
        case Shape::Diamond: b_.Polygon({{cx, r.top}, {r.right, cy}, {cx, r.bottom}, {r.left, cy}}, fill, stroke, sw, dash); break;
        case Shape::Hexagon: {
            const float d = std::min(h / 2, w / 4);
            b_.Polygon({{r.left, cy}, {r.left + d, r.top}, {r.right - d, r.top}, {r.right, cy}, {r.right - d, r.bottom}, {r.left + d, r.bottom}}, fill,
                       stroke, sw, dash);
            break;
        }
        case Shape::Asymmetric: {
            const float d = h / 4;
            b_.Polygon({{r.left, r.top}, {r.right, r.top}, {r.right, r.bottom}, {r.left, r.bottom}, {r.left + d, cy}}, fill, stroke, sw, dash);
            break;
        }
        case Shape::Parallelogram:
        case Shape::ParallelogramAlt:
        case Shape::Trapezoid:
        case Shape::TrapezoidAlt: {
            const float d = std::min(h / 2, w / 4);
            const bool lean = bl.shape == Shape::Parallelogram, alt = bl.shape == Shape::ParallelogramAlt;
            if (lean || alt) {
                const float s = lean ? d : -d;
                b_.Polygon({{r.left + std::max(s, 0.f), r.top}, {r.right + std::min(s, 0.f), r.top}, {r.right - std::max(s, 0.f), r.bottom},
                            {r.left - std::min(s, 0.f), r.bottom}},
                           fill, stroke, sw, dash);
            } else {
                const bool wide_bottom = bl.shape == Shape::Trapezoid;
                const float t = wide_bottom ? d : 0, u = wide_bottom ? 0 : d;
                b_.Polygon({{r.left + t, r.top}, {r.right - t, r.top}, {r.right - u, r.bottom}, {r.left + u, r.bottom}}, fill, stroke, sw, dash);
            }
            break;
        }
        case Shape::Cylinder: {
            // The body down its sides and round the bottom's front half (the
            // top edge left open), then the top.
            const float e = std::min(h / 6, 12.f), k = 0.5523f, rx = w / 2, ry = e / 2, y = r.bottom - ry;
            b_.Begin(D2D1::Point2F(r.right, r.top + ry));
            b_.LineTo(D2D1::Point2F(r.right, y));
            b_.CubicTo(D2D1::Point2F(r.right, y + ry * k), D2D1::Point2F(cx + rx * k, r.bottom), D2D1::Point2F(cx, r.bottom));
            b_.CubicTo(D2D1::Point2F(cx - rx * k, r.bottom), D2D1::Point2F(r.left, y + ry * k), D2D1::Point2F(r.left, y));
            b_.LineTo(D2D1::Point2F(r.left, r.top + ry));
            b_.End(fill, stroke, sw, dash);
            b_.Ellipse(D2D1::RectF(r.left, r.top, r.right, r.top + e), fill, stroke, sw, dash);
            break;
        }
        case Shape::Subroutine:
            b_.Rect(r, 0, fill, stroke, sw, dash);
            b_.Begin(D2D1::Point2F(r.left + 8, r.top));
            b_.LineTo(D2D1::Point2F(r.left + 8, r.bottom));
            b_.MoveTo(D2D1::Point2F(r.right - 8, r.top));
            b_.LineTo(D2D1::Point2F(r.right - 8, r.bottom));
            b_.End(Role(kDgNone), stroke, sw);
            break;
        case Shape::Arrow: b_.Polygon(ArrowPoints(bl.arrow, r), fill, stroke, sw, dash); break;
        default: b_.Rect(r, 0, fill, stroke, sw, dash); break;
        }
        const M& m = text_[i];
        b_.Text(m, D2D1::Point2F(cx - m.w / 2, cy - m.h / 2), st.color.role != kDgNone ? st.color : Role(kDgText), bl.pos, bl.len);
    }

    // Mermaid's block arrow for its directions (right, left, up, down; x
    // both across, y both up and down), in its box.
    static std::vector<D2D1_POINT_2F> ArrowPoints(std::wstring_view dirs, const D2D1_RECT_F& r) {
        bool right = false, left = false, up = false, down = false;
        size_t i = 0;
        while (i < dirs.size()) {
            size_t e = i;
            while (e < dirs.size() && dirs[e] != L',' && dirs[e] != L' ') ++e;
            const std::wstring_view d = dirs.substr(i, e - i);
            if (d == L"right" || d == L"x") right = true;
            if (d == L"left" || d == L"x") left = true;
            if (d == L"up" || d == L"y") up = true;
            if (d == L"down" || d == L"y") down = true;
            i = e + 1;
        }
        if (!right && !left && !up && !down) right = true;
        const float W = r.right - r.left, H = r.bottom - r.top, m = H / 2, p = 4;
        std::vector<D2D1_POINT_2F> q;  // from the bottom left, up negative
        if (right && left && up && down) {
            q = {{0, 0}, {m, 0}, {W / 2, 2 * p}, {W - m, 0}, {W, 0}, {W, -H / 3}, {W + 2 * p, -H / 2}, {W, -2 * H / 3},
                 {W, -H}, {W - m, -H}, {W / 2, -H - 2 * p}, {m, -H}, {0, -H}, {0, -2 * H / 3}, {-2 * p, -H / 2}, {0, -H / 3}};
        } else if (right && left && up) {
            q = {{m, 0}, {W - m, 0}, {W, -H / 2}, {W - m, -H}, {m, -H}, {0, -H / 2}};
        } else if (right && left && down) {
            q = {{0, 0}, {m, -H}, {W - m, -H}, {W, 0}};
        } else if (right && up && down) {
            q = {{0, 0}, {W, -m}, {W, -H + m}, {0, -H}};
        } else if (left && up && down) {
            q = {{W, 0}, {0, -m}, {0, -H + m}, {W, -H}};
        } else if (right && left) {
            q = {{m, 0}, {m, -p}, {W - m, -p}, {W - m, 0}, {W, -H / 2}, {W - m, -H}, {W - m, -H + p}, {m, -H + p}, {m, -H}, {0, -H / 2}};
        } else if (up && down) {
            q = {{W / 2, 0}, {0, -p}, {m, -p}, {m, -H + p}, {0, -H + p}, {W / 2, -H}, {W, -H + p}, {W - m, -H + p}, {W - m, -p}, {W, -p}};
        } else if (right && up) {
            q = {{0, 0}, {W, -m}, {0, -H}};
        } else if (right && down) {
            q = {{0, 0}, {W, 0}, {0, -H}};
        } else if (left && up) {
            q = {{W, 0}, {0, -m}, {W, -H}};
        } else if (left && down) {
            q = {{W, 0}, {0, 0}, {W, -H}};
        } else if (right) {
            q = {{m, -p}, {W - m, -p}, {W - m, 0}, {W, -H / 2}, {W - m, -H}, {W - m, -H + p}, {m, -H + p}};
        } else if (left) {
            q = {{m, 0}, {m, -p}, {W - m, -p}, {W - m, -H + p}, {m, -H + p}, {m, -H}, {0, -H / 2}};
        } else if (up) {
            q = {{m, -p}, {m, -H + p}, {0, -H + p}, {W / 2, -H}, {W, -H + p}, {W - m, -H + p}, {W - m, -p}};
        } else {
            q = {{W / 2, 0}, {0, -p}, {m, -p}, {m, -H + p}, {W - m, -H + p}, {W - m, -p}, {W, -p}};
        }
        std::vector<D2D1_POINT_2F> pts;
        for (const D2D1_POINT_2F& o : q) pts.push_back(D2D1::Point2F(r.left + o.x, r.bottom + o.y));
        return pts;
    }

    D2D1_POINT_2F Center(int i) const {
        const D2D1_RECT_F& r = rect_[static_cast<size_t>(i)];
        return D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    }

    D2D1_POINT_2F Border(int i, D2D1_POINT_2F toward) const {
        const D2D1_RECT_F& r = rect_[static_cast<size_t>(i)];
        const Shape s = p_.blocks[static_cast<size_t>(i)].shape;
        const D2D1_POINT_2F c = Center(i);
        if (s == Shape::Circle || s == Shape::DoubleCircle) return ClipEllipse(c, (r.right - r.left) / 2, (r.bottom - r.top) / 2, toward);
        if (s == Shape::Diamond) return ClipPolygon(c, {{c.x, r.top}, {r.right, c.y}, {c.x, r.bottom}, {r.left, c.y}}, toward);
        return ClipRect(c, r.right - r.left, r.bottom - r.top, toward);
    }

    void DrawEdge(const Edge& e) {
        if (e.from == e.to) return;
        const D2D1_POINT_2F a = Center(e.from), z = Center(e.to);
        D2D1_POINT_2F p0 = Border(e.from, z), p1 = Border(e.to, a);
        const DgColor line = Role(kDgLine);
        const float width = e.thick ? 3.5f : 1.f;
        const D2D1_POINT_2F tip = p1, start = p0;
        if (e.end != Marker::None) p1 = Retreat(p1, p0, MarkerLength(e.end));
        if (e.start != Marker::None) p0 = Retreat(p0, p1, MarkerLength(e.start));
        b_.Begin(p0);
        b_.LineTo(p1);
        b_.End(Role(kDgNone), line, width, e.dotted ? 2 : 0);
        AddMarker(b_, e.end, tip, start, line, width);
        AddMarker(b_, e.start, start, tip, line, width);
    }

    void DrawEdgeLabel(const Edge& e) {
        if (e.label.empty() || e.from == e.to) return;
        const D2D1_POINT_2F a = Border(e.from, Center(e.to)), z = Border(e.to, Center(e.from));
        const M m = b_.Measure(ParseLabel(e.label, false), 0, false, 1.f);
        const D2D1_POINT_2F c = D2D1::Point2F((a.x + z.x) / 2, (a.y + z.y) / 2);
        b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
        b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), e.pos, e.len);
    }

    const Parser& p_;
    SceneBuilder& b_;
    size_t n_;
    std::vector<M> text_;
    std::vector<float> w_, h_;
    std::vector<D2D1_RECT_F> rect_;
    std::map<size_t, float> cell_w_, cell_h_;
    std::map<size_t, int> rows_;
};

}  // namespace

std::shared_ptr<DiagramScene> BlockDiagram(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder).Build();
}

}  // namespace mdv::mermaid
