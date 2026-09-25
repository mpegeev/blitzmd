// Mermaid tree views (treeView-beta): a file tree by indentation (or drawn
// with box characters: ├── └── │), a row per node with lines to its
// parent, directories bold, icons when asked for, descriptions (## text) in
// a column, highlighted rows; laid out as Mermaid's renderer does. (Its text
// and lines are black in every theme; here the theme's.) And the info
// diagram: the Mermaid version these diagrams follow.
#include <algorithm>
#include <cmath>
#include <cwctype>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct TreeNode {
    std::wstring name;
    uint32_t pos = 0, len = 0;
    bool directory = false;
    std::wstring icon;          // icon(name); "none": none
    bool has_icon = false;
    bool highlight = false;     // :::highlight
    std::wstring description;   // ## text
    uint32_t desc_pos = 0, desc_len = 0;
    int level = 0;
    std::vector<int> children;
};

bool IsBoxChar(wchar_t c) {
    return c == L'─' || c == L'━' || c == L'│' || c == L'┃' || c == L'└' || c == L'┗' || c == L'├' || c == L'┣';
}
bool IsBranchChar(wchar_t c) { return c == L'└' || c == L'┗' || c == L'├' || c == L'┣'; }

class TreeParser {
public:
    explicit TreeParser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"treeView-beta")) c_.Fail(Tr(L"Expected \"treeView-beta\"", L"Ожидалось «treeView-beta»"));
        c_.SkipBlanks();
        if (c_.Peek() != L'\n' && c_.Peek() != L'\r' && c_.Peek() != L'\0' && !c_.Rest().starts_with(L"%%")) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        c_.SkipToLineEnd();
        nodes.push_back(TreeNode{L"/", 0, 0, true});  // the root, as Mermaid draws it
        nodes[0].level = -1;
        // The lines (the box-drawing form, if any has its characters: each
        // node's depth from its branch's column).
        std::vector<std::pair<size_t, size_t>> lines;  // start, end
        for (size_t p = c_.pos(); p < src_.text.size();) {
            size_t e = src_.text.find(L'\n', p);
            if (e == std::wstring_view::npos) e = src_.text.size();
            lines.push_back({p, e});
            p = e + 1;
        }
        bool boxes = false;
        size_t segment = 0;
        for (const auto& [s, e] : lines) {
            const std::wstring_view line = src_.text.substr(s, e - s);
            if (Trim(line).empty() || Trim(line).starts_with(L"%%") || Meta(line) || Only(line, false)) continue;
            for (size_t k = 0; k < line.size(); ++k) {
                if (!IsBoxChar(line[k])) continue;
                boxes = true;
                if (!IsBranchChar(line[k])) continue;
                if (k > 0 && segment == 0) segment = Column(line, k);
                break;
            }
        }
        if (segment == 0) segment = 4;
        std::vector<size_t> stack{0};
        bool description = false;  // in a multi-line accDescr { }
        for (const auto& [s, e] : lines) {
            std::wstring_view line = src_.text.substr(s, e - s);
            if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
            if (description) {
                description = line.find(L'}') == std::wstring_view::npos;
                continue;
            }
            if (Trim(line).empty() || Trim(line).starts_with(L"%%")) continue;
            if (Meta(line)) {
                if (nodes.size() > 1) Fail(s, Tr(L"A title or accessibility line after the nodes", L"Заголовок или описание после узлов"));
                const std::wstring_view t = Trim(line);
                description = t.starts_with(L"accDescr") && t.find(L'{') != std::wstring_view::npos && t.find(L'}') == std::wstring_view::npos;
                continue;
            }
            int level = 0;
            size_t from = 0;
            if (boxes) {
                // Mermaid's box-drawing form: a node's depth from its branch,
                // lines of bars (or box characters only) left out.
                if (Only(line, false)) continue;
                size_t branch = line.size();
                for (size_t k = 0; k < line.size(); ++k) {
                    if (IsBranchChar(line[k])) {
                        branch = k;
                        break;
                    }
                }
                if (branch < line.size()) {
                    level = 4 * (static_cast<int>(std::lround(static_cast<double>(Column(line, branch)) / static_cast<double>(segment))) + 1);
                    from = branch + 1;
                    while (from < line.size() && (line[from] == L'─' || line[from] == L'━')) ++from;
                    while (from < line.size() && line[from] == L' ') ++from;
                    if (from >= line.size()) Fail(s, Tr(L"Empty node after the branch", L"Пустой узел после ветки"));
                } else if (Only(line, true)) {
                    continue;
                } else if (std::none_of(line.begin(), line.end(), IsBoxChar) && IsSpace(line[0])) {
                    Fail(s, Tr(L"Indented without box characters (├── or └──)", L"Отступ без символов рамки (├── или └──)"));
                } else {
                    while (from < line.size() && (line[from] == L' ' || line[from] == L'\t')) ++from;
                    level = static_cast<int>(from);
                }
            } else {
                while (from < line.size() && (line[from] == L' ' || line[from] == L'\t')) ++from;
                level = static_cast<int>(from);  // (a tab counts one, as in Mermaid)
            }
            TreeNode n = Node(line, from, s);
            n.level = level;
            while (level <= nodes[stack.back()].level) stack.pop_back();
            if (nodes.size() > kMaxNodes) Fail(s, Tr(L"Too many nodes", L"Слишком много узлов"));
            nodes[stack.back()].children.push_back(static_cast<int>(nodes.size()));
            stack.push_back(nodes.size());
            nodes.push_back(std::move(n));
        }
    }

    std::vector<TreeNode> nodes;

