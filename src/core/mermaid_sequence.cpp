// Mermaid sequence diagrams: participants and actors (boxes, stick figures,
// grouped in colored boxes, created and destroyed), messages of every arrow
// kind, activations, notes, loop / alt / opt / par / critical / break / rect
// blocks and autonumbering, laid out like Mermaid does.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

// Messages, notes and blocks: they only stack up (linear work), so more than
// a flowchart's links.
constexpr size_t kMaxItems = 4000;

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;  // in the source
};

struct Participant {
    std::wstring id;
    Text label;
    bool stick = false;  // actor: a stick figure
    int box = -1;
    int created_at = -1;    // item that creates it (it appears there)
    int destroyed_at = -1;  // item after which it ends
};

struct Group {
    Text title;
    DgColor fill;
    std::vector<int> members;
};

enum class Head : uint8_t { None, Arrow, Cross, Open };

enum class ItemKind : uint8_t {
    Message, Note, Activate, Deactivate, BlockStart, BlockSection, BlockEnd, Autonumber,
};

struct Item {
    ItemKind kind = ItemKind::Message;
    int a = -1, b = -1;        // message: from, to; note: first, last participant; activation: participant
    bool dotted = false;
    Head head = Head::Arrow;
    bool both = false;         // <<->>: heads at both ends
    bool activate = false;     // ->>+B
    bool deactivate = false;   // -->>-A (the sender)
    int note_side = 0;         // note: 0 over, 1 left of, 2 right of
    std::wstring block;        // loop, alt, opt, par, critical, break, rect
    DgColor fill;              // rect
    Text text;
    int number_start = 1, number_step = 1;
    bool numbering = true;
};

bool IsArrowStart(wchar_t c) { return c == L'-' || c == L'<'; }

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"sequenceDiagram")) c_.Fail(Tr(L"Expected \"sequenceDiagram\"", L"Ожидалось «sequenceDiagram»"));
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
        if (depth_ > 0) c_.Fail(Tr(L"A block has no \"end\"", L"Блок без «end»"));
        if (open_box_ >= 0) open_box_ = -1;
    }

    std::vector<Participant> people;
    std::vector<Group> groups;
    std::vector<Item> items;
    std::wstring title;
    uint32_t title_pos = 0, title_len = 0;

