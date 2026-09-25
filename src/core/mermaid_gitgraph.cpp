// Mermaid git graphs: branches as lanes (left to right, or top to bottom
// with TB/BT), commits on them in the order made, merges and cherry-picks,
// commit types (normal, reverse, highlight), tags and ids, in the theme's
// git colors; laid out as Mermaid's gitGraph renderer does.
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <unordered_map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

enum class Type : uint8_t { Normal, Reverse, Highlight, Merge, CherryPick };

struct Commit {
    std::wstring id;
    Text id_text;                // a custom id in the source
    int branch = 0;
    int parents[2] = {-1, -1};
    Type type = Type::Normal;    // what it is
    Type symbol = Type::Normal;  // how it is drawn (a merge's type: sets this)
    std::vector<Text> tags;
};

struct Branch {
    std::wstring name;
    Text text;                   // where it is named
    double order = 0;            // (0 unless given, as in Mermaid)
    int head = -1;
};

enum class Dir : uint8_t { LR, TB, BT };

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"gitGraph")) c_.Fail(Tr(L"Expected \"gitGraph\"", L"Ожидалось «gitGraph»"));
        c_.SkipBlanks();
        if (c_.EatKeyword(L"TB")) dir = Dir::TB;
        else if (c_.EatKeyword(L"BT")) dir = Dir::BT;
        else c_.EatKeyword(L"LR");
        c_.SkipBlanks();
        c_.Eat(L':');
        branches.push_back(Branch{src_.main_branch.empty() ? std::wstring(L"main") : src_.main_branch, {}, 0, -1});
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
    }

    Dir dir = Dir::LR;
    Text title;
    std::vector<Commit> commits;
    std::vector<Branch> branches;