private:
    // A title, accTitle: or accDescr line (Mermaid's metadata; not drawn).
    static bool Meta(std::wstring_view line) {
        const std::wstring_view t = Trim(line);
        auto word = [&](std::wstring_view w) { return t.starts_with(w) && (t.size() == w.size() || !IsWordChar(t[w.size()])); };
        return (word(L"title") && (t.size() == 5 || t[5] == L' ' || t[5] == L'\t')) || word(L"accTitle") || word(L"accDescr");
    }

    // A line of blanks and bars only (`any_box`: of any box characters).
    static bool Only(std::wstring_view line, bool any_box) {
        bool some = false;
        for (wchar_t ch : line) {
            if (ch == L' ' || ch == L'\t' || ch == L'\r') continue;
            if (any_box ? !IsBoxChar(ch) : ch != L'│' && ch != L'┃') return false;
            some = true;
        }
        return some;
    }

    // A column with tabs as four spaces (the box-drawing form's).
    static size_t Column(std::wstring_view line, size_t k) {
        size_t col = 0;
        for (size_t i = 0; i < k; ++i) col += line[i] == L'\t' ? 4 : 1;
        return col;
    }

    // "name" or 'name' or a bare name, then :::class, icon(x), ## text.
    TreeNode Node(std::wstring_view line, size_t from, size_t base) {
        TreeNode n;
        size_t k = from;
        std::wstring_view name;
        if (k < line.size() && (line[k] == L'"' || line[k] == L'\'')) {
            const size_t q = line.find(line[k], k + 1);
            if (q == std::wstring_view::npos) Fail(base + k, Tr(L"Unclosed quote", L"Не закрыта кавычка"));
            name = line.substr(k + 1, q - k - 1);
            k = q + 1;
        } else {
            size_t e = k;
            while (e < line.size() && !Annotation(line, e)) ++e;
            name = line.substr(k, e - k);
            while (!name.empty() && (name.back() == L' ' || name.back() == L'\t')) name.remove_suffix(1);
            k = e;
        }
        if (name.size() > kMaxLabel) Fail(base, Tr(L"Text too long", L"Слишком длинный текст"));
        if (!name.empty() && name.back() == L'/') {
            n.directory = true;
            name.remove_suffix(1);
        }
        n.name = std::wstring(name);
        n.pos = static_cast<uint32_t>(base + static_cast<size_t>(name.data() - line.data()));
        n.len = static_cast<uint32_t>(name.size());
        // Annotations, any order.
        while (k < line.size()) {
            size_t a = k;
            while (a < line.size() && (line[a] == L' ' || line[a] == L'\t')) ++a;
            if (a >= line.size()) break;
            const std::wstring_view rest = line.substr(a);
            if (rest.starts_with(L":::")) {
                size_t e = a + 3;
                while (e < line.size() && (line[e] == L' ' || line[e] == L'\t')) ++e;
                const size_t s = e;
                while (e < line.size() && (IsWordChar(line[e]) || line[e] == L'-')) ++e;
                n.highlight = line.substr(s, e - s) == L"highlight";  // (the last class counts)
                k = e;
            } else if (rest.starts_with(L"icon(")) {
                const size_t close = line.find(L')', a);
                if (close == std::wstring_view::npos) Fail(base + a, Tr(L"Expected ')'", L"Ожидалась «)»"));
                n.icon = std::wstring(line.substr(a + 5, close - a - 5));
                {
                    // [\w-]*(:[\w-]+)?
                    const size_t colon = n.icon.find(L':');
                    auto word = [](std::wstring_view w) { return std::all_of(w.begin(), w.end(), [](wchar_t ch) { return IsWordChar(ch) || ch == L'-'; }); };
                    const std::wstring_view ic = n.icon;
                    if (!(colon == std::wstring::npos ? word(ic) : word(ic.substr(0, colon)) && colon + 1 < ic.size() && word(ic.substr(colon + 1)))) {
                        Fail(base + a, Tr(L"Unexpected text", L"Непонятный текст"));
                    }
                }
                n.has_icon = true;
                if (n.icon.empty()) n.icon = L"none";
                k = close + 1;
            } else if (rest.starts_with(L"##")) {
                const std::wstring_view d = Trim(line.substr(a + 2));
                if (d.size() > kMaxLabel) Fail(base, Tr(L"Text too long", L"Слишком длинный текст"));
                n.description = std::wstring(d);
                n.desc_pos = static_cast<uint32_t>(base + static_cast<size_t>(d.data() - line.data()));
                n.desc_len = static_cast<uint32_t>(d.size());
                k = line.size();
            } else {
                Fail(base + a, Tr(L"Unexpected text", L"Непонятный текст"));
            }
        }
        return n;
    }

    // Whether an annotation (blanks, then :::x, icon( or ##) starts at k.
    static bool Annotation(std::wstring_view line, size_t k) {
        if (line[k] != L' ' && line[k] != L'\t') return false;
        size_t a = k;
        while (a < line.size() && (line[a] == L' ' || line[a] == L'\t')) ++a;
        const std::wstring_view rest = line.substr(a);
        if (rest.starts_with(L":::")) {
            size_t e = a + 3;
            while (e < line.size() && (line[e] == L' ' || line[e] == L'\t')) ++e;
            return e < line.size() && (std::iswalpha(line[e]) || line[e] == L'_');
        }
        return rest.starts_with(L"icon(") || rest.starts_with(L"##");
    }

    [[noreturn]] void Fail(size_t pos, std::wstring message) {
        c_.set_pos(pos);
        c_.Fail(std::move(message));
    }

    const Source& src_;
    Cursor c_;
};