private:
    void Statement() {
        if (c_.EatKeyword(L"participant")) return Declare(false, -1);
        if (c_.EatKeyword(L"actor")) return Declare(true, -1);
        if (c_.EatKeyword(L"create")) {
            c_.SkipBlanks();
            const bool stick = c_.EatKeyword(L"actor");
            if (!stick && !c_.EatKeyword(L"participant")) c_.Fail(Tr(L"Expected \"participant\"", L"Ожидалось «participant»"));
            return Declare(stick, static_cast<int>(items.size()));
        }
        if (c_.EatKeyword(L"destroy")) {
            c_.SkipBlanks();
            const int p = Person(std::wstring(c_.LineRest()), c_.pos());
            people[static_cast<size_t>(p)].destroyed_at = static_cast<int>(items.size());  // after the next message
            return;
        }
        if (c_.EatKeyword(L"box")) return Box();
        if (c_.EatKeyword(L"end")) {
            if (depth_ > 0) {
                --depth_;
                Item it;
                it.kind = ItemKind::BlockEnd;
                Add(std::move(it));
            } else if (open_box_ >= 0) {
                open_box_ = -1;
            } else {
                c_.Fail(Tr(L"\"end\" without a block", L"«end» без блока"));
            }
            return;
        }
        for (const wchar_t* kw : {L"loop", L"alt", L"opt", L"par", L"critical", L"break", L"rect"}) {
            if (c_.EatKeyword(kw)) return Block(kw);
        }
        for (const wchar_t* kw : {L"else", L"and", L"option"}) {
            if (c_.EatKeyword(kw)) {
                if (depth_ == 0) c_.Fail(std::wstring(L"\"") + kw + Tr(L"\" outside a block", L"» вне блока"));
                Item it;
                it.kind = ItemKind::BlockSection;
                it.text = Rest();
                Add(std::move(it));
                return;
            }
        }
        const bool activate = c_.EatKeyword(L"activate");
        if (activate || c_.EatKeyword(L"deactivate")) {
            const bool on = activate;
            c_.SkipBlanks();
            Item it;
            it.kind = on ? ItemKind::Activate : ItemKind::Deactivate;
            it.a = Person(std::wstring(c_.LineRest()), c_.pos());
            Add(std::move(it));
            return;
        }
        if (c_.EatKeyword(L"note")) return Note();
        if (c_.EatKeyword(L"autonumber")) {
            c_.SkipBlanks();
            Item it;
            it.kind = ItemKind::Autonumber;
            const std::wstring rest(c_.LineRest());
            if (EqualsNoCase(rest, L"off")) {
                it.numbering = false;
            } else if (!rest.empty()) {
                wchar_t* end = nullptr;
                it.number_start = static_cast<int>(std::wcstol(rest.c_str(), &end, 10));
                if (end && *end) it.number_step = static_cast<int>(std::wcstol(end, nullptr, 10));
                if (it.number_step == 0) it.number_step = 1;
            }
            Add(std::move(it));
            return;
        }
        if (c_.EatKeyword(L"title")) {
            c_.SkipBlanks();
            c_.Eat(L':');
            const std::wstring_view t = Trim(c_.LineRest());
            title = std::wstring(t);
            title_pos = static_cast<uint32_t>(t.data() - src_.text.data());
            title_len = static_cast<uint32_t>(t.size());
            return;
        }
        for (const wchar_t* kw : {L"accTitle", L"accDescr", L"link", L"links", L"properties", L"details"}) {
            if (c_.EatKeyword(kw)) {
                c_.LineRest();
                return;
            }
        }
        Message();
    }

    Text Rest() {
        c_.SkipBlanks();
        Text t;
        t.pos = static_cast<uint32_t>(c_.pos());
        const std::wstring_view r = c_.LineRest();
        t.raw = std::wstring(r);
        t.len = static_cast<uint32_t>(r.size());
        if (t.raw.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return t;
    }

    void Add(Item it) {
        if (items.size() >= kMaxItems) c_.Fail(Tr(L"Too many messages", L"Слишком много сообщений"));
        items.push_back(std::move(it));
    }

    int Person(std::wstring id, size_t pos) {
        id = std::wstring(Trim(id));
        if (id.empty()) mermaid::Fail(pos, Tr(L"Expected a participant", L"Ожидался участник"));
        auto it = ids_.find(id);
        if (it != ids_.end()) return it->second;
        if (people.size() >= kMaxNodes) mermaid::Fail(pos, Tr(L"Too many participants", L"Слишком много участников"));
        Participant p;
        p.id = id;
        p.label.raw = id;
        // Named first here: that's its text for search.
        if (const size_t at = src_.text.find(id, pos); at != std::wstring_view::npos && at - pos < 64) {
            p.label.pos = static_cast<uint32_t>(at);
            p.label.len = static_cast<uint32_t>(id.size());
        }
        p.box = open_box_;
        if (open_box_ >= 0) groups[static_cast<size_t>(open_box_)].members.push_back(static_cast<int>(people.size()));
        ids_.emplace(id, static_cast<int>(people.size()));
        people.push_back(std::move(p));
        return static_cast<int>(people.size()) - 1;
    }

    // participant A [as Label]
    void Declare(bool stick, int created_at) {
        c_.SkipBlanks();
        const size_t start = c_.pos();
        while (!c_.AtStatementEnd() && !IsSpace(c_.Peek())) c_.Advance();
        std::wstring id(src_.text.substr(start, c_.pos() - start));
        // participant A@{ "type": "..." } (v11): the part before @ names it.
        if (const size_t at = id.find(L'@'); at != std::wstring::npos) id.resize(at);
        const int p = Person(id, start);
        Participant& person = people[static_cast<size_t>(p)];
        person.stick = stick;
        person.created_at = created_at;
        c_.SkipBlanks();
        if (c_.EatKeyword(L"as")) {
            person.label = Rest();
        } else {
            person.label.raw = person.id;
            person.label.pos = static_cast<uint32_t>(start);
            person.label.len = static_cast<uint32_t>(id.size());
            c_.LineRest();  // v11 metadata and such
        }
    }

    // box [color] [title]
    void Box() {
        c_.SkipBlanks();
        Group g;
        const size_t start = c_.pos();
        std::wstring_view rest = c_.LineRest();
        // A color first: a name, #hex, rgb(...), or "transparent".
        size_t split = 0;
        if (StartsWithNoCase(rest, L"rgb") || StartsWithNoCase(rest, L"hsl")) {
            const size_t close = rest.find(L')');
            split = close == std::wstring_view::npos ? rest.size() : close + 1;
        } else {
            split = rest.find_first_of(L" \t");
            if (split == std::wstring_view::npos) split = rest.size();
        }
        uint32_t argb = 0;
        if (split > 0 && ParseCssColor(rest.substr(0, split), &argb)) {
            // Opaque colors are toned down: the messages run across the box.
            g.fill = Argb((argb >> 24) == 0xFF ? (argb & 0xFFFFFF) | 0x40000000u : argb);
            rest = rest.substr(split);
        } else {
            split = 0;
        }
        const std::wstring_view t = Trim(rest);
        g.title.raw = std::wstring(t);
        g.title.pos = static_cast<uint32_t>(t.data() - src_.text.data());
        (void)start;
        g.title.len = static_cast<uint32_t>(t.size());
        open_box_ = static_cast<int>(groups.size());
        groups.push_back(std::move(g));
    }

    void Block(const wchar_t* kind) {
        if (depth_ >= kMaxNesting) c_.Fail(Tr(L"Blocks nested too deep", L"Слишком глубокая вложенность блоков"));
        ++depth_;
        Item it;
        it.kind = ItemKind::BlockStart;
        it.block = kind;
        if (it.block == L"rect") {
            c_.SkipBlanks();
            const std::wstring_view rest = c_.LineRest();
            uint32_t argb = 0;
            it.fill = ParseCssColor(rest, &argb) ? Argb(argb) : Role(kDgBoxFill);
        } else {
            it.text = Rest();
        }
        Add(std::move(it));
    }

    // Note left of A: text | Note over A,B: text
    void Note() {
        c_.SkipBlanks();
        Item it;
        it.kind = ItemKind::Note;
        if (c_.EatKeyword(L"left")) {
            it.note_side = 1;
        } else if (c_.EatKeyword(L"right")) {
            it.note_side = 2;
        } else if (!c_.EatKeyword(L"over")) {
            c_.Fail(Tr(L"Expected \"left of\", \"right of\" or \"over\"", L"Ожидалось «left of», «right of» или «over»"));
        }
        c_.SkipBlanks();
        if (it.note_side != 0 && !c_.EatKeyword(L"of")) c_.Fail(Tr(L"Expected \"of\"", L"Ожидалось «of»"));
        c_.SkipBlanks();
        const size_t start = c_.pos();
        while (!c_.AtStatementEnd() && c_.Peek() != L':') c_.Advance();
        const std::wstring_view who = src_.text.substr(start, c_.pos() - start);
        const size_t comma = who.find(L',');
        it.a = Person(std::wstring(who.substr(0, comma)), start);
        it.b = comma == std::wstring_view::npos ? it.a : Person(std::wstring(who.substr(comma + 1)), start + comma + 1);
        if (!c_.Eat(L':')) c_.Fail(Tr(L"Expected ':'", L"Ожидалось «:»"));
        it.text = Rest();
        Add(std::move(it));
    }

    // A ->> +B: text
    void Message() {
        const size_t start = c_.pos();
        while (!c_.AtStatementEnd() && !IsArrowStart(c_.Peek()) && c_.Peek() != L':') c_.Advance();
        const std::wstring from(src_.text.substr(start, c_.pos() - start));
        struct Arrow {
            const wchar_t* token;
            bool dotted;
            Head head;
            bool both;
        };
        static constexpr Arrow kArrows[] = {
            {L"<<-->>", true, Head::Arrow, true}, {L"<<->>", false, Head::Arrow, true},
            {L"-->>", true, Head::Arrow, false},  {L"->>", false, Head::Arrow, false},
            {L"--x", true, Head::Cross, false},   {L"-x", false, Head::Cross, false},
            {L"--)", true, Head::Open, false},    {L"-)", false, Head::Open, false},
            {L"-->", true, Head::None, false},    {L"->", false, Head::None, false},
        };
        const Arrow* arrow = nullptr;
        for (const Arrow& a : kArrows) {
            if (c_.EatStr(a.token)) {
                arrow = &a;
                break;
            }
        }
        if (!arrow || Trim(from).empty()) c_.Fail(Tr(L"Expected a message (A->>B: text)", L"Ожидалось сообщение (A->>B: текст)"));
        Item it;
        it.kind = ItemKind::Message;
        it.dotted = arrow->dotted;
        it.head = arrow->head;
        it.both = arrow->both;
        c_.SkipBlanks();
        if (c_.Eat(L'+')) it.activate = true;
        else if (c_.Eat(L'-')) it.deactivate = true;
        c_.SkipBlanks();
        const size_t to_start = c_.pos();
        while (!c_.AtStatementEnd() && c_.Peek() != L':') c_.Advance();
        const std::wstring to(src_.text.substr(to_start, c_.pos() - to_start));
        it.a = Person(from, start);
        it.b = Person(to, to_start);
        if (c_.Eat(L':')) it.text = Rest();
        Add(std::move(it));
    }

    const Source& src_;
    Cursor c_;
    std::unordered_map<std::wstring, int> ids_;
    int depth_ = 0;
    int open_box_ = -1;
};