private:
    void Statement() {
        const size_t at = c_.pos();
        if (c_.EatKeyword(L"commit")) {
            Commit c;
            Options(&c, nullptr, nullptr, nullptr);
            Add(std::move(c), at);
            return;
        }
        if (c_.EatKeyword(L"branch")) {
            c_.SkipBlanks();
            const Text name = Name();
            if (Find(name.raw) >= 0) {
                c_.set_pos(at);
                c_.Fail(Tr(L"The branch exists already: ", L"Ветка уже есть: ") + name.raw);
            }
            Branch b{name.raw, name, 0, branches[static_cast<size_t>(current_)].head};
            c_.SkipBlanks();
            if (c_.EatKeyword(L"order")) {
                c_.SkipBlanks();
                if (!c_.Eat(L':')) c_.Fail(Tr(L"Expected ':'", L"Ожидалось «:»"));
                c_.SkipBlanks();
                const size_t start = c_.pos();
                while (c_.Peek() >= L'0' && c_.Peek() <= L'9') c_.Advance();
                if (c_.pos() == start || c_.pos() - start > 9) c_.Fail(Tr(L"Expected a number", L"Ожидалось число"));
                b.order = std::wcstod(std::wstring(src_.text.substr(start, c_.pos() - start)).c_str(), nullptr);
            }
            if (branches.size() >= 64) c_.Fail(Tr(L"Too many branches", L"Слишком много веток"));
            branches.push_back(std::move(b));
            current_ = static_cast<int>(branches.size()) - 1;  // (branch checks it out)
            return;
        }
        if (c_.EatKeyword(L"checkout") || c_.EatKeyword(L"switch")) {
            c_.SkipBlanks();
            const Text name = Name();
            const int b = Find(name.raw);
            if (b < 0) {
                c_.set_pos(at);
                c_.Fail(Tr(L"Checkout of a branch not yet created: ", L"Переход на ещё не созданную ветку: ") + name.raw);
            }
            current_ = b;
            return;
        }
        if (c_.EatKeyword(L"merge")) {
            c_.SkipBlanks();
            const Text name = Name();
            const int other = Find(name.raw);
            const int head = branches[static_cast<size_t>(current_)].head;
            std::wstring error;
            if (other < 0) error = Tr(L"Merge of a branch not yet created: ", L"Слияние с ещё не созданной веткой: ") + name.raw;
            else if (other == current_) error = Tr(L"A branch can't be merged into itself", L"Ветку нельзя слить с ней самой");
            else if (head < 0 || branches[static_cast<size_t>(other)].head < 0) error = Tr(L"Merge of a branch without commits", L"Слияние с веткой без коммитов");
            else if (branches[static_cast<size_t>(other)].head == head) error = Tr(L"Both branches have the same head", L"У обеих веток одна вершина");
            if (!error.empty()) {
                c_.set_pos(at);
                c_.Fail(error);
            }
            Commit c;
            c.type = c.symbol = Type::Merge;
            Options(&c, nullptr, nullptr, nullptr);
            if (!c.id_text.raw.empty() && ids_.count(c.id_text.raw)) {
                c_.set_pos(at);
                c_.Fail(Tr(L"A commit with this id exists already: ", L"Коммит с таким id уже есть: ") + c.id_text.raw);
            }
            c.parents[1] = branches[static_cast<size_t>(other)].head;
            Add(std::move(c), at);
            return;
        }
        if (c_.EatKeyword(L"cherry-pick")) {
            CherryPick(at);
            return;
        }
        if (c_.EatKeyword(L"title")) {
            c_.SkipBlanks();
            const std::wstring_view t = c_.WholeLine();
            title = Text{std::wstring(t), static_cast<uint32_t>(t.data() - src_.text.data()), static_cast<uint32_t>(t.size())};
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
        c_.Fail(Tr(L"Expected commit, branch, checkout, merge or cherry-pick", L"Ожидалось commit, branch, checkout, merge или cherry-pick"));
    }

    // cherry-pick id: "x" [parent: "p"] [tag: "t"]: Mermaid's checks, and its
    // tag (unless tags are given; empty ones dropped).
    void CherryPick(size_t at) {
        Commit c;
        c.type = c.symbol = Type::CherryPick;
        std::wstring source, parent;
        bool tagged = false;
        Options(&c, &source, &parent, &tagged);
        auto fail = [&](std::wstring message) {
            c_.set_pos(at);
            c_.Fail(std::move(message));
        };
        const auto it = ids_.find(source);
        if (source.empty() || it == ids_.end()) fail(Tr(L"cherry-pick needs the id of a commit there is", L"cherry-pick нужен id существующего коммита"));
        const Commit& s = commits[static_cast<size_t>(it->second)];
        if (!parent.empty()) {
            bool found = false;
            for (const int p : s.parents) found = found || (p >= 0 && commits[static_cast<size_t>(p)].id == parent);
            if (!found) fail(Tr(L"The parent isn't a parent of the cherry-picked commit", L"parent не родитель выбранного коммита"));
        }
        if (s.type == Type::Merge && parent.empty()) fail(Tr(L"A merge commit is cherry-picked with parent:", L"Коммит слияния берётся с parent:"));
        if (s.branch == current_) fail(Tr(L"The commit is on the current branch already", L"Коммит уже на текущей ветке"));
        if (branches[static_cast<size_t>(current_)].head < 0) fail(Tr(L"The current branch has no commits", L"На текущей ветке нет коммитов"));
        c.parents[1] = it->second;
        std::erase_if(c.tags, [](const Text& t) { return t.raw.empty(); });
        if (!tagged) {
            Text t;
            t.raw = L"cherry-pick:" + s.id + (s.type == Type::Merge ? L"|parent:" + parent : L"");
            c.tags.push_back(std::move(t));
        }
        Add(std::move(c), at);
    }

    // id: "x", tag: "t", type: HIGHLIGHT, msg: "m" or just "m" (cherry-pick:
    // id and parent).
    void Options(Commit* c, std::wstring* source, std::wstring* parent, bool* tagged) {
        for (;;) {
            c_.SkipBlanks();
            if (c_.AtStatementEnd()) return;
            if (c_.Peek() == L'"' || c_.Peek() == L'\'') {  // a message (not drawn)
                Quoted();
                continue;
            }
            const size_t start = c_.pos();
            while (IsWordChar(c_.Peek())) c_.Advance();
            const std::wstring key(src_.text.substr(start, c_.pos() - start));
            c_.SkipBlanks();
            if (key.empty() || !c_.Eat(L':')) {
                c_.set_pos(start);
                c_.Fail(Tr(L"Expected id:, tag: or type:", L"Ожидалось id:, tag: или type:"));
            }
            c_.SkipBlanks();
            if (key == L"type") {
                Type t = Type::Normal;
                if (c_.EatKeyword(L"NORMAL")) t = Type::Normal;
                else if (c_.EatKeyword(L"REVERSE")) t = Type::Reverse;
                else if (c_.EatKeyword(L"HIGHLIGHT")) t = Type::Highlight;
                else c_.Fail(Tr(L"Expected NORMAL, REVERSE or HIGHLIGHT", L"Ожидалось NORMAL, REVERSE или HIGHLIGHT"));
                if (c->type == Type::Normal || c->type == Type::Reverse || c->type == Type::Highlight) c->type = t;
                c->symbol = t;
                continue;
            }
            const Text value = Quoted();
            if (key == L"id") {
                if (source) *source = value.raw;
                else c->id_text = value;
            } else if (key == L"tag") {
                if (c->tags.size() < 64) c->tags.push_back(value);
                if (tagged) *tagged = true;
            } else if (key == L"parent" && parent) {
                *parent = value.raw;
            } else if (key != L"msg") {
                c_.set_pos(start);
                c_.Fail(Tr(L"Unknown option: ", L"Неизвестный параметр: ") + key);
            }
        }
    }

    void Add(Commit c, size_t at) {
        if (commits.size() >= kMaxNodes) {
            c_.set_pos(at);
            c_.Fail(Tr(L"Too many commits", L"Слишком много коммитов"));
        }
        Branch& b = branches[static_cast<size_t>(current_)];
        c.branch = current_;
        c.parents[0] = b.head;
        const int seq = static_cast<int>(commits.size());
        if (!c.id_text.raw.empty()) {
            c.id = c.id_text.raw;
        } else {
            // Mermaid's ids are "seq-" and seven random hex digits: here a
            // hash of the commit's place, so they stay the same.
            uint32_t h = 2166136261u ^ static_cast<uint32_t>(seq * 2654435761u);
            for (wchar_t ch : b.name) h = (h ^ ch) * 16777619u;
            h = (h ^ (h >> 15)) * 2246822519u;
            wchar_t buf[16];
            std::swprintf(buf, 16, L"%07x", h & 0xFFFFFFF);
            c.id = std::to_wstring(seq) + L"-" + buf;
        }
        ids_[c.id] = seq;
        b.head = seq;
        commits.push_back(std::move(c));
    }

    int Find(const std::wstring& name) const {
        for (size_t k = 0; k < branches.size(); ++k) {
            if (branches[k].name == name) return static_cast<int>(k);
        }
        return -1;
    }

    // A branch name: letters, digits, - . / _ (not ending in . or /), or quoted.
    Text Name() {
        if (c_.Peek() == L'"' || c_.Peek() == L'\'') return Quoted();
        const size_t start = c_.pos();
        while (!c_.AtEnd() && (IsWordChar(c_.Peek()) || c_.Peek() == L'-' || c_.Peek() == L'.' || c_.Peek() == L'/')) c_.Advance();
        while (c_.pos() > start && (src_.text[c_.pos() - 1] == L'.' || src_.text[c_.pos() - 1] == L'/')) c_.set_pos(c_.pos() - 1);
        if (c_.pos() == start) c_.Fail(Tr(L"Expected a branch name", L"Ожидалось имя ветки"));
        return Text{std::wstring(src_.text.substr(start, c_.pos() - start)), static_cast<uint32_t>(start), static_cast<uint32_t>(c_.pos() - start)};
    }

    // "text" or 'text', with \ escapes.
    Text Quoted() {
        const size_t open = c_.pos();
        const wchar_t quote = c_.Peek();
        if (quote != L'"' && quote != L'\'') c_.Fail(Tr(L"Expected a \"quoted\" value", L"Ожидалось значение в кавычках"));
        c_.Advance();
        const size_t start = c_.pos();
        std::wstring raw;
        while (!c_.AtEnd() && c_.Peek() != quote && c_.Peek() != L'\n') {
            if (c_.Peek() == L'\\' && c_.Peek(1) != L'\0' && c_.Peek(1) != L'\n') c_.Advance();
            raw.push_back(c_.Peek());
            c_.Advance();
        }
        if (c_.Peek() != quote) {
            c_.set_pos(open);
            c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        }
        if (raw.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        const size_t end = c_.pos();
        c_.Advance();
        return Text{std::move(raw), static_cast<uint32_t>(start), static_cast<uint32_t>(end - start)};
    }

    const Source& src_;
    Cursor c_;
    int current_ = 0;
    std::unordered_map<std::wstring, int> ids_;
};

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

using M = SceneBuilder::Measured;

constexpr float kStep = 50;              // between commits
constexpr float kLane = 90;              // between branches (50, and 40 for the turned ids)
constexpr float kR = 10;                 // a commit's radius
constexpr float kArc = 20;               // corners of the lines between lanes
constexpr float kMargin = 12;

// `p` turned by `deg` degrees (clockwise) about `c`.
D2D1_POINT_2F Turn(D2D1_POINT_2F p, D2D1_POINT_2F c, float deg) {
    const float a = deg * 3.14159265f / 180, cs = std::cos(a), sn = std::sin(a);
    const float x = p.x - c.x, y = p.y - c.y;
    return D2D1::Point2F(c.x + x * cs - y * sn, c.y + x * sn + y * cs);
}

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src) {}

    std::shared_ptr<DiagramScene> Build() {
        const bool lr = p_.dir == Dir::LR;
        // Lanes in order (as made, unless ordered otherwise).
        std::vector<int> order(p_.branches.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return p_.branches[static_cast<size_t>(a)].order < p_.branches[static_cast<size_t>(b)].order; });
        lane_.assign(p_.branches.size(), 0);
        for (size_t i = 0; i < order.size(); ++i) lane_[static_cast<size_t>(order[i])] = static_cast<int>(i);
        // Branch labels.
        std::vector<M> labels;
        float label_w = 0, label_h = 0;
        for (const Branch& br : p_.branches) {
            labels.push_back(b_.Measure(ParseLabel(br.name, false), 0, false, 0.875f));
            label_w = std::max(label_w, labels.back().w + 18);
            label_h = std::max(label_h, labels.back().h + 4);
        }
        // Where lanes are: 90 apart, top to bottom also half a label wider.
        lane_at_.assign(p_.branches.size(), 0);
        float next = 0;
        for (const int k : order) {
            lane_at_[static_cast<size_t>(k)] = next;
            next += kLane + (lr ? 0.f : labels[static_cast<size_t>(k)].w / 2);
        }
        const float unit = b_.size();
        // Where things are: LR, lanes are rows and commits go right; TB/BT,
        // lanes are columns and commits go down (up).
        const float start = lr ? label_w + 40 : label_h + 40;
        const size_t n = p_.commits.size();
        auto along = [&](size_t seq) {
            const float a = start + static_cast<float>(seq) * kStep;
            return p_.dir == Dir::BT ? start + static_cast<float>(n > 0 ? n - 1 : 0) * kStep - a + start : a;
        };
        pos_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const float l = lane_at_[static_cast<size_t>(p_.commits[i].branch)], a = along(i);
            pos_[i] = lr ? D2D1::Point2F(a, l) : D2D1::Point2F(l, a);
        }
        const float end = start + static_cast<float>(n > 0 ? n - 1 : 0) * kStep + 40;
        // Lane lines, dotted, from the label to past the last commit.
        for (size_t k = 0; k < p_.branches.size(); ++k) {
            const float l = lane_at_[k];
            if (lr) {
                b_.Begin(D2D1::Point2F(label_w + 10, l));
                b_.LineTo(D2D1::Point2F(end, l));
            } else {
                b_.Begin(D2D1::Point2F(l, p_.dir == Dir::BT ? 0.f : label_h + 10));
                b_.LineTo(D2D1::Point2F(l, end));
            }
            b_.End(Role(kDgNone), Role(kDgLine), 1.f, 2);
        }
        // Lines from parents (the lanes they may run between start as the
        // branches' own).
        lanes_ = lane_at_;
        for (size_t i = 0; i < n; ++i) {
            const Commit& c = p_.commits[i];
            for (int k = 0; k < 2; ++k) {
                if (c.parents[k] >= 0) Arrow(static_cast<size_t>(c.parents[k]), i, k == 1 && c.type == Type::Merge);
            }
        }
        for (size_t i = 0; i < n; ++i) DrawCommit(i);
        // Ids (not of cherry-picks, nor of merges without their own), turned
        // 45 degrees on a see-through background, as Mermaid; then tags.
        for (size_t i = 0; i < n; ++i) {
            const Commit& c = p_.commits[i];
            if (c.type != Type::CherryPick && (c.type != Type::Merge || !c.id_text.raw.empty())) Id(i, unit);
            Tags(i, unit);
        }
        // Branch labels at the lanes' start.
        for (size_t k = 0; k < p_.branches.size(); ++k) {
            const M& m = labels[k];
            const float w = m.w + 18, h = m.h + 4, l = lane_at_[k];
            const D2D1_RECT_F r = lr ? D2D1::RectF(label_w - w, l - h / 2, label_w, l + h / 2)
                                     : D2D1::RectF(l - w / 2, p_.dir == Dir::BT ? end + 10 : 0.f, l + w / 2, (p_.dir == Dir::BT ? end + 10 : 0.f) + h);
            b_.Rect(r, 4, Role(Git(static_cast<int>(k))), Role(kDgNone));
            const Branch& br = p_.branches[k];
            b_.Text(m, D2D1::Point2F(r.left + 9, r.top + 2), Role(static_cast<uint8_t>(kDgGitText + lane_[k] % 8)), br.text.pos, br.text.len);
        }
        Title(unit);
        // Everything moved inside the margins.
        return b_.FinishFitted(kMargin);
    }

