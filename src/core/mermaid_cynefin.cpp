// Mermaid cynefin diagrams (cynefin-beta): the framework's four domains as
// quadrants (complex, complicated, chaotic, clear) with confusion in the
// middle, their names and models, items as badges in them, transitions as
// curved arrows between them; drawn as Mermaid's cynefin renderer does, the
// boundaries with its seeded jitter (the seed from the source here).
#include <algorithm>
#include <cmath>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

// Domains in Mermaid's drawing order.
enum Domain { kComplex, kComplicated, kChaotic, kClear, kConfusion, kDomains };
constexpr const wchar_t* kDomainNames[kDomains] = {L"complex", L"complicated", L"chaotic", L"clear", L"confusion"};

struct Transition {
    int from = 0, to = 0;
    Text label;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"cynefin-beta")) c_.Fail(Tr(L"Expected \"cynefin-beta\"", L"Ожидалось «cynefin-beta»"));
        c_.Eat(L':');
        int current = -1;  // the domain whose items follow
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            if (c_.EatKeyword(L"title", true)) {
                c_.SkipBlanks();
                std::wstring_view t = c_.WholeLine();
                if (const size_t comment = t.find(L"%%"); comment != std::wstring_view::npos) t = Trim(t.substr(0, comment));
                title = Text{std::wstring(t), static_cast<uint32_t>(t.data() - src_.text.data()), static_cast<uint32_t>(t.size())};
                continue;
            }
            if (c_.EatKeyword(L"accTitle", true)) {
                c_.WholeLine();
                continue;
            }
            if (c_.EatKeyword(L"accDescr", true)) {
                c_.SkipBlanks();
                if (c_.Eat(L'{')) {
                    while (!c_.AtEnd() && c_.Peek() != L'}') c_.Advance();
                    c_.Eat(L'}');
                } else {
                    c_.WholeLine();
                }
                continue;
            }
            if (c_.Peek() == L'"' || c_.Peek() == L'\'') {
                // An item of the domain above.
                const size_t at = c_.pos();
                const Text t = Quoted();
                if (current < 0) {
                    c_.set_pos(at);
                    c_.Fail(Tr(L"An item outside a domain", L"Элемент вне домена"));
                }
                if (++count_ > kMaxNodes) c_.Fail(Tr(L"Too many items", L"Слишком много элементов"));
                items[current].push_back(t);
                continue;
            }
            const size_t at = c_.pos();
            const int d = DomainName();
            if (d < 0) c_.Fail(Tr(L"Expected complex, complicated, clear, chaotic or confusion", L"Ожидалось complex, complicated, clear, chaotic или confusion"));
            c_.SkipBlanks();
            if (c_.EatStr(L"-->")) {
                // from --> to : "label" (one to itself left out, as in Mermaid).
                c_.SkipBlanks();
                Transition t;
                t.from = d;
                t.to = DomainName();
                if (t.to < 0) c_.Fail(Tr(L"Expected a domain", L"Ожидался домен"));
                c_.SkipBlanks();
                if (c_.Eat(L':')) {
                    c_.SkipBlanks();
                    t.label = Quoted();
                }
                c_.SkipBlanks();
                if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
                if (t.from != t.to) {
                    if (transitions.size() >= kMaxLines) Fail(at, TooMany(L"transitions", L"переходов", kMaxLines));
                    transitions.push_back(std::move(t));
                }
                current = -1;
                continue;
            }
            // A domain block: its items follow (a later one replaces them).
            current = d;
            items[d].clear();
        }
    }

    Text title;
    std::vector<Text> items[kDomains];
    std::vector<Transition> transitions;

private:
    int DomainName() {
        for (int d = 0; d < kDomains; ++d) {
            // (Mermaid's keywords: "complex-->" is complex then -->.)
            const std::wstring_view w = kDomainNames[d];
            if (!c_.Rest().starts_with(w)) continue;
            const wchar_t next = c_.Peek(w.size());
            if (IsWordChar(next)) continue;
            c_.Advance(w.size());
            return d;
        }
        return -1;
    }

    // "text" or 'text', with \ escapes.
    Text Quoted() {
        const size_t open = c_.pos();
        const wchar_t quote = c_.Peek();
        if (quote != L'"' && quote != L'\'') c_.Fail(Tr(L"Expected a \"quoted\" text", L"Ожидался текст в кавычках"));
        c_.Advance();
        const size_t s = c_.pos();
        std::wstring raw;
        while (!c_.AtEnd() && c_.Peek() != quote && c_.Peek() != L'\n') {
            if (c_.Peek() == L'\\' && c_.Peek(1) != L'\0' && c_.Peek(1) != L'\n') c_.Advance();
            raw.push_back(c_.Peek());
            c_.Advance();
        }
        const size_t e = c_.pos();
        if (!c_.Eat(quote)) Fail(open, Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        if (raw.size() > kMaxLabel) Fail(open, Tr(L"Text too long", L"Слишком длинный текст"));
        return Text{std::move(raw), static_cast<uint32_t>(s), static_cast<uint32_t>(e - s)};
    }

    [[noreturn]] void Fail(size_t pos, std::wstring message) {
        c_.set_pos(pos);
        c_.Fail(std::move(message));
    }

    const Source& src_;
    Cursor c_;
    size_t count_ = 0;
};