// ---------------------------------------------------------------------------
// Layout and drawing
// ---------------------------------------------------------------------------

constexpr float kActorW = 150, kActorH = 65;   // Mermaid's sequence defaults
constexpr float kActorGap = 50;
constexpr float kMarginX = 50, kMarginY = 10;
constexpr float kMessageGap = 10;
constexpr float kNoteMin = 150;
constexpr float kBoxMargin = 10;
constexpr float kActivationW = 10;
constexpr float kSelfLoop = 30;                // width of a message to itself
constexpr float kSmall = 0.875f;               // actors and notes: 14px

class Builder {
public:
    Builder(Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src) {}

    std::shared_ptr<DiagramScene> Build() {
        const size_t n = p_.people.size();
        if (n == 0) return b_.Finish(1, 1);
        Measure();
        PlaceParticipants();
        Walk();
        return Draw();
    }

private:
    using M = SceneBuilder::Measured;

    void Measure() {
        const size_t n = p_.people.size();
        names_.resize(n);
        widths_.resize(n);
        for (size_t i = 0; i < n; ++i) {
            names_[i] = b_.Measure(ParseLabel(p_.people[i].label.raw, false), 0, false, kSmall);
            widths_[i] = std::max(kActorW, names_[i].w + 24);
        }
        texts_.resize(p_.items.size());
        for (size_t i = 0; i < p_.items.size(); ++i) {
            const Item& it = p_.items[i];
            if (it.text.raw.empty()) continue;
            const bool note = it.kind == ItemKind::Note;
            texts_[i] = b_.Measure(ParseLabel(it.text.raw, false), 0, false, note ? kSmall : 1.f);
        }
        for (const Group& g : p_.groups) {
            box_titles_.push_back(g.title.raw.empty() ? M{} : b_.Measure(ParseLabel(g.title.raw, false), 0, true, kSmall));
        }
        // The title statement or the front matter's, 18px as in Mermaid.
        const bool own = !p_.title.empty();
        const std::wstring& title = own ? p_.title : src_.title;
        title_pos_ = own ? p_.title_pos : src_.title_pos;
        title_len_ = own ? p_.title_len : src_.title_len;
        if (!title.empty()) title_ = b_.Measure(ParseLabel(title, false), 0, false, 1.125f);
    }

