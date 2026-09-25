// Mermaid venn diagrams (venn-beta): sets as circles whose areas and
// overlaps follow their sizes, laid out as Mermaid's venn.js lays them out (a
// greedy start or constrained MDS, then Nelder-Mead on the overlaps' squared
// error; clusters turned, packed and fitted), each region's label where it is
// widest, text nodes in a grid there, styles; in Mermaid's venn colors.
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <functional>
#include <map>
#include <optional>
#include <set>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

constexpr size_t kMaxSets = 24;
constexpr size_t kMaxAreas = 1000;    // subsets, the pairs they add and all
constexpr double kMaxWork = 1e6;      // one loss evaluation's intersection work
constexpr double kSmall = 1e-10;
constexpr double kPi = 3.14159265358979323846;

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

// A set or an overlap of sets.
struct Subset {
    std::vector<std::wstring> sets;  // names, sorted
    double size = 0;
    Text label;                      // raw empty: none
    Text name;                       // a set's name where it is declared
};

struct TextNode {
    std::vector<std::wstring> sets;
    Text id, label;
    bool has_label = false;
};

// Mermaid's lexer (venn.jison): its tokens and states.
enum class Tok { Eof, Newline, Title, Venn, Set, Union, TextKw, IndentText, Style, Label, Number, Hex, Rgb, Ident, String, Comma, Colon };

struct Token {
    Tok kind = Tok::Eof;
    size_t pos = 0, len = 0;  // in the source
    Text text;                // a label's, string's, title's text; a word as it is
};

bool AsciiWord(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'_'; }
bool Digit(wchar_t c) { return c >= L'0' && c <= L'9'; }
bool HexDigit(wchar_t c) { return Digit(c) || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'); }

class Lexer {
public:
    Lexer(std::wstring_view s, size_t pos) : s_(s), pos_(pos) {}
    bool indent_mode = false;  // after set / union: indented "text" lines are theirs

    Token Next() {
        for (;;) {
            if (pos_ >= s_.size()) return Make(Tok::Eof, pos_, 0);
            const wchar_t c = s_[pos_];
            // Comments (%% to the line end; a character before them goes too).
            if (c == L'%' && At(1) == L'%' && At(2) != L'{') {
                SkipComment(pos_);
                continue;
            }
            if (c != L'}' && At(1) == L'%' && At(2) == L'%') {
                SkipComment(pos_ + 1);
                continue;
            }
            if (bol_ && (c == L' ' || c == L'\t')) {
                size_t e = pos_;
                while (e < s_.size() && (s_[e] == L' ' || s_[e] == L'\t')) ++e;
                if (indent_mode && e < s_.size() && Word(e, L"text")) {
                    // An indented "text" line: the text of the set above.
                    const size_t at = pos_;
                    pos_ = e;
                    consume_text_ = true;
                    bol_ = false;
                    return Make(Tok::IndentText, at, e - at);
                }
                pos_ = e;
                continue;
            }
            if (bol_ && c != L'\n' && c != L'\r') {
                indent_mode = false;
                bol_ = false;
                continue;
            }
            if (c == L'\n' || c == L'\r') {
                const size_t at = pos_;
                while (pos_ < s_.size() && (s_[pos_] == L'\n' || s_[pos_] == L'\r')) ++pos_;
                bol_ = true;
                return Make(Tok::Newline, at, pos_ - at);
            }
            if (c == L' ' || c == L'\t') {
                ++pos_;
                continue;
            }
            return Lex();
        }
    }

private:
    wchar_t At(size_t k) const { return pos_ + k < s_.size() ? s_[pos_ + k] : L'\0'; }

    void SkipComment(size_t from) {
        pos_ = from;
        while (pos_ < s_.size() && s_[pos_] != L'\n') ++pos_;
    }

    // A keyword (any case) at `at`, not followed by a word character.
    bool Word(size_t at, std::wstring_view w) const {
        if (at + w.size() > s_.size()) return false;
        for (size_t k = 0; k < w.size(); ++k) {
            if (std::towlower(s_[at + k]) != w[k]) return false;
        }
        return at + w.size() >= s_.size() || !AsciiWord(s_[at + w.size()]);
    }

    Token Make(Tok kind, size_t at, size_t len) const {
        Token t;
        t.kind = kind;
        t.pos = at;
        t.len = len;
        return t;
    }

    Token Take(Tok kind, size_t len) {
        if (len > kMaxLabel) Fail(pos_, Tr(L"Text too long", L"Слишком длинный текст"));
        Token t = Make(kind, pos_, len);
        t.text = Text{std::wstring(s_.substr(pos_, len)), static_cast<uint32_t>(pos_), static_cast<uint32_t>(len)};
        pos_ += len;
        return t;
    }

    // Text between `from` and `to`, trimmed (as Mermaid's normalizeText).
    Text Inner(size_t from, size_t to) const {
        while (from < to && (s_[from] == L' ' || s_[from] == L'\t')) ++from;
        while (to > from && (s_[to - 1] == L' ' || s_[to - 1] == L'\t')) --to;
        if (to - from > kMaxLabel) Fail(from, Tr(L"Text too long", L"Слишком длинный текст"));
        return Text{std::wstring(s_.substr(from, to - from)), static_cast<uint32_t>(from), static_cast<uint32_t>(to - from)};
    }

    [[noreturn]] void Fail(size_t at, std::wstring message) const { mermaid::Fail(at, std::move(message)); }

    // The rules after blanks, comments and line ends, in Mermaid's order.
    Token Lex() {
        const size_t at = pos_;
        const wchar_t c = s_[pos_];
        if (Word(at, L"title") && (At(5) == L' ' || At(5) == L'\t')) {
            size_t e = at + 6;
            while (e < s_.size() && s_[e] != L'\n' && s_[e] != L'\r' && s_[e] != L'#' && s_[e] != L';') ++e;
            if (e > at + 6) {
                Token t = Make(Tok::Title, at, e - at);
                t.text = Inner(at + 6, e);
                pos_ = e;
                return t;
            }
        }
        if (Word(at, L"venn-beta")) return Take(Tok::Venn, 9);
        if (Word(at, L"set")) return Take(Tok::Set, 3);
        if (Word(at, L"union")) return Take(Tok::Union, 5);
        if (Word(at, L"text")) {
            if (consume_text_) {
                // (The keyword of an indented text line.)
                consume_text_ = false;
                pos_ += 4;
                return Next();
            }
            return Take(Tok::TextKw, 4);
        }
        if (Word(at, L"style")) return Take(Tok::Style, 5);
        if (c == L'[') {
            if (At(1) == L'"') {
                size_t e = at + 2;
                while (e < s_.size() && s_[e] != L'"' && s_[e] != L'\n') ++e;
                if (e + 1 < s_.size() && s_[e] == L'"' && s_[e + 1] == L']') {
                    Token t = Make(Tok::Label, at, e + 2 - at);
                    t.text = Inner(at + 2, e);
                    pos_ = e + 2;
                    return t;
                }
            } else {
                size_t e = at + 1;
                while (e < s_.size() && s_[e] != L']' && s_[e] != L'"' && s_[e] != L'\n') ++e;
                if (e > at + 1 && e < s_.size() && s_[e] == L']') {
                    Token t = Make(Tok::Label, at, e + 1 - at);
                    t.text = Inner(at + 1, e);
                    pos_ = e + 1;
                    return t;
                }
            }
            Fail(at, Tr(L"Unclosed [label]", L"Не закрыта [метка]"));
        }
        {
            // [+-]?(\d+(\.\d+)?|\.\d+)
            size_t e = at;
            if (s_[e] == L'+' || s_[e] == L'-') ++e;
            const size_t digits = e;
            while (e < s_.size() && Digit(s_[e])) ++e;
            if (e > digits) {
                if (e + 1 < s_.size() && s_[e] == L'.' && Digit(s_[e + 1])) {
                    ++e;
                    while (e < s_.size() && Digit(s_[e])) ++e;
                }
                return Take(Tok::Number, e - at);
            }
            if (e + 1 < s_.size() && s_[e] == L'.' && Digit(s_[e + 1])) {
                ++e;
                while (e < s_.size() && Digit(s_[e])) ++e;
                return Take(Tok::Number, e - at);
            }
        }
        if (c == L'#') {
            size_t e = at + 1;
            while (e < s_.size() && e - at <= 8 && HexDigit(s_[e])) ++e;
            if (e - at - 1 >= 3) return Take(Tok::Hex, e - at);
        }
        if (std::towlower(c) == L'r') {
            if (const size_t n = RgbLength(at, 4)) return Take(Tok::Rgb, n);
            if (const size_t n = RgbLength(at, 3)) return Take(Tok::Rgb, n);
        }
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_') {
            size_t e = at + 1;
            while (e < s_.size() && (AsciiWord(s_[e]) || s_[e] == L'-')) ++e;
            return Take(Tok::Ident, e - at);
        }
        if (c == L'"') {
            size_t e = at + 1;
            while (e < s_.size() && s_[e] != L'"' && s_[e] != L'\n') ++e;
            if (e >= s_.size() || s_[e] != L'"') Fail(at, Tr(L"Unclosed quote", L"Не закрыта кавычка"));
            if (e - at - 1 > kMaxLabel) Fail(at, Tr(L"Text too long", L"Слишком длинный текст"));
            Token t = Make(Tok::String, at, e + 1 - at);
            // (Quotes off, the rest as it is.)
            t.text = Text{std::wstring(s_.substr(at + 1, e - at - 1)), static_cast<uint32_t>(at + 1), static_cast<uint32_t>(e - at - 1)};
            pos_ = e + 1;
            return t;
        }
        if (c == L',') return Take(Tok::Comma, 1);
        if (c == L':') return Take(Tok::Colon, 1);
        Fail(at, Tr(L"Unexpected character", L"Непонятный символ"));
    }

    // rgb(r, g, b) or rgba(r, g, b, a): its length, or 0.
    size_t RgbLength(size_t at, int parts) const {
        const std::wstring_view head = parts == 4 ? L"rgba(" : L"rgb(";
        if (at + head.size() > s_.size()) return 0;
        for (size_t k = 0; k < head.size(); ++k) {
            if (std::towlower(s_[at + k]) != head[k]) return 0;
        }
        size_t e = at + head.size();
        auto blanks = [&] {
            while (e < s_.size() && (s_[e] == L' ' || s_[e] == L'\t')) ++e;
        };
        for (int k = 0; k < parts; ++k) {
            blanks();
            const size_t start = e;
            while (e < s_.size() && (Digit(s_[e]) || s_[e] == L'.')) ++e;
            if (e == start) return 0;
            blanks();
            if (k + 1 < parts) {
                if (e >= s_.size() || s_[e] != L',') return 0;
                ++e;
            }
        }
        if (e >= s_.size() || s_[e] != L')') return 0;
        return e + 1 - at;
    }

    std::wstring_view s_;
    size_t pos_;
    bool bol_ = false;           // at a line's start
    bool consume_text_ = false;  // an indented line's "text" is next
};

