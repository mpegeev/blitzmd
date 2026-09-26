// Mermaid entity relationship diagrams: entities with their attributes (type,
// name, keys, comment), relationships with crow's foot cardinalities (solid
// when identifying, dashed when not) and labels, entity aliases, subgraphs
// and styles, laid out in layers as Mermaid (dagre) does.
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
    Text type, name, comment;
    std::wstring keys;          // PK,FK,UK
    uint32_t keys_pos = 0, keys_len = 0;
};

struct Entity {
    std::wstring id;
    Text name;                  // where it is first named
    Text label;                 // p[Person]
    std::vector<Attribute> attrs;
    std::vector<std::wstring> classes;
    Style style;
    int parent = -1;            // the subgraph it is in
    int group = -1;             // the name of a subgraph: that one
};

struct Subgraph {
    std::wstring id;
    Text title;
    int parent = -1;
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
            if (Statement()) continue;  // (after '}' more may follow on the line)
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        if (!open_.empty()) {
            c_.set_pos(open_.back().second);
            c_.Fail(Tr(L"\"subgraph\" without \"end\"", L"«subgraph» без «end»"));
        }
        Finish();
    }

    RankDir dir = RankDir::TB;
    std::vector<Entity> entities;
    std::vector<Subgraph> subgraphs;
    std::vector<Relationship> relationships;
    std::map<std::wstring, Style> class_defs;

