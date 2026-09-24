// Mermaid entity relationship diagrams: entities with their attributes (type,
// name, keys, comment), relationships with crow's foot cardinalities (solid
// when identifying, dashed when not) and labels, entity aliases and styles,
// laid out in layers as Mermaid (dagre) does.
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <map>
#include <unordered_map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;  // in the source
};

enum class Card : uint8_t { ZeroOrOne, ExactlyOne, ZeroOrMore, OneOrMore };

struct Attribute {
    Text type, name, keys, comment;
};

struct Entity {
    std::wstring id;
    Text name;                  // where it is first named
    Text label;                 // p[Person]
    std::vector<Attribute> attrs;
    std::vector<std::wstring> classes;
    Style style;
};

struct Relationship {
    int a = -1, b = -1;
    Card ca = Card::ExactlyOne, cb = Card::ExactlyOne;  // at a, at b
    bool identifying = true;
    Text label;
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"erDiagram")) c_.Fail(Tr(L"Expected \"erDiagram\"", L"Ожидалось «erDiagram»"));
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
    }

    RankDir dir = RankDir::TB;
    std::vector<Entity> entities;
    std::vector<Relationship> relationships;
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
            const std::wstring name(c_.LineRest());
            for (const std::wstring& id : ids) {
                if (auto it = ids_.find(id); it != ids_.end()) entities[static_cast<size_t>(it->second)].classes.push_back(name);
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
                if (auto it = ids_.find(id); it != ids_.end()) entities[static_cast<size_t>(it->second)].style.Merge(st);
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
        if (c_.EatKeyword(L"accTitle")) {
            c_.LineRest();
            return;
        }
        const int a = EntityRef();
        c_.SkipBlanks();
        if (c_.Eat(L'{')) return Attributes(a);
        if (c_.AtStatementEnd()) return;  // an entity on its own
        Relationship r;
        r.a = a;
        if (!CardAt(true, &r.ca)) c_.Fail(Tr(L"Expected a relationship", L"Ожидалась связь"));
        c_.SkipBlanks();
        if (c_.EatStr(L"--") || c_.EatKeyword(L"to")) {
            r.identifying = true;
        } else if (c_.EatStr(L"..")) {
            r.identifying = false;
        } else if (c_.EatKeyword(L"optionally")) {
            c_.SkipBlanks();
            if (!c_.EatKeyword(L"to")) c_.Fail(Tr(L"Expected \"to\"", L"Ожидалось «to»"));
            r.identifying = false;
        } else {
            c_.Fail(Tr(L"Expected a relationship", L"Ожидалась связь"));
        }
        c_.SkipBlanks();
        if (!CardAt(false, &r.cb)) c_.Fail(Tr(L"Expected a cardinality", L"Ожидалась кратность"));
        c_.SkipBlanks();
        r.b = EntityRef();
        c_.SkipBlanks();
        if (!c_.Eat(L':')) c_.Fail(Tr(L"Expected ':' and a label", L"Ожидались «:» и подпись"));
        c_.SkipBlanks();
        if (c_.Peek() == L'"') {
            r.label = Quoted();
        } else {
            const std::wstring_view l = c_.LineRest();
            r.label.raw = std::wstring(l);
            r.label.pos = static_cast<uint32_t>(l.data() - src_.text.data());
            r.label.len = static_cast<uint32_t>(l.size());
        }
        if (r.label.raw.size() > kMaxLabel) c_.Fail(Tr(L"Label too long", L"Слишком длинная подпись"));
        if (relationships.size() >= kMaxEdges) {
            c_.Fail(Tr(L"Too many relationships: Mermaid draws at most 500", L"Слишком много связей: Mermaid рисует не больше 500"));
        }
        relationships.push_back(std::move(r));
    }

    // A cardinality: a symbol (|o || }o }| on the left, o| || o{ |{ on the
    // right) or its words (zero or more, 1+, only one...).
    bool CardAt(bool left, Card* card) {
        struct Symbol {
            const wchar_t* text;
            Card card;
        };
        static constexpr Symbol kLeft[] = {{L"|o", Card::ZeroOrOne}, {L"||", Card::ExactlyOne},
                                           {L"}o", Card::ZeroOrMore}, {L"}|", Card::OneOrMore}};
        static constexpr Symbol kRight[] = {{L"o|", Card::ZeroOrOne}, {L"||", Card::ExactlyOne},
                                            {L"o{", Card::ZeroOrMore}, {L"|{", Card::OneOrMore}};
        for (const Symbol& s : left ? kLeft : kRight) {
            if (c_.EatStr(s.text)) {
                *card = s.card;
                return true;
            }
        }
        static constexpr Symbol kWords[] = {
            {L"zero or more", Card::ZeroOrMore}, {L"zero or many", Card::ZeroOrMore}, {L"zero or one", Card::ZeroOrOne},
            {L"one or zero", Card::ZeroOrOne},   {L"one or more", Card::OneOrMore},   {L"one or many", Card::OneOrMore},
            {L"only one", Card::ExactlyOne},     {L"many(1)", Card::OneOrMore},       {L"many(0)", Card::ZeroOrMore},
            {L"1+", Card::OneOrMore},            {L"0+", Card::ZeroOrMore},           {L"1", Card::ExactlyOne},
        };
        for (const Symbol& s : kWords) {
            const size_t len = std::wcslen(s.text);
            const std::wstring_view r = c_.Rest();
            if (r.size() >= len && EqualsNoCase(r.substr(0, len), s.text) && (r.size() == len || !IsWordChar(r[len]))) {
                c_.Advance(len);
                *card = s.card;
                return true;
            }
        }
        return false;
    }

    // NAME, NAME-2, "A name", then [Alias] / ["Alias"] and :::class.
    int EntityRef() {
        Text name;
        if (c_.Peek() == L'"') {
            name = Quoted();
        } else {
            const size_t start = c_.pos();
            while (!c_.AtEnd()) {
                const wchar_t ch = c_.Peek();
                // (- inside a name, not where a relationship starts: A--B, A-to...)
                if (IsWordChar(ch) || (ch == L'-' && c_.pos() > start && IsWordChar(c_.Peek(1)))) {
                    c_.Advance();
                } else {
                    break;
                }
            }
            if (c_.pos() == start) c_.Fail(Tr(L"Expected an entity", L"Ожидалась сущность"));
            name.raw = std::wstring(src_.text.substr(start, c_.pos() - start));
            name.pos = static_cast<uint32_t>(start);
            name.len = static_cast<uint32_t>(name.raw.size());
        }
        const int e = EntityIndex(name);
        if (c_.Peek() == L'[') {
            c_.Advance();
            c_.SkipBlanks();
            Text label;
            if (c_.Peek() == L'"') {
                label = Quoted();
            } else {
                const size_t start = c_.pos();
                while (!c_.AtEnd() && c_.Peek() != L']' && c_.Peek() != L'\n') c_.Advance();
                const std::wstring_view l = Trim(src_.text.substr(start, c_.pos() - start));
                label.raw = std::wstring(l);
                label.pos = static_cast<uint32_t>(l.data() - src_.text.data());
                label.len = static_cast<uint32_t>(l.size());
            }
            c_.SkipBlanks();
            if (!c_.Eat(L']')) c_.Fail(Tr(L"Expected ']'", L"Ожидалась «]»"));
            if (!label.raw.empty()) entities[static_cast<size_t>(e)].label = std::move(label);
        }
        while (c_.EatStr(L":::")) {
            const size_t s = c_.pos();
            while (!c_.AtEnd() && (IsWordChar(c_.Peek()) || c_.Peek() == L'-')) c_.Advance();
            entities[static_cast<size_t>(e)].classes.emplace_back(src_.text.substr(s, c_.pos() - s));
        }
        return e;
    }

    int EntityIndex(const Text& name) {
        if (auto it = ids_.find(name.raw); it != ids_.end()) return it->second;
        if (entities.size() >= kMaxNodes) c_.Fail(Tr(L"Too many entities", L"Слишком много сущностей"));
        Entity en;
        en.id = name.raw;
        en.name = name;
        ids_.emplace(name.raw, static_cast<int>(entities.size()));
        entities.push_back(std::move(en));
        return static_cast<int>(entities.size()) - 1;
    }

    // { type name [PK, FK, UK] ["comment"] ... }
    void Attributes(int e) {
        const size_t open = c_.pos() - 1;
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) {
                c_.set_pos(open);
                c_.Fail(Tr(L"'{' without '}'", L"«{» без «}»"));
            }
            if (c_.Eat(L'}')) return;
            Attribute a;
            a.type = Token();
            c_.SkipBlanks();
            if (c_.AtStatementEnd() || c_.Peek() == L'}') c_.Fail(Tr(L"Expected an attribute name", L"Ожидалось имя атрибута"));
            a.name = Token();
            c_.SkipBlanks();
            // Keys: PK, FK, UK, with commas between.
            const size_t keys = c_.pos();
            while (!c_.AtStatementEnd() && c_.Peek() != L'"' && c_.Peek() != L'}') c_.Advance();
            const std::wstring_view k = Trim(src_.text.substr(keys, c_.pos() - keys));
            a.keys.raw = std::wstring(k);
            a.keys.pos = static_cast<uint32_t>(k.data() - src_.text.data());
            a.keys.len = static_cast<uint32_t>(k.size());
            if (c_.Peek() == L'"') a.comment = Quoted();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd() && c_.Peek() != L'}') c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
            Entity& en = entities[static_cast<size_t>(e)];
            if (en.attrs.size() >= kMaxNodes) c_.Fail(Tr(L"Too many attributes", L"Слишком много атрибутов"));
            en.attrs.push_back(std::move(a));
        }
    }

    Text Token() {
        const size_t start = c_.pos();
        while (!c_.AtStatementEnd() && !IsSpace(c_.Peek()) && c_.Peek() != L'}' && c_.Peek() != L'"') c_.Advance();
        Text t;
        t.raw = std::wstring(src_.text.substr(start, c_.pos() - start));
        t.pos = static_cast<uint32_t>(start);
        t.len = static_cast<uint32_t>(t.raw.size());
        if (t.raw.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return t;
    }

    Text Quoted() {
        const size_t open = c_.pos();
        c_.Eat(L'"');
        const size_t start = c_.pos();
        while (!c_.AtEnd() && c_.Peek() != L'"' && c_.Peek() != L'\n') c_.Advance();
        if (c_.Peek() != L'"') {
            c_.set_pos(open);
            c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        }
        Text t;
        t.raw = std::wstring(src_.text.substr(start, c_.pos() - start));
        t.pos = static_cast<uint32_t>(start);
        t.len = static_cast<uint32_t>(t.raw.size());
        if (t.raw.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        c_.Advance();
        return t;
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

// ---------------------------------------------------------------------------
// Layout and drawing
// ---------------------------------------------------------------------------

constexpr float kPadX = 12, kPadY = 8;   // the name
constexpr float kCellX = 8, kCellY = 3;  // attribute cells
constexpr float kMinW = 100;
constexpr float kMargin = 8;
constexpr float kLoopReach = 36;
constexpr float kSmall = 0.875f;

using M = SceneBuilder::Measured;

struct Box {
    float w = 0, h = 0;
    D2D1_POINT_2F center{};
    M name;
    float header = 0;                        // the name's band
    std::vector<std::array<M, 4>> cells;     // per attribute: type, name, keys, comment
    std::array<float, 4> columns{};          // their widths (0: no such column)
    std::vector<float> rows;                 // row heights
    Style style;
    int loops = 0;
    float loop_side = 0;                     // its loops' labels, across
};

// Crow's foot ends, from the entity out: the maximum (a bar for one, a
// fork for many) at the entity, the minimum (a circle for zero, a bar for
// one) further out.
void AddCard(SceneBuilder& b, Card card, D2D1_POINT_2F tip, D2D1_POINT_2F from, DgColor color) {
    float dx = from.x - tip.x, dy = from.y - tip.y;
    const float len = std::hypot(dx, dy);
    if (len < 1e-3f) return;
    dx /= len;
    dy /= len;
    auto at = [&](float out, float side) { return D2D1::Point2F(tip.x + dx * out - dy * side, tip.y + dy * out + dx * side); };
    auto bar = [&](float out) {
        b.Begin(at(out, -8));
        b.LineTo(at(out, 8));
        b.End(Role(kDgNone), color, 1.f);
    };
    auto circle = [&](float out) {
        const D2D1_POINT_2F c = at(out, 0);
        b.Ellipse(D2D1::RectF(c.x - 4.5f, c.y - 4.5f, c.x + 4.5f, c.y + 4.5f), Role(kDgPage), color, 1.f);
    };
    const bool many = card == Card::ZeroOrMore || card == Card::OneOrMore;
    if (many) {
        b.Begin(at(0, -9));
        b.LineTo(at(16, 0));
        b.LineTo(at(0, 9));
        b.MoveTo(at(0, 0));
        b.LineTo(at(16, 0));
        b.End(Role(kDgNone), color, 1.f);
    } else {
        bar(9);
    }
    const bool zero = card == Card::ZeroOrOne || card == Card::ZeroOrMore;
    if (zero) circle(many ? 22.f : 19.f);
    else bar(many ? 21.f : 15.f);
}

class Builder {
public:
    Builder(Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src) {}

    std::shared_ptr<DiagramScene> Build() {
        MeasureAll();
        Layout();
        float top = kMargin;
        M title;
        if (!src_.title.empty()) {
            title = b_.Measure(ParseLabel(src_.title, false), 0, false, 1.125f);
            top += title.h + 8;
        }
        const float width = std::max(layout_.width, title.w) + 2 * kMargin;
        const float height = top + layout_.height + kMargin;
        ox_ = kMargin + (width - 2 * kMargin - layout_.width) / 2;
        oy_ = top;
        for (size_t i = 0; i < boxes_.size(); ++i) {
            boxes_[i].center = D2D1::Point2F(ox_ + layout_.centers[i].x, oy_ + layout_.centers[i].y);
        }
        if (title.layout) b_.Text(title, D2D1::Point2F((width - title.w) / 2, kMargin), Role(kDgText), src_.title_pos, src_.title_len);
        DrawRelationships();
        for (size_t i = 0; i < boxes_.size(); ++i) DrawEntity(i);
        return b_.Finish(width, height);
    }

private:
    bool sideways() const { return p_.dir == RankDir::LR || p_.dir == RankDir::RL; }

    Style EntityStyle(const Entity& e) const {
        Style st;
        if (auto it = p_.class_defs.find(L"default"); it != p_.class_defs.end()) st.Merge(it->second);
        for (const std::wstring& n : e.classes) {
            if (auto it = p_.class_defs.find(n); it != p_.class_defs.end()) st.Merge(it->second);
        }
        st.Merge(e.style);
        return st;
    }

    // Plain text: no markup in types and names (varchar(255), string[]).
    M Plain(const std::wstring& s, float scale) {
        RichText t;
        t.text = s;
        return b_.Measure(t, 0, false, scale, false);
    }

    void MeasureAll() {
        boxes_.resize(p_.entities.size());
        for (size_t i = 0; i < p_.entities.size(); ++i) {
            const Entity& e = p_.entities[i];
            Box& b = boxes_[i];
            b.style = EntityStyle(e);
            RichText name = ParseLabel(e.label.raw.empty() ? e.id : e.label.raw, false);
            if (b.style.bold == 1 || b.style.italic == 1) {
                if (name.spans.empty() && !name.text.empty()) name.spans.push_back({0, static_cast<uint32_t>(name.text.size()), false, false});
                for (auto& s : name.spans) {
                    s.bold = s.bold || b.style.bold == 1;
                    s.italic = s.italic || b.style.italic == 1;
                }
            }
            b.name = b_.Measure(name, 0, false, 1.f);
            b.header = b.name.h + 2 * kPadY;
            float w = b.name.w + 2 * kPadX, h = b.header;
            for (const Attribute& a : e.attrs) {
                // Generics as Mermaid shows them: type~T~ is type<T>.
                std::wstring type = a.type.raw;
                for (size_t k = 0, n = 0; k < type.size(); ++k) {
                    if (type[k] == L'~') type[k] = (n++ % 2 == 0) ? L'<' : L'>';
                }
                std::array<M, 4> row{Plain(type, kSmall), Plain(a.name.raw, kSmall),
                                     a.keys.raw.empty() ? M{} : Plain(a.keys.raw, kSmall),
                                     a.comment.raw.empty() ? M{} : b_.Measure(ParseLabel(a.comment.raw, false), 0, false, kSmall, false)};
                float rh = 0;
                for (size_t c = 0; c < 4; ++c) {
                    if (row[c].layout) {
                        b.columns[c] = std::max(b.columns[c], row[c].w + 2 * kCellX);
                        rh = std::max(rh, row[c].h);
                    }
                }
                b.rows.push_back(rh + 2 * kCellY);
                h += b.rows.back();
                b.cells.push_back(std::move(row));
            }
            float table = 0;
            for (float c : b.columns) table += c;
            // Wider than its columns: the last one takes the rest.
            w = std::max({w, table, kMinW});
            if (table > 0 && w > table) {
                for (size_t c = 4; c-- > 0;) {
                    if (b.columns[c] > 0) {
                        b.columns[c] += w - table;
                        break;
                    }
                }
            }
            b.w = w;
            b.h = e.attrs.empty() ? std::max(h, 50.f) : h;
            if (e.attrs.empty()) b.header = b.h;
        }
        for (const Relationship& r : p_.relationships) {
            labels_.push_back(r.label.raw.empty() ? M{} : b_.Measure(ParseLabel(r.label.raw, false), 0, false, kSmall));
        }
    }

    void Layout() {
        LayeredGraph g;
        g.dir = p_.dir;
        for (const Box& b : boxes_) {
            LayeredGraph::Node n;
            n.w = b.w;
            n.h = b.h;
            g.nodes.push_back(n);
        }
        std::vector<float> loop_labels(boxes_.size(), 0.f);
        slot_.assign(p_.relationships.size(), -1);
        for (size_t k = 0; k < p_.relationships.size(); ++k) {
            const Relationship& r = p_.relationships[k];
            if (r.a == r.b) {
                Box& b = boxes_[static_cast<size_t>(r.a)];
                ++b.loops;
                if (labels_[k].layout) {
                    loop_labels[static_cast<size_t>(r.a)] += (sideways() ? labels_[k].h : labels_[k].w) + 6;
                    b.loop_side = std::max(b.loop_side, sideways() ? labels_[k].w : labels_[k].h);
                }
                continue;
            }
            LayeredGraph::Edge e;
            e.from = r.a;
            e.to = r.b;
            e.minlen = 2;  // room at both ends for the cardinalities
            if (labels_[k].layout) {
                e.label_w = labels_[k].w + 8;
                e.label_h = labels_[k].h + 4;
            }
            slot_[k] = static_cast<int>(g.edges.size());
            g.edges.push_back(e);
        }
        for (size_t i = 0; i < boxes_.size(); ++i) {
            if (boxes_[i].loops > 0) {
                g.nodes[i].after = (kLoopReach * (1 + 0.6f * static_cast<float>(boxes_[i].loops - 1)) + 16) * 0.75f + 6 + loop_labels[i];
                float& across = sideways() ? g.nodes[i].w : g.nodes[i].h;  // as wide as the labels
                across = std::max(across, boxes_[i].loop_side + 8);
            }
        }
        layout_ = LayoutLayered(g);
        if (layout_.too_large) TooLarge();
    }

    void DrawRelationships() {
        const DgColor line = Role(kDgLine);
        std::vector<int> loops(boxes_.size(), 0);
        std::vector<float> loop_labels(boxes_.size(), 0.f);
        std::vector<size_t> labelled;
        for (size_t k = 0; k < p_.relationships.size(); ++k) {
            const Relationship& r = p_.relationships[k];
            const uint8_t dash = r.identifying ? 0 : 1;
            if (slot_[k] < 0) {
                DrawLoop(r, k, loops[static_cast<size_t>(r.a)]++, &loop_labels[static_cast<size_t>(r.a)], dash);
                continue;
            }
            const Box& a = boxes_[static_cast<size_t>(r.a)];
            const Box& b = boxes_[static_cast<size_t>(r.b)];
            std::vector<D2D1_POINT_2F> pts;
            pts.push_back(a.center);
            for (const D2D1_POINT_2F& q : layout_.bends[static_cast<size_t>(slot_[k])]) pts.push_back(D2D1::Point2F(ox_ + q.x, oy_ + q.y));
            pts.push_back(b.center);
            pts.front() = ClipRect(a.center, a.w, a.h, pts[1]);
            pts.back() = ClipRect(b.center, b.w, b.h, pts[pts.size() - 2]);
            AddBasisCurve(b_, pts);
            b_.End(Role(kDgNone), line, 1.f, dash);
            AddCard(b_, r.ca, pts.front(), pts[1], line);
            AddCard(b_, r.cb, pts.back(), pts[pts.size() - 2], line);
            if (labels_[k].layout) labelled.push_back(k);
        }
        for (size_t k : labelled) {
            const M& m = labels_[k];
            const D2D1_POINT_2F lp = layout_.labels[static_cast<size_t>(slot_[k])];
            const D2D1_POINT_2F c = D2D1::Point2F(ox_ + lp.x, oy_ + lp.y);
            b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
            b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), p_.relationships[k].label.pos,
                    p_.relationships[k].label.len);
        }
    }

    // An entity related to itself: a loop beside it (below it sideways),
    // its label past the outermost loop.
    void DrawLoop(const Relationship& r, size_t k, int n, float* labels, uint8_t dash) {
        const Box& b = boxes_[static_cast<size_t>(r.a)];
        const bool below = sideways();
        auto at = [&](float out, float side) {
            return below ? D2D1::Point2F(b.center.x + side, b.center.y + out) : D2D1::Point2F(b.center.x + out, b.center.y + side);
        };
        const float edge = (below ? b.h : b.w) / 2, s = (below ? b.w : b.h) / 4;
        const float reach = kLoopReach * (1 + 0.6f * static_cast<float>(n)) + 16;
        const D2D1_POINT_2F a0 = at(edge, -s), a1 = at(edge, s);
        const D2D1_POINT_2F c1 = at(edge + reach, -s - 12), c2 = at(edge + reach, s + 12);
        const DgColor line = Role(kDgLine);
        b_.Begin(a0);
        b_.CubicTo(c1, c2, a1);
        b_.End(Role(kDgNone), line, 1.f, dash);
        AddCard(b_, r.ca, a0, c1, line);
        AddCard(b_, r.cb, a1, c2, line);
        const M& m = labels_[k];
        if (!m.layout) return;
        const float len = below ? m.h : m.w;
        const float start = std::max((kLoopReach * (1 + 0.6f * static_cast<float>(b.loops - 1)) + 16) * 0.75f + 6, *labels > 0 ? *labels + 6 : 0.f);
        *labels = start + len;
        const D2D1_POINT_2F c = at(edge + start + len / 2, 0);
        b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
        b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), r.label.pos, r.label.len);
    }

    // The name in a band, then a row per attribute in columns, rows
    // alternately shaded.
    void DrawEntity(size_t i) {
        const Box& b = boxes_[i];
        const Entity& e = p_.entities[i];
        const Style& st = b.style;
        const DgColor fill = st.fill.role != kDgNone ? st.fill : Role(kDgNodeFill);
        const DgColor stroke = st.stroke.role != kDgNone ? st.stroke : Role(kDgNodeStroke);
        const DgColor text = st.color.role != kDgNone ? st.color : Role(kDgText);
        const float sw = st.stroke_width >= 0 ? st.stroke_width : 1.f;
        const D2D1_RECT_F r = D2D1::RectF(b.center.x - b.w / 2, b.center.y - b.h / 2, b.center.x + b.w / 2, b.center.y + b.h / 2);
        b_.Rect(r, 0, fill, stroke, sw, st.dash == 1 ? 1 : 0);
        const Text& name = e.label.raw.empty() ? e.name : e.label;
        b_.Text(b.name, D2D1::Point2F(b.center.x - b.name.w / 2, r.top + (b.header - b.name.h) / 2), text, name.pos, name.len);
        float y = r.top + b.header;
        for (size_t k = 0; k < b.cells.size(); ++k) {
            const float rh = b.rows[k];
            b_.Rect(D2D1::RectF(r.left, y, r.right, y + rh), 0, Role(k % 2 == 0 ? kDgPage : kDgActivationFill), stroke, 0.5f);
            float x = r.left;
            const Attribute& a = e.attrs[k];
            const Text* src[4] = {&a.type, &a.name, &a.keys, &a.comment};
            for (size_t c = 0; c < 4; ++c) {
                if (b.columns[c] <= 0) continue;
                const M& cell = b.cells[k][c];
                if (cell.layout) b_.Text(cell, D2D1::Point2F(x + kCellX, y + (rh - cell.h) / 2), Role(kDgText), src[c]->pos, src[c]->len);
                x += b.columns[c];
            }
            y += rh;
        }
        // The frame again, over the rows.
        if (!b.cells.empty()) b_.Rect(r, 0, Role(kDgNone), stroke, sw, st.dash == 1 ? 1 : 0);
    }

    Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
    std::vector<Box> boxes_;
    std::vector<M> labels_;
    std::vector<int> slot_;
    LayeredLayout layout_;
    float ox_ = 0, oy_ = 0;
};

// Whether the layout will fit its budget (see the flowchart's).
void CheckBudget(const Parser& p) {
    LayeredGraph g;
    for (size_t i = 0; i < p.entities.size(); ++i) {
        LayeredGraph::Node n;
        n.w = n.h = 10;
        g.nodes.push_back(n);
    }
    for (const Relationship& r : p.relationships) {
        if (r.a == r.b) continue;
        LayeredGraph::Edge e;
        e.from = r.a;
        e.to = r.b;
        e.minlen = 2;
        g.edges.push_back(e);
    }
    if (LayoutLayered(g, true).too_large) TooLarge();
}

}  // namespace

std::shared_ptr<DiagramScene> ErDiagram(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) {
        CheckBudget(p);
        return nullptr;
    }
    Builder b(p, *builder, src);
    return b.Build();
}

}  // namespace mdv::mermaid