using M = SceneBuilder::Measured;

constexpr float kW = 800, kH = 600, kPad = 40;
constexpr float kAmplitude = 8;          // Mermaid's boundaryAmplitude
constexpr float kItemH = 26, kItemPadX = 10;
constexpr size_t kMostConfusion = 3;

// Mermaid's seededRandom (a mulberry32 step; JavaScript's 32-bit wrap).
double Random(int64_t seed) {
    uint32_t t = static_cast<uint32_t>(seed) + 1831565813u;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

// Mermaid's hashString.
int64_t Hash(std::wstring_view s) {
    uint32_t h = 0;
    for (const wchar_t ch : s) h = (h << 5) - h + static_cast<uint32_t>(ch);
    return static_cast<int32_t>(h);
}

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b, const Source& src) : p_(p), b_(b), src_(src) {}

    std::shared_ptr<DiagramScene> Build() {
        const float unit = b_.size();
        const int64_t seed = Hash(src_.text.substr(src_.body));
        // Where the domains are: quadrants, confusion in the middle.
        const float hw = kW / 2, hh = kH / 2;
        const D2D1_RECT_F quad[4] = {D2D1::RectF(0, 0, hw, hh), D2D1::RectF(hw, 0, kW, hh), D2D1::RectF(0, hh, hw, kH), D2D1::RectF(hw, hh, kW, kH)};
        auto center = [&](int d) {
            if (d == kConfusion) return D2D1::Point2F(hw, hh);
            return D2D1::Point2F((quad[d].left + quad[d].right) / 2, (quad[d].top + quad[d].bottom) / 2);
        };
        auto bg = [&](int d) { return static_cast<uint8_t>(kDgCynefin + d); };
        for (int d = 0; d < 4; ++d) b_.Rect(Move(quad[d]), 0, RoleAt(bg(d), 102), Role(kDgNone));
        // Boundaries: the fold down the middle, across it, the cliff.
        Fold(seed, true);
        Fold(seed + 100, false);
        {
            const float cx = kW / 2, top = kH * 0.5f, bottom = kH, a = kW * 0.03f, span = bottom - top;
            b_.Begin(P(cx, top));
            b_.CubicTo(P(cx + a, top + span * 0.2f), P(cx - a * 1.5f, top + span * 0.55f), P(cx + a * 0.5f, top + span * 0.75f));
            b_.CubicTo(P(cx - a, top + span * 0.85f), P(cx + a * 0.3f, top + span * 0.95f), P(cx, bottom));
            b_.End(Role(kDgNone), Role(static_cast<uint8_t>(kDgCynefin + 5)), 4.f);
        }
        b_.Ellipse(Move(D2D1::RectF(hw - kW * 0.15f, hh - kH * 0.15f, hw + kW * 0.15f, hh + kH * 0.15f)), RoleAt(bg(kConfusion), 128), Role(kDgLine), 1.5f, 1);
        // Names and their models and practices.
        static constexpr const wchar_t* kModel[4] = {L"Probe → Sense → Respond", L"Sense → Analyse → Respond",
                                                     L"Act → Sense → Respond", L"Sense → Categorise → Respond"};
        static constexpr const wchar_t* kPractice[4] = {L"Emergent Practices", L"Good Practices", L"Novel Practices", L"Best Practices"};
        static constexpr const wchar_t* kName[kDomains] = {L"Complex", L"Complicated", L"Chaotic", L"Clear", L"Confusion"};
        for (int d = 0; d < kDomains; ++d) {
            const D2D1_POINT_2F c = center(d);
            Plain(kName[d], 16, true, false, c.x, d == kConfusion ? c.y - 10 : c.y - 30);
            if (d == kConfusion) {
                Plain(L"Disorder", 11, false, true, c.x, c.y + 8);
            } else {
                Plain(kModel[d], 11, false, true, c.x, c.y - 10);
                Plain(kPractice[d], 11, false, true, c.x, c.y + 5);
            }
        }
        // Items as badges down the middle of their domain (confusion: three,
        // then how many more).
        for (int d = 0; d < kDomains; ++d) {
            const std::vector<Text>& items = p_.items[d];
            const D2D1_POINT_2F c = center(d);
            const float start = c.y + (d == kConfusion ? 22 : 25);
            const size_t shown = d == kConfusion ? std::min(items.size(), kMostConfusion) : items.size();
            for (size_t k = 0; k <= shown; ++k) {
                const bool more = k == shown;
                if (more && shown == items.size()) break;
                RichText t;
                t.text = more ? L"+" + std::to_wstring(items.size() - shown) + L" more" : DecodeEntities(items[k].raw);
                const M m = b_.Measure(t, 0, false, 12 / unit);
                const float w = m.w + 2 * kItemPadX, y = start + static_cast<float>(k) * (kItemH + 4);
                b_.Rect(Move(D2D1::RectF(c.x - w / 2, y, c.x + w / 2, y + kItemH)), 4, RoleAt(bg(d), more ? 153 : 242), Role(kDgLine), 1.f, more ? 1 : 0);
                b_.Text(m, P(c.x - m.w / 2, y + kItemH / 2 - m.h / 2), Role(kDgText), more ? 0 : items[k].pos, more ? 0 : items[k].len);
            }
        }
        // Transitions: curving (to the left) between the domains' middles.
        for (const Transition& t : p_.transitions) {
            const D2D1_POINT_2F a = center(t.from), z = center(t.to);
            const float dx = z.x - a.x, dy = z.y - a.y, len = std::sqrt(dx * dx + dy * dy);
            const D2D1_POINT_2F cp = D2D1::Point2F((a.x + z.x) / 2 - dy / len * len * 0.15f, (a.y + z.y) / 2 + dx / len * len * 0.15f);
            const D2D1_POINT_2F c1 = D2D1::Point2F(a.x + (cp.x - a.x) * 2 / 3, a.y + (cp.y - a.y) * 2 / 3);
            const D2D1_POINT_2F c2 = D2D1::Point2F(z.x + (cp.x - z.x) * 2 / 3, z.y + (cp.y - z.y) * 2 / 3);
            const D2D1_POINT_2F end = Retreat(z, c2, MarkerLength(Marker::Arrow));
            b_.Begin(P(a.x, a.y));
            b_.CubicTo(P(c1.x, c1.y), P(c2.x, c2.y), P(end.x, end.y));
            b_.End(Role(kDgNone), Role(kDgLine), 2.f);
            AddMarker(b_, Marker::Arrow, P(z.x, z.y), P(c2.x, c2.y), Role(kDgLine), 2.f);
            if (!t.label.raw.empty()) {
                RichText lt;
                lt.text = DecodeEntities(t.label.raw);
                const M m = b_.Measure(lt, 0, false, 11 / unit);
                b_.Text(m, P(cp.x - m.w / 2, cp.y - 6 - m.h * 0.8f), Role(kDgText), t.label.pos, t.label.len);
            }
        }
        if (!p_.title.raw.empty()) Plain(p_.title.raw, 18, true, false, kW / 2, -kPad / 2, p_.title.pos, p_.title.len);
        return b_.Finish(kW + 2 * kPad, kH + 2 * kPad);
    }

