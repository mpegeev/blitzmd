// Mermaid flowcharts (flowchart / graph): nodes of every shape, edges with
// their strokes, ends and labels, subgraphs (with their own direction), and
// classDef / class / style / linkStyle.
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <map>
#include <unordered_map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

enum class Shape : uint8_t {
    Rect, Round, Stadium, Subroutine, Cylinder, Circle, DoubleCircle, Asymmetric, Rhombus, Hexagon,
    LeanRight, LeanLeft, Trapezoid, TrapezoidAlt, Text,
};

struct Label {
    std::wstring raw;
    bool markdown = false;
    uint32_t pos = 0, len = 0;  // in the source
    bool set = false;
};

struct Node {
    std::wstring id;
    Label label;
    Shape shape = Shape::Rect;
    int cluster = -1;           // innermost subgraph
    std::vector<std::wstring> classes;
    Style style;
};

struct Ref {
    int index = -1;
    bool cluster = false;       // a subgraph as an edge end
};

struct Edge {
    Ref from, to;
    Marker start = Marker::None, end = Marker::Arrow;
    int stroke = 0;             // 0 normal, 1 thick, 2 dotted, 3 invisible
    int length = 1;
    Label label;
    Style style;
};

struct Cluster {
    std::wstring id;
    Label title;
    int parent = -1;
    bool has_dir = false;
    RankDir dir = RankDir::TB;
    std::vector<int> mentioned;  // nodes named inside it (claimed at its end)
    std::vector<std::wstring> classes;
    Style style;
};

struct EdgeToken {
    Marker start = Marker::None, end = Marker::None;
    int stroke = 0, length = 1;
    Label label;
};

bool IsIdChar(wchar_t c) {
    return IsWordChar(c) || c == L'.' || c == L'$' || c == L'!' || c == L'?' || c == L'*' || c == L'+' || c == L'\'';
}

RankDir ParseDirection(std::wstring_view w, bool* ok) {
    *ok = true;
    if (EqualsNoCase(w, L"TB") || EqualsNoCase(w, L"TD") || w == L"v") return RankDir::TB;
    if (EqualsNoCase(w, L"BT") || w == L"^") return RankDir::BT;
    if (EqualsNoCase(w, L"LR") || w == L">") return RankDir::LR;
    if (EqualsNoCase(w, L"RL") || w == L"<") return RankDir::RL;
    *ok = false;
    return RankDir::TB;
}

Shape ShapeByName(std::wstring_view name) {
    struct Entry {
        const wchar_t* name;
        Shape shape;
    };
    static constexpr Entry kNames[] = {
        {L"rect", Shape::Rect}, {L"proc", Shape::Rect}, {L"process", Shape::Rect}, {L"rectangle", Shape::Rect},
        {L"rounded", Shape::Round}, {L"event", Shape::Round}, {L"stadium", Shape::Stadium}, {L"pill", Shape::Stadium},
        {L"terminal", Shape::Stadium}, {L"fr-rect", Shape::Subroutine}, {L"subroutine", Shape::Subroutine},
        {L"subproc", Shape::Subroutine}, {L"framed-rectangle", Shape::Subroutine}, {L"cyl", Shape::Cylinder},
        {L"cylinder", Shape::Cylinder}, {L"db", Shape::Cylinder}, {L"database", Shape::Cylinder},
        {L"circle", Shape::Circle}, {L"circ", Shape::Circle}, {L"dbl-circ", Shape::DoubleCircle},
        {L"double-circle", Shape::DoubleCircle}, {L"diam", Shape::Rhombus}, {L"diamond", Shape::Rhombus},
        {L"decision", Shape::Rhombus}, {L"question", Shape::Rhombus}, {L"hex", Shape::Hexagon},
        {L"hexagon", Shape::Hexagon}, {L"prepare", Shape::Hexagon}, {L"lean-r", Shape::LeanRight},
        {L"lean-right", Shape::LeanRight}, {L"in-out", Shape::LeanRight}, {L"lean-l", Shape::LeanLeft},
        {L"lean-left", Shape::LeanLeft}, {L"out-in", Shape::LeanLeft}, {L"trap-b", Shape::Trapezoid},
        {L"trapezoid", Shape::Trapezoid}, {L"priority", Shape::Trapezoid}, {L"trapezoid-bottom", Shape::Trapezoid},
        {L"trap-t", Shape::TrapezoidAlt}, {L"inv-trapezoid", Shape::TrapezoidAlt}, {L"manual", Shape::TrapezoidAlt},
        {L"trapezoid-top", Shape::TrapezoidAlt}, {L"text", Shape::Text}, {L"odd", Shape::Asymmetric},
        {L"flag", Shape::Asymmetric},
    };
    for (const Entry& e : kNames) {
        if (EqualsNoCase(name, e.name)) return e.shape;
    }
    return Shape::Rect;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"flowchart-elk") && !c_.EatKeyword(L"flowchart") && !c_.EatKeyword(L"graph")) {
            c_.Fail(Tr(L"Expected \"flowchart\"", L"Ожидалось «flowchart»"));
        }
        c_.SkipBlanks();
        if (!c_.AtStatementEnd()) {
            const size_t start = c_.pos();
            while (!c_.AtStatementEnd() && !IsSpace(c_.Peek())) c_.Advance();
            bool ok = false;
            dir = ParseDirection(src_.text.substr(start, c_.pos() - start), &ok);
            if (!ok) c_.set_pos(start);  // a statement on the same line
        }
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        while (!open_.empty()) CloseSubgraph();
    }

    RankDir dir = RankDir::TB;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Cluster> clusters;
    std::map<std::wstring, Style> class_defs;
    Style default_link;
    std::map<size_t, Style> link_styles;

