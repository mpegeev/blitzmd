// Mermaid Wardley maps (wardley-beta): components placed by visibility and
// evolution, anchors, links with flows and labels, evolution trends,
// pipelines, notes, annotations, accelerators; the evolution stages as
// columns. Read with Mermaid's grammar (its lexer's tokens, first match
// wins) and drawn as its wardley renderer draws them.
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <map>
#include <optional>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

enum class T {
    Lbr, Comma, Rbr, At, Slash, Lpar, Rpar, Lbrace, Rbrace, Minus,
    Number, Arrow, Port, LinkArrow, LinkLabel, Strategy,
    KwWardley, KwSize, KwEvolution, KwAnchor, KwComponent, KwLabel, KwInertia, KwEvolve, KwPipeline, KwNote,
    KwAnnotations, KwAnnotation, KwAccelerator, KwDeaccelerator,
    Name, AccDescr, AccTitle, Title, Int, String, Id, Newline, Eof
};

struct Token {
    T kind = T::Eof;
    Text text;  // the value (strings unquoted, labels trimmed...)
};

// The words that are tokens of their own, in the lexer's order (no word
// boundary: "sizes" is "size" and "s").
constexpr std::pair<const wchar_t*, T> kWords[] = {
    {L"build", T::Strategy},         {L"buy", T::Strategy},           {L"outsource", T::Strategy},
    {L"market", T::Strategy},        {L"wardley-beta", T::KwWardley}, {L"size", T::KwSize},
    {L"evolution", T::KwEvolution},  {L"anchor", T::KwAnchor},        {L"component", T::KwComponent},
    {L"label", T::KwLabel},          {L"inertia", T::KwInertia},      {L"evolve", T::KwEvolve},
    {L"pipeline", T::KwPipeline},    {L"note", T::KwNote},            {L"annotations", T::KwAnnotations},
    {L"annotation", T::KwAnnotation}, {L"accelerator", T::KwAccelerator}, {L"deaccelerator", T::KwDeaccelerator},
};

