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
    Text id_text;              // a custom id in the source
    int branch = 0;
    int parents[2] = {-1, -1};
    Type type = Type::Normal;
    std::vector<Text> tags;
};

struct Branch {
    std::wstring name;
    Text text;                 // where it is named
    double order = 0;
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
        branches.push_back(Branch{L"main", {}, 0, -1});
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
    }

    Dir dir = Dir::LR;
    std::vector<Commit> commits;
    std::vector<Branch> branches;

private:
    void Statement() {
        const size_t at = c_.pos();
        if (c_.EatKeyword(L"commit")) {
            Commit c;
            Options(&c, nullptr, nullptr);
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
            Branch b{name.raw, name, static_cast<double>(branches.size()), branches[static_cast<size_t>(current_)].head};
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
            c.type = Type::Merge;
            Options(&c, nullptr, nullptr);
            if (c.type == Type::Normal) c.type = Type::Merge;
            c.parents[1] = branches[static_cast<size_t>(other)].head;
            Add(std::move(c), at);
            return;
        }
        if (c_.EatKeyword(L"cherry-pick")) {
            Commit c;
            c.type = Type::CherryPick;
            std::wstring source, parent;
            Options(&c, &source, &parent);
            auto it = ids_.find(source);
            if (source.empty() || it == ids_.end()) {
                c_.set_pos(at);
                c_.Fail(Tr(L"cherry-pick needs the id of a commit there is", L"cherry-pick нужен id существующего коммита"));
            }
            if (commits[static_cast<size_t>(it->second)].branch == current_) {
                c_.set_pos(at);
                c_.Fail(Tr(L"The commit is on the current branch already", L"Коммит уже на текущей ветке"));
            }
            c.type = Type::CherryPick;
            c.parents[1] = it->second;
            if (c.tags.empty()) {
                Text t;
                t.raw = L"cherry-pick:" + source;
                c.tags.push_back(std::move(t));
            }
            Add(std::move(c), at);
            return;
        }
        for (const wchar_t* kw : {L"accTitle", L"accDescr", L"title"}) {
            if (c_.EatKeyword(kw)) {
                c_.LineRest();
                return;
            }
        }
        c_.Fail(Tr(L"Expected commit, branch, checkout, merge or cherry-pick", L"Ожидалось commit, branch, checkout, merge или cherry-pick"));
    }

    // id: "x", tag: "t", type: HIGHLIGHT, msg: "m" (cherry-pick: id and parent).
    void Options(Commit* c, std::wstring* source, std::wstring* parent) {
        for (;;) {
            c_.SkipBlanks();
            if (c_.AtStatementEnd()) return;
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
                if (c_.EatKeyword(L"NORMAL")) c->type = c->type == Type::Merge ? Type::Merge : Type::Normal;
                else if (c_.EatKeyword(L"REVERSE")) c->type = Type::Reverse;
                else if (c_.EatKeyword(L"HIGHLIGHT")) c->type = Type::Highlight;
                else c_.Fail(Tr(L"Expected NORMAL, REVERSE or HIGHLIGHT", L"Ожидалось NORMAL, REVERSE или HIGHLIGHT"));
                continue;
            }
            const Text value = Quoted();
            if (key == L"id") {
                if (source) *source = value.raw;
                else c->id_text = value;
            } else if (key == L"tag") {
                if (c->tags.size() < 64) c->tags.push_back(value);
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

    // A branch name: letters, digits, - . / _ (or "quoted").
    Text Name() {
        if (c_.Peek() == L'"') return Quoted();
        const size_t start = c_.pos();
        while (!c_.AtEnd() && (IsWordChar(c_.Peek()) || c_.Peek() == L'-' || c_.Peek() == L'.' || c_.Peek() == L'/')) c_.Advance();
        if (c_.pos() == start) c_.Fail(Tr(L"Expected a branch name", L"Ожидалось имя ветки"));
        return Text{std::wstring(src_.text.substr(start, c_.pos() - start)), static_cast<uint32_t>(start), static_cast<uint32_t>(c_.pos() - start)};
    }

    Text Quoted() {
        const size_t open = c_.pos();
        if (!c_.Eat(L'"')) c_.Fail(Tr(L"Expected a \"quoted\" value", L"Ожидалось значение в кавычках"));
        const size_t start = c_.pos();
        while (!c_.AtEnd() && c_.Peek() != L'"' && c_.Peek() != L'\n') c_.Advance();
        if (c_.Peek() != L'"') {
            c_.set_pos(open);
            c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        }
        const std::wstring_view v = src_.text.substr(start, c_.pos() - start);
        if (v.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        c_.Advance();
        return Text{std::wstring(v), static_cast<uint32_t>(start), static_cast<uint32_t>(v.size())};
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
constexpr float kLane = 90;              // between branches
constexpr float kR = 10;                 // a commit's radius
constexpr float kArc = 20;               // corners of the lines between lanes
constexpr float kMargin = 12;

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b) : p_(p), b_(b) {}

    std::shared_ptr<DiagramScene> Build() {
        const bool lr = p_.dir == Dir::LR;
        // Lanes in order (main first unless ordered otherwise).
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
        const float unit = b_.size();
        // Where things are: LR, lanes are rows and commits go right; TB/BT,
        // lanes are columns and commits go down (up).
        const float start = lr ? label_w + 40 : label_h + 40;
        const size_t n = p_.commits.size();
        auto lane_at = [&](int branch) { return static_cast<float>(lane_[static_cast<size_t>(branch)]) * kLane; };
        auto along = [&](size_t seq) {
            const float a = start + static_cast<float>(seq) * kStep;
            return p_.dir == Dir::BT ? start + static_cast<float>(n > 0 ? n - 1 : 0) * kStep - a + start : a;
        };
        pos_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const float l = lane_at(p_.commits[i].branch), a = along(i);
            pos_[i] = lr ? D2D1::Point2F(a, l) : D2D1::Point2F(l, a);
        }
        const float end = start + static_cast<float>(n > 0 ? n - 1 : 0) * kStep + 40;
        // Lane lines, dotted, from the label to past the last commit.
        for (size_t k = 0; k < p_.branches.size(); ++k) {
            const float l = lane_at(static_cast<int>(k));
            if (lr) {
                b_.Begin(D2D1::Point2F(label_w + 10, l));
                b_.LineTo(D2D1::Point2F(end, l));
            } else {
                b_.Begin(D2D1::Point2F(l, p_.dir == Dir::BT ? 0.f : label_h + 10));
                b_.LineTo(D2D1::Point2F(l, p_.dir == Dir::BT ? end : end));
            }
            b_.End(Role(kDgNone), Role(kDgLine), 1.f, 2);
        }
        // Lines from parents, in the child's branch color (from a merged or
        // cherry-picked commit: in its own).
        for (size_t i = 0; i < n; ++i) {
            const Commit& c = p_.commits[i];
            for (int k = 0; k < 2; ++k) {
                const int par = c.parents[k];
                if (par < 0) continue;
                Arrow(static_cast<size_t>(par), i, k == 1 ? p_.commits[static_cast<size_t>(par)].branch : c.branch, k == 1);
            }
        }
        for (size_t i = 0; i < n; ++i) DrawCommit(i);
        // Commit ids (on a see-through background, as Mermaid) and tags.
        for (size_t i = 0; i < n; ++i) {
            const Commit& c = p_.commits[i];
            RichText t;
            t.text = c.id;
            const M m = b_.Measure(t, 0, false, 10 / unit, false);
            const D2D1_POINT_2F at = pos_[i];
            // Turned 45 degrees up, ending by the commit (below it, or left of it).
            const float cs = 0.7071f;
            const D2D1_POINT_2F end_top = lr ? D2D1::Point2F(at.x + 4, at.y + kR + 4) : D2D1::Point2F(at.x - kR - 6, at.y - 4);
            const D2D1_POINT_2F anchor = D2D1::Point2F(end_top.x - m.w * cs, end_top.y + m.w * cs);
            auto corner = [&](float x, float y) { return D2D1::Point2F(anchor.x + x * cs + y * cs, anchor.y - x * cs + y * cs); };
            b_.Polygon({corner(-2, -1), corner(m.w + 2, -1), corner(m.w + 2, m.h + 1), corner(-2, m.h + 1)}, RoleAt(kDgClusterFill, 128), Role(kDgNone));
            b_.TextTurned(m, anchor, -45.f, Role(kDgText), c.id_text.pos, c.id_text.len);
            // Tags above the commit (right of it top to bottom), one over another.
            float offset = 0;
            for (const Text& tag : c.tags) {
                RichText tt;
                tt.text = tag.raw;
                const M tm = b_.Measure(tt, 0, false, 10 / unit, false);
                const float w = tm.w + 12, h = tm.h + 2;
                D2D1_RECT_F r;
                if (lr) r = D2D1::RectF(at.x - w / 2 + 3, at.y - kR - 8 - h - offset, at.x + w / 2 + 3, at.y - kR - 8 - offset);
                else r = D2D1::RectF(at.x + kR + 12, at.y - h / 2 - offset, at.x + kR + 12 + w, at.y + h / 2 - offset);
                const float cy = (r.top + r.bottom) / 2;
                b_.Polygon({{r.left - 6, cy}, {r.left, r.top}, {r.right, r.top}, {r.right, r.bottom}, {r.left, r.bottom}}, Role(kDgNodeFill),
                           Role(kDgQuadrantBorder), 1.f);
                b_.Ellipse(D2D1::RectF(r.left - 1.5f, cy - 1.5f, r.left + 1.5f, cy + 1.5f), Role(kDgText), Role(kDgNone));
                b_.Text(tm, D2D1::Point2F(r.left + 6, r.top + 1), Role(kDgText), tag.pos, tag.len);
                offset += (lr ? h : h) + 4;
            }
        }
        // Branch labels at the lanes' start.
        for (size_t k = 0; k < p_.branches.size(); ++k) {
            const M& m = labels[k];
            const float w = m.w + 18, h = m.h + 4, l = lane_at(static_cast<int>(k));
            const D2D1_RECT_F r = lr ? D2D1::RectF(label_w - w, l - h / 2, label_w, l + h / 2)
                                     : D2D1::RectF(l - w / 2, p_.dir == Dir::BT ? end + 10 : 0.f, l + w / 2, (p_.dir == Dir::BT ? end + 10 : 0.f) + h);
            b_.Rect(r, 4, Role(Git(static_cast<int>(k))), Role(kDgNone));
            const Branch& br = p_.branches[k];
            b_.Text(m, D2D1::Point2F(r.left + 9, r.top + 2), Role(static_cast<uint8_t>(kDgGitText + lane_[k] % 8)), br.text.pos, br.text.len);
        }
        // Everything moved inside the margins.
        return b_.FinishFitted(kMargin);
    }

private:
    // A branch's colors: by its lane, as Mermaid's.
    uint8_t Git(int branch) const { return static_cast<uint8_t>(kDgGit + lane_[static_cast<size_t>(branch)] % 8); }

    // Lines between lanes turn with rounded corners: to a branch's first
    // commit, across at the parent, then along the child's lane; from a
    // merged (cherry-picked) commit, along its lane, then across at the
    // child. Along a lane with commits in the way, the line goes between
    // the lanes instead.
    void Arrow(size_t from, size_t to, int branch, bool merged) {
        const D2D1_POINT_2F a = pos_[from], z = pos_[to];
        const DgColor color = Role(Git(branch));
        const bool lr = p_.dir == Dir::LR;
        const float ac = lr ? a.y : a.x, zc = lr ? z.y : z.x;  // across the lanes
        const float aa = lr ? a.x : a.y, za = lr ? z.x : z.y;  // along them
        auto P = [&](float along, float across) { return lr ? D2D1::Point2F(along, across) : D2D1::Point2F(across, along); };
        b_.Begin(a);
        if (ac == zc) {
            b_.LineTo(z);
        } else {
            const float s_across = zc > ac ? 1.f : -1.f, s_along = za > aa ? 1.f : -1.f;
            const float r = std::min({kArc, std::abs(zc - ac) / 2, std::abs(za - aa) / 2});
            const float k = r * 0.5523f;
            float run = merged ? ac : zc;
            if (Blocked(run, aa, za, from, to)) run = ac + s_across * kLane / 2;
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

    // Whether commits other than the ends sit on lane `across` strictly
    // between `a0` and `a1` along it.
    bool Blocked(float across, float a0, float a1, size_t from, size_t to) const {
        const bool lr = p_.dir == Dir::LR;
        const float lo = std::min(a0, a1), hi = std::max(a0, a1);
        for (size_t i = 0; i < pos_.size(); ++i) {
            if (i == from || i == to) continue;
            const float c = lr ? pos_[i].y : pos_[i].x, a = lr ? pos_[i].x : pos_[i].y;
            if (c == across && a > lo && a < hi) return true;
        }
        return false;
    }

    void DrawCommit(size_t i) {
        const Commit& c = p_.commits[i];
        const D2D1_POINT_2F p = pos_[i];
        const DgColor color = Role(Git(c.branch));
        const D2D1_RECT_F r = D2D1::RectF(p.x - kR, p.y - kR, p.x + kR, p.y + kR);
        switch (c.type) {
        case Type::Highlight:
            b_.Rect(r, 0, Role(static_cast<uint8_t>(kDgGitInv + lane_[static_cast<size_t>(c.branch)] % 8)), Role(kDgNone));
            b_.Rect(D2D1::RectF(p.x - 6, p.y - 6, p.x + 6, p.y + 6), 0, Role(kDgPage), Role(kDgNone));
            break;
        case Type::Merge:
            b_.Ellipse(D2D1::RectF(p.x - 9, p.y - 9, p.x + 9, p.y + 9), color, Role(kDgNone));
            b_.Ellipse(D2D1::RectF(p.x - 6, p.y - 6, p.x + 6, p.y + 6), Role(kDgNodeFill), Role(kDgNone));
            break;
        case Type::CherryPick:
            b_.Ellipse(r, Argb(0xFF555555), Role(kDgNone));
            b_.Ellipse(D2D1::RectF(p.x - 5.75f, p.y - 0.75f, p.x - 0.25f, p.y + 4.75f), Argb(0xFFFFFFFF), Role(kDgNone));
            b_.Ellipse(D2D1::RectF(p.x + 0.25f, p.y - 0.75f, p.x + 5.75f, p.y + 4.75f), Argb(0xFFFFFFFF), Role(kDgNone));
            b_.Begin(D2D1::Point2F(p.x - 3, p.y + 1));
            b_.LineTo(D2D1::Point2F(p.x, p.y - 5));
            b_.LineTo(D2D1::Point2F(p.x + 3, p.y + 1));
            b_.End(Role(kDgNone), Argb(0xFFFFFFFF), 1.f);
            break;
        case Type::Reverse:
            b_.Ellipse(r, color, Role(kDgNone));
            b_.Begin(D2D1::Point2F(p.x - 5, p.y - 5));
            b_.LineTo(D2D1::Point2F(p.x + 5, p.y + 5));
            b_.MoveTo(D2D1::Point2F(p.x - 5, p.y + 5));
            b_.LineTo(D2D1::Point2F(p.x + 5, p.y - 5));
            b_.End(Role(kDgNone), Role(kDgPage), 3.f);
            break;
        default: b_.Ellipse(r, color, Role(kDgNone)); break;
        }
    }

    const Parser& p_;
    SceneBuilder& b_;
    std::vector<int> lane_;
    std::vector<D2D1_POINT_2F> pos_;
};

}  // namespace

std::shared_ptr<DiagramScene> GitGraph(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder).Build();
}

}  // namespace mdv::mermaid
