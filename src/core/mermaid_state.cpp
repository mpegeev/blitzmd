// Mermaid state diagrams (stateDiagram, stateDiagram-v2): states with
// descriptions, start and end points, choices, forks and joins, composite
// states with their own start and end and concurrent regions, notes and
// styles, laid out in layers as Mermaid (dagre) does.
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

enum class Kind : uint8_t { Normal, Start, End, Choice, Fork, Join };

struct State {
    std::wstring id;
    Kind kind = Kind::Normal;
    Text name;                       // where it is first named
    Text label;                      // state "label" as id
    std::vector<Text> descriptions;  // id : description
    int parent = -1;                 // the cluster it is in
    int composite = -1;              // a composite state: its cluster
    std::vector<std::wstring> classes;
    Style style;
};

struct Cluster {
    int state = -1;                  // the composite state; -1: one of its concurrent regions
    int parent = -1;
    std::vector<int> regions;        // a composite's regions (two or more)
    bool folded = false;             // a lone region: its contents belong to the composite
};

struct Transition {
    int from = -1, to = -1;
    Text label;
};

struct Note {
    int state = -1;
    bool left = false;
    Text text;
};

// Mermaid's line breaks in texts: <br>, "\n" written out, and real ones.
std::wstring Breaks(std::wstring_view s) {
    std::wstring out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size() && s[i + 1] == L'n') {
            out += L"<br>";
            ++i;
        } else if (s[i] == L'\n') {
            out += L"<br>";
        } else if (s[i] != L'\r') {
            out.push_back(s[i]);
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
        if (!c_.EatKeyword(L"stateDiagram-v2") && !c_.EatKeyword(L"stateDiagram")) {
            c_.Fail(Tr(L"Expected \"stateDiagram\"", L"Ожидалось «stateDiagram»"));
        }
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd() && c_.Peek() != L'}') c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        if (!scopes_.empty()) {
            c_.set_pos(scopes_.back().open);
            c_.Fail(Tr(L"'{' without '}'", L"«{» без «}»"));
        }
    }

    RankDir dir = RankDir::TB;
    std::vector<State> states;
    std::vector<Cluster> clusters;
    std::vector<Transition> transitions;
    std::vector<Note> notes;
    std::map<std::wstring, Style> class_defs;