using M = SceneBuilder::Measured;

constexpr float kRowIndent = 10, kPadX = 5, kPadY = 5;
constexpr float kTextH = 18.5f;            // a 16px label's box in the browser
constexpr float kIconSize = 14, kIconGap = 4, kDescGap = 16;

// The tree view's icons at (x, y), 14 wide: a folder, a file, or the "?"
// Mermaid shows for an icon it doesn't have.
void TreeIcon(SceneBuilder& b, std::wstring_view name, float x, float y) {
    const float k = kIconSize / 24;
    auto p = [&](float px, float py) { return D2D1::Point2F(x + px * k, y + py * k); };
    const DgColor ink = Argb(0xFF546E7A), none = Role(kDgNone);
    constexpr float c = 2 * 0.5523f;  // a corner of radius 2
    if (name.starts_with(L"mermaid-treeview:")) name.remove_prefix(17);
    if (name == L"folder") {
        b.Begin(p(10.59f, 4.59f));
        b.LineTo(p(9.17f, 4));
        b.LineTo(p(4, 4));
        b.CubicTo(p(4 - c, 4), p(2, 6 - c), p(2, 6));
        b.LineTo(p(2, 18));
        b.CubicTo(p(2, 18 + c), p(4 - c, 20), p(4, 20));
        b.LineTo(p(20, 20));
        b.CubicTo(p(20 + c, 20), p(22, 18 + c), p(22, 18));
        b.LineTo(p(22, 8));
        b.CubicTo(p(22, 8 - c), p(20 + c, 6), p(20, 6));
        b.LineTo(p(12.83f, 6));
        b.Close();
        b.End(ink, none);
    } else if (name == L"file") {
        // The page with its corner folded (the fold cut out).
        b.Begin(p(6, 2));
        b.CubicTo(p(6 - c, 2), p(4, 4 - c), p(4, 4));
        b.LineTo(p(4, 20));
        b.CubicTo(p(4, 20 + c), p(6 - c, 22), p(6, 22));
        b.LineTo(p(18, 22));
        b.CubicTo(p(18 + c, 22), p(20, 20 + c), p(20, 20));
        b.LineTo(p(20, 8.83f));
        b.CubicTo(p(20, 8.3f), p(19.79f, 7.79f), p(19.41f, 7.41f));
        b.LineTo(p(14.59f, 2.59f));
        b.CubicTo(p(14.21f, 2.21f), p(13.7f, 2), p(13.17f, 2));
        b.Close();
        b.MoveTo(p(13.5f, 3.9f));
        b.LineTo(p(18.1f, 8.5f));
        b.LineTo(p(14.5f, 8.5f));
        b.CubicTo(p(13.95f, 8.5f), p(13.5f, 8.05f), p(13.5f, 7.5f));
        b.Close();
        b.End(ink, none);
    } else {
        b.Rect(D2D1::RectF(x, y, x + kIconSize, y + kIconSize), 0, Argb(0xFF087EBF), none);
        RichText q;
        q.text = L"?";
        const M m = b.Measure(q, 0, true, kIconSize * 0.85f / b.size());
        b.Text(m, D2D1::Point2F(x + kIconSize / 2 - m.w / 2, y + kIconSize / 2 - m.h * 0.55f), Argb(0xFFFFFFFF));
    }
}