class Parser {
public:
    explicit Parser(const Source& src) : lex_(src.text, src.body) {}

    void Parse() {
        while (Peek().kind == Tok::Newline) Take();
        if (Peek().kind != Tok::Venn) Fail(Peek().pos, Tr(L"Expected \"venn-beta\"", L"Ожидалось «venn-beta»"));
        Take();
        for (;;) {
            const Token t = Take();
            switch (t.kind) {
            case Tok::Eof: {
                // Mermaid's khroma can't make a set's text color from a
                // fill it can't read (when no color is given): an error.
                for (const auto& [key, at] : fill_at_) {
                    const auto& st = styles.at(key);
                    uint32_t argb = 0;
                    const std::wstring& fill = st.at(L"fill");
                    const auto color = st.find(L"color");
                    if ((fill != L"none" && ParseCssColor(fill, &argb)) || (color != st.end() && !color->second.empty())) continue;
                    if (std::none_of(subsets.begin(), subsets.end(), [&](const Subset& s) { return s.sets.size() == 1 && s.sets[0] == key && s.size != 0; })) continue;
                    Fail(at, Tr(L"Unsupported color format: ", L"Непонятный формат цвета: ") + st.at(L"fill"));
                }
                return;
            }
            case Tok::Newline: break;
            case Tok::Title: title = t.text; break;
            case Tok::Set: {
                const Text id = Identifier();
                const Text label = OptLabel();
                AddSubset({id}, label, OptSize(), t.pos);
                lex_.indent_mode = true;
                break;
            }
            case Tok::Union: {
                const std::vector<Text> ids = Identifiers();
                const Text label = OptLabel();
                const std::optional<double> size = OptSize();
                if (ids.size() < 2) Fail(t.pos, Tr(L"A union needs two sets or more", L"Объединению нужно два множества или больше"));
                for (const Text& id : ids) {
                    if (!known_.contains(id.raw)) Fail(id.pos, Tr(L"Unknown set: ", L"Неизвестное множество: ") + id.raw);
                }
                AddSubset(ids, label, size, t.pos);
                lex_.indent_mode = true;
                break;
            }
            case Tok::TextKw: {
                std::vector<Text> ids = Identifiers();
                const Text id = NodeId();
                AddText(std::move(ids), id);
                break;
            }
            case Tok::IndentText: {
                const Text id = NodeId();
                std::vector<Text> ids;
                for (const std::wstring& s : current_) ids.push_back(Text{s});
                AddText(std::move(ids), id);
                break;
            }
            case Tok::Style: {
                const std::vector<Text> ids = Identifiers();
                const std::wstring key = Key(Sorted(ids));
                std::map<std::wstring, std::wstring>& style = styles[key];
                for (;;) {
                    const Token field = Take();
                    if (field.kind != Tok::Ident) Fail(field.pos, Tr(L"Expected a style property", L"Ожидалось свойство стиля"));
                    if (Take().kind != Tok::Colon) Fail(field.pos + field.len, Tr(L"Expected \":\"", L"Ожидалось «:»"));
                    std::wstring value;
                    if (Peek().kind == Tok::String) {
                        value = std::wstring(Trim(Take().text.raw));
                    } else {
                        // Words, numbers and colors, joined by spaces.
                        while (Peek().kind == Tok::Number || Peek().kind == Tok::Ident || Peek().kind == Tok::Hex || Peek().kind == Tok::Rgb) {
                            if (!value.empty()) value += L' ';
                            value += Take().text.raw;
                        }
                        if (value.empty()) Fail(Peek().pos, Tr(L"Expected a value", L"Ожидалось значение"));
                    }
                    if (field.text.raw == L"fill") fill_at_[key] = field.pos;
                    style[field.text.raw] = std::move(value);
                    if (Peek().kind != Tok::Comma) break;
                    Take();
                }
                break;
            }
            default: Fail(t.pos, Tr(L"Expected set, union, text, style or title", L"Ожидалось set, union, text, style или title"));
            }
            // What may follow a statement.
            const Tok next = Peek().kind;
            if (next != Tok::Eof && next != Tok::Newline && next != Tok::Title && next != Tok::Set && next != Tok::Union &&
                next != Tok::TextKw && next != Tok::IndentText && next != Tok::Style) {
                Fail(Peek().pos, Tr(L"Unexpected text", L"Непонятный текст"));
            }
        }
    }

    static std::vector<std::wstring> Sorted(const std::vector<Text>& ids) {
        std::vector<std::wstring> out;
        for (const Text& t : ids) out.push_back(t.raw);
        std::sort(out.begin(), out.end());
        return out;
    }

    // Mermaid's key of a list of sets.
    static std::wstring Key(const std::vector<std::wstring>& sets) {
        std::wstring k;
        for (size_t i = 0; i < sets.size(); ++i) k += (i ? L"|" : L"") + sets[i];
        return k;
    }

    Text title;
    std::vector<Subset> subsets;
    std::vector<size_t> drawn;  // subsets in the order declared, again when declared again
    std::vector<std::pair<std::vector<std::wstring>, double>> earlier;  // unions declared again: their sizes before

private:
    std::map<std::wstring, size_t> fill_at_;  // style keys with a fill: where it is set

public:
    std::vector<TextNode> texts;
    std::map<std::wstring, std::map<std::wstring, std::wstring>> styles;  // by key: property, value

private:
    const Token& Peek() {
        if (!has_) {
            la_ = lex_.Next();
            has_ = true;
        }
        return la_;
    }
    Token Take() {
        Peek();
        has_ = false;
        return la_;
    }

    Text Identifier() {
        const Token t = Take();
        if (t.kind != Tok::Ident && t.kind != Tok::String) Fail(t.pos, Tr(L"Expected a set's name", L"Ожидалось имя множества"));
        return t.text;
    }
    std::vector<Text> Identifiers() {
        std::vector<Text> ids{Identifier()};
        while (Peek().kind == Tok::Comma) {
            Take();
            ids.push_back(Identifier());
        }
        return ids;
    }
    Text OptLabel() { return Peek().kind == Tok::Label ? Take().text : Text{}; }
    std::optional<double> OptSize() {
        if (Peek().kind != Tok::Colon) return std::nullopt;
        Take();
        const Token t = Take();
        if (t.kind != Tok::Number) Fail(t.pos, Tr(L"Expected a number", L"Ожидалось число"));
        const double v = std::wcstod(t.text.raw.c_str(), nullptr);
        if (!std::isfinite(v)) Fail(t.pos, Tr(L"The number is too large", L"Слишком большое число"));
        if (v < 0) Fail(t.pos, Tr(L"A size can't be negative", L"Размер не может быть отрицательным"));
        return v;
    }
    // A text node's id and label.
    Text NodeId() {
        const Token t = Take();
        if (t.kind != Tok::Ident && t.kind != Tok::String && t.kind != Tok::Number) Fail(t.pos, Tr(L"Expected a text's id", L"Ожидался идентификатор текста"));
        pending_label_ = OptLabel();
        return t.text;
    }