private:
    void Statement() {
        if (c_.EatKeyword(L"subgraph")) return Subgraph();
        if (c_.EatKeyword(L"end")) {
            if (open_.empty()) c_.Fail(Tr(L"\"end\" without \"subgraph\"", L"«end» без «subgraph»"));
            CloseSubgraph();
            return;
        }
        if (c_.EatKeyword(L"direction")) {
            c_.SkipBlanks();
            bool ok = false;
            const RankDir d = ParseDirection(c_.LineRest(), &ok);
            if (!ok) c_.Fail(Tr(L"Unknown direction", L"Неизвестное направление"));
            if (open_.empty()) {
                dir = d;
            } else {
                clusters[static_cast<size_t>(open_.back())].dir = d;
                clusters[static_cast<size_t>(open_.back())].has_dir = true;
            }
            return;
        }
        if (c_.EatKeyword(L"classDef")) return ClassDef();
        if (c_.EatKeyword(L"class")) return ClassStatement();
        if (c_.EatKeyword(L"style")) return StyleStatement();
        if (c_.EatKeyword(L"linkStyle")) return LinkStyle();
        if (c_.EatKeyword(L"click") || c_.EatKeyword(L"callback") || c_.EatKeyword(L"accTitle")) {
            c_.LineRest();
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
        NodeStatement();
    }

    // subgraph id [title] | subgraph "title" | subgraph title words
    void Subgraph() {
        if (open_.size() >= static_cast<size_t>(kMaxNesting)) c_.Fail(Tr(L"Subgraphs nested too deep", L"Слишком глубокая вложенность подграфов"));
        c_.SkipBlanks();
        Cluster cl;
        cl.parent = open_.empty() ? -1 : open_.back();
        if (c_.Peek() == L'"') {
            cl.title = Quoted();
            cl.id = cl.title.raw;
        } else {
            const size_t start = c_.pos();
            while (!c_.AtStatementEnd() && !IsSpace(c_.Peek()) && c_.Peek() != L'[') c_.Advance();
            cl.id = std::wstring(src_.text.substr(start, c_.pos() - start));
            c_.SkipBlanks();
            if (c_.Eat(L'[')) {
                c_.SkipBlanks();
                if (c_.Peek() == L'"') {
                    cl.title = Quoted();
                    c_.SkipBlanks();
                    if (!c_.Eat(L']')) c_.Fail(Tr(L"Expected ']'", L"Ожидалась «]»"));
                } else {
                    cl.title = Until(L"]");
                }
            } else if (!c_.AtStatementEnd()) {
                // "subgraph Some title": all of it names and titles it.
                c_.LineRest();
                cl.id = std::wstring(Trim(src_.text.substr(start, c_.pos() - start)));
            }
            if (!cl.title.set) {
                cl.title.raw = cl.id;
                cl.title.pos = static_cast<uint32_t>(start);
                cl.title.len = static_cast<uint32_t>(cl.id.size());
                cl.title.set = true;
            }
        }
        if (cl.id.empty()) cl.id = L"subGraph" + std::to_wstring(clusters.size());
        if (clusters.size() >= kMaxNodes) c_.Fail(Tr(L"Too many subgraphs", L"Слишком много подграфов"));
        cluster_ids_[cl.id] = static_cast<int>(clusters.size());
        open_.push_back(static_cast<int>(clusters.size()));
        clusters.push_back(std::move(cl));
    }

    void CloseSubgraph() {
        const int id = open_.back();
        open_.pop_back();
        // Innermost subgraphs close first: each node goes to the first that names it.
        for (int n : clusters[static_cast<size_t>(id)].mentioned) {
            Node& node = nodes[static_cast<size_t>(n)];
            if (node.cluster < 0) node.cluster = id;
        }
        clusters[static_cast<size_t>(id)].mentioned.clear();
    }

    void ClassDef() {
        c_.SkipBlanks();
        std::vector<std::wstring> names = Names();
        c_.SkipBlanks();
        Style st;
        st.Apply(c_.LineRest());
        for (const std::wstring& n : names) class_defs[n].Merge(st);
    }

    void ClassStatement() {
        c_.SkipBlanks();
        std::vector<std::wstring> ids = Names();
        c_.SkipBlanks();
        const std::wstring name(c_.LineRest());
        for (const std::wstring& id : ids) {
            if (auto it = cluster_ids_.find(id); it != cluster_ids_.end()) {
                clusters[static_cast<size_t>(it->second)].classes.push_back(name);
            } else {
                nodes[static_cast<size_t>(NodeIndex(id))].classes.push_back(name);
            }
        }
    }

    void StyleStatement() {
        c_.SkipBlanks();
        const size_t start = c_.pos();
        while (!c_.AtStatementEnd() && !IsSpace(c_.Peek())) c_.Advance();
        const std::wstring id(src_.text.substr(start, c_.pos() - start));
        c_.SkipBlanks();
        Style st;
        st.Apply(c_.LineRest());
        if (auto it = cluster_ids_.find(id); it != cluster_ids_.end()) {
            clusters[static_cast<size_t>(it->second)].style.Merge(st);
        } else if (!id.empty()) {
            nodes[static_cast<size_t>(NodeIndex(id))].style.Merge(st);
        }
    }

    void LinkStyle() {
        c_.SkipBlanks();
        std::vector<std::wstring> which = Names();
        c_.SkipBlanks();
        std::wstring_view css = c_.LineRest();
        if (StartsWithNoCase(css, L"interpolate")) {  // curve names: ignored
            size_t k = 11;
            while (k < css.size() && IsSpace(css[k])) ++k;
            while (k < css.size() && !IsSpace(css[k])) ++k;
            css = Trim(css.substr(k));
        }
        Style st;
        st.Apply(css);
        for (const std::wstring& w : which) {
            if (EqualsNoCase(w, L"default")) {
                default_link.Merge(st);
            } else {
                const long n = std::wcstol(w.c_str(), nullptr, 10);
                if (n >= 0 && n < static_cast<long>(kMaxEdges)) link_styles[static_cast<size_t>(n)].Merge(st);
            }
        }
    }

    // a,b,c (to the next blank).
    std::vector<std::wstring> Names() {
        std::vector<std::wstring> out;
        std::wstring cur;
        while (!c_.AtStatementEnd() && !IsSpace(c_.Peek())) {
            const wchar_t ch = c_.Peek();
            c_.Advance();
            if (ch == L',') {
                if (!cur.empty()) out.push_back(std::move(cur));
                cur.clear();
                // "a, b": a blank after the comma continues the list.
                c_.SkipBlanks();
            } else {
                cur.push_back(ch);
            }
        }
        if (!cur.empty()) out.push_back(std::move(cur));
        if (out.empty()) c_.Fail(Tr(L"Expected a name", L"Ожидалось имя"));
        return out;
    }

    // A "quoted" label (markdown when in backticks).
    Label Quoted() {
        Label l;
        c_.Eat(L'"');
        const size_t start = c_.pos();
        while (!c_.AtEnd() && c_.Peek() != L'"') c_.Advance();
        if (c_.AtEnd()) c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        std::wstring_view raw = src_.text.substr(start, c_.pos() - start);
        c_.Eat(L'"');
        l.pos = static_cast<uint32_t>(start);
        l.len = static_cast<uint32_t>(raw.size());
        if (raw.size() >= 2 && raw.front() == L'`' && raw.back() == L'`') {
            raw = raw.substr(1, raw.size() - 2);
            l.markdown = true;
        }
        if (raw.size() > kMaxLabel) c_.Fail(Tr(L"Label too long", L"Слишком длинная подпись"));
        l.raw = std::wstring(raw);
        l.set = true;
        return l;
    }

    // Text up to `closer` on this line.
    Label Until(std::wstring_view closer) {
        Label l;
        const size_t start = c_.pos();
        while (!c_.AtEnd() && !c_.Rest().starts_with(closer)) {
            if (c_.Peek() == L'\n') c_.Fail(std::wstring(Tr(L"Expected '", L"Ожидалось «")) + std::wstring(closer) + Tr(L"'", L"»"));
            c_.Advance();
        }
        if (c_.AtEnd()) c_.Fail(std::wstring(Tr(L"Expected '", L"Ожидалось «")) + std::wstring(closer) + Tr(L"'", L"»"));
        const std::wstring_view raw = src_.text.substr(start, c_.pos() - start);
        if (raw.size() > kMaxLabel) c_.Fail(Tr(L"Label too long", L"Слишком длинная подпись"));
        c_.Advance(closer.size());
        l.raw = std::wstring(Trim(raw));
        l.pos = static_cast<uint32_t>(start);
        l.len = static_cast<uint32_t>(raw.size());
        l.set = true;
        return l;
    }

    int NodeIndex(const std::wstring& id) {
        auto it = node_ids_.find(id);
        if (it != node_ids_.end()) return it->second;
        if (nodes.size() >= kMaxNodes) c_.Fail(Tr(L"Too many nodes", L"Слишком много узлов"));
        Node n;
        n.id = id;
        node_ids_.emplace(id, static_cast<int>(nodes.size()));
        nodes.push_back(std::move(n));
        return static_cast<int>(nodes.size()) - 1;
    }

    // A (& B)* [edge (A & B ...)]*
    void NodeStatement() {
        std::vector<Ref> group = NodeGroup();
        for (;;) {
            c_.SkipBlanks();
            if (c_.AtStatementEnd()) return;
            EdgeToken e;
            if (!ScanEdge(&e)) c_.Fail(Tr(L"Expected an arrow", L"Ожидалась стрелка"));
            c_.SkipBlanks();
            std::vector<Ref> next = NodeGroup();
            for (const Ref& a : group) {
                for (const Ref& b : next) {
                    if (edges.size() >= kMaxEdges) c_.Fail(Tr(L"Too many edges", L"Слишком много связей"));
                    Edge edge;
                    edge.from = a;
                    edge.to = b;
                    edge.start = e.start;
                    edge.end = e.end;
                    edge.stroke = e.stroke;
                    edge.length = e.length;
                    edge.label = e.label;
                    edges.push_back(std::move(edge));
                }
            }
            group = std::move(next);
        }
    }

    std::vector<Ref> NodeGroup() {
        std::vector<Ref> out{NodeRef()};
        for (;;) {
            const size_t save = c_.pos();
            c_.SkipBlanks();
            if (!c_.Eat(L'&')) {
                c_.set_pos(save);
                return out;
            }
            c_.SkipBlanks();
            out.push_back(NodeRef());
        }
    }

    Ref NodeRef() {
        const size_t start = c_.pos();
        std::wstring id;
        while (!c_.AtEnd()) {
            const wchar_t ch = c_.Peek();
            if (IsIdChar(ch)) {
                id.push_back(ch);
                c_.Advance();
            } else if (ch == L'-' && !id.empty() && IsIdChar(c_.Peek(1)) && c_.Peek(1) != L'.') {
                id.push_back(ch);  // node-1
                c_.Advance();
            } else {
                break;
            }
        }
        if (id.empty()) c_.Fail(Tr(L"Expected a node", L"Ожидался узел"));
        (void)start;
        const bool shaped = c_.Peek() == L'[' || c_.Peek() == L'(' || c_.Peek() == L'{' ||
                            (c_.Peek() == L'>' && !id.empty()) || (c_.Peek() == L'@' && c_.Peek(1) == L'{');
        if (!shaped) {
            if (auto it = cluster_ids_.find(id); it != cluster_ids_.end() && c_.Peek() != L':') {
                return Ref{it->second, true};
            }
        }
        const int index = NodeIndex(id);
        if (!open_.empty()) clusters[static_cast<size_t>(open_.back())].mentioned.push_back(index);
        if (shaped) ShapeOf(nodes[static_cast<size_t>(index)]);
        while (c_.EatStr(L":::")) {
            const size_t s = c_.pos();
            while (!c_.AtEnd() && (IsWordChar(c_.Peek()) || c_.Peek() == L'-')) c_.Advance();
            nodes[static_cast<size_t>(index)].classes.emplace_back(src_.text.substr(s, c_.pos() - s));
        }
        return Ref{index, false};
    }

    void ShapeOf(Node& n) {
        if (c_.Peek() == L'@') {
            // A@{ shape: rect, label: "Text" }
            c_.Advance(2);
            for (;;) {
                c_.SkipStatementGap();
                if (c_.Eat(L'}')) return;
                if (c_.AtEnd()) c_.Fail(Tr(L"Expected '}'", L"Ожидалась «}»"));
                const size_t ks = c_.pos();
                while (!c_.AtEnd() && (IsWordChar(c_.Peek()) || c_.Peek() == L'-')) c_.Advance();
                const std::wstring key(src_.text.substr(ks, c_.pos() - ks));
                c_.SkipBlanks();
                if (!c_.Eat(L':')) c_.Fail(Tr(L"Expected ':'", L"Ожидалось «:»"));
                c_.SkipBlanks();
                Label value;
                if (c_.Peek() == L'"') {
                    value = Quoted();
                } else {
                    const size_t vs = c_.pos();
                    while (!c_.AtEnd() && c_.Peek() != L',' && c_.Peek() != L'}' && c_.Peek() != L'\n') c_.Advance();
                    value.raw = std::wstring(Trim(src_.text.substr(vs, c_.pos() - vs)));
                    value.pos = static_cast<uint32_t>(vs);
                    value.len = static_cast<uint32_t>(c_.pos() - vs);
                    value.set = true;
                }
                if (EqualsNoCase(key, L"shape")) n.shape = ShapeByName(value.raw);
                else if (EqualsNoCase(key, L"label")) n.label = value;
                c_.SkipBlanks();
                c_.Eat(L',');
            }
        }
        struct Opener {
            const wchar_t* open;
            const wchar_t* close;
            Shape shape;
            const wchar_t* close2 = nullptr;  // the other ending: another shape
            Shape shape2 = Shape::Rect;
        };
        static constexpr Opener kOpeners[] = {
            {L"(((", L")))", Shape::DoubleCircle},
            {L"((", L"))", Shape::Circle},
            {L"([", L"])", Shape::Stadium},
            {L"[[", L"]]", Shape::Subroutine},
            {L"[(", L")]", Shape::Cylinder},
            {L"[/", L"/]", Shape::LeanRight, L"\\]", Shape::Trapezoid},
            {L"[\\", L"\\]", Shape::LeanLeft, L"/]", Shape::TrapezoidAlt},
            {L"{{", L"}}", Shape::Hexagon},
            {L"(", L")", Shape::Round},
            {L"[", L"]", Shape::Rect},
            {L"{", L"}", Shape::Rhombus},
            {L">", L"]", Shape::Asymmetric},
        };
        for (const Opener& o : kOpeners) {
            if (!c_.EatStr(o.open)) continue;
            n.shape = o.shape;
            c_.SkipBlanks();
            if (c_.Peek() == L'"') {
                n.label = Quoted();
                c_.SkipBlanks();
                if (c_.EatStr(o.close)) return;
                if (o.close2 && c_.EatStr(o.close2)) {
                    n.shape = o.shape2;
                    return;
                }
                c_.Fail(std::wstring(Tr(L"Expected '", L"Ожидалось «")) + o.close + Tr(L"'", L"»"));
            }
            // Unquoted: up to the nearest ending.
            const size_t start = c_.pos();
            const std::wstring_view rest = c_.Rest();
            size_t end = rest.find(o.close);
            size_t end2 = o.close2 ? rest.find(o.close2) : std::wstring_view::npos;
            const size_t nl = rest.find(L'\n');
            if (end2 < end) {
                end = end2;
                n.shape = o.shape2;
            } else {
                end2 = std::wstring_view::npos;
            }
            if (end == std::wstring_view::npos || (nl != std::wstring_view::npos && nl < end)) {
                c_.Fail(std::wstring(Tr(L"Expected '", L"Ожидалось «")) + o.close + Tr(L"'", L"»"));
            }
            if (end > kMaxLabel) c_.Fail(Tr(L"Label too long", L"Слишком длинная подпись"));
            n.label.raw = std::wstring(Trim(rest.substr(0, end)));
            n.label.pos = static_cast<uint32_t>(start);
            n.label.len = static_cast<uint32_t>(end);
            n.label.set = true;
            c_.Advance(end + (end2 != std::wstring_view::npos ? std::wcslen(o.close2) : std::wcslen(o.close)));
            return;
        }
    }

    // Arrows: -->, ---, -.->, ==>, ~~~, with o/x/< ends, lengths and labels
    // (-- text -->, -->|text|).
    bool ScanEdge(EdgeToken* e) {
        const size_t save = c_.pos();
        auto fail = [&] {
            c_.set_pos(save);
            return false;
        };
        const wchar_t c0 = c_.Peek(), c1 = c_.Peek(1);
        if (c0 == L'<' && (c1 == L'-' || c1 == L'=' )) {
            e->start = Marker::Arrow;
            c_.Advance();
        } else if ((c0 == L'o' || c0 == L'x') && (c1 == L'-' || c1 == L'=' || c1 == L'.')) {
            e->start = c0 == L'o' ? Marker::Circle : Marker::Cross;
            c_.Advance();
        }
        auto end_marker = [&]() -> bool {  // > o x after the line
            const wchar_t ch = c_.Peek();
            if (ch == L'>') {
                e->end = Marker::Arrow;
                c_.Advance();
                return true;
            }
            if ((ch == L'o' || ch == L'x') && !IsIdChar(c_.Peek(1))) {
                e->end = ch == L'o' ? Marker::Circle : Marker::Cross;
                c_.Advance();
                return true;
            }
            return false;
        };
        auto run = [&](wchar_t ch) {
            size_t n = 0;
            while (c_.Peek() == ch) {
                ++n;
                c_.Advance();
            }
            return n;
        };
        bool text_form = false;
        wchar_t family = 0;
        if (c_.Peek() == L'~') {
            const size_t n = run(L'~');
            if (n < 3) return fail();
            e->stroke = 3;
            e->length = static_cast<int>(n) - 2;
        } else if (c_.Peek() == L'=') {
            const size_t n = run(L'=');
            if (n < 2) return fail();
            e->stroke = 1;
            if (end_marker()) {
                e->length = static_cast<int>(n) - 1;
            } else if (n >= 3) {
                e->length = static_cast<int>(n) - 2;
            } else {
                text_form = true;
                family = L'=';
            }
        } else if (c_.Peek() == L'-' && c_.Peek(1) == L'.') {
            c_.Advance();
            const size_t dots = run(L'.');
            e->stroke = 2;
            if (c_.Peek() == L'-') {
                c_.Advance();
                end_marker();
                e->length = static_cast<int>(dots);
            } else {
                text_form = true;
                family = L'.';
            }
        } else if (c_.Peek() == L'-') {
            const size_t n = run(L'-');
            if (n < 2) return fail();
            if (end_marker()) {
                e->length = static_cast<int>(n) - 1;
            } else if (n >= 3) {
                e->length = static_cast<int>(n) - 2;
            } else {
                text_form = true;
                family = L'-';
            }
        } else {
            return fail();
        }
        if (text_form) {
            // The label runs to the closing half: -->, ---, .->, ==> ...
            const size_t start = c_.pos();
            std::wstring_view rest = c_.Rest();
            size_t q = std::wstring_view::npos;
            for (size_t k = 0; k + 1 < rest.size(); ++k) {
                if (rest[k] == L'\n') break;
                if (family == L'.') {
                    if (rest[k] == L'.' && rest[k + 1] == L'-') {
                        q = k;
                        break;
                    }
                } else if (rest[k] == family && rest[k + 1] == family && k + 2 < rest.size()) {
                    const wchar_t nx = rest[k + 2];
                    if (nx == family || nx == L'>' || nx == L'o' || nx == L'x') {
                        q = k;
                        break;
                    }
                }
            }
            if (q == std::wstring_view::npos) return fail();
            std::wstring_view raw = rest.substr(0, q);
            Label l;
            const std::wstring_view t = Trim(raw);
            if (t.size() >= 2 && t.front() == L'"' && t.back() == L'"') {
                const size_t off = static_cast<size_t>(t.data() - raw.data()) + 1;
                std::wstring_view inner = t.substr(1, t.size() - 2);
                l.pos = static_cast<uint32_t>(start + off);
                l.len = static_cast<uint32_t>(inner.size());
                if (inner.size() >= 2 && inner.front() == L'`' && inner.back() == L'`') {
                    inner = inner.substr(1, inner.size() - 2);
                    l.markdown = true;
                }
                l.raw = std::wstring(inner);
            } else {
                l.raw = std::wstring(t);
                l.pos = static_cast<uint32_t>(start);
                l.len = static_cast<uint32_t>(raw.size());
            }
            l.set = true;
            e->label = std::move(l);
            c_.Advance(q);
            if (family == L'.') {
                const size_t dots = run(L'.');
                if (!c_.Eat(L'-')) return fail();
                end_marker();
                e->length = static_cast<int>(std::max<size_t>(dots, 1));
            } else {
                const size_t n = run(family);
                if (end_marker()) e->length = static_cast<int>(n) - 1;
                else e->length = std::max(1, static_cast<int>(n) - 2);
            }
        }
        e->length = std::clamp(e->length, 1, 10);
        // -->|label|
        const size_t after = c_.pos();
        c_.SkipBlanks();
        if (c_.Eat(L'|')) {
            const size_t start = c_.pos();
            while (!c_.AtEnd() && c_.Peek() != L'|' && c_.Peek() != L'\n') c_.Advance();
            if (!c_.Eat(L'|')) c_.Fail(Tr(L"Expected '|'", L"Ожидалась «|»"));
            std::wstring_view raw = src_.text.substr(start, c_.pos() - 1 - start);
            Label l;
            const std::wstring_view t = Trim(raw);
            l.pos = static_cast<uint32_t>(start);
            l.len = static_cast<uint32_t>(raw.size());
            if (t.size() >= 2 && t.front() == L'"' && t.back() == L'"') {
                std::wstring_view inner = t.substr(1, t.size() - 2);
                if (inner.size() >= 2 && inner.front() == L'`' && inner.back() == L'`') {
                    inner = inner.substr(1, inner.size() - 2);
                    l.markdown = true;
                }
                l.raw = std::wstring(inner);
            } else {
                l.raw = std::wstring(t);
            }
            if (l.raw.size() > kMaxLabel) c_.Fail(Tr(L"Label too long", L"Слишком длинная подпись"));
            l.set = true;
            e->label = std::move(l);
        } else {
            c_.set_pos(after);
        }
        return true;
    }

    const Source& src_;
    Cursor c_;
    std::vector<int> open_;  // subgraphs being read
    std::unordered_map<std::wstring, int> node_ids_, cluster_ids_;
};

