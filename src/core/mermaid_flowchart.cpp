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
    PaperTape, SmallCircle, FilledCircle, FramedCircle, Fork,  // v11 names only
};

// Shapes too small for a label (Mermaid shows none).
bool Unlabelled(Shape s) {
    return s == Shape::SmallCircle || s == Shape::FilledCircle || s == Shape::FramedCircle || s == Shape::Fork;
}

struct Label {
    std::wstring raw;
    bool markdown = false;
    uint32_t pos = 0, len = 0;  // in the source
    bool set = false;
};

struct Node {
    std::wstring id;
    uint32_t id_pos = 0, id_len = 0;  // where it is first named (its text without a label)
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

bool IsIdChar(wchar_t c) {  // Mermaid's NODE_STRING ('-' and '=' apart)
    return IsWordChar(c) || c == L'.' || c == L'$' || c == L'!' || c == L'?' || c == L'*' || c == L'+' || c == L'\'' ||
           c == L'/' || c == L'\\' || c == L'#';
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
        {L"flag", Shape::PaperTape}, {L"paper-tape", Shape::PaperTape}, {L"subprocess", Shape::Subroutine},
        {L"sm-circ", Shape::SmallCircle}, {L"small-circle", Shape::SmallCircle}, {L"start", Shape::SmallCircle},
        {L"f-circ", Shape::FilledCircle}, {L"filled-circle", Shape::FilledCircle}, {L"junction", Shape::FilledCircle},
        {L"fr-circ", Shape::FramedCircle}, {L"framed-circle", Shape::FramedCircle}, {L"stop", Shape::FramedCircle},
        {L"fork", Shape::Fork}, {L"join", Shape::Fork},
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
        swimlane = c_.EatKeyword(L"swimlane-beta");
        if (!swimlane && !c_.EatKeyword(L"flowchart-elk") && !c_.EatKeyword(L"flowchart") && !c_.EatKeyword(L"graph")) {
            c_.Fail(Tr(L"Expected \"flowchart\"", L"Ожидалось «flowchart»"));
        }
        c_.SkipBlanks();
        if (!c_.AtStatementEnd()) {
            const size_t start = c_.pos();
            while (!c_.AtStatementEnd() && !IsSpace(c_.Peek())) c_.Advance();
            bool ok = false;
            dir = ParseDirection(src_.text.substr(start, c_.pos() - start), &ok);
            if (!ok) {
                c_.set_pos(start);
                c_.Fail(Tr(L"Unknown direction", L"Неизвестное направление"));
            }
        }
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        while (!open_.empty()) CloseSubgraph();
        MergeSubgraphNodes();
    }