class TreeBuilder {
public:
    TreeBuilder(const TreeParser& p, SceneBuilder& b) : p_(p), b_(b) {}

    std::shared_ptr<DiagramScene> Build() {
        const size_t n = p_.nodes.size();
        rows_.resize(n);
        const float unit = b_.size();
        // Rows top to bottom, depth first; each 10 taller than its text.
        float y = 0, right = 0;
        Place(0, 0, &y);
        for (size_t i = 0; i < n; ++i) {
            const TreeNode& nd = p_.nodes[i];
            Row& r = rows_[i];
            RichText t;
            t.text = DecodeEntities(nd.name);
            r.label = b_.Measure(t, 0, nd.directory, 16 / unit, false);
            r.icon = Icon(nd);
            r.icon_name = nd.icon;
            r.label_x = r.x + kPadX + (r.icon ? kIconSize + kIconGap : 0.f);
            right = std::max(right, r.label_x + r.label.w);
        }
        // Lines: a stub to each row from the left (not the root's), and down
        // from a directory to its last child's middle.
        const DgColor line = Role(kDgLine), ink = Role(kDgText);
        for (size_t i = 0; i < n; ++i) {
            const Row& r = rows_[i];
            const float mid = r.y + Height() / 2;
            if (i != 0) Stroke(D2D1::Point2F(r.x - kRowIndent, mid), D2D1::Point2F(r.x, mid), line);
            const std::vector<int>& kids = p_.nodes[i].children;
            if (!kids.empty()) {
                const Row& last = rows_[static_cast<size_t>(kids.back())];
                Stroke(D2D1::Point2F(r.x + kPadX, r.y + Height()), D2D1::Point2F(r.x + kPadX, last.y + Height() / 2 + 0.5f), line);
            }
        }
        // Descriptions in a column past the longest label.
        float width = right;
        const float desc_x = right + kDescGap;
        std::vector<M> descs(n);
        for (size_t i = 0; i < n; ++i) {
            const TreeNode& nd = p_.nodes[i];
            if (nd.description.empty()) continue;
            RichText d;
            d.text = DecodeEntities(nd.description);
            d.spans.push_back({0, static_cast<uint32_t>(d.text.size()), false, true});
            descs[i] = b_.Measure(d, 0, false, 16 / unit, false);
            width = std::max(width, desc_x + descs[i].w + kPadX);
        }
        // Highlighted rows: a band to the right edge.
        for (size_t i = 0; i < n; ++i) {
            if (!p_.nodes[i].highlight) continue;
            const Row& r = rows_[i];
            const float w = width - r.x + 8;
            width = std::max(width, r.x + w + 2);
            b_.Rect(D2D1::RectF(r.x, r.y + 1, r.x + w, r.y + Height() - 1), 3, Argb(0x26FFC107), Argb(0xFFFFC107), 1.f);
        }
        for (size_t i = 0; i < n; ++i) {
            const TreeNode& nd = p_.nodes[i];
            const Row& r = rows_[i];
            const float mid = r.y + Height() / 2;
            if (r.icon) TreeIcon(b_, r.icon_name, r.x + kPadX, r.y + kPadY);
            b_.Text(r.label, D2D1::Point2F(r.label_x, mid - r.label.h / 2), ink, nd.pos, nd.len);
            if (descs[i].layout) b_.Text(descs[i], D2D1::Point2F(desc_x, mid - descs[i].h / 2), Argb(0xFF6A9955), nd.desc_pos, nd.desc_len);
        }
        return b_.FinishFitted(4);
    }

private:
    struct Row {
        float x = 0, y = 0;
        M label;
        bool icon = false;
        std::wstring icon_name;
        float label_x = 0;
    };

