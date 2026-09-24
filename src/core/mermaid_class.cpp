// Mermaid class diagrams: classes with annotations, attributes and methods
// (visibility, static and abstract classifiers, generics), every relation
// (inheritance, composition, aggregation, association, dependency,
// realization, links, lollipop interfaces) with cardinalities and labels,
// notes, namespaces and styles, laid out in layers as Mermaid (dagre) does.
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;  // in the source
};

struct Member {
    Text text;                  // as written, without its classifier
    bool is_static = false, is_abstract = false;
};

struct Class {
    std::wstring id;
    Text name;                  // where it is first named
    Text label;                 // class A["label"]
    std::wstring generic;       // class A~T~
    std::vector<Text> annotations;
    std::vector<Member> attributes, methods;
    int ns = -1;                // namespace
    std::vector<std::wstring> classes;
    Style style;
};

struct Relation {
    int a = -1, b = -1;
    Marker start = Marker::None, end = Marker::None;  // at a, at b
    bool dashed = false;
    Text label, card_a, card_b;
};

struct Note {
    Text text;
    int target = -1;            // note for a class
    int ns = -1;
};

struct Namespace {
    Text name;
    int parent = -1;
};

// Mermaid's generics: List~List~int~~ is List<List<int>>.
std::wstring Generics(std::wstring_view s) {
    std::wstring out;
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if (c != L'~') {
            out.push_back(c);
        } else if (i + 1 < s.size() && IsWordChar(s[i + 1])) {
            out.push_back(L'<');
            ++depth;
        } else if (depth > 0) {
            out.push_back(L'>');
            --depth;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"classDiagram-v2") && !c_.EatKeyword(L"classDiagram")) {
            c_.Fail(Tr(L"Expected \"classDiagram\"", L"Ожидалось «classDiagram»"));
        }
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd() && c_.Peek() != L'}') c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        if (!open_.empty()) {
            c_.set_pos(open_at_.back());
            c_.Fail(Tr(L"'{' without '}'", L"«{» без «}»"));
        }
    }

    RankDir dir = RankDir::TB;
    std::vector<Class> classes;
    std::vector<Relation> relations;
    std::vector<Note> notes;
    std::vector<Namespace> namespaces;
    std::map<std::wstring, Style> class_defs;

