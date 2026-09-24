// Mermaid packet diagrams: fields of a network packet as boxes over their
// bits, 32 to a row (a field longer than its row goes on in the next), the
// bit numbers at their ends, a title below.
#include <algorithm>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Field {
    long long start = 0, end = 0;  // bits, inclusive
    std::wstring label;
    uint32_t pos = 0, len = 0;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src), c_(src.text, src.body) {}

    void Parse() {
        c_.SkipStatementGap();
        if (!c_.EatKeyword(L"packet-beta") && !c_.EatKeyword(L"packet")) c_.Fail(Tr(L"Expected \"packet-beta\"", L"Ожидалось «packet-beta»"));
        for (;;) {
            c_.SkipStatementGap();
            if (c_.AtEnd()) break;
            Statement();
            c_.SkipBlanks();
            if (!c_.AtStatementEnd()) c_.Fail(Tr(L"Unexpected text", L"Непонятный текст"));
        }
    }

    std::wstring title;
    uint32_t title_pos = 0, title_len = 0;
    std::vector<Field> fields;

private:
    void Statement() {
        if (c_.EatKeyword(L"title")) {
            c_.SkipBlanks();
            const std::wstring_view t = c_.LineRest();
            title = std::wstring(t);
            title_pos = static_cast<uint32_t>(t.data() - src_.text.data());
            title_len = static_cast<uint32_t>(t.size());
            return;
        }
        for (const wchar_t* kw : {L"accTitle", L"accDescr"}) {
            if (c_.EatKeyword(kw)) {
                c_.LineRest();
                return;
            }
        }
        // start-end: "label" | start: "label" | +count: "label"
        const size_t at = c_.pos();
        Field f;
        const long long next = fields.empty() ? 0 : fields.back().end + 1;
        if (c_.Eat(L'+')) {
            const long long count = Number();
            if (count < 1) Error(at, Tr(L"A field has at least one bit", L"В поле хотя бы один бит"));
            f.start = next;
            f.end = next + count - 1;
        } else {
            f.start = Number();
            f.end = f.start;
            c_.SkipBlanks();
            if (c_.Eat(L'-')) {
                c_.SkipBlanks();
                f.end = Number();
            }
            if (f.end < f.start) Error(at, Tr(L"The field ends before it starts", L"Поле кончается раньше, чем начинается"));
            if (f.start != next) Error(at, Tr(L"Fields must follow one another, next bit: ", L"Поля должны идти подряд, следующий бит: ") + std::to_wstring(next));
        }
        c_.SkipBlanks();
        if (!c_.Eat(L':')) c_.Fail(Tr(L"Expected ':'", L"Ожидалось «:»"));
        c_.SkipBlanks();
        if (!c_.Eat(L'"')) c_.Fail(Tr(L"Expected a \"label\"", L"Ожидалась «подпись»"));
        const size_t s = c_.pos();
        while (!c_.AtEnd() && c_.Peek() != L'"' && c_.Peek() != L'\n') c_.Advance();
        if (!c_.Eat(L'"')) c_.Fail(Tr(L"Unclosed quote", L"Не закрыта кавычка"));
        f.label = std::wstring(src_.text.substr(s, c_.pos() - 1 - s));
        if (f.label.size() > kMaxLabel) Error(at, Tr(L"Text too long", L"Слишком длинный текст"));
        f.pos = static_cast<uint32_t>(s);
        f.len = static_cast<uint32_t>(f.label.size());
        if (f.end >= 32 * 1000) Error(at, Tr(L"The packet is too long", L"Пакет слишком длинный"));
        fields.push_back(std::move(f));
    }

    long long Number() {
        const size_t start = c_.pos();
        long long v = 0;
        while (c_.Peek() >= L'0' && c_.Peek() <= L'9' && c_.pos() - start < 9) {
            v = v * 10 + (c_.Peek() - L'0');
            c_.Advance();
        }
        if (c_.pos() == start) c_.Fail(Tr(L"Expected a bit number", L"Ожидался номер бита"));
        return v;
    }

    [[noreturn]] void Error(size_t at, std::wstring message) {
        c_.set_pos(at);
        c_.Fail(std::move(message));
    }

    const Source& src_;
    Cursor c_;
};

using M = SceneBuilder::Measured;

constexpr int kBitsPerRow = 32;
constexpr float kBitW = 32, kRowH = 32, kGapX = 5, kRowStep = 47;

std::shared_ptr<DiagramScene> Build(const Parser& p, SceneBuilder& b, const Source& src) {
    const float unit = b.size();
    auto tiny = [&](const std::wstring& s) {
        RichText t;
        t.text = s;
        return b.Measure(t, 0, false, 10 / unit);
    };
    float bottom = 0;
    for (const Field& f : p.fields) {
        // A piece of the field in each row it takes.
        for (long long s = f.start; s <= f.end;) {
            const long long row = s / kBitsPerRow, e = std::min(f.end, (row + 1) * kBitsPerRow - 1);
            const float y = 15 + static_cast<float>(row) * kRowStep;
            const float x0 = static_cast<float>(s % kBitsPerRow) * kBitW, x1 = static_cast<float>(e % kBitsPerRow + 1) * kBitW - kGapX;
            b.Rect(D2D1::RectF(x0, y, x1, y + kRowH), 0, Role(kDgActivationFill), Role(kDgText), 1.f);
            const M label = b.Measure(ParseLabel(f.label, false), 0, false, 12 / unit);
            b.Text(label, D2D1::Point2F((x0 + x1) / 2 - label.w / 2, y + kRowH / 2 - label.h / 2), Role(kDgText), f.pos, f.len);
            // Bit numbers: the first at the left, the last at the right.
            const M first = tiny(std::to_wstring(s));
            b.Text(first, D2D1::Point2F(x0, y - first.h + 2), Role(kDgText));
            if (e != s) {
                const M last = tiny(std::to_wstring(e));
                b.Text(last, D2D1::Point2F(x1 - last.w, y - last.h + 2), Role(kDgText));
            }
            bottom = std::max(bottom, y + kRowH);
            s = e + 1;
        }
    }
    const bool own = !p.title.empty();
    const std::wstring& title = own ? p.title : src.title;
    if (!title.empty()) {
        const M m = b.Measure(ParseLabel(title, false), 0, false, 14 / unit);
        const float width = kBitsPerRow * kBitW;
        b.Text(m, D2D1::Point2F(width / 2 - m.w / 2, bottom + 10), Role(kDgText), own ? p.title_pos : src.title_pos, own ? p.title_len : src.title_len);
    }
    return b.FinishFitted(4);
}

}  // namespace

std::shared_ptr<DiagramScene> Packet(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Build(p, *builder, src);
}

}  // namespace mdv::mermaid
