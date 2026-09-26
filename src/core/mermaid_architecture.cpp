// Mermaid architecture diagrams (architecture-beta): services drawn as icons
// (database, disk, server, internet, cloud; other names a "?", as Mermaid
// shows them without their icon pack), junctions where edges meet, groups
// as dashed boxes with an icon and a title, edges between the sides of
// services. Mermaid lays them out with a force-directed layout (fcose) held
// to the grid the edges' sides imply; here each service goes on that grid
// (found as Mermaid finds it), groups' borders widening the gaps.
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

// A service, a junction or a group.
struct Item {
    enum class Kind : uint8_t { Service, Junction, Group };
    Kind kind = Kind::Service;
    std::wstring id;
    std::wstring icon;  // (name)
    Text icon_text;     // a service's "text" in place of an icon
    Text title;         // [title]
    std::wstring in;    // the group it is in
    size_t pos = 0;     // its statement
    int parent = -1;    // that group's index
};

// a:R <-[title]-> L:b, a{group}:B -- T:b
struct Edge {
    std::wstring from, to;
    wchar_t from_dir = L'R', to_dir = L'L';
    bool from_arrow = false, to_arrow = false;
    bool from_group = false, to_group = false;
    Text title;
    size_t pos = 0;
    int a = -1, b = -1;  // items
};

// align row|column a b c
struct Align {
    bool row = true;
    std::vector<std::wstring> members;
    std::vector<int> items;
    size_t pos = 0;
};

bool IsX(wchar_t d) { return d == L'L' || d == L'R'; }

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"architecture-beta", true)) c_.Fail(Tr(L"Expected \"architecture-beta\"", L"Ожидалось «architecture-beta»"));
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        Resolve();
    }

    std::vector<Item> items;
    std::vector<Edge> edges;
    std::vector<Align> aligns;