    void AddSubset(const std::vector<Text>& ids, const Text& label, std::optional<double> size, size_t at) {
        Subset s;
        s.sets = Sorted(ids);
        s.size = size ? *size : 10.0 / static_cast<double>(ids.size() * ids.size());
        s.label = label;
        s.name = ids[0];
        current_ = s.sets;
        if (s.sets.size() == 1) known_.insert(s.sets[0]);
        // A subset declared again: laid out once (its size and label now),
        // drawn again (as Mermaid draws each).
        if (drawn.size() >= kMaxAreas * 4) Fail(at, Tr(L"Too many sets and unions", L"Слишком много множеств и объединений"));
        for (size_t k = 0; k < subsets.size(); ++k) {
            Subset& old = subsets[k];
            if (old.sets == s.sets) {
                if (old.sets.size() > 1) earlier.push_back({old.sets, old.size});
                old.size = s.size;
                if (!label.raw.empty()) old.label = label;
                drawn.push_back(k);
                return;
            }
        }
        if (subsets.size() >= kMaxAreas) Fail(at, Tr(L"Too many sets and unions", L"Слишком много множеств и объединений"));
        if (s.sets.size() == 1 && ++singles_ > kMaxSets) Fail(at, Tr(L"Too many sets", L"Слишком много множеств"));
        drawn.push_back(subsets.size());
        subsets.push_back(std::move(s));
    }

    void AddText(std::vector<Text> ids, const Text& id) {
        if (texts.size() >= kMaxNodes) Fail(id.pos, Tr(L"Too many texts", L"Слишком много текстов"));
        TextNode n;
        n.sets = Sorted(ids);
        n.id = id;
        n.has_label = !pending_label_.raw.empty();
        n.label = pending_label_;
        texts.push_back(std::move(n));
    }

    Lexer lex_;
    Token la_;
    bool has_ = false;
    std::vector<std::wstring> current_;  // the sets of the last set or union
    std::set<std::wstring> known_;
    size_t singles_ = 0;
    Text pending_label_;
};

// ---------------------------------------------------------------------------
// venn.js

struct Circle {
    double x = 0, y = 0, r = 0;
};
struct Area {
    std::vector<int> sets;  // indices of the circles, in the names' order
    double size = 0;
};

double Distance(double ax, double ay, double bx, double by) { return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); }
double Distance(const Circle& a, const Circle& b) { return Distance(a.x, a.y, b.x, b.y); }

// A circular segment's area.
double SegmentArea(double r, double width) { return r * r * std::acos(1 - width / r) - (r - width) * std::sqrt(width * (2 * r - width)); }

double CircleOverlap(double r1, double r2, double d) {
    if (d >= r1 + r2) return 0;
    if (d <= std::fabs(r1 - r2)) return kPi * std::min(r1, r2) * std::min(r1, r2);
    const double w1 = r1 - (d * d - r2 * r2 + r1 * r1) / (2 * d);
    const double w2 = r2 - (d * d - r1 * r1 + r2 * r2) / (2 * d);
    return SegmentArea(r1, w1) + SegmentArea(r2, w2);
}

struct IPoint {
    double x = 0, y = 0;
    int a = -1, b = -1;  // the circles it is on
    double angle = 0;
};

void CircleCircle(const Circle& p1, const Circle& p2, int a, int b, std::vector<IPoint>* out) {
    const double d = Distance(p1, p2), r1 = p1.r, r2 = p2.r;
    if (d >= r1 + r2 || d <= std::fabs(r1 - r2)) return;
    const double aa = (r1 * r1 - r2 * r2 + d * d) / (2 * d), h = std::sqrt(r1 * r1 - aa * aa);
    const double x0 = p1.x + (aa * (p2.x - p1.x)) / d, y0 = p1.y + (aa * (p2.y - p1.y)) / d;
    const double rx = -(p2.y - p1.y) * (h / d), ry = -(p2.x - p1.x) * (h / d);
    out->push_back({x0 + rx, y0 - ry, a, b});
    out->push_back({x0 - rx, y0 + ry, a, b});
}

struct Arc {
    Circle c;
    double width = 0;
    IPoint p1, p2;  // from p2 to p1, clockwise
};

// The area where all the circles overlap; its outline's arcs.
double IntersectionArea(const std::vector<Circle>& cs, std::vector<Arc>* arcs_out = nullptr) {
    std::vector<IPoint> points, inner;
    for (size_t i = 0; i < cs.size(); ++i) {
        for (size_t j = i + 1; j < cs.size(); ++j) CircleCircle(cs[i], cs[j], static_cast<int>(i), static_cast<int>(j), &points);
    }
    for (const IPoint& p : points) {
        if (std::all_of(cs.begin(), cs.end(), [&](const Circle& c) { return Distance(p.x, p.y, c.x, c.y) < c.r + kSmall; })) inner.push_back(p);
    }
    double arc_area = 0, polygon_area = 0;
    std::vector<Arc> arcs;
    if (inner.size() > 1) {
        double cx = 0, cy = 0;
        for (const IPoint& p : inner) {
            cx += p.x;
            cy += p.y;
        }
        cx /= static_cast<double>(inner.size());
        cy /= static_cast<double>(inner.size());
        for (IPoint& p : inner) p.angle = std::atan2(p.x - cx, p.y - cy);
        std::stable_sort(inner.begin(), inner.end(), [](const IPoint& l, const IPoint& r) { return r.angle < l.angle; });
        IPoint p2 = inner.back();
        for (const IPoint& p1 : inner) {
            polygon_area += (p2.x + p1.x) * (p1.y - p2.y);
            const double mx = (p1.x + p2.x) / 2, my = (p1.y + p2.y) / 2;
            std::optional<Arc> arc;
            for (const int k : {p1.a, p1.b}) {
                if (k != p2.a && k != p2.b) continue;
                const Circle& c = cs[static_cast<size_t>(k)];
                const double a1 = std::atan2(p1.x - c.x, p1.y - c.y), a2 = std::atan2(p2.x - c.x, p2.y - c.y);
                double diff = a2 - a1;
                if (diff < 0) diff += 2 * kPi;
                const double a = a2 - diff / 2;
                double width = Distance(mx, my, c.x + c.r * std::sin(a), c.y + c.r * std::cos(a));
                if (width > c.r * 2) width = c.r * 2;
                if (!arc || arc->width > width) arc = Arc{c, width, p1, p2};
            }
            if (arc) {
                arcs.push_back(*arc);
                arc_area += SegmentArea(arc->c.r, arc->width);
                p2 = p1;
            }
        }
    } else if (!cs.empty()) {
        // Disjoint, or the smallest inside all the others.
        const Circle* smallest = &cs[0];
        for (const Circle& c : cs) {
            if (c.r < smallest->r) smallest = &c;
        }
        const bool disjoint = std::any_of(cs.begin(), cs.end(), [&](const Circle& c) { return Distance(c, *smallest) > std::fabs(smallest->r - c.r); });
        if (!disjoint) {
            const Circle& s = *smallest;
            arc_area = s.r * s.r * kPi;
            arcs.push_back(Arc{s, s.r * 2, IPoint{s.x, s.y + s.r}, IPoint{s.x - kSmall, s.y + s.r}});
        }
    }
    polygon_area /= 2;
    if (arcs_out) *arcs_out = std::move(arcs);
    return arc_area + polygon_area;
}

// The squared error of the overlaps (pairs directly, more by their area).
double Loss(const std::vector<Circle>& circles, const std::vector<Area>& areas) {
    double out = 0;
    std::vector<Circle> some;
    for (const Area& area : areas) {
        if (area.sets.size() == 1) continue;
        double overlap;
        if (area.sets.size() == 2) {
            const Circle &l = circles[static_cast<size_t>(area.sets[0])], &r = circles[static_cast<size_t>(area.sets[1])];
            overlap = CircleOverlap(l.r, r.r, Distance(l, r));
        } else {
            some.clear();
            for (const int s : area.sets) some.push_back(circles[static_cast<size_t>(s)]);
            overlap = IntersectionArea(some);
        }
        out += (overlap - area.size) * (overlap - area.size);
    }
    return out;
}

// fmin's bisect: a zero of f between a and b.
template <typename F>
double Bisect(F&& f, double a, double b) {
    const double fa = f(a), fb = f(b);
    double delta = b - a;
    if (fa == 0) return a;
    if (fb == 0) return b;
    for (int i = 0; i < 100; ++i) {
        delta /= 2;
        const double mid = a + delta, fmid = f(mid);
        if (fmid * fa >= 0) a = mid;
        if (std::fabs(delta) < 1e-10 || fmid == 0) return mid;
    }
    return a + delta;
}

