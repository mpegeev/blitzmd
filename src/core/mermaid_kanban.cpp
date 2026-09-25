// Mermaid kanban boards: columns (the least indented lines) side by side in
// the theme's cScale colors, their cards stacked in them: text (markdown),
// ticket and assignee, the priority as a colored edge.
#include <algorithm>
#include <cmath>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

struct Card {
    Text text, ticket, assigned;
    std::wstring priority;
};

struct Column {
    Text title;
    std::vector<Card> cards;
};

constexpr size_t kMaxColumns = 500;

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"kanban")) c_.Fail(Tr(L"Expected \"kanban\"", L"Ожидалось «kanban»"));
        int column_indent = -1;
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            size_t line = c_.pos();
            while (line > 0 && src_.text[line - 1] != L'\n') --line;
            const int indent = static_cast<int>(c_.pos() - line);  // (a tab counts one, as in Mermaid)
            const size_t at = c_.pos();
            Card card;
            card.text = Node();
            c_.SkipBlanks();
            if (c_.EatStr(L"@{")) Metadata(&card);
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
            // A column at the least indent there has been, else a card in the last one.
            if (column_indent < 0 || indent <= column_indent || columns.empty()) {
                column_indent = column_indent < 0 ? indent : std::min(column_indent, indent);
                if (columns.size() >= kMaxColumns) c_.Fail(Tr(L"Too many columns", L"Слишком много колонок"));
                columns.push_back(Column{card.text, {}});
            } else {
                if (++cards_ > kMaxNodes) {
                    c_.set_pos(at);
                    c_.Fail(Tr(L"Too many cards", L"Слишком много карточек"));
                }
                columns.back().cards.push_back(std::move(card));
            }
        }
    }

    std::vector<Column> columns;

private:
    // id[text], id(text), id((text)), id)text(, id))text((, id{{text}}, or
    // text: the text (the shape isn't drawn in a kanban).
    Text Node() {
        static constexpr struct {
            const wchar_t* open;
            const wchar_t* close;
        } kShapes[] = {{L"((", L"))"}, {L"))", L"(("}, {L"{{", L"}}"}, {L"[", L"]"}, {L"(", L")"}, {L")", L"("}};
        const size_t start = c_.pos();
        while (!c_.AtStatementEnd() && c_.Peek() != L'[' && c_.Peek() != L'(' && c_.Peek() != L')' && c_.Peek() != L'{' &&
               !c_.Rest().starts_with(L"@{")) {
            c_.Advance();
        }
        for (const auto& s : kShapes) {
            if (!c_.Rest().starts_with(s.open)) continue;
            const size_t open = c_.pos(), from = open + std::wcslen(s.open);
            // Its end: the close before the line's end (or its metadata).
            size_t e = from;
            while (e < src_.text.size() && src_.text[e] != L'\n') ++e;
            const size_t meta = src_.text.substr(from, e - from).find(L"@{");
            if (meta != std::wstring_view::npos) e = from + meta;
            const std::wstring_view rest = TrimRight(src_.text.substr(from, e - from));
            if (!rest.ends_with(s.close)) continue;
            std::wstring_view inner = Trim(rest.substr(0, rest.size() - std::wcslen(s.close)));
            if (inner.size() >= 2 && inner.front() == L'"' && inner.back() == L'"') inner = inner.substr(1, inner.size() - 2);
            c_.set_pos(from + rest.size());
            return Here(inner);
        }
        return Here(Trim(src_.text.substr(start, c_.pos() - start)));
    }

    static std::wstring_view TrimRight(std::wstring_view s) {
        while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r')) s.remove_suffix(1);
        return s;
    }

    // @{ ticket: MC-1, assigned: 'knsv', priority: 'High', label: 'x' }, the
    // entries on one line or several.
    void Metadata(Card* card) {
        auto gap = [&] {
            while (c_.Peek() == L' ' || c_.Peek() == L'\t' || c_.Peek() == L'\r' || c_.Peek() == L'\n' || c_.Peek() == L',') c_.Advance();
        };
        for (;;) {
            gap();
            if (c_.Eat(L'}')) return;
            if (c_.AtEnd()) c_.Fail(Tr(L"Expected '}'", L"Ожидалась «}»"));
            const size_t k = c_.pos();
            while (IsWordChar(c_.Peek())) c_.Advance();
            const std::wstring key(src_.text.substr(k, c_.pos() - k));
            c_.SkipBlanks();
            if (key.empty() || !c_.Eat(L':')) c_.Fail(Tr(L"Expected key: value", L"Ожидалось ключ: значение"));
            c_.SkipBlanks();
            Text value;
            if (c_.Peek() == L'\'' || c_.Peek() == L'"') {
                const wchar_t q = c_.Peek();
                c_.Advance();
                const size_t s = c_.pos();
                while (!c_.AtEnd() && c_.Peek() != q && c_.Peek() != L'\n') c_.Advance();
                if (!c_.Eat(q)) c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
                value = Here(src_.text.substr(s, c_.pos() - 1 - s));
            } else {
                const size_t s = c_.pos();
                while (!c_.AtEnd() && c_.Peek() != L'\n' && c_.Peek() != L',' && c_.Peek() != L'}') c_.Advance();
                value = Here(Trim(src_.text.substr(s, c_.pos() - s)));
            }
            if (key == L"ticket") card->ticket = value;
            else if (key == L"assigned") card->assigned = value;
            else if (key == L"priority") card->priority = value.raw;
            else if (key == L"label" && !value.raw.empty()) card->text = value;
        }
    }

    Text Here(std::wstring_view s) {
        if (s.size() > kMaxLabel) c_.Fail(Tr(L"Text too long", L"Слишком длинный текст"));
        return Text{std::wstring(s), static_cast<uint32_t>(s.data() - src_.text.data()), static_cast<uint32_t>(s.size())};
    }

    const Source& src_;
    Cursor c_;
    size_t cards_ = 0;
};