private:
    struct Scope {
        int cluster;      // the region being read
        size_t open;      // its composite's '{'
    };

    int Current() const { return scopes_.empty() ? -1 : scopes_.back().cluster; }

    // Keywords as Mermaid's lexer reads them: in any case.
    void Statement() {
        if (c_.Peek() == L'}') {
            if (scopes_.empty()) c_.Fail(Tr(L"'}' without '{'", L"«}» без «{»"));
            c_.Advance();
            Close();
            return;
        }
        if (c_.Rest().starts_with(L"--") && Trim(LinePeek()) == L"--") {
            // Concurrency: the composite's next region.
            if (scopes_.empty()) c_.Fail(Tr(L"'--' outside a composite state", L"«--» вне составного состояния"));
            c_.Advance(2);
            const int composite = clusters[static_cast<size_t>(Current())].parent;
            scopes_.back().cluster = NewRegion(composite);
            return;
        }
        if (c_.EatKeyword(L"state")) return StateDecl();
        if (c_.EatKeyword(L"note")) return NoteStmt();
        if (c_.EatKeyword(L"direction")) {
            c_.SkipBlanks();
            const std::wstring_view d = c_.LineRest();
            RankDir r;
            if (EqualsNoCase(d, L"TB") || EqualsNoCase(d, L"TD")) r = RankDir::TB;
            else if (EqualsNoCase(d, L"BT")) r = RankDir::BT;
            else if (EqualsNoCase(d, L"LR")) r = RankDir::LR;
            else if (EqualsNoCase(d, L"RL")) r = RankDir::RL;
            else c_.Fail(Tr(L"Unknown direction", L"Неизвестное направление"));
            if (scopes_.empty()) dir = r;  // (inside composites: the whole diagram's applies)
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
                if (auto it = ids_.find(id); it != ids_.end()) states[static_cast<size_t>(it->second)].classes.push_back(name);
            }
            return;
        }
        if (c_.EatKeyword(L"style")) {
            c_.SkipBlanks();
            const std::vector<std::wstring> ids = Names();
            c_.SkipBlanks();
            Style st;
            st.Apply(c_.LineRest());
            for (const std::wstring& id : ids) states[static_cast<size_t>(StateIndex(Text{id, 0, 0}))].style.Merge(st);
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
        for (const wchar_t* kw : {L"accTitle", L"hide", L"scale", L"click"}) {
            if (c_.EatKeyword(kw)) {
                c_.LineRest();
                return;
            }
        }
        TransitionOrDescription();
    }

    std::wstring_view LinePeek() const {
        const std::wstring_view r = c_.Rest();
        const size_t e = r.find(L'\n');
        return e == std::wstring_view::npos ? r : r.substr(0, e);
    }

    // A state id: letters, digits, _ (and - . inside).
    Text Id() {
        const size_t start = c_.pos();
        while (!c_.AtEnd()) {
            const wchar_t ch = c_.Peek();
            if (IsWordChar(ch) || ((ch == L'-' || ch == L'.') && c_.pos() > start && IsWordChar(c_.Peek(1)))) {
                c_.Advance();
            } else {
                break;
            }
        }
        if (c_.pos() == start) c_.Fail(Tr(L"Expected a state", L"Ожидалось состояние"));
        Text t;
        t.raw = std::wstring(src_.text.substr(start, c_.pos() - start));
        t.pos = static_cast<uint32_t>(start);
        t.len = static_cast<uint32_t>(t.raw.size());
        return t;
    }

    int StateIndex(const Text& id) {
        if (auto it = ids_.find(id.raw); it != ids_.end()) return it->second;
        if (states.size() >= kMaxNodes) c_.Fail(Tr(L"Too many states", L"Слишком много состояний"));
        State s;
        s.id = id.raw;
        s.name = id;
        s.parent = Current();
        ids_.emplace(id.raw, static_cast<int>(states.size()));
        states.push_back(std::move(s));
        return static_cast<int>(states.size()) - 1;
    }

    // [*]: the start of the region being read (as the source of a
    // transition) or its end.
    int Terminal(bool end) {
        const int scope = Current();
        auto& map = end ? ends_ : starts_;
        if (auto it = map.find(scope); it != map.end()) return it->second;
        if (states.size() >= kMaxNodes) c_.Fail(Tr(L"Too many states", L"Слишком много состояний"));
        State s;
        s.id = (end ? L"[*]end" : L"[*]start") + std::to_wstring(scope);
        s.kind = end ? Kind::End : Kind::Start;
        s.parent = scope;
        map.emplace(scope, static_cast<int>(states.size()));
        states.push_back(std::move(s));
        return static_cast<int>(states.size()) - 1;
    }

    // A state in a transition: an id (with :::class) or [*].
    int Ref(bool end) {
        if (c_.EatStr(L"[*]")) return Terminal(end);
        const int s = StateIndex(Id());
        Classes(s);
        return s;
    }

    void Classes(int s) {
        while (c_.EatStr(L":::")) {
            const size_t start = c_.pos();
            while (!c_.AtEnd() && (IsWordChar(c_.Peek()) || c_.Peek() == L'-')) c_.Advance();
            states[static_cast<size_t>(s)].classes.emplace_back(src_.text.substr(start, c_.pos() - start));
        }
    }

    // A --> B [: label] | A : description | A
    void TransitionOrDescription() {
        const int a = Ref(false);
        c_.SkipBlanks();
        if (c_.EatStr(L"-->")) {
            c_.SkipBlanks();
            Transition t;
            t.from = a;
            t.to = Ref(true);
            c_.SkipBlanks();
            if (c_.Eat(L':')) t.label = Rest();
            if (transitions.size() >= kMaxEdges) {
                c_.Fail(Tr(L"Too many transitions: Mermaid draws at most 500", L"Слишком много переходов: Mermaid рисует не больше 500"));
            }
            transitions.push_back(std::move(t));
            return;
        }
        if (c_.Eat(L':')) {
            Text d = Rest();
            if (!d.raw.empty()) states[static_cast<size_t>(a)].descriptions.push_back(std::move(d));
        }
    }

    // state A | state "label" as A | state A <<fork>> | state A { ... }
    void StateDecl() {
        c_.SkipBlanks();
        Text label;
        if (c_.Peek() == L'"') {
            label = Quoted();
            c_.SkipBlanks();
            if (!c_.EatKeyword(L"as")) c_.Fail(Tr(L"Expected \"as\"", L"Ожидалось «as»"));
            c_.SkipBlanks();
        }
        const Text id = Id();
        const int s = StateIndex(id);
        if (!label.raw.empty()) states[static_cast<size_t>(s)].label = label;
        c_.SkipBlanks();
        if (c_.EatStr(L"<<")) {
            const size_t start = c_.pos();
            while (!c_.AtEnd() && !c_.Rest().starts_with(L">>") && c_.Peek() != L'\n') c_.Advance();
            const std::wstring_view kind = Trim(src_.text.substr(start, c_.pos() - start));
            if (!c_.EatStr(L">>")) c_.Fail(Tr(L"Expected '>>'", L"Ожидалось «>>»"));
            if (EqualsNoCase(kind, L"fork")) states[static_cast<size_t>(s)].kind = Kind::Fork;
            else if (EqualsNoCase(kind, L"join")) states[static_cast<size_t>(s)].kind = Kind::Join;
            else if (EqualsNoCase(kind, L"choice")) states[static_cast<size_t>(s)].kind = Kind::Choice;
            c_.SkipBlanks();
        }
        Classes(s);
        c_.SkipBlanks();
        if (c_.Eat(L':')) {
            Text d = Rest();
            if (!d.raw.empty()) states[static_cast<size_t>(s)].descriptions.push_back(std::move(d));
            return;
        }
        if (!c_.Eat(L'{')) return;
        // A composite state: its contents are its first region.
        if (static_cast<int>(scopes_.size()) >= kMaxNesting) c_.Fail(Tr(L"States nested too deep", L"Слишком глубокая вложенность состояний"));
        if (states[static_cast<size_t>(s)].composite >= 0) c_.Fail(Tr(L"The state is defined twice", L"Состояние определено дважды"));
        if (clusters.size() + 2 > kMaxNodes) c_.Fail(Tr(L"Too many states", L"Слишком много состояний"));
        Cluster cl;
        cl.state = s;
        cl.parent = Current();
        const int composite = static_cast<int>(clusters.size());
        clusters.push_back(cl);
        states[static_cast<size_t>(s)].composite = composite;
        scopes_.push_back({NewRegion(composite), c_.pos() - 1});
    }

    int NewRegion(int composite) {
        if (clusters.size() >= kMaxNodes) c_.Fail(Tr(L"Too many states", L"Слишком много состояний"));
        Cluster r;
        r.parent = composite;
        clusters.push_back(r);
        const int index = static_cast<int>(clusters.size()) - 1;
        clusters[static_cast<size_t>(composite)].regions.push_back(index);
        return index;
    }

    // The end of a composite: with a single region, its contents are the
    // composite's own.
    void Close() {
        const int region = scopes_.back().cluster;
        scopes_.pop_back();
        Cluster& composite = clusters[static_cast<size_t>(clusters[static_cast<size_t>(region)].parent)];
        if (composite.regions.size() != 1) return;
        const int to = clusters[static_cast<size_t>(region)].parent;
        clusters[static_cast<size_t>(region)].folded = true;
        composite.regions.clear();
        for (State& s : states) {
            if (s.parent == region) s.parent = to;
        }
        for (Cluster& c : clusters) {
            if (c.parent == region) c.parent = to;
        }
    }

    // note left of A : text | note right of A \n text \n end note
    void NoteStmt() {
        c_.SkipBlanks();
        Note n;
        if (c_.EatKeyword(L"left")) n.left = true;
        else if (!c_.EatKeyword(L"right")) c_.Fail(Tr(L"Expected \"left of\" or \"right of\"", L"Ожидалось «left of» или «right of»"));
        c_.SkipBlanks();
        if (!c_.EatKeyword(L"of")) c_.Fail(Tr(L"Expected \"of\"", L"Ожидалось «of»"));
        c_.SkipBlanks();
        n.state = StateIndex(Id());
        c_.SkipBlanks();
        if (c_.Eat(L':')) {
            n.text = Rest();
        } else {
            // Up to "end note" on a line of its own.
            const size_t open = c_.pos();
            c_.SkipToLineEnd();
            c_.Advance();
            const size_t start = c_.pos();
            size_t end = std::wstring_view::npos;
            while (!c_.AtEnd()) {
                const size_t line = c_.pos();
                c_.SkipToLineEnd();
                if (EqualsNoCase(Trim(src_.text.substr(line, c_.pos() - line)), L"end note")) {
                    end = line;
                    break;
                }
                c_.Advance();
            }
            if (end == std::wstring_view::npos) {
                c_.set_pos(open);
                c_.Fail(Tr(L"A note without \"end note\"", L"Заметка без «end note»"));
            }
            const std::wstring_view body = Trim(src_.text.substr(start, end - start));
            if (body.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
            n.text.raw = std::wstring(body);
            n.text.pos = static_cast<uint32_t>(body.data() - src_.text.data());
            n.text.len = static_cast<uint32_t>(body.size());
        }
        if (notes.size() >= kMaxNodes) c_.Fail(Tr(L"Too many notes", L"Слишком много заметок"));
        notes.push_back(std::move(n));
    }

    Text Rest() {
        c_.SkipBlanks();
        const std::wstring_view r = c_.LineRest();
        Text t;
        t.raw = std::wstring(r);
        t.pos = static_cast<uint32_t>(r.data() - src_.text.data());
        t.len = static_cast<uint32_t>(r.size());
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
    std::map<int, int> starts_, ends_;  // per region (-1: the diagram's)
    std::vector<Scope> scopes_;
};

// ---------------------------------------------------------------------------
// Layout and drawing
// ---------------------------------------------------------------------------

constexpr float kPadX = 16, kPadY = 8;   // around a state's text
constexpr float kNotePad = 10;
constexpr float kNoteGap = 30;           // between a note and its state
constexpr float kClusterPad = 12;
constexpr float kMargin = 8;
constexpr float kLoopReach = 36;

using M = SceneBuilder::Measured;

struct Box {
    float w = 0, h = 0;
    D2D1_POINT_2F center{};
    M title;                  // a state's text, or its title above descriptions
    std::vector<M> descriptions;
    Style style;
    int node = -1;            // in the layered graph (-1: a composite, drawn as its cluster)
    float note_before = 0, note_after = 0;  // notes beside it
    int loops = 0;
};

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
        for (Box& b : boxes_) {
            if (b.node >= 0) b.center = D2D1::Point2F(ox_ + layout_.centers[static_cast<size_t>(b.node)].x,
                                                      oy_ + layout_.centers[static_cast<size_t>(b.node)].y);
        }
        for (size_t k = 0; k < p_.notes.size(); ++k) {
            if (note_node_[k] >= 0) {
                const D2D1_POINT_2F c = layout_.centers[static_cast<size_t>(note_node_[k])];
                note_center_[k] = D2D1::Point2F(ox_ + c.x, oy_ + c.y);
            }
        }
        if (title.layout) b_.Text(title, D2D1::Point2F((width - title.w) / 2, kMargin), Role(kDgText), src_.title_pos, src_.title_len);
        DrawComposites();
        DrawTransitions();
        DrawNotes();
        for (size_t i = 0; i < p_.states.size(); ++i) {
            if (boxes_[i].node >= 0) DrawState(i);
        }
        return b_.Finish(width, height);
    }