// How far apart two circles overlap by `overlap`.
double DistanceFromIntersectArea(double r1, double r2, double overlap) {
    if (std::min(r1, r2) * std::min(r1, r2) * kPi <= overlap + kSmall) return std::fabs(r1 - r2);
    return Bisect([&](double d) { return CircleOverlap(r1, r2, d) - overlap; }, 0, r1 + r2);
}

// fmin's nelderMead (downhill simplex), at most `max_evals` evaluations.
std::vector<double> NelderMead(const std::function<double(const std::vector<double>&)>& f, const std::vector<double>& x0, int max_iterations,
                               double min_error_delta, double min_tolerance, size_t max_evals) {
    struct Vertex {
        std::vector<double> x;
        double fx = 0;
    };
    size_t evals = 0;
    auto eval = [&](const std::vector<double>& x) {
        ++evals;
        return f(x);
    };
    const size_t n = x0.size();
    std::vector<Vertex> simplex(n + 1);
    simplex[0] = {x0, eval(x0)};
    for (size_t i = 0; i < n; ++i) {
        std::vector<double> p = x0;
        p[i] = p[i] != 0 && !std::isnan(p[i]) ? p[i] * 1.05 : 0.001;
        simplex[i + 1] = {p, eval(p)};
    }
    auto by_fx = [](const Vertex& a, const Vertex& b) { return a.fx < b.fx; };
    auto weighted = [&](std::vector<double>& out, double w1, const std::vector<double>& v1, double w2, const std::vector<double>& v2) {
        for (size_t j = 0; j < out.size(); ++j) out[j] = w1 * v1[j] + w2 * v2[j];
    };
    constexpr double rho = 1, chi = 2, psi = -0.5, sigma = 0.5;
    std::vector<double> centroid(n), reflected(n), contracted(n), expanded(n);
    auto update = [&](const std::vector<double>& v, double fx) {
        simplex[n].x = v;
        simplex[n].fx = fx;
    };
    for (int it = 0; it < max_iterations && evals < max_evals; ++it) {
        std::stable_sort(simplex.begin(), simplex.end(), by_fx);
        double max_diff = 0;
        for (size_t i = 0; i < n; ++i) max_diff = std::max(max_diff, std::fabs(simplex[0].x[i] - simplex[1].x[i]));
        if (std::fabs(simplex[0].fx - simplex[n].fx) < min_error_delta && max_diff < min_tolerance) break;
        for (size_t i = 0; i < n; ++i) {
            centroid[i] = 0;
            for (size_t j = 0; j < n; ++j) centroid[i] += simplex[j].x[i];
            centroid[i] /= static_cast<double>(n);
        }
        const Vertex worst = simplex[n];
        weighted(reflected, 1 + rho, centroid, -rho, worst.x);
        const double fr = eval(reflected);
        if (fr < simplex[0].fx) {
            weighted(expanded, 1 + chi, centroid, -chi, worst.x);
            const double fe = eval(expanded);
            if (fe < fr) update(expanded, fe);
            else update(reflected, fr);
        } else if (fr >= simplex[n - 1].fx) {
            bool reduce = false;
            if (fr > worst.fx) {
                weighted(contracted, 1 + psi, centroid, -psi, worst.x);
                const double fc = eval(contracted);
                if (fc < worst.fx) update(contracted, fc);
                else reduce = true;
            } else {
                weighted(contracted, 1 - psi * rho, centroid, psi * rho, worst.x);
                const double fc = eval(contracted);
                if (fc < fr) update(contracted, fc);
                else reduce = true;
            }
            if (reduce) {
                for (size_t i = 1; i < simplex.size(); ++i) {
                    weighted(simplex[i].x, 1 - sigma, simplex[0].x, sigma, simplex[i].x);
                    simplex[i].fx = eval(simplex[i].x);
                }
            }
        } else {
            update(reflected, fr);
        }
    }
    std::stable_sort(simplex.begin(), simplex.end(), by_fx);
    return simplex[0].x;
}

double Dot(const std::vector<double>& a, const std::vector<double>& b) {
    double out = 0;
    for (size_t i = 0; i < a.size(); ++i) out += a[i] * b[i];
    return out;
}

void WeightedSum(std::vector<double>& out, double w1, const std::vector<double>& v1, double w2, const std::vector<double>& v2) {
    for (size_t j = 0; j < out.size(); ++j) out[j] = w1 * v1[j] + w2 * v2[j];
}

// fmin's conjugateGradient (Polak-Ribiere, Wolfe line search).
struct CgPoint {
    std::vector<double> x, fxprime;
    double fx = 0;
};
using GradFn = std::function<double(const std::vector<double>&, std::vector<double>&)>;

double WolfeLineSearch(const GradFn& f, const std::vector<double>& pk, const CgPoint& current, CgPoint& next, double a) {
    const double phi0 = current.fx, phi_prime0 = Dot(current.fxprime, pk);
    double phi = phi0, phi_old = phi0, phi_prime = phi_prime0, a0 = 0;
    if (a == 0 || std::isnan(a)) a = 1;
    constexpr double c1 = 1e-6, c2 = 0.1;
    auto zoom = [&](double lo, double high, double phi_lo) {
        for (int it = 0; it < 16; ++it) {
            a = (lo + high) / 2;
            WeightedSum(next.x, 1.0, current.x, a, pk);
            phi = next.fx = f(next.x, next.fxprime);
            phi_prime = Dot(next.fxprime, pk);
            if (phi > phi0 + c1 * a * phi_prime0 || phi >= phi_lo) {
                high = a;
            } else {
                if (std::fabs(phi_prime) <= -c2 * phi_prime0) return a;
                if (phi_prime * (high - lo) >= 0) high = lo;
                lo = a;
                phi_lo = phi;
            }
        }
        return 0.0;
    };
    for (int it = 0; it < 10; ++it) {
        WeightedSum(next.x, 1.0, current.x, a, pk);
        phi = next.fx = f(next.x, next.fxprime);
        phi_prime = Dot(next.fxprime, pk);
        if (phi > phi0 + c1 * a * phi_prime0 || (it && phi >= phi_old)) return zoom(a0, a, phi_old);
        if (std::fabs(phi_prime) <= -c2 * phi_prime0) return a;
        if (phi_prime >= 0) return zoom(a, a0, phi);
        phi_old = phi;
        a0 = a;
        a *= 2;
    }
    return a;
}

CgPoint ConjugateGradient(const GradFn& f, const std::vector<double>& initial, int max_iterations) {
    CgPoint current{initial, initial}, next{initial, initial};
    std::vector<double> yk = initial, pk(initial.size());
    double a = 1;
    current.fx = f(current.x, current.fxprime);
    for (size_t i = 0; i < pk.size(); ++i) pk[i] = -current.fxprime[i];
    for (int it = 0; it < max_iterations; ++it) {
        a = WolfeLineSearch(f, pk, current, next, a);
        if (a == 0 || std::isnan(a)) {
            for (size_t i = 0; i < pk.size(); ++i) pk[i] = -current.fxprime[i];
        } else {
            WeightedSum(yk, 1, next.fxprime, -1, current.fxprime);
            const double delta = Dot(current.fxprime, current.fxprime), ratio = Dot(yk, next.fxprime) / delta;
            const double beta = std::isnan(ratio) ? ratio : std::max(0.0, ratio);
            WeightedSum(pk, beta, pk, -1, next.fxprime);
            std::swap(current, next);
        }
        if (std::sqrt(Dot(current.fxprime, current.fxprime)) <= 1e-5) break;
    }
    return current;
}

// The sets and what they share, laid out as venn.js does.
class VennLayout {
public:
    // `names` and `sizes`: the sets in JavaScript's key order; `areas` in the
    // data's order.
    VennLayout(const std::vector<std::wstring>& names, const std::vector<double>& sizes, std::vector<Area> areas)
        : names_(names), sizes_(sizes), areas_(std::move(areas)) {}

