// Mermaid requirement diagrams: requirements (of six kinds) and elements as
// boxes with their type, name and fields, relationships between them
// (contains, copies, derives, satisfies, verifies, refines, traces) as
// labelled lines, laid out in layers as Mermaid (dagre) does.
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>
#include <unordered_map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

// Fields in the order Mermaid shows them.
enum Field { kId, kText, kRisk, kVerify, kType, kDocRef, kFields };
constexpr const wchar_t* kFieldNames[kFields] = {L"ID", L"Text", L"Risk", L"Verification", L"Type", L"Doc Ref"};

struct Item {
    std::wstring id;
    Text name;
    std::wstring type;                             // shown: Requirement, Element...
    Text fields[kFields];                          // (empty: not given)
    bool given[kFields] = {};
    std::vector<std::wstring> classes;
    Style style;
};

struct Relation {
    int from = -1, to = -1;
    std::wstring kind;  // contains, copies...
    uint32_t pos = 0, len = 0;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"requirementDiagram")) c_.Fail(Tr(L"Expected \"requirementDiagram\"", L"Ожидалось «requirementDiagram»"));
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
    }

    RankDir dir = RankDir::TB;
    std::vector<Item> items;
    std::vector<Relation> relations;
    std::map<std::wstring, Style> class_defs;