    static float Height() { return kTextH + 2 * kPadY; }

    void Place(size_t i, int depth, float* y) {
        rows_[i].x = static_cast<float>(depth) * (kRowIndent + kPadX);
        rows_[i].y = *y;
        *y += Height();
        for (int c : p_.nodes[i].children) Place(static_cast<size_t>(c), depth + 1, y);
    }

    // Mermaid's icon for a node: only one asked for (showIcons is off).
    bool Icon(const TreeNode& nd) {
        return nd.has_icon && nd.icon != L"none";
    }

    void Stroke(D2D1_POINT_2F a, D2D1_POINT_2F z, DgColor color) {
        b_.Begin(a);
        b_.LineTo(z);
        b_.End(Role(kDgNone), color, 1.f);
    }

    const TreeParser& p_;
    SceneBuilder& b_;
    std::vector<Row> rows_;
};

}  // namespace

std::shared_ptr<DiagramScene> TreeView(const Source& src, SceneBuilder* builder) {
    TreeParser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return TreeBuilder(p, *builder).Build();
}

// info [showInfo]: the version of Mermaid these diagrams follow, as it
// shows its own.
std::shared_ptr<DiagramScene> InfoDiagram(const Source& src, SceneBuilder* builder) {
    Cursor c(src.text, src.body);
    c.SkipStatementGap();
    if (!c.EatKeyword(L"info", true)) c.Fail(Tr(L"Expected \"info\"", L"Ожидалось «info»"));
    // info, then showInfo, then title and accessibility lines (each on its
    // own line), comments anywhere.
    auto gap = [&] {
        for (;;) {
            c.SkipBlanks();
            if (c.Rest().starts_with(L"%%")) c.SkipToLineEnd();
            if (c.Peek() == L'\r' || c.Peek() == L'\n') {
                c.Advance();
                continue;
            }
            return;
        }
    };
    auto line_end = [&] {
        c.SkipBlanks();
        if (c.Rest().starts_with(L"%%")) c.SkipToLineEnd();
        if (!c.AtEnd() && c.Peek() != L'\n' && c.Peek() != L'\r') c.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
    };
    gap();
    if (c.EatKeyword(L"showInfo", true)) gap();
    while (!c.AtEnd()) {
        if (c.EatKeyword(L"title", true)) {
            c.SkipToLineEnd();
        } else if (c.EatKeyword(L"accTitle", true)) {
            c.SkipBlanks();
            if (!c.Eat(L':')) c.Fail(Tr(L"Expected \":\"", L"Ожидалось «:»"));
            c.SkipToLineEnd();
        } else if (c.EatKeyword(L"accDescr", true)) {
            c.SkipBlanks();
            if (c.Eat(L'{')) {
                while (!c.AtEnd() && c.Peek() != L'}') c.Advance();
                if (!c.Eat(L'}')) c.Fail(Tr(L"Expected \"}\"", L"Ожидалось «}»"));
            } else if (c.Eat(L':')) {
                c.SkipToLineEnd();
            } else {
                c.Fail(Tr(L"Expected \":\"", L"Ожидалось «:»"));
            }
        } else {
            c.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        line_end();
        gap();
    }
    if (!builder) return nullptr;
    // Mermaid's: a 400 x 100 box, the version centred on x 100, its
    // baseline at 40.
    RichText t;
    t.text = L"v11.17.2";
    const SceneBuilder::Measured m = builder->Measure(t, 0, false, 32 / builder->size());
    builder->Text(m, D2D1::Point2F(100 - m.w / 2, 40 - 1.17f * 32), Role(kDgText));
    return builder->Finish(400, 100);
}

}  // namespace mdv::mermaid