using M = SceneBuilder::Measured;

constexpr float kColumnW = 200, kColumnGap = 5, kCardPad = 10, kCardGap = 6, kInset = 7;

std::shared_ptr<DiagramScene> Build(const Parser& p, SceneBuilder& b) {
    float x = 0;
    const float card_w = kColumnW - 2 * kInset, text_w = card_w - 2 * kCardPad;
    for (size_t k = 0; k < p.columns.size(); ++k) {
        const Column& col = p.columns[k];
        const M title = b.Measure(ParseLabel(col.title.raw, true), kColumnW - 10, false, 1.f);
        // Cards first measured: the column is as tall as they are.
        struct Laid {
            M text, ticket, assigned;
            float h;
        };
        std::vector<Laid> cards;
        float h = title.h + 10;
        for (const Card& c : col.cards) {
            Laid l{b.Measure(ParseLabel(c.text.raw, true), text_w, false, 1.f, false), {}, {}, 0};
            if (!c.ticket.raw.empty()) l.ticket = b.Measure(ParseLabel(c.ticket.raw, false), text_w / 2, false, 0.875f, false);
            if (!c.assigned.raw.empty()) l.assigned = b.Measure(ParseLabel(c.assigned.raw, false), text_w / 2, false, 0.875f, false);
            l.h = l.text.h + 2 * kCardPad - 4 + std::max(l.ticket.h, l.assigned.h);
            h += l.h + kCardGap;
            cards.push_back(std::move(l));
        }
        h += kInset - kCardGap + (col.cards.empty() ? kCardGap + 10 : 0.f);
        // Mermaid's kanban columns: cScale from the third on, 10% lighter.
        b.Rect(D2D1::RectF(x, 0, x + kColumnW, h), 5, Role(static_cast<uint8_t>(kDgScaleLighter + (k + 2) % 12)), Role(kDgNone));
        b.Text(title, D2D1::Point2F(x + kColumnW / 2 - title.w / 2, 4), Role(kDgText), col.title.pos, col.title.len);
        float y = title.h + 10;
        for (size_t i = 0; i < col.cards.size(); ++i) {
            const Card& c = col.cards[i];
            const Laid& l = cards[i];
            const D2D1_RECT_F r = D2D1::RectF(x + kInset, y, x + kInset + card_w, y + l.h);
            b.Rect(r, 5, Role(kDgPage), Role(kDgNodeStroke), 1.f);
            // The priority: a colored edge on the left.
            uint32_t edge = 0;
            if (EqualsNoCase(c.priority, L"Very High")) edge = 0xFFFF0000;
            else if (EqualsNoCase(c.priority, L"High")) edge = 0xFFFFA500;
            else if (EqualsNoCase(c.priority, L"Low")) edge = 0xFF0000FF;
            else if (EqualsNoCase(c.priority, L"Very Low")) edge = 0xFFADD8E6;
            if (edge) b.Rect(D2D1::RectF(r.left + 1, r.top + 2, r.left + 4, r.bottom - 2), 0, Argb(edge), Role(kDgNone));
            b.Text(l.text, D2D1::Point2F(r.left + kCardPad, r.top + kCardPad - 2), Role(kDgText), c.text.pos, c.text.len);
            const float my = r.top + kCardPad - 2 + l.text.h;
            if (l.ticket.layout) b.Text(l.ticket, D2D1::Point2F(r.left + kCardPad, my), Role(kDgText), c.ticket.pos, c.ticket.len);
            if (l.assigned.layout) {
                b.Text(l.assigned, D2D1::Point2F(r.right - kCardPad - l.assigned.w, my), Role(kDgText), c.assigned.pos, c.assigned.len);
            }
            y += l.h + kCardGap;
        }
        x += kColumnW + kColumnGap;
    }
    return b.FinishFitted(8);
}

}  // namespace

std::shared_ptr<DiagramScene> Kanban(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Build(p, *builder);
}

}  // namespace mdv::mermaid