    // Centers left to right: gaps as wide as the messages and notes between
    // neighbours need.
    void PlaceParticipants() {
        const size_t n = p_.people.size();
        std::vector<float> gap(n, 0.f);  // after i: center distance to i+1
        for (size_t i = 0; i + 1 < n; ++i) gap[i] = widths_[i] / 2 + kActorGap + widths_[i + 1] / 2;
        std::vector<float> left_need(n, 0.f), right_need(n, 0.f);  // room beside the first and last
        struct Span {
            size_t a, b;
            float need;
        };
        std::vector<Span> spans;
        for (size_t k = 0; k < p_.items.size(); ++k) {
            const Item& it = p_.items[k];
            const float tw = texts_[k].layout ? texts_[k].w : 0.f;
            if (it.kind == ItemKind::Message) {
                if (it.a == it.b) {
                    // To itself: text centered over the loop, as in Mermaid.
                    const size_t i = static_cast<size_t>(it.a);
                    const float right = std::max(tw / 2, kSelfLoop + 10) + 20;
                    if (i + 1 < n) spans.push_back({i, i + 1, right});
                    else right_need[i] = std::max(right_need[i], right);
                    if (i > 0) spans.push_back({i - 1, i, tw / 2 + 20});
                    else left_need[i] = std::max(left_need[i], tw / 2);
                } else {
                    const size_t a = static_cast<size_t>(std::min(it.a, it.b)), b = static_cast<size_t>(std::max(it.a, it.b));
                    spans.push_back({a, b, tw + 40});
                }
            } else if (it.kind == ItemKind::Note) {
                const float w = std::max(kNoteMin, tw + 20);
                const size_t a = static_cast<size_t>(std::min(it.a, it.b)), b = static_cast<size_t>(std::max(it.a, it.b));
                if (it.note_side == 2) {
                    if (a + 1 < n) spans.push_back({a, a + 1, w + 2 * kMessageGap + widths_[a + 1] / 2});
                    else right_need[a] = std::max(right_need[a], w + kMessageGap);
                } else if (it.note_side == 1) {
                    if (a > 0) spans.push_back({a - 1, a, w + 2 * kMessageGap + widths_[a - 1] / 2});
                    else left_need[a] = std::max(left_need[a], w + kMessageGap);
                } else if (a == b) {
                    if (a + 1 < n) spans.push_back({a, a + 1, w / 2 + kMessageGap + widths_[a + 1] / 2});
                    if (a > 0) spans.push_back({a - 1, a, w / 2 + kMessageGap + widths_[a - 1] / 2});
                    left_need[a] = std::max(left_need[a], w / 2);
                    right_need[a] = std::max(right_need[a], w / 2);
                } else {
                    spans.push_back({a, b, w - 20});
                }
            }
        }
        // Adjacent spans first, then longer ones take what they still lack
        // from their last gap.
        std::stable_sort(spans.begin(), spans.end(), [](const Span& x, const Span& y) { return x.b - x.a < y.b - y.a; });
        for (const Span& s : spans) {
            float have = 0;
            for (size_t i = s.a; i < s.b; ++i) have += gap[i];
            if (have < s.need) gap[s.b - 1] += s.need - have;
        }
        centers_.assign(n, 0.f);
        centers_[0] = std::max(kMarginX + widths_[0] / 2, kMarginX + left_need[0]);
        for (size_t i = 1; i < n; ++i) centers_[i] = centers_[i - 1] + gap[i - 1];
        right_ = centers_[n - 1] + std::max(widths_[n - 1] / 2, right_need[n - 1]);
    }

    struct Frame {  // an open block
        size_t item;
        float top;
        float x0 = 1e30f, x1 = -1e30f;
        std::vector<std::pair<float, size_t>> sections;  // y, item
        int depth;
    };