// ---------------------------------------------------------------------------
// Layout and drawing
// ---------------------------------------------------------------------------

constexpr float kPadding = 15;        // around node labels (Mermaid's flowchart.padding)
constexpr float kClusterPad = 12;
constexpr float kMargin = 8;
constexpr float kLoopReach = 36;      // self loops, beside their node

struct Placed {
    float w = 0, h = 0;               // bounding box of the shape
    float text_w = 0, text_h = 0;
    SceneBuilder::Measured text;
    Style style;
    D2D1_POINT_2F center{};           // absolute, once placed
    bool loop = false;                // has a self loop (drawn right of it)
};

struct ClusterBox {
    float w = 0, h = 0;               // outer box
    float content_w = 0, content_h = 0;
    float top = 0;                    // title band
    SceneBuilder::Measured title;
    D2D1_POINT_2F center{};           // absolute
    Style style;
};

struct Level {
    int cluster = -1;
    std::vector<Ref> items;           // nodes and subgraphs directly in it
    std::vector<int> edges;           // edges whose ends meet here
    LayeredLayout layout;
    std::vector<int> edge_slot;       // edge -> index in the layered graph's edges
};

class Builder {
public:
    Builder(Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src) {}

    std::shared_ptr<DiagramScene> Build() {
        MeasureAll();
        // Levels: the graph and each subgraph, children before parents.
        const size_t nc = p_.clusters.size();
        levels_.resize(nc + 1);
        for (size_t i = 0; i < nc; ++i) levels_[i + 1].cluster = static_cast<int>(i);
        for (size_t n = 0; n < p_.nodes.size(); ++n) levels_[static_cast<size_t>(p_.nodes[n].cluster + 1)].items.push_back(Ref{static_cast<int>(n), false});
        for (size_t c = 0; c < nc; ++c) levels_[static_cast<size_t>(p_.clusters[c].parent + 1)].items.push_back(Ref{static_cast<int>(c), true});
        AssignEdges();
        // Depth order: deepest first.
        std::vector<int> order(nc);
        for (size_t i = 0; i < nc; ++i) order[i] = static_cast<int>(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return Depth(a) > Depth(b); });
        for (int c : order) LayoutLevel(static_cast<size_t>(c) + 1);
        LayoutLevel(0);