    std::vector<Circle> Solve() {
        AddMissingAreas();
        double work = 0;
        for (const Area& a : areas_) work += a.sets.size() < 3 ? 1.0 : std::pow(static_cast<double>(a.sets.size()), 3);
        max_evals_ = static_cast<size_t>(std::max(100.0, 1e8 / std::max(work, 1.0)));
        std::vector<Circle> circles = Greedy();
        if (areas_.size() >= 8) {
            std::vector<Circle> mds = ConstrainedMds();
            if (Loss(mds, areas_) + 1e-8 < Loss(circles, areas_)) circles = std::move(mds);
        }
        std::vector<double> x;
        for (const Circle& c : circles) {
            x.push_back(c.x);
            x.push_back(c.y);
        }
        std::vector<Circle> probe = circles;
        const std::vector<double> best = NelderMead(
            [&](const std::vector<double>& v) {
                for (size_t i = 0; i < probe.size(); ++i) {
                    probe[i].x = v[2 * i];
                    probe[i].y = v[2 * i + 1];
                }
                return Loss(probe, areas_);
            },
            x, 500, 1e-6, 1e-5, max_evals_);
        for (size_t i = 0; i < circles.size(); ++i) {
            circles[i].x = best[2 * i];
            circles[i].y = best[2 * i + 1];
        }
        return circles;
    }

private:
    // Pairs not given: not overlapping.
    void AddMissingAreas() {
        std::set<std::pair<int, int>> pairs;
        std::vector<int> ids;
        for (const Area& a : areas_) {
            if (a.sets.size() == 1) ids.push_back(a.sets[0]);
            else if (a.sets.size() == 2) {
                pairs.insert({a.sets[0], a.sets[1]});
                pairs.insert({a.sets[1], a.sets[0]});
            }
        }
        std::stable_sort(ids.begin(), ids.end(), [&](int l, int r) { return names_[static_cast<size_t>(l)] < names_[static_cast<size_t>(r)]; });
        for (size_t i = 0; i < ids.size(); ++i) {
            for (size_t j = i + 1; j < ids.size(); ++j) {
                if (!pairs.contains({ids[i], ids[j]})) areas_.push_back(Area{{ids[i], ids[j]}, 0});
            }
        }
    }

    std::vector<Area> Pairs() const {
        std::vector<Area> out;
        for (const Area& a : areas_) {
            if (a.sets.size() == 2) out.push_back(a);
        }
        return out;
    }

    // Most overlapped first, each where its overlaps with those placed are
    // best.
    std::vector<Circle> Greedy() const {
        const size_t n = sizes_.size();
        std::vector<Circle> circles(n);
        for (size_t i = 0; i < n; ++i) circles[i] = Circle{1e10, 1e10, std::sqrt(sizes_[i] / kPi)};
        struct Overlap {
            int set;
            double size, weight;
        };
        std::vector<std::vector<Overlap>> overlaps(n);
        const std::vector<Area> pairs = Pairs();
        for (const Area& p : pairs) {
            const int l = p.sets[0], r = p.sets[1];
            // (Subsets are left for later.)
            const double weight = p.size + kSmall >= std::min(sizes_[static_cast<size_t>(l)], sizes_[static_cast<size_t>(r)]) ? 0 : 1;
            overlaps[static_cast<size_t>(l)].push_back({r, p.size, weight});
            overlaps[static_cast<size_t>(r)].push_back({l, p.size, weight});
        }
        struct Most {
            int set;
            double size;
        };
        std::vector<Most> most;
        for (size_t i = 0; i < n; ++i) {
            double size = 0;
            for (const Overlap& o : overlaps[i]) size += o.size * o.weight;
            most.push_back({static_cast<int>(i), size});
        }
        std::stable_sort(most.begin(), most.end(), [](const Most& a, const Most& b) { return b.size < a.size; });
        std::vector<char> placed(n, 0);
        if (n == 0) return circles;
        circles[static_cast<size_t>(most[0].set)].x = circles[static_cast<size_t>(most[0].set)].y = 0;
        placed[static_cast<size_t>(most[0].set)] = 1;
        for (size_t i = 1; i < n; ++i) {
            const size_t s = static_cast<size_t>(most[i].set);
            std::vector<Overlap> ov;
            for (const Overlap& o : overlaps[s]) {
                if (placed[static_cast<size_t>(o.set)]) ov.push_back(o);
            }
            std::stable_sort(ov.begin(), ov.end(), [](const Overlap& a, const Overlap& b) { return b.size < a.size; });
            std::vector<IPoint> points;
            for (size_t j = 0; j < ov.size(); ++j) {
                const Circle& p1 = circles[static_cast<size_t>(ov[j].set)];
                const double d1 = DistanceFromIntersectArea(circles[s].r, p1.r, ov[j].size);
                points.push_back({p1.x + d1, p1.y});
                points.push_back({p1.x - d1, p1.y});
                points.push_back({p1.x, p1.y + d1});
                points.push_back({p1.x, p1.y - d1});
                for (size_t k = j + 1; k < ov.size(); ++k) {
                    const Circle& p2 = circles[static_cast<size_t>(ov[k].set)];
                    const double d2 = DistanceFromIntersectArea(circles[s].r, p2.r, ov[k].size);
                    CircleCircle(Circle{p1.x, p1.y, d1}, Circle{p2.x, p2.y, d2}, 0, 0, &points);
                }
            }
            if (points.empty()) points.push_back({0, 0});  // (not for data with all pairs)
            double best_loss = 1e50;
            IPoint best = points[0];
            for (const IPoint& p : points) {
                circles[s].x = p.x;
                circles[s].y = p.y;
                const double loss = Loss(circles, pairs);
                if (loss < best_loss) {
                    best_loss = loss;
                    best = p;
                }
            }
            circles[s].x = best.x;
            circles[s].y = best.y;
            placed[s] = 1;
        }
        return circles;
    }

    // Constrained MDS from random starts (venn.js's Math.random seeded here).
    std::vector<Circle> ConstrainedMds() {
        const size_t n = sizes_.size();
        std::vector<std::vector<double>> distances(n, std::vector<double>(n, 0)), constraints = distances;
        for (const Area& p : Pairs()) {
            const size_t l = static_cast<size_t>(p.sets[0]), r = static_cast<size_t>(p.sets[1]);
            const double d = DistanceFromIntersectArea(std::sqrt(sizes_[l] / kPi), std::sqrt(sizes_[r] / kPi), p.size);
            distances[l][r] = distances[r][l] = d;
            double c = 0;
            if (p.size + 1e-10 >= std::min(sizes_[l], sizes_[r])) c = 1;
            else if (p.size <= 1e-10) c = -1;
            constraints[l][r] = constraints[r][l] = c;
        }
        double sum = 0;
        for (const auto& row : distances) sum += Dot(row, row);
        const double norm = std::sqrt(sum) / static_cast<double>(n);
        for (auto& row : distances) {
            for (double& v : row) v /= norm;
        }
        const GradFn f = [&](const std::vector<double>& x, std::vector<double>& fxprime) {
            std::fill(fxprime.begin(), fxprime.end(), 0.0);
            double loss = 0;
            for (size_t i = 0; i < n; ++i) {
                const double xi = x[2 * i], yi = x[2 * i + 1];
                for (size_t j = i + 1; j < n; ++j) {
                    const double xj = x[2 * j], yj = x[2 * j + 1], dij = distances[i][j], constraint = constraints[i][j];
                    const double squared = (xj - xi) * (xj - xi) + (yj - yi) * (yj - yi), dist = std::sqrt(squared), delta = squared - dij * dij;
                    if ((constraint > 0 && dist <= dij) || (constraint < 0 && dist >= dij)) continue;
                    loss += 2 * delta * delta;
                    fxprime[2 * i] += 4 * delta * (xi - xj);
                    fxprime[2 * i + 1] += 4 * delta * (yi - yj);
                    fxprime[2 * j] += 4 * delta * (xj - xi);
                    fxprime[2 * j + 1] += 4 * delta * (yj - yi);
                }
            }
            return loss;
        };
        std::optional<CgPoint> best;
        for (int restart = 0; restart < 10; ++restart) {
            std::vector<double> initial(2 * n);
            for (double& v : initial) v = Random();
            CgPoint current = ConjugateGradient(f, initial, 500);
            if (!best || current.fx < best->fx) best = std::move(current);
        }
        std::vector<Circle> circles(n);
        for (size_t i = 0; i < n; ++i) circles[i] = Circle{best->x[2 * i] * norm, best->x[2 * i + 1] * norm, std::sqrt(sizes_[i] / kPi)};
        return circles;
    }

    // mulberry32, seeded the same each time.
    double Random() {
        uint32_t t = (seed_ += 0x6D2B79F5u);
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
    }

    const std::vector<std::wstring>& names_;
    const std::vector<double>& sizes_;
    std::vector<Area> areas_;
    size_t max_evals_ = 0;
    uint32_t seed_ = 0x2F6B3C1Du;
};

struct Bounds {
    double x0, x1, y0, y1;
};
Bounds BoundsOf(const std::vector<Circle>& cs, const std::vector<int>& which) {
    Bounds b{INFINITY, -INFINITY, INFINITY, -INFINITY};
    for (const int i : which) {
        const Circle& c = cs[static_cast<size_t>(i)];
        b.x1 = std::max(b.x1, c.x + c.r);
        b.x0 = std::min(b.x0, c.x - c.r);
        b.y1 = std::max(b.y1, c.y + c.r);
        b.y0 = std::min(b.y0, c.y - c.r);
    }
    return b;
}