    void Extend(float x0, float x1) {
        for (Frame& f : frames_) {
            f.x0 = std::min(f.x0, x0);
            f.x1 = std::max(f.x1, x1);
        }
    }

    // Everything top down: messages, notes, activations, blocks.
    void Walk() {
        const size_t n = p_.people.size();
        float y = kMarginY + (title_.layout ? title_.h + 10 : 0.f);
        // Room above the participants for the titles of their boxes.
        float band = 0;
        for (const M& t : box_titles_) band = std::max(band, t.layout ? t.h + 8 : 6.f);
        y += band;
        top_ = y;
        y += kActorH;
        // A created participant appears (its box) at its creating message.
        appear_.assign(n, top_);
        end_.assign(n, -1.f);
        std::vector<std::vector<std::pair<float, int>>> active(n);  // open activations: (top, level)
        int number = 1, step = 1;
        bool numbering = false;
        for (size_t k = 0; k < p_.items.size(); ++k) {
            const Item& it = p_.items[k];
            const M& text = texts_[k];
            switch (it.kind) {
            case ItemKind::Message: {
                const size_t a = static_cast<size_t>(it.a), b = static_cast<size_t>(it.b);
                y += kMessageGap;
                const float th = text.layout ? text.h : 0.f;
                Placed m;
                m.item = k;
                m.text_y = y;
                m.y = y + th + 4;
                m.number = numbering ? number : 0;
                if (numbering) number += step;
                // The new participant's box stands where the message arrives.
                if (p_.people[b].created_at >= 0 && static_cast<size_t>(p_.people[b].created_at) <= k &&
                    appear_[b] == top_ && created_done_.insert(b).second) {
                    appear_[b] = m.y - kActorH / 2;
                    m.to_box = true;
                }
                m.x0 = Edge(a, active[a], b > a ? 1 : -1);
                m.x1 = Edge(b, active[b], a > b ? 1 : -1);
                if (m.to_box) m.x1 = centers_[b] + (a < b ? -widths_[b] / 2 : widths_[b] / 2);
                y = m.y + (a == b ? 20.f : 0.f) + (m.to_box ? kActorH / 2 : 0.f);
                if (a == b) Extend(m.x0 - text.w / 2 - 10, m.x0 + std::max(text.w / 2, kSelfLoop + 10) + 10);
                else Extend(std::min(centers_[a], centers_[b]) - 20, std::max(centers_[a], centers_[b]) + 20);
                messages_.push_back(m);
                if (it.activate) active[b].push_back({m.y, static_cast<int>(active[b].size())});
                if (it.deactivate && !active[a].empty()) {
                    activations_.push_back({a, active[a].back().first, m.y, active[a].back().second});
                    active[a].pop_back();
                }
                for (size_t d = 0; d < n; ++d) {
                    if (p_.people[d].destroyed_at >= 0 && static_cast<size_t>(p_.people[d].destroyed_at) <= k && end_[d] < 0) {
                        end_[d] = m.y;
                    }
                }
                break;
            }
            case ItemKind::Note: {
                const size_t a = static_cast<size_t>(std::min(it.a, it.b)), b = static_cast<size_t>(std::max(it.a, it.b));
                const float w = std::max(kNoteMin, (text.layout ? text.w : 0.f) + 20);
                const float h = (text.layout ? text.h : 0.f) + 20;
                y += kMessageGap;
                PlacedNote nt;
                nt.item = k;
                if (it.note_side == 1) nt.x = centers_[a] - kMessageGap - w;
                else if (it.note_side == 2) nt.x = centers_[a] + kMessageGap;
                else if (a == b) nt.x = centers_[a] - w / 2;
                else {
                    const float l = centers_[a] - 25, r = centers_[b] + 25;
                    nt.x = std::min(l, (l + r) / 2 - w / 2);
                    nt.w = std::max(w, r - l);
                }
                if (nt.w == 0) nt.w = w;
                nt.y = y;
                nt.h = h;
                y += h;
                Extend(nt.x, nt.x + nt.w);
                notes_.push_back(nt);
                break;
            }
            case ItemKind::Activate:
                active[static_cast<size_t>(it.a)].push_back({y + 2, static_cast<int>(active[static_cast<size_t>(it.a)].size())});
                break;
            case ItemKind::Deactivate: {
                auto& st = active[static_cast<size_t>(it.a)];
                if (!st.empty()) {
                    activations_.push_back({static_cast<size_t>(it.a), st.back().first, std::max(y, st.back().first + 10), st.back().second});
                    st.pop_back();
                }
                break;
            }
            case ItemKind::BlockStart: {
                y += kBoxMargin;
                Frame f;
                f.item = k;
                f.top = y;
                f.depth = static_cast<int>(frames_.size());
                frames_.push_back(f);
                y += it.block == L"rect" ? 4.f : 24.f + (text.layout ? std::max(0.f, text.h - 18) : 0.f);
                break;
            }
            case ItemKind::BlockSection:
                if (!frames_.empty()) {
                    y += kBoxMargin;
                    frames_.back().sections.push_back({y, k});
                    y += 4 + (text.layout ? text.h : 0.f);
                }
                break;
            case ItemKind::BlockEnd:
                if (!frames_.empty()) {
                    y += kBoxMargin;
                    Frame f = frames_.back();
                    frames_.pop_back();
                    if (f.x0 > f.x1) {  // an empty block: across everything
                        f.x0 = centers_.front() - 20;
                        f.x1 = centers_.back() + 20;
                    }
                    const float pad = 12.f;
                    PlacedFrame pf{f.item, f.x0 - pad, f.top, f.x1 + pad, y, f.sections};
                    closed_.push_back(pf);
                    // The outer blocks hold this one.
                    Extend(pf.x0 - 6, pf.x1 + 6);
                    y += 2;
                }
                break;
            case ItemKind::Autonumber:
                numbering = it.numbering;
                if (it.numbering) {
                    number = it.number_start;
                    step = it.number_step;
                }
                break;
            }
        }
        // Unclosed activations run to the end.
        y += kMessageGap * 2;
        for (size_t i = 0; i < n; ++i) {
            for (const auto& [top, level] : active[i]) activations_.push_back({i, top, y, level});
        }
        bottom_ = y;
    }