private:
    // Scene points: inside the padding.
    static D2D1_POINT_2F P(float x, float y) { return D2D1::Point2F(x + kPad, y + kPad); }
    static D2D1_RECT_F Move(D2D1_RECT_F r) { return D2D1::RectF(r.left + kPad, r.top + kPad, r.right + kPad, r.bottom + kPad); }

    // Text centred on (x, y).
    void Plain(const std::wstring& raw, float px, bool bold, bool italic, float x, float y, uint32_t pos = 0, uint32_t len = 0) {
        RichText t;
        t.text = DecodeEntities(raw);
        if (italic) t.spans.push_back({0, static_cast<uint32_t>(t.text.size()), false, true});
        const M m = b_.Measure(t, 0, bold, px / b_.size());
        b_.Text(m, P(x - m.w / 2, y - m.h / 2), Role(kDgText), pos, len);
    }

    // Mermaid's wavering boundary down (across) the middle, dashed.
    void Fold(int64_t seed, bool down) {
        constexpr int kSegments = 7;
        const float length = down ? kH : kW, middle = (down ? kW : kH) / 2, step = length / kSegments;
        const int64_t jitter_step = down ? 17 : 23, offset_step = down ? 31 : 37, offset_add = down ? 7 : 11;
        auto at = [&](float along, float across) { return down ? P(across, along) : P(along, across); };
        float prev_across = 0;
        for (int i = 0; i <= kSegments; ++i) {
            const float across = middle + static_cast<float>(Random(seed + i * jitter_step) * kAmplitude * 2 - kAmplitude);
            const float along = static_cast<float>(i) * step;
            if (i == 0) {
                b_.Begin(at(along, across));
            } else {
                const float mid = along - step / 2, dir = (i - 1) % 2 == 0 ? 1.f : -1.f;
                const float off = kAmplitude * 1.5f * dir * static_cast<float>(Random(seed + (i - 1) * offset_step + offset_add));
                b_.CubicTo(at(mid, prev_across + off), at(mid, across - off), at(along, across));
            }
            prev_across = across;
        }
        b_.End(Role(kDgNone), Role(kDgLine), 2.f, 1);
    }

    const Parser& p_;
    SceneBuilder& b_;
    const Source& src_;
};

}  // namespace

std::shared_ptr<DiagramScene> CynefinDiagram(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder, src).Build();
}

}  // namespace mdv::mermaid