// venn.js's orientateCircles: the largest at the origin, the next to its
// right... (orientation pi / 2), the third below.
void Orientate(std::vector<Circle>& cs, std::vector<int>& cluster) {
    std::stable_sort(cluster.begin(), cluster.end(), [&](int a, int b) { return cs[static_cast<size_t>(b)].r < cs[static_cast<size_t>(a)].r; });
    auto at = [&](size_t k) -> Circle& { return cs[static_cast<size_t>(cluster[k])]; };
    const double lx = at(0).x, ly = at(0).y;
    for (const int i : cluster) {
        cs[static_cast<size_t>(i)].x -= lx;
        cs[static_cast<size_t>(i)].y -= ly;
    }
    if (cluster.size() == 2 && Distance(at(0), at(1)) < std::fabs(at(1).r - at(0).r)) {
        at(1).x = at(0).x + at(0).r - at(1).r - 1e-10;
        at(1).y = at(0).y;
    }
    constexpr double orientation = kPi / 2;
    if (cluster.size() > 1) {
        const double rotation = std::atan2(at(1).x, at(1).y) - orientation, c = std::cos(rotation), s = std::sin(rotation);
        for (const int i : cluster) {
            Circle& k = cs[static_cast<size_t>(i)];
            const double x = k.x, y = k.y;
            k.x = c * x - s * y;
            k.y = s * x + c * y;
        }
    }
    if (cluster.size() > 2) {
        double angle = std::atan2(at(2).x, at(2).y) - orientation;
        while (angle < 0) angle += 2 * kPi;
        while (angle > 2 * kPi) angle -= 2 * kPi;
        if (angle > kPi) {
            const double slope = at(1).y / (1e-10 + at(1).x);
            for (const int i : cluster) {
                Circle& k = cs[static_cast<size_t>(i)];
                const double d = (k.x + slope * k.y) / (1 + slope * slope);
                k.x = 2 * d - k.x;
                k.y = 2 * d * slope - k.y;
            }
        }
    }
}

// venn.js's normalizeSolution: clusters of overlapping circles turned, the
// largest first, the others beside and below it.
std::vector<Circle> Normalize(std::vector<Circle> cs) {
    const size_t n = cs.size();
    std::vector<int> parent(n);
    for (size_t i = 0; i < n; ++i) parent[i] = static_cast<int>(i);
    auto find = [&](int i) {
        int root = i;
        while (parent[static_cast<size_t>(root)] != root) root = parent[static_cast<size_t>(root)];
        while (parent[static_cast<size_t>(i)] != root) {
            const int up = parent[static_cast<size_t>(i)];
            parent[static_cast<size_t>(i)] = root;
            i = up;
        }
        return root;
    };
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            if (Distance(cs[i], cs[j]) + 1e-10 < cs[i].r + cs[j].r) parent[static_cast<size_t>(find(static_cast<int>(j)))] = find(static_cast<int>(i));
        }
    }
    struct Cluster {
        std::vector<int> members;
        Bounds bounds{};
        double size = 0;
    };
    std::vector<Cluster> clusters;
    std::vector<int> root_cluster(n, -1);
    for (size_t i = 0; i < n; ++i) {
        const int root = find(static_cast<int>(i));
        int& k = root_cluster[static_cast<size_t>(root)];
        if (k < 0) {
            k = static_cast<int>(clusters.size());
            clusters.emplace_back();
        }
        clusters[static_cast<size_t>(k)].members.push_back(static_cast<int>(i));
    }
    for (Cluster& c : clusters) {
        Orientate(cs, c.members);
        c.bounds = BoundsOf(cs, c.members);
        c.size = (c.bounds.x1 - c.bounds.x0) * (c.bounds.y1 - c.bounds.y0);
    }
    std::stable_sort(clusters.begin(), clusters.end(), [](const Cluster& a, const Cluster& b) { return b.size < a.size; });
    if (clusters.empty()) return cs;
    std::vector<int> placed = clusters[0].members;
    Bounds ret = clusters[0].bounds;
    const double spacing = (ret.x1 - ret.x0) / 50;
    auto add = [&](size_t k, bool right, bool bottom) {
        if (k >= clusters.size()) return;
        const Bounds& b = clusters[k].bounds;
        double dx, dy;
        if (right) {
            dx = ret.x1 - b.x0 + spacing;
        } else {
            dx = ret.x1 - b.x1;
            const double centring = (b.x1 - b.x0) / 2 - (ret.x1 - ret.x0) / 2;
            if (centring < 0) dx += centring;
        }
        if (bottom) {
            dy = ret.y1 - b.y0 + spacing;
        } else {
            dy = ret.y1 - b.y1;
            const double centring = (b.y1 - b.y0) / 2 - (ret.y1 - ret.y0) / 2;
            if (centring < 0) dy += centring;
        }
        for (const int i : clusters[k].members) {
            cs[static_cast<size_t>(i)].x += dx;
            cs[static_cast<size_t>(i)].y += dy;
            placed.push_back(i);
        }
    };
    for (size_t k = 1; k < clusters.size(); k += 3) {
        add(k, true, false);
        add(k + 1, false, true);
        add(k + 2, true, true);
        ret = BoundsOf(cs, placed);
    }
    return cs;
}

// venn.js's scaleSolution: fitted into width x height, centred.
std::vector<Circle> Fit(const std::vector<Circle>& cs, double width, double height, double padding) {
    width -= 2 * padding;
    height -= 2 * padding;
    std::vector<int> all(cs.size());
    for (size_t i = 0; i < cs.size(); ++i) all[i] = static_cast<int>(i);
    const Bounds b = BoundsOf(cs, all);
    if (b.x1 == b.x0 || b.y1 == b.y0) return cs;
    const double scaling = std::min(height / (b.y1 - b.y0), width / (b.x1 - b.x0));
    const double dx = (width - (b.x1 - b.x0) * scaling) / 2, dy = (height - (b.y1 - b.y0) * scaling) / 2;
    std::vector<Circle> out;
    for (const Circle& c : cs) out.push_back(Circle{padding + dx + (c.x - b.x0) * scaling, padding + dy + (c.y - b.y0) * scaling, c.r * scaling});
    return out;
}

struct Centre {
    double x = 0, y = 0;
    bool disjoint = false;
};

double Margin(double x, double y, const std::vector<Circle>& interior, const std::vector<Circle>& exterior) {
    double margin = interior[0].r - Distance(interior[0].x, interior[0].y, x, y);
    for (size_t i = 1; i < interior.size(); ++i) margin = std::min(margin, interior[i].r - Distance(interior[i].x, interior[i].y, x, y));
    for (const Circle& e : exterior) margin = std::min(margin, Distance(e.x, e.y, x, y) - e.r);
    return margin;
}

// venn.js's computeTextCentre: the point deepest inside the region.
Centre TextCentre(const std::vector<Circle>& interior, const std::vector<Circle>& exterior) {
    std::vector<std::pair<double, double>> points;
    for (const Circle& c : interior) {
        points.push_back({c.x, c.y});
        points.push_back({c.x + c.r / 2, c.y});
        points.push_back({c.x - c.r / 2, c.y});
        points.push_back({c.x, c.y + c.r / 2});
        points.push_back({c.x, c.y - c.r / 2});
    }
    std::pair<double, double> initial = points[0];
    double margin = Margin(initial.first, initial.second, interior, exterior);
    for (size_t i = 1; i < points.size(); ++i) {
        const double m = Margin(points[i].first, points[i].second, interior, exterior);
        if (m >= margin) {
            initial = points[i];
            margin = m;
        }
    }
    const std::vector<double> best = NelderMead([&](const std::vector<double>& p) { return -Margin(p[0], p[1], interior, exterior); },
                                                {initial.first, initial.second}, 500, 1e-10, 1e-10, 1000000);
    const Centre ret{best[0], best[1]};
    bool valid = true;
    for (const Circle& c : interior) {
        if (Distance(ret.x, ret.y, c.x, c.y) > c.r) valid = false;
    }
    for (const Circle& c : exterior) {
        if (Distance(ret.x, ret.y, c.x, c.y) < c.r) valid = false;
    }
    if (valid) return ret;
    if (interior.size() == 1) return Centre{interior[0].x, interior[0].y};
    std::vector<Arc> arcs;
    IntersectionArea(interior, &arcs);
    if (arcs.empty()) return Centre{0, -1000, true};
    if (arcs.size() == 1) return Centre{arcs[0].c.x, arcs[0].c.y};
    if (!exterior.empty()) return TextCentre(interior, {});
    Centre mean;
    for (const Arc& a : arcs) {
        mean.x += a.p1.x;
        mean.y += a.p1.y;
    }
    mean.x /= static_cast<double>(arcs.size());
    mean.y /= static_cast<double>(arcs.size());
    return mean;
}