    // Where a message leaves / meets participant i's lifeline (the side of
    // its open activation facing the other end).
    float Edge(size_t i, const std::vector<std::pair<float, int>>& act, int toward) const {
        float x = centers_[i];
        if (!act.empty()) x += toward * (kActivationW / 2 + static_cast<float>(act.back().second) * kActivationW / 2);
        return x;
    }

    void ActorBox(size_t i, float top) {
        const Participant& p = p_.people[i];
        const float cx = centers_[i], w = widths_[i];
        const M& name = names_[i];
        if (p.stick) {
            // A stick figure, its name below.
            const float hy = top + 12;
            b_.Ellipse(D2D1::RectF(cx - 11, hy - 11, cx + 11, hy + 11), Role(kDgActorFill), Role(kDgActorStroke), 1.5f);
            b_.Begin(D2D1::Point2F(cx, hy + 11));
            b_.LineTo(D2D1::Point2F(cx, top + 42));
            b_.MoveTo(D2D1::Point2F(cx - 16, top + 29));
            b_.LineTo(D2D1::Point2F(cx + 16, top + 29));
            b_.MoveTo(D2D1::Point2F(cx - 14, top + 58));
            b_.LineTo(D2D1::Point2F(cx, top + 42));
            b_.LineTo(D2D1::Point2F(cx + 14, top + 58));
            b_.End(Role(kDgNone), Role(kDgActorStroke), 1.5f);
            b_.Text(name, D2D1::Point2F(cx - name.w / 2, top + kActorH - 2), Role(kDgText), p.label.pos, p.label.len);
            return;
        }
        b_.Rect(D2D1::RectF(cx - w / 2, top, cx + w / 2, top + kActorH), 3, Role(kDgActorFill), Role(kDgActorStroke), 1.f);
        b_.Text(name, D2D1::Point2F(cx - name.w / 2, top + (kActorH - name.h) / 2), Role(kDgText), p.label.pos, p.label.len);
    }