private:
    void Statement() {
        const size_t at = c_.pos();
        if (c_.EatKeyword(L"group", true)) return Declare(Item::Kind::Group, at);
        if (c_.EatKeyword(L"service", true)) return Declare(Item::Kind::Service, at);
        if (c_.EatKeyword(L"junction", true)) return Declare(Item::Kind::Junction, at);
        if (c_.EatKeyword(L"align", true)) return AlignStatement(at);
        if (c_.EatKeyword(L"title", true) || c_.EatKeyword(L"accTitle", true)) {
            c_.WholeLine();  // (Mermaid doesn't show an architecture diagram's title)
            return;
        }
        if (c_.EatKeyword(L"accDescr", true)) {
            c_.SkipBlanks();
            if (c_.Eat(L'{')) {
                while (!c_.AtEnd() && c_.Peek() != L'}') c_.Advance();
                if (!c_.Eat(L'}')) {
                    c_.set_pos(at);
                    c_.Fail(Tr(L"'{' without '}'", L"«{» без «}»"));
                }
            } else {
                c_.WholeLine();
            }
            return;
        }
        EdgeStatement(at);
    }

    // group id(icon)[title] in parent, service id(icon)[title] in parent
    // (or id "text"[title]), junction id in parent.
    void Declare(Item::Kind kind, size_t at) {
        Item it;
        it.kind = kind;
        it.pos = at;
        c_.SkipBlanks();
        it.id = Id();
        c_.SkipBlanks();
        if (kind != Item::Kind::Junction) {
            if (kind == Item::Kind::Service && (c_.Peek() == L'"' || c_.Peek() == L'\'')) {
                it.icon_text = Quoted();
            } else if (c_.Peek() == L'(') {
                it.icon = Icon();
            }
            c_.SkipBlanks();
            if (c_.Peek() == L'[') it.title = Title();
            c_.SkipBlanks();
        }
        if (c_.EatKeyword(L"in", true)) {
            c_.SkipBlanks();
            it.in = Id();
        }
        if (items.size() >= kMaxNodes) c_.Fail(Tr(L"Too many services", L"Слишком много сервисов"));
        items.push_back(std::move(it));
    }

    void EdgeStatement(size_t at) {
        Edge e;
        e.pos = at;
        if (!IsWordChar(c_.Peek())) {
            c_.Fail(Tr(L"Expected a service, a group, a junction or an edge (a:R -- L:b)",
                       L"Ожидался сервис, группа, соединение или связь (a:R -- L:b)"));
        }
        e.from = Id();
        e.from_group = GroupMark();
        c_.SkipBlanks();
        if (!c_.Eat(L':')) {
            c_.set_pos(at);
            c_.Fail(Tr(L"Expected a service, a group, a junction or an edge (a:R -- L:b)",
                       L"Ожидался сервис, группа, соединение или связь (a:R -- L:b)"));
        }
        c_.SkipBlanks();
        e.from_dir = Side();
        c_.SkipBlanks();
        e.from_arrow = c_.Eat(L'<');
        c_.SkipBlanks();
        if (!c_.EatStr(L"--")) {
            if (!c_.Eat(L'-')) c_.Fail(Tr(L"Expected \"--\"", L"Ожидалось «--»"));
            c_.SkipBlanks();
            if (c_.Peek() != L'[') c_.Fail(Tr(L"Expected \"--\" or \"-[label]-\"", L"Ожидалось «--» или «-[подпись]-»"));
            e.title = Title();
            c_.SkipBlanks();
            if (!c_.Eat(L'-')) c_.Fail(Tr(L"Expected '-'", L"Ожидался «-»"));
        }
        c_.SkipBlanks();
        e.to_arrow = c_.Eat(L'>');
        c_.SkipBlanks();
        e.to_dir = Side();
        c_.SkipBlanks();
        if (!c_.Eat(L':')) c_.Fail(Tr(L"Expected ':'", L"Ожидалось «:»"));
        c_.SkipBlanks();
        e.to = Id();
        e.to_group = GroupMark();
        if (edges.size() >= kMaxNodes) c_.Fail(Tr(L"Too many edges", L"Слишком много связей"));
        edges.push_back(std::move(e));
    }

    void AlignStatement(size_t at) {
        Align a;
        a.pos = at;
        c_.SkipBlanks();
        if (c_.EatKeyword(L"row", true)) a.row = true;
        else if (c_.EatKeyword(L"column", true)) a.row = false;
        else c_.Fail(Tr(L"Expected \"row\" or \"column\"", L"Ожидалось «row» или «column»"));
        for (;;) {
            c_.SkipBlanks();
            if (c_.AtStatementEnd()) break;
            if (++members_ > kMaxNodes) c_.Fail(Tr(L"Too many aligned services", L"Слишком много выровненных сервисов"));
            a.members.push_back(Id());
        }
        if (a.members.size() < 2) c_.Fail(Tr(L"align needs two services or more", L"align нужно хотя бы два сервиса"));
        aligns.push_back(std::move(a));
    }

    // [\w]([-\w]*\w)?
    std::wstring Id() {
        const size_t s = c_.pos();
        while (IsWordChar(c_.Peek()) || (c_.Peek() == L'-' && c_.pos() > s)) c_.Advance();
        while (c_.pos() > s && src_.text[c_.pos() - 1] == L'-') c_.set_pos(c_.pos() - 1);
        if (c_.pos() == s) c_.Fail(Tr(L"Expected an id", L"Ожидался идентификатор"));
        if (c_.pos() - s > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return std::wstring(src_.text.substr(s, c_.pos() - s));
    }

    bool GroupMark() {
        const size_t at = c_.pos();
        c_.SkipBlanks();
        if (c_.EatStr(L"{group}")) return true;
        c_.set_pos(at);
        return false;
    }

    wchar_t Side() {
        const wchar_t d = c_.Peek();
        if ((d != L'L' && d != L'R' && d != L'T' && d != L'B') || IsWordChar(c_.Peek(1))) {
            c_.Fail(Tr(L"Expected a side: L, R, T or B", L"Ожидалась сторона: L, R, T или B"));
        }
        c_.Advance();
        return d;
    }

    // (name): letters, digits, '-', ':' (an icon pack's prefix).
    std::wstring Icon() {
        const size_t open = c_.pos();
        c_.Advance();
        const size_t s = c_.pos();
        while (IsWordChar(c_.Peek()) || c_.Peek() == L'-' || c_.Peek() == L':') c_.Advance();
        const size_t e = c_.pos();
        if (e == s || !c_.Eat(L')')) {
            c_.set_pos(open);
            c_.Fail(Tr(L"Expected an icon: (name)", L"Ожидался значок: (имя)"));
        }
        if (e - s > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return std::wstring(src_.text.substr(s, e - s));
    }

    // [text], ["text"] or ['text'] (a quote not closed right before the ']'
    // is part of the text).
    Text Title() {
        const size_t open = c_.pos();
        c_.Advance();
        if (c_.Peek() == L'"' || c_.Peek() == L'\'') {
            const size_t q = c_.pos();
            if (const Text t = Quoted(false); c_.Eat(L']')) return t;
            c_.set_pos(q);
        }
        const size_t s = c_.pos();
        while (!c_.AtEnd() && c_.Peek() != L']' && c_.Peek() != L'[' && c_.Peek() != L'\n' && c_.Peek() != L'\r') c_.Advance();
        const size_t e = c_.pos();
        if (e == s || !c_.Eat(L']')) {
            c_.set_pos(open);
            c_.Fail(Tr(L"Expected [title]", L"Ожидалось [название]"));
        }
        return Here(Trim(src_.text.substr(s, e - s)));
    }

    // "text" or 'text', with \ escapes. `must_close`: an unclosed one is an error.
    Text Quoted(bool must_close = true) {
        const size_t open = c_.pos();
        const wchar_t quote = c_.Peek();
        c_.Advance();
        const size_t s = c_.pos();
        std::wstring raw;
        while (!c_.AtEnd() && c_.Peek() != quote && c_.Peek() != L'\n') {
            if (c_.Peek() == L'\\' && c_.Peek(1) != L'\0' && c_.Peek(1) != L'\n') c_.Advance();
            raw.push_back(c_.Peek());
            c_.Advance();
        }
        const size_t e = c_.pos();
        if (!c_.Eat(quote)) {
            if (!must_close) return {};
            c_.set_pos(open);
            c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        }
        if (raw.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return Text{std::move(raw), static_cast<uint32_t>(s), static_cast<uint32_t>(e - s)};
    }

    Text Here(std::wstring_view s) {
        if (s.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return Text{std::wstring(s), static_cast<uint32_t>(s.data() - src_.text.data()), static_cast<uint32_t>(s.size())};
    }

    [[noreturn]] void FailAt(size_t pos, std::wstring message) {
        c_.set_pos(pos);
        c_.Fail(std::move(message));
    }

    // Mermaid's checks, in its order: groups, then services, junctions,
    // edges and aligns (a group's parent declared before it).
    void Resolve() {
        std::unordered_map<std::wstring, int> ids;
        auto fail = [&](size_t pos, std::wstring message) { FailAt(pos, std::move(message)); };
        std::vector<int> depth(items.size(), 0);
        for (const Item::Kind kind : {Item::Kind::Group, Item::Kind::Service, Item::Kind::Junction}) {
            for (size_t i = 0; i < items.size(); ++i) {
                Item& it = items[i];
                if (it.kind != kind) continue;
                if (ids.count(it.id)) fail(it.pos, Tr(L"The id is already in use: ", L"Идентификатор уже занят: ") + it.id);
                if (!it.in.empty()) {
                    const auto p = ids.find(it.in);
                    if (it.in == it.id) fail(it.pos, Tr(L"Placed within itself: ", L"Помещён сам в себя: ") + it.id);
                    if (p == ids.end()) {  // (all groups are known when services are read)
                        if (kind == Item::Kind::Group) fail(it.pos, Tr(L"No such group (declared before): ", L"Нет такой группы (объявленной раньше): ") + it.in);
                        fail(it.pos, Tr(L"No such group: ", L"Нет такой группы: ") + it.in);
                    }
                    if (items[static_cast<size_t>(p->second)].kind != Item::Kind::Group) fail(it.pos, Tr(L"Not a group: ", L"Это не группа: ") + it.in);
                    it.parent = p->second;
                    depth[i] = depth[static_cast<size_t>(it.parent)] + 1;
                    if (kind == Item::Kind::Group && depth[i] > kMaxNesting) fail(it.pos, Tr(L"Groups nested too deep", L"Группы вложены слишком глубоко"));
                }
                ids.emplace(it.id, static_cast<int>(i));
            }
        }
        auto node = [&](const std::wstring& id, size_t pos) -> int {
            const auto f = ids.find(id);
            if (f == ids.end()) FailAt(pos, Tr(L"No such service: ", L"Нет такого сервиса: ") + id);
            if (items[static_cast<size_t>(f->second)].kind == Item::Kind::Group) {
                FailAt(pos, Tr(L"Edges join services and junctions, not groups: ", L"Связи соединяют сервисы и соединения, а не группы: ") + id);
            }
            return f->second;
        };
        for (Edge& e : edges) {
            e.a = node(e.from, e.pos);
            e.b = node(e.to, e.pos);
            const int ga = items[static_cast<size_t>(e.a)].parent, gb = items[static_cast<size_t>(e.b)].parent;
            if ((e.from_group || e.to_group) && ga >= 0 && ga == gb) {
                fail(e.pos, Tr(L"{group} on an edge within one group", L"{group} у связи внутри одной группы"));
            }
        }
        for (Align& a : aligns) {
            std::unordered_set<std::wstring> seen;
            for (const std::wstring& m : a.members) {
                a.items.push_back(node(m, a.pos));
                if (!seen.insert(m).second) fail(a.pos, Tr(L"Aligned twice: ", L"Выровнен дважды: ") + m);
            }
        }
    }

    const Source& src_;
    Cursor c_;
    size_t members_ = 0;
};

// ---------------------------------------------------------------------------
// Layout and drawing
// ---------------------------------------------------------------------------

using M = SceneBuilder::Measured;

constexpr float kIcon = 80;       // Mermaid's iconSize
constexpr float kGap = 121;       // between icons on the grid (fcose's for an ideal edge of 1.5 icons)
constexpr float kPad = 42.5f;     // a group's border around what is in it
constexpr float kSep = 60;        // between a group's border and what is outside it
constexpr float kBoxLabel = 17;   // the one-line label a group makes room for under an icon
constexpr float kLabelGap = 5;    // an icon to its label
constexpr float kArrow = kIcon / 6;
constexpr float kMargin = 40;
constexpr size_t kMaxWork = 20000000;  // placing units: overlap checks

// Mermaid's own icons, `size` wide at (x, y): white lines on #087EBF. Any
// other name (an icon pack's) is the "?" Mermaid shows without that pack.
void DrawIcon(SceneBuilder& b, std::wstring_view name, float x, float y, float size) {
    if (name.starts_with(L"mermaid-architecture:")) name.remove_prefix(21);
    const float k = size / kIcon, line = 2 * k;
    auto p = [&](float px, float py) { return D2D1::Point2F(x + px * k, y + py * k); };
    auto r = [&](float l, float t, float rt, float bt) { return D2D1::RectF(x + l * k, y + t * k, x + rt * k, y + bt * k); };
    const DgColor blue = Argb(0xFF087EBF), white = Argb(0xFFFFFFFF), none = Role(kDgNone);
    b.Rect(r(0, 0, 80, 80), 0, blue, none);
    if (name == L"blank") return;
    if (name == L"database") {
        b.Ellipse(r(20, 15, 60, 29.28f), none, white, line);
        for (const float cy : {34.05f, 45.95f, 57.86f}) {
            b.Begin(p(20, cy));
            b.CubicTo(p(20, cy + 3.94f), p(28.95f, cy + 7.14f), p(40, cy + 7.14f));
            b.CubicTo(p(51.05f, cy + 7.14f), p(60, cy + 3.94f), p(60, cy));
            b.End(none, white, line);
        }
        b.Begin(p(20, 57.86f));
        b.LineTo(p(20, 22.14f));
        b.MoveTo(p(60, 57.86f));
        b.LineTo(p(60, 22.14f));
        b.End(none, white, line);
    } else if (name == L"server") {
        b.Rect(r(17.5f, 17.5f, 62.5f, 62.5f), 2 * k, none, white, line);
        b.Begin(p(17.5f, 32.5f));
        b.LineTo(p(62.5f, 32.5f));
        b.MoveTo(p(17.5f, 47.5f));
        b.LineTo(p(62.5f, 47.5f));
        b.End(none, white, line);
        for (const float cy : {25.f, 40.f, 55.f}) {
            b.Rect(r(43.25f, cy - 1, 56.75f, cy + 1), k, white, none);
            for (const float cx : {22.5f, 27.5f, 32.5f}) b.Ellipse(r(cx - 1.25f, cy - 1.25f, cx + 1.25f, cy + 1.25f), white, none);
        }
    } else if (name == L"disk") {
        b.Rect(r(20, 15, 60, 65), k, none, white, line);
        for (const float cx : {24.f, 56.f}) {
            for (const float cy : {19.17f, 60.83f}) b.Ellipse(r(cx - 1.8f, cy - 1.83f, cx + 1.8f, cy + 1.83f), white, none);
        }
        b.Ellipse(r(26, 19.17f, 54, 48.33f), none, white, line);
        b.Ellipse(r(35, 28.58f, 45, 38.92f), white, none);
        b.Begin(p(37.51f, 42.52f));
        b.LineTo(p(32.68f, 55.74f));
        b.CubicTo(p(32.42f, 56.45f), p(31.58f, 56.76f), p(30.92f, 56.38f));
        b.LineTo(p(26.74f, 53.96f));
        b.CubicTo(p(26.08f, 53.58f), p(25.93f, 52.70f), p(26.41f, 52.12f));
        b.LineTo(p(35.42f, 41.32f));
        b.CubicTo(p(36.30f, 40.27f), p(37.98f, 41.24f), p(37.51f, 42.52f));
        b.Close();
        b.End(white, none);
    } else if (name == L"internet") {
        b.Ellipse(r(17.5f, 17.5f, 62.5f, 62.5f), none, white, line);
        b.Begin(p(40, 17.5f));
        b.LineTo(p(40, 62.5f));
        b.MoveTo(p(17.5f, 40));
        b.LineTo(p(62.5f, 40));
        b.MoveTo(p(19.75f, 30.1f));
        b.LineTo(p(60.25f, 30.1f));
        b.MoveTo(p(19.75f, 49.9f));
        b.LineTo(p(60.25f, 49.9f));
        b.MoveTo(p(39.99f, 17.51f));
        b.CubicTo(p(24.71f, 28.61f), p(24.71f, 51.39f), p(39.99f, 62.49f));
        b.MoveTo(p(40.01f, 17.51f));
        b.CubicTo(p(55.29f, 28.61f), p(55.29f, 51.39f), p(40.01f, 62.49f));
        b.End(none, white, line);
    } else if (name == L"cloud") {
        b.Begin(p(65, 47.5f));
        b.CubicTo(p(65, 50.26f), p(62.76f, 52.5f), p(60, 52.5f));
        b.LineTo(p(20, 52.5f));
        b.CubicTo(p(17.24f, 52.5f), p(15, 50.26f), p(15, 47.5f));
        b.CubicTo(p(15, 45.63f), p(16.03f, 43.99f), p(17.56f, 43.14f));
        b.CubicTo(p(17.52f, 42.93f), p(17.5f, 42.72f), p(17.5f, 42.5f));
        b.CubicTo(p(17.5f, 39.9f), p(19.98f, 37.76f), p(23.15f, 37.53f));
        b.CubicTo(p(24.8f, 33.02f), p(29.49f, 29.77f), p(35, 29.77f));
        b.CubicTo(p(35.86f, 29.77f), p(36.69f, 29.85f), p(37.5f, 30));
        b.CubicTo(p(39.59f, 28.43f), p(42.19f, 27.5f), p(45, 27.5f));
        b.CubicTo(p(51.1f, 27.5f), p(56.19f, 31.88f), p(57.28f, 37.67f));
        b.CubicTo(p(59.42f, 38.23f), p(61, 40.18f), p(61, 42.5f));
        b.CubicTo(p(61, 42.53f), p(61, 42.57f), p(60.99f, 42.6f));
        b.CubicTo(p(63.28f, 43.06f), p(65, 45.08f), p(65, 47.5f));
        b.Close();
        b.End(none, white, line);
    } else {
        RichText q;
        q.text = L"?";
        const M m = b.Measure(q, 0, false, 67.75f * k / b.size());
        b.Text(m, D2D1::Point2F(x + 40 * k - m.w / 2, y + 40 * k - m.h * 0.55f), white);
    }
}

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b) : p_(p), b_(b), unit_(b.size()) {}

    std::shared_ptr<DiagramScene> Build() {
        Nodes();
        Grid();
        Place();
        DrawEdges();
        DrawNodes();
        DrawGroups();
        return b_.FinishFitted(kMargin);
    }

private:
    struct Node {
        int item = -1;
        bool placeholder = false;  // an empty group's place
        int col = 0, row = 0;
        float x = 0, y = 0;        // the icon's top left
        M label;                   // under the icon, wrapped
        float label_w = 0;         // unwrapped (what a group makes room for)
    };

    const Item& ItemOf(const Node& n) const { return p_.items[static_cast<size_t>(n.item)]; }
    int GroupOf(const Node& n) const { return n.placeholder ? n.item : ItemOf(n).parent; }
    float Scale() const { return 16 / unit_; }

    // Services, then junctions (Mermaid's order), then a place for each
    // empty group.
    void Nodes() {
        const size_t n = p_.items.size();
        node_of_.assign(n, -1);
        for (const Item::Kind kind : {Item::Kind::Service, Item::Kind::Junction}) {
            for (size_t i = 0; i < n; ++i) {
                if (p_.items[i].kind != kind) continue;
                node_of_[i] = static_cast<int>(nodes_.size());
                Node nd;
                nd.item = static_cast<int>(i);
                const Text& t = p_.items[i].title;
                if (kind == Item::Kind::Service && !t.raw.empty()) {
                    nd.label = b_.Measure(ParseLabel(t.raw, true), 1.5f * kIcon, false, Scale());
                    if (p_.items[i].parent >= 0) nd.label_w = b_.Measure(ParseLabel(t.raw, true), 0, false, Scale()).w;
                }
                nodes_.push_back(std::move(nd));
            }
        }
        std::vector<char> used(n, 0);
        for (const Node& nd : nodes_) {
            for (int g = GroupOf(nd); g >= 0 && !used[static_cast<size_t>(g)]; g = p_.items[static_cast<size_t>(g)].parent) used[static_cast<size_t>(g)] = 1;
        }
        // (Inner groups first: one holding only an empty group isn't empty.)
        std::vector<int> empty;
        for (size_t i = n; i-- > 0;) {
            if (p_.items[i].kind != Item::Kind::Group || used[i]) continue;
            empty.push_back(static_cast<int>(i));
            for (int g = static_cast<int>(i); g >= 0 && !used[static_cast<size_t>(g)]; g = p_.items[static_cast<size_t>(g)].parent) used[static_cast<size_t>(g)] = 1;
        }
        for (auto it = empty.rbegin(); it != empty.rend(); ++it) {
            Node nd;
            nd.item = *it;
            nd.placeholder = true;
            nodes_.push_back(std::move(nd));
        }
    }

    // A node or a group on its group's grid (or the diagram's); a group
    // takes the cells of its own grid, as a block.
    struct Unit {
        int w = 1, h = 1;
        int col = 0, row = 0;
        int first = 0;           // its first node (Mermaid's order)
        std::vector<int> nodes;  // the nodes in it
    };

    // An edge (or an align) from x's side d1 to y's side d2.
    struct Link {
        int x, y;
        wchar_t d1, d2;
    };

    // Places `b` (holding y) from `a` (holding x) joined x's side d1 to y's
    // side d2 (nodes' col and row: where they are in their unit): y's cell
    // by Mermaid's rule, except that a bend from a side to a top or bottom
    // goes where the bend leads (Mermaid puts those where its forces happen
    // to), then `b` clear of `a` along d1.
    void Follow(const Unit& a, int x, wchar_t d1, wchar_t d2, int y, Unit* b) const {
        const Node& nx = nodes_[static_cast<size_t>(x)];
        const Node& ny = nodes_[static_cast<size_t>(y)];
        int tc = a.col + nx.col, tr = a.row + nx.row;
        if (IsX(d1)) {
            tc += d1 == L'L' ? -1 : 1;
            if (!IsX(d2)) tr += d2 == L'T' ? 1 : -1;
        } else {
            tr += d1 == L'T' ? -1 : 1;
            if (IsX(d2)) tc += d2 == L'L' ? 1 : -1;
        }
        b->col = tc - ny.col;
        b->row = tr - ny.row;
        switch (d1) {
        case L'L': b->col = std::min(b->col, a.col - b->w); break;
        case L'R': b->col = std::max(b->col, a.col + a.w); break;
        case L'T': b->row = std::min(b->row, a.row - b->h); break;
        default: b->row = std::max(b->row, a.row + a.h); break;
        }
    }

    // Places on the grid, innermost groups first: in each group (and the
    // diagram) its nodes and groups as Mermaid's breadth-first walk over the
    // edges puts them, two on one place moved to the nearest free one, the
    // unconnected parts packed in rows. Groups so never overlap.
    void Grid() {
        const size_t items = p_.items.size(), n = nodes_.size();
        std::vector<int> depth(items, 0);
        for (size_t i = 0; i < items; ++i) {
            const int parent = p_.items[i].parent;
            if (parent >= 0) depth[i] = depth[static_cast<size_t>(parent)] + 1;  // (a group's parent comes before it)
        }
        auto depth_of = [&](int g) { return g < 0 ? -1 : depth[static_cast<size_t>(g)]; };
        auto up = [&](int g) { return p_.items[static_cast<size_t>(g)].parent; };
        std::vector<Unit> units;
        std::vector<int> group_unit(items, -1), node_unit(n, -1);
        for (size_t i = 0; i < items; ++i) {
            if (p_.items[i].kind != Item::Kind::Group) continue;
            group_unit[i] = static_cast<int>(units.size());
            units.emplace_back().first = std::numeric_limits<int>::max();
        }
        std::vector<std::vector<int>> children(items + 1);  // [group + 1], [0] the diagram's
        for (size_t v = 0; v < n; ++v) {
            node_unit[v] = static_cast<int>(units.size());
            Unit& u = units.emplace_back();
            u.first = static_cast<int>(v);
            u.nodes = {static_cast<int>(v)};
            const int g = GroupOf(nodes_[v]);
            children[static_cast<size_t>(g + 1)].push_back(node_unit[v]);
            for (int a = g; a >= 0; a = up(a)) {
                Unit& gu = units[static_cast<size_t>(group_unit[static_cast<size_t>(a)])];
                gu.nodes.push_back(static_cast<int>(v));
                gu.first = std::min(gu.first, static_cast<int>(v));
            }
            nodes_[v].col = nodes_[v].row = 0;
        }
        for (size_t i = 0; i < items; ++i) {
            if (group_unit[i] >= 0) children[static_cast<size_t>(p_.items[i].parent + 1)].push_back(group_unit[i]);
        }
        // Each edge (and align) belongs to the innermost group holding both ends.
        std::vector<std::vector<Link>> links(items + 1);
        auto link = [&](int x, wchar_t d1, wchar_t d2, int y) {
            if (x == y || d1 == d2) return;  // (LL, RR...: no place follows)
            int a = GroupOf(nodes_[static_cast<size_t>(x)]), b = GroupOf(nodes_[static_cast<size_t>(y)]);
            while (depth_of(a) > depth_of(b)) a = up(a);
            while (depth_of(b) > depth_of(a)) b = up(b);
            while (a != b) {
                a = up(a);
                b = up(b);
            }
            links[static_cast<size_t>(a + 1)].push_back({x, y, d1, d2});
        };
        for (const Edge& e : p_.edges) link(node_of_[static_cast<size_t>(e.a)], e.from_dir, e.to_dir, node_of_[static_cast<size_t>(e.b)]);
        for (const Align& al : p_.aligns) {
            for (size_t i = 0; i + 1 < al.items.size(); ++i) {
                link(node_of_[static_cast<size_t>(al.items[i])], al.row ? L'R' : L'B', al.row ? L'L' : L'T', node_of_[static_cast<size_t>(al.items[i + 1])]);
            }
        }
        // The unit holding node v on `level`'s grid.
        auto unit_at = [&](int level, int v) {
            int g = GroupOf(nodes_[static_cast<size_t>(v)]);
            if (g == level) return node_unit[static_cast<size_t>(v)];
            while (up(g) != level) g = up(g);
            return group_unit[static_cast<size_t>(g)];
        };
        std::vector<int> levels;
        for (size_t i = 0; i < items; ++i) {
            if (group_unit[i] >= 0) levels.push_back(static_cast<int>(i));
        }
        std::stable_sort(levels.begin(), levels.end(), [&](int a, int b) { return depth_of(a) > depth_of(b); });
        levels.push_back(-1);
        size_t work = 0;
        for (const int level : levels) {
            const std::vector<int>& kids = children[static_cast<size_t>(level + 1)];
            std::unordered_map<int, size_t> slot;
            for (size_t k = 0; k < kids.size(); ++k) slot[kids[k]] = k;
            const std::vector<Link>& ls = links[static_cast<size_t>(level + 1)];
            std::vector<std::pair<size_t, size_t>> ends(ls.size());
            std::vector<std::vector<std::pair<size_t, bool>>> adj(kids.size());  // (link, from its x)
            for (size_t l = 0; l < ls.size(); ++l) {
                ends[l] = {slot[unit_at(level, ls[l].x)], slot[unit_at(level, ls[l].y)]};
                adj[ends[l].first].push_back({l, true});
                adj[ends[l].second].push_back({l, false});
            }
            std::vector<size_t> order(kids.size());
            for (size_t k = 0; k < kids.size(); ++k) order[k] = k;
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return units[static_cast<size_t>(kids[a])].first < units[static_cast<size_t>(kids[b])].first;
            });
            std::vector<char> visited(kids.size(), 0), placed(kids.size(), 0);
            std::vector<std::vector<size_t>> parts;
            for (const size_t s : order) {
                if (visited[s]) continue;
                std::vector<size_t> part{s};
                placed[s] = 1;
                units[static_cast<size_t>(kids[s])].col = units[static_cast<size_t>(kids[s])].row = 0;
                std::deque<size_t> queue{s};
                while (!queue.empty()) {
                    const size_t v = queue.front();
                    queue.pop_front();
                    if (visited[v]) continue;
                    visited[v] = 1;
                    for (const auto& [l, forward] : adj[v]) {
                        const Link& k = ls[l];
                        const size_t w = forward ? ends[l].second : ends[l].first;
                        if (visited[w]) continue;
                        if (!placed[w]) {
                            placed[w] = 1;
                            part.push_back(w);
                        }
                        const Unit& from = units[static_cast<size_t>(kids[v])];
                        Unit* to = &units[static_cast<size_t>(kids[w])];
                        if (forward) Follow(from, k.x, k.d1, k.d2, k.y, to);
                        else Follow(from, k.y, k.d2, k.d1, k.x, to);
                        queue.push_back(w);
                    }
                }
                parts.push_back(std::move(part));
            }
            // Each part: units on one another apart (to the nearest free
            // place, ring by ring: below, right, above, left before the
            // corners), then from 0, 0.
            struct Size {
                int w, h;
            };
            std::vector<Size> sizes;
            size_t cells = 0;
            int widest = 1;
            for (const std::vector<size_t>& part : parts) {
                struct Box {
                    int c0, r0, c1, r1;
                };
                std::vector<Box> taken;
                std::vector<size_t> big;              // those of taken bigger than a cell
                std::unordered_set<uint64_t> occupied;  // the cells of the others
                auto cell = [](int c, int r) { return (static_cast<uint64_t>(static_cast<uint32_t>(c)) << 32) | static_cast<uint32_t>(r); };
                std::vector<std::pair<int, int>> ring;
                for (const size_t v : part) {
                    Unit& u = units[static_cast<size_t>(kids[v])];
                    auto free_at = [&](int c, int r) {
                        for (const size_t b : big) {
                            const Box& o = taken[b];
                            if (++work > kMaxWork) TooLarge();
                            if (c < o.c1 && c + u.w > o.c0 && r < o.r1 && r + u.h > o.r0) return false;
                        }
                        for (int dc = 0; dc < u.w; ++dc) {
                            for (int dr = 0; dr < u.h; ++dr) {
                                if (++work > kMaxWork) TooLarge();
                                if (occupied.count(cell(c + dc, r + dr))) return false;
                            }
                        }
                        return true;
                    };
                    for (int d = 1; !free_at(u.col, u.row); ++d) {
                        ring.clear();
                        for (int k = 0; k < 8 * d; ++k) {
                            const int side = k / (2 * d), t = k % (2 * d) - d;
                            ring.push_back({side == 0 ? d : side == 1 ? -t : side == 2 ? -d : t, side == 0 ? t : side == 1 ? d : side == 2 ? -t : -d});
                        }
                        auto rank = [](const std::pair<int, int>& o) {
                            const int dir = o.second > 0 ? 0 : o.first > 0 ? 1 : o.second < 0 ? 2 : 3;
                            return (std::abs(o.first) + std::abs(o.second)) * 4 + dir;
                        };
                        std::stable_sort(ring.begin(), ring.end(), [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
                        for (const auto& [dc, dr] : ring) {
                            if (free_at(u.col + dc, u.row + dr)) {
                                u.col += dc;
                                u.row += dr;
                                break;
                            }
                        }
                    }
                    taken.push_back({u.col, u.row, u.col + u.w, u.row + u.h});
                    if (u.w == 1 && u.h == 1) occupied.insert(cell(u.col, u.row));
                    else big.push_back(taken.size() - 1);
                }
                int c0 = std::numeric_limits<int>::max(), r0 = c0, c1 = std::numeric_limits<int>::min(), r1 = c1;
                for (const Box& o : taken) {
                    c0 = std::min(c0, o.c0);
                    r0 = std::min(r0, o.r0);
                    c1 = std::max(c1, o.c1);
                    r1 = std::max(r1, o.r1);
                }
                for (const size_t v : part) {
                    units[static_cast<size_t>(kids[v])].col -= c0;
                    units[static_cast<size_t>(kids[v])].row -= r0;
                }
                sizes.push_back({c1 - c0, r1 - r0});
                cells += static_cast<size_t>(sizes.back().w) * static_cast<size_t>(sizes.back().h);
                widest = std::max(widest, sizes.back().w);
            }
            // The parts in rows about as wide as the whole is high.
            const int target = std::max(widest, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(cells)))));
            int x = 0, y = 0, shelf = 0, w = 0, h = 0;
            for (size_t k = 0; k < parts.size(); ++k) {
                if (x > 0 && x + sizes[k].w > target) {
                    x = 0;
                    y += shelf;
                    shelf = 0;
                }
                for (const size_t v : parts[k]) {
                    Unit& u = units[static_cast<size_t>(kids[v])];
                    u.col += x;
                    u.row += y;
                    w = std::max(w, u.col + u.w);
                    h = std::max(h, u.row + u.h);
                    for (const int node : u.nodes) {
                        nodes_[static_cast<size_t>(node)].col += u.col;
                        nodes_[static_cast<size_t>(node)].row += u.row;
                    }
                }
                x += sizes[k].w;
                shelf = std::max(shelf, sizes[k].h);
            }
            if (level >= 0) {
                units[static_cast<size_t>(group_unit[static_cast<size_t>(level)])].w = std::max(w, 1);
                units[static_cast<size_t>(group_unit[static_cast<size_t>(level)])].h = std::max(h, 1);
            }
        }
        for (const Node& nd : nodes_) {
            cols_ = std::max(cols_, nd.col + 1);
            rows_ = std::max(rows_, nd.row + 1);
        }
        // Each group's columns and rows (what is in it, at any depth).
        const int most = std::numeric_limits<int>::max(), least = std::numeric_limits<int>::min();
        span_.assign(items, {most, most, least, least});
        for (const Node& nd : nodes_) {
            for (int g = GroupOf(nd); g >= 0; g = p_.items[static_cast<size_t>(g)].parent) {
                Span& s = span_[static_cast<size_t>(g)];
                s.c0 = std::min(s.c0, nd.col);
                s.r0 = std::min(s.r0, nd.row);
                s.c1 = std::max(s.c1, nd.col);
                s.r1 = std::max(s.r1, nd.row);
            }
        }
    }

    // Columns and rows to coordinates: the gaps widened where groups' borders
    // fall between them.
    void Place() {
        std::vector<float> gap_x(static_cast<size_t>(cols_), kGap), gap_y(static_cast<size_t>(rows_), kGap);
        Position(gap_x, gap_y);
        std::vector<float> right(gap_x.size(), 0), left(gap_x.size(), 0), bottom(gap_y.size(), 0), top(gap_y.size(), 0);
        for (size_t g = 0; g < p_.items.size(); ++g) {
            const Span& s = span_[g];
            if (p_.items[g].kind != Item::Kind::Group || s.c0 > s.c1) continue;
            const D2D1_RECT_F& r = rect_[g];
            right[static_cast<size_t>(s.c1)] = std::max(right[static_cast<size_t>(s.c1)], r.right - (col_x_[static_cast<size_t>(s.c1)] + kIcon));
            left[static_cast<size_t>(s.c0)] = std::max(left[static_cast<size_t>(s.c0)], col_x_[static_cast<size_t>(s.c0)] - r.left);
            bottom[static_cast<size_t>(s.r1)] = std::max(bottom[static_cast<size_t>(s.r1)], r.bottom - (row_y_[static_cast<size_t>(s.r1)] + kIcon));
            top[static_cast<size_t>(s.r0)] = std::max(top[static_cast<size_t>(s.r0)], row_y_[static_cast<size_t>(s.r0)] - r.top);
        }
        for (size_t c = 0; c + 1 < gap_x.size(); ++c) {
            if (right[c] > 0 || left[c + 1] > 0) gap_x[c] = std::max(kGap, right[c] + left[c + 1] + kSep);
        }
        for (size_t r = 0; r + 1 < gap_y.size(); ++r) {
            if (bottom[r] > 0 || top[r + 1] > 0) gap_y[r] = std::max(kGap, bottom[r] + top[r + 1] + kSep);
        }
        Position(gap_x, gap_y);
    }

    void Position(const std::vector<float>& gap_x, const std::vector<float>& gap_y) {
        col_x_.assign(gap_x.size(), 0);
        row_y_.assign(gap_y.size(), 0);
        for (size_t c = 1; c < gap_x.size(); ++c) col_x_[c] = col_x_[c - 1] + kIcon + gap_x[c - 1];
        for (size_t r = 1; r < gap_y.size(); ++r) row_y_[r] = row_y_[r - 1] + kIcon + gap_y[r - 1];
        for (Node& nd : nodes_) {
            nd.x = col_x_[static_cast<size_t>(nd.col)];
            nd.y = row_y_[static_cast<size_t>(nd.row)];
        }
        // Groups' boxes, the innermost first: what is in them (icons and
        // their one-line labels, as Mermaid's cytoscape measures them), then
        // the border around.
        const size_t items = p_.items.size();
        rect_.assign(items, D2D1::RectF(1e30f, 1e30f, -1e30f, -1e30f));
        auto grow = [](D2D1_RECT_F* r, const D2D1_RECT_F& o) {
            r->left = std::min(r->left, o.left);
            r->top = std::min(r->top, o.top);
            r->right = std::max(r->right, o.right);
            r->bottom = std::max(r->bottom, o.bottom);
        };
        for (const Node& nd : nodes_) {
            const int g = GroupOf(nd);
            if (g < 0) continue;
            // (An empty group's place: cytoscape's 30 wide node for it.)
            const float in = nd.placeholder ? (kIcon - 30) / 2 : 0.f;
            D2D1_RECT_F box = D2D1::RectF(nd.x + in, nd.y + in, nd.x + kIcon - in, nd.y + kIcon - in);
            if (nd.label.layout) grow(&box, D2D1::RectF(nd.x + kIcon / 2 - nd.label_w / 2, nd.y, nd.x + kIcon / 2 + nd.label_w / 2, nd.y + kIcon + kBoxLabel));
            grow(&rect_[static_cast<size_t>(g)], box);
        }
        std::vector<int> depth(items, 0), order;
        for (size_t i = 0; i < items; ++i) {
            if (p_.items[i].kind != Item::Kind::Group) continue;
            const int parent = p_.items[i].parent;
            depth[i] = parent >= 0 ? depth[static_cast<size_t>(parent)] + 1 : 0;  // (parents come first)
            order.push_back(static_cast<int>(i));
        }
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return depth[static_cast<size_t>(a)] > depth[static_cast<size_t>(b)]; });
        for (const int g : order) {
            D2D1_RECT_F& r = rect_[static_cast<size_t>(g)];
            if (r.left > r.right) continue;
            r = D2D1::RectF(r.left - kPad, r.top - kPad, r.right + kPad, r.bottom + kPad);
            const int parent = p_.items[static_cast<size_t>(g)].parent;
            if (parent >= 0) grow(&rect_[static_cast<size_t>(parent)], D2D1::RectF(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1));
        }
    }

    // Where an edge meets a side (the icon's middle there); a junction's
    // edges meet in its middle.
    D2D1_POINT_2F SidePoint(const Node& nd, wchar_t d) const {
        switch (d) {
        case L'L': return D2D1::Point2F(nd.x, nd.y + kIcon / 2);
        case L'R': return D2D1::Point2F(nd.x + kIcon, nd.y + kIcon / 2);
        case L'T': return D2D1::Point2F(nd.x + kIcon / 2, nd.y);
        default: return D2D1::Point2F(nd.x + kIcon / 2, nd.y + kIcon);
        }
    }

    D2D1_POINT_2F EndPoint(const Node& nd, wchar_t d, bool group) const {
        D2D1_POINT_2F p = SidePoint(nd, d);
        if (group) {  // from the group's border (Mermaid's padding + 4 out)
            if (d == L'L') p.x -= 44;
            else if (d == L'R') p.x += 44;
            else if (d == L'T') p.y -= 44;
            else p.y += 44 + 18;
        } else if (ItemOf(nd).kind == Item::Kind::Junction) {
            p = D2D1::Point2F(nd.x + kIcon / 2, nd.y + kIcon / 2);
        }
        return p;
    }

    // An arrowhead at `p` on side `d`, pointing into it (Mermaid's polygons).
    void Arrow(D2D1_POINT_2F p, wchar_t d) {
        const float s = kArrow;
        D2D1_POINT_2F o;  // the polygon's origin
        std::vector<D2D1_POINT_2F> pts;
        switch (d) {
        case L'T':
            o = D2D1::Point2F(p.x - s / 2, p.y - s + 2);
            pts = {{0, 0}, {s, 0}, {s / 2, s}};
            break;
        case L'B':
            o = D2D1::Point2F(p.x - s / 2, p.y - 2);
            pts = {{s / 2, 0}, {s, s}, {0, s}};
            break;
        case L'L':
            o = D2D1::Point2F(p.x - s + 2, p.y - s / 2);
            pts = {{s, s / 2}, {0, s}, {0, 0}};
            break;
        default:
            o = D2D1::Point2F(p.x - 2, p.y - s / 2);
            pts = {{0, s / 2}, {s, 0}, {s, s}};
            break;
        }
        for (D2D1_POINT_2F& q : pts) q = D2D1::Point2F(q.x + o.x, q.y + o.y);
        b_.Polygon(pts, Role(kDgLine), Role(kDgNone));
    }

    void DrawEdges() {
        const DgColor line = Role(kDgLine), ink = Role(kDgText);
        for (const Edge& e : p_.edges) {
            const Node& a = nodes_[static_cast<size_t>(node_of_[static_cast<size_t>(e.a)])];
            const Node& z = nodes_[static_cast<size_t>(node_of_[static_cast<size_t>(e.b)])];
            const D2D1_POINT_2F s0 = SidePoint(a, e.from_dir), t0 = SidePoint(z, e.to_dir);
            const D2D1_POINT_2F start = EndPoint(a, e.from_dir, e.from_group), end = EndPoint(z, e.to_dir, e.to_group);
            // A bend where a side meets a top or bottom; else straight.
            const bool bend = IsX(e.from_dir) != IsX(e.to_dir);
            const D2D1_POINT_2F mid = bend ? (IsX(e.from_dir) ? D2D1::Point2F(t0.x, s0.y) : D2D1::Point2F(s0.x, t0.y))
                                           : D2D1::Point2F((s0.x + t0.x) / 2, (s0.y + t0.y) / 2);
            b_.Begin(start);
            b_.LineTo(mid);
            b_.LineTo(end);
            b_.End(Role(kDgNone), line, 3.f);
            if (e.from_arrow) Arrow(start, e.from_dir);
            if (e.to_arrow) Arrow(end, e.to_dir);
            if (e.title.raw.empty()) continue;
            // The label: under a line across, beside one up, turned along a bend.
            const RichText label = ParseLabel(e.title.raw, true);
            if (!bend && IsX(e.from_dir)) {
                const M m = b_.Measure(label, std::max(std::fabs(start.x - end.x), kIcon), false, Scale());
                b_.Text(m, D2D1::Point2F(mid.x - m.w / 2, mid.y + kLabelGap), ink, e.title.pos, e.title.len);
            } else if (!bend) {
                const M m = b_.Measure(label, std::max(std::fabs(start.y - end.y) / 1.5f, kIcon), false, Scale());
                b_.TextTurned(m, D2D1::Point2F(mid.x + kLabelGap, mid.y + m.w / 2), -90, ink, e.title.pos, e.title.len);
            } else {
                const M m = b_.Measure(label, std::max(std::fabs(start.x - end.x) / 2, kIcon), false, Scale());
                // Mermaid's factors for the pair of sides, the text turned by
                // 45 degrees into the bend's outer corner.
                const std::wstring pair{e.from_dir, e.to_dir};
                float fx = -1, fy = 1;
                if (pair == L"LT" || pair == L"TL") {
                    fx = 1;
                } else if (pair == L"BL" || pair == L"LB") {
                    fx = 1;
                    fy = -1;
                } else if (pair == L"BR" || pair == L"RB") {
                    fy = -1;
                }
                const float angle = -fx * fy * 45, rad = angle * 3.14159265f / 180, c = std::cos(rad), sn = std::sin(rad);
                const float bound = (m.w + m.h) * 0.70710678f;
                const D2D1_POINT_2F center = D2D1::Point2F(mid.x + fx * bound / 2, mid.y + fy * bound / 2);
                const float u = -m.w / 2, v = -m.h / 2;
                b_.TextTurned(m, D2D1::Point2F(center.x + u * c - v * sn, center.y + u * sn + v * c), angle, ink, e.title.pos, e.title.len);
            }
        }
    }

    void DrawNodes() {
        const DgColor ink = Role(kDgText);
        for (const Node& nd : nodes_) {
            if (nd.placeholder) continue;
            const Item& it = ItemOf(nd);
            if (it.kind != Item::Kind::Service) continue;  // (a junction isn't drawn)
            if (nd.label.layout) {
                b_.Text(nd.label, D2D1::Point2F(nd.x + kIcon / 2 - nd.label.w / 2, nd.y + kIcon + kLabelGap), ink, it.title.pos, it.title.len);
            }
            if (!it.icon.empty()) {
                DrawIcon(b_, it.icon, nd.x, nd.y, kIcon);
            } else if (!it.icon_text.raw.empty()) {
                DrawIcon(b_, L"blank", nd.x, nd.y, kIcon);
                const M m = IconText(it.icon_text.raw);
                b_.Text(m, D2D1::Point2F(nd.x + kIcon / 2 - m.w / 2, nd.y + kIcon / 2 - m.h / 2), Argb(0xFFFFFFFF), it.icon_text.pos, it.icon_text.len);
            } else {
                // No icon: a dashed outline, rounded at the top.
                b_.Begin(D2D1::Point2F(nd.x, nd.y + kIcon));
                b_.LineTo(D2D1::Point2F(nd.x, nd.y + 5));
                b_.CubicTo(D2D1::Point2F(nd.x, nd.y + 1.67f), D2D1::Point2F(nd.x + 1.67f, nd.y), D2D1::Point2F(nd.x + 5, nd.y));
                b_.LineTo(D2D1::Point2F(nd.x + kIcon - 5, nd.y));
                b_.CubicTo(D2D1::Point2F(nd.x + kIcon - 1.67f, nd.y), D2D1::Point2F(nd.x + kIcon, nd.y + 1.67f), D2D1::Point2F(nd.x + kIcon, nd.y + 5));
                b_.LineTo(D2D1::Point2F(nd.x + kIcon, nd.y + kIcon));
                b_.Close();
                b_.End(Role(kDgNone), Role(kDgPrimaryBorder), 2.f, 1);
            }
        }
    }

    // Text in place of an icon: wrapped in it, at most four lines (then "…").
    M IconText(const std::wstring& raw) {
        // Lines as HTML sets them (1.2 of the 16px font), so four fit.
        constexpr float kLine = 16 * 1.2f;
        auto measure = [&](const std::wstring& s) {
            M m = b_.Measure(ParseLabel(s, false), kIcon - 2, false, Scale());
            if (m.layout) {
                m.layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, kLine, kLine * 0.8f);
                DWRITE_TEXT_METRICS tm{};
                if (SUCCEEDED(m.layout->GetMetrics(&tm))) m.h = tm.height;
            }
            return m;
        };
        const float most = kLine * 4 + 1;
        M m = measure(raw);
        if (m.h <= most) return m;
        size_t lo = 0, hi = raw.size();
        while (lo + 1 < hi) {
            const size_t mid = (lo + hi) / 2;
            if (measure(raw.substr(0, mid) + L"\u2026").h <= most) lo = mid;
            else hi = mid;
        }
        return measure(raw.substr(0, lo) + L"\u2026");
    }

    void DrawGroups() {
        const DgColor ink = Role(kDgText);
        for (size_t g = 0; g < p_.items.size(); ++g) {
            const Item& it = p_.items[g];
            if (it.kind != Item::Kind::Group) continue;
            const D2D1_RECT_F& r = rect_[g];
            if (r.left > r.right) continue;
            b_.Rect(r, 0, Role(kDgNone), Role(kDgPrimaryBorder), 2.f, 1);
            // Its icon in the corner, its title beside.
            const bool icon = !it.icon.empty();
            if (icon) DrawIcon(b_, it.icon, r.left + 1, r.top + 1, 0.75f * 40);
            if (!it.title.raw.empty()) {
                const M m = b_.Measure(ParseLabel(it.title.raw, true), r.right - r.left, false, Scale(), false);
                b_.Text(m, D2D1::Point2F(r.left + (icon ? 34.f : 4.f), r.top + (icon ? 7.f : 2.f)), ink, it.title.pos, it.title.len);
            }
        }
    }

    struct Span {
        int c0, r0, c1, r1;
    };

    const Parser& p_;
    SceneBuilder& b_;
    float unit_;
    std::vector<Node> nodes_;
    std::vector<int> node_of_;  // per item: its node, or -1
    int cols_ = 0, rows_ = 0;
    std::vector<Span> span_;    // per group
    std::vector<D2D1_RECT_F> rect_;
    std::vector<float> col_x_, row_y_;
};

}  // namespace

std::shared_ptr<DiagramScene> ArchitectureDiagram(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder).Build();
}

}  // namespace mdv::mermaid