std::vector<Centre> TextCentres(const std::vector<Circle>& cs, const std::vector<Area>& areas) {
    // Which circles hold each whole.
    const size_t n = cs.size();
    std::vector<std::vector<int>> holders(n);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            const double d = Distance(cs[i], cs[j]);
            if (d + cs[j].r <= cs[i].r + 1e-10) holders[j].push_back(static_cast<int>(i));
            else if (d + cs[i].r <= cs[j].r + 1e-10) holders[i].push_back(static_cast<int>(j));
        }
    }
    std::vector<Centre> out;
    for (const Area& area : areas) {
        std::vector<char> in(n, 0), exclude(n, 0);
        for (const int s : area.sets) {
            in[static_cast<size_t>(s)] = 1;
            for (const int h : holders[static_cast<size_t>(s)]) exclude[static_cast<size_t>(h)] = 1;
        }
        std::vector<Circle> interior, exterior;
        for (size_t i = 0; i < n; ++i) {
            if (in[i]) interior.push_back(cs[i]);
            else if (!exclude[i]) exterior.push_back(cs[i]);
        }
        out.push_back(TextCentre(interior, exterior));
    }
    return out;
}

// ---------------------------------------------------------------------------

using M = SceneBuilder::Measured;

constexpr float kWidth = 800, kHeight = 450;
constexpr float kScale = 0.5f;  // Mermaid's: the width over 1600

// JavaScript's key order: names that are array indices first, by value.
bool ArrayIndex(const std::wstring& s, double* value) {
    if (s.empty() || s.size() > 10 || (s.size() > 1 && s[0] == L'0')) return false;
    double v = 0;
    for (const wchar_t c : s) {
        if (!Digit(c)) return false;
        v = v * 10 + (c - L'0');
    }
    *value = v;
    return v < 4294967295.0;
}

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b) : p_(p), b_(b) {}

    std::shared_ptr<DiagramScene> Build() {
        const bool titled = !p_.title.raw.empty();
        const float title_h = titled ? 48 * kScale : 0;
        if (titled) {
            // (32px, as Mermaid's style has it, middle at 16.)
            const M m = Measure(p_.title.raw, 32);
            b_.Text(m, D2D1::Point2F(kWidth / 2 - m.w / 2, 16 + 0.265f * 32 - 1.17f * 32), Role(kDgText), p_.title.pos, p_.title.len);
        }
        // Mermaid's data: the subsets, pairs the unions of more imply; sets
        // of size 0 left out, and what has them.
        std::vector<Subset> data = p_.subsets;
        std::map<std::wstring, double> single_size;
        std::set<std::vector<std::wstring>> have;
        for (const Subset& s : data) {
            have.insert(s.sets);
            if (s.sets.size() == 1) single_size[s.sets[0]] = s.size;
        }
        const size_t declared = data.size();
        for (size_t k = 0; k < declared; ++k) {
            if (data[k].sets.size() < 3) continue;
            const std::vector<std::wstring> members = data[k].sets;
            for (size_t i = 0; i + 1 < members.size(); ++i) {
                for (size_t j = i + 1; j < members.size(); ++j) {
                    std::vector<std::wstring> pair{members[i], members[j]};
                    if (!have.insert(pair).second) continue;
                    Subset s;
                    s.sets = std::move(pair);
                    s.size = single_size.contains(members[i]) && single_size.contains(members[j])
                                 ? std::min(single_size[members[i]], single_size[members[j]]) / 4
                                 : 2.5;
                    if (data.size() >= kMaxAreas) TooLarge();
                    data.push_back(std::move(s));
                }
            }
        }
        std::set<std::wstring> empty;
        for (const Subset& s : data) {
            if (s.sets.size() == 1 && s.size == 0) empty.insert(s.sets[0]);
        }
        // What is drawn, in order: as declared, then the pairs added.
        std::vector<size_t> order = p_.drawn;
        for (size_t k = declared; k < data.size(); ++k) order.push_back(k);
        std::vector<Subset> kept;
        std::vector<int> moved(data.size(), -1);
        for (size_t k = 0; k < data.size(); ++k) {
            if (std::any_of(data[k].sets.begin(), data[k].sets.end(), [&](const std::wstring& n) { return empty.contains(n); })) continue;
            moved[k] = static_cast<int>(kept.size());
            kept.push_back(std::move(data[k]));
        }
        std::vector<size_t> shown;
        for (const size_t k : order) {
            if (moved[k] >= 0) shown.push_back(static_cast<size_t>(moved[k]));
        }
        if (!kept.empty()) Draw(kept, shown, title_h);
        return b_.Finish(kWidth, kHeight);
    }