private:
    // Statement keywords are case-sensitive: Note and Class are fine as names.
    void Statement() {
        if (c_.Peek() == L'}') {
            if (open_.empty()) c_.Fail(Tr(L"'}' without '{'", L"«}» без «{»"));
            c_.Advance();
            open_.pop_back();
            open_at_.pop_back();
            return;
        }
        if (c_.EatKeyword(L"class", true)) return ClassDecl();
        if (c_.EatKeyword(L"namespace", true)) return NamespaceDecl();
        if (c_.Rest().starts_with(L"<<")) {
            const Text a = Annotation();
            c_.SkipBlanks();
            const int ci = ClassIndex(Name());
            classes[static_cast<size_t>(ci)].annotations.push_back(a);
            return;
        }
        if (c_.EatKeyword(L"note", true)) return NoteStmt();
        if (c_.EatKeyword(L"direction", true)) {
            c_.SkipBlanks();
            const std::wstring_view d = c_.LineRest();
            if (EqualsNoCase(d, L"TB") || EqualsNoCase(d, L"TD")) dir = RankDir::TB;
            else if (EqualsNoCase(d, L"BT")) dir = RankDir::BT;
            else if (EqualsNoCase(d, L"LR")) dir = RankDir::LR;
            else if (EqualsNoCase(d, L"RL")) dir = RankDir::RL;
            else c_.Fail(Tr(L"Unknown direction", L"Неизвестное направление"));
            return;
        }
        if (c_.EatKeyword(L"classDef", true)) {
            c_.SkipBlanks();
            const std::vector<std::wstring> names = Names();
            c_.SkipBlanks();
            Style st;
            st.Apply(c_.LineRest());
            for (const std::wstring& n : names) class_defs[n].Merge(st);
            return;
        }
        if (c_.EatKeyword(L"cssClass", true)) {
            c_.SkipBlanks();
            std::vector<std::wstring> ids;
            if (c_.Peek() == L'"') {
                const Text t = Quoted();
                size_t s = 0;
                while (s <= t.raw.size()) {
                    size_t e = t.raw.find(L',', s);
                    if (e == std::wstring::npos) e = t.raw.size();
                    const std::wstring_view id = Trim(std::wstring_view(t.raw).substr(s, e - s));
                    if (!id.empty()) ids.emplace_back(id);
                    s = e + 1;
                }
            } else {
                ids = Names();
            }
            c_.SkipBlanks();
            const std::wstring name(c_.LineRest());
            for (const std::wstring& id : ids) {
                if (auto it = ids_.find(id); it != ids_.end()) classes[static_cast<size_t>(it->second)].classes.push_back(name);
            }
            return;
        }
        if (c_.EatKeyword(L"style", true)) {
            c_.SkipBlanks();
            const int ci = ClassIndex(Name());
            c_.SkipBlanks();
            Style st;
            st.Apply(c_.LineRest());
            classes[static_cast<size_t>(ci)].style.Merge(st);
            return;
        }
        for (const wchar_t* kw : {L"callback", L"link", L"click", L"accTitle"}) {
            if (c_.EatKeyword(kw, true)) {
                c_.LineRest();
                return;
            }
        }
        if (c_.EatKeyword(L"accDescr", true)) {
            c_.SkipBlanks();
            if (c_.Eat(L'{')) {
                while (!c_.AtEnd() && c_.Peek() != L'}') c_.Advance();
                c_.Eat(L'}');
            } else {
                c_.LineRest();
            }
            return;
        }
        RelationOrMember();
    }

    // A class name: letters, digits, _ (- inside), or anything in `backticks`.
    Text Name() {
        Text t;
        if (c_.Peek() == L'`') {
            c_.Advance();
            const size_t start = c_.pos();
            while (!c_.AtEnd() && c_.Peek() != L'`' && c_.Peek() != L'\n') c_.Advance();
            if (c_.Peek() != L'`') c_.Fail(Tr(L"Expected '`'", L"Ожидалась «`»"));
            t.raw = std::wstring(src_.text.substr(start, c_.pos() - start));
            t.pos = static_cast<uint32_t>(start);
            t.len = static_cast<uint32_t>(t.raw.size());
            c_.Advance();
            return t;
        }
        const size_t start = c_.pos();
        while (!c_.AtEnd()) {
            const wchar_t ch = c_.Peek();
            if (IsWordChar(ch) || ch == L'.' || (ch == L'-' && c_.pos() > start && IsWordChar(c_.Peek(1)))) {
                c_.Advance();
            } else {
                break;
            }
        }
        if (c_.pos() == start) c_.Fail(Tr(L"Expected a class name", L"Ожидалось имя класса"));
        t.raw = std::wstring(src_.text.substr(start, c_.pos() - start));
        t.pos = static_cast<uint32_t>(start);
        t.len = static_cast<uint32_t>(t.raw.size());
        return t;
    }

    // Name~T~: the class, with its generic type.
    int ClassRef() {
        const Text n = Name();
        const int ci = ClassIndex(n);
        if (c_.Peek() == L'~') {
            c_.Advance();
            const size_t start = c_.pos();
            while (!c_.AtEnd() && c_.Peek() != L'~' && c_.Peek() != L'\n') c_.Advance();
            if (c_.Peek() != L'~') c_.Fail(Tr(L"Expected '~'", L"Ожидалась «~»"));
            Class& cl = classes[static_cast<size_t>(ci)];
            if (cl.generic.empty()) cl.generic = std::wstring(src_.text.substr(start, c_.pos() - start));
            c_.Advance();
        }
        while (c_.EatStr(L":::")) {
            const size_t s = c_.pos();
            while (!c_.AtEnd() && (IsWordChar(c_.Peek()) || c_.Peek() == L'-')) c_.Advance();
            classes[static_cast<size_t>(ci)].classes.emplace_back(src_.text.substr(s, c_.pos() - s));
        }
        return ci;
    }

    int ClassIndex(const Text& n) {
        if (auto it = ids_.find(n.raw); it != ids_.end()) return it->second;
        if (classes.size() >= kMaxNodes) c_.Fail(Tr(L"Too many classes", L"Слишком много классов"));
        Class cl;
        cl.id = n.raw;
        cl.name = n;
        cl.ns = open_.empty() ? -1 : open_.back();
        ids_.emplace(n.raw, static_cast<int>(classes.size()));
        classes.push_back(std::move(cl));
        return static_cast<int>(classes.size()) - 1;
    }

    // class Name~T~["label"]:::css { members }
    void ClassDecl() {
        c_.SkipBlanks();
        const int ci = ClassRef();
        Class& cl = classes[static_cast<size_t>(ci)];
        if (cl.ns < 0 && !open_.empty()) cl.ns = open_.back();
        if (c_.Peek() == L'[') {
            c_.Advance();
            c_.SkipBlanks();
            if (c_.Peek() != L'"') c_.Fail(Tr(L"Expected '\"'", L"Ожидалась «\"»"));
            classes[static_cast<size_t>(ci)].label = Quoted();
            c_.SkipBlanks();
            if (!c_.Eat(L']')) c_.Fail(Tr(L"Expected ']'", L"Ожидалась «]»"));
        }
        while (c_.EatStr(L":::")) {
            const size_t s = c_.pos();
            while (!c_.AtEnd() && (IsWordChar(c_.Peek()) || c_.Peek() == L'-')) c_.Advance();
            classes[static_cast<size_t>(ci)].classes.emplace_back(src_.text.substr(s, c_.pos() - s));
        }
        c_.SkipBlanks();
        if (!c_.Eat(L'{')) return;
        // One member per line, up to the closing brace.
        const size_t open = c_.pos() - 1;
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) {
                c_.set_pos(open);
                c_.Fail(Tr(L"'{' without '}'", L"«{» без «}»"));
            }
            if (c_.Eat(L'}')) return;
            const size_t start = c_.pos();
            while (!c_.AtEnd() && c_.Peek() != L'\n' && c_.Peek() != L'}') c_.Advance();
            AddMember(ci, start, c_.pos());
        }
    }

    void NamespaceDecl() {
        c_.SkipBlanks();
        Namespace ns;
        ns.name = Name();
        ns.parent = open_.empty() ? -1 : open_.back();
        if (static_cast<int>(open_.size()) >= kMaxNesting) c_.Fail(Tr(L"Namespaces nested too deep", L"Слишком глубокая вложенность"));
        c_.SkipBlanks();
        if (!c_.Eat(L'{')) c_.Fail(Tr(L"Expected '{'", L"Ожидалась «{»"));
        open_.push_back(static_cast<int>(namespaces.size()));
        open_at_.push_back(c_.pos() - 1);
        namespaces.push_back(std::move(ns));
    }

    Text Annotation() {
        const size_t start = c_.pos();
        c_.Advance(2);
        while (!c_.AtEnd() && !c_.Rest().starts_with(L">>") && c_.Peek() != L'\n') c_.Advance();
        if (!c_.EatStr(L">>")) {
            c_.set_pos(start);
            c_.Fail(Tr(L"Expected '>>'", L"Ожидалось «>>»"));
        }
        Text t;
        t.raw = std::wstring(Trim(src_.text.substr(start + 2, c_.pos() - start - 4)));
        t.pos = static_cast<uint32_t>(start);
        t.len = static_cast<uint32_t>(c_.pos() - start);
        return t;
    }

    // note "text" | note for Class "text"
    void NoteStmt() {
        c_.SkipBlanks();
        Note n;
        n.ns = open_.empty() ? -1 : open_.back();
        if (c_.EatKeyword(L"for", true)) {
            c_.SkipBlanks();
            n.target = ClassIndex(Name());
            c_.SkipBlanks();
        }
        if (c_.Peek() != L'"') c_.Fail(Tr(L"Expected a note in quotes", L"Ожидалась заметка в кавычках"));
        n.text = Quoted();
        if (notes.size() >= kMaxNodes) c_.Fail(Tr(L"Too many notes", L"Слишком много заметок"));
        notes.push_back(std::move(n));
    }

    // A [card] relation [card] B [: label] | A : member
    void RelationOrMember() {
        const int a = ClassRef();
        c_.SkipBlanks();
        if (c_.Eat(L':')) {
            c_.SkipBlanks();
            const size_t start = c_.pos();
            while (!c_.AtStatementEnd()) c_.Advance();
            AddMember(a, start, c_.pos());
            return;
        }
        if (c_.AtStatementEnd()) return;  // a class named on its own
        Relation r;
        r.a = a;
        if (c_.Peek() == L'"') {
            r.card_a = Quoted();
            c_.SkipBlanks();
        }
        if (!ScanRelation(&r)) c_.Fail(Tr(L"Expected a relation", L"Ожидалась связь"));
        c_.SkipBlanks();
        if (c_.Peek() == L'"') {
            r.card_b = Quoted();
            c_.SkipBlanks();
        }
        r.b = ClassRef();
        c_.SkipBlanks();
        if (c_.Eat(L':')) {
            c_.SkipBlanks();
            r.label.pos = static_cast<uint32_t>(c_.pos());
            const std::wstring_view l = c_.LineRest();
            r.label.raw = std::wstring(l);
            r.label.len = static_cast<uint32_t>(l.size());
            if (l.size() > kMaxLabel) c_.Fail(Tr(L"Label too long", L"Слишком длинная подпись"));
        }
        if (relations.size() >= kMaxEdges) {
            c_.Fail(Tr(L"Too many relations: Mermaid draws at most 500", L"Слишком много связей: Mermaid рисует не больше 500"));
        }
        relations.push_back(std::move(r));
    }

    // [<| * o < ()] (-- | ..) [|> * o > ()]
    bool ScanRelation(Relation* r) {
        const size_t save = c_.pos();
        if (c_.EatStr(L"<|")) r->start = Marker::OpenArrow;
        else if (c_.EatStr(L"()")) r->start = Marker::Lollipop;
        else if (c_.Eat(L'*')) r->start = Marker::Diamond;
        else if (c_.Peek() == L'o' && (c_.Peek(1) == L'-' || c_.Peek(1) == L'.')) {
            c_.Advance();
            r->start = Marker::OpenDiamond;
        } else if (c_.Eat(L'<')) {
            r->start = Marker::Arrow;
        }
        if (c_.EatStr(L"--")) {
            r->dashed = false;
        } else if (c_.EatStr(L"..")) {
            r->dashed = true;
        } else {
            c_.set_pos(save);
            return false;
        }
        if (c_.EatStr(L"|>")) r->end = Marker::OpenArrow;
        else if (c_.EatStr(L"()")) r->end = Marker::Lollipop;
        else if (c_.Eat(L'*')) r->end = Marker::Diamond;
        else if (c_.Peek() == L'o' && !IsWordChar(c_.Peek(1))) {
            c_.Advance();
            r->end = Marker::OpenDiamond;
        } else if (c_.Eat(L'>')) {
            r->end = Marker::Arrow;
        }
        return true;
    }

    // A member line: <<annotation>>, an attribute or a method (it has
    // parentheses), with a trailing $ (static) or * (abstract).
    void AddMember(int ci, size_t start, size_t end) {
        std::wstring_view raw = src_.text.substr(start, end - start);
        const size_t lead = raw.size() - TrimLeft(raw).size();
        raw = Trim(raw);
        if (raw.empty()) return;
        if (raw.size() > kMaxLabel) c_.Fail(Tr(L"Member too long", L"Слишком длинный член класса"));
        Class& cl = classes[static_cast<size_t>(ci)];
        Text t;
        t.pos = static_cast<uint32_t>(start + lead);
        t.len = static_cast<uint32_t>(raw.size());
        if (raw.starts_with(L"<<") && raw.ends_with(L">>") && raw.size() >= 4) {
            t.raw = std::wstring(Trim(raw.substr(2, raw.size() - 4)));
            cl.annotations.push_back(std::move(t));
            return;
        }
        Member m;
        while (!raw.empty() && (raw.back() == L'$' || raw.back() == L'*')) {
            (raw.back() == L'$' ? m.is_static : m.is_abstract) = true;
            raw.remove_suffix(1);
            raw = Trim(raw);
        }
        t.raw = std::wstring(raw);
        m.text = std::move(t);
        const bool method = raw.find(L'(') != std::wstring_view::npos;
        if (cl.attributes.size() + cl.methods.size() >= kMaxNodes) c_.Fail(Tr(L"Too many members", L"Слишком много членов класса"));
        (method ? cl.methods : cl.attributes).push_back(std::move(m));
    }

    Text Quoted() {
        const size_t open = c_.pos();
        c_.Eat(L'"');
        const size_t start = c_.pos();
        while (!c_.AtEnd() && c_.Peek() != L'"') c_.Advance();
        if (c_.AtEnd()) {
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

    static std::wstring_view TrimLeft(std::wstring_view s) {
        size_t i = 0;
        while (i < s.size() && IsSpace(s[i])) ++i;
        return s.substr(i);
    }

    const Source& src_;
    Cursor c_;
    std::unordered_map<std::wstring, int> ids_;
    std::vector<int> open_;         // namespaces being read
    std::vector<size_t> open_at_;   // where their '{' is
};

// ---------------------------------------------------------------------------
// Layout and drawing
// ---------------------------------------------------------------------------

constexpr float kPadX = 10;          // text inside class boxes
constexpr float kPadY = 6;
constexpr float kGap = 4;            // around the dividers
constexpr float kNotePad = 10;
constexpr float kClusterPad = 12;
constexpr float kMargin = 8;
constexpr float kLoopReach = 36;

using M = SceneBuilder::Measured;

struct Box {                         // a class or a note, as laid out
    float w = 0, h = 0;
    D2D1_POINT_2F center{};
    std::vector<M> annotations;
    M name;
    std::vector<M> attributes, methods;
    M note;
    Style style;
    int loops = 0;
};

class Builder {
public:
    Builder(Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src) {}

    std::shared_ptr<DiagramScene> Build() {
        MeasureAll();
        Layout();
        // Front matter title above.
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
        const size_t nc = p_.classes.size();
        for (size_t i = 0; i < boxes_.size(); ++i) {
            const D2D1_POINT_2F c = layout_.centers[i];
            boxes_[i].center = D2D1::Point2F(ox_ + c.x, oy_ + c.y);
        }
        if (title.layout) b_.Text(title, D2D1::Point2F((width - title.w) / 2, kMargin), Role(kDgText), src_.title_pos, src_.title_len);
        DrawNamespaces();
        DrawRelations();
        for (size_t i = nc; i < boxes_.size(); ++i) DrawNote(i);
        for (size_t i = 0; i < nc; ++i) DrawClass(i);
        return b_.Finish(width, height);
    }

private:
    Style ClassStyle(const Class& c) const {
        Style st;
        if (auto it = p_.class_defs.find(L"default"); it != p_.class_defs.end()) st.Merge(it->second);
        for (const std::wstring& n : c.classes) {
            if (auto it = p_.class_defs.find(n); it != p_.class_defs.end()) st.Merge(it->second);
        }
        st.Merge(c.style);
        return st;
    }

    // Text with every character bold / italic / underlined.
    static RichText Styled(RichText t, bool bold, bool italic, bool underline) {
        if (!bold && !italic && !underline) return t;
        if (t.spans.empty() && !t.text.empty()) t.spans.push_back({0, static_cast<uint32_t>(t.text.size()), false, false});
        for (auto& s : t.spans) {
            s.bold = s.bold || bold;
            s.italic = s.italic || italic;
            s.underline = s.underline || underline;
        }
        return t;
    }

    // What a member shows: +name(params) : type, with generics as <T>.
    static std::wstring Display(std::wstring_view raw) {
        // A leading ~ is the package visibility, not a generic.
        if (!raw.empty() && raw.front() == L'~') return L"~" + Display(raw.substr(1));
        const size_t open = raw.find(L'(');
        if (open == std::wstring_view::npos) return Generics(raw);
        const size_t close = raw.rfind(L')');
        if (close == std::wstring_view::npos || close < open) return Generics(raw);
        std::wstring out = Generics(raw.substr(0, close + 1));
        const std::wstring_view ret = Trim(raw.substr(close + 1));
        if (!ret.empty()) out += L" : " + Generics(ret);
        return out;
    }

    void MeasureAll() {
        const size_t nc = p_.classes.size();
        boxes_.resize(nc + p_.notes.size());
        for (size_t i = 0; i < nc; ++i) {
            const Class& c = p_.classes[i];
            Box& b = boxes_[i];
            b.style = ClassStyle(c);
            const bool bold = b.style.bold != 0, italic = b.style.italic == 1;
            float w = 0, h = kPadY;
            for (const Text& a : c.annotations) {
                b.annotations.push_back(b_.Measure(ParseLabel(L"«" + a.raw + L"»", false), 0, false, 1.f));
                w = std::max(w, b.annotations.back().w);
                h += b.annotations.back().h;
            }
            // (Generics appended as text: <T> is no HTML tag here.)
            RichText name = c.label.raw.empty() ? RichText{c.id, {}} : ParseLabel(c.label.raw, false);
            if (!c.generic.empty()) name.text += L"<" + Generics(c.generic) + L">";
            b.name = b_.Measure(Styled(std::move(name), bold, italic, false), 0, false, 1.f);
            w = std::max(w, b.name.w);
            h += b.name.h + kPadY;
            auto rows = [&](const std::vector<Member>& members, std::vector<M>* out) {
                h += kGap;
                for (const Member& m : members) {
                    RichText t;
                    t.text = Display(m.text.raw);
                    out->push_back(b_.Measure(Styled(std::move(t), false, m.is_abstract, m.is_static), 0, false, 1.f, false));
                    w = std::max(w, out->back().w);
                    h += out->back().h;
                }
                h += kGap + (members.empty() ? 4.f : 0.f);
            };
            rows(c.attributes, &b.attributes);
            rows(c.methods, &b.methods);
            b.w = std::max(w + 2 * kPadX, 60.f);
            b.h = h + kGap;  // (DrawClass goes down the same way)
        }
        for (size_t k = 0; k < p_.notes.size(); ++k) {
            Box& b = boxes_[nc + k];
            b.note = b_.Measure(ParseLabel(p_.notes[k].text.raw, false), 0, false, 1.f);
            b.w = b.note.w + 2 * kNotePad;
            b.h = b.note.h + 2 * kNotePad;
        }
        for (const Relation& r : p_.relations) {
            labels_.push_back(r.label.raw.empty() ? M{} : b_.Measure(ParseLabel(r.label.raw, false), 0, false, 1.f));
            cards_.push_back({r.card_a.raw.empty() ? M{} : b_.Measure(ParseLabel(r.card_a.raw, false), 0, false, 0.875f),
                              r.card_b.raw.empty() ? M{} : b_.Measure(ParseLabel(r.card_b.raw, false), 0, false, 0.875f)});
        }
    }

    void Layout() {
        const size_t nc = p_.classes.size();
        LayeredGraph g;
        g.dir = p_.dir;
        g.cluster_pad = kClusterPad;
        // Namespaces are clusters, parents first.
        std::vector<int> order;
        std::vector<char> done(p_.namespaces.size(), 0);
        for (size_t k = 0; k < p_.namespaces.size(); ++k) {
            std::vector<int> chain;
            for (int n = static_cast<int>(k); n >= 0 && !done[static_cast<size_t>(n)]; n = p_.namespaces[static_cast<size_t>(n)].parent) chain.push_back(n);
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                done[static_cast<size_t>(*it)] = 1;
                order.push_back(*it);
            }
        }
        cluster_of_ns_.assign(p_.namespaces.size(), -1);
        for (int n : order) {
            const Namespace& ns = p_.namespaces[static_cast<size_t>(n)];
            LayeredGraph::Cluster cl;
            cl.parent = ns.parent >= 0 ? cluster_of_ns_[static_cast<size_t>(ns.parent)] : -1;
            titles_.push_back(b_.Measure(ParseLabel(ns.name.raw, false), 0, true, 1.f));
            cl.title_w = titles_.back().w + 8;
            cl.title_h = titles_.back().h;
            cluster_of_ns_[static_cast<size_t>(n)] = static_cast<int>(g.clusters.size());
            ns_of_cluster_.push_back(n);
            g.clusters.push_back(cl);
        }
        auto cluster = [&](int ns) { return ns >= 0 ? cluster_of_ns_[static_cast<size_t>(ns)] : -1; };
        for (size_t i = 0; i < boxes_.size(); ++i) {
            LayeredGraph::Node n;
            n.w = boxes_[i].w;
            n.h = boxes_[i].h;
            n.cluster = cluster(i < nc ? p_.classes[i].ns : p_.notes[i - nc].ns);
            g.nodes.push_back(n);
        }
        std::vector<float> loop_room(boxes_.size(), 0.f);
        const bool sideways = p_.dir == RankDir::LR || p_.dir == RankDir::RL;
        edge_slot_.assign(p_.relations.size(), -1);
        for (size_t k = 0; k < p_.relations.size(); ++k) {
            const Relation& r = p_.relations[k];
            if (r.a == r.b) {
                Box& b = boxes_[static_cast<size_t>(r.a)];
                const float label = labels_[k].layout ? (sideways ? labels_[k].h : labels_[k].w) : 0.f;
                loop_room[static_cast<size_t>(r.a)] =
                    std::max(loop_room[static_cast<size_t>(r.a)], kLoopReach * (1 + 0.6f * static_cast<float>(b.loops++)) * 0.75f + label + 6);
                continue;
            }
            LayeredGraph::Edge e;
            e.from = r.a;
            e.to = r.b;
            if (labels_[k].layout) {
                e.label_w = labels_[k].w + 8;
                e.label_h = labels_[k].h + 4;
            }
            edge_slot_[k] = static_cast<int>(g.edges.size());
            g.edges.push_back(e);
        }
        for (size_t i = 0; i < boxes_.size(); ++i) g.nodes[i].after = loop_room[i];
        // A note hangs off its class.
        note_slot_.assign(p_.notes.size(), -1);
        for (size_t k = 0; k < p_.notes.size(); ++k) {
            if (p_.notes[k].target < 0) continue;
            LayeredGraph::Edge e;
            e.from = static_cast<int>(nc + k);
            e.to = p_.notes[k].target;
            note_slot_[k] = static_cast<int>(g.edges.size());
            g.edges.push_back(e);
        }
        layout_ = LayoutLayered(g);
        if (layout_.too_large) TooLarge();
    }

    D2D1_RECT_F Rect(size_t i) const {
        const Box& b = boxes_[i];
        return D2D1::RectF(b.center.x - b.w / 2, b.center.y - b.h / 2, b.center.x + b.w / 2, b.center.y + b.h / 2);
    }

    void DrawNamespaces() {
        for (size_t k = 0; k < layout_.clusters.size(); ++k) {
            const D2D1_RECT_F& c = layout_.clusters[k];
            if (c.right <= c.left || c.bottom <= c.top) continue;
            const D2D1_RECT_F r = D2D1::RectF(ox_ + c.left, oy_ + c.top, ox_ + c.right, oy_ + c.bottom);
            b_.Rect(r, 0, Role(kDgClusterFill), Role(kDgClusterStroke));
            const M& t = titles_[k];
            const Namespace& ns = p_.namespaces[static_cast<size_t>(ns_of_cluster_[k])];
            b_.Text(t, D2D1::Point2F((r.left + r.right) / 2 - t.w / 2, r.top + kClusterPad / 2), Role(kDgText), ns.name.pos, ns.name.len);
        }
    }

    std::vector<D2D1_POINT_2F> Path(int from, int to, int slot) const {
        std::vector<D2D1_POINT_2F> pts;
        pts.push_back(boxes_[static_cast<size_t>(from)].center);
        for (const D2D1_POINT_2F& p : layout_.bends[static_cast<size_t>(slot)]) pts.push_back(D2D1::Point2F(ox_ + p.x, oy_ + p.y));
        pts.push_back(boxes_[static_cast<size_t>(to)].center);
        const Box& a = boxes_[static_cast<size_t>(from)];
        const Box& b = boxes_[static_cast<size_t>(to)];
        pts.front() = ClipRect(a.center, a.w, a.h, pts[1]);
        pts.back() = ClipRect(b.center, b.w, b.h, pts[pts.size() - 2]);
        return pts;
    }

    void DrawRelations() {
        std::vector<int> loops(boxes_.size(), 0);
        const DgColor line = Role(kDgLine);
        for (size_t k = 0; k < p_.notes.size(); ++k) {
            if (note_slot_[k] < 0) continue;
            const std::vector<D2D1_POINT_2F> pts = Path(static_cast<int>(p_.classes.size() + k), p_.notes[k].target, note_slot_[k]);
            AddBasisCurve(b_, pts);
            b_.End(Role(kDgNone), line, 1.f, 1);
        }
        std::vector<size_t> labelled;
        for (size_t k = 0; k < p_.relations.size(); ++k) {
            const Relation& r = p_.relations[k];
            const uint8_t dash = r.dashed ? 1 : 0;
            if (edge_slot_[k] < 0) {
                DrawLoop(r, k, loops[static_cast<size_t>(r.a)]++, dash);
                continue;
            }
            std::vector<D2D1_POINT_2F> pts = Path(r.a, r.b, edge_slot_[k]);
            const D2D1_POINT_2F tip_a = pts.front(), tip_b = pts.back(), after_a = pts[1], before_b = pts[pts.size() - 2];
            if (r.start != Marker::None) pts.front() = Retreat(tip_a, after_a, MarkerLength(r.start));
            if (r.end != Marker::None) pts.back() = Retreat(tip_b, before_b, MarkerLength(r.end));
            AddBasisCurve(b_, pts);
            b_.End(Role(kDgNone), line, 1.f, dash);
            AddMarker(b_, r.start, tip_a, after_a, line, 1.f);
            AddMarker(b_, r.end, tip_b, before_b, line, 1.f);
            Cardinality(cards_[k].first, tip_a, after_a, r.card_a);
            Cardinality(cards_[k].second, tip_b, before_b, r.card_b);
            if (labels_[k].layout) labelled.push_back(k);
        }
        // Labels over every line.
        for (size_t k : labelled) {
            const D2D1_POINT_2F lp = layout_.labels[static_cast<size_t>(edge_slot_[k])];
            const M& m = labels_[k];
            const D2D1_POINT_2F c = D2D1::Point2F(ox_ + lp.x, oy_ + lp.y);
            b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
            b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), p_.relations[k].label.pos, p_.relations[k].label.len);
        }
    }

    // A multiplicity near an end, beside the line.
    void Cardinality(const M& m, D2D1_POINT_2F tip, D2D1_POINT_2F from, const Text& src) {
        if (!m.layout) return;
        float dx = from.x - tip.x, dy = from.y - tip.y;
        const float len = std::hypot(dx, dy);
        if (len < 1e-3f) return;
        dx /= len;
        dy /= len;
        const float back = 14 + (std::fabs(dx) > std::fabs(dy) ? m.w / 2 : m.h / 2);
        const float side = 6 + (std::fabs(dx) > std::fabs(dy) ? m.h / 2 : m.w / 2);
        const D2D1_POINT_2F c = D2D1::Point2F(tip.x + dx * back - dy * side, tip.y + dy * back + dx * side);
        b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), src.pos, src.len);
    }

    // A class related to itself: a loop beside it (below it in LR/RL).
    void DrawLoop(const Relation& r, size_t k, int n, uint8_t dash) {
        const Box& b = boxes_[static_cast<size_t>(r.a)];
        const bool below = p_.dir == RankDir::LR || p_.dir == RankDir::RL;
        auto at = [&](float out, float side) {
            return below ? D2D1::Point2F(b.center.x + side, b.center.y + out) : D2D1::Point2F(b.center.x + out, b.center.y + side);
        };
        const float edge = (below ? b.h : b.w) / 2, s = (below ? b.w : b.h) / 4;
        const float reach = kLoopReach * (1 + 0.6f * static_cast<float>(n));
        const D2D1_POINT_2F a0 = at(edge, -s), a1 = at(edge, s);
        const D2D1_POINT_2F c1 = at(edge + reach, -s - 12), c2 = at(edge + reach, s + 12);
        const D2D1_POINT_2F from = r.start != Marker::None ? Retreat(a0, c1, MarkerLength(r.start)) : a0;
        const D2D1_POINT_2F to = r.end != Marker::None ? Retreat(a1, c2, MarkerLength(r.end)) : a1;
        const DgColor line = Role(kDgLine);
        b_.Begin(from);
        b_.CubicTo(c1, c2, to);
        b_.End(Role(kDgNone), line, 1.f, dash);
        AddMarker(b_, r.end, a1, c2, line, 1.f);
        AddMarker(b_, r.start, a0, c1, line, 1.f);
        const M& m = labels_[k];
        if (m.layout) {
            const D2D1_POINT_2F c = at(edge + reach * 0.75f + (below ? m.h : m.w) / 2 - 4, 0);
            b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
            b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), r.label.pos, r.label.len);
        }
    }

    void DrawNote(size_t i) {
        const Box& b = boxes_[i];
        const Note& n = p_.notes[i - p_.classes.size()];
        const D2D1_RECT_F r = Rect(i);
        b_.Rect(r, 0, Role(kDgNoteFill), Role(kDgNoteStroke));
        b_.Text(b.note, D2D1::Point2F(b.center.x - b.note.w / 2, r.top + kNotePad), Role(kDgNoteText), n.text.pos, n.text.len);
    }

    void DrawClass(size_t i) {
        const Box& b = boxes_[i];
        const Class& c = p_.classes[i];
        const Style& st = b.style;
        const D2D1_RECT_F r = Rect(i);
        const DgColor fill = st.fill.role != kDgNone ? st.fill : Role(kDgNodeFill);
        const DgColor stroke = st.stroke.role != kDgNone ? st.stroke : Role(kDgNodeStroke);
        const DgColor text = st.color.role != kDgNone ? st.color : Role(kDgText);
        const float sw = st.stroke_width >= 0 ? st.stroke_width : 1.f;
        b_.Rect(r, 0, fill, stroke, sw, st.dash == 1 ? 1 : 0);
        float y = r.top + kPadY;
        for (size_t k = 0; k < b.annotations.size(); ++k) {
            const M& a = b.annotations[k];
            b_.Text(a, D2D1::Point2F(b.center.x - a.w / 2, y), text, c.annotations[k].pos, c.annotations[k].len);
            y += a.h;
        }
        const Text& name = c.label.raw.empty() ? c.name : c.label;
        b_.Text(b.name, D2D1::Point2F(b.center.x - b.name.w / 2, y), text, name.pos, name.len);
        y += b.name.h + kPadY;
        auto divider = [&] {
            b_.Begin(D2D1::Point2F(r.left, y));
            b_.LineTo(D2D1::Point2F(r.right, y));
            b_.End(Role(kDgNone), stroke, sw);
        };
        auto rows = [&](const std::vector<M>& ms, const std::vector<Member>& members) {
            divider();
            y += kGap;
            for (size_t k = 0; k < ms.size(); ++k) {
                b_.Text(ms[k], D2D1::Point2F(r.left + kPadX, y), text, members[k].text.pos, members[k].text.len);
                y += ms[k].h;
            }
            y += kGap + (ms.empty() ? 4.f : 0.f);
        };
        rows(b.attributes, c.attributes);
        rows(b.methods, c.methods);
    }

    Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
    std::vector<Box> boxes_;                  // classes, then notes
    std::vector<M> labels_;                   // per relation
    std::vector<std::pair<M, M>> cards_;      // per relation: at a, at b
    std::vector<M> titles_;                   // per cluster
    std::vector<int> cluster_of_ns_, ns_of_cluster_;
    std::vector<int> edge_slot_, note_slot_;
    LayeredLayout layout_;
    float ox_ = 0, oy_ = 0;
};

// Whether the layout will fit its budget (see the flowchart's).
void CheckBudget(const Parser& p) {
    LayeredGraph g;
    g.dir = p.dir;
    for (const Namespace& ns : p.namespaces) {  // parents first, as read
        LayeredGraph::Cluster cl;
        cl.parent = ns.parent;
        g.clusters.push_back(cl);
    }
    for (const Class& c : p.classes) {
        LayeredGraph::Node n;
        n.w = n.h = 10;
        n.cluster = c.ns;
        g.nodes.push_back(n);
    }
    for (const Note& note : p.notes) {
        LayeredGraph::Node n;
        n.w = n.h = 10;
        n.cluster = note.ns;
        g.nodes.push_back(n);
        if (note.target >= 0) g.edges.push_back({static_cast<int>(g.nodes.size()) - 1, note.target});
    }
    for (const Relation& r : p.relations) {
        if (r.a != r.b) g.edges.push_back({r.a, r.b});
    }
    if (LayoutLayered(g, true).too_large) TooLarge();
}

}  // namespace

std::shared_ptr<DiagramScene> ClassDiagram(const Source& src, SceneBuilder* builder) {
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