    std::shared_ptr<DiagramScene> Draw() {
        const size_t n = p_.people.size();
        const float actors_bottom = bottom_ + kActorH + (IsStickAny() ? 18.f : 0.f);
        float width = right_ + kMarginX;
        for (const PlacedFrame& f : closed_) width = std::max(width, f.x1 + 20);
        for (const PlacedNote& nt : notes_) width = std::max(width, nt.x + nt.w + 20);
        float x_shift = 0;  // things left of the margin move everything right
        float min_x = kMarginX;
        for (const PlacedFrame& f : closed_) min_x = std::min(min_x, f.x0 - 10);
        for (const PlacedNote& nt : notes_) min_x = std::min(min_x, nt.x - 10);
        if (min_x < 10) x_shift = 10 - min_x;
        width += x_shift;
        if (title_.layout) width = std::max(width, title_.w + 2 * kMarginX);
        const float height = actors_bottom + kMarginY;
        shift_ = x_shift;
        for (float& c : centers_) c += x_shift;
        for (Placed& m : messages_) {
            m.x0 += x_shift;
            m.x1 += x_shift;
        }
        for (PlacedNote& nt : notes_) nt.x += x_shift;
        for (PlacedFrame& f : closed_) {
            f.x0 += x_shift;
            f.x1 += x_shift;
        }

        if (title_.layout) b_.Text(title_, D2D1::Point2F((width - title_.w) / 2, kMarginY), Role(kDgText), title_pos_, title_len_);
        // Group boxes behind everything.
        for (size_t g = 0; g < p_.groups.size(); ++g) {
            const Group& grp = p_.groups[g];
            if (grp.members.empty()) continue;
            float x0 = 1e30f, x1 = -1e30f;
            for (int m : grp.members) {
                x0 = std::min(x0, centers_[static_cast<size_t>(m)] - widths_[static_cast<size_t>(m)] / 2);
                x1 = std::max(x1, centers_[static_cast<size_t>(m)] + widths_[static_cast<size_t>(m)] / 2);
            }
            const M& t = box_titles_[g];
            const float top = top_ - (t.layout ? t.h + 8 : 6.f);
            b_.Rect(D2D1::RectF(x0 - 8, top, x1 + 8, actors_bottom + 6), 0,
                    grp.fill.role == kDgNone ? Role(kDgNone) : grp.fill, Role(kDgNone));
            if (t.layout) b_.Text(t, D2D1::Point2F((x0 + x1) / 2 - t.w / 2, top + 4), Role(kDgText), grp.title.pos, grp.title.len);
        }
        // rect blocks: colored backgrounds.
        for (const PlacedFrame& f : closed_) {
            const Item& it = p_.items[f.item];
            if (it.block == L"rect") b_.Rect(D2D1::RectF(f.x0, f.top, f.x1, f.bottom), 0, it.fill, Role(kDgNone));
        }
        // Lifelines.
        for (size_t i = 0; i < n; ++i) {
            const float top = appear_[i] + kActorH + (p_.people[i].stick ? names_[i].h - 2 : 0.f);  // below a figure's name
            const float bottom = end_[i] >= 0 ? end_[i] : bottom_;
            b_.Begin(D2D1::Point2F(centers_[i], top));
            b_.LineTo(D2D1::Point2F(centers_[i], bottom));
            b_.End(Role(kDgNone), Role(kDgMuted), 1.f);
        }
        // Activations.
        for (const Activation& a : activations_) {
            const float cx = centers_[a.who] + static_cast<float>(a.level) * kActivationW / 2;
            b_.Rect(D2D1::RectF(cx - kActivationW / 2, a.top, cx + kActivationW / 2, a.bottom), 0, Role(kDgActivationFill),
                    Role(kDgActivationStroke), 1.f);
        }
        // Blocks: frames, tabs, section lines.
        for (const PlacedFrame& f : closed_) {
            const Item& it = p_.items[f.item];
            if (it.block == L"rect") continue;
            b_.Rect(D2D1::RectF(f.x0, f.top, f.x1, f.bottom), 0, Role(kDgNone), Role(kDgActorStroke), 1.5f, 1);
            const M kind = b_.Measure(ParseLabel(it.block, false), 0, true, kSmall);
            const float tab_w = kind.w + 16, tab_h = 20;
            b_.Polygon({{f.x0, f.top}, {f.x0 + tab_w, f.top}, {f.x0 + tab_w, f.top + tab_h - 6}, {f.x0 + tab_w - 6, f.top + tab_h},
                        {f.x0, f.top + tab_h}},
                       Role(kDgBoxFill), Role(kDgActorStroke), 1.f);
            b_.Text(kind, D2D1::Point2F(f.x0 + 8, f.top + (tab_h - kind.h) / 2), Role(kDgText));
            const M& cond = texts_[f.item];
            if (cond.layout) {
                const float cx = std::max((f.x0 + f.x1) / 2, f.x0 + tab_w + 8 + cond.w / 2);
                BracketText(cond, cx, f.top + 2, it.text);
            }
            for (const auto& [sy, si] : f.sections) {
                b_.Begin(D2D1::Point2F(f.x0, sy));
                b_.LineTo(D2D1::Point2F(f.x1, sy));
                b_.End(Role(kDgNone), Role(kDgActorStroke), 1.5f, 1);
                const M& st = texts_[si];
                if (st.layout) BracketText(st, (f.x0 + f.x1) / 2, sy + 3, p_.items[si].text);
            }
        }
        // Participants, top and (mirrored) bottom.
        for (size_t i = 0; i < n; ++i) {
            ActorBox(i, appear_[i]);
            if (end_[i] < 0) ActorBox(i, bottom_);
            else {
                // Destroyed: an X ends its lifeline.
                const float cx = centers_[i], yy = end_[i];
                b_.Begin(D2D1::Point2F(cx - 9, yy - 9));
                b_.LineTo(D2D1::Point2F(cx + 9, yy + 9));
                b_.MoveTo(D2D1::Point2F(cx - 9, yy + 9));
                b_.LineTo(D2D1::Point2F(cx + 9, yy - 9));
                b_.End(Role(kDgNone), Role(kDgLine), 2.f);
            }
        }
        // Messages.
        for (const Placed& m : messages_) {
            const Item& it = p_.items[m.item];
            const uint8_t dash = it.dotted ? 2 : 0;
            const Marker head = it.head == Head::Arrow ? Marker::Arrow
                                : it.head == Head::Cross ? Marker::Cross
                                : it.head == Head::Open ? Marker::LineArrow
                                                        : Marker::None;
            const M& text = texts_[m.item];
            if (it.a == it.b) {
                // To itself: a loop on the right.
                const float x = m.x0, y0 = m.y, y1 = m.y + 20;
                const D2D1_POINT_2F end = D2D1::Point2F(x + 2, y1);
                b_.Begin(D2D1::Point2F(x, y0));
                b_.CubicTo(D2D1::Point2F(x + kSelfLoop + 10, y0 - 6), D2D1::Point2F(x + kSelfLoop + 10, y1 + 6),
                           D2D1::Point2F(end.x + (head != Marker::None ? 8.f : 0.f), end.y));
                b_.End(Role(kDgNone), Role(kDgLine), 1.5f, dash);
                if (head != Marker::None) AddMarker(b_, head, end, D2D1::Point2F(end.x + 10, end.y), Role(kDgLine), 1.5f);
                if (text.layout) b_.Text(text, D2D1::Point2F(x - text.w / 2, m.text_y), Role(kDgText), it.text.pos, it.text.len);
            } else {
                const float dir = m.x1 > m.x0 ? 1.f : -1.f;
                const float back = head == Marker::None ? 0.f : MarkerLength(head);
                D2D1_POINT_2F a = D2D1::Point2F(m.x0, m.y), b = D2D1::Point2F(m.x1 - dir * back, m.y);
                if (it.both) a.x += dir * MarkerLength(Marker::Arrow);
                b_.Begin(a);
                b_.LineTo(b);
                b_.End(Role(kDgNone), Role(kDgLine), 1.5f, dash);
                if (head != Marker::None) AddMarker(b_, head, D2D1::Point2F(m.x1, m.y), D2D1::Point2F(m.x0, m.y), Role(kDgLine), 1.5f);
                if (it.both) AddMarker(b_, Marker::Arrow, D2D1::Point2F(m.x0, m.y), D2D1::Point2F(m.x1, m.y), Role(kDgLine), 1.5f);
                if (text.layout) {
                    b_.Text(text, D2D1::Point2F((m.x0 + m.x1) / 2 - text.w / 2, m.text_y), Role(kDgText), it.text.pos, it.text.len);
                }
            }
            if (m.number > 0) {
                const float cx = m.x0, cy = m.y;
                b_.Ellipse(D2D1::RectF(cx - 9, cy - 9, cx + 9, cy + 9), Role(kDgLine), Role(kDgLine), 1.f);
                const M num = b_.Measure(ParseLabel(std::to_wstring(m.number), false), 0, true, 0.7f);
                b_.Text(num, D2D1::Point2F(cx - num.w / 2, cy - num.h / 2), Role(kDgPage));
            }
        }
        // Notes on top.
        for (const PlacedNote& nt : notes_) {
            const Item& it = p_.items[nt.item];
            b_.Rect(D2D1::RectF(nt.x, nt.y, nt.x + nt.w, nt.y + nt.h), 0, Role(kDgNoteFill), Role(kDgNoteStroke), 1.f);
            const M& text = texts_[nt.item];
            if (text.layout) {
                b_.Text(text, D2D1::Point2F(nt.x + (nt.w - text.w) / 2, nt.y + 10), Role(kDgNoteText), it.text.pos, it.text.len);
            }
        }
        return b_.Finish(width, height);
    }