private:
    void Draw(const std::vector<Subset>& data, const std::vector<size_t>& shown, float title_h) {
        // The sets in JavaScript's key order.
        std::vector<std::wstring> names;
        std::vector<double> sizes;
        for (const Subset& s : data) {
            if (s.sets.size() == 1) {
                names.push_back(s.sets[0]);
                sizes.push_back(s.size);
            }
        }
        std::vector<size_t> order(names.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            double va = 0, vb = 0;
            const bool ia = ArrayIndex(names[a], &va), ib = ArrayIndex(names[b], &vb);
            if (ia != ib) return ia;
            return ia && va < vb;
        });
        std::vector<std::wstring> key_names;
        std::vector<double> key_sizes;
        std::map<std::wstring, int> index;
        for (const size_t i : order) {
            index[names[i]] = static_cast<int>(key_names.size());
            key_names.push_back(names[i]);
            key_sizes.push_back(sizes[i]);
        }
        std::vector<Area> areas;
        double work = 0;
        for (const Subset& s : data) {
            Area a;
            for (const std::wstring& n : s.sets) a.sets.push_back(index.at(n));
            a.size = s.size;
            work += a.sets.size() < 3 ? 1.0 : std::pow(static_cast<double>(a.sets.size()), 3);
            areas.push_back(std::move(a));
        }
        // Laid out with the earlier sizes of unions declared again too.
        std::vector<Area> fitted = areas;
        for (const auto& [sets, size] : p_.earlier) {
            if (!std::all_of(sets.begin(), sets.end(), [&](const std::wstring& n) { return index.contains(n); })) continue;
            Area a;
            for (const std::wstring& n : sets) a.sets.push_back(index.at(n));
            a.size = size;
            work += a.sets.size() < 3 ? 1.0 : std::pow(static_cast<double>(a.sets.size()), 3);
            fitted.push_back(std::move(a));
        }
        if (work > kMaxWork) TooLarge();
        VennLayout venn(key_names, key_sizes, fitted);
        const std::vector<Circle> solution = Normalize(venn.Solve());
        // Drawn fitted with padding 15; text nodes placed by a layout with
        // Mermaid's padding (8).
        const std::vector<Circle> circles = Fit(solution, kWidth, kHeight - title_h, 15);
        const std::vector<Circle> text_circles = Fit(solution, kWidth, kHeight - title_h, 8);
        for (const std::vector<Circle>* cs : {&circles, &text_circles}) {
            for (const Circle& c : *cs) {
                if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.r) || std::fabs(c.x) > 1e6 || std::fabs(c.y) > 1e6 || c.r > 1e6) {
                    Fail(kNoLine, Tr(L"The sizes can't be drawn", L"Эти размеры не нарисовать"));
                }
            }
        }
        const std::vector<Centre> centres = TextCentres(circles, areas);
        auto style = [&](const std::wstring& key, const wchar_t* prop) -> const std::wstring* {
            const auto it = p_.styles.find(key);
            if (it == p_.styles.end()) return nullptr;
            const auto v = it->second.find(prop);
            return v == it->second.end() ? nullptr : &v->second;
        };
        auto color = [&](const std::wstring* v, uint32_t* argb) { return v && ParseCssColor(*v, argb); };
        const auto y = [&](double v) { return static_cast<float>(v) + title_h; };
        int nth = 0;  // circles so far
        for (const size_t k : shown) {
            const Subset& s = data[k];
            const Area& a = areas[k];
            const std::wstring key = Parser::Key(s.sets);
            uint32_t fill = 0, stroke = 0, text = 0;
            const bool custom_fill = color(style(key, L"fill"), &fill), custom_text = color(style(key, L"color"), &text);
            DgColor text_color = custom_text ? Argb(text) : Role(kDgText);
            if (a.sets.size() == 1) {
                const Circle& c = circles[static_cast<size_t>(a.sets[0])];
                const uint8_t role = static_cast<uint8_t>(kDgVenn + nth % 8);
                double opacity = 0.1;
                if (const std::wstring* v = style(key, L"fill-opacity")) {
                    wchar_t* end = nullptr;
                    opacity = std::wcstod(v->c_str(), &end);
                    if (end == v->c_str() || !std::isfinite(opacity)) opacity = 0.25;  // (venn.js's, the CSS left)
                    else if (*end == L'%') opacity /= 100;
                    opacity = std::clamp(opacity, 0.0, 1.0);
                }
                const DgColor fill_color = custom_fill ? Argb(Alpha(fill, opacity)) : RoleAt(role, static_cast<uint8_t>(std::lround(opacity * 255)));
                DgColor stroke_color = custom_fill ? Argb(Alpha(fill, 0.95)) : RoleAt(role, 242);
                if (const std::wstring* v = style(key, L"stroke")) stroke_color = ParseCssColor(*v, &stroke) ? Argb(Alpha(stroke, 0.95)) : Role(kDgNone);
                float width = 5 * kScale;
                if (const std::wstring* v = style(key, L"stroke-width")) {
                    wchar_t* end = nullptr;
                    const double w = std::wcstod(v->c_str(), &end);
                    width = end != v->c_str() && std::isfinite(w) && w >= 0 ? static_cast<float>(std::min(w, 20.0)) : 1.f;
                }
                b_.Ellipse(D2D1::RectF(static_cast<float>(c.x - c.r), y(c.y - c.r), static_cast<float>(c.x + c.r), y(c.y + c.r)), fill_color, stroke_color, width);
                if (!custom_text) text_color = custom_fill ? Shade(fill | 0xFF000000u) : Role(static_cast<uint8_t>(kDgVennText + nth % 8));
                ++nth;
            } else if (custom_fill) {
                std::vector<Circle> some;
                for (const int i : a.sets) some.push_back(circles[static_cast<size_t>(i)]);
                std::vector<Arc> arcs;
                IntersectionArea(some, &arcs);
                Outline(arcs, title_h, Argb(fill));
            }
            // Its label (a set: its name without one).
            const Text& label = !s.label.raw.empty() ? s.label : s.sets.size() == 1 ? s.name : s.label;
            const Centre& at = centres[k];
            if (label.raw.empty() || at.disjoint) continue;
            const M m = Measure(label.raw, 48 * kScale);
            const float baseline = y(std::floor(at.y)) + 0.35f * 48 * kScale;
            b_.Text(m, D2D1::Point2F(static_cast<float>(std::floor(at.x)) - m.w / 2, baseline - 1.17f * 48 * kScale), text_color, label.pos, label.len);
        }
        if (!p_.texts.empty()) TextNodes(data, areas, text_circles, TextCentres(text_circles, areas), title_h);
    }

    // Text nodes: by region, in a grid in its middle.
    void TextNodes(const std::vector<Subset>& data, const std::vector<Area>& areas, const std::vector<Circle>& cs, const std::vector<Centre>& centres,
                   float title_h) {
        std::vector<std::pair<std::wstring, std::vector<const TextNode*>>> groups;
        for (const TextNode& n : p_.texts) {
            const std::wstring key = Parser::Key(n.sets);
            auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& g) { return g.first == key; });
            if (it == groups.end()) {
                groups.push_back({key, {}});
                it = groups.end() - 1;
            }
            it->second.push_back(&n);
        }
        const float px = 40 * kScale;
        for (const auto& [key, nodes] : groups) {
            size_t k = 0;
            while (k < data.size() && Parser::Key(data[k].sets) != key) ++k;
            if (k == data.size()) continue;
            const double cx = centres[k].x, cy = centres[k].y;
            double min_r = INFINITY, inner = INFINITY;
            for (const int i : areas[k].sets) {
                const Circle& c = cs[static_cast<size_t>(i)];
                min_r = std::min(min_r, c.r);
                inner = std::min(inner, c.r - std::hypot(cx - c.x, cy - c.y));
            }
            inner = std::isfinite(inner) ? std::max(0.0, inner) : 0;
            if (inner == 0 && std::isfinite(min_r)) inner = min_r * 0.6;
            const double inner_w = std::max(80.0 * kScale, inner * 2 * 0.95), inner_h = std::max(60.0 * kScale, inner * 2 * 0.95);
            const double label_offset = (data[k].label.raw.empty() ? 0 : std::min(32.0 * kScale, inner * 0.25)) + (nodes.size() <= 2 ? 30.0 * kScale : 0);
            const double start_x = cx - inner_w / 2, start_y = cy - inner_h / 2 + label_offset;
            const size_t cols = std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(static_cast<double>(nodes.size())))));
            const size_t rows = std::max<size_t>(1, (nodes.size() + cols - 1) / cols);
            const double cell_w = inner_w / static_cast<double>(cols), cell_h = inner_h / static_cast<double>(rows);
            for (size_t i = 0; i < nodes.size(); ++i) {
                const TextNode& n = *nodes[i];
                const double x = start_x + cell_w * (static_cast<double>(i % cols) + 0.5), yy = start_y + cell_h * (static_cast<double>(i / cols) + 0.5);
                const Text& t = n.has_label ? n.label : n.id;
                if (t.raw.empty()) continue;
                M m = Measure(t.raw, px, static_cast<float>(cell_w * 0.9));
                uint32_t argb = 0;
                const auto st = p_.styles.find(n.id.raw);
                const bool custom = st != p_.styles.end() && st->second.contains(L"color") && ParseCssColor(st->second.at(L"color"), &argb);
                b_.Text(m, D2D1::Point2F(static_cast<float>(x) - m.w / 2, static_cast<float>(yy) + title_h - m.h / 2), custom ? Argb(argb) : Role(kDgText), t.pos,
                        t.len);
            }
        }
    }

    // A region's outline: arcs clockwise, as cubic curves.
    void Outline(const std::vector<Arc>& arcs, float title_h, DgColor fill) {
        if (arcs.empty()) return;
        auto pt = [&](double x, double y) { return D2D1::Point2F(static_cast<float>(x), static_cast<float>(y) + title_h); };
        if (arcs.size() == 1) {
            const Circle& c = arcs[0].c;
            b_.Ellipse(D2D1::RectF(static_cast<float>(c.x - c.r), static_cast<float>(c.y - c.r) + title_h, static_cast<float>(c.x + c.r), static_cast<float>(c.y + c.r) + title_h),
                       fill, Role(kDgNone));
            return;
        }
        b_.Begin(pt(arcs[0].p2.x, arcs[0].p2.y));
        for (const Arc& arc : arcs) {
            const Circle& c = arc.c;
            const double a0 = std::atan2(arc.p2.y - c.y, arc.p2.x - c.x);
            double sweep = std::atan2(arc.p1.y - c.y, arc.p1.x - c.x) - a0;
            while (sweep < 0) sweep += 2 * kPi;
            const int parts = std::max(1, static_cast<int>(std::ceil(sweep / (kPi / 2))));
            const double step = sweep / parts, k = 4.0 / 3 * std::tan(step / 4);
            for (int i = 0; i < parts; ++i) {
                const double s = a0 + step * i, e = s + step;
                b_.CubicTo(pt(c.x + c.r * (std::cos(s) - k * std::sin(s)), c.y + c.r * (std::sin(s) + k * std::cos(s))),
                           pt(c.x + c.r * (std::cos(e) + k * std::sin(e)), c.y + c.r * (std::sin(e) - k * std::cos(e))), pt(c.x + c.r * std::cos(e), c.y + c.r * std::sin(e)));
            }
        }
        b_.Close();
        b_.End(fill, Role(kDgNone));
    }

    // Plain text (SVG text: no markup) at `px`; wrapped at `wrap` (HTML's
    // line height).
    M Measure(const std::wstring& raw, float px, float wrap = 0) {
        RichText t;
        if (wrap > 0) {
            // Whole words: a longer one is a line of its own, overflowing.
            for (size_t s = 0; s < raw.size();) {
                size_t e = raw.find(L' ', s);
                if (e == std::wstring::npos) e = raw.size();
                if (e > s) {
                    t.text = raw.substr(s, e - s);
                    wrap = std::max(wrap, b_.Measure(t, 0, false, px / b_.size()).w + 1);
                }
                s = e + 1;
            }
        }
        t.text = DecodeEntities(raw);
        M m = b_.Measure(t, wrap, false, px / b_.size());
        if (wrap > 0 && m.layout) {
            const float lh = px * 1.15f;
            m.layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, lh, lh * 0.8f);
            DWRITE_TEXT_METRICS tm{};
            if (SUCCEEDED(m.layout->GetMetrics(&tm))) m.h = tm.height;
        }
        return m;
    }

    static uint32_t Alpha(uint32_t argb, double opacity) {
        const double a = static_cast<double>(argb >> 24) * opacity;
        return (argb & 0xFFFFFF) | (static_cast<uint32_t>(std::lround(std::clamp(a, 0.0, 255.0))) << 24);
    }

    const Parser& p_;
    SceneBuilder& b_;
};

}  // namespace

std::shared_ptr<DiagramScene> VennDiagram(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder).Build();
}

}  // namespace mdv::mermaid