bool Letter(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
bool Digit(wchar_t c) { return c >= L'0' && c <= L'9'; }
bool Word(wchar_t c) { return Letter(c) || Digit(c) || c == L'_'; }

class Lexer {
public:
    Lexer(std::wstring_view s, size_t pos) : s_(s), p_(pos) {}

    std::vector<Token> Run() {
        std::vector<Token> out;
        for (;;) {
            Token t = Next();
            if (out.size() > 200000) TooLarge();
            const bool end = t.kind == T::Eof;
            out.push_back(std::move(t));
            if (end) return out;
        }
    }

private:
    wchar_t At(size_t k) const { return p_ + k < s_.size() ? s_[p_ + k] : L'\0'; }
    bool Starts(std::wstring_view w) const { return s_.substr(p_).starts_with(w); }

    Token Make(T kind, size_t len) {
        Token t{kind, Text{std::wstring(s_.substr(p_, len)), static_cast<uint32_t>(p_), static_cast<uint32_t>(len)}};
        p_ += len;
        return t;
    }
    Token Value(T kind, size_t len, size_t from, size_t to) {
        while (from < to && (s_[from] == L' ' || s_[from] == L'\t')) ++from;
        while (to > from && (s_[to - 1] == L' ' || s_[to - 1] == L'\t')) --to;
        if (to - from > kMaxLabel) mermaid::Fail(from, Tr(L"Text too long", L"Слишком длинный текст"));
        Token t{kind, Text{std::wstring(s_.substr(from, to - from)), static_cast<uint32_t>(from), static_cast<uint32_t>(to - from)}};
        p_ += len;
        return t;
    }
    // Up to the line's end, or a %% before it.
    size_t LineValueEnd(size_t from) const {
        size_t e = from;
        while (e < s_.size() && s_[e] != L'\n' && s_[e] != L'\r') ++e;
        const size_t comment = s_.substr(from, e - from).find(L"%%");
        return comment == std::wstring_view::npos ? e : from + comment;
    }

    Token Next() {
        for (;;) {
            if (p_ >= s_.size()) return Token{T::Eof, Text{L"", static_cast<uint32_t>(p_), 0}};
            if (const size_t n = YamlBlockLength(s_, p_)) {
                p_ += n;
                continue;
            }
            const wchar_t c = s_[p_];
            if (c == L' ' || c == L'\t') {
                ++p_;
                continue;
            }
            if (c == L'%' && At(1) == L'%') {
                if (At(2) == L'{') {
                    const size_t e = s_.find(L"}%%", p_ + 3);
                    if (e != std::wstring_view::npos) {
                        p_ = e + 3;
                        continue;
                    }
                }
                while (p_ < s_.size() && s_[p_] != L'\n' && s_[p_] != L'\r') ++p_;
                continue;
            }
            break;
        }
        const wchar_t c = s_[p_];
        switch (c) {
        case L'[': return Make(T::Lbr, 1);
        case L',': return Make(T::Comma, 1);
        case L']': return Make(T::Rbr, 1);
        case L'@': return Make(T::At, 1);
        case L'/': return Make(T::Slash, 1);
        case L'(': return Make(T::Lpar, 1);
        case L')': return Make(T::Rpar, 1);
        case L'{': return Make(T::Lbrace, 1);
        case L'}': return Make(T::Rbrace, 1);
        case L'-':
            if (Starts(L"->")) return Make(T::Arrow, 2);
            if (Starts(L"-->")) return Make(T::LinkArrow, 3);
            if (Starts(L"-.->")) return Make(T::LinkArrow, 4);
            return Make(T::Minus, 1);
        default: break;
        }
        if (Digit(c)) {
            size_t e = p_;
            while (e < s_.size() && Digit(s_[e])) ++e;
            if (e + 1 < s_.size() && s_[e] == L'.' && Digit(s_[e + 1])) {
                ++e;
                while (e < s_.size() && Digit(s_[e])) ++e;
                return Make(T::Number, e - p_);
            }
        }
        if (c == L'+') {
            if (Starts(L"+<>")) return Make(T::Port, 3);
            if (Starts(L"+>") || Starts(L"+<")) return Make(T::Port, 2);
            if (At(1) == L'\'') {
                size_t e = p_ + 2;
                while (e < s_.size() && s_[e] != L'\'') ++e;
                if (e < s_.size()) {
                    if (s_.substr(e + 1).starts_with(L"<>")) return Make(T::LinkArrow, e + 3 - p_);
                    if (e + 1 < s_.size() && (s_[e + 1] == L'<' || s_[e + 1] == L'>')) return Make(T::LinkArrow, e + 2 - p_);
                }
            }
        }
        if (c == L'>') return Make(T::LinkArrow, 1);
        if (c == L';' && At(1) != L'\0' && At(1) != L'\n' && At(1) != L'\r') {
            size_t e = p_ + 1;
            while (e < s_.size() && s_[e] != L'\n' && s_[e] != L'\r') ++e;
            return Value(T::LinkLabel, e - p_, p_ + 1, e);
        }
        for (const auto& [w, kind] : kWords) {
            if (Starts(w)) return Make(kind, std::wcslen(w));
        }
        const bool title_next = Starts(L"title") && (At(5) == L' ' || At(5) == L'\t' || At(5) == L'\n' || At(5) == L'\r' || At(5) == L'\f' || At(5) == L'\v');
        if (Letter(c) && !title_next && !Starts(L"accTitle") && !Starts(L"accDescr")) {
            // Words, spaces between them; "-" not before ">".
            auto part = [&](size_t e) {
                while (e < s_.size()) {
                    const wchar_t k = s_[e];
                    if (Word(k) || k == L'(' || k == L')' || k == L'&') ++e;
                    else if (k == L'-' && (e + 1 >= s_.size() || s_[e + 1] != L'>')) ++e;
                    else break;
                }
                return e;
            };
            size_t e = part(p_ + 1);
            for (;;) {
                size_t k = e;
                while (k < s_.size() && (s_[k] == L' ' || s_[k] == L'\t')) ++k;
                if (k == e || k >= s_.size() || !(Letter(s_[k]) || s_[k] == L'(')) break;
                e = part(k + 1);
            }
            if (e - p_ > kMaxLabel) mermaid::Fail(p_, Tr(L"Text too long", L"Слишком длинный текст"));
            return Make(T::Name, e - p_);
        }
        if (Starts(L"accDescr")) {
            size_t k = p_ + 8;
            while (k < s_.size() && (s_[k] == L' ' || s_[k] == L'\t')) ++k;
            if (k < s_.size() && s_[k] == L':') {
                const size_t e = LineValueEnd(k + 1);
                return Value(T::AccDescr, e - p_, k + 1, e);
            }
            k = p_ + 8;
            while (k < s_.size() && std::iswspace(s_[k])) ++k;
            if (k < s_.size() && s_[k] == L'{') {
                const size_t e = s_.find(L'}', k);
                if (e != std::wstring_view::npos) return Value(T::AccDescr, e + 1 - p_, k + 1, e);
            }
        }
        if (Starts(L"accTitle")) {
            size_t k = p_ + 8;
            while (k < s_.size() && (s_[k] == L' ' || s_[k] == L'\t')) ++k;
            if (k < s_.size() && s_[k] == L':') {
                const size_t e = LineValueEnd(k + 1);
                return Value(T::AccTitle, e - p_, k + 1, e);
            }
        }
        if (Starts(L"title") && (At(5) == L' ' || At(5) == L'\t' || !Word(At(5)))) {
            if (At(5) == L' ' || At(5) == L'\t') {
                const size_t e = LineValueEnd(p_ + 6);
                return Value(T::Title, e - p_, p_ + 6, e);
            }
            return Value(T::Title, 5, p_ + 5, p_ + 5);
        }
        if (Digit(c)) {
            // 0|[1-9][0-9]*(?!\.)
            if (c == L'0') return Make(T::Int, 1);
            size_t e = p_;
            while (e < s_.size() && Digit(s_[e])) ++e;
            if (e < s_.size() && s_[e] == L'.') --e;
            if (e > p_) return Make(T::Int, e - p_);
        }
        if (c == L'"' || c == L'\'') {
            // Quoted, with \ escapes (lines and all).
            std::wstring v;
            size_t e = p_ + 1;
            while (e < s_.size() && s_[e] != c) {
                if (s_[e] == L'\\' && e + 1 < s_.size()) {
                    // (\. : not a line end.)
                    if (s_[e + 1] == L'\n' || s_[e + 1] == L'\r') mermaid::Fail(p_, Tr(L"Unclosed quote", L"Не закрыта кавычка"));
                    ++e;
                    const wchar_t k = s_[e];
                    v.push_back(k == L'n' ? L'\n' : k == L't' ? L'\t' : k == L'r' ? L'\r' : k == L'b' ? L'\b' : k == L'f' ? L'\f' : k == L'v' ? L'\v' : k == L'0' ? L'\0' : k);
                } else {
                    v.push_back(s_[e]);
                }
                ++e;
            }
            if (e >= s_.size()) mermaid::Fail(p_, Tr(L"Unclosed quote", L"Не закрыта кавычка"));
            if (v.size() > kMaxLabel) mermaid::Fail(p_, Tr(L"Text too long", L"Слишком длинный текст"));
            Token t{T::String, Text{std::move(v), static_cast<uint32_t>(p_ + 1), static_cast<uint32_t>(e - p_ - 1)}};
            p_ = e + 1;
            return t;
        }
        if (Word(c)) {
            size_t e = p_ + 1, last = p_ + 1;
            while (e < s_.size() && (Word(s_[e]) || s_[e] == L'-')) {
                ++e;
                if (Word(s_[e - 1])) last = e;
            }
            return Make(T::Id, last - p_);
        }
        if (c == L'\n') return Make(T::Newline, 1);
        if (c == L'\r' && At(1) == L'\n') return Make(T::Newline, 2);
        mermaid::Fail(p_, Tr(L"Unexpected character", L"Непонятный символ"));
    }

    std::wstring_view s_;
    size_t p_;
};

struct Node {
    Text label;
    std::wstring id;
    double x = 0, y = 0;  // evolution, visibility: 0 to 100
    enum Kind { Anchor, Component, PipelineComponent } kind = Component;
    std::optional<double> label_dx, label_dy;
    bool inertia = false;
    std::wstring strategy;  // build, buy, outsource, market
    bool pipeline_parent = false;
};

struct Link {
    std::wstring from, to;
    bool dashed = false;
    bool forward = false, backward = false;
    Text label;
};

struct Pipeline {
    std::wstring parent;
    std::vector<std::wstring> members;
};

struct Placed {
    double x = 0, y = 0;  // evolution, visibility: 0 to 100
    Text text;
};

class Parser {
public:
    explicit Parser(const Source& src) : src_(src) {}

    void Parse() {
        toks_ = Lexer(src_.text, src_.body).Run();
        while (Is(T::Newline)) ++i_;
        Expect(T::KwWardley, Tr(L"Expected \"wardley-beta\"", L"Ожидалось «wardley-beta»"));
        for (;;) {
            const Token& t = toks_[i_];
            switch (t.kind) {
            case T::Eof: Finish(); return;
            case T::Newline: ++i_; continue;
            case T::Title: title = t.text; ++i_; break;
            case T::AccTitle:
            case T::AccDescr: ++i_; break;
            case T::KwSize: {
                ++i_;
                Expect(T::Lbr, Tr(L"Expected \"[\"", L"Ожидалось «[»"));
                const double w = IntValue();
                Expect(T::Comma, Tr(L"Expected \",\"", L"Ожидалось «,»"));
                const double h = IntValue();
                Expect(T::Rbr, Tr(L"Expected \"]\"", L"Ожидалось «]»"));
                size = {w, h};
                break;
            }
            case T::KwEvolution: {
                ++i_;
                stages.clear();
                boundaries.clear();
                for (;;) {
                    Text name = NameValue();
                    if (Is(T::At)) {
                        ++i_;
                        boundaries.push_back(Number());
                    }
                    if (Is(T::Slash)) {
                        ++i_;
                        const Text second = NameValue();
                        name.raw = std::wstring(Trim(name.raw)) + L" / " + std::wstring(Trim(second.raw));
                    } else {
                        name.raw = std::wstring(Trim(name.raw));
                    }
                    stages.push_back(name);
                    if (!Is(T::Arrow)) break;
                    ++i_;
                }
                if (stages.size() < 2) Fail(toks_[i_].text.pos, Tr(L"Expected \"->\" and a stage", L"Ожидалось «->» и этап"));
                if (stages.size() > 50) TooLarge();
                break;
            }
            case T::KwAnchor: {
                ++i_;
                Node n;
                n.kind = Node::Anchor;
                n.label = NameValue();
                Coordinates(&n, L"Anchor");
                anchors.push_back(std::move(n));
                break;
            }
            case T::KwComponent: {
                ++i_;
                Node n;
                n.label = NameValue();
                Coordinates(&n, L"Component");
                if (Is(T::KwLabel)) Offsets(&n);
                if (Is(T::Lpar) && toks_[i_ + 1].kind == T::Strategy) {
                    n.strategy = toks_[i_ + 1].text.raw;
                    i_ += 2;
                    Expect(T::Rpar, Tr(L"Expected \")\"", L"Ожидалось «)»"));
                }
                if (Is(T::KwInertia)) {
                    n.inertia = true;
                    ++i_;
                } else if (Is(T::Lpar) && toks_[i_ + 1].kind == T::KwInertia) {
                    n.inertia = true;
                    i_ += 2;
                    Expect(T::Rpar, Tr(L"Expected \")\"", L"Ожидалось «)»"));
                }
                components.push_back(std::move(n));
                break;
            }
            case T::KwEvolve: {
                ++i_;
                const Text name = NameValue();
                const size_t at = toks_[i_].text.pos;
                evolves_.push_back({name.raw, Number(), at});
                break;
            }
            case T::KwPipeline: {
                ++i_;
                PipelineStatement();
                break;
            }
            case T::KwNote: {
                ++i_;
                if (!Is(T::String)) Fail(toks_[i_].text.pos, Tr(L"Expected a \"quoted\" text", L"Ожидался текст в кавычках"));
                Placed n;
                n.text = toks_[i_++].text;
                Position(&n, L"Note");
                notes.push_back(std::move(n));
                break;
            }
            case T::KwAnnotations: {
                ++i_;
                Placed box;
                Position(&box, L"Annotations box", true, !annotations_box);
                if (!annotations_box) annotations_box = box;
                break;
            }
            case T::KwAnnotation: {
                ++i_;
                const double number = IntValue();
                Expect(T::Comma, Tr(L"Expected \",\"", L"Ожидалось «,»"));
                Placed a;
                Position(&a, L"Annotation", true);
                if (!Is(T::String)) Fail(toks_[i_].text.pos, Tr(L"Expected a \"quoted\" text", L"Ожидался текст в кавычках"));
                a.text = toks_[i_++].text;
                annotations.push_back({number, std::move(a)});
                break;
            }
            case T::KwAccelerator:
            case T::KwDeaccelerator: {
                const bool accelerate = t.kind == T::KwAccelerator;
                ++i_;
                Placed a;
                a.text = NameValue();
                Position(&a, accelerate ? L"Accelerator" : L"Deaccelerator");
                (accelerate ? accelerators : deaccelerators).push_back(std::move(a));
                break;
            }
            case T::String:
            case T::Id:
            case T::Name: LinkStatement(); break;
            default: Fail(t.text.pos, Tr(L"Unexpected ", L"Непонятно: ") + Shown(t));
            }
            // The statement's end: a line end or the end.
            if (!Is(T::Newline) && !Is(T::Eof)) Fail(toks_[i_].text.pos, Tr(L"Unexpected ", L"Непонятно: ") + Shown(toks_[i_]));
            if (++statements_ > kMaxNodes) TooLarge();
        }
    }

    Text title;
    std::optional<std::pair<double, double>> size;
    std::vector<Text> stages;
    std::vector<double> boundaries;
    std::vector<Node> anchors, components;
    std::vector<Placed> notes, accelerators, deaccelerators;
    std::optional<Placed> annotations_box;
    std::vector<std::pair<double, Placed>> annotations;  // number, where and its text
    // Mermaid's data: the nodes (anchors, components, pipelines' parts; one
    // per id), links, pipelines, trends (node, target evolution).
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<Pipeline> pipelines;
    std::vector<std::pair<size_t, double>> trends;

    const Node* Find(const std::wstring& id) const {
        const auto it = index_.find(id);
        return it == index_.end() ? nullptr : &nodes[it->second];
    }
    int IndexOf(const std::wstring& id) const {
        const auto it = index_.find(id);
        return it == index_.end() ? -1 : static_cast<int>(it->second);
    }

private:
    struct RawLink {
        Text from, to;
        std::wstring from_port, to_port;
        Text arrow;
        Text label;
    };
    struct Evolve {
        std::wstring component;
        double target = 0;
        size_t at = 0;
    };
    struct RawPipeline {
        Text parent;
        size_t at = 0;
        std::vector<Node> members;
    };

    bool Is(T k) const { return toks_[i_].kind == k; }
    void Expect(T k, std::wstring message) {
        if (!Is(k)) Fail(toks_[i_].text.pos, std::move(message));
        ++i_;
    }
    static std::wstring Shown(const Token& t) {
        if (t.kind == T::Newline) return Tr(L"line end", L"конец строки");
        if (t.kind == T::Eof) return Tr(L"end of the diagram", L"конец диаграммы");
        return L"\"" + t.text.raw.substr(0, 40) + L"\"";
    }
    Text NameValue() {
        if (!Is(T::String) && !Is(T::Id) && !Is(T::Name)) Fail(toks_[i_].text.pos, Tr(L"Expected a name", L"Ожидалось имя"));
        return toks_[i_++].text;
    }
    double Number() {
        if (!Is(T::Number)) Fail(toks_[i_].text.pos, Tr(L"Expected a number like 0.5", L"Ожидалось число вида 0.5"));
        return std::wcstod(toks_[i_++].text.raw.c_str(), nullptr);
    }
    double IntValue() {
        if (!Is(T::Int)) Fail(toks_[i_].text.pos, Tr(L"Expected a whole number", L"Ожидалось целое число"));
        return std::wcstod(toks_[i_++].text.raw.c_str(), nullptr);
    }
    // Mermaid's toPercent: 0 to 1, or 0 to 100.
    static double Percent(double v, size_t at, const std::wstring& what) {
        const double p = v <= 1 ? v * 100 : v;
        if (p < 0 || p > 100) Fail(at, what + Tr(L" must be between 0-1 or 0-100", L": допустимо от 0 до 1 или от 0 до 100"));
        return p;
    }
    // [visibility, evolution]
    void Coordinates(Node* n, const wchar_t* what) {
        Expect(T::Lbr, Tr(L"Expected \"[\"", L"Ожидалось «[»"));
        const size_t at_v = toks_[i_].text.pos;
        const double v = Number();
        Expect(T::Comma, Tr(L"Expected \",\"", L"Ожидалось «,»"));
        const size_t at_e = toks_[i_].text.pos;
        const double e = Number();
        Expect(T::Rbr, Tr(L"Expected \"]\"", L"Ожидалось «]»"));
        const std::wstring name = std::wstring(what) + L" \"" + n->label.raw + L"\"";
        n->x = Percent(e, at_e, name + L" evolution");
        n->y = Percent(v, at_v, name + L" visibility");
    }
    // [visibility, evolution] of a note, annotation, accelerator (whole
    // numbers too for annotations).
    void Position(Placed* p, const wchar_t* what, bool ints = false, bool check = true) {
        Expect(T::Lbr, Tr(L"Expected \"[\"", L"Ожидалось «[»"));
        auto value = [&] { return ints && Is(T::Int) ? IntValue() : Number(); };
        const size_t at_v = toks_[i_].text.pos;
        const double v = value();
        Expect(T::Comma, Tr(L"Expected \",\"", L"Ожидалось «,»"));
        const size_t at_e = toks_[i_].text.pos;
        const double e = value();
        Expect(T::Rbr, Tr(L"Expected \"]\"", L"Ожидалось «]»"));
        if (!check) return;
        p->x = Percent(e, at_e, std::wstring(what) + L" evolution");
        p->y = Percent(v, at_v, std::wstring(what) + L" visibility");
    }
    // label [x, y]
    void Offsets(Node* n) {
        ++i_;
        Expect(T::Lbr, Tr(L"Expected \"[\"", L"Ожидалось «[»"));
        const bool neg_x = Is(T::Minus);
        if (neg_x) ++i_;
        const double x = IntValue();
        Expect(T::Comma, Tr(L"Expected \",\"", L"Ожидалось «,»"));
        const bool neg_y = Is(T::Minus);
        if (neg_y) ++i_;
        const double y = IntValue();
        Expect(T::Rbr, Tr(L"Expected \"]\"", L"Ожидалось «]»"));
        n->label_dx = neg_x ? -x : x;
        n->label_dy = neg_y ? -y : y;
    }

    void PipelineStatement() {
        RawPipeline p;
        p.at = toks_[i_].text.pos;
        p.parent = NameValue();
        Expect(T::Lbrace, Tr(L"Expected \"{\"", L"Ожидалось «{»"));
        if (!Is(T::Newline)) Fail(toks_[i_].text.pos, Tr(L"Expected a line end", L"Ожидался конец строки"));
        while (Is(T::Newline)) ++i_;
        do {
            if (!Is(T::KwComponent)) Fail(toks_[i_].text.pos, Tr(L"Expected \"component\"", L"Ожидалось «component»"));
            ++i_;
            Node n;
            n.kind = Node::PipelineComponent;
            n.label = NameValue();
            Expect(T::Lbr, Tr(L"Expected \"[\"", L"Ожидалось «[»"));
            const size_t at = toks_[i_].text.pos;
            n.x = Percent(Number(), at, L"Pipeline component \"" + n.label.raw + L"\" evolution");
            Expect(T::Rbr, Tr(L"Expected \"]\"", L"Ожидалось «]»"));
            if (Is(T::KwLabel)) Offsets(&n);
            if (!Is(T::Newline) && !Is(T::Eof)) Fail(toks_[i_].text.pos, Tr(L"Unexpected ", L"Непонятно: ") + Shown(toks_[i_]));
            while (Is(T::Newline)) ++i_;
            p.members.push_back(std::move(n));
            if (p.members.size() > kMaxNodes) TooLarge();
        } while (!Is(T::Rbrace));
        ++i_;
        raw_pipelines_.push_back(std::move(p));
    }

    void LinkStatement() {
        RawLink l;
        l.from = NameValue();
        if (Is(T::Port)) l.from_port = toks_[i_++].text.raw;
        if (Is(T::LinkArrow) || Is(T::Arrow)) l.arrow = toks_[i_++].text;
        l.to = NameValue();
        if (Is(T::Port)) l.to_port = toks_[i_++].text.raw;
        if (Is(T::LinkLabel)) l.label = toks_[i_++].text;
        if (raw_links_.size() >= kMaxEdges * 4) Fail(l.from.pos, Tr(L"Too many links", L"Слишком много связей"));
        raw_links_.push_back(std::move(l));
    }

    // Mermaid's populateDb and WardleyBuilder.
    void AddNode(Node n) {
        const auto it = index_.find(n.id);
        if (it == index_.end()) {
            if (nodes.size() >= kMaxNodes) TooLarge();
            index_[n.id] = nodes.size();
            nodes.push_back(std::move(n));
            return;
        }
        Node& old = nodes[it->second];
        if (!n.label_dx) n.label_dx = old.label_dx;
        if (!n.label_dy) n.label_dy = old.label_dy;
        n.pipeline_parent = old.pipeline_parent;
        old = std::move(n);
    }

    std::wstring Resolve(const std::wstring& name) const {
        if (index_.contains(name)) return name;
        for (const Node& n : nodes) {
            if (n.label.raw == name) return n.id;
        }
        return name;
    }

    void Finish() {
        for (Node& n : anchors) {
            n.id = n.label.raw;
            AddNode(n);
        }
        for (Node& n : components) {
            n.id = n.label.raw;
            AddNode(n);
        }
        for (RawPipeline& rp : raw_pipelines_) {
            const auto it = index_.find(rp.parent.raw);
            if (it == index_.end()) {
                Fail(rp.at, Tr(L"A pipeline needs an existing component: ", L"Конвейеру нужен существующий компонент: ") + rp.parent.raw);
            }
            const double y = nodes[it->second].y;
            nodes[it->second].pipeline_parent = true;
            Pipeline p{rp.parent.raw, {}};
            for (Node& m : rp.members) {
                m.id = rp.parent.raw + L"_" + m.label.raw;
                m.y = y;
                AddNode(m);
                p.members.push_back(m.id);
            }
            // (A pipeline declared again starts over, in its place.)
            const auto old = std::find_if(pipelines.begin(), pipelines.end(), [&](const Pipeline& q) { return q.parent == p.parent; });
            if (old != pipelines.end()) *old = std::move(p);
            else pipelines.push_back(std::move(p));
        }
        for (const RawLink& r : raw_links_) {
            Link l;
            const std::wstring& arrow = r.arrow.raw;
            l.dashed = arrow.find(L"-.->") != std::wstring::npos || arrow.find(L".-.") != std::wstring::npos;
            // The flow: from the ports, else the arrow; +'label'> labels it.
            auto port_flow = [](const std::wstring& port) { return port == L"+<>" ? L"both" : port == L"+<" ? L"back" : port == L"+>" ? L"forward" : L""; };
            std::wstring flow = port_flow(r.from_port);
            if (flow.empty()) flow = port_flow(r.to_port);
            std::optional<Text> flow_label;
            if (arrow.starts_with(L"+")) {
                if (arrow.size() > 1 && arrow[1] == L'\'') {
                    const size_t q = arrow.find(L'\'', 2);
                    if (q != std::wstring::npos) flow_label = Text{arrow.substr(2, q - 2), r.arrow.pos + 2, static_cast<uint32_t>(q - 2)};
                }
                if (flow.empty()) flow = arrow.find(L"<>") != std::wstring::npos ? L"both" : arrow.find(L'<') != std::wstring::npos ? L"back" : arrow.find(L'>') != std::wstring::npos ? L"forward" : L"";
            }
            l.label = flow_label ? *flow_label : r.label;
            l.forward = flow == L"forward" || flow == L"both";
            l.backward = flow == L"back" || flow == L"both";
            l.from = Resolve(r.from.raw);
            l.to = Resolve(r.to.raw);
            links.push_back(std::move(l));
        }
        for (const Evolve& e : evolves_) {
            const auto it = index_.find(e.component);
            if (it == index_.end()) continue;
            const double target = Percent(e.target, e.at, L"Evolve target for \"" + e.component + L"\"");
            const auto old = std::find_if(trends.begin(), trends.end(), [&](const auto& t) { return t.first == it->second; });
            if (old != trends.end()) old->second = target;
            else trends.push_back({it->second, target});
        }
    }

    const Source& src_;
    std::vector<Token> toks_;
    size_t i_ = 0;
    size_t statements_ = 0;
    std::vector<RawPipeline> raw_pipelines_;
    std::vector<RawLink> raw_links_;
    std::vector<Evolve> evolves_;
    std::map<std::wstring, size_t> index_;
};

using M = SceneBuilder::Measured;

constexpr float kPad = 48, kRadius = 6, kLabelOffset = 8, kAxisFont = 12, kLabelFont = 10;
constexpr float kSquare = kRadius * 1.6f;

// (In the dark theme Mermaid keeps white fills, black anchor names and
// black stage lines on its grey panel; here they follow the theme, readable
// on the page.)
class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b) : p_(p), b_(b) {}

    std::shared_ptr<DiagramScene> Build() {
        w_ = 900;
        h_ = 600;
        if (p_.size) {
            w_ = static_cast<float>(std::clamp(p_.size->first, 1.0, 10000.0));
            h_ = static_cast<float>(std::clamp(p_.size->second, 1.0, 10000.0));
        }
        cw_ = w_ - kPad * 2;
        ch_ = h_ - kPad * 2;
        const DgColor line = Role(kDgLine), text = Role(kDgText), fill = Role(kDgWardley), evolution = Role(static_cast<uint8_t>(kDgWardley + 1));
        if (!p_.title.raw.empty()) Label(p_.title, kAxisFont * 1.05f, true, w_ / 2, kPad / 2, Anchor::Middle, Base::Middle, text);
        Line(kPad, h_ - kPad, w_ - kPad, h_ - kPad, line, 1);
        Line(kPad, kPad, kPad, h_ - kPad, line, 1);
        Label(Text{L"Evolution"}, kAxisFont, true, kPad + cw_ / 2, h_ - kPad / 4, Anchor::Middle, Base::Alphabetic, text);
        {
            // Read upwards, centred on the axis.
            RichText t;
            t.text = L"Visibility";
            const M m = b_.Measure(t, 0, true, kAxisFont / b_.size());
            b_.TextUp(m, D2D1::Point2F(kPad / 3 - 1.17f * kAxisFont, kPad + ch_ / 2 - m.w / 2), text);
        }
        Stages();
        // Where the nodes are (pipelines move their parents above them).
        pos_.clear();
        for (const Node& n : p_.nodes) pos_.push_back(D2D1::Point2F(X(n.x), Y(n.y)));
        Pipelines();
        Links();
        for (const auto& [node, target] : p_.trends) {
            const D2D1_POINT_2F o = pos_[node];
            const float tx = X(target), ty = Y(p_.nodes[node].y), dx = tx - o.x, dy = ty - o.y, d = std::sqrt(dx * dx + dy * dy);
            constexpr float shorten = kRadius + 2;
            const D2D1_POINT_2F end = d > shorten ? D2D1::Point2F(tx - dx / d * shorten, ty - dy / d * shorten) : D2D1::Point2F(tx, ty);
            Line(o.x, o.y, end.x, end.y, evolution, 1, 1);
            Head(end, o, 0.6f, evolution);
        }
        for (size_t k = 0; k < p_.nodes.size(); ++k) DrawNode(p_.nodes[k], pos_[k], line, fill, text);
        Annotations(line, fill, text);
        for (const Placed& n : p_.notes) Label(n.text, 11, true, X(n.x), Y(n.y), Anchor::Start, Base::Alphabetic, text);
        for (const Placed& a : p_.accelerators) Accelerator(a, true, line, fill, text);
        for (const Placed& a : p_.deaccelerators) Accelerator(a, false, line, fill, text);
        return b_.Finish(w_, h_);
    }

