// Mermaid railroad diagrams: grammar rules as syntax diagrams, written as
// calls (railroad-beta), EBNF (railroad-ebnf-beta), ABNF (railroad-abnf-beta)
// or PEG (railroad-peg-beta). Each is read with Mermaid's grammar and its
// lexer's rules, turned into the same tree as Mermaid turns it, and drawn
// as its railroad renderer draws it: terminals rounded, nonterminals square,
// specials dashed, choices stacked, optional parts bypassed, repetitions
// looped back.
#include <algorithm>
#include <cmath>
#include <cwctype>

#include "core/mermaid_internal.h"

namespace mdv::mermaid {
namespace {

struct Text {
    std::wstring raw;
    uint32_t pos = 0, len = 0;
};

// Mermaid's railroad tree.
struct Node {
    enum Kind { Terminal, NonTerminal, Special, Sequence, Choice, Optional, Repetition } kind = Terminal;
    Text text;
    std::vector<Node> kids;
    bool bypass = false;  // a repetition of zero or more
    // Laid out: its size, how far its line is from its top.
    mutable double w = 0, h = 0, up = 0;
};

struct Rule {
    Text name;
    Node def;
};

enum class Dialect { Calls, Ebnf, Abnf, Peg };

enum class T { Keyword, Id, String, Special, NumVal, Repeat, Exact, Title, Acc, Eof };

struct Token {
    T kind = T::Eof;
    std::wstring word;  // a keyword's text
    Text text;          // the value
};

bool Letter(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'); }
bool Digit(wchar_t c) { return c >= L'0' && c <= L'9'; }
bool WordChar(wchar_t c) { return Letter(c) || Digit(c) || c == L'_'; }
bool Hex(wchar_t c) { return Digit(c) || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'); }

constexpr size_t kMaxItems = 5000;  // parts of all the rules
constexpr int kMaxDepth = 40;
constexpr int kMaxHeight = 3 * kMaxDepth;  // levels of a rule's tree (a group: a choice, a sequence, a repeat)

// The lexer of each dialect: keywords first (longest first, a longer match
// of another token wins), then its terminals in order.
class Lexer {
public:
    Lexer(std::wstring_view s, size_t pos, Dialect d) : s_(s), p_(pos), d_(d) {}

    std::vector<Token> Run() {
        std::vector<Token> out;
        for (;;) {
            Token t = Next();
            if (out.size() > 100000) TooLarge();
            const bool end = t.kind == T::Eof;
            out.push_back(std::move(t));
            if (end) return out;
        }
    }

private:
    wchar_t At(size_t k) const { return p_ + k < s_.size() ? s_[p_ + k] : L'\0'; }
    bool Starts(std::wstring_view w) const { return s_.substr(p_).starts_with(w); }

    Token Make(T kind, size_t len, size_t from, size_t to) {
        if (to - from > kMaxLabel) mermaid::Fail(from, Tr(L"Text too long", L"Слишком длинный текст"));
        Token t{kind, {}, Text{std::wstring(s_.substr(from, to - from)), static_cast<uint32_t>(from), static_cast<uint32_t>(to - from)}};
        p_ += len;
        return t;
    }
    Token Key(std::wstring_view w) {
        Token t{T::Keyword, std::wstring(w), Text{std::wstring(w), static_cast<uint32_t>(p_), static_cast<uint32_t>(w.size())}};
        p_ += w.size();
        return t;
    }

    // Identifiers: [A-Z_a-z][\w-]* (ABNF: [A-Za-z][A-Za-z0-9-]*).
    size_t IdLength(size_t at) const {
        if (at >= s_.size()) return 0;
        const wchar_t c = s_[at];
        if (d_ == Dialect::Abnf ? !Letter(c) : !(Letter(c) || c == L'_')) return 0;
        size_t e = at + 1;
        while (e < s_.size() && ((d_ == Dialect::Abnf ? (Letter(s_[e]) || Digit(s_[e])) : WordChar(s_[e])) || s_[e] == L'-')) ++e;
        return e - at;
    }
    // "..." or '...' with \ escapes (ABNF: "..." only, none).
    size_t StringLength(size_t at) const {
        if (at >= s_.size()) return 0;
        const wchar_t q = s_[at];
        if (q != L'"' && (q != L'\'' || d_ == Dialect::Abnf)) return 0;
        size_t e = at + 1;
        while (e < s_.size() && s_[e] != q) {
            if (s_[e] == L'\\' && d_ != Dialect::Abnf) {
                // (\. : not a line end.)
                if (e + 1 >= s_.size() || s_[e + 1] == L'\n' || s_[e + 1] == L'\r') return 0;
                ++e;
            }
            ++e;
        }
        return e < s_.size() ? e + 1 - at : 0;
    }
    // EBNF's ? special sequence ?: no ? or ; inside, not all blank.
    size_t SpecialLength(size_t at) const {
        if (at >= s_.size() || s_[at] != L'?') return 0;
        size_t e = at + 1;
        bool text = false;
        while (e < s_.size() && s_[e] != L'?' && s_[e] != L';') {
            if (!std::iswspace(s_[e])) text = true;
            ++e;
        }
        return text && e < s_.size() && s_[e] == L'?' ? e + 1 - at : 0;
    }
    // Hidden: blanks, %% comments, /* */ (calls, EBNF), (* *) (EBNF), # (PEG), ; (ABNF).
    bool Skip() {
        if (const size_t n = YamlBlockLength(s_, p_, &yaml_none_)) {
            p_ += n;
            return true;
        }
        const wchar_t c = s_[p_];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
            ++p_;
            return true;
        }
        if (c == L'%' && At(1) == L'%') {
            if (const size_t n = DirectiveLength(s_, p_, &directive_none_)) {
                p_ += n;
                return true;
            }
            while (p_ < s_.size() && s_[p_] != L'\n' && s_[p_] != L'\r') ++p_;
            return true;
        }
        // Comments are skipped here, in Next()'s loop: no token or keyword
        // starts like one ("(*" is never a group, "; x" never ends an ABNF rule).
        size_t n = 0;
        if (d_ == Dialect::Ebnf) n = BlockComment(L"(*", L"*)");
        if (!n && d_ != Dialect::Abnf && d_ != Dialect::Peg) n = BlockComment(L"/*", L"*/");
        if (n) {
            p_ += n;
            return true;
        }
        if ((d_ == Dialect::Peg && c == L'#') ||
            (d_ == Dialect::Abnf && c == L';' && At(1) != L'\n' && At(1) != L'\r' && At(1) != L'\0')) {
            while (p_ < s_.size() && s_[p_] != L'\n' && s_[p_] != L'\r') ++p_;
            return true;
        }
        return false;
    }
    size_t BlockComment(std::wstring_view open, std::wstring_view close) const {
        if (!Starts(open)) return 0;
        size_t& none = no_close_[close == L"*)" ? 0 : 1];  // (none after here: found out once)
        if (p_ >= none) return 0;
        const size_t e = s_.find(close, p_ + open.size());
        if (e == std::wstring_view::npos) {
            none = p_;
            return 0;
        }
        return e + close.size() - p_;
    }

    Token Next() {
        while (p_ < s_.size() && Skip()) {
        }
        if (p_ >= s_.size()) return Token{T::Eof, {}, Text{L"", static_cast<uint32_t>(p_), 0}};
        // Keywords, longest first.
        static const std::vector<std::wstring_view> kCalls = {L"railroad-beta", L"zeroOrMore", L"nonterminal", L"oneOrMore", L"sequence", L"optional",
                                                              L"terminal", L"special", L"choice", L"=", L";", L"(", L",", L")"};
        static const std::vector<std::wstring_view> kEbnf = {L"railroad-ebnf-beta", L"::=", L"=", L";", L"|", L",", L"(", L")", L"[", L"]", L"{", L"}", L"?", L"*", L"+", L"-"};
        static const std::vector<std::wstring_view> kAbnf = {L"railroad-abnf-beta", L"=", L";", L"/", L"(", L")", L"[", L"]"};
        static const std::vector<std::wstring_view> kPeg = {L"railroad-peg-beta", L"<-", L";", L"/", L"&", L"!", L"?", L"*", L"+", L"(", L")", L"."};
        const std::vector<std::wstring_view>& words = d_ == Dialect::Calls ? kCalls : d_ == Dialect::Ebnf ? kEbnf : d_ == Dialect::Abnf ? kAbnf : kPeg;
        for (const std::wstring_view w : words) {
            if (!Starts(w)) continue;
            if (w.starts_with(L"railroad-")) {
                // (Mermaid's: then a blank, %% or the end.)
                const wchar_t n = At(w.size());
                if (n != L'\0' && !std::iswspace(n) && !(n == L'%' && At(w.size() + 1) == L'%')) continue;
            }
            // A longer identifier, comment or special sequence wins.
            if (Letter(w[0]) && IdLength(p_) > w.size()) break;
            if (w == L"?" && d_ == Dialect::Ebnf) {
                if (const size_t n = SpecialLength(p_)) return SpecialToken(n);
            }
            return Key(w);
        }
        // Titles and accessibility (not drawn, as in Mermaid).
        if (Starts(L"title")) {
            size_t e = p_ + 5;
            if (At(5) == L' ' || At(5) == L'\t') {
                while (e < s_.size() && s_[e] != L'\n' && s_[e] != L'\r') ++e;
                const size_t comment = s_.substr(p_, e - p_).find(L"%%");
                if (comment != std::wstring_view::npos) e = p_ + comment;
            }
            return Make(T::Title, e - p_, p_ + 5, e);
        }
        if (Starts(L"accTitle") || Starts(L"accDescr")) {
            size_t k = p_ + 8;
            while (k < s_.size() && (s_[k] == L' ' || s_[k] == L'\t')) ++k;
            if (k < s_.size() && s_[k] == L':') {
                size_t e = k + 1;
                while (e < s_.size() && s_[e] != L'\n' && s_[e] != L'\r') ++e;
                return Make(T::Acc, e - p_, k + 1, e);
            }
            if (Starts(L"accDescr")) {
                k = p_ + 8;
                while (k < s_.size() && std::iswspace(s_[k])) ++k;
                if (k < s_.size() && s_[k] == L'{') {
                    const size_t e = s_.find(L'}', k);
                    if (e != std::wstring_view::npos) return Make(T::Acc, e + 1 - p_, k + 1, e);
                }
            }
        }
        if (const size_t n = IdLength(p_)) return Make(T::Id, n, p_, p_ + n);
        if (const size_t n = StringLength(p_)) return StringToken(n);
        if (d_ == Dialect::Ebnf) {
            if (const size_t n = SpecialLength(p_)) return SpecialToken(n);
        }
        if (d_ == Dialect::Abnf) {
            if (s_[p_] == L'%' && (At(1) == L'x' || At(1) == L'X' || At(1) == L'd' || At(1) == L'D' || At(1) == L'b' || At(1) == L'B') && Hex(At(2))) {
                size_t e = p_ + 2;
                while (e < s_.size() && Hex(s_[e])) ++e;
                while (e + 1 < s_.size() && (s_[e] == L'-' || s_[e] == L'.') && Hex(s_[e + 1])) {
                    ++e;
                    while (e < s_.size() && Hex(s_[e])) ++e;
                }
                return Make(T::NumVal, e - p_, p_, e);
            }
            size_t e = p_;
            while (e < s_.size() && Digit(s_[e])) ++e;
            if (e < s_.size() && s_[e] == L'*') {
                ++e;
                while (e < s_.size() && Digit(s_[e])) ++e;
                return Make(T::Repeat, e - p_, p_, e);
            }
            if (e > p_) return Make(T::Exact, e - p_, p_, e);
        }
        mermaid::Fail(p_, Tr(L"Unexpected character", L"Непонятный символ"));
    }

    Token StringToken(size_t n) {
        // Quotes off; \n \r \t and \x (as x) (ABNF: as it is).
        std::wstring v;
        const size_t from = p_ + 1, to = p_ + n - 1;
        for (size_t i = from; i < to; ++i) {
            if (s_[i] == L'\\' && d_ != Dialect::Abnf && i + 1 < to) {
                ++i;
                v.push_back(s_[i] == L'n' ? L'\n' : s_[i] == L'r' ? L'\r' : s_[i] == L't' ? L'\t' : s_[i]);
            } else {
                v.push_back(s_[i]);
            }
        }
        if (v.size() > kMaxLabel) mermaid::Fail(p_, Tr(L"Text too long", L"Слишком длинный текст"));
        Token t{T::String, {}, Text{std::move(v), static_cast<uint32_t>(from), static_cast<uint32_t>(to - from)}};
        p_ += n;
        return t;
    }
    Token SpecialToken(size_t n) {
        size_t from = p_ + 1, to = p_ + n - 1;
        while (from < to && std::iswspace(s_[from])) ++from;
        while (to > from && std::iswspace(s_[to - 1])) --to;
        Token t{T::Special, {}, Text{std::wstring(s_.substr(from, to - from)), static_cast<uint32_t>(from), static_cast<uint32_t>(to - from)}};
        p_ += n;
        return t;
    }

    std::wstring_view s_;
    size_t yaml_none_ = std::wstring_view::npos, directive_none_ = std::wstring_view::npos;  // (YamlBlockLength)
    size_t p_;
    Dialect d_;
    mutable size_t no_close_[2] = {std::wstring_view::npos, std::wstring_view::npos};  // "*)", "*/" from here on
};

class Parser {
public:
    Parser(const Source& src, Dialect d) : src_(src), d_(d) {}

    void Parse() {
        toks_ = Lexer(src_.text, src_.body, d_).Run();
        static const wchar_t* kHeads[] = {L"railroad-beta", L"railroad-ebnf-beta", L"railroad-abnf-beta", L"railroad-peg-beta"};
        const std::wstring head = kHeads[static_cast<int>(d_)];
        if (!IsKey(head.c_str())) Fail(toks_[i_].text.pos, Tr(L"Expected \"", L"Ожидалось «") + head + Tr(L"\"", L"»"));
        ++i_;
        while (toks_[i_].kind == T::Title || toks_[i_].kind == T::Acc) ++i_;
        while (toks_[i_].kind != T::Eof) {
            Rule r;
            r.name = Take(T::Id, Tr(L"Expected a rule's name", L"Ожидалось имя правила"));
            switch (d_) {
            case Dialect::Calls:
                Expect(L"=");
                r.def = Call(0);
                break;
            case Dialect::Ebnf:
                if (IsKey(L"=") || IsKey(L"::=")) ++i_;
                else Fail(toks_[i_].text.pos, Tr(L"Expected \"=\" or \"::=\"", L"Ожидалось «=» или «::=»"));
                r.def = EbnfChoice(0);
                break;
            case Dialect::Abnf:
                Expect(L"=");
                r.def = AbnfAlternation(0);
                break;
            case Dialect::Peg:
                Expect(L"<-");
                r.def = PegChoice(0);
                break;
            }
            Expect(L";");
            if (rules.size() >= kMaxNodes) TooLarge();
            // (Each level of brackets is checked, but the ? * + after a group
            // wrap it outside that check: 40 levels made a tree 800 deep, too
            // deep for the recursion that sizes and draws it.)
            if (Height(r.def) > kMaxHeight) Fail(r.name.pos, Tr(L"Nested too deeply", L"Слишком глубокая вложенность"));
            rules.push_back(std::move(r));
        }
    }

    std::vector<Rule> rules;

private:
    bool IsKey(const wchar_t* w) const { return toks_[i_].kind == T::Keyword && toks_[i_].word == w; }
    void Expect(const wchar_t* w) {
        if (!IsKey(w)) Fail(toks_[i_].text.pos, Tr(L"Expected \"", L"Ожидалось «") + std::wstring(w) + Tr(L"\"", L"»"));
        ++i_;
    }
    Text Take(T kind, std::wstring message) {
        if (toks_[i_].kind != kind) Fail(toks_[i_].text.pos, std::move(message));
        return toks_[i_++].text;
    }
    void Count(size_t at, int depth) {
        if (++items_ > kMaxItems) TooLarge();
        if (depth > kMaxDepth) Fail(at, Tr(L"Nested too deeply", L"Слишком глубокая вложенность"));
    }
    // Levels of a tree, without recursing.
    static int Height(const Node& root) {
        int most = 0;
        std::vector<std::pair<const Node*, int>> todo{{&root, 1}};
        while (!todo.empty()) {
            const auto [n, level] = todo.back();
            todo.pop_back();
            most = std::max(most, level);
            for (const Node& k : n->kids) todo.push_back({&k, level + 1});
        }
        return most;
    }
    static Node Leaf(Node::Kind k, Text t) {
        Node n;
        n.kind = k;
        n.text = std::move(t);
        return n;
    }
    static Node Wrap(Node::Kind k, Node inner, bool bypass = false) {
        Node n;
        n.kind = k;
        n.bypass = bypass;
        n.kids.push_back(std::move(inner));
        return n;
    }
    // A sequence or choice of several; one alone is itself.
    static Node Many(Node::Kind k, std::vector<Node> kids) {
        if (kids.size() == 1) return std::move(kids[0]);
        Node n;
        n.kind = k;
        n.kids = std::move(kids);
        return n;
    }

    // railroad-beta: sequence(...), choice(...), terminal("x")...
    Node Call(int depth) {
        const Token& t = toks_[i_];
        Count(t.text.pos, depth);
        static const wchar_t* kCalls[] = {L"sequence", L"choice", L"optional", L"oneOrMore", L"zeroOrMore", L"terminal", L"nonterminal", L"special"};
        if (t.kind != T::Keyword || std::none_of(std::begin(kCalls), std::end(kCalls), [&](const wchar_t* c) { return t.word == c; })) Fail(t.text.pos, Tr(L"Expected sequence, choice, optional, oneOrMore, zeroOrMore, terminal, nonterminal or special",
                                                       L"Ожидалось sequence, choice, optional, oneOrMore, zeroOrMore, terminal, nonterminal или special"));
        const std::wstring w = t.word;
        ++i_;
        Expect(L"(");
        Node n;
        if (w == L"terminal" || w == L"nonterminal" || w == L"special") {
            const Text s = Take(T::String, Tr(L"Expected a \"quoted\" text", L"Ожидался текст в кавычках"));
            n = Leaf(w == L"terminal" ? Node::Terminal : w == L"nonterminal" ? Node::NonTerminal : Node::Special, s);
        } else if (w == L"sequence" || w == L"choice") {
            std::vector<Node> kids{Call(depth + 1)};
            while (IsKey(L",")) {
                ++i_;
                kids.push_back(Call(depth + 1));
            }
            n = Many(w == L"sequence" ? Node::Sequence : Node::Choice, std::move(kids));
        } else if (w == L"optional") {
            n = Wrap(Node::Optional, Call(depth + 1));
        } else if (w == L"oneOrMore" || w == L"zeroOrMore") {
            n = Wrap(Node::Repetition, Call(depth + 1), w == L"zeroOrMore");
        } else {
            Fail(t.text.pos, Tr(L"Unexpected \"", L"Непонятно: «") + w + Tr(L"\"", L"»"));
        }
        Expect(L")");
        return n;
    }

    // EBNF: a | b, sequences (commas optional), postfixes ? * + - x.
    Node EbnfChoice(int depth) {
        std::vector<Node> alts{EbnfSequence(depth)};
        while (IsKey(L"|")) {
            ++i_;
            alts.push_back(EbnfSequence(depth));
        }
        return Many(Node::Choice, std::move(alts));
    }
    bool EbnfStarts() const {
        const Token& t = toks_[i_];
        return t.kind == T::String || t.kind == T::Special || t.kind == T::Id || (t.kind == T::Keyword && (t.word == L"(" || t.word == L"[" || t.word == L"{"));
    }
    Node EbnfSequence(int depth) {
        std::vector<Node> elements{EbnfTerm(depth)};
        for (;;) {
            if (IsKey(L",")) {
                ++i_;
                elements.push_back(EbnfTerm(depth));
            } else if (EbnfStarts()) {
                elements.push_back(EbnfTerm(depth));
            } else {
                break;
            }
        }
        return Many(Node::Sequence, std::move(elements));
    }
    Node EbnfTerm(int depth) {
        Node n = EbnfPrimary(depth);
        for (int nested = 1;; ++nested) {
            if (depth + nested > kMaxDepth) Fail(toks_[i_].text.pos, Tr(L"Nested too deeply", L"Слишком глубокая вложенность"));
            if (IsKey(L"?")) {
                ++i_;
                n = Wrap(Node::Optional, std::move(n));
            } else if (IsKey(L"*") || IsKey(L"+")) {
                const bool zero = IsKey(L"*");
                ++i_;
                n = Wrap(Node::Repetition, std::move(n), zero);
            } else if (IsKey(L"-")) {
                const Text minus = toks_[i_].text;
                ++i_;
                Node except = EbnfPrimary(depth);
                std::vector<Node> kids;
                kids.push_back(std::move(n));
                kids.push_back(Leaf(Node::Terminal, Text{L"-", minus.pos, minus.len}));
                kids.push_back(std::move(except));
                Node seq;
                seq.kind = Node::Sequence;
                seq.kids = std::move(kids);
                n = std::move(seq);
            } else {
                return n;
            }
            Count(toks_[i_].text.pos, depth);
        }
    }
    Node EbnfPrimary(int depth) {
        const Token& t = toks_[i_];
        Count(t.text.pos, depth);
        switch (t.kind) {
        case T::String: ++i_; return Leaf(Node::Terminal, t.text);
        case T::Special: ++i_; return Leaf(Node::Special, t.text);
        case T::Id: ++i_; return Leaf(Node::NonTerminal, t.text);
        default: break;
        }
        if (IsKey(L"(")) {
            ++i_;
            Node n = EbnfChoice(depth + 1);
            Expect(L")");
            return n;
        }
        if (IsKey(L"[")) {
            ++i_;
            Node n = Wrap(Node::Optional, EbnfChoice(depth + 1));
            Expect(L"]");
            return n;
        }
        if (IsKey(L"{")) {
            ++i_;
            Node n = Wrap(Node::Repetition, EbnfChoice(depth + 1), true);
            Expect(L"}");
            return n;
        }
        Fail(t.text.pos, Tr(L"Expected a term", L"Ожидался элемент"));
    }

    // ABNF: a / b, concatenations, repeats (1*, *5, 3), [ ] and ( ).
    Node AbnfAlternation(int depth) {
        std::vector<Node> alts{AbnfConcatenation(depth)};
        while (IsKey(L"/")) {
            ++i_;
            alts.push_back(AbnfConcatenation(depth));
        }
        return Many(Node::Choice, std::move(alts));
    }
    bool AbnfStarts() const {
        const Token& t = toks_[i_];
        return t.kind == T::Repeat || t.kind == T::Exact || t.kind == T::String || t.kind == T::NumVal || t.kind == T::Id ||
               (t.kind == T::Keyword && (t.word == L"(" || t.word == L"["));
    }
    Node AbnfConcatenation(int depth) {
        std::vector<Node> elements{AbnfElement(depth)};
        while (AbnfStarts()) elements.push_back(AbnfElement(depth));
        return Many(Node::Sequence, std::move(elements));
    }
    Node AbnfElement(int depth) {
        std::wstring repeat;
        if (toks_[i_].kind == T::Repeat || toks_[i_].kind == T::Exact) repeat = toks_[i_++].text.raw;
        Node inner = AbnfPrimary(depth);
        if (repeat.empty()) return inner;
        // min*max, or an exact count.
        double lo = 0, hi = INFINITY;
        if (const size_t star = repeat.find(L'*'); star != std::wstring::npos) {
            if (star > 0) lo = std::wcstod(repeat.substr(0, star).c_str(), nullptr);
            if (star + 1 < repeat.size()) hi = std::wcstod(repeat.substr(star + 1).c_str(), nullptr);
        } else {
            lo = hi = std::wcstod(repeat.c_str(), nullptr);
        }
        if (lo == 0 && hi == 1) return Wrap(Node::Optional, std::move(inner));
        return Wrap(Node::Repetition, std::move(inner), lo == 0);
    }
    Node AbnfPrimary(int depth) {
        const Token& t = toks_[i_];
        Count(t.text.pos, depth);
        switch (t.kind) {
        case T::String:
        case T::NumVal: ++i_; return Leaf(Node::Terminal, t.text);
        case T::Id: ++i_; return Leaf(Node::NonTerminal, t.text);
        default: break;
        }
        if (IsKey(L"(")) {
            ++i_;
            Node n = AbnfAlternation(depth + 1);
            Expect(L")");
            return n;
        }
        if (IsKey(L"[")) {
            ++i_;
            Node n = Wrap(Node::Optional, AbnfAlternation(depth + 1));
            Expect(L"]");
            return n;
        }
        Fail(t.text.pos, Tr(L"Expected an element", L"Ожидался элемент"));
    }

    // PEG: a / b, sequences, &x !x (as specials), x? x* x+, ( ), .
    Node PegChoice(int depth) {
        std::vector<Node> alts{PegSequence(depth)};
        while (IsKey(L"/")) {
            ++i_;
            alts.push_back(PegSequence(depth));
        }
        return Many(Node::Choice, std::move(alts));
    }
    bool PegStarts() const {
        const Token& t = toks_[i_];
        return t.kind == T::String || t.kind == T::Id || (t.kind == T::Keyword && (t.word == L"&" || t.word == L"!" || t.word == L"(" || t.word == L"."));
    }
    Node PegSequence(int depth) {
        if (!PegStarts()) Fail(toks_[i_].text.pos, Tr(L"Expected an expression", L"Ожидалось выражение"));
        std::vector<Node> elements;
        while (PegStarts()) elements.push_back(PegPrefix(depth));
        return Many(Node::Sequence, std::move(elements));
    }
    Node PegPrefix(int depth) {
        std::wstring op;
        Text at;
        if (IsKey(L"&") || IsKey(L"!")) {
            op = toks_[i_].word;
            at = toks_[i_].text;
            ++i_;
        }
        Node inner = PegPrimary(depth);
        if (IsKey(L"?")) {
            ++i_;
            inner = Wrap(Node::Optional, std::move(inner));
        } else if (IsKey(L"*") || IsKey(L"+")) {
            const bool zero = IsKey(L"*");
            ++i_;
            inner = Wrap(Node::Repetition, std::move(inner), zero);
        }
        if (op.empty()) return inner;
        // Mermaid's label of what it looks at.
        std::wstring label;
        switch (inner.kind) {
        case Node::Terminal: label = L"\"" + inner.text.raw + L"\""; break;
        case Node::NonTerminal:
        case Node::Special: label = inner.text.raw; break;
        default: label = L"(...)"; break;
        }
        return Leaf(Node::Special, Text{op + label, at.pos, 0});
    }
    Node PegPrimary(int depth) {
        const Token& t = toks_[i_];
        Count(t.text.pos, depth);
        switch (t.kind) {
        case T::String: ++i_; return Leaf(Node::Terminal, t.text);
        case T::Id: ++i_; return Leaf(Node::NonTerminal, t.text);
        default: break;
        }
        if (IsKey(L"(")) {
            ++i_;
            Node n = PegChoice(depth + 1);
            Expect(L")");
            return n;
        }
        if (IsKey(L".")) {
            const Text dot = t.text;
            ++i_;
            return Leaf(Node::Special, dot);
        }
        Fail(t.text.pos, Tr(L"Expected an expression", L"Ожидалось выражение"));
    }

    const Source& src_;
    Dialect d_;
    std::vector<Token> toks_;
    size_t i_ = 0;
    size_t items_ = 0;
};

using M = SceneBuilder::Measured;

constexpr double kPad = 10, kVSep = 8, kHSep = 10, kArc = 10, kFont = 16, kTextH = 19, kMarker = 5, kStroke = 2;

class Builder {
public:
    Builder(const Parser& p, SceneBuilder& b) : p_(p), b_(b) {}

    std::shared_ptr<DiagramScene> Build() {
        if (p_.rules.empty()) return b_.Finish(200, 100);
        double y = kPad, width = 0;
        for (const Rule& r : p_.rules) {
            const auto [h, w] = DrawRule(r, y);
            y += h + kVSep;
            width = std::max(width, w);
        }
        return b_.Finish(static_cast<float>(width + kPad * 2), static_cast<float>(y + kPad));
    }

private:
    static D2D1_POINT_2F P(double x, double y) { return D2D1::Point2F(static_cast<float>(x), static_cast<float>(y)); }

    M Measure(const std::wstring& s, bool bold = false) {
        RichText t;
        t.text = s;
        return b_.Measure(t, 0, bold, static_cast<float>(kFont) / b_.size());
    }

    // Sizes, bottom up (Mermaid's renderExpression without drawing).
    void Size(const Node& n) {
        switch (n.kind) {
        case Node::Terminal:
        case Node::NonTerminal:
        case Node::Special: {
            // (No text: SVG measures nothing, not even a line.)
            const std::wstring shown = Shown(n);
            n.w = (shown.empty() ? 0 : Measure(shown).w) + kPad * 2;
            n.h = (shown.empty() ? 0 : kTextH) + kPad * 2;
            n.up = n.h / 2;
            break;
        }
        case Node::Sequence: {
            double w = 0, up = 0, down = 0;
            for (const Node& k : n.kids) {
                Size(k);
                w += k.w;
                up = std::max(up, k.up);
                down = std::max(down, k.h - k.up);
            }
            n.w = w + static_cast<double>(n.kids.size() - 1) * kHSep;
            n.up = up;
            n.h = up + down;
            break;
        }
        case Node::Choice: {
            double w = 0, h = 0;
            for (const Node& k : n.kids) {
                Size(k);
                w = std::max(w, k.w);
                h += k.h;
            }
            h += static_cast<double>(n.kids.size() - 1) * kVSep;
            n.w = w + kArc * 4;
            n.h = h;
            n.up = h / 2;
            break;
        }
        case Node::Optional:
        case Node::Repetition: {
            const Node& k = n.kids[0];
            Size(k);
            n.w = k.w + kArc * 4;
            const bool over = n.kind == Node::Optional || n.bypass;
            n.h = k.h + kArc * 2 + (n.kind == Node::Repetition && over ? kArc * 2 : 0);
            n.up = (over ? kArc * 2 : 0) + k.up;
            break;
        }
        }
    }

    // The text as SVG shows it: line ends and tabs as spaces, runs of them
    // one, none at the ends.
    static std::wstring Shown(const Node& n) {
        const std::wstring raw = DecodeEntities(n.kind == Node::Special ? L"? " + n.text.raw + L" ?" : n.text.raw);
        std::wstring out;
        for (const wchar_t c : raw) {
            const bool blank = c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
            if (!blank) out.push_back(c);
            else if (!out.empty() && out.back() != L' ') out.push_back(L' ');
        }
        if (!out.empty() && out.back() == L' ') out.pop_back();
        return out;
    }

    // A path of lines and quarter arcs (SVG's A with radius kArc).
    class Path {
    public:
        Path(SceneBuilder& b, double ox, double oy, double x, double y) : b_(b), ox_(ox), oy_(oy), x_(x), y_(y) { b_.Begin(P(ox_ + x, oy_ + y)); }
        Path& Line(double x, double y) {
            b_.LineTo(P(ox_ + x, oy_ + y));
            x_ = x;
            y_ = y;
            return *this;
        }
        // To (x, y) along a quarter circle, clockwise with `sweep`.
        Path& Arc(bool sweep, double x, double y) {
            const double cands[2][2] = {{x_, y}, {x, y_}};
            for (const auto& c : cands) {
                const double ax = x_ - c[0], ay = y_ - c[1], bx = x - c[0], by = y - c[1];
                const double cross = ax * by - ay * bx;
                if ((cross > 0) != sweep) continue;
                constexpr double k = 0.5522847498;
                b_.CubicTo(P(ox_ + x_ + bx * k, oy_ + y_ + by * k), P(ox_ + x + ax * k, oy_ + y + ay * k), P(ox_ + x, oy_ + y));
                break;
            }
            x_ = x;
            y_ = y;
            return *this;
        }
        void End() { b_.End(Role(kDgNone), Role(kDgLine), static_cast<float>(kStroke)); }

    private:
        SceneBuilder& b_;
        double ox_, oy_, x_, y_;
    };

    void Line(double ox, double oy, double x1, double y1, double x2, double y2) { Path(b_, ox, oy, x1, y1).Line(x2, y2).End(); }

    // Draws `n` with its top left at (ox, oy).
    void Draw(const Node& n, double ox, double oy) {
        switch (n.kind) {
        case Node::Terminal:
        case Node::NonTerminal:
        case Node::Special: {
            const D2D1_RECT_F r = D2D1::RectF(static_cast<float>(ox), static_cast<float>(oy), static_cast<float>(ox + n.w), static_cast<float>(oy + n.h));
            const uint8_t rr = kDgRailroad;
            if (n.kind == Node::Terminal) b_.Rect(r, 10, Role(static_cast<uint8_t>(kDgFillType + 1)), Role(rr), static_cast<float>(kStroke));
            else if (n.kind == Node::NonTerminal) b_.Rect(r, 0, Role(kDgNodeFill), Role(kDgPrimaryBorder), static_cast<float>(kStroke));
            else b_.Rect(r, 0, Role(static_cast<uint8_t>(rr + 2)), Role(static_cast<uint8_t>(rr + 3)), static_cast<float>(kStroke), 1);
            const std::wstring shown = Shown(n);
            if (shown.empty()) break;
            const M m = Measure(shown);
            const double baseline = oy + n.h / 2 + 0.3 * kFont;
            const bool shown_as_is = n.kind != Node::Special;
            b_.Text(m, P(ox + n.w / 2 - m.w / 2, baseline - 1.17 * kFont), n.kind == Node::Terminal ? Role(static_cast<uint8_t>(rr + 1)) : Role(kDgText),
                    shown_as_is ? n.text.pos : 0, shown_as_is ? n.text.len : 0);
            break;
        }
        case Node::Sequence: {
            double x = 0;
            for (size_t i = 0; i < n.kids.size(); ++i) {
                const Node& k = n.kids[i];
                Draw(k, ox + x, oy + n.up - k.up);
                if (i + 1 < n.kids.size()) Line(ox, oy, x + k.w, n.up, x + k.w + kHSep, n.up);
                x += k.w + kHSep;
            }
            break;
        }
        case Node::Choice: {
            const double max_w = n.w - kArc * 4, center = n.h / 2, right_lane = n.w - kArc * 2;
            double y = 0;
            for (const Node& k : n.kids) {
                const double ey = y, ec = ey + k.up, ex = kArc * 2 + (max_w - k.w) / 2;
                Draw(k, ox + ex, oy + ey);
                const bool below = ec > center;
                if (ec == center) {
                    Path(b_, ox, oy, 0, center).Line(ex, ec).End();
                    Path(b_, ox, oy, ex + k.w, ec).Line(n.w, center).End();
                } else {
                    Path(b_, ox, oy, 0, center)
                        .Arc(below, kArc, center + (below ? kArc : -kArc))
                        .Line(kArc, ec - (below ? kArc : -kArc))
                        .Arc(!below, kArc * 2, ec)
                        .Line(ex, ec)
                        .End();
                    Path(b_, ox, oy, ex + k.w, ec)
                        .Line(right_lane, ec)
                        .Arc(!below, n.w - kArc, ec + (below ? -kArc : kArc))
                        .Line(n.w - kArc, center + (below ? kArc : -kArc))
                        .Arc(below, n.w, center)
                        .End();
                }
                y += k.h + kVSep;
            }
            break;
        }
        case Node::Optional:
        case Node::Repetition: {
            const Node& k = n.kids[0];
            const bool over = n.kind == Node::Optional || n.bypass;
            const double ex = kArc * 2, ey = over ? kArc * 2 : 0, center = ey + k.up;
            Draw(k, ox + ex, oy + ey);
            Line(ox, oy, 0, center, kArc * 2, center);
            Line(ox, oy, ex + k.w, center, n.w, center);
            if (n.kind == Node::Repetition) {
                const double loop = ey + k.h + kArc;
                Path(b_, ox, oy, ex + k.w, center)
                    .Arc(true, ex + k.w + kArc, center + kArc)
                    .Line(ex + k.w + kArc, loop)
                    .Arc(true, ex + k.w, loop + kArc)
                    .Line(kArc * 2, loop + kArc)
                    .Arc(true, kArc, loop)
                    .Line(kArc, center + kArc)
                    .Arc(true, kArc * 2, center)
                    .End();
            }
            if (over) {
                Path(b_, ox, oy, 0, center)
                    .Arc(false, kArc, center - kArc)
                    .Line(kArc, kArc)
                    .Arc(true, kArc * 2, 0)
                    .Line(n.w - kArc * 2, 0)
                    .Arc(true, n.w - kArc, kArc)
                    .Line(n.w - kArc, center - kArc)
                    .Arc(false, n.w, center)
                    .End();
            }
            break;
        }
        }
    }

    // A rule: "name =", a dot, its diagram, a dot. Returns its height and width.
    std::pair<double, double> DrawRule(const Rule& r, double y) {
        const std::wstring name = r.name.raw + L" =";
        const double name_w = Measure(name).w + 20, def_x = name_w + 20;
        Size(r.def);
        const double baseline = std::max(20.0, r.def.up), def_y = baseline - r.def.up;
        Draw(r.def, def_x, y + def_y);
        const M m = Measure(name, true);
        b_.Text(m, P(0, y + baseline - 1.17 * kFont), Role(kDgText), r.name.pos, r.name.len);
        const double end_x = def_x + r.def.w + 10;
        b_.Ellipse(D2D1::RectF(static_cast<float>(name_w - kMarker), static_cast<float>(y + baseline - kMarker), static_cast<float>(name_w + kMarker),
                               static_cast<float>(y + baseline + kMarker)),
                   Role(kDgLine), Role(kDgNone));
        b_.Ellipse(D2D1::RectF(static_cast<float>(end_x - kMarker), static_cast<float>(y + baseline - kMarker), static_cast<float>(end_x + kMarker),
                               static_cast<float>(y + baseline + kMarker)),
                   Role(kDgLine), Role(kDgNone));
        Line(0, y, name_w + kMarker, baseline, def_x, baseline);
        Line(0, y, def_x + r.def.w, baseline, end_x - kMarker, baseline);
        return {std::max(40.0, def_y + r.def.h + kPad * 2), end_x + kMarker};
    }

    const Parser& p_;
    SceneBuilder& b_;
};

std::shared_ptr<DiagramScene> Railroad(const Source& src, SceneBuilder* builder, Dialect d) {
    Parser p(src, d);
    p.Parse();
    if (!builder) return nullptr;
    return Builder(p, *builder).Build();
}

}  // namespace

std::shared_ptr<DiagramScene> RailroadDiagram(const Source& src, SceneBuilder* builder) { return Railroad(src, builder, Dialect::Calls); }
std::shared_ptr<DiagramScene> EbnfDiagram(const Source& src, SceneBuilder* builder) { return Railroad(src, builder, Dialect::Ebnf); }
std::shared_ptr<DiagramScene> AbnfDiagram(const Source& src, SceneBuilder* builder) { return Railroad(src, builder, Dialect::Abnf); }
std::shared_ptr<DiagramScene> PegDiagram(const Source& src, SceneBuilder* builder) { return Railroad(src, builder, Dialect::Peg); }

}  // namespace mdv::mermaid