private:
    bool sideways() const { return p_.dir == RankDir::LR || p_.dir == RankDir::RL; }

    Style StateStyle(const State& s) const {
        Style st;
        if (auto it = p_.class_defs.find(L"default"); it != p_.class_defs.end()) st.Merge(it->second);
        for (const std::wstring& n : s.classes) {
            if (auto it = p_.class_defs.find(n); it != p_.class_defs.end()) st.Merge(it->second);
        }
        st.Merge(s.style);
        return st;
    }

    static RichText Styled(RichText t, bool bold, bool italic) {
        if (!bold && !italic) return t;
        if (t.spans.empty() && !t.text.empty()) t.spans.push_back({0, static_cast<uint32_t>(t.text.size()), false, false});
        for (auto& s : t.spans) {
            s.bold = s.bold || bold;
            s.italic = s.italic || italic;
        }
        return t;
    }

    // Composites with nothing in them are plain states.
    bool IsComposite(const State& s) const { return s.composite >= 0 && used_[static_cast<size_t>(s.composite)]; }

    void MeasureAll() {
        // Clusters holding a state (at any depth) are drawn.
        used_.assign(p_.clusters.size(), 0);
        for (const State& s : p_.states) {
            for (int c = s.parent; c >= 0 && !used_[static_cast<size_t>(c)]; c = p_.clusters[static_cast<size_t>(c)].parent) used_[static_cast<size_t>(c)] = 1;
        }
        boxes_.resize(p_.states.size());
        for (size_t i = 0; i < p_.states.size(); ++i) {
            const State& s = p_.states[i];
            Box& b = boxes_[i];
            b.style = StateStyle(s);
            switch (s.kind) {
            case Kind::Start:
            case Kind::End: b.w = b.h = 14; continue;
            case Kind::Choice: b.w = b.h = 28; continue;
            case Kind::Fork:
            case Kind::Join:
                b.w = sideways() ? 10.f : 70.f;
                b.h = sideways() ? 70.f : 10.f;
                continue;
            default: break;
            }
            const bool bold = b.style.bold == 1, italic = b.style.italic == 1;
            // One description is the state's text; more go under its name.
            const std::wstring& name = !s.label.raw.empty() ? s.label.raw : s.id;
            const bool one = s.descriptions.size() == 1 && s.label.raw.empty();
            b.title = b_.Measure(Styled(ParseLabel(Breaks(one ? s.descriptions[0].raw : name), false), bold, italic), 0, false, 1.f);
            if (IsComposite(s)) {
                composite_titles_[s.composite] = b.title;
                continue;
            }
            float w = b.title.w, h = b.title.h;
            if (!one) {
                for (const Text& d : s.descriptions) {
                    b.descriptions.push_back(b_.Measure(ParseLabel(Breaks(d.raw), false), 0, false, 1.f, false));
                    w = std::max(w, b.descriptions.back().w);
                    h += b.descriptions.back().h;
                }
                if (!s.descriptions.empty()) h += kPadY;  // the divider
            }
            b.w = std::max(w + 2 * kPadX, 40.f);
            b.h = h + 2 * kPadY;
        }
        for (const Transition& t : p_.transitions) {
            labels_.push_back(t.label.raw.empty() ? M{} : b_.Measure(ParseLabel(Breaks(t.label.raw), false), 0, false, 1.f));
        }
        for (const Note& n : p_.notes) {
            notes_.push_back(b_.Measure(ParseLabel(Breaks(n.text.raw), false), 0, false, 0.875f));
        }
    }

    // A composite's box in the layered graph (-1: not a composite).
    int BoxOf(int s) const {
        const State& st = p_.states[static_cast<size_t>(s)];
        return IsComposite(st) ? cluster_graph_[static_cast<size_t>(st.composite)] : -1;
    }

    // A state that stands for a composite in the layout: the first plain
    // state inside it.
    int Representative(int s) const {
        const State& st = p_.states[static_cast<size_t>(s)];
        if (!IsComposite(st)) return s;
        for (size_t i = 0; i < p_.states.size(); ++i) {
            if (IsComposite(p_.states[i])) continue;
            for (int c = p_.states[i].parent; c >= 0; c = p_.clusters[static_cast<size_t>(c)].parent) {
                if (c == st.composite) return static_cast<int>(i);
            }
        }
        return s;
    }

    void Layout() {
        LayeredGraph g;
        g.dir = p_.dir;
        g.cluster_pad = kClusterPad;
        // Clusters (parents first, as read): composites with a title,
        // regions without; folded regions and empty ones are left out.
        cluster_graph_.assign(p_.clusters.size(), -1);
        for (size_t c = 0; c < p_.clusters.size(); ++c) {
            const Cluster& cl = p_.clusters[c];
            if (!used_[c] || cl.folded) continue;
            LayeredGraph::Cluster gc;
            gc.parent = cl.parent >= 0 ? cluster_graph_[static_cast<size_t>(cl.parent)] : -1;
            if (cl.state >= 0) {
                const M& t = composite_titles_[static_cast<int>(c)];
                gc.title_w = t.w + 8;
                gc.title_h = t.h;
            }
            cluster_graph_[c] = static_cast<int>(g.clusters.size());
            g.clusters.push_back(gc);
        }
        auto cluster = [&](int c) {
            // A folded region's states belong to its composite (Close did that).
            return c >= 0 ? cluster_graph_[static_cast<size_t>(c)] : -1;
        };
        for (size_t i = 0; i < p_.states.size(); ++i) {
            if (IsComposite(p_.states[i])) continue;
            Box& b = boxes_[i];
            b.node = static_cast<int>(g.nodes.size());
            LayeredGraph::Node n;
            n.w = b.w;
            n.h = b.h;
            n.cluster = cluster(p_.states[i].parent);
            g.nodes.push_back(n);
        }
        // Notes beside their state; a composite's note is a node of its own.
        note_node_.assign(p_.notes.size(), -1);
        note_center_.assign(p_.notes.size(), D2D1::Point2F(0, 0));
        std::vector<std::pair<int, int>> note_links;  // note node, state node
        for (size_t k = 0; k < p_.notes.size(); ++k) {
            const Note& n = p_.notes[k];
            const M& m = notes_[k];
            const float w = m.w + 2 * kNotePad, h = m.h + 2 * kNotePad;
            Box& b = boxes_[static_cast<size_t>(n.state)];
            if (b.node >= 0) {
                const float room = (sideways() ? h : w) + kNoteGap;
                (n.left ? b.note_before : b.note_after) += room;
                continue;
            }
            LayeredGraph::Node nn;
            nn.w = w;
            nn.h = h;
            nn.cluster = cluster(p_.states[static_cast<size_t>(n.state)].parent);
            note_node_[k] = static_cast<int>(g.nodes.size());
            g.nodes.push_back(nn);
            note_links.push_back({note_node_[k], boxes_[static_cast<size_t>(Representative(n.state))].node});
            note_of_composite_.push_back(BoxOf(n.state));
        }
        // Transitions; one to a composite goes to a state inside it and is
        // cut at the box.
        slot_.assign(p_.transitions.size(), -1);
        for (size_t k = 0; k < p_.transitions.size(); ++k) {
            const Transition& t = p_.transitions[k];
            const int a = boxes_[static_cast<size_t>(Representative(t.from))].node;
            const int bn = boxes_[static_cast<size_t>(Representative(t.to))].node;
            if (a < 0 || bn < 0) continue;
            if (a == bn) {
                if (t.from == t.to) {
                    Box& b = boxes_[static_cast<size_t>(t.from)];
                    const float label = labels_[k].layout ? (sideways() ? labels_[k].h : labels_[k].w) + 6 : 0.f;
                    loop_labels_[t.from] += label;
                    ++b.loops;
                }
                continue;
            }
            LayeredGraph::Edge e;
            e.from = a;
            e.to = bn;
            e.from_cluster = BoxOf(t.from);  // a composite: from its box's bottom
            e.to_cluster = BoxOf(t.to);      // into its box's top
            if (labels_[k].layout) {
                e.label_w = labels_[k].w + 8;
                e.label_h = labels_[k].h + 4;
            }
            slot_[k] = static_cast<int>(g.edges.size());
            g.edges.push_back(e);
        }
        for (size_t k = 0; k < note_links.size(); ++k) {
            const auto& [note, state] = note_links[k];
            if (state < 0) continue;
            LayeredGraph::Edge e;
            e.from = state;
            e.to = note;
            e.from_cluster = note_of_composite_[k];
            g.edges.push_back(e);
        }
        for (size_t i = 0; i < boxes_.size(); ++i) {
            Box& b = boxes_[i];
            if (b.node < 0) continue;
            float after = b.note_after;
            if (b.loops > 0) after += kLoopReach * (1 + 0.6f * static_cast<float>(b.loops - 1)) * 0.75f + 6 + loop_labels_[static_cast<int>(i)];
            g.nodes[static_cast<size_t>(b.node)].after = after;
            g.nodes[static_cast<size_t>(b.node)].before = b.note_before;
        }
        layout_ = LayoutLayered(g);
        if (layout_.too_large) TooLarge();
    }

    D2D1_RECT_F ClusterRect(int c) const {
        const int gc = cluster_graph_[static_cast<size_t>(c)];
        if (gc < 0) return D2D1::RectF(0, 0, 0, 0);
        const D2D1_RECT_F& r = layout_.clusters[static_cast<size_t>(gc)];
        return D2D1::RectF(ox_ + r.left, oy_ + r.top, ox_ + r.right, oy_ + r.bottom);
    }

    D2D1_RECT_F Rect(size_t i) const {
        const Box& b = boxes_[i];
        return D2D1::RectF(b.center.x - b.w / 2, b.center.y - b.h / 2, b.center.x + b.w / 2, b.center.y + b.h / 2);
    }

    // A composite: a rounded box, its title in a band at the top, its
    // contents on the page color below; dashed lines between regions.
    void DrawComposites() {
        for (size_t c = 0; c < p_.clusters.size(); ++c) {
            const Cluster& cl = p_.clusters[c];
            if (cl.state < 0 || !used_[c]) continue;
            const D2D1_RECT_F r = ClusterRect(static_cast<int>(c));
            if (r.right <= r.left) continue;
            const State& s = p_.states[static_cast<size_t>(cl.state)];
            const Style st = StateStyle(s);
            const DgColor fill = st.fill.role != kDgNone ? st.fill : Role(kDgNodeFill);
            const DgColor stroke = st.stroke.role != kDgNone ? st.stroke : Role(kDgNodeStroke);
            const M& t = composite_titles_[static_cast<int>(c)];
            const float band = t.h + 6;
            b_.Rect(r, 5, fill, stroke, 1.f);
            b_.Rect(D2D1::RectF(r.left + 3, r.top + band, r.right - 3, r.bottom - 3), 0, Role(kDgPage), stroke, 1.f);
            const Text& name = !s.label.raw.empty() ? s.label : (s.descriptions.size() == 1 ? s.descriptions[0] : s.name);
            b_.Text(t, D2D1::Point2F((r.left + r.right) / 2 - t.w / 2, r.top + 3), st.color.role != kDgNone ? st.color : Role(kDgText),
                    name.pos, name.len);
            // Regions side by side (above one another sideways), a dashed
            // line between each two.
            std::vector<D2D1_RECT_F> regions;
            for (int rg : cl.regions) {
                const D2D1_RECT_F rr = ClusterRect(rg);
                if (rr.right > rr.left) regions.push_back(rr);
            }
            std::sort(regions.begin(), regions.end(),
                      [&](const D2D1_RECT_F& a, const D2D1_RECT_F& b) { return sideways() ? a.top < b.top : a.left < b.left; });
            for (size_t k = 0; k + 1 < regions.size(); ++k) {
                if (sideways()) {
                    const float y = (regions[k].bottom + regions[k + 1].top) / 2;
                    b_.Begin(D2D1::Point2F(r.left + 3, y));
                    b_.LineTo(D2D1::Point2F(r.right - 3, y));
                } else {
                    const float x = (regions[k].right + regions[k + 1].left) / 2;
                    b_.Begin(D2D1::Point2F(x, r.top + band));
                    b_.LineTo(D2D1::Point2F(x, r.bottom - 3));
                }
                b_.End(Role(kDgNone), stroke, 1.f, 1);
            }
        }
    }

    // Where a line from inside state i towards `p` leaves it.
    D2D1_POINT_2F Clip(int i, D2D1_POINT_2F p) const {
        const State& s = p_.states[static_cast<size_t>(i)];
        if (IsComposite(s)) {
            const D2D1_RECT_F r = ClusterRect(s.composite);
            return ClipRect(D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2), r.right - r.left, r.bottom - r.top, p);
        }
        const Box& b = boxes_[static_cast<size_t>(i)];
        switch (s.kind) {
        case Kind::Start:
        case Kind::End: return ClipEllipse(b.center, b.w / 2, b.h / 2, p);
        case Kind::Choice: {
            const D2D1_POINT_2F c = b.center;
            return ClipPolygon(c, {{c.x, c.y - b.h / 2}, {c.x + b.w / 2, c.y}, {c.x, c.y + b.h / 2}, {c.x - b.w / 2, c.y}}, p);
        }
        default: return ClipRect(b.center, b.w, b.h, p);
        }
    }

    D2D1_POINT_2F Center(int i) const {
        const State& s = p_.states[static_cast<size_t>(i)];
        if (!IsComposite(s)) return boxes_[static_cast<size_t>(i)].center;
        const D2D1_RECT_F r = ClusterRect(s.composite);
        return D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2);
    }

    bool Inside(int i, D2D1_POINT_2F p) const {
        const State& s = p_.states[static_cast<size_t>(i)];
        if (!IsComposite(s)) return false;
        const D2D1_RECT_F r = ClusterRect(s.composite);
        return p.x > r.left && p.x < r.right && p.y > r.top && p.y < r.bottom;
    }

    void DrawTransitions() {
        const DgColor line = Role(kDgLine);
        std::vector<size_t> labelled;
        std::map<int, int> loops;
        std::map<int, float> loop_labels;
        for (size_t k = 0; k < p_.transitions.size(); ++k) {
            const Transition& t = p_.transitions[k];
            if (slot_[k] < 0) {
                if (t.from == t.to && boxes_[static_cast<size_t>(t.from)].node >= 0) DrawLoop(t, k, loops[t.from]++, &loop_labels[t.from]);
                continue;
            }
            std::vector<D2D1_POINT_2F> pts;
            pts.push_back(Center(t.from));
            for (const D2D1_POINT_2F& q : layout_.bends[static_cast<size_t>(slot_[k])]) pts.push_back(D2D1::Point2F(ox_ + q.x, oy_ + q.y));
            pts.push_back(Center(t.to));
            // A composite end: from where the line leaves its box.
            while (pts.size() > 2 && Inside(t.from, pts[1])) pts.erase(pts.begin() + 1);
            while (pts.size() > 2 && Inside(t.to, pts[pts.size() - 2])) pts.erase(pts.end() - 2);
            pts.front() = Clip(t.from, pts[1]);
            pts.back() = Clip(t.to, pts[pts.size() - 2]);
            const D2D1_POINT_2F tip = pts.back(), before = pts[pts.size() - 2];
            pts.back() = Retreat(tip, before, MarkerLength(Marker::Arrow));
            AddBasisCurve(b_, pts);
            b_.End(Role(kDgNone), line, 1.f);
            AddMarker(b_, Marker::Arrow, tip, before, line, 1.f);
            if (labels_[k].layout) {
                const D2D1_POINT_2F lp = layout_.labels[static_cast<size_t>(slot_[k])];
                label_at_[k] = D2D1::Point2F(ox_ + lp.x, oy_ + lp.y);
                labelled.push_back(k);
            }
        }
        for (size_t k : labelled) {
            const M& m = labels_[k];
            const D2D1_POINT_2F c = label_at_[k];
            b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
            b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), p_.transitions[k].label.pos, p_.transitions[k].label.len);
        }
    }

    // A state's transition to itself: a loop beside it (below it
    // sideways), its label past the outermost loop.
    void DrawLoop(const Transition& t, size_t k, int n, float* labels) {
        const Box& b = boxes_[static_cast<size_t>(t.from)];
        const bool below = sideways();
        auto at = [&](float out, float side) {
            return below ? D2D1::Point2F(b.center.x + side, b.center.y + out) : D2D1::Point2F(b.center.x + out, b.center.y + side);
        };
        const float edge = (below ? b.h : b.w) / 2, s = (below ? b.w : b.h) / 4;
        const float reach = kLoopReach * (1 + 0.6f * static_cast<float>(n));
        const D2D1_POINT_2F a0 = at(edge, -s), a1 = at(edge, s);
        const D2D1_POINT_2F c1 = at(edge + reach, -s - 12), c2 = at(edge + reach, s + 12);
        const DgColor line = Role(kDgLine);
        b_.Begin(a0);
        b_.CubicTo(c1, c2, Retreat(a1, c2, MarkerLength(Marker::Arrow)));
        b_.End(Role(kDgNone), line, 1.f);
        AddMarker(b_, Marker::Arrow, a1, c2, line, 1.f);
        const M& m = labels_[k];
        if (m.layout) {
            const float len = below ? m.h : m.w;
            const float start = std::max(kLoopReach * (1 + 0.6f * static_cast<float>(b.loops - 1)) * 0.75f + 6, *labels > 0 ? *labels + 6 : 0.f);
            *labels = start + len;
            const D2D1_POINT_2F c = at(edge + start + len / 2, 0);
            b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
            b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), Role(kDgText), t.label.pos, t.label.len);
        }
    }

    // Notes beside their states (left or right; above or below sideways),
    // each tied to its state by a dashed line.
    void DrawNotes() {
        std::map<int, float> used_before, used_after;  // how far out each side is taken
        for (size_t k = 0; k < p_.notes.size(); ++k) {
            const Note& n = p_.notes[k];
            const M& m = notes_[k];
            const float w = m.w + 2 * kNotePad, h = m.h + 2 * kNotePad;
            D2D1_POINT_2F c;
            D2D1_POINT_2F from, to;
            if (note_node_[k] >= 0) {
                c = note_center_[k];
                to = ClipRect(c, w, h, Center(n.state));
                from = Clip(n.state, c);
            } else {
                const Box& b = boxes_[static_cast<size_t>(n.state)];
                float& taken = (n.left ? used_before : used_after)[n.state];
                const float along = (sideways() ? h : w);
                const float out = (sideways() ? b.h : b.w) / 2 + taken + kNoteGap + along / 2;
                taken += along + kNoteGap;
                const float sign = n.left ? -1.f : 1.f;
                c = sideways() ? D2D1::Point2F(b.center.x, b.center.y + sign * out) : D2D1::Point2F(b.center.x + sign * out, b.center.y);
                from = ClipRect(b.center, b.w, b.h, c);
                to = ClipRect(c, w, h, b.center);
            }
            b_.Begin(from);
            b_.LineTo(to);
            b_.End(Role(kDgNone), Role(kDgNoteStroke), 1.f, 1);
            const D2D1_RECT_F r = D2D1::RectF(c.x - w / 2, c.y - h / 2, c.x + w / 2, c.y + h / 2);
            b_.Rect(r, 0, Role(kDgNoteFill), Role(kDgNoteStroke));
            b_.Text(m, D2D1::Point2F(c.x - m.w / 2, r.top + kNotePad), Role(kDgNoteText), n.text.pos, n.text.len);
        }
    }

    void DrawState(size_t i) {
        const State& s = p_.states[i];
        const Box& b = boxes_[i];
        const D2D1_RECT_F r = Rect(i);
        const Style& st = b.style;
        const DgColor fill = st.fill.role != kDgNone ? st.fill : Role(kDgNodeFill);
        const DgColor stroke = st.stroke.role != kDgNone ? st.stroke : Role(kDgNodeStroke);
        const DgColor text = st.color.role != kDgNone ? st.color : Role(kDgText);
        const float sw = st.stroke_width >= 0 ? st.stroke_width : 1.f;
        const uint8_t dash = st.dash == 1 ? 1 : 0;
        switch (s.kind) {
        case Kind::Start: b_.Ellipse(r, Role(kDgLine), Role(kDgLine)); return;
        case Kind::End: {
            b_.Ellipse(r, Role(kDgPage), Role(kDgLine), 1.5f);
            const D2D1_POINT_2F c = b.center;
            b_.Ellipse(D2D1::RectF(c.x - 4, c.y - 4, c.x + 4, c.y + 4), Role(kDgLine), Role(kDgLine));
            return;
        }
        case Kind::Choice: {
            const D2D1_POINT_2F c = b.center;
            b_.Polygon({{c.x, r.top}, {r.right, c.y}, {c.x, r.bottom}, {r.left, c.y}}, fill, stroke, sw, dash);
            return;
        }
        case Kind::Fork:
        case Kind::Join: b_.Rect(r, 0, Role(kDgLine), Role(kDgLine)); return;
        default: break;
        }
        b_.Rect(r, 5, fill, stroke, sw, dash);
        const bool one = s.descriptions.size() == 1 && s.label.raw.empty();
        const Text& name = one ? s.descriptions[0] : (!s.label.raw.empty() ? s.label : s.name);
        float y = r.top + kPadY;
        b_.Text(b.title, D2D1::Point2F(b.center.x - b.title.w / 2, y), text, name.pos, name.len);
        y += b.title.h;
        if (b.descriptions.empty()) return;
        // More descriptions: under a line, left-aligned.
        y += kPadY / 2;
        b_.Begin(D2D1::Point2F(r.left, y));
        b_.LineTo(D2D1::Point2F(r.right, y));
        b_.End(Role(kDgNone), stroke, sw);
        y += kPadY / 2;
        for (size_t k = 0; k < b.descriptions.size(); ++k) {
            b_.Text(b.descriptions[k], D2D1::Point2F(r.left + kPadX, y), text, s.descriptions[k].pos, s.descriptions[k].len);
            y += b.descriptions[k].h;
        }
    }

    Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
    std::vector<char> used_;                  // per cluster: drawn
    std::vector<Box> boxes_;                  // per state
    std::map<int, M> composite_titles_;       // per composite cluster
    std::vector<M> labels_, notes_;
    std::map<size_t, D2D1_POINT_2F> label_at_;
    std::map<int, float> loop_labels_;        // per state: its loops' labels' length
    std::vector<int> cluster_graph_;          // cluster -> layered graph cluster
    std::vector<int> slot_, note_node_;
    std::vector<int> note_of_composite_;      // per composite's note: its box
    std::vector<D2D1_POINT_2F> note_center_;
    LayeredLayout layout_;
    float ox_ = 0, oy_ = 0;
};

// Whether the layout will fit its budget (see the flowchart's).
void CheckBudget(const Parser& p) {
    LayeredGraph g;
    std::vector<int> graph(p.clusters.size(), -1);
    for (size_t c = 0; c < p.clusters.size(); ++c) {
        if (p.clusters[c].folded) continue;
        LayeredGraph::Cluster gc;
        gc.parent = p.clusters[c].parent >= 0 ? graph[static_cast<size_t>(p.clusters[c].parent)] : -1;
        graph[c] = static_cast<int>(g.clusters.size());
        g.clusters.push_back(gc);
    }
    for (const State& s : p.states) {
        LayeredGraph::Node n;
        n.w = n.h = 10;
        n.cluster = s.parent >= 0 ? graph[static_cast<size_t>(s.parent)] : -1;
        g.nodes.push_back(n);
    }
    for (const Transition& t : p.transitions) {
        if (t.from != t.to) g.edges.push_back({t.from, t.to});
    }
    if (LayoutLayered(g, true).too_large) TooLarge();
}

}  // namespace

std::shared_ptr<DiagramScene> StateDiagram(const Source& src, SceneBuilder* builder) {
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