        // The title above, then everything placed from the top.
        float top = kMargin;
        SceneBuilder::Measured title;
        if (!src_.title.empty()) {
            title = b_.Measure(ParseLabel(src_.title, false), 0, true, 1.125f);
            top += title.h + 8;
        }
        const Level& root = levels_[0];
        const float width = std::max(root.layout.width, title.w) + 2 * kMargin;
        const float left = kMargin + (width - 2 * kMargin - root.layout.width) / 2;
        Place(0, left, top);
        const float height = top + root.layout.height + kMargin;

        if (title.layout) b_.Text(title, D2D1::Point2F((width - title.w) / 2, kMargin), Role(kDgText));
        DrawClusters(-1);
        DrawEdges();
        DrawNodes();
        return b_.Finish(width, height);
    }

private:
    int Depth(int cluster) const {
        int d = 0;
        for (int c = cluster; c >= 0 && d <= kMaxNesting; c = p_.clusters[static_cast<size_t>(c)].parent) ++d;
        return d;
    }

    Style NodeStyle(const Node& n) const {
        Style st;
        if (auto it = p_.class_defs.find(L"default"); it != p_.class_defs.end()) st.Merge(it->second);
        for (const std::wstring& c : n.classes) {
            if (auto it = p_.class_defs.find(c); it != p_.class_defs.end()) st.Merge(it->second);
        }
        st.Merge(n.style);
        return st;
    }

    RichText LabelText(const Label& l, const std::wstring& fallback, bool bold, bool italic) const {
        RichText t = ParseLabel(l.set ? std::wstring_view(l.raw) : std::wstring_view(fallback), l.markdown);
        if (bold || italic) {
            if (t.spans.empty() && !t.text.empty()) t.spans.push_back({0, static_cast<uint32_t>(t.text.size()), false, false});
            for (auto& s : t.spans) {
                s.bold = s.bold || bold;
                s.italic = s.italic || italic;
            }
        }
        return t;
    }

    void MeasureAll() {
        placed_.resize(p_.nodes.size());
        for (size_t i = 0; i < p_.nodes.size(); ++i) {
            const Node& n = p_.nodes[i];
            Placed& pl = placed_[i];
            pl.style = NodeStyle(n);
            const RichText t = LabelText(n.label, n.id, pl.style.bold == 1, pl.style.italic == 1);
            pl.text = b_.Measure(t, n.label.markdown ? 200.f : 0.f);
            pl.text_w = pl.text.w;
            pl.text_h = pl.text.h;
            const float w = pl.text_w + kPadding, h = pl.text_h + kPadding;
            switch (n.shape) {
            case Shape::Stadium: pl.w = w + h / 2; pl.h = h; break;
            case Shape::Subroutine: pl.w = w + 16; pl.h = h; break;
            case Shape::Cylinder: {
                const float rx = w / 2, ry = rx / (2.5f + w / 50);
                pl.w = w;
                pl.h = h + 2 * ry;
                break;
            }
            case Shape::Circle: pl.w = pl.h = std::max(w, h); break;
            case Shape::DoubleCircle: pl.w = pl.h = std::max(w, h) + 10; break;
            case Shape::Rhombus: pl.w = pl.h = w + h; break;
            case Shape::Hexagon: pl.w = w + h / 2; pl.h = h; break;
            case Shape::LeanRight:
            case Shape::LeanLeft:
            case Shape::Trapezoid:
            case Shape::TrapezoidAlt: pl.w = w + 2 * h / 3; pl.h = h; break;
            case Shape::Asymmetric: pl.w = w + h / 2; pl.h = h; break;
            case Shape::Text: pl.w = pl.text_w + 4; pl.h = pl.text_h + 4; break;
            default: pl.w = w; pl.h = h; break;
            }
        }
        boxes_.resize(p_.clusters.size());
        for (size_t i = 0; i < p_.clusters.size(); ++i) {
            const Cluster& c = p_.clusters[i];
            ClusterBox& box = boxes_[i];
            Style st;
            for (const std::wstring& cl : c.classes) {
                if (auto it = p_.class_defs.find(cl); it != p_.class_defs.end()) st.Merge(it->second);
            }
            st.Merge(c.style);
            box.style = st;
            const RichText t = LabelText(c.title, c.id, st.bold == 1, st.italic == 1);
            if (!t.text.empty()) box.title = b_.Measure(t, c.title.markdown ? 200.f : 0.f);
        }
        for (const Edge& e : p_.edges) {
            SceneBuilder::Measured m;
            if (e.label.set && !Trim(e.label.raw).empty()) m = b_.Measure(ParseLabel(e.label.raw, e.label.markdown), e.label.markdown ? 200.f : 0.f);
            edge_labels_.push_back(std::move(m));
        }
    }

    // The chain of subgraphs around an edge end, innermost first.
    std::vector<int> Chain(const Ref& r) const {
        std::vector<int> out;
        int c = r.cluster ? r.index : p_.nodes[static_cast<size_t>(r.index)].cluster;
        for (int guard = 0; c >= 0 && guard <= kMaxNesting + 1; ++guard) {
            out.push_back(c);
            c = p_.clusters[static_cast<size_t>(c)].parent;
        }
        return out;
    }

    // What stands for `r` among the items of `level` (a subgraph around it,
    // or itself).
    Ref Representative(const Ref& r, int level_cluster) const {
        if (!r.cluster && p_.nodes[static_cast<size_t>(r.index)].cluster == level_cluster) return r;
        int c = r.cluster ? r.index : p_.nodes[static_cast<size_t>(r.index)].cluster;
        for (int guard = 0; c >= 0 && guard <= kMaxNesting + 1; ++guard) {
            if (p_.clusters[static_cast<size_t>(c)].parent == level_cluster) return Ref{c, true};
            c = p_.clusters[static_cast<size_t>(c)].parent;
        }
        return Ref{-1, false};
    }

    void AssignEdges() {
        for (size_t i = 0; i < p_.edges.size(); ++i) {
            const Edge& e = p_.edges[i];
            // The innermost subgraph holding both ends (not an end itself).
            std::vector<int> a = Chain(e.from), b = Chain(e.to);
            if (e.from.cluster) a.erase(a.begin());
            if (e.to.cluster) b.erase(b.begin());
            int level = -1;
            for (int x : a) {
                if (std::find(b.begin(), b.end(), x) != b.end()) {
                    level = x;
                    break;
                }
            }
            levels_[static_cast<size_t>(level + 1)].edges.push_back(static_cast<int>(i));
        }
    }

    Placed& P(int node) { return placed_[static_cast<size_t>(node)]; }

    void LayoutLevel(size_t li) {
        Level& lv = levels_[li];
        LayeredGraph g;
        g.dir = lv.cluster >= 0 && p_.clusters[static_cast<size_t>(lv.cluster)].has_dir
                    ? p_.clusters[static_cast<size_t>(lv.cluster)].dir
                    : (lv.cluster >= 0 ? InheritedDir(lv.cluster) : p_.dir);
        std::map<std::pair<int, bool>, int> slot;
        for (size_t k = 0; k < lv.items.size(); ++k) {
            const Ref& r = lv.items[k];
            slot[{r.index, r.cluster}] = static_cast<int>(k);
            LayeredGraph::Node n;
            if (r.cluster) {
                n.w = boxes_[static_cast<size_t>(r.index)].w;
                n.h = boxes_[static_cast<size_t>(r.index)].h;
            } else {
                n.w = P(r.index).w;
                n.h = P(r.index).h;
            }
            g.nodes.push_back(n);
        }
        lv.edge_slot.assign(p_.edges.size(), -1);
        for (int ei : lv.edges) {
            const Edge& e = p_.edges[static_cast<size_t>(ei)];
            const Ref a = Representative(e.from, lv.cluster), b = Representative(e.to, lv.cluster);
            const auto ia = slot.find({a.index, a.cluster}), ib = slot.find({b.index, b.cluster});
            if (ia == slot.end() || ib == slot.end()) continue;
            const int sa = ia->second, sb = ib->second;
            if (sa == sb) {
                if (!a.cluster && !e.from.cluster && !e.to.cluster && e.from.index == e.to.index) {
                    P(e.from.index).loop = true;  // drawn beside the node
                }
                continue;
            }
            LayeredGraph::Edge ge;
            ge.from = sa;
            ge.to = sb;
            ge.minlen = e.length;
            const SceneBuilder::Measured& m = edge_labels_[static_cast<size_t>(ei)];
            if (m.layout) {
                ge.label_w = m.w + 8;
                ge.label_h = m.h + 4;
            }
            lv.edge_slot[static_cast<size_t>(ei)] = static_cast<int>(g.edges.size());
            g.edges.push_back(ge);
        }
        // Room for self loops.
        for (size_t k = 0; k < lv.items.size(); ++k) {
            const Ref& r = lv.items[k];
            if (!r.cluster && P(r.index).loop) g.nodes[k].w += 2 * kLoopReach;
        }
        lv.layout = LayoutLayered(g);
        if (lv.cluster >= 0) {
            ClusterBox& box = boxes_[static_cast<size_t>(lv.cluster)];
            box.content_w = lv.layout.width;
            box.content_h = lv.layout.height;
            box.top = box.title.layout ? box.title.h + 8 : 0;
            box.w = std::max(box.content_w, box.title.layout ? box.title.w : 0.f) + 2 * kClusterPad;
            box.h = box.content_h + box.top + 2 * kClusterPad;
        }
    }

    RankDir InheritedDir(int cluster) const {
        for (int c = p_.clusters[static_cast<size_t>(cluster)].parent; c >= 0; c = p_.clusters[static_cast<size_t>(c)].parent) {
            if (p_.clusters[static_cast<size_t>(c)].has_dir) return p_.clusters[static_cast<size_t>(c)].dir;
        }
        return p_.dir;
    }

    // Absolute positions: level `li`'s layout with its top left at (x, y).
    void Place(size_t li, float x, float y) {
        Level& lv = levels_[li];
        origin_.resize(levels_.size());
        origin_[li] = D2D1::Point2F(x, y);
        for (size_t k = 0; k < lv.items.size(); ++k) {
            const Ref& r = lv.items[k];
            const D2D1_POINT_2F c = lv.layout.centers[k];
            const D2D1_POINT_2F abs = D2D1::Point2F(x + c.x, y + c.y);
            if (r.cluster) {
                ClusterBox& box = boxes_[static_cast<size_t>(r.index)];
                box.center = abs;
                const float left = abs.x - box.w / 2 + kClusterPad + (box.w - 2 * kClusterPad - box.content_w) / 2;
                const float top = abs.y - box.h / 2 + kClusterPad + box.top;
                Place(static_cast<size_t>(r.index) + 1, left, top);
            } else {
                // A node with a loop sits left of its widened slot.
                P(r.index).center = D2D1::Point2F(abs.x - (P(r.index).loop ? kLoopReach : 0.f), abs.y);
            }
        }
    }

    D2D1_POINT_2F CenterOf(const Ref& r) const {
        return r.cluster ? boxes_[static_cast<size_t>(r.index)].center : placed_[static_cast<size_t>(r.index)].center;
    }

    std::vector<D2D1_POINT_2F> Outline(int node) const {
        const Placed& pl = placed_[static_cast<size_t>(node)];
        const float cx = pl.center.x, cy = pl.center.y, hw = pl.w / 2, hh = pl.h / 2;
        const float l = cx - hw, r = cx + hw, t = cy - hh, b = cy + hh;
        const float s = pl.h / 3;
        switch (p_.nodes[static_cast<size_t>(node)].shape) {
        case Shape::Rhombus: return {{cx, t}, {r, cy}, {cx, b}, {l, cy}};
        case Shape::Hexagon: return {{l + pl.h / 4, t}, {r - pl.h / 4, t}, {r, cy}, {r - pl.h / 4, b}, {l + pl.h / 4, b}, {l, cy}};
        case Shape::LeanRight: return {{l + s, t}, {r, t}, {r - s, b}, {l, b}};
        case Shape::LeanLeft: return {{l, t}, {r - s, t}, {r, b}, {l + s, b}};
        case Shape::Trapezoid: return {{l + s, t}, {r - s, t}, {r, b}, {l, b}};
        case Shape::TrapezoidAlt: return {{l, t}, {r, t}, {r - s, b}, {l + s, b}};
        case Shape::Asymmetric: return {{l, t}, {r, t}, {r, b}, {l, b}, {l + pl.h / 2, cy}};
        default: return {};
        }
    }

    // Where a line from inside `r` towards `toward` leaves its shape.
    D2D1_POINT_2F Clip(const Ref& r, D2D1_POINT_2F toward) const {
        if (r.cluster) {
            const ClusterBox& box = boxes_[static_cast<size_t>(r.index)];
            return ClipRect(box.center, box.w, box.h, toward);
        }
        const Placed& pl = placed_[static_cast<size_t>(r.index)];
        switch (p_.nodes[static_cast<size_t>(r.index)].shape) {
        case Shape::Circle:
        case Shape::DoubleCircle: return ClipEllipse(pl.center, pl.w / 2, pl.h / 2, toward);
        case Shape::Rhombus:
        case Shape::Hexagon:
        case Shape::LeanRight:
        case Shape::LeanLeft:
        case Shape::Trapezoid:
        case Shape::TrapezoidAlt:
        case Shape::Asymmetric: {
            const auto poly = Outline(r.index);
            const D2D1_POINT_2F p = ClipPolygon(pl.center, poly, toward);
            return (p.x == pl.center.x && p.y == pl.center.y) ? ClipRect(pl.center, pl.w, pl.h, toward) : p;
        }
        default: return ClipRect(pl.center, pl.w, pl.h, toward);
        }
    }

    void DrawClusters(int parent) {
        for (size_t i = 0; i < p_.clusters.size(); ++i) {
            if (p_.clusters[i].parent != parent) continue;
            const ClusterBox& box = boxes_[i];
            const D2D1_RECT_F r = D2D1::RectF(box.center.x - box.w / 2, box.center.y - box.h / 2, box.center.x + box.w / 2,
                                              box.center.y + box.h / 2);
            const Style& st = box.style;
            b_.Rect(r, 0, st.fill.role != kDgNone ? st.fill : Role(kDgClusterFill),
                    st.stroke.role != kDgNone ? st.stroke : Role(kDgClusterStroke), st.stroke_width >= 0 ? st.stroke_width : 1.f,
                    st.dash == 1 ? 1 : 0);
            if (box.title.layout) {
                const Label& l = p_.clusters[i].title;
                b_.Text(box.title, D2D1::Point2F(box.center.x - box.title.w / 2, r.top + kClusterPad / 2 + 2),
                        st.color.role != kDgNone ? st.color : Role(kDgText), l.pos, l.len);
            }
            DrawClusters(static_cast<int>(i));
        }
    }

    void DrawEdges() {
        std::vector<std::pair<size_t, SceneBuilder::Measured*>> labels;
        for (size_t li = 0; li < levels_.size(); ++li) {
            const Level& lv = levels_[li];
            const D2D1_POINT_2F o = origin_[li];
            for (int ei : lv.edges) {
                const Edge& e = p_.edges[static_cast<size_t>(ei)];
                const int slot = lv.edge_slot[static_cast<size_t>(ei)];
                Style st = p_.default_link;
                if (auto it = p_.link_styles.find(static_cast<size_t>(ei)); it != p_.link_styles.end()) st.Merge(it->second);
                const DgColor color = st.stroke.role != kDgNone ? st.stroke : Role(kDgLine);
                const float width = st.stroke_width >= 0 ? st.stroke_width : (e.stroke == 1 ? 3.5f : 2.f);
                const uint8_t dash = e.stroke == 2 ? 2 : (st.dash == 1 ? 1 : 0);
                if (slot < 0) {
                    if (!e.from.cluster && !e.to.cluster && e.from.index == e.to.index) {
                        DrawLoop(e, static_cast<size_t>(ei), color, width, dash, st);
                    }
                    continue;
                }
                if (e.stroke == 3) continue;  // ~~~ only shapes the layout
                std::vector<D2D1_POINT_2F> pts;
                pts.push_back(CenterOf(e.from));
                for (const D2D1_POINT_2F& p : lv.layout.bends[static_cast<size_t>(slot)]) pts.push_back(D2D1::Point2F(o.x + p.x, o.y + p.y));
                pts.push_back(CenterOf(e.to));
                pts.front() = Clip(e.from, pts[1]);
                pts.back() = Clip(e.to, pts[pts.size() - 2]);
                // Drop bends swallowed by the end shapes.
                const D2D1_POINT_2F tip_end = pts.back(), tip_start = pts.front();
                const D2D1_POINT_2F before_end = pts[pts.size() - 2], after_start = pts[1];
                if (e.end != Marker::None) pts.back() = Retreat(pts.back(), before_end, MarkerLength(e.end));
                if (e.start != Marker::None) pts.front() = Retreat(pts.front(), after_start, MarkerLength(e.start));
                AddBasisCurve(b_, pts);
                b_.End(Role(kDgNone), color, width, dash);
                AddMarker(b_, e.end, tip_end, before_end, color, width);
                AddMarker(b_, e.start, tip_start, after_start, color, width);
                if (edge_labels_[static_cast<size_t>(ei)].layout) {
                    const D2D1_POINT_2F lp = lv.layout.labels[static_cast<size_t>(slot)];
                    label_at_[static_cast<size_t>(ei)] = D2D1::Point2F(o.x + lp.x, o.y + lp.y);
                    labels.push_back({static_cast<size_t>(ei), &edge_labels_[static_cast<size_t>(ei)]});
                }
            }
        }
        // Labels over every edge.
        for (auto& [ei, m] : labels) {
            const D2D1_POINT_2F c = label_at_[ei];
            const Edge& e = p_.edges[ei];
            Style st = p_.default_link;
            if (auto it = p_.link_styles.find(ei); it != p_.link_styles.end()) st.Merge(it->second);
            b_.Rect(D2D1::RectF(c.x - m->w / 2 - 2, c.y - m->h / 2, c.x + m->w / 2 + 2, c.y + m->h / 2), 0,
                    Role(kDgLabelBg), Role(kDgNone));
            b_.Text(*m, D2D1::Point2F(c.x - m->w / 2, c.y - m->h / 2), st.color.role != kDgNone ? st.color : Role(kDgText),
                    e.label.pos, e.label.len);
        }
    }

    void DrawLoop(const Edge& e, size_t ei, DgColor color, float width, uint8_t dash, const Style& st) {
        const Placed& pl = placed_[static_cast<size_t>(e.from.index)];
        const float r = pl.center.x + pl.w / 2;
        const float y0 = pl.center.y - pl.h / 4, y1 = pl.center.y + pl.h / 4;
        const D2D1_POINT_2F start = D2D1::Point2F(r, y0), end = D2D1::Point2F(r + 2, y1);
        b_.Begin(start);
        b_.CubicTo(D2D1::Point2F(r + kLoopReach, y0 - 12), D2D1::Point2F(r + kLoopReach, y1 + 12),
                   D2D1::Point2F(end.x + MarkerLength(e.end) * 0.7f, end.y + 2));
        b_.End(Role(kDgNone), color, width, dash);
        AddMarker(b_, e.end, end, D2D1::Point2F(end.x + 10, end.y + 3), color, width);
        const SceneBuilder::Measured& m = edge_labels_[ei];
        if (m.layout) {
            const D2D1_POINT_2F c = D2D1::Point2F(r + kLoopReach * 0.75f, pl.center.y);
            b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg),
                    Role(kDgNone));
            b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), st.color.role != kDgNone ? st.color : Role(kDgText),
                    e.label.pos, e.label.len);
        }
    }

    // Quarter ellipse from `a` to `b` bulging toward `corner`.
    void Quarter(D2D1_POINT_2F a, D2D1_POINT_2F corner, D2D1_POINT_2F b) {
        constexpr float k = 0.5523f;
        b_.CubicTo(D2D1::Point2F(a.x + (corner.x - a.x) * k, a.y + (corner.y - a.y) * k),
                   D2D1::Point2F(b.x + (corner.x - b.x) * k, b.y + (corner.y - b.y) * k), b);
    }

    void DrawNodes() {
        for (size_t i = 0; i < p_.nodes.size(); ++i) {
            const Node& n = p_.nodes[i];
            Placed& pl = placed_[i];
            const Style& st = pl.style;
            const DgColor fill = st.fill.role != kDgNone ? st.fill : Role(kDgNodeFill);
            const DgColor stroke = st.stroke.role != kDgNone ? st.stroke : Role(kDgNodeStroke);
            const float sw = st.stroke_width >= 0 ? st.stroke_width : 1.f;
            const uint8_t dash = st.dash == 1 ? 1 : 0;
            const float cx = pl.center.x, cy = pl.center.y;
            const D2D1_RECT_F r = D2D1::RectF(cx - pl.w / 2, cy - pl.h / 2, cx + pl.w / 2, cy + pl.h / 2);
            switch (n.shape) {
            case Shape::Round: b_.Rect(r, 5, fill, stroke, sw, dash); break;
            case Shape::Stadium: b_.Rect(r, pl.h / 2, fill, stroke, sw, dash); break;
            case Shape::Subroutine:
                b_.Rect(r, 0, fill, stroke, sw, dash);
                b_.Begin(D2D1::Point2F(r.left + 8, r.top));
                b_.LineTo(D2D1::Point2F(r.left + 8, r.bottom));
                b_.Begin(D2D1::Point2F(r.right - 8, r.top));
                b_.LineTo(D2D1::Point2F(r.right - 8, r.bottom));
                b_.End(Role(kDgNone), stroke, sw, dash);
                break;
            case Shape::Cylinder: {
                const float rx = pl.w / 2, ry = rx / (2.5f + (pl.w) / 50);
                const float top = r.top + ry, bottom = r.bottom - ry;
                b_.Begin(D2D1::Point2F(r.left, top));
                b_.LineTo(D2D1::Point2F(r.left, bottom));
                Quarter(D2D1::Point2F(r.left, bottom), D2D1::Point2F(r.left, r.bottom), D2D1::Point2F(cx, r.bottom));
                Quarter(D2D1::Point2F(cx, r.bottom), D2D1::Point2F(r.right, r.bottom), D2D1::Point2F(r.right, bottom));
                b_.LineTo(D2D1::Point2F(r.right, top));
                Quarter(D2D1::Point2F(r.right, top), D2D1::Point2F(r.right, r.top), D2D1::Point2F(cx, r.top));
                Quarter(D2D1::Point2F(cx, r.top), D2D1::Point2F(r.left, r.top), D2D1::Point2F(r.left, top));
                b_.Close();
                b_.End(fill, stroke, sw, dash);
                b_.Begin(D2D1::Point2F(r.left, top));
                Quarter(D2D1::Point2F(r.left, top), D2D1::Point2F(r.left, top + ry), D2D1::Point2F(cx, top + ry));
                Quarter(D2D1::Point2F(cx, top + ry), D2D1::Point2F(r.right, top + ry), D2D1::Point2F(r.right, top));
                b_.End(Role(kDgNone), stroke, sw, dash);
                break;
            }
            case Shape::Circle: b_.Ellipse(r, fill, stroke, sw, dash); break;
            case Shape::DoubleCircle:
                b_.Ellipse(r, fill, stroke, sw, dash);
                b_.Ellipse(D2D1::RectF(r.left + 5, r.top + 5, r.right - 5, r.bottom - 5), Role(kDgNone), stroke, sw, dash);
                break;
            case Shape::Text: break;
            case Shape::Rect: b_.Rect(r, 0, fill, stroke, sw, dash); break;
            default: b_.Polygon(Outline(static_cast<int>(i)), fill, stroke, sw, dash); break;
            }
            float tx = cx - pl.text_w / 2;
            if (n.shape == Shape::Asymmetric) tx += pl.h / 8;
            b_.Text(pl.text, D2D1::Point2F(tx, cy - pl.text_h / 2), st.color.role != kDgNone ? st.color : Role(kDgText),
                    n.label.set ? n.label.pos : 0, n.label.set ? n.label.len : 0);
        }
    }

    Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
    std::vector<Placed> placed_;
    std::vector<ClusterBox> boxes_;
    std::vector<SceneBuilder::Measured> edge_labels_;
    std::vector<Level> levels_;
    std::vector<D2D1_POINT_2F> origin_;
    std::map<size_t, D2D1_POINT_2F> label_at_;
};

}  // namespace

std::shared_ptr<DiagramScene> Flowchart(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    Builder b(p, *builder, src);
    return b.Build();
}

}  // namespace mdv::mermaid