private:
    // A branch's colors: by its lane, as Mermaid's.
    uint8_t Git(int branch) const { return static_cast<uint8_t>(kDgGit + lane_[static_cast<size_t>(branch)] % 8); }

    // The commit's id as Mermaid places it: under it, turned up to the right
    // (left of it top to bottom, turned too), on a see-through background.
    void Id(size_t i, float unit) {
        const Commit& c = p_.commits[i];
        RichText t;
        t.text = c.id;
        const M m = b_.Measure(t, 0, false, 10 / unit, false);
        const D2D1_POINT_2F at = pos_[i];
        D2D1_POINT_2F box[4], text;
        if (p_.dir == Dir::LR) {
            // Its box under the commit, turned -45 degrees about the point
            // 10 left of it, then moved as Mermaid moves it.
            const D2D1_POINT_2F pivot = D2D1::Point2F(at.x - 10, at.y);
            const float dx = -7.5f - (m.w + 10) / 25 * 9.5f, dy = 10 + m.w / 25 * 8.5f;
            const float left = at.x - m.w / 2 - 2, top = at.y + 13.5f;
            const D2D1_POINT_2F corners[4] = {{left, top}, {left + m.w + 4, top}, {left + m.w + 4, top + m.h + 4}, {left, top + m.h + 4}};
            for (int k = 0; k < 4; ++k) {
                const D2D1_POINT_2F q = Turn(corners[k], pivot, -45);
                box[k] = D2D1::Point2F(q.x + dx, q.y + dy);
            }
            const D2D1_POINT_2F q = Turn(D2D1::Point2F(left + 2, top + 2), pivot, -45);
            text = D2D1::Point2F(q.x + dx, q.y + dy);
        } else {
            // Left of the commit, turned -45 degrees about it.
            const float left = at.x - m.w - 21, top = at.y - 12;
            const D2D1_POINT_2F corners[4] = {{left, top}, {left + m.w + 4, top}, {left + m.w + 4, top + m.h + 4}, {left, top + m.h + 4}};
            for (int k = 0; k < 4; ++k) box[k] = Turn(corners[k], at, -45);
            text = Turn(D2D1::Point2F(left + 5, top + 2), at, -45);
        }
        b_.Polygon({box[0], box[1], box[2], box[3]}, RoleAt(kDgClusterFill, 128), Role(kDgNone));
        b_.TextTurned(m, text, -45.f, Role(kDgText), c.id_text.pos, c.id_text.len);
    }

    // Tags, the last next to the commit (above it; top to bottom, right of
    // it turned 45 degrees), all as wide as the widest.
    void Tags(size_t i, float unit) {
        const Commit& c = p_.commits[i];
        if (c.tags.empty()) return;
        std::vector<M> ms;
        float w = 0, h = 0;
        for (const Text& tag : c.tags) {
            RichText tt;
            tt.text = tag.raw;
            ms.push_back(b_.Measure(tt, 0, false, 10 / unit, false));
            w = std::max(w, ms.back().w);
            h = std::max(h, ms.back().h);
        }
        const D2D1_POINT_2F at = pos_[i];
        const float h2 = h / 2;
        for (size_t k = c.tags.size(); k-- > 0;) {
            const float offset = 20.f * static_cast<float>(c.tags.size() - 1 - k);
            const M& m = ms[k];
            std::vector<D2D1_POINT_2F> tag;
            D2D1_POINT_2F hole, text;
            float angle = 0;
            if (p_.dir == Dir::LR) {
                const float ly = at.y - 19.2f - offset, pos = at.x - 10;
                tag = {{pos - w / 2 - 2, ly + 2}, {pos - w / 2 - 2, ly - 2}, {at.x - w / 2 - 4, ly - h2 - 2}, {at.x + w / 2 + 4, ly - h2 - 2},
                       {at.x + w / 2 + 4, ly + h2 + 2}, {at.x - w / 2 - 4, ly + h2 + 2}};
                hole = D2D1::Point2F(pos - w / 2 + 2, ly);
                text = D2D1::Point2F(at.x - m.w / 2, ly - m.h / 2);
            } else {
                // Mermaid's shape right of the commit, turned 45 degrees about
                // the point 10 before it and moved 12 (the text 14) down right.
                const D2D1_POINT_2F pivot = D2D1::Point2F(at.x, at.y - 10);
                const float y = pivot.y + offset;
                for (const D2D1_POINT_2F q : {D2D1::Point2F(at.x, y + 2), D2D1::Point2F(at.x, y - 2), D2D1::Point2F(at.x + 10, y - h2 - 2),
                                              D2D1::Point2F(at.x + 14 + w, y - h2 - 2), D2D1::Point2F(at.x + 14 + w, y + h2 + 2),
                                              D2D1::Point2F(at.x + 10, y + h2 + 2)}) {
                    const D2D1_POINT_2F t = Turn(q, pivot, 45);
                    tag.push_back(D2D1::Point2F(t.x + 12, t.y + 12));
                }
                const D2D1_POINT_2F hq = Turn(D2D1::Point2F(at.x + 2, y), pivot, 45);
                hole = D2D1::Point2F(hq.x + 12, hq.y + 12);
                const D2D1_POINT_2F tq = Turn(D2D1::Point2F(at.x + 12, y - m.h / 2), pivot, 45);
                text = D2D1::Point2F(tq.x + 14, tq.y + 14);
                angle = 45;
            }
            b_.Polygon(tag, Role(kDgNodeFill), Role(kDgPrimaryBorder), 1.f);
            b_.Ellipse(D2D1::RectF(hole.x - 1.5f, hole.y - 1.5f, hole.x + 1.5f, hole.y + 1.5f), Role(kDgText), Role(kDgNone));
            if (angle != 0) b_.TextTurned(m, text, angle, Role(kDgText), c.tags[k].pos, c.tags[k].len);
            else b_.Text(m, text, Role(kDgText), c.tags[k].pos, c.tags[k].len);
        }
    }

    // The title (the diagram's, or the front matter's), centred above.
    void Title(float unit) {
        const bool own = !p_.title.raw.empty();
        const std::wstring& raw = own ? p_.title.raw : src_.title;
        if (raw.empty()) return;
        const DiagramScene& s = b_.scene();
        float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f;
        for (const DiagramScene::Shape& sh : s.shapes) {
            x0 = std::min(x0, sh.box.left);
            x1 = std::max(x1, sh.box.right);
            y0 = std::min(y0, sh.box.top);
        }
        for (const DiagramScene::Text& t : s.texts) {
            x0 = std::min(x0, t.origin.x);
            x1 = std::max(x1, t.origin.x + t.w);
            y0 = std::min(y0, t.origin.y);
        }
        if (x0 > x1) x0 = x1 = y0 = 0;
        const M m = b_.Measure(ParseLabel(raw, false), 0, false, 18 / unit);
        b_.Text(m, D2D1::Point2F((x0 + x1) / 2 - m.w / 2, std::min(-25 - m.h * 0.75f, y0 - m.h - 6)), Role(kDgText),
                own ? p_.title.pos : src_.title_pos, own ? p_.title.len : src_.title_len);
    }

    // Mermaid's lines from a parent: in the child's branch color (from a
    // merged commit, in its own), turning with rounded corners across at
    // the parent then along the child's lane (from a merged commit: along
    // its lane, then across at the child). With commits of the lane it
    // would turn onto in between, it runs along a lane of its own between
    // the two instead, in the upper (left) commit's color.
    void Arrow(size_t from, size_t to, bool merged) {
        const Commit& ca = p_.commits[from];
        const Commit& cz = p_.commits[to];
        const D2D1_POINT_2F a = pos_[from], z = pos_[to];
        const bool lr = p_.dir == Dir::LR;
        const float ac = lr ? a.y : a.x, zc = lr ? z.y : z.x;  // across the lanes
        const float aa = lr ? a.x : a.y, za = lr ? z.x : z.y;  // along them
        const bool z_further = ac < zc;
        const int curve_branch = z_further ? cz.branch : ca.branch;
        bool reroute = false;
        for (size_t x = from + 1; x < to && !reroute; ++x) reroute = p_.commits[x].branch == curve_branch;
        int branch = merged ? ca.branch : cz.branch;
        if (reroute && !z_further) branch = ca.branch;
        const DgColor color = Role(Git(branch));
        auto P = [&](float along, float across) { return lr ? D2D1::Point2F(along, across) : D2D1::Point2F(across, along); };
        b_.Begin(a);
        if (ac == zc) {
            b_.LineTo(z);
        } else {
            const float s_across = zc > ac ? 1.f : -1.f, s_along = za > aa ? 1.f : -1.f;
            const float r = std::min({reroute ? kArc / 2 : kArc, std::abs(zc - ac) / 2, std::abs(za - aa) / 2});
            const float k = r * 0.5523f;
            const float run = reroute ? FindLane(std::min(ac, zc), std::max(ac, zc)) : merged ? ac : zc;
            if (run == ac) {
                // Along, then across.
                b_.LineTo(P(za - s_along * r, ac));
                b_.CubicTo(P(za - s_along * (r - k), ac), P(za, ac + s_across * (r - k)), P(za, ac + s_across * r));
            } else {
                // Across (to the lane it runs along), along, across.
                b_.LineTo(P(aa, run - s_across * r));
                b_.CubicTo(P(aa, run - s_across * (r - k)), P(aa + s_along * (r - k), run), P(aa + s_along * r, run));
                if (run != zc) {
                    b_.LineTo(P(za - s_along * r, run));
                    b_.CubicTo(P(za - s_along * (r - k), run), P(za, run + s_across * (r - k)), P(za, run + s_across * r));
                }
            }
            b_.LineTo(z);
        }
        b_.End(Role(kDgNone), color, 8.f);
    }

    // Mermaid's lane for a line between lanes at y1 < y2: half way, or
    // nearer y1 while within 10 of another lane (up to five times).
    float FindLane(float y1, float y2) {
        float candidate = y1;
        for (int depth = 0;; ++depth) {
            candidate = y1 + std::abs(y1 - y2) / 2;
            if (depth > 5) return candidate;
            if (std::all_of(lanes_.begin(), lanes_.end(), [&](float l) { return std::abs(l - candidate) >= 10; })) {
                lanes_.push_back(candidate);
                return candidate;
            }
            y2 -= std::abs(y1 - y2) / 5;
        }
    }

    // Mermaid's bullets: the inner parts in its primary color, a
    // cherry-pick's in the text color with the page's cherries.
    void DrawCommit(size_t i) {
        const Commit& c = p_.commits[i];
        const D2D1_POINT_2F p = pos_[i];
        const DgColor color = Role(Git(c.branch)), inner = Role(kDgNodeFill);
        const D2D1_RECT_F r = D2D1::RectF(p.x - kR, p.y - kR, p.x + kR, p.y + kR);
        switch (c.symbol) {
        case Type::Highlight:
            b_.Rect(r, 0, Role(static_cast<uint8_t>(kDgGitInv + lane_[static_cast<size_t>(c.branch)] % 8)), Role(kDgNone));
            b_.Rect(D2D1::RectF(p.x - 6, p.y - 6, p.x + 6, p.y + 6), 0, inner, Role(kDgNone));
            break;
        case Type::Merge:
            b_.Ellipse(r, color, Role(kDgNone));
            b_.Ellipse(D2D1::RectF(p.x - 6.5f, p.y - 6.5f, p.x + 6.5f, p.y + 6.5f), inner, Role(kDgNone));
            break;
        case Type::CherryPick: {
            const DgColor cherry = Role(kDgPage);
            b_.Ellipse(r, Role(kDgText), Role(kDgNone));
            b_.Ellipse(D2D1::RectF(p.x - 5.75f, p.y - 0.75f, p.x - 0.25f, p.y + 4.75f), cherry, Role(kDgNone));
            b_.Ellipse(D2D1::RectF(p.x + 0.25f, p.y - 0.75f, p.x + 5.75f, p.y + 4.75f), cherry, Role(kDgNone));
            b_.Begin(D2D1::Point2F(p.x - 3, p.y + 1));
            b_.LineTo(D2D1::Point2F(p.x, p.y - 5));
            b_.LineTo(D2D1::Point2F(p.x + 3, p.y + 1));
            b_.End(Role(kDgNone), cherry, 1.f);
            break;
        }
        case Type::Reverse:
            b_.Ellipse(r, color, Role(kDgNone));
            b_.Begin(D2D1::Point2F(p.x - 5, p.y - 5));
            b_.LineTo(D2D1::Point2F(p.x + 5, p.y + 5));
            b_.MoveTo(D2D1::Point2F(p.x - 5, p.y + 5));
            b_.LineTo(D2D1::Point2F(p.x + 5, p.y - 5));
            b_.End(Role(kDgNone), inner, 3.f);
            break;
        default: b_.Ellipse(r, color, Role(kDgNone)); break;
        }
    }

    const Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
    std::vector<int> lane_;         // per branch: its lane
    std::vector<float> lane_at_;    // per branch: where its lane is
    std::vector<float> lanes_;      // lanes lines run along
    std::vector<D2D1_POINT_2F> pos_;
};

}  // namespace

std::shared_ptr<DiagramScene> GitGraph(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder, src).Build();
}

}  // namespace mdv::mermaid