private:
    void Statement() {
        if (c_.EatKeyword(L"direction")) {
            c_.SkipBlanks();
            const std::wstring_view d = c_.LineRest();
            if (EqualsNoCase(d, L"TB") || EqualsNoCase(d, L"TD")) dir = RankDir::TB;
            else if (EqualsNoCase(d, L"BT")) dir = RankDir::BT;
            else if (EqualsNoCase(d, L"LR")) dir = RankDir::LR;
            else if (EqualsNoCase(d, L"RL")) dir = RankDir::RL;
            else c_.Fail(Tr(L"Unknown direction", L"Неизвестное направление"));
            return;
        }
        if (c_.EatKeyword(L"classDef")) {
            c_.SkipBlanks();
            const std::vector<std::wstring> names = Names();
            c_.SkipBlanks();
            Style st;
            st.Apply(c_.LineRest());
            for (const std::wstring& n : names) class_defs[n].Merge(st);
            return;
        }
        if (c_.EatKeyword(L"class")) {
            c_.SkipBlanks();
            const std::vector<std::wstring> ids = Names();
            c_.SkipBlanks();
            const std::vector<std::wstring> names = Names();
            for (const std::wstring& id : ids) {
                if (auto it = ids_.find(id); it != ids_.end()) {
                    std::vector<std::wstring>& c = items[static_cast<size_t>(it->second)].classes;
                    c.insert(c.end(), names.begin(), names.end());
                }
            }
            return;
        }
        if (c_.EatKeyword(L"style")) {
            c_.SkipBlanks();
            const std::vector<std::wstring> ids = Names();
            c_.SkipBlanks();
            Style st;
            st.Apply(c_.LineRest());
            for (const std::wstring& id : ids) {
                if (auto it = ids_.find(id); it != ids_.end()) items[static_cast<size_t>(it->second)].style.Merge(st);
            }
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
        for (const wchar_t* kw : {L"accTitle", L"title"}) {
            if (c_.EatKeyword(kw)) {
                c_.LineRest();
                return;
            }
        }
        static const std::pair<const wchar_t*, const wchar_t*> kKinds[] = {
            {L"requirement", L"Requirement"},
            {L"functionalRequirement", L"Functional Requirement"},
            {L"interfaceRequirement", L"Interface Requirement"},
            {L"performanceRequirement", L"Performance Requirement"},
            {L"physicalRequirement", L"Physical Requirement"},
            {L"designConstraint", L"Design Constraint"},
            {L"element", L"Element"},
        };
        for (const auto& [kw, shown] : kKinds) {
            if (c_.EatKeyword(kw)) return Declaration(shown);
        }
        // a - rel -> b | b <- rel - a | a[, b]:::class
        const size_t at = c_.pos();
        const Text a = Name();
        if (c_.Peek() == L',' || c_.Rest().starts_with(L":::")) {
            std::vector<int> targets{Index(a)};
            while (c_.Eat(L',')) {
                c_.SkipBlanks();
                targets.push_back(Index(Name()));
            }
            if (!c_.EatStr(L":::")) c_.Fail(Tr(L"Expected \":::\"", L"Ожидалось «:::»"));
            const std::vector<std::wstring> names = Names();
            for (const int t : targets) {
                std::vector<std::wstring>& cls = items[static_cast<size_t>(t)].classes;
                cls.insert(cls.end(), names.begin(), names.end());
            }
            return;
        }
        c_.SkipBlanks();
        const bool backwards = c_.EatStr(L"<-");
        if (!backwards && !c_.Eat(L'-')) {
            c_.set_pos(at);
            c_.Fail(Tr(L"Expected a requirement, an element or a relationship", L"Ожидалось требование, элемент или связь"));
        }
        c_.SkipBlanks();
        Relation r;
        const size_t k = c_.pos();
        while (IsWordChar(c_.Peek())) c_.Advance();
        r.kind = ToLower(src_.text.substr(k, c_.pos() - k));
        r.pos = static_cast<uint32_t>(k);
        r.len = static_cast<uint32_t>(c_.pos() - k);
        static const wchar_t* kRelations[] = {L"contains", L"copies", L"derives", L"satisfies", L"verifies", L"refines", L"traces"};
        if (std::find_if(std::begin(kRelations), std::end(kRelations), [&](const wchar_t* s) { return r.kind == s; }) == std::end(kRelations)) {
            c_.set_pos(k);
            c_.Fail(Tr(L"Unknown relationship: ", L"Неизвестная связь: ") + r.kind);
        }
        c_.SkipBlanks();
        if (!c_.EatStr(backwards ? L"-" : L"->")) c_.Fail(Tr(L"Expected \"->\"", L"Ожидалось «->»"));
        c_.SkipBlanks();
        const Text b = Name();
        r.from = Index(backwards ? b : a);
        r.to = Index(backwards ? a : b);
        if (relations.size() >= kMaxNodes) c_.Fail(Tr(L"Too many relationships", L"Слишком много связей"));
        relations.push_back(std::move(r));
    }

    // kind name[:::class] { key: value ... }
    void Declaration(const wchar_t* shown) {
        c_.SkipBlanks();
        const Text name = Name();
        Item& item = items[static_cast<size_t>(Index(name))];
        item.type = shown;
        while (c_.EatStr(L":::")) {
            const size_t s = c_.pos();
            while (IsWordChar(c_.Peek()) || c_.Peek() == L'-') c_.Advance();
            item.classes.emplace_back(src_.text.substr(s, c_.pos() - s));
        }
        c_.SkipBlanks();
        const size_t open = c_.pos();
        if (!c_.Eat(L'{')) c_.Fail(Tr(L"Expected '{'", L"Ожидалась «{»"));
        const size_t index = static_cast<size_t>(Index(name));
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) {
                c_.set_pos(open);
                c_.Fail(Tr(L"'{' without '}'", L"«{» без «}»"));
            }
            if (c_.Eat(L'}')) return;
            const size_t k = c_.pos();
            while (IsWordChar(c_.Peek())) c_.Advance();
            const std::wstring key = ToLower(src_.text.substr(k, c_.pos() - k));
            c_.SkipBlanks();
            if (key.empty() || !c_.Eat(L':')) c_.Fail(Tr(L"Expected key: value", L"Ожидалось ключ: значение"));
            c_.SkipBlanks();
            // The value: to the line's end (or a '}' closing on it).
            const size_t v = c_.pos();
            while (!c_.AtEnd() && c_.Peek() != L'\n' && c_.Peek() != L'}') c_.Advance();
            std::wstring_view value = Trim(src_.text.substr(v, c_.pos() - v));
            if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') value = value.substr(1, value.size() - 2);
            if (value.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
            Text t{std::wstring(value), static_cast<uint32_t>(value.data() - src_.text.data()), static_cast<uint32_t>(value.size())};
            // Shown as Mermaid shows them: a known word capitalized; the first
            // of a field given twice.
            auto capital = [](std::wstring s) {
                if (!s.empty()) s[0] = static_cast<wchar_t>(std::towupper(s[0]));
                return s;
            };
            Field f = kFields;
            if (key == L"id") f = kId;
            else if (key == L"text") f = kText;
            else if (key == L"risk") f = kRisk, t.raw = capital(ToLower(t.raw));
            else if (key == L"verifymethod") f = kVerify, t.raw = capital(ToLower(t.raw));
            else if (key == L"type") f = kType;
            else if (key == L"docref") f = kDocRef;
            else c_.Fail(Tr(L"Unknown field: ", L"Неизвестное поле: ") + key);
            Item& it = items[index];
            if (!it.given[f]) {
                it.fields[f] = t;
                it.given[f] = true;
            }
        }
    }

    // A name: word characters (and . - inside) or "quoted".
    Text Name() {
        if (c_.Peek() == L'"') {
            const size_t open = c_.pos();
            c_.Advance();
            const size_t s = c_.pos();
            while (!c_.AtEnd() && c_.Peek() != L'"' && c_.Peek() != L'\n') c_.Advance();
            if (!c_.Eat(L'"')) {
                c_.set_pos(open);
                c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
            }
            return Here(src_.text.substr(s, c_.pos() - 1 - s));
        }
        const size_t s = c_.pos();
        while (IsWordChar(c_.Peek()) || ((c_.Peek() == L'.' || c_.Peek() == L'-') && IsWordChar(c_.Peek(1)))) c_.Advance();
        if (c_.pos() == s) c_.Fail(Tr(L"Expected a name", L"Ожидалось имя"));
        return Here(src_.text.substr(s, c_.pos() - s));
    }

    int Index(const Text& name) {
        if (auto it = ids_.find(name.raw); it != ids_.end()) return it->second;
        if (items.size() >= kMaxNodes) c_.Fail(Tr(L"Too many requirements", L"Слишком много требований"));
        Item item;
        item.id = name.raw;
        item.name = name;
        ids_.emplace(name.raw, static_cast<int>(items.size()));
        items.push_back(std::move(item));
        return static_cast<int>(items.size()) - 1;
    }

    Text Here(std::wstring_view s) {
        if (s.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return Text{std::wstring(s), static_cast<uint32_t>(s.data() - src_.text.data()), static_cast<uint32_t>(s.size())};
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
};

using M = SceneBuilder::Measured;

constexpr float kPadX = 10, kPadY = 8, kWrap = 280, kMargin = 8;

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src) {}

    std::shared_ptr<DiagramScene> Build() {
        const size_t n = p_.items.size();
        boxes_.resize(n);
        LayeredGraph g;
        g.dir = p_.dir;
        for (size_t i = 0; i < n; ++i) {
            const Item& it = p_.items[i];
            Box& bx = boxes_[i];
            RichText type;
            type.text = L"<<" + (it.type.empty() ? std::wstring(L"Element") : it.type) + L">>";
            bx.type = b_.Measure(type, 0, false, 0.875f);
            bx.name = b_.Measure(ParseLabel(it.name.raw, true), kWrap, true, 1.f);
            float w = std::max(bx.type.w, bx.name.w), h = bx.type.h + bx.name.h;
            for (int f = 0; f < kFields; ++f) {
                if (!it.given[f]) continue;
                // "Key: value", the value's markdown kept.
                RichText v = ParseLabel(it.fields[f].raw, true);
                const std::wstring key = std::wstring(kFieldNames[f]) + L": ";
                for (RichText::Span& s : v.spans) s.start += static_cast<uint32_t>(key.size());
                v.text = key + v.text;
                bx.fields.push_back(b_.Measure(v, kWrap, false, 0.875f, false));
                bx.field_of.push_back(f);
                w = std::max(w, bx.fields.back().w);
            }
            float fields_h = 0;
            for (const M& m : bx.fields) fields_h += m.h;
            bx.w = std::max(w + 2 * kPadX, 100.f);
            bx.h = h + 2 * kPadY + (bx.fields.empty() ? 0.f : fields_h + 2 * kPadY);
            LayeredGraph::Node node;
            node.w = bx.w;
            node.h = bx.h;
            g.nodes.push_back(node);
        }
        for (const Relation& r : p_.relations) {
            RichText t;
            t.text = L"<<" + r.kind + L">>";
            labels_.push_back(b_.Measure(t, 0, false, 0.875f));
            if (r.from == r.to) {
                // A loop: room beside its box for it and its label.
                const size_t i = static_cast<size_t>(r.from);
                const int nth = boxes_[i].loops++;
                const bool across = p_.dir == RankDir::LR || p_.dir == RankDir::RL;
                boxes_[i].loop_labels += (across ? labels_.back().h : labels_.back().w) + 4;
                g.nodes[i].after = LoopReach(nth) * 0.75f + 6 + boxes_[i].loop_labels + 4;
                continue;
            }
            LayeredGraph::Edge e;
            e.from = r.from;
            e.to = r.to;
            e.label_w = labels_.back().w + 8;
            e.label_h = labels_.back().h + 4;
            g.edges.push_back(e);
        }
        layout_ = LayoutLayered(g);
        if (layout_.too_large) TooLarge();
        auto at = [&](D2D1_POINT_2F q) { return D2D1::Point2F(q.x + kMargin, q.y + kMargin); };
        for (size_t i = 0; i < n; ++i) boxes_[i].center = at(layout_.centers[i]);
        // (Edges index the graph's edges: loops aren't among them.)
        std::vector<size_t> edge_of(p_.relations.size(), 0);
        for (size_t k = 0, e = 0; k < p_.relations.size(); ++k) {
            if (p_.relations[k].from != p_.relations[k].to) edge_of[k] = e++;
        }
        // Relationships: dashed with an open arrow at the target; contains
        // solid with a circled plus at the container.
        const DgColor line = Role(kDgLine);
        std::vector<int> loops_drawn(n, 0);
        for (size_t k = 0; k < p_.relations.size(); ++k) {
            const Relation& r = p_.relations[k];
            if (r.from == r.to) {
                DrawLoop(r, k, loops_drawn[static_cast<size_t>(r.from)]++);
                continue;
            }
            const Box& a = boxes_[static_cast<size_t>(r.from)];
            const Box& z = boxes_[static_cast<size_t>(r.to)];
            std::vector<D2D1_POINT_2F> pts{a.center};
            for (const D2D1_POINT_2F& q : layout_.bends[edge_of[k]]) pts.push_back(at(q));
            pts.push_back(z.center);
            pts.front() = ClipRect(a.center, a.w, a.h, pts[1]);
            pts.back() = ClipRect(z.center, z.w, z.h, pts[pts.size() - 2]);
            const bool contains = r.kind == L"contains";
            const D2D1_POINT_2F tip = pts.back(), before = pts[pts.size() - 2];
            if (!contains) pts.back() = Retreat(tip, before, 1);
            D2D1_POINT_2F start = pts.front();
            if (contains) pts.front() = Retreat(start, pts[1], 14);
            AddBasisCurve(b_, pts);
            b_.End(Role(kDgNone), line, 1.f, contains ? 0 : 1);
            if (contains) {
                // A circle with a plus, at the container's border.
                const D2D1_POINT_2F c = Retreat(start, pts[1], 7);
                b_.Ellipse(D2D1::RectF(c.x - 7, c.y - 7, c.x + 7, c.y + 7), Role(kDgPage), line, 1.f);
                b_.Begin(D2D1::Point2F(c.x - 7, c.y));
                b_.LineTo(D2D1::Point2F(c.x + 7, c.y));
                b_.MoveTo(D2D1::Point2F(c.x, c.y - 7));
                b_.LineTo(D2D1::Point2F(c.x, c.y + 7));
                b_.End(Role(kDgNone), line, 1.f);
            } else {
                AddMarker(b_, Marker::LineArrow, tip, before, line, 1.f);
            }
        }
        for (size_t k = 0; k < p_.relations.size(); ++k) {
            const Relation& r = p_.relations[k];
            if (r.from == r.to) continue;
            const M& m = labels_[k];
            const D2D1_POINT_2F c = at(layout_.labels[edge_of[k]]);
            b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
            b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), r.pos, r.len);
        }
        for (size_t i = 0; i < n; ++i) DrawBox(i);
        // The front matter's title, centred above (Mermaid's diagram title).
        if (!src_.title.empty()) {
            float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f;
            for (const DiagramScene::Shape& sh : b_.scene().shapes) {
                x0 = std::min(x0, sh.box.left);
                x1 = std::max(x1, sh.box.right);
                y0 = std::min(y0, sh.box.top);
            }
            if (x0 > x1) x0 = x1 = y0 = 0;
            const M m = b_.Measure(ParseLabel(src_.title, false), 0, false, 18 / b_.size());
            b_.Text(m, D2D1::Point2F((x0 + x1) / 2 - m.w / 2, y0 - m.h - 10), Role(kDgText), src_.title_pos, src_.title_len);
        }
        return b_.FinishFitted(kMargin);
    }