private:
    enum class Anchor { Start, Middle };
    enum class Base { Alphabetic, Middle, Central };

    float X(double v) const { return kPad + static_cast<float>(v) / 100 * cw_; }
    float Y(double v) const { return h_ - kPad - static_cast<float>(v) / 100 * ch_; }

    void Line(float x1, float y1, float x2, float y2, DgColor color, float width, uint8_t dash = 0) {
        b_.Begin(D2D1::Point2F(x1, y1));
        b_.LineTo(D2D1::Point2F(x2, y2));
        b_.End(Role(kDgNone), color, width, dash);
    }

    // SVG text at (x, y): its anchor and baseline as Mermaid sets them.
    M Label(const Text& t, float px, bool bold, float x, float y, Anchor anchor, Base base, DgColor color, float angle = 0) {
        RichText r;
        r.text = t.raw;
        const M m = b_.Measure(r, 0, bold, px / b_.size());
        const float baseline_shift = base == Base::Alphabetic ? 0.f : base == Base::Middle ? 0.3f * px : 0.34f * px;
        const float dx = anchor == Anchor::Middle ? -m.w / 2 : 0.f, dy = baseline_shift - 1.17f * px;
        if (angle == 0) {
            b_.Text(m, D2D1::Point2F(x + dx, y + dy), color, t.pos, t.len);
        } else {
            const float a = angle * 3.14159265f / 180, c = std::cos(a), s = std::sin(a);
            b_.TextTurned(m, D2D1::Point2F(x + dx * c - dy * s, y + dx * s + dy * c), angle, color, t.pos, t.len);
        }
        return m;
    }

    // An arrowhead (Mermaid's 10 x 10 marker at `scale`) at the line's end
    // `tip`, pointing away from `from`.
    void Head(D2D1_POINT_2F end, D2D1_POINT_2F from, float scale, DgColor color) {
        const float dx = end.x - from.x, dy = end.y - from.y, d = std::sqrt(dx * dx + dy * dy);
        if (d <= 0) return;
        const float ux = dx / d, uy = dy / d;
        auto at = [&](float along, float across) { return D2D1::Point2F(end.x + (ux * along - uy * across) * scale, end.y + (uy * along + ux * across) * scale); };
        b_.Polygon({at(1, 0), at(-9, -5), at(-9, 5)}, color, Role(kDgNone));
    }

    void Stages() {
        static const wchar_t* kDefault[] = {L"Genesis", L"Custom Built", L"Product", L"Commodity"};
        std::vector<Text> stages = p_.stages;
        if (stages.empty()) {
            for (const wchar_t* s : kDefault) stages.push_back(Text{s});
        }
        const size_t n = stages.size();
        const bool bounded = p_.boundaries.size() == n;
        double prev = 0;
        for (size_t i = 0; i < n; ++i) {
            double start, end;
            if (bounded) {
                start = prev;
                end = p_.boundaries[i];
                prev = end;
            } else {
                start = static_cast<double>(i) / static_cast<double>(n);
                end = static_cast<double>(i + 1) / static_cast<double>(n);
            }
            const float sx = kPad + static_cast<float>(std::clamp(start, -100.0, 100.0)) * cw_, ex = kPad + static_cast<float>(std::clamp(end, -100.0, 100.0)) * cw_;
            if (i > 0) Line(sx, kPad, sx, h_ - kPad, RoleAt(kDgLine, 204), 1, 1);
            Label(stages[i], kAxisFont - 2, false, (sx + ex) / 2, h_ - kPad / 1.5f, Anchor::Middle, Base::Alphabetic, Role(kDgText));
        }
    }

    void Pipelines() {
        for (const Pipeline& pl : p_.pipelines) {
            if (pl.members.empty()) continue;
            std::vector<int> members;
            for (const std::wstring& id : pl.members) {
                const int k = p_.IndexOf(id);
                if (k >= 0) members.push_back(k);
            }
            std::vector<int> sorted = members;
            std::stable_sort(sorted.begin(), sorted.end(), [&](int a, int b) { return p_.nodes[static_cast<size_t>(a)].x < p_.nodes[static_cast<size_t>(b)].x; });
            for (size_t i = 0; i + 1 < sorted.size(); ++i) {
                const D2D1_POINT_2F a = pos_[static_cast<size_t>(sorted[i])], b = pos_[static_cast<size_t>(sorted[i + 1])];
                Line(a.x, a.y, b.x, b.y, Role(kDgLine), 1, 1);
            }
            if (members.empty()) continue;
            float lo = INFINITY, hi = -INFINITY, y = 0;
            for (const int k : members) {
                lo = std::min(lo, pos_[static_cast<size_t>(k)].x);
                hi = std::max(hi, pos_[static_cast<size_t>(k)].x);
                y = pos_[static_cast<size_t>(k)].y;
            }
            constexpr float pad = 15, height = kRadius * 4;
            const float top = y - height / 2;
            if (const int parent = p_.IndexOf(pl.parent); parent >= 0) pos_[static_cast<size_t>(parent)] = D2D1::Point2F((lo + hi) / 2, top - kSquare / 6);
            b_.Rect(D2D1::RectF(lo - pad, top, hi + pad, top + height), 4, Role(kDgNone), Role(kDgLine), 1.5f);
        }
    }

    void Links() {
        std::vector<const Link*> valid;
        for (const Link& l : p_.links) {
            const int s = p_.IndexOf(l.from), t = p_.IndexOf(l.to);
            if (s < 0 || t < 0) continue;
            // (A pipeline's part to its parent is left out.)
            const auto pl = std::find_if(p_.pipelines.begin(), p_.pipelines.end(), [&](const Pipeline& q) { return q.parent == l.to; });
            if (pl != p_.pipelines.end() && std::find(pl->members.begin(), pl->members.end(), l.from) != pl->members.end()) continue;
            valid.push_back(&l);
        }
        const DgColor line = Role(kDgLine);
        for (const Link* l : valid) {
            const size_t s = static_cast<size_t>(p_.IndexOf(l->from)), t = static_cast<size_t>(p_.IndexOf(l->to));
            const D2D1_POINT_2F a = pos_[s], b = pos_[t];
            const float dx = b.x - a.x, dy = b.y - a.y, d = std::sqrt(dx * dx + dy * dy);
            if (!(d > 0)) continue;
            const float ra = p_.nodes[s].pipeline_parent ? kSquare / std::sqrt(2.f) : kRadius, rb = p_.nodes[t].pipeline_parent ? kSquare / std::sqrt(2.f) : kRadius;
            const D2D1_POINT_2F p1 = D2D1::Point2F(a.x + dx / d * ra, a.y + dy / d * ra), p2 = D2D1::Point2F(b.x - dx / d * rb, b.y - dy / d * rb);
            Line(p1.x, p1.y, p2.x, p2.y, line, 1, l->dashed ? 1 : 0);
            if (l->forward) Head(p2, p1, 0.5f, line);
            if (l->backward) Head(p1, p2, 0.5f, line);
        }
        for (const Link* l : valid) {
            if (l->label.raw.empty()) continue;
            const D2D1_POINT_2F a = pos_[static_cast<size_t>(p_.IndexOf(l->from))], b = pos_[static_cast<size_t>(p_.IndexOf(l->to))];
            const float dx = b.x - a.x, dy = b.y - a.y, d = std::sqrt(dx * dx + dy * dy);
            if (!(d > 0)) continue;
            const float lx = (a.x + b.x) / 2 + dy / d * 8, ly = (a.y + b.y) / 2 - dx / d * 8;
            float angle = std::atan2(dy, dx) * 180 / 3.14159265f;
            if (angle > 90 || angle < -90) angle += 180;
            Label(l->label, kLabelFont, false, lx, ly, Anchor::Middle, Base::Middle, Role(kDgText), angle);
        }
    }

    void DrawNode(const Node& n, D2D1_POINT_2F c, DgColor line, DgColor fill, DgColor text) {
        auto circle = [&](float x, float y, float r, DgColor f, DgColor s, float w) { b_.Ellipse(D2D1::RectF(x - r, y - r, x + r, y + r), f, s, w); };
        const std::wstring& st = n.strategy;
        if (st == L"outsource") circle(c.x, c.y, kRadius * 2, Argb(0xFF666666), line, 1);
        else if (st == L"buy") circle(c.x, c.y, kRadius * 2, Argb(0xFFCCCCCC), line, 1);
        else if (st == L"build") circle(c.x, c.y, kRadius * 2, Argb(0xFFEEEEEE), line, 1);
        else if (st == L"market") circle(c.x, c.y, kRadius * 2, fill, line, 1);
        if (!n.pipeline_parent && st != L"market" && n.kind != Node::Anchor) circle(c.x, c.y, kRadius, fill, line, 1);
        if (st == L"market") {
            const float tr = kRadius * 1.2f, cx = tr * std::cos(3.14159265f / 6), sy = tr * std::sin(3.14159265f / 6);
            const D2D1_POINT_2F top = D2D1::Point2F(c.x, c.y - tr), left = D2D1::Point2F(c.x - cx, c.y + sy), right = D2D1::Point2F(c.x + cx, c.y + sy);
            b_.Polygon({top, left, right}, Role(kDgNone), line, 1);
            for (const D2D1_POINT_2F& p : {top, left, right}) circle(p.x, p.y, kRadius * 0.7f, fill, line, 2);
        }
        if (n.pipeline_parent) b_.Rect(D2D1::RectF(c.x - kSquare / 2, c.y - kSquare / 2, c.x + kSquare / 2, c.y + kSquare / 2), 0, fill, line, 1);
        if (n.inertia) {
            float offset = n.pipeline_parent ? kSquare / 2 + 15 : kRadius + 15;
            if (!st.empty()) offset += kRadius + 10;
            const float half = (n.pipeline_parent ? kSquare : kRadius * 2) / 2;
            Line(c.x + offset, c.y - half, c.x + offset, c.y + half, line, 6);
        }
        if (n.kind == Node::Anchor) {
            Label(n.label, kLabelFont, true, c.x + static_cast<float>(n.label_dx.value_or(0)), c.y + static_cast<float>(n.label_dy.value_or(-3)), Anchor::Middle,
                  Base::Middle, text);
            return;
        }
        const float extra = st.empty() ? 0.f : 10.f;
        const float lx = n.label_dx ? static_cast<float>(*n.label_dx) : kLabelOffset + extra, ly = n.label_dy ? static_cast<float>(*n.label_dy) : -kLabelOffset - extra;
        Label(n.label, kLabelFont, false, c.x + lx, c.y + ly, Anchor::Start, Base::Alphabetic, text);
    }

    void Annotations(DgColor line, DgColor fill, DgColor text) {
        for (const auto& [number, a] : p_.annotations) {
            const float x = X(a.x), y = Y(a.y);
            b_.Ellipse(D2D1::RectF(x - 10, y - 10, x + 10, y + 10), fill, line, 1.5f);
            Label(Text{JsNumber(number)}, 10, true, x, y, Anchor::Middle, Base::Central, text);
        }
        if (!p_.annotations_box) return;
        std::vector<const std::pair<double, Placed>*> listed;
        for (const auto& a : p_.annotations) {
            if (!a.second.text.raw.empty()) listed.push_back(&a);
        }
        std::stable_sort(listed.begin(), listed.end(), [](const auto* a, const auto* b) { return a->first < b->first; });
        if (listed.empty()) return;
        constexpr float pad = 10, line_h = 16, font = 11;
        float widest = 0;
        std::vector<M> measured;
        for (const auto* a : listed) {
            RichText r;
            r.text = JsNumber(a->first) + L". " + a->second.text.raw;
            measured.push_back(b_.Measure(r, 0, false, font / b_.size()));
            widest = std::max(widest, measured.back().w);
        }
        const float box_w = widest + pad * 2 + 105, box_h = static_cast<float>(listed.size()) * line_h + pad * 2 + font * 1.13f / 2;
        const float x = std::max(kPad, std::min(X(p_.annotations_box->x), w_ - kPad - box_w));
        const float y = std::max(kPad, std::min(Y(p_.annotations_box->y), h_ - kPad - box_h));
        b_.Rect(D2D1::RectF(x, y, x + box_w, y + box_h), 4, fill, line, 1.5f);
        for (size_t i = 0; i < listed.size(); ++i) {
            const float baseline = y + pad + static_cast<float>(i + 1) * line_h + 0.3f * font;
            // (The text's source: from its number on.)
            b_.Text(measured[i], D2D1::Point2F(x + pad, baseline - 1.17f * font), text, listed[i]->second.text.pos, listed[i]->second.text.len);
        }
    }

    void Accelerator(const Placed& a, bool forward, DgColor line, DgColor fill, DgColor text) {
        const float x = X(a.x), y = Y(a.y);
        constexpr float w = 60, h = 30, head = 20;
        std::vector<D2D1_POINT_2F> pts;
        if (forward) {
            pts = {{x, y - h / 2}, {x + w - head, y - h / 2}, {x + w - head, y - h / 2 - 8}, {x + w, y}, {x + w - head, y + h / 2 + 8}, {x + w - head, y + h / 2}, {x, y + h / 2}};
        } else {
            pts = {{x + w, y - h / 2}, {x + head, y - h / 2}, {x + head, y - h / 2 - 8}, {x, y}, {x + head, y + h / 2 + 8}, {x + head, y + h / 2}, {x + w, y + h / 2}};
        }
        b_.Polygon(pts, fill, line, 1);
        Label(a.text, 10, true, x + w / 2, y + h / 2 + 15, Anchor::Middle, Base::Alphabetic, text);
    }

    const Parser& p_;
    SceneBuilder& b_;
    float w_ = 900, h_ = 600, cw_ = 0, ch_ = 0;
    std::vector<D2D1_POINT_2F> pos_;
};

}  // namespace

std::shared_ptr<DiagramScene> WardleyMap(const Source& src, SceneBuilder* builder) {
    Parser p(src);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder).Build();
}

}  // namespace mdv::mermaid