    bool IsStickAny() const {
        return std::any_of(p_.people.begin(), p_.people.end(), [](const Participant& p) { return p.stick; });
    }

    // "[condition]" centered at cx.
    void BracketText(const M& t, float cx, float top, const Text& src) {
        const M open = b_.Measure(ParseLabel(L"[", false), 0, false, 1.f);
        const M close = b_.Measure(ParseLabel(L"]", false), 0, false, 1.f);
        const float w = open.w + t.w + close.w;
        const float x = cx - w / 2;
        b_.Text(open, D2D1::Point2F(x, top), Role(kDgText));
        b_.Text(t, D2D1::Point2F(x + open.w, top), Role(kDgText), src.pos, src.len);
        b_.Text(close, D2D1::Point2F(x + open.w + t.w, top), Role(kDgText));
    }

    struct Placed {
        size_t item = 0;
        float x0 = 0, x1 = 0, y = 0, text_y = 0;
        int number = 0;
        bool to_box = false;
    };
    struct PlacedNote {
        size_t item = 0;
        float x = 0, y = 0, w = 0, h = 0;
    };
    struct PlacedFrame {
        size_t item;
        float x0, top, x1, bottom;
        std::vector<std::pair<float, size_t>> sections;
    };
    struct Activation {
        size_t who;
        float top, bottom;
        int level;
    };

    Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
    std::vector<M> names_, texts_, box_titles_;
    M title_;
    uint32_t title_pos_ = 0, title_len_ = 0;
    std::vector<float> widths_, centers_, appear_, end_;
    float right_ = 0, top_ = 0, bottom_ = 0, shift_ = 0;
    std::vector<Frame> frames_;
    std::vector<Placed> messages_;
    std::vector<PlacedNote> notes_;
    std::vector<PlacedFrame> closed_;
    std::vector<Activation> activations_;
    std::set<size_t> created_done_;
};

}  // namespace

std::shared_ptr<DiagramScene> Sequence(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    Builder b(p, *builder, src);
    return b.Build();
}

}  // namespace mdv::mermaid