    RankDir dir = RankDir::TB;
    bool swimlane = false;  // swimlane-beta: top-level subgraphs are lanes
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::vector<Cluster> clusters;
    std::map<std::wstring, Style> class_defs;
    Style default_link;
    std::map<size_t, Style> link_styles;

private:
    void Statement() {
        if (c_.EatKeyword(L"subgraph", true)) return Subgraph();
        if (c_.EatKeyword(L"end", true)) {
            if (open_.empty()) c_.Fail(Tr(L"\"end\" without \"subgraph\"", L"«end» без «subgraph»"));
            CloseSubgraph();
            return;
        }
        if (c_.EatKeyword(L"direction", true)) {
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
        if (c_.EatKeyword(L"classDef", true)) return ClassDef();
        if (c_.EatKeyword(L"class", true)) return ClassStatement();
        if (c_.EatKeyword(L"style", true)) return StyleStatement();
        if (c_.EatKeyword(L"linkStyle", true)) return LinkStyle();
        if (c_.EatKeyword(L"click", true) || c_.EatKeyword(L"callback", true) || c_.EatKeyword(L"accTitle", true)) {
            c_.LineRest();
            return;
        }
        {
            // e1@{ animate: true }: settings of the edge with that id (none
            // of them drawn).
            size_t k = 0;
            while (c_.Peek(k) && !IsSpace(c_.Peek(k)) && c_.Peek(k) != L'"' && c_.Peek(k) != L'@' && c_.Peek(k) != L';') ++k;
            if (k > 0 && c_.Peek(k) == L'@' && c_.Peek(k + 1) == L'{' && edge_ids_.count(std::wstring(c_.Rest().substr(0, k)))) {
                const size_t open = c_.pos() + k;
                c_.Advance(k + 2);
                bool quoted = false;  // a } in a "value" doesn't end it
                while (!c_.AtEnd() && (quoted || c_.Peek() != L'}')) {
                    if (c_.Peek() == L'"') quoted = !quoted;
                    c_.Advance();
                }
                if (!c_.Eat(L'}')) {
                    c_.set_pos(open);
                    c_.Fail(Tr(L"'@{' without '}'", L"«@{» без «}»"));
                }
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

    // A subgraph linked to before its definition was read as a node: in
    // Mermaid, the link goes to the subgraph.
    void MergeSubgraphNodes() {
        std::vector<int> as_cluster(nodes.size(), -1);
        bool any = false;
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto it = cluster_ids_.find(nodes[i].id);
            if (nodes[i].label.set || it == cluster_ids_.end()) continue;
            as_cluster[i] = it->second;
            Cluster& cl = clusters[static_cast<size_t>(it->second)];
            cl.style.Merge(nodes[i].style);
            cl.classes.insert(cl.classes.end(), nodes[i].classes.begin(), nodes[i].classes.end());
            any = true;
        }
        if (!any) return;
        std::vector<int> remap(nodes.size(), -1);
        std::vector<Node> kept;
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (as_cluster[i] >= 0) continue;
            remap[i] = static_cast<int>(kept.size());
            kept.push_back(std::move(nodes[i]));
        }
        nodes = std::move(kept);
        for (Edge& e : edges) {
            for (Ref* r : {&e.from, &e.to}) {
                if (r->cluster) continue;
                const size_t i = static_cast<size_t>(r->index);
                *r = as_cluster[i] >= 0 ? Ref{as_cluster[i], true} : Ref{remap[i], false};
            }
        }
        node_ids_.clear();
        for (size_t i = 0; i < nodes.size(); ++i) node_ids_.emplace(nodes[i].id, static_cast<int>(i));
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
        // Only for what exists by now, as in Mermaid.
        for (const std::wstring& id : ids) {
            if (auto it = cluster_ids_.find(id); it != cluster_ids_.end()) {
                clusters[static_cast<size_t>(it->second)].classes.push_back(name);
            } else if (auto nt = node_ids_.find(id); nt != node_ids_.end()) {
                nodes[static_cast<size_t>(nt->second)].classes.push_back(name);
            }
        }
    }

    void StyleStatement() {
        c_.SkipBlanks();
        std::vector<std::wstring> ids = Names();
        c_.SkipBlanks();
        Style st;
        st.Apply(c_.LineRest());
        for (const std::wstring& id : ids) {
            if (auto it = cluster_ids_.find(id); it != cluster_ids_.end()) {
                clusters[static_cast<size_t>(it->second)].style.Merge(st);
            } else {
                nodes[static_cast<size_t>(NodeIndex(id))].style.Merge(st);
            }
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
        if (c_.AtEnd()) {
            c_.set_pos(start - 1);  // reported where it opens
            c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        }
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
                    if (edges.size() >= kMaxEdges) {
                        c_.Fail(Tr(L"Too many links: Mermaid draws at most 500", L"Слишком много связей: Mermaid рисует не больше 500"));
                    }
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
            } else if ((ch == L'-' || ch == L'=') && !id.empty() && IsIdChar(c_.Peek(1)) && c_.Peek(1) != L'.') {
                id.push_back(ch);  // node-1, a=b (not before an arrow)
                c_.Advance();
            } else {
                break;
            }
        }
        if (id.empty()) c_.Fail(Tr(L"Expected a node", L"Ожидался узел"));
        const bool shaped = c_.Peek() == L'[' || c_.Peek() == L'(' || c_.Peek() == L'{' ||
                            (c_.Peek() == L'>' && !id.empty()) || (c_.Peek() == L'@' && c_.Peek(1) == L'{');
        if (!shaped) {
            if (auto it = cluster_ids_.find(id); it != cluster_ids_.end() && c_.Peek() != L':') {
                return Ref{it->second, true};
            }
        }
        const int index = NodeIndex(id);
        if (nodes[static_cast<size_t>(index)].id_len == 0) {
            nodes[static_cast<size_t>(index)].id_pos = static_cast<uint32_t>(start);
            nodes[static_cast<size_t>(index)].id_len = static_cast<uint32_t>(id.size());
        }
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
            const size_t open = c_.pos();
            auto unclosed = [&] {
                c_.set_pos(open);
                c_.Fail(Tr(L"'@{' without '}'", L"«@{» без «}»"));
            };
            c_.Advance(2);
            for (;;) {
                c_.SkipStatementGap();
                if (c_.Eat(L'}')) return;
                if (c_.AtEnd()) unclosed();
                const size_t ks = c_.pos();
                while (!c_.AtEnd() && (IsWordChar(c_.Peek()) || c_.Peek() == L'-')) c_.Advance();
                const std::wstring key(src_.text.substr(ks, c_.pos() - ks));
                c_.SkipBlanks();
                if (!c_.Eat(L':')) {
                    if (src_.text.find(L'}', open) == std::wstring_view::npos) unclosed();
                    c_.Fail(Tr(L"Expected ':'", L"Ожидалось «:»"));
                }
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
        {
            // v11: an id before the arrow (A e1@--> B): anything up to an @
            // not followed by { or a quote, as Mermaid's lexer takes it.
            size_t k = 0;
            while (c_.Peek(k) && !IsSpace(c_.Peek(k)) && c_.Peek(k) != L'"' && c_.Peek(k) != L'@' && c_.Peek(k) != L';') ++k;
            if (k > 0 && c_.Peek(k) == L'@' && c_.Peek(k + 1) != L'{' && c_.Peek(k + 1) != L'"') {
                edge_ids_.emplace(std::wstring(c_.Rest().substr(0, k)), static_cast<int>(edges.size()));
                c_.Advance(k + 1);
                c_.SkipBlanks();
            }
        }
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
            if (ch == L'o' || ch == L'x') {  // even before a node: A--oB is A --o B in Mermaid
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
    std::unordered_map<std::wstring, int> edge_ids_;  // v11: A e1@--> B
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
    int loops = 0;                    // self loops: right of it, one around the other
    bool loop_below = false;          // below it instead (LR/RL)
    float loop_labels = 0;            // where their labels end, out from its box
    float loop_side = 0;              // their labels across
};

// The point halfway along a polyline.
D2D1_POINT_2F Halfway(const std::vector<D2D1_POINT_2F>& pts) {
    float total = 0;
    for (size_t i = 1; i < pts.size(); ++i) total += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    float left = total / 2;
    for (size_t i = 1; i < pts.size(); ++i) {
        const float d = std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        if (d >= left && d > 0) {
            const float t = left / d;
            return D2D1::Point2F(pts[i - 1].x + (pts[i].x - pts[i - 1].x) * t, pts[i - 1].y + (pts[i].y - pts[i - 1].y) * t);
        }
        left -= d;
    }
    return pts.empty() ? D2D1::Point2F(0, 0) : pts.back();
}

// How far the k-th self loop of a node reaches out.
float LoopReach(int k) { return kLoopReach * (1 + 0.6f * static_cast<float>(k)); }

// Where a loop's label starts, out from the node's box: past the outermost
// of its `n` loops, and past the label before it (`end`).
float LoopLabelStart(int n, float end) { return std::max(LoopReach(n - 1) * 0.75f + 6, end > 0 ? end + 6 : 0.f); }

// A subgraph is laid out on its own when no edge crosses its border (then
// its direction applies and it is one node of its parent's layout), or else
// as a cluster of its parent's layout, like Mermaid does.
struct ClusterBox {
    bool isolated = false;
    int level = -1;                   // isolated: its own level; else the one it is a cluster of
    int graph_index = -1;             // not isolated: its index among that level's clusters
    float w = 0, h = 0;               // isolated: the outer box
    float content_w = 0, content_h = 0;
    float top = 0;                    // title band
    SceneBuilder::Measured title;
    D2D1_RECT_F rect{};               // absolute, once placed
    Style style;
};

struct Level {
    int cluster = -1;                 // -1: the whole graph
    std::vector<Ref> items;           // nodes and isolated subgraphs laid out in it
    std::vector<int> clusters;        // subgraphs drawn as its clusters (parents first)
    std::vector<int> edges;           // edges whose ends meet here
    LayeredLayout layout;
    std::vector<int> edge_slot;       // edge -> index in the layered graph's edges
};

class Builder {
public:
    Builder(Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src) {}

    std::shared_ptr<DiagramScene> Build() {
        if (p_.swimlane) return BuildLanes();
        MeasureAll();
        Classify();
        AssignEdges();
        // Deepest levels first: their sizes are nodes of their parents.
        std::vector<int> order;
        for (int li = 1; li < static_cast<int>(levels_.size()); ++li) order.push_back(li);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return Depth(levels_[static_cast<size_t>(a)].cluster) > Depth(levels_[static_cast<size_t>(b)].cluster);
        });
        for (int li : order) LayoutLevel(static_cast<size_t>(li));
        LayoutLevel(0);

        // The title above, then everything placed from the top.
        float top = kMargin;
        SceneBuilder::Measured title;
        if (!src_.title.empty()) {
            title = b_.Measure(ParseLabel(src_.title, false), 0, false, 1.125f);  // 18px, as Mermaid's
            top += title.h + 8;
        }
        const Level& root = levels_[0];
        const float width = std::max(root.layout.width, title.w) + 2 * kMargin;
        const float left = kMargin + (width - 2 * kMargin - root.layout.width) / 2;
        origin_.assign(levels_.size(), D2D1::Point2F(0, 0));
        Place(0, left, top);
        const float height = top + root.layout.height + kMargin;

        if (title.layout) {
            b_.Text(title, D2D1::Point2F((width - title.w) / 2, kMargin), Role(kDgText), src_.title_pos, src_.title_len);
        }
        DrawClusters();
        DrawEdges();
        DrawNodes();
        return b_.Finish(width, height);
    }

private:
    int Parent(int c) const { return p_.clusters[static_cast<size_t>(c)].parent; }

    // Swimlanes (Mermaid's swimlane layout, simplified): ranks and orders
    // from the layered layout of the whole graph; each top-level subgraph a
    // lane (a column top to bottom, a row left to right) as wide as its
    // widest rank, nodes side by side in it; lanes with a title band; edges
    // at right angles with rounded corners.
    std::shared_ptr<DiagramScene> BuildLanes() {
        MeasureAll();
        constexpr float kNodeGap = 50, kLayerGap = 50, kLaneGap = 100, kInset = 20;
        const bool lr = p_.dir == RankDir::LR || p_.dir == RankDir::RL;
        const bool reverse = p_.dir == RankDir::BT || p_.dir == RankDir::RL;
        const size_t n = p_.nodes.size();
        auto along_size = [&](size_t i) { return lr ? placed_[i].w : placed_[i].h; };
        auto across_size = [&](size_t i) { return lr ? placed_[i].h : placed_[i].w; };
        // Ranks and orders.
        LayeredGraph g;
        g.dir = RankDir::TB;
        for (size_t i = 0; i < n; ++i) {
            LayeredGraph::Node gn;
            gn.w = across_size(i);
            gn.h = along_size(i);
            g.nodes.push_back(gn);
        }
        for (const Edge& e : p_.edges) {
            if (e.from.cluster || e.to.cluster || e.from.index == e.to.index) continue;
            LayeredGraph::Edge ge;
            ge.from = e.from.index;
            ge.to = e.to.index;
            ge.minlen = std::max(1, e.length);
            g.edges.push_back(ge);
        }
        const LayeredLayout lay = LayoutLayered(g);
        if (lay.too_large) TooLarge();
        std::vector<float> ranks;
        for (size_t i = 0; i < n; ++i) ranks.push_back(lay.centers[i].y);
        std::sort(ranks.begin(), ranks.end());
        ranks.erase(std::unique(ranks.begin(), ranks.end(), [](float a, float b) { return std::fabs(a - b) < 0.5f; }), ranks.end());
        std::vector<int> layer(n, 0);
        for (size_t i = 0; i < n; ++i) {
            layer[i] = static_cast<int>(std::lower_bound(ranks.begin(), ranks.end(), lay.centers[i].y - 0.5f) - ranks.begin());
        }
        const int layers = static_cast<int>(ranks.size());
        // Lanes: the top-level subgraphs in order (nodes in none: a column
        // of their own first, with no lane drawn).
        std::vector<int> lane_of(n, -1);
        for (size_t i = 0; i < n; ++i) {
            for (int c = p_.nodes[i].cluster; c >= 0; c = Parent(c)) lane_of[i] = c;
        }
        std::vector<int> columns;  // lanes used, -1 for none
        if (std::find(lane_of.begin(), lane_of.end(), -1) != lane_of.end()) columns.push_back(-1);
        for (size_t c = 0; c < p_.clusters.size(); ++c) {
            if (Parent(static_cast<int>(c)) < 0 && std::find(lane_of.begin(), lane_of.end(), static_cast<int>(c)) != lane_of.end()) columns.push_back(static_cast<int>(c));
        }
        auto column_of = [&](size_t i) { return static_cast<size_t>(std::find(columns.begin(), columns.end(), lane_of[i]) - columns.begin()); };
        // Nodes of each rank in each lane, in the layout's order.
        std::vector<std::vector<std::vector<size_t>>> cells(static_cast<size_t>(layers), std::vector<std::vector<size_t>>(columns.size()));
        std::vector<size_t> by_x(n);
        for (size_t i = 0; i < n; ++i) by_x[i] = i;
        std::stable_sort(by_x.begin(), by_x.end(), [&](size_t a, size_t b) { return lay.centers[a].x < lay.centers[b].x; });
        for (const size_t i : by_x) cells[static_cast<size_t>(layer[i])][column_of(i)].push_back(i);
        std::vector<float> lane_w(columns.size(), 0), layer_h(static_cast<size_t>(layers), 0);
        for (int l = 0; l < layers; ++l) {
            for (size_t c = 0; c < columns.size(); ++c) {
                const auto& ids = cells[static_cast<size_t>(l)][c];
                float total = ids.empty() ? 0.f : kNodeGap * static_cast<float>(ids.size() - 1);
                for (const size_t i : ids) {
                    total += across_size(i);
                    layer_h[static_cast<size_t>(l)] = std::max(layer_h[static_cast<size_t>(l)], along_size(i));
                }
                lane_w[c] = std::max(lane_w[c], total);
            }
        }
        // Titles: a band at the lanes' start (read upwards in a row).
        std::vector<SceneBuilder::Measured> titles(columns.size());
        float band = 0;
        for (size_t c = 0; c < columns.size(); ++c) {
            if (columns[c] < 0) continue;
            const Cluster& cl = p_.clusters[static_cast<size_t>(columns[c])];
            titles[c] = b_.Measure(ParseLabel(cl.title.raw, cl.title.markdown));
            band = std::max(band, titles[c].h + (lr ? 8.f : 0.f));
            lane_w[c] = std::max(lane_w[c], titles[c].w - kLaneGap + 20);
        }
        // Where things are, across (lanes) and along (ranks).
        std::vector<float> lane_mid(columns.size());
        float cursor = 0;
        for (size_t c = 0; c < columns.size(); ++c) {
            lane_mid[c] = cursor + kLaneGap / 2 + lane_w[c] / 2;
            cursor += lane_w[c] + kLaneGap;
        }
        const float across_total = cursor;
        std::vector<float> layer_mid(static_cast<size_t>(layers));
        float along = band + kInset;
        for (int l = 0; l < layers; ++l) {
            layer_mid[static_cast<size_t>(l)] = along + layer_h[static_cast<size_t>(l)] / 2;
            along += layer_h[static_cast<size_t>(l)] + kLayerGap;
        }
        const float content_end = along - kLayerGap + kInset, content_start = band + kInset;
        auto flip = [&](float a) { return reverse ? content_start + content_end - kInset - a : a; };
        std::vector<D2D1_POINT_2F> canon(n);  // (across, along)
        for (int l = 0; l < layers; ++l) {
            for (size_t c = 0; c < columns.size(); ++c) {
                const auto& ids = cells[static_cast<size_t>(l)][c];
                float total = ids.empty() ? 0.f : kNodeGap * static_cast<float>(ids.size() - 1);
                for (const size_t i : ids) total += across_size(i);
                float at = lane_mid[c] - total / 2;
                for (const size_t i : ids) {
                    canon[i] = D2D1::Point2F(at + across_size(i) / 2, flip(layer_mid[static_cast<size_t>(l)]));
                    at += across_size(i) + kNodeGap;
                }
            }
        }
        // (across, along) to the page, inside the margin and under the title.
        float top = kMargin;
        SceneBuilder::Measured heading;
        if (!src_.title.empty()) {
            heading = b_.Measure(ParseLabel(src_.title, false), 0, false, 1.125f);
            top += heading.h + 8;
        }
        auto page = [&](D2D1_POINT_2F q) { return lr ? D2D1::Point2F(kMargin + q.y, top + q.x) : D2D1::Point2F(kMargin + q.x, top + q.y); };
        for (size_t i = 0; i < n; ++i) placed_[i].center = page(canon[i]);
        const float width = 2 * kMargin + (lr ? content_end : across_total), height = top + kMargin + (lr ? across_total : content_end);
        if (heading.layout) b_.Text(heading, D2D1::Point2F((width - heading.w) / 2, kMargin), Role(kDgText), src_.title_pos, src_.title_len);
        // The lanes.
        for (size_t c = 0; c < columns.size(); ++c) {
            if (columns[c] < 0) continue;
            const float a0 = lane_mid[c] - (lane_w[c] + kLaneGap) / 2, a1 = lane_mid[c] + (lane_w[c] + kLaneGap) / 2;
            const D2D1_POINT_2F p0 = page(D2D1::Point2F(a0, 0)), p1 = page(D2D1::Point2F(a1, content_end)), pb = page(D2D1::Point2F(a1, band));
            const Cluster& cl = p_.clusters[static_cast<size_t>(columns[c])];
            const DgColor fill = cl.style.fill.role != kDgNone ? cl.style.fill : Role(kDgClusterFill);
            const DgColor stroke = cl.style.stroke.role != kDgNone ? cl.style.stroke : Role(kDgClusterStroke);
            const D2D1_RECT_F all = D2D1::RectF(std::min(p0.x, p1.x), std::min(p0.y, p1.y), std::max(p0.x, p1.x), std::max(p0.y, p1.y));
            b_.Rect(all, 0, fill, stroke);
            const D2D1_RECT_F head = lr ? D2D1::RectF(all.left, all.top, pb.x, all.bottom) : D2D1::RectF(all.left, all.top, all.right, pb.y);
            b_.Rect(head, 0, fill, stroke);
            const SceneBuilder::Measured& m = titles[c];
            const float cx = (head.left + head.right) / 2, cy = (head.top + head.bottom) / 2;
            if (lr) b_.TextUp(m, D2D1::Point2F(cx - m.h / 2, cy - m.w / 2), Role(kDgText), cl.title.pos, cl.title.len);
            else b_.Text(m, D2D1::Point2F(cx - m.w / 2, cy - m.h / 2), Role(kDgText), cl.title.pos, cl.title.len);
        }
        // Edges at right angles: down a lane, across into another, around
        // (before the nodes) back up.
        std::vector<std::pair<size_t, D2D1_POINT_2F>> labels;
        std::vector<int> incoming(n, 0);
        for (const Edge& e : p_.edges) {
            if (!e.from.cluster && !e.to.cluster && e.from.index != e.to.index && e.stroke != 3) ++incoming[static_cast<size_t>(e.to.index)];
        }
        for (size_t ei = 0; ei < p_.edges.size(); ++ei) {
            const Edge& e = p_.edges[ei];
            if (e.from.cluster || e.to.cluster || e.from.index == e.to.index || e.stroke == 3) continue;
            const size_t a = static_cast<size_t>(e.from.index), z = static_cast<size_t>(e.to.index);
            const D2D1_POINT_2F ca = canon[a], cz = canon[z];
            const int la = layer[a], lz = layer[z];
            // Routes in order of preference; the first that passes no other
            // node is taken (else the last).
            using Route = std::vector<D2D1_POINT_2F>;
            auto blocked = [&](const Route& r) {
                for (size_t k = 1; k < r.size(); ++k) {
                    const float x0 = std::min(r[k - 1].x, r[k].x), x1 = std::max(r[k - 1].x, r[k].x);
                    const float y0 = std::min(r[k - 1].y, r[k].y), y1 = std::max(r[k - 1].y, r[k].y);
                    for (size_t i = 0; i < n; ++i) {
                        if (i == a || i == z) continue;
                        const float hw = across_size(i) / 2 + 4, hh = along_size(i) / 2 + 4;
                        if (x1 >= canon[i].x - hw && x0 <= canon[i].x + hw && y1 >= canon[i].y - hh && y0 <= canon[i].y + hh) return true;
                    }
                }
                return false;
            };
            auto via = [&](float along) { return Route{ca, D2D1::Point2F(ca.x, along), D2D1::Point2F(cz.x, along), cz}; };
            const float after = flip(layer_mid[static_cast<size_t>(la)] + layer_h[static_cast<size_t>(la)] / 2 + kLayerGap / 2);
            const float before = flip(layer_mid[static_cast<size_t>(lz)] - layer_h[static_cast<size_t>(lz)] / 2 - kLayerGap / 2);
            // Beside everything between the two (right, or left going back).
            auto beside = [&](bool right) {
                const int lo = std::min(la, lz), hi = std::max(la, lz);
                float out = right ? std::max(ca.x + across_size(a) / 2, cz.x + across_size(z) / 2) : std::min(ca.x - across_size(a) / 2, cz.x - across_size(z) / 2);
                const float x0 = std::min(ca.x, cz.x), x1 = std::max(ca.x, cz.x);
                for (size_t i = 0; i < n; ++i) {
                    if (layer[i] <= lo || layer[i] >= hi || canon[i].x + across_size(i) / 2 < x0 - 1 || canon[i].x - across_size(i) / 2 > x1 + 1) continue;
                    out = right ? std::max(out, canon[i].x + across_size(i) / 2) : std::min(out, canon[i].x - across_size(i) / 2);
                }
                out += right ? kInset : -kInset;
                return Route{ca, D2D1::Point2F(out, ca.y), D2D1::Point2F(out, cz.y), cz};
            };
            std::vector<Route> routes;
            if (la < lz) {
                if (std::fabs(ca.x - cz.x) < 0.5f) routes.push_back({ca, cz});
                if (column_of(a) == column_of(z)) {
                    routes.push_back(via(after));
                } else if (incoming[z] > 1) {
                    // (Into a node others reach too: across in the gap
                    // before it, where they meet.)
                    routes.push_back(via(before));
                    routes.push_back({ca, D2D1::Point2F(cz.x, ca.y), cz});
                } else {
                    routes.push_back({ca, D2D1::Point2F(cz.x, ca.y), cz});
                    routes.push_back(via(before));
                }
                routes.push_back(via(after));
                routes.push_back(beside(true));
            } else if (la == lz) {
                routes.push_back({ca, cz});
                routes.push_back(via(after));
            } else {
                routes.push_back(beside(false));
            }
            const Route* chosen = &routes.back();
            for (const Route& r : routes) {
                if (!blocked(r)) {
                    chosen = &r;
                    break;
                }
            }
            const Route q = *chosen;
            std::vector<D2D1_POINT_2F> pts;
            for (const D2D1_POINT_2F& k : q) pts.push_back(page(k));
            pts.front() = Clip(e.from, pts[1]);
            pts.back() = Clip(e.to, pts[pts.size() - 2]);
            DropDoubledEnds(&pts);
            Style st = p_.default_link;
            if (auto it = p_.link_styles.find(ei); it != p_.link_styles.end()) st.Merge(it->second);
            const DgColor color = st.stroke.role != kDgNone ? st.stroke : Role(kDgLine);
            const float lw = st.stroke_width >= 0 ? st.stroke_width : (e.stroke == 1 ? 3.5f : 2.f);
            const uint8_t dash = e.stroke == 2 ? 2 : (st.dash == 1 ? 1 : 0);
            const D2D1_POINT_2F tip_end = pts.back(), tip_start = pts.front(), before_end = pts[pts.size() - 2], after_start = pts[1];
            if (e.end != Marker::None) pts.back() = Retreat(pts.back(), before_end, MarkerLength(e.end));
            if (e.start != Marker::None) pts.front() = Retreat(pts.front(), after_start, MarkerLength(e.start));
            Corners(pts);
            b_.End(Role(kDgNone), color, lw, dash);
            AddMarker(b_, e.end, tip_end, before_end, color, lw);
            AddMarker(b_, e.start, tip_start, after_start, color, lw);
            if (edge_labels_[ei].layout) {
                // On the middle of its longest stretch.
                size_t best = 1;
                float longest = -1;
                for (size_t k = 1; k < pts.size(); ++k) {
                    const float d = std::hypot(pts[k].x - pts[k - 1].x, pts[k].y - pts[k - 1].y);
                    if (d > longest) {
                        longest = d;
                        best = k;
                    }
                }
                labels.push_back({ei, D2D1::Point2F((pts[best].x + pts[best - 1].x) / 2, (pts[best].y + pts[best - 1].y) / 2)});
            }
        }
        for (const auto& [ei, c] : labels) {
            const SceneBuilder::Measured& m = edge_labels_[ei];
            Style st = p_.default_link;
            if (auto it = p_.link_styles.find(ei); it != p_.link_styles.end()) st.Merge(it->second);
            b_.Rect(D2D1::RectF(c.x - m.w / 2 - 2, c.y - m.h / 2, c.x + m.w / 2 + 2, c.y + m.h / 2), 0, Role(kDgLabelBg), Role(kDgNone));
            b_.Text(m, D2D1::Point2F(c.x - m.w / 2, c.y - m.h / 2), st.color.role != kDgNone ? st.color : Role(kDgText), p_.edges[ei].label.pos,
                    p_.edges[ei].label.len);
        }
        DrawNodes();
        return b_.Finish(width, height);
    }

    // A path through `pts`, its corners rounded (5, as Mermaid's).
    void Corners(const std::vector<D2D1_POINT_2F>& pts) {
        b_.Begin(pts[0]);
        for (size_t k = 1; k + 1 < pts.size(); ++k) {
            const D2D1_POINT_2F a = pts[k - 1], c = pts[k], z = pts[k + 1];
            const float din = std::hypot(c.x - a.x, c.y - a.y), dout = std::hypot(z.x - c.x, z.y - c.y);
            const float r = std::min({5.f, din / 2, dout / 2});
            if (r <= 0.01f) {
                b_.LineTo(c);
                continue;
            }
            const D2D1_POINT_2F ui = D2D1::Point2F((c.x - a.x) / din, (c.y - a.y) / din), uo = D2D1::Point2F((z.x - c.x) / dout, (z.y - c.y) / dout);
            const D2D1_POINT_2F in = D2D1::Point2F(c.x - ui.x * r, c.y - ui.y * r), out = D2D1::Point2F(c.x + uo.x * r, c.y + uo.y * r);
            b_.LineTo(in);
            b_.CubicTo(D2D1::Point2F(in.x + ui.x * r * 0.55f, in.y + ui.y * r * 0.55f), D2D1::Point2F(out.x - uo.x * r * 0.55f, out.y - uo.y * r * 0.55f), out);
        }
        b_.LineTo(pts.back());
    }

    int Depth(int cluster) const {
        int d = 0;
        for (int c = cluster; c >= 0 && d <= kMaxNesting; c = Parent(c)) ++d;
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
            if (!Unlabelled(n.shape)) {
                const RichText t = LabelText(n.label, n.id, pl.style.bold == 1, pl.style.italic == 1);
                pl.text = b_.Measure(t, n.label.markdown ? 200.f : 0.f);
                pl.text_w = pl.text.w;
                pl.text_h = pl.text.h;
            }
            const float w = pl.text_w + kPadding, h = pl.text_h + kPadding;
            RankDir dir = p_.dir;  // its subgraph's, if one says
            for (int c = n.cluster; c >= 0; c = Parent(c)) {
                if (p_.clusters[static_cast<size_t>(c)].has_dir) {
                    dir = p_.clusters[static_cast<size_t>(c)].dir;
                    break;
                }
            }
            const bool across = dir == RankDir::LR || dir == RankDir::RL;
            switch (n.shape) {
            case Shape::SmallCircle:
            case Shape::FilledCircle:
            case Shape::FramedCircle: pl.w = pl.h = 14; break;
            case Shape::Fork:  // a bar across the flow
                pl.w = across ? 10.f : 70.f;
                pl.h = across ? 70.f : 10.f;
                break;
            case Shape::PaperTape: pl.w = w; pl.h = h + h / 3; break;
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

    // Subgraphs around an edge end, innermost first (a subgraph end itself
    // is not inside itself).
    std::vector<int> Around(const Ref& r) const {
        std::vector<int> out;
        int c = r.cluster ? Parent(r.index) : p_.nodes[static_cast<size_t>(r.index)].cluster;
        for (int guard = 0; c >= 0 && guard <= kMaxNesting + 1; ++guard, c = Parent(c)) out.push_back(c);
        return out;
    }

    // Which subgraphs are laid out on their own, and the levels.
    void Classify() {
        const size_t nc = p_.clusters.size();
        std::vector<char> crossed(nc, 0);
        for (const Edge& e : p_.edges) {
            const std::vector<int> a = Around(e.from), b = Around(e.to);
            for (const auto* side : {&a, &b}) {
                const auto* other = side == &a ? &b : &a;
                for (int c : *side) {
                    const bool is_end = (e.from.cluster && e.from.index == c) || (e.to.cluster && e.to.index == c);
                    if (!is_end && std::find(other->begin(), other->end(), c) == other->end()) crossed[static_cast<size_t>(c)] = 1;
                }
            }
        }
        levels_.assign(1, Level{});
        for (size_t c = 0; c < nc; ++c) {
            ClusterBox& box = boxes_[c];
            box.isolated = !crossed[c];
            if (box.isolated) {
                box.level = static_cast<int>(levels_.size());
                levels_.push_back(Level{});
                levels_.back().cluster = static_cast<int>(c);
            }
        }
        for (size_t n = 0; n < p_.nodes.size(); ++n) {
            const Ref r{static_cast<int>(n), false};
            levels_[static_cast<size_t>(Container(r))].items.push_back(r);
        }
        for (size_t c = 0; c < nc; ++c) {
            const Ref r{static_cast<int>(c), true};
            const int level = Container(r);
            if (boxes_[c].isolated) {
                levels_[static_cast<size_t>(level)].items.push_back(r);
            } else {
                boxes_[c].level = level;
                boxes_[c].graph_index = static_cast<int>(levels_[static_cast<size_t>(level)].clusters.size());
                levels_[static_cast<size_t>(level)].clusters.push_back(static_cast<int>(c));
            }
        }
    }

    // The level an item is laid out in: that of the innermost isolated
    // subgraph around it (or the graph's).
    int Container(const Ref& r) const {
        int c = r.cluster ? Parent(r.index) : p_.nodes[static_cast<size_t>(r.index)].cluster;
        for (int guard = 0; c >= 0 && guard <= kMaxNesting + 1; ++guard, c = Parent(c)) {
            if (boxes_[static_cast<size_t>(c)].isolated) return boxes_[static_cast<size_t>(c)].level;
        }
        return 0;
    }

    // Levels holding an edge end, innermost first.
    std::vector<int> Levels(const Ref& r) const {
        std::vector<int> out;
        int li = (r.cluster && !boxes_[static_cast<size_t>(r.index)].isolated) ? boxes_[static_cast<size_t>(r.index)].level : Container(r);
        for (int guard = 0; guard <= kMaxNesting + 2; ++guard) {
            out.push_back(li);
            if (li == 0) break;
            li = Container(Ref{levels_[static_cast<size_t>(li)].cluster, true});
        }
        return out;
    }

    void AssignEdges() {
        for (size_t i = 0; i < p_.edges.size(); ++i) {
            const Edge& e = p_.edges[i];
            const std::vector<int> a = Levels(e.from), b = Levels(e.to);
            int level = 0;
            for (int x : a) {
                if (std::find(b.begin(), b.end(), x) != b.end()) {
                    level = x;
                    break;
                }
            }
            levels_[static_cast<size_t>(level)].edges.push_back(static_cast<int>(i));
        }
    }

    // The item of `level` standing for an edge end: itself, the isolated
    // subgraph around it, or, for a cluster, some item inside it.
    Ref Representative(const Ref& r, int level) const {
        if (!(r.cluster && !boxes_[static_cast<size_t>(r.index)].isolated) && Container(r) == level) return r;
        if (r.cluster && !boxes_[static_cast<size_t>(r.index)].isolated && boxes_[static_cast<size_t>(r.index)].level == level) {
            for (const Ref& item : levels_[static_cast<size_t>(level)].items) {
                const std::vector<int> around = Around(item);
                if (std::find(around.begin(), around.end(), r.index) != around.end()) return item;
            }
            return Ref{-1, false};
        }
        int c = r.cluster ? r.index : p_.nodes[static_cast<size_t>(r.index)].cluster;
        for (int guard = 0; c >= 0 && guard <= kMaxNesting + 1; ++guard, c = Parent(c)) {
            if (boxes_[static_cast<size_t>(c)].isolated && Container(Ref{c, true}) == level) return Ref{c, true};
        }
        return Ref{-1, false};
    }

    // The innermost cluster of `level` around an item (-1: none).
    int GraphCluster(const Ref& item, int level) const {
        int c = item.cluster ? Parent(item.index) : p_.nodes[static_cast<size_t>(item.index)].cluster;
        for (int guard = 0; c >= 0 && guard <= kMaxNesting + 1; ++guard, c = Parent(c)) {
            const ClusterBox& box = boxes_[static_cast<size_t>(c)];
            if (box.isolated) return -1;
            if (box.level == level) return box.graph_index;
        }
        return -1;
    }

    Placed& P(int node) { return placed_[static_cast<size_t>(node)]; }

    RankDir LevelDir(const Level& lv) const {
        for (int c = lv.cluster; c >= 0; c = Parent(c)) {
            if (p_.clusters[static_cast<size_t>(c)].has_dir) return p_.clusters[static_cast<size_t>(c)].dir;
        }
        return p_.dir;
    }

    void LayoutLevel(size_t li) {
        Level& lv = levels_[li];
        LayeredGraph g;
        g.dir = LevelDir(lv);
        g.cluster_pad = kClusterPad;
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
            n.cluster = GraphCluster(r, static_cast<int>(li));
            g.nodes.push_back(n);
        }
        for (int c : lv.clusters) {
            LayeredGraph::Cluster gc;
            const int parent = Parent(c);
            gc.parent = parent >= 0 && !boxes_[static_cast<size_t>(parent)].isolated && boxes_[static_cast<size_t>(parent)].level == static_cast<int>(li)
                            ? boxes_[static_cast<size_t>(parent)].graph_index
                            : -1;
            const ClusterBox& box = boxes_[static_cast<size_t>(c)];
            if (box.title.layout) {
                gc.title_w = box.title.w + 8;
                gc.title_h = box.title.h;
            }
            g.clusters.push_back(gc);
        }
        lv.edge_slot.assign(p_.edges.size(), -1);
        for (int ei : lv.edges) {
            const Edge& e = p_.edges[static_cast<size_t>(ei)];
            const Ref a = Representative(e.from, static_cast<int>(li)), b = Representative(e.to, static_cast<int>(li));
            const auto ia = slot.find({a.index, a.cluster}), ib = slot.find({b.index, b.cluster});
            if (ia == slot.end() || ib == slot.end()) continue;
            const int sa = ia->second, sb = ib->second;
            if (sa == sb) {
                if (!e.from.cluster && !e.to.cluster && e.from.index == e.to.index && e.stroke != 3) {
                    // A self loop: counted, with its label's length.
                    Placed& pl = P(e.from.index);
                    pl.loop_below = g.dir == RankDir::LR || g.dir == RankDir::RL;
                    const SceneBuilder::Measured& m = edge_labels_[static_cast<size_t>(ei)];
                    ++pl.loops;
                    if (m.layout) {
                        pl.loop_labels += (pl.loop_below ? m.h : m.w) + 6;
                        pl.loop_side = std::max(pl.loop_side, pl.loop_below ? m.w : m.h);
                    }
                }
                continue;
            }
            LayeredGraph::Edge ge;
            ge.from = sa;
            ge.to = sb;
            ge.minlen = e.length;
            // A subgraph end of this level: the edge meets its box, leaving
            // from its bottom or entering at its top (as in Mermaid).
            auto box_end = [&](const Ref& r) {
                if (!r.cluster) return -1;
                const ClusterBox& box = boxes_[static_cast<size_t>(r.index)];
                return !box.isolated && box.level == static_cast<int>(li) ? box.graph_index : -1;
            };
            ge.from_cluster = box_end(e.from);
            ge.to_cluster = box_end(e.to);
            const SceneBuilder::Measured& m = edge_labels_[static_cast<size_t>(ei)];
            if (m.layout) {
                ge.label_w = m.w + 8;
                ge.label_h = m.h + 4;
            }
            lv.edge_slot[static_cast<size_t>(ei)] = static_cast<int>(g.edges.size());
            g.edges.push_back(ge);
        }
        // Room beside a node for its loops, then their labels one after
        // another (DrawLoop places them the same way).
        for (size_t k = 0; k < lv.items.size(); ++k) {
            const Ref& r = lv.items[k];
            if (r.cluster || P(r.index).loops == 0) continue;
            const Placed& pl = P(r.index);
            g.nodes[k].after = LoopReach(pl.loops - 1) * 0.75f + 6 + pl.loop_labels;
            // Across, laid out as wide as its loops' labels (drawn its own size).
            float& across = pl.loop_below ? g.nodes[k].w : g.nodes[k].h;
            across = std::max(across, pl.loop_side + 8);
        }
        lv.layout = LayoutLayered(g);
        if (lv.layout.too_large) TooLarge();
        if (lv.cluster >= 0) {
            ClusterBox& box = boxes_[static_cast<size_t>(lv.cluster)];
            box.content_w = lv.layout.width;
            box.content_h = lv.layout.height;
            box.top = box.title.layout ? box.title.h + 8 : 0;
            box.w = std::max(box.content_w, box.title.layout ? box.title.w : 0.f) + 2 * kClusterPad;
            box.h = box.content_h + box.top + 2 * kClusterPad;
        }
    }

    // Absolute positions: level `li`'s layout with its top left at (x, y).
    void Place(size_t li, float x, float y) {
        Level& lv = levels_[li];
        origin_[li] = D2D1::Point2F(x, y);
        for (size_t k = 0; k < lv.items.size(); ++k) {
            const Ref& r = lv.items[k];
            const D2D1_POINT_2F c = lv.layout.centers[k];
            const D2D1_POINT_2F abs = D2D1::Point2F(x + c.x, y + c.y);
            if (r.cluster) {
                ClusterBox& box = boxes_[static_cast<size_t>(r.index)];
                box.rect = D2D1::RectF(abs.x - box.w / 2, abs.y - box.h / 2, abs.x + box.w / 2, abs.y + box.h / 2);
                const float left = box.rect.left + kClusterPad + (box.w - 2 * kClusterPad - box.content_w) / 2;
                const float top = box.rect.top + kClusterPad + box.top;
                Place(static_cast<size_t>(box.level), left, top);
            } else {
                P(r.index).center = abs;
            }
        }
        for (size_t k = 0; k < lv.clusters.size(); ++k) {
            const D2D1_RECT_F& r = lv.layout.clusters[k];
            boxes_[static_cast<size_t>(lv.clusters[k])].rect = D2D1::RectF(x + r.left, y + r.top, x + r.right, y + r.bottom);
        }
    }

    D2D1_POINT_2F CenterOf(const Ref& r) const {
        if (r.cluster) {
            const D2D1_RECT_F& b = boxes_[static_cast<size_t>(r.index)].rect;
            return D2D1::Point2F((b.left + b.right) / 2, (b.top + b.bottom) / 2);
        }
        return placed_[static_cast<size_t>(r.index)].center;
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
            const D2D1_RECT_F& b = boxes_[static_cast<size_t>(r.index)].rect;
            return ClipRect(CenterOf(r), b.right - b.left, b.bottom - b.top, toward);
        }
        const Placed& pl = placed_[static_cast<size_t>(r.index)];
        switch (p_.nodes[static_cast<size_t>(r.index)].shape) {
        case Shape::Circle:
        case Shape::DoubleCircle:
        case Shape::SmallCircle:
        case Shape::FilledCircle:
        case Shape::FramedCircle: return ClipEllipse(pl.center, pl.w / 2, pl.h / 2, toward);
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

    void DrawClusters() {
        std::vector<int> order(p_.clusters.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return Depth(a) < Depth(b); });
        for (int i : order) {
            const ClusterBox& box = boxes_[static_cast<size_t>(i)];
            const D2D1_RECT_F& r = box.rect;
            if (r.right <= r.left || r.bottom <= r.top) continue;
            const Style& st = box.style;
            b_.Rect(r, 0, st.fill.role != kDgNone ? st.fill : Role(kDgClusterFill),
                    st.stroke.role != kDgNone ? st.stroke : Role(kDgClusterStroke), st.stroke_width >= 0 ? st.stroke_width : 1.f,
                    st.dash == 1 ? 1 : 0);
            if (box.title.layout) {
                const Label& l = p_.clusters[static_cast<size_t>(i)].title;
                const float y = r.top + (box.isolated ? kClusterPad / 2 + 2 : kClusterPad / 2);
                b_.Text(box.title, D2D1::Point2F((r.left + r.right) / 2 - box.title.w / 2, y),
                        st.color.role != kDgNone ? st.color : Role(kDgText), l.pos, l.len);
            }
        }
    }

    void DrawEdges() {
        std::vector<std::pair<size_t, SceneBuilder::Measured*>> labels;
        std::vector<int> loops(p_.nodes.size(), 0);  // drawn so far, per node
        std::vector<float> loop_labels(p_.nodes.size(), 0.f);
        for (size_t li = 0; li < levels_.size(); ++li) {
            const Level& lv = levels_[li];
            const D2D1_POINT_2F o = origin_[li];
            for (int ei : lv.edges) {
                const Edge& e = p_.edges[static_cast<size_t>(ei)];
                const int slot = lv.edge_slot.empty() ? -1 : lv.edge_slot[static_cast<size_t>(ei)];
                Style st = p_.default_link;
                if (auto it = p_.link_styles.find(static_cast<size_t>(ei)); it != p_.link_styles.end()) st.Merge(it->second);
                const DgColor color = st.stroke.role != kDgNone ? st.stroke : Role(kDgLine);
                const float width = st.stroke_width >= 0 ? st.stroke_width : (e.stroke == 1 ? 3.5f : 2.f);
                const uint8_t dash = e.stroke == 2 ? 2 : (st.dash == 1 ? 1 : 0);
                if (slot < 0) {
                    if (!e.from.cluster && !e.to.cluster && e.from.index == e.to.index && e.stroke != 3) {
                        DrawLoop(e, static_cast<size_t>(ei), loops[static_cast<size_t>(e.from.index)]++,
                                 &loop_labels[static_cast<size_t>(e.from.index)], color, width, dash, st);
                    }
                    continue;
                }
                if (e.stroke == 3) continue;  // ~~~ only shapes the layout
                std::vector<D2D1_POINT_2F> pts;
                pts.push_back(CenterOf(e.from));
                for (const D2D1_POINT_2F& p : lv.layout.bends[static_cast<size_t>(slot)]) pts.push_back(D2D1::Point2F(o.x + p.x, o.y + p.y));
                pts.push_back(CenterOf(e.to));
                // A subgraph end: the edge starts (or ends) where it leaves
                // the box, not at bends still inside it.
                auto inside = [&](const Ref& r, D2D1_POINT_2F p) {
                    const D2D1_RECT_F& b = boxes_[static_cast<size_t>(r.index)].rect;
                    return p.x > b.left && p.x < b.right && p.y > b.top && p.y < b.bottom;
                };
                if (e.from.cluster) {
                    while (pts.size() > 2 && inside(e.from, pts[1])) pts.erase(pts.begin() + 1);
                }
                if (e.to.cluster) {
                    while (pts.size() > 2 && inside(e.to, pts[pts.size() - 2])) pts.erase(pts.end() - 2);
                }
                pts.front() = Clip(e.from, pts[1]);
                pts.back() = Clip(e.to, pts[pts.size() - 2]);
                DropDoubledEnds(&pts);
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
                    // Laid out towards a node inside, drawn to the box: the
                    // label goes in the middle of what is drawn.
                    if (e.from.cluster || e.to.cluster) label_at_[static_cast<size_t>(ei)] = Halfway(pts);
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

    // The k-th self loop of a node: out of its outline and back, right of it
    // (below it in LR/RL), around the ones before; its label past its far
    // end and past the labels before (`labels`: where they end).
    void DrawLoop(const Edge& e, size_t ei, int k, float* labels, DgColor color, float width, uint8_t dash, const Style& st) {
        const Ref ref{e.from.index, false};
        const Placed& pl = placed_[static_cast<size_t>(e.from.index)];
        const bool below = pl.loop_below;
        auto at = [&](float out, float side) {  // `out` from the center away from the node, `side` along it
            return below ? D2D1::Point2F(pl.center.x + side, pl.center.y + out) : D2D1::Point2F(pl.center.x + out, pl.center.y + side);
        };
        const float half_out = (below ? pl.h : pl.w) / 2, s = (below ? pl.w : pl.h) / 4;
        const D2D1_POINT_2F a = Clip(ref, at(half_out, -s)), b = Clip(ref, at(half_out, s));
        const float edge = below ? std::max(a.y, b.y) - pl.center.y : std::max(a.x, b.x) - pl.center.x;
        const float reach = LoopReach(k);
        const D2D1_POINT_2F c1 = at(edge + reach, -s - 12), c2 = at(edge + reach, s + 12);
        const D2D1_POINT_2F from = e.start != Marker::None ? Retreat(a, c1, MarkerLength(e.start)) : a;
        const D2D1_POINT_2F to = e.end != Marker::None ? Retreat(b, c2, MarkerLength(e.end)) : b;
        b_.Begin(from);
        b_.CubicTo(c1, c2, to);
        b_.End(Role(kDgNone), color, width, dash);
        AddMarker(b_, e.end, b, c2, color, width);
        AddMarker(b_, e.start, a, c1, color, width);
        const SceneBuilder::Measured& m = edge_labels_[ei];
        if (m.layout) {
            const float len = below ? m.h : m.w, start = LoopLabelStart(pl.loops, *labels);
            *labels = start + len;
            const D2D1_POINT_2F c = at(half_out + start + len / 2, 0);
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
                b_.MoveTo(D2D1::Point2F(r.right - 8, r.top));
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
            case Shape::SmallCircle: b_.Ellipse(r, fill, stroke, sw, dash); break;
            case Shape::FilledCircle: b_.Ellipse(r, stroke, stroke, sw, dash); break;
            case Shape::FramedCircle:
                b_.Ellipse(r, fill, stroke, sw, dash);
                b_.Ellipse(D2D1::RectF(r.left + 3, r.top + 3, r.right - 3, r.bottom - 3), stroke, Role(kDgNone));
                break;
            case Shape::Fork: b_.Rect(r, 0, stroke, stroke, sw); break;
            case Shape::PaperTape: {
                // Wavy top and bottom: a sine wave, one period across.
                const float a = pl.h / 16, w = pl.w, t = r.top + 2 * a, bt = r.bottom - 2 * a;
                b_.Begin(D2D1::Point2F(r.left, t));
                b_.CubicTo(D2D1::Point2F(r.left + w / 6, t - 2 * a), D2D1::Point2F(r.left + w / 3, t - 2 * a), D2D1::Point2F(cx, t));
                b_.CubicTo(D2D1::Point2F(cx + w / 6, t + 2 * a), D2D1::Point2F(cx + w / 3, t + 2 * a), D2D1::Point2F(r.right, t));
                b_.LineTo(D2D1::Point2F(r.right, bt));
                b_.CubicTo(D2D1::Point2F(r.right - w / 6, bt + 2 * a), D2D1::Point2F(r.right - w / 3, bt + 2 * a), D2D1::Point2F(cx, bt));
                b_.CubicTo(D2D1::Point2F(cx - w / 6, bt - 2 * a), D2D1::Point2F(cx - w / 3, bt - 2 * a), D2D1::Point2F(r.left, bt));
                b_.Close();
                b_.End(fill, stroke, sw, dash);
                break;
            }
            default: b_.Polygon(Outline(static_cast<int>(i)), fill, stroke, sw, dash); break;
            }
            float tx = cx - pl.text_w / 2;
            if (n.shape == Shape::Asymmetric) tx += pl.h / 8;
            if (pl.text.layout) {
                b_.Text(pl.text, D2D1::Point2F(tx, cy - pl.text_h / 2), st.color.role != kDgNone ? st.color : Role(kDgText),
                        n.label.set ? n.label.pos : n.id_pos, n.label.set ? n.label.len : n.id_len);
            }
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

namespace {

// Whether the layout will fit its budget, found when the document is read
// (a diagram too large stays code): every node and subgraph in one layered
// graph, sizes aside, as they don't change the ranks.
void CheckBudget(const Parser& p) {
    LayeredGraph g;
    g.dir = p.dir;
    for (const Cluster& c : p.clusters) {
        LayeredGraph::Cluster gc;
        gc.parent = c.parent;
        g.clusters.push_back(gc);
    }
    std::vector<int> member(p.clusters.size(), -1);  // a node inside each subgraph
    for (size_t i = 0; i < p.nodes.size(); ++i) {
        LayeredGraph::Node n;
        n.w = n.h = 10;
        n.cluster = p.nodes[i].cluster;
        g.nodes.push_back(n);
        for (int c = p.nodes[i].cluster; c >= 0 && member[static_cast<size_t>(c)] < 0; c = p.clusters[static_cast<size_t>(c)].parent) {
            member[static_cast<size_t>(c)] = static_cast<int>(i);
        }
    }
    auto end = [&](const Ref& r) { return r.cluster ? member[static_cast<size_t>(r.index)] : r.index; };
    for (const Edge& e : p.edges) {
        const int a = end(e.from), b = end(e.to);
        if (a < 0 || b < 0 || a == b) continue;
        LayeredGraph::Edge ge;
        ge.from = a;
        ge.to = b;
        ge.minlen = e.length;
        g.edges.push_back(ge);
    }
    if (LayoutLayered(g, true).too_large) TooLarge();
}

}  // namespace

std::shared_ptr<DiagramScene> Flowchart(const Source& src, SceneBuilder* builder) {
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