private:
    int Current() const { return open_.empty() ? -1 : open_.back().first; }

    // True after an attribute block's '}'.
    bool Statement() {
        if (c_.EatKeyword(L"direction")) {
            c_.SkipBlanks();
            const std::wstring_view d = c_.LineRest();
            RankDir r;
            if (EqualsNoCase(d, L"TB") || EqualsNoCase(d, L"TD")) r = RankDir::TB;
            else if (EqualsNoCase(d, L"BT")) r = RankDir::BT;
            else if (EqualsNoCase(d, L"LR")) r = RankDir::LR;
            else if (EqualsNoCase(d, L"RL")) r = RankDir::RL;
            else c_.Fail(Tr(L"Unknown direction", L"Неизвестное направление"));
            if (open_.empty()) dir = r;  // (in subgraphs: the whole diagram's applies)
            return false;
        }
        if (c_.EatKeyword(L"subgraph")) {
            SubgraphStart();
            return false;
        }
        if (!open_.empty() && c_.EatKeyword(L"end")) {
            SubgraphEnd();
            return false;
        }
        if (c_.EatKeyword(L"classDef")) {
            c_.SkipBlanks();
            const std::vector<std::wstring> names = Names();
            c_.SkipBlanks();
            Style st;
            st.Apply(c_.LineRest());
            for (const std::wstring& n : names) class_defs[n].Merge(st);
            return false;
        }
        // class and style: for the entities (and subgraphs) there are so far,
        // as in Mermaid.
        if (c_.EatKeyword(L"class")) {
            c_.SkipBlanks();
            const std::vector<std::wstring> ids = Names();
            c_.SkipBlanks();
            const std::vector<std::wstring> names = Names();
            for (const std::wstring& id : ids) {
                std::vector<std::wstring>* classes = ClassesOf(id);
                if (classes) classes->insert(classes->end(), names.begin(), names.end());
            }
            return false;
        }
        if (c_.EatKeyword(L"style")) {
            c_.SkipBlanks();
            const std::vector<std::wstring> ids = Names();
            c_.SkipBlanks();
            Style st;
            st.Apply(c_.LineRest());
            for (const std::wstring& id : ids) {
                if (auto it = ids_.find(id); it != ids_.end()) entities[static_cast<size_t>(it->second)].style.Merge(st);
                else if (const int g = SubgraphIndex(id); g >= 0) subgraphs[static_cast<size_t>(g)].style.Merge(st);
            }
            return false;
        }
        if (c_.EatKeyword(L"accDescr")) {
            c_.SkipBlanks();
            if (c_.Eat(L'{')) {
                const size_t open = c_.pos() - 1;
                while (!c_.AtEnd() && c_.Peek() != L'}') c_.Advance();
                if (!c_.Eat(L'}')) {
                    c_.set_pos(open);
                    c_.Fail(Tr(L"'{' without '}'", L"«{» без «}»"));
                }
            } else {
                c_.LineRest();
            }
            return false;
        }
        if (c_.EatKeyword(L"accTitle")) {
            c_.LineRest();
            return false;
        }
        const int a = EntityRef();
        c_.SkipBlanks();
        if (c_.Eat(L'{')) {
            Attributes(a);
            return true;
        }
        if (c_.AtStatementEnd()) return false;  // an entity on its own
        Relationship r;
        r.a = a;
        if (!CardAt(&r.ca)) c_.Fail(Tr(L"Expected a relationship", L"Ожидалась связь"));
        c_.SkipBlanks();
        if (c_.EatStr(L"--") || c_.EatKeyword(L"to")) {
            r.identifying = true;
        } else if (c_.EatStr(L"..") || c_.EatStr(L".-") || c_.EatStr(L"-.")) {
            r.identifying = false;
        } else if (c_.EatKeyword(L"optionally")) {
            c_.SkipBlanks();
            if (!c_.EatKeyword(L"to")) c_.Fail(Tr(L"Expected \"to\"", L"Ожидалось «to»"));
            r.identifying = false;
        } else {
            c_.Fail(Tr(L"Expected a relationship", L"Ожидалась связь"));
        }
        c_.SkipBlanks();
        if (!CardAt(&r.cb)) c_.Fail(Tr(L"Expected a cardinality", L"Ожидалась кратность"));
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
        if (relationships.size() >= kMaxNodes) c_.Fail(Tr(L"Too many relationships", L"Слишком много связей"));
        relationships.push_back(std::move(r));
        return false;
    }

    int SubgraphIndex(std::wstring_view id) const {
        for (size_t k = 0; k < subgraphs.size(); ++k) {
            if (subgraphs[k].id == id) return static_cast<int>(k);
        }
        return -1;
    }

    std::vector<std::wstring>* ClassesOf(const std::wstring& id) {
        if (auto it = ids_.find(id); it != ids_.end()) return &entities[static_cast<size_t>(it->second)].classes;
        const int g = SubgraphIndex(id);
        return g >= 0 ? &subgraphs[static_cast<size_t>(g)].classes : nullptr;
    }

    // subgraph id | subgraph id [title] | subgraph title words; defined
    // again, it goes on.
    void SubgraphStart() {
        const size_t at = c_.pos();
        c_.SkipBlanks();
        Text title;
        if (c_.Peek() == L'"') {
            title = Quoted();  // subgraph "Two words"
            c_.SkipBlanks();
        } else {
            const size_t start = c_.pos();
            while (!c_.AtStatementEnd() && c_.Peek() != L'[') c_.Advance();
            const std::wstring_view t = Trim(src_.text.substr(start, c_.pos() - start));
            if (t.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
            title = Text{std::wstring(t), static_cast<uint32_t>(t.data() - src_.text.data()), static_cast<uint32_t>(t.size())};
        }
        const std::wstring id = title.raw;
        if (id.empty()) c_.Fail(Tr(L"Expected a subgraph name", L"Ожидалось имя подграфа"));
        if (c_.Eat(L'[')) {
            c_.SkipBlanks();
            if (c_.Peek() == L'"') {
                title = Quoted();
            } else {
                const size_t s = c_.pos();
                while (!c_.AtEnd() && c_.Peek() != L']' && c_.Peek() != L'\n') c_.Advance();
                const std::wstring_view t = Trim(src_.text.substr(s, c_.pos() - s));
                title = Text{std::wstring(t), static_cast<uint32_t>(t.data() - src_.text.data()), static_cast<uint32_t>(t.size())};
            }
            c_.SkipBlanks();
            if (!c_.Eat(L']')) c_.Fail(Tr(L"Expected ']'", L"Ожидалась «]»"));
        }
        if (static_cast<int>(open_.size()) >= kMaxNesting) c_.Fail(Tr(L"Subgraphs nested too deep", L"Слишком глубокая вложенность подграфов"));
        int index = SubgraphIndex(id);
        if (index < 0) {
            if (subgraphs.size() >= kMaxNodes) c_.Fail(Tr(L"Too many subgraphs", L"Слишком много подграфов"));
            Subgraph sg;
            sg.id = id;
            sg.title = std::move(title);
            sg.parent = Current();
            index = static_cast<int>(subgraphs.size());
            subgraphs.push_back(std::move(sg));
        }
        open_.push_back({index, at});
        named_.emplace_back();
    }

    // A subgraph's end: it takes the entities named in it that no subgraph
    // ended before has taken (Mermaid's rule: the first to end holds them).
    void SubgraphEnd() {
        const int g = open_.back().first;
        for (int e : named_.back()) {
            Entity& en = entities[static_cast<size_t>(e)];
            if (en.parent < 0) en.parent = g;
        }
        open_.pop_back();
        named_.pop_back();
    }

    // A cardinality: a symbol (|o || }o }| or the other way round, on
    // either side, o in any case) or its words (zero or more, 1+, one...).
    bool CardAt(Card* card) {
        struct Symbol {
            const wchar_t* text;
            Card card;
        };
        static constexpr Symbol kSymbols[] = {{L"|o", Card::ZeroOrOne}, {L"o|", Card::ZeroOrOne},  {L"||", Card::ExactlyOne},
                                              {L"}o", Card::ZeroOrMore}, {L"o{", Card::ZeroOrMore}, {L"}|", Card::OneOrMore},
                                              {L"|{", Card::OneOrMore}};
        const std::wstring_view r = c_.Rest();
        for (const Symbol& s : kSymbols) {
            if (r.size() >= 2 && EqualsNoCase(r.substr(0, 2), s.text)) {
                c_.Advance(2);
                *card = s.card;
                return true;
            }
        }
        static constexpr Symbol kWords[] = {
            {L"zero or more", Card::ZeroOrMore}, {L"zero or many", Card::ZeroOrMore}, {L"zero or one", Card::ZeroOrOne},
            {L"one or zero", Card::ZeroOrOne},   {L"one or more", Card::OneOrMore},   {L"one or many", Card::OneOrMore},
            {L"only one", Card::ExactlyOne},     {L"many(1)", Card::OneOrMore},       {L"many(0)", Card::ZeroOrMore},
            {L"1+", Card::OneOrMore},            {L"0+", Card::ZeroOrMore},           {L"1", Card::ExactlyOne},
            {L"one", Card::ExactlyOne},          {L"many", Card::ZeroOrMore},
        };
        for (const Symbol& s : kWords) {
            const size_t len = std::wcslen(s.text);
            if (r.size() >= len && EqualsNoCase(r.substr(0, len), s.text) && (r.size() == len || !IsWordChar(r[len]))) {
                c_.Advance(len);
                *card = s.card;
                return true;
            }
        }
        return false;
    }

    // NAME, NAME-2, schema.table, "A name", then [Alias] / ["Alias"] and
    // :::class,class.
    int EntityRef() {
        Text name;
        if (c_.Peek() == L'"') {
            name = Quoted();
        } else {
            const size_t start = c_.pos();
            while (!c_.AtEnd()) {
                const wchar_t ch = c_.Peek(), next = c_.Peek(1);
                // (- and . inside a name, not where a relationship starts:
                // A--B, A..B, A-.B)
                if (IsWordChar(ch) || ch == L'*' || ((ch == L'-' || ch == L'.') && next != L'-' && next != L'.')) {
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
            // (The first alias stays, as in Mermaid.)
            Entity& en = entities[static_cast<size_t>(e)];
            if (!label.raw.empty() && en.label.raw.empty()) en.label = std::move(label);
        }
        while (c_.EatStr(L":::")) {
            for (;;) {
                const size_t s = c_.pos();
                while (!c_.AtEnd() && (IsWordChar(c_.Peek()) || c_.Peek() == L'-')) c_.Advance();
                if (c_.pos() > s) entities[static_cast<size_t>(e)].classes.emplace_back(src_.text.substr(s, c_.pos() - s));
                if (!c_.Eat(L',')) break;
            }
        }
        return e;
    }

    // An entity named here (noted for the subgraph being read).
    int EntityIndex(const Text& name) {
        int e;
        if (auto it = ids_.find(name.raw); it != ids_.end()) {
            e = it->second;
        } else {
            if (entities.size() >= kMaxNodes) c_.Fail(Tr(L"Too many entities", L"Слишком много сущностей"));
            Entity en;
            en.id = name.raw;
            en.name = name;
            e = static_cast<int>(entities.size());
            ids_.emplace(name.raw, e);
            entities.push_back(std::move(en));
        }
        if (!named_.empty() && (named_.back().empty() || named_.back().back() != e)) {
            if (named_.back().size() < 4 * kMaxNodes) named_.back().push_back(e);
        }
        return e;
    }

    // { type name [PK, FK, UK] ["comment"] ... }, one or more a line.
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
            if (a.type.raw.empty()) c_.Fail(Tr(L"Expected an attribute type", L"Ожидался тип атрибута"));
            c_.SkipBlanks();
            if (c_.AtStatementEnd() || c_.Peek() == L'}') c_.Fail(Tr(L"Expected an attribute name", L"Ожидалось имя атрибута"));
            a.name = Token();
            c_.SkipBlanks();
            // Keys: PK, FK, UK, with commas between.
            const size_t keys = c_.pos();
            for (;;) {
                if (!c_.EatKeyword(L"PK") && !c_.EatKeyword(L"FK") && !c_.EatKeyword(L"UK")) break;
                a.keys += std::wstring(src_.text.substr(c_.pos() - 2, 2));
                a.keys_len = static_cast<uint32_t>(c_.pos() - keys);
                c_.SkipBlanks();
                if (!c_.Eat(L',')) break;
                a.keys.push_back(L',');
                c_.SkipBlanks();
            }
            if (!a.keys.empty() && a.keys.back() == L',') a.keys.pop_back();
            a.keys_pos = static_cast<uint32_t>(keys);
            c_.SkipBlanks();
            if (c_.Peek() == L'"') a.comment = Quoted();
            Entity& en = entities[static_cast<size_t>(e)];
            if (en.attrs.size() >= kMaxNodes) c_.Fail(Tr(L"Too many attributes", L"Слишком много атрибутов"));
            en.attrs.push_back(std::move(a));
            // (What follows on the line is the next attribute.)
            c_.SkipBlanks();
        }
    }

    // A word of an attribute, or `words in backquotes`.
    Text Token() {
        Text t;
        if (c_.Peek() == L'`') {
            const size_t open = c_.pos();
            c_.Advance();
            const size_t start = c_.pos();
            while (!c_.AtEnd() && c_.Peek() != L'`' && c_.Peek() != L'\n') c_.Advance();
            if (c_.Peek() != L'`') {
                c_.set_pos(open);
                c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
            }
            t.raw = std::wstring(src_.text.substr(start, c_.pos() - start));
            t.pos = static_cast<uint32_t>(start);
            c_.Advance();
        } else {
            const size_t start = c_.pos();
            while (!c_.AtStatementEnd() && !IsSpace(c_.Peek()) && c_.Peek() != L'}' && c_.Peek() != L'"') c_.Advance();
            t.raw = std::wstring(src_.text.substr(start, c_.pos() - start));
            t.pos = static_cast<uint32_t>(start);
        }
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

    // Names that are subgraphs holding entities stand for their boxes; an
    // empty one is drawn as an entity with its title.
    void Finish() {
        std::unordered_map<std::wstring, int> groups;
        for (size_t k = 0; k < subgraphs.size(); ++k) groups.emplace(subgraphs[k].id, static_cast<int>(k));
        std::vector<char> held(subgraphs.size(), 0);
        for (const Entity& e : entities) {
            if (groups.count(e.id)) continue;
            for (int g = e.parent; g >= 0 && !held[static_cast<size_t>(g)]; g = subgraphs[static_cast<size_t>(g)].parent) held[static_cast<size_t>(g)] = 1;
        }
        for (Entity& e : entities) {
            auto it = groups.find(e.id);
            if (it == groups.end()) continue;
            if (held[static_cast<size_t>(it->second)]) {
                e.group = it->second;
            } else if (e.label.raw.empty()) {
                e.label = subgraphs[static_cast<size_t>(it->second)].title;
            }
        }
    }

    const Source& src_;
    Cursor c_;
    std::unordered_map<std::wstring, int> ids_;
    std::vector<std::pair<int, size_t>> open_;  // subgraphs being read, where each opened
    std::vector<std::vector<int>> named_;       // per subgraph being read: the entities named in it
};

// ---------------------------------------------------------------------------
// The structure laid out: entities as nodes, subgraphs holding some as boxes.
// ---------------------------------------------------------------------------

class Structure {
public:
    explicit Structure(const Parser& p) : p_(p) {
        used_.assign(p.subgraphs.size(), 0);
        representative_.assign(p.subgraphs.size(), -1);
        for (size_t i = 0; i < p.entities.size(); ++i) {
            if (p.entities[i].group >= 0) continue;
            for (int s = p.entities[i].parent; s >= 0; s = p.subgraphs[static_cast<size_t>(s)].parent) {
                used_[static_cast<size_t>(s)] = 1;
                if (representative_[static_cast<size_t>(s)] < 0) representative_[static_cast<size_t>(s)] = static_cast<int>(i);
            }
        }
    }

    bool Used(int s) const { return used_[static_cast<size_t>(s)] != 0; }
    // An end of a relationship: the entity, or a subgraph's first entity.
    int Node(int e) const {
        const int g = p_.entities[static_cast<size_t>(e)].group;
        return g < 0 ? e : representative_[static_cast<size_t>(g)];
    }
    // The subgraph box an end names (-1: an entity, or an empty subgraph).
    int Box(int e) const {
        const int g = p_.entities[static_cast<size_t>(e)].group;
        return g >= 0 && used_[static_cast<size_t>(g)] ? g : -1;
    }
    // Whether end a is a subgraph holding end b (at any depth).
    bool Holds(int a, int b) const {
        const int g = Box(a);
        if (g < 0 || a == b) return false;
        const Entity& eb = p_.entities[static_cast<size_t>(b)];
        for (int s = eb.group >= 0 ? p_.subgraphs[static_cast<size_t>(eb.group)].parent : eb.parent; s >= 0;
             s = p_.subgraphs[static_cast<size_t>(s)].parent) {
            if (s == g) return true;
        }
        return false;
    }

private:
    const Parser& p_;
    std::vector<char> used_;
    std::vector<int> representative_;
};

// ---------------------------------------------------------------------------
// Layout and drawing
// ---------------------------------------------------------------------------

constexpr float kPadX = 12, kPadY = 8;   // the name
constexpr float kCellX = 8, kCellY = 3;  // attribute cells
constexpr float kMinW = 100;
constexpr float kMargin = 8;
constexpr float kLoopReach = 36;
constexpr float kLoopStep = 20;          // between the ends of an entity's loops
constexpr float kSmall = 0.875f;
constexpr float kClusterPad = 12;

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
    float loop_labels = 0;                   // their labels' length
    float loop_side = 0;                     // and width across
};