private:
    struct Box {
        float w = 0, h = 0;
        D2D1_POINT_2F center{};
        M type, name;
        std::vector<M> fields;
        std::vector<int> field_of;  // the Field each is
        int loops = 0;              // relations to itself
        float loop_labels = 0;      // their labels' length, one after another
    };

    static float LoopReach(int n) { return 36 * (1 + 0.6f * static_cast<float>(n)) + 16; }

    // A relation of an item to itself: a loop beside its box (below it
    // sideways), one inside another, the label past them.
    void DrawLoop(const Relation& r, size_t k, int n) {
        const Box& bx = boxes_[static_cast<size_t>(r.from)];
        const bool below = p_.dir == RankDir::LR || p_.dir == RankDir::RL;
        auto at = [&](float out, float side) {
            return below ? D2D1::Point2F(bx.center.x + side, bx.center.y + out) : D2D1::Point2F(bx.center.x + out, bx.center.y + side);
        };
        const float edge = (below ? bx.h : bx.w) / 2, across = (below ? bx.w : bx.h) / 2;
        const float s = std::min(std::min(across / 2, 12.f) + 20 * static_cast<float>(n), across - 2), reach = LoopReach(n);
        const D2D1_POINT_2F a0 = at(edge, -s), a1 = at(edge, s), c1 = at(edge + reach, -s - 12), c2 = at(edge + reach, s + 12);
        const DgColor line = Role(kDgLine);
        const bool contains = r.kind == L"contains";
        b_.Begin(a0);
        b_.CubicTo(c1, c2, a1);
        b_.End(Role(kDgNone), line, 1.f, contains ? 0 : 1);
        if (contains) {
            b_.Ellipse(D2D1::RectF(a0.x - 7, a0.y - 7, a0.x + 7, a0.y + 7), Role(kDgPage), line, 1.f);
            b_.Begin(D2D1::Point2F(a0.x - 7, a0.y));
            b_.LineTo(D2D1::Point2F(a0.x + 7, a0.y));
            b_.MoveTo(D2D1::Point2F(a0.x, a0.y - 7));
            b_.LineTo(D2D1::Point2F(a0.x, a0.y + 7));
            b_.End(Role(kDgNone), line, 1.f);
        } else {
            AddMarker(b_, Marker::LineArrow, a1, c2, line, 1.f);
        }
        if (n != bx.loops - 1) return;  // (the labels once, past the outermost)
        float offset = reach * 0.75f + 6;
        for (size_t j = 0; j <= k; ++j) {
            const Relation& o = p_.relations[j];
            if (o.from != r.from || o.to != r.from) continue;
            const M& m = labels_[j];
            const float len = below ? m.h : m.w;
            const D2D1_POINT_2F c = at(edge + offset + len / 2, 0);
            b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
            b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), o.pos, o.len);
            offset += len + 4;
        }
    }

    void DrawBox(size_t i) {
        const Item& it = p_.items[i];
        const Box& bx = boxes_[i];
        Style st;
        if (auto d = p_.class_defs.find(L"default"); d != p_.class_defs.end()) st.Merge(d->second);
        for (const std::wstring& c : it.classes) {
            if (auto d = p_.class_defs.find(c); d != p_.class_defs.end()) st.Merge(d->second);
        }
        st.Merge(it.style);
        const DgColor fill = st.fill.role != kDgNone ? st.fill : Role(kDgNodeFill);
        const DgColor stroke = st.stroke.role != kDgNone ? st.stroke : Role(kDgNodeStroke);
        const DgColor text = st.color.role != kDgNone ? st.color : Role(kDgText);
        const D2D1_RECT_F r = D2D1::RectF(bx.center.x - bx.w / 2, bx.center.y - bx.h / 2, bx.center.x + bx.w / 2, bx.center.y + bx.h / 2);
        b_.Rect(r, 0, fill, stroke, st.stroke_width >= 0 ? st.stroke_width : 1.f, st.dash == 1 ? 1 : 0);
        float y = r.top + kPadY;
        b_.Text(bx.type, D2D1::Point2F(bx.center.x - bx.type.w / 2, y), text);
        y += bx.type.h;
        b_.Text(bx.name, D2D1::Point2F(bx.center.x - bx.name.w / 2, y), text, it.name.pos, it.name.len);
        y += bx.name.h + kPadY;
        if (bx.fields.empty()) return;
        b_.Begin(D2D1::Point2F(r.left, y));
        b_.LineTo(D2D1::Point2F(r.right, y));
        b_.End(Role(kDgNone), stroke, 1.f);
        y += kPadY;
        for (size_t k = 0; k < bx.fields.size(); ++k) {
            const Text& v = it.fields[bx.field_of[k]];
            b_.Text(bx.fields[k], D2D1::Point2F(r.left + kPadX, y), text, v.pos, v.len);
            y += bx.fields[k].h;
        }
    }

    const Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
    std::vector<Box> boxes_;
    std::vector<M> labels_;
    LayeredLayout layout_;
};

// Whether the layout will fit its budget (see the flowchart's).
void CheckBudget(const Parser& p) {
    LayeredGraph g;
    for (size_t i = 0; i < p.items.size(); ++i) {
        LayeredGraph::Node n;
        n.w = n.h = 10;
        g.nodes.push_back(n);
    }
    for (const Relation& r : p.relations) {
        if (r.from != r.to) g.edges.push_back({r.from, r.to});
    }
    if (LayoutLayered(g, true).too_large) TooLarge();
}

}  // namespace

std::shared_ptr<DiagramScene> RequirementDiagram(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) {
        CheckBudget(p);
        return nullptr;
    }
    return Builder(p, *builder, src).Build();
}

}  // namespace mdv::mermaid