// The room loops take beside an entity (their labels past them).
float LoopRoom(int loops, float labels) {
    return loops > 0 ? (kLoopReach * (1 + 0.6f * static_cast<float>(loops - 1)) + 16) * 0.75f + 6 + labels : 0.f;
}

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
    Builder(Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src), s_(p) {}

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
            Box& b = boxes_[i];
            if (node_[i] >= 0) {
                b.center = D2D1::Point2F(ox_ + layout_.centers[static_cast<size_t>(node_[i])].x, oy_ + layout_.centers[static_cast<size_t>(node_[i])].y);
            } else if (const int g = s_.Box(static_cast<int>(i)); g >= 0) {
                // A subgraph's name: its box.
                const D2D1_RECT_F r = GroupRect(g);
                b.center = D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2);
                b.w = r.right - r.left;
                b.h = r.bottom - r.top;
            }
        }
        if (title.layout) b_.Text(title, D2D1::Point2F((width - title.w) / 2, kMargin), Role(kDgText), src_.title_pos, src_.title_len);
        DrawSubgraphs();
        DrawRelationships();
        for (size_t i = 0; i < boxes_.size(); ++i) {
            if (node_[i] >= 0) DrawEntity(i);
        }
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
            if (e.group >= 0) continue;
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
                                     a.keys.empty() ? M{} : Plain(a.keys, kSmall),
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
        for (const Subgraph& sg : p_.subgraphs) titles_.push_back(b_.Measure(ParseLabel(sg.title.raw, false), 0, false, 1.f));
    }

    void Layout() {
        LayeredGraph g;
        g.dir = p_.dir;
        g.cluster_pad = kClusterPad;
        // Subgraphs (parents before children: as read) with entities in them.
        group_graph_.assign(p_.subgraphs.size(), -1);
        for (size_t k = 0; k < p_.subgraphs.size(); ++k) {
            if (!s_.Used(static_cast<int>(k))) continue;
            LayeredGraph::Cluster c;
            const int parent = p_.subgraphs[k].parent;
            c.parent = parent >= 0 ? group_graph_[static_cast<size_t>(parent)] : -1;
            c.title_w = titles_[k].w + 8;
            c.title_h = titles_[k].h;
            group_graph_[k] = static_cast<int>(g.clusters.size());
            g.clusters.push_back(c);
        }
        node_.assign(boxes_.size(), -1);
        for (size_t i = 0; i < boxes_.size(); ++i) {
            const Entity& e = p_.entities[i];
            if (e.group >= 0) continue;
            LayeredGraph::Node n;
            n.w = boxes_[i].w;
            n.h = boxes_[i].h;
            n.cluster = e.parent >= 0 ? group_graph_[static_cast<size_t>(e.parent)] : -1;
            node_[i] = static_cast<int>(g.nodes.size());
            g.nodes.push_back(n);
        }
        slot_.assign(p_.relationships.size(), -1);
        for (size_t k = 0; k < p_.relationships.size(); ++k) {
            const Relationship& r = p_.relationships[k];
            if (r.a == r.b) {
                Box& b = boxes_[static_cast<size_t>(r.a)];
                if (node_[static_cast<size_t>(r.a)] < 0 && s_.Box(r.a) < 0) continue;
                ++b.loops;
                if (labels_[k].layout) {
                    b.loop_labels += (sideways() ? labels_[k].h : labels_[k].w) + 6;
                    b.loop_side = std::max(b.loop_side, sideways() ? labels_[k].w : labels_[k].h);
                }
                continue;
            }
            if (s_.Holds(r.a, r.b) || s_.Holds(r.b, r.a)) continue;  // drawn across the box
            const int a = s_.Node(r.a), bn = s_.Node(r.b);
            if (a < 0 || bn < 0 || a == bn) continue;
            LayeredGraph::Edge e;
            e.from = node_[static_cast<size_t>(a)];
            e.to = node_[static_cast<size_t>(bn)];
            const int ga = s_.Box(r.a), gb = s_.Box(r.b);
            e.from_cluster = ga >= 0 ? group_graph_[static_cast<size_t>(ga)] : -1;
            e.to_cluster = gb >= 0 ? group_graph_[static_cast<size_t>(gb)] : -1;
            e.minlen = 2;  // room at both ends for the cardinalities
            if (labels_[k].layout) {
                e.label_w = labels_[k].w + 8;
                e.label_h = labels_[k].h + 4;
            }
            slot_[k] = static_cast<int>(g.edges.size());
            g.edges.push_back(e);
        }
        for (size_t i = 0; i < boxes_.size(); ++i) {
            const Box& b = boxes_[i];
            if (b.loops == 0) continue;
            const float room = LoopRoom(b.loops, b.loop_labels);
            if (node_[i] >= 0) {
                LayeredGraph::Node& n = g.nodes[static_cast<size_t>(node_[i])];
                n.after = room;
                float& across = sideways() ? n.w : n.h;  // as wide as the labels, room for the loops' ends
                across = std::max({across, b.loop_side + 8, 2 * (12 + kLoopStep * static_cast<float>(b.loops - 1)) + 20});
                Box& grown = boxes_[i];
                (sideways() ? grown.w : grown.h) = across;
                if (p_.entities[i].attrs.empty()) grown.header = grown.h;
            } else if (const int gr = s_.Box(static_cast<int>(i)); gr >= 0) {
                g.clusters[static_cast<size_t>(group_graph_[static_cast<size_t>(gr)])].after = room;
            }
        }
        layout_ = LayoutLayered(g);
        if (layout_.too_large) TooLarge();
    }

    D2D1_RECT_F GroupRect(int s) const {
        const int gc = group_graph_[static_cast<size_t>(s)];
        if (gc < 0) return D2D1::RectF(0, 0, 0, 0);
        const D2D1_RECT_F& r = layout_.clusters[static_cast<size_t>(gc)];
        return D2D1::RectF(ox_ + r.left, oy_ + r.top, ox_ + r.right, oy_ + r.bottom);
    }

    // Subgraphs: boxes in the cluster colors, their titles at the top.
    void DrawSubgraphs() {
        for (size_t k = 0; k < p_.subgraphs.size(); ++k) {
            if (group_graph_[k] < 0) continue;
            const D2D1_RECT_F r = GroupRect(static_cast<int>(k));
            if (r.right <= r.left) continue;
            const Subgraph& sg = p_.subgraphs[k];
            Style st;
            for (const std::wstring& n : sg.classes) {
                if (auto it = p_.class_defs.find(n); it != p_.class_defs.end()) st.Merge(it->second);
            }
            st.Merge(sg.style);
            b_.Rect(r, 0, st.fill.role != kDgNone ? st.fill : Role(kDgClusterFill), st.stroke.role != kDgNone ? st.stroke : Role(kDgClusterStroke),
                    st.stroke_width >= 0 ? st.stroke_width : 1.f, st.dash == 1 ? 1 : 0);
            const M& t = titles_[k];
            b_.Text(t, D2D1::Point2F((r.left + r.right) / 2 - t.w / 2, r.top + 4), st.color.role != kDgNone ? st.color : Role(kDgText),
                    sg.title.pos, sg.title.len);
        }
    }

    bool Inside(int e, D2D1_POINT_2F p) const {
        const int g = s_.Box(e);
        if (g < 0) return false;
        const D2D1_RECT_F r = GroupRect(g);
        return p.x > r.left && p.x < r.right && p.y > r.top && p.y < r.bottom;
    }

    // A subgraph and an entity in it: straight from under its title down to
    // the entity (from the left sideways).
    bool Inner(const Relationship& r, std::vector<D2D1_POINT_2F>* pts) const {
        const bool down = s_.Holds(r.a, r.b);
        if (!down && !s_.Holds(r.b, r.a)) return false;
        const int group = down ? r.a : r.b, member = down ? r.b : r.a;
        const int g = s_.Box(group);
        const D2D1_RECT_F rc = GroupRect(g);
        const Box& m = boxes_[static_cast<size_t>(member)];
        const float band = titles_[static_cast<size_t>(g)].h + 8;
        const D2D1_POINT_2F edge = sideways() ? D2D1::Point2F(rc.left, std::clamp(m.center.y, rc.top + band, rc.bottom - 3))
                                              : D2D1::Point2F(std::clamp(m.center.x, rc.left + 3, rc.right - 3), rc.top + band);
        const D2D1_POINT_2F end = ClipRect(m.center, m.w, m.h, edge);
        pts->assign({down ? edge : end, down ? end : edge});
        return true;
    }

    void DrawRelationships() {
        const DgColor line = Role(kDgLine);
        std::vector<int> loops(boxes_.size(), 0);
        std::vector<float> loop_labels(boxes_.size(), 0.f);
        std::vector<std::pair<size_t, D2D1_POINT_2F>> labelled;
        for (size_t k = 0; k < p_.relationships.size(); ++k) {
            const Relationship& r = p_.relationships[k];
            const uint8_t dash = r.identifying ? 0 : 1;
            if (r.a == r.b) {
                if (boxes_[static_cast<size_t>(r.a)].loops > 0) {
                    DrawLoop(r, k, loops[static_cast<size_t>(r.a)]++, &loop_labels[static_cast<size_t>(r.a)], dash);
                }
                continue;
            }
            const Box& a = boxes_[static_cast<size_t>(r.a)];
            const Box& b = boxes_[static_cast<size_t>(r.b)];
            std::vector<D2D1_POINT_2F> pts;
            if (slot_[k] >= 0) {
                pts.push_back(a.center);
                for (const D2D1_POINT_2F& q : layout_.bends[static_cast<size_t>(slot_[k])]) pts.push_back(D2D1::Point2F(ox_ + q.x, oy_ + q.y));
                pts.push_back(b.center);
                // A subgraph end: from where the line leaves its box.
                while (pts.size() > 2 && Inside(r.a, pts[1])) pts.erase(pts.begin() + 1);
                while (pts.size() > 2 && Inside(r.b, pts[pts.size() - 2])) pts.erase(pts.end() - 2);
                pts.front() = ClipRect(a.center, a.w, a.h, pts[1]);
                pts.back() = ClipRect(b.center, b.w, b.h, pts[pts.size() - 2]);
                DropDoubledEnds(&pts);
            } else if (!Inner(r, &pts)) {
                continue;
            }
            AddBasisCurve(b_, pts);
            b_.End(Role(kDgNone), line, 1.f, dash);
            AddCard(b_, r.ca, pts.front(), pts[1], line);
            AddCard(b_, r.cb, pts.back(), pts[pts.size() - 2], line);
            if (!labels_[k].layout) continue;
            if (slot_[k] >= 0) {
                const D2D1_POINT_2F lp = layout_.labels[static_cast<size_t>(slot_[k])];
                labelled.push_back({k, D2D1::Point2F(ox_ + lp.x, oy_ + lp.y)});
            } else {
                labelled.push_back({k, D2D1::Point2F((pts.front().x + pts.back().x) / 2, (pts.front().y + pts.back().y) / 2)});
            }
        }
        for (const auto& [k, c] : labelled) {
            const M& m = labels_[k];
            b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
            b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), p_.relationships[k].label.pos,
                    p_.relationships[k].label.len);
        }
    }

    // An entity (or subgraph) related to itself: loops beside it (below it
    // sideways), one inside the other so their ends don't meet, the labels
    // past the outermost.
    void DrawLoop(const Relationship& r, size_t k, int n, float* labels, uint8_t dash) {
        const Box& b = boxes_[static_cast<size_t>(r.a)];
        const bool below = sideways();
        auto at = [&](float out, float side) {
            return below ? D2D1::Point2F(b.center.x + side, b.center.y + out) : D2D1::Point2F(b.center.x + out, b.center.y + side);
        };
        const float edge = (below ? b.h : b.w) / 2, across = (below ? b.w : b.h) / 2;
        const float s = std::min(std::min(across / 2, 12.f) + kLoopStep * static_cast<float>(n), across - 2);
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
        const float start = std::max(LoopRoom(b.loops, 0), *labels > 0 ? *labels + 6 : 0.f);
        *labels = start + len;
        const D2D1_POINT_2F c = at(edge + start + len / 2, 0);
        b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
        b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), r.label.pos, r.label.len);
    }

    // The name in a band, then a row per attribute in cells, every other
    // row in the entity's color.
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
            b_.Rect(D2D1::RectF(r.left, y, r.right, y + rh), 0, k % 2 == 0 ? Role(kDgPage) : fill, stroke, 0.5f);
            float x = r.left;
            const Attribute& a = e.attrs[k];
            const Text keys{a.keys, a.keys_pos, a.keys_len};
            const Text* src[4] = {&a.type, &a.name, &keys, &a.comment};
            for (size_t c = 0; c < 4; ++c) {
                if (b.columns[c] <= 0) continue;
                const M& cell = b.cells[k][c];
                if (cell.layout) {
                    b_.Text(cell, D2D1::Point2F(x + kCellX, y + (rh - cell.h) / 2), k % 2 == 0 ? Role(kDgText) : text, src[c]->pos,
                            src[c]->len);
                }
                x += b.columns[c];
                // A line between the cells.
                if (x < r.right - 1) {
                    b_.Begin(D2D1::Point2F(x, y));
                    b_.LineTo(D2D1::Point2F(x, y + rh));
                    b_.End(Role(kDgNone), stroke, 0.5f);
                }
            }
            y += rh;
        }
        // The frame again, over the rows.
        if (!b.cells.empty()) b_.Rect(r, 0, Role(kDgNone), stroke, sw, st.dash == 1 ? 1 : 0);
    }

    Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
    Structure s_;
    std::vector<Box> boxes_;       // per entity
    std::vector<int> node_;        // per entity: its node (-1: a subgraph's name)
    std::vector<M> labels_, titles_;
    std::vector<int> slot_;
    std::vector<int> group_graph_;  // per subgraph: its cluster
    LayeredLayout layout_;
    float ox_ = 0, oy_ = 0;
};

// Whether the layout will fit its budget (see the flowchart's): the graph
// Builder::Layout makes, with sizes guessed.
void CheckBudget(const Parser& p) {
    const Structure s(p);
    LayeredGraph g;
    std::vector<int> groups(p.subgraphs.size(), -1), nodes(p.entities.size(), -1);
    for (size_t k = 0; k < p.subgraphs.size(); ++k) {
        if (!s.Used(static_cast<int>(k))) continue;
        LayeredGraph::Cluster c;
        c.parent = p.subgraphs[k].parent >= 0 ? groups[static_cast<size_t>(p.subgraphs[k].parent)] : -1;
        c.title_w = c.title_h = 20;
        groups[k] = static_cast<int>(g.clusters.size());
        g.clusters.push_back(c);
    }
    for (size_t i = 0; i < p.entities.size(); ++i) {
        if (p.entities[i].group >= 0) continue;
        LayeredGraph::Node n;
        n.w = n.h = 10;
        n.cluster = p.entities[i].parent >= 0 ? groups[static_cast<size_t>(p.entities[i].parent)] : -1;
        nodes[i] = static_cast<int>(g.nodes.size());
        g.nodes.push_back(n);
    }
    for (const Relationship& r : p.relationships) {
        if (r.a == r.b || s.Holds(r.a, r.b) || s.Holds(r.b, r.a)) continue;
        const int a = s.Node(r.a), b = s.Node(r.b);
        if (a < 0 || b < 0 || a == b) continue;
        LayeredGraph::Edge e;
        e.from = nodes[static_cast<size_t>(a)];
        e.to = nodes[static_cast<size_t>(b)];
        const int ga = s.Box(r.a), gb = s.Box(r.b);
        e.from_cluster = ga >= 0 ? groups[static_cast<size_t>(ga)] : -1;
        e.to_cluster = gb >= 0 ? groups[static_cast<size_t>(gb)] : -1;
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
