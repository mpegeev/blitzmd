#include "core/texmath.h"

#include <algorithm>

#include "core/document.h"
#include "core/util.h"

namespace mdv {
namespace {

struct Symbol {
    const wchar_t* name;
    const wchar_t* text;
};

// Sorted by name for binary search.
constexpr Symbol kSymbols[] = {
    {L"Alpha", L"Α"}, {L"Beta", L"Β"}, {L"Chi", L"Χ"}, {L"Delta", L"Δ"}, {L"Downarrow", L"⇓"},
    {L"Epsilon", L"Ε"}, {L"Eta", L"Η"}, {L"Gamma", L"Γ"}, {L"Im", L"ℑ"}, {L"Iota", L"Ι"}, {L"Kappa", L"Κ"},
    {L"Lambda", L"Λ"}, {L"Leftarrow", L"⇐"}, {L"Leftrightarrow", L"⇔"}, {L"Longleftarrow", L"⟸"},
    {L"Longleftrightarrow", L"⟺"}, {L"Longrightarrow", L"⟹"}, {L"Mu", L"Μ"}, {L"Nu", L"Ν"}, {L"Omega", L"Ω"},
    {L"Phi", L"Φ"}, {L"Pi", L"Π"}, {L"Psi", L"Ψ"}, {L"Re", L"ℜ"}, {L"Rho", L"Ρ"}, {L"Rightarrow", L"⇒"},
    {L"Sigma", L"Σ"}, {L"Tau", L"Τ"}, {L"Theta", L"Θ"}, {L"Uparrow", L"⇑"}, {L"Upsilon", L"Υ"}, {L"Vert", L"‖"},
    {L"Xi", L"Ξ"}, {L"Zeta", L"Ζ"}, {L"aleph", L"ℵ"}, {L"alpha", L"α"}, {L"amalg", L"⨿"}, {L"angle", L"∠"},
    {L"approx", L"≈"}, {L"ast", L"∗"}, {L"asymp", L"≍"}, {L"backslash", L"∖"}, {L"because", L"∵"},
    {L"beta", L"β"}, {L"beth", L"ℶ"}, {L"bigcap", L"⋂"}, {L"bigcirc", L"◯"}, {L"bigcup", L"⋃"},
    {L"bigodot", L"⨀"}, {L"bigoplus", L"⨁"}, {L"bigotimes", L"⨂"}, {L"bigsqcup", L"⨆"}, {L"bigstar", L"★"},
    {L"bigtriangledown", L"▽"}, {L"bigtriangleup", L"△"}, {L"biguplus", L"⨄"}, {L"bigvee", L"⋁"},
    {L"bigwedge", L"⋀"}, {L"bot", L"⊥"}, {L"bowtie", L"⋈"}, {L"bullet", L"∙"}, {L"cap", L"∩"}, {L"cdot", L"⋅"},
    {L"cdots", L"⋯"}, {L"checkmark", L"✓"}, {L"chi", L"χ"}, {L"circ", L"∘"}, {L"clubsuit", L"♣"},
    {L"colon", L":"}, {L"cong", L"≅"}, {L"coprod", L"∐"}, {L"cup", L"∪"}, {L"dagger", L"†"}, {L"dashv", L"⊣"},
    {L"ddagger", L"‡"}, {L"ddots", L"⋱"}, {L"degree", L"°"}, {L"delta", L"δ"}, {L"diamond", L"⋄"},
    {L"diamondsuit", L"♢"}, {L"digamma", L"ϝ"}, {L"div", L"÷"}, {L"dots", L"…"}, {L"dotsb", L"⋯"},
    {L"dotsc", L"…"}, {L"doteq", L"≐"}, {L"downarrow", L"↓"}, {L"ell", L"ℓ"}, {L"emptyset", L"∅"},
    {L"epsilon", L"ϵ"}, {L"equiv", L"≡"}, {L"eta", L"η"}, {L"exists", L"∃"}, {L"flat", L"♭"},
    {L"forall", L"∀"}, {L"frown", L"⌢"}, {L"gamma", L"γ"}, {L"ge", L"≥"}, {L"geq", L"≥"}, {L"geqslant", L"⩾"},
    {L"gets", L"←"}, {L"gg", L"≫"}, {L"gimel", L"ℷ"}, {L"hbar", L"ℏ"}, {L"heartsuit", L"♡"},
    {L"hookleftarrow", L"↩"}, {L"hookrightarrow", L"↪"}, {L"iff", L"⟺"}, {L"iiint", L"∭"}, {L"iint", L"∬"},
    {L"imath", L"ı"}, {L"implies", L"⟹"}, {L"in", L"∈"}, {L"infty", L"∞"}, {L"int", L"∫"}, {L"iota", L"ι"},
    {L"jmath", L"ȷ"}, {L"kappa", L"κ"}, {L"lambda", L"λ"}, {L"land", L"∧"}, {L"langle", L"⟨"}, {L"lbrace", L"{"},
    {L"lceil", L"⌈"}, {L"ldots", L"…"}, {L"le", L"≤"}, {L"leftarrow", L"←"}, {L"leftrightarrow", L"↔"},
    {L"leq", L"≤"}, {L"leqslant", L"⩽"}, {L"lfloor", L"⌊"}, {L"ll", L"≪"}, {L"lnot", L"¬"},
    {L"longleftarrow", L"⟵"}, {L"longleftrightarrow", L"⟷"}, {L"longmapsto", L"⟼"}, {L"longrightarrow", L"⟶"},
    {L"lor", L"∨"}, {L"mapsto", L"↦"}, {L"mho", L"℧"}, {L"mid", L"∣"}, {L"models", L"⊨"}, {L"mp", L"∓"},
    {L"mu", L"μ"}, {L"nabla", L"∇"}, {L"natural", L"♮"}, {L"ne", L"≠"}, {L"nearrow", L"↗"}, {L"neg", L"¬"},
    {L"neq", L"≠"}, {L"nexists", L"∄"}, {L"ni", L"∋"}, {L"notin", L"∉"}, {L"nu", L"ν"}, {L"nwarrow", L"↖"},
    {L"odot", L"⊙"}, {L"oint", L"∮"}, {L"omega", L"ω"}, {L"ominus", L"⊖"}, {L"oplus", L"⊕"}, {L"oslash", L"⊘"},
    {L"otimes", L"⊗"}, {L"parallel", L"∥"}, {L"partial", L"∂"}, {L"perp", L"⊥"}, {L"phi", L"ϕ"}, {L"pi", L"π"},
    {L"pm", L"±"}, {L"prec", L"≺"}, {L"preceq", L"⪯"}, {L"prime", L"′"}, {L"prod", L"∏"}, {L"propto", L"∝"},
    {L"psi", L"ψ"}, {L"qed", L"∎"}, {L"rangle", L"⟩"}, {L"rbrace", L"}"}, {L"rceil", L"⌉"}, {L"rfloor", L"⌋"},
    {L"rho", L"ρ"}, {L"rightarrow", L"→"}, {L"rightleftharpoons", L"⇌"}, {L"searrow", L"↘"}, {L"setminus", L"∖"},
    {L"sharp", L"♯"}, {L"sigma", L"σ"}, {L"sim", L"∼"}, {L"simeq", L"≃"}, {L"smile", L"⌣"}, {L"spadesuit", L"♠"},
    {L"sqcap", L"⊓"}, {L"sqcup", L"⊔"}, {L"sqsubseteq", L"⊑"}, {L"sqsupseteq", L"⊒"}, {L"star", L"⋆"},
    {L"subset", L"⊂"}, {L"subseteq", L"⊆"}, {L"subsetneq", L"⊊"}, {L"succ", L"≻"}, {L"succeq", L"⪰"},
    {L"sum", L"∑"}, {L"supset", L"⊃"}, {L"supseteq", L"⊇"}, {L"supsetneq", L"⊋"}, {L"surd", L"√"},
    {L"swarrow", L"↙"}, {L"tau", L"τ"}, {L"therefore", L"∴"}, {L"theta", L"θ"}, {L"times", L"×"},
    {L"to", L"→"}, {L"top", L"⊤"}, {L"triangle", L"△"}, {L"triangleleft", L"◁"}, {L"triangleright", L"▷"},
    {L"uparrow", L"↑"}, {L"uplus", L"⊎"}, {L"upsilon", L"υ"}, {L"varepsilon", L"ε"}, {L"varkappa", L"ϰ"},
    {L"varnothing", L"∅"}, {L"varphi", L"φ"}, {L"varpi", L"ϖ"}, {L"varrho", L"ϱ"}, {L"varsigma", L"ς"},
    {L"vartheta", L"ϑ"}, {L"vdash", L"⊢"}, {L"vdots", L"⋮"}, {L"vee", L"∨"}, {L"vert", L"|"}, {L"wedge", L"∧"},
    {L"wp", L"℘"}, {L"wr", L"≀"}, {L"xi", L"ξ"}, {L"zeta", L"ζ"},
};

constexpr const wchar_t* kFunctions[] = {
    L"Pr", L"arccos", L"arcsin", L"arctan", L"arg", L"cos", L"cosh", L"cot", L"coth", L"csc", L"deg", L"det",
    L"dim", L"exp", L"gcd", L"hom", L"inf", L"ker", L"lg", L"lim", L"liminf", L"limsup", L"ln", L"log", L"max",
    L"min", L"mod", L"sec", L"sin", L"sinh", L"sup", L"tan", L"tanh",
};

const wchar_t* LookupSymbol(std::wstring_view name) {
    size_t lo = 0, hi = std::size(kSymbols);
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        const int c = name.compare(kSymbols[mid].name);
        if (c == 0) return kSymbols[mid].text;
        if (c < 0) hi = mid;
        else lo = mid + 1;
    }
    return nullptr;
}

bool IsFunction(std::wstring_view name) {
    for (const wchar_t* f : kFunctions) {
        if (name == f) return true;
    }
    return false;
}

enum class Font : uint8_t { Italic, Upright, Bold, BlackBoard, Script, Fraktur, Sans, Mono, BoldItalic };

// Maps an ASCII letter/digit to a Mathematical Alphanumeric Symbol.
void AppendStyled(std::wstring& out, wchar_t c, Font font) {
    const bool upper = c >= L'A' && c <= L'Z';
    const bool lower = c >= L'a' && c <= L'z';
    const bool digit = c >= L'0' && c <= L'9';
    auto cp = [&](uint32_t upper_base, uint32_t lower_base) {
        AppendCodepoint(out, upper ? upper_base + (c - L'A') : lower_base + (c - L'a'));
    };
    switch (font) {
    case Font::Italic:
        if (c == L'h') out.push_back(0x210E);
        else if (upper || lower) cp(0x1D434, 0x1D44E);
        else out.push_back(c);
        return;
    case Font::BoldItalic:
        if (upper || lower) cp(0x1D468, 0x1D482);
        else out.push_back(c);
        return;
    case Font::Bold:
        if (upper || lower) cp(0x1D400, 0x1D41A);
        else if (digit) AppendCodepoint(out, 0x1D7CE + (c - L'0'));
        else out.push_back(c);
        return;
    case Font::BlackBoard: {
        static const wchar_t kExceptions[] = L"CHNPQRZ";
        static const wchar_t kChars[] = L"ℂℍℕℙℚℝℤ";
        if (upper) {
            for (int i = 0; kExceptions[i]; ++i) {
                if (kExceptions[i] == c) {
                    out.push_back(kChars[i]);
                    return;
                }
            }
            AppendCodepoint(out, 0x1D538 + (c - L'A'));
        } else if (lower) {
            AppendCodepoint(out, 0x1D552 + (c - L'a'));
        } else if (digit) {
            AppendCodepoint(out, 0x1D7D8 + (c - L'0'));
        } else {
            out.push_back(c);
        }
        return;
    }
    case Font::Script: {
        static const wchar_t kExceptions[] = L"BEFHILMRego";
        static const wchar_t kChars[] = L"ℬℰℱℋℐℒℳℛℯℊℴ";
        for (int i = 0; kExceptions[i]; ++i) {
            if (kExceptions[i] == c) {
                out.push_back(kChars[i]);
                return;
            }
        }
        if (upper || lower) cp(0x1D49C, 0x1D4B6);
        else out.push_back(c);
        return;
    }
    case Font::Fraktur: {
        static const wchar_t kExceptions[] = L"CHIRZ";
        static const wchar_t kChars[] = L"ℭℌℑℜℨ";
        for (int i = 0; kExceptions[i]; ++i) {
            if (kExceptions[i] == c) {
                out.push_back(kChars[i]);
                return;
            }
        }
        if (upper || lower) cp(0x1D504, 0x1D51E);
        else out.push_back(c);
        return;
    }
    case Font::Sans:
        if (upper || lower) cp(0x1D5A0, 0x1D5BA);
        else if (digit) AppendCodepoint(out, 0x1D7E2 + (c - L'0'));
        else out.push_back(c);
        return;
    case Font::Mono:
        if (upper || lower) cp(0x1D670, 0x1D68A);
        else if (digit) AppendCodepoint(out, 0x1D7F6 + (c - L'0'));
        else out.push_back(c);
        return;
    default:
        out.push_back(c);
        return;
    }
}

// Unicode superscript / subscript forms, if every character has one.
bool ToScript(std::wstring_view s, bool sup, std::wstring& out) {
    static const wchar_t kSupFrom[] = L"0123456789+-−=()niʹ′";
    static const wchar_t kSupTo[] = L"⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻⁻⁼⁽⁾ⁿⁱʹ′";
    static const wchar_t kSubFrom[] = L"0123456789+-−=()aeoxhklmnpstij";
    static const wchar_t kSubTo[] = L"₀₁₂₃₄₅₆₇₈₉₊₋₋₌₍₎ₐₑₒₓₕₖₗₘₙₚₛₜᵢⱼ";
    const wchar_t* from = sup ? kSupFrom : kSubFrom;
    const wchar_t* to = sup ? kSupTo : kSubTo;
    std::wstring r;
    for (wchar_t c : s) {
        const wchar_t* p = wcschr(from, c);
        if (!p || !c) return false;
        r.push_back(to[p - from]);
    }
    out = std::move(r);
    return !out.empty();
}

class Converter {
public:
    Converter(std::wstring_view s, bool display, int depth = 0) : s_(s), display_(display), depth_(depth) {}

    void Run(std::vector<MathPiece>& out) {
        ParseSeq(false, 0, Font::Italic);
        Trim();
        out.insert(out.end(), pieces_.begin(), pieces_.end());
    }

private:
    void Emit(std::wstring_view t, uint32_t flags) {
        if (t.empty()) return;
        if (!pieces_.empty() && pieces_.back().flags == flags) {
            pieces_.back().text.append(t);
        } else {
            pieces_.push_back(MathPiece{std::wstring(t), flags});
        }
    }

    void Trim() {
        while (!pieces_.empty() && !pieces_.back().text.empty() && IsSpace(pieces_.back().text.back())) {
            pieces_.back().text.pop_back();
            if (pieces_.back().text.empty()) pieces_.pop_back();
        }
    }

    bool AtEnd() const { return i_ >= s_.size(); }

    void SkipSpaces() {
        while (!AtEnd() && IsSpace(s_[i_])) ++i_;
    }

    std::wstring ReadCommandName() {
        // After '\': letters, or a single non-letter character.
        size_t b = i_;
        if (!AtEnd() && ((s_[i_] >= L'a' && s_[i_] <= L'z') || (s_[i_] >= L'A' && s_[i_] <= L'Z'))) {
            while (!AtEnd() && ((s_[i_] >= L'a' && s_[i_] <= L'z') || (s_[i_] >= L'A' && s_[i_] <= L'Z'))) ++i_;
        } else if (!AtEnd()) {
            ++i_;
        }
        return std::wstring(s_.substr(b, i_ - b));
    }

    // Raw text of the next argument ({...} or one token), without converting.
    std::wstring_view RawArgument() {
        SkipSpaces();
        if (AtEnd()) return {};
        if (s_[i_] == L'{') {
            const size_t b = ++i_;
            int depth = 1;
            while (!AtEnd()) {
                if (s_[i_] == L'\\' && i_ + 1 < s_.size()) {
                    i_ += 2;
                    continue;
                }
                if (s_[i_] == L'{') ++depth;
                else if (s_[i_] == L'}' && --depth == 0) break;
                ++i_;
            }
            const std::wstring_view r = s_.substr(b, i_ - b);
            if (!AtEnd()) ++i_;
            return r;
        }
        const size_t b = i_;
        if (s_[i_] == L'\\') {
            ++i_;
            ReadCommandName();
        } else {
            ++i_;
            if (i_ < s_.size() && s_[i_ - 1] >= 0xD800 && s_[i_ - 1] <= 0xDBFF) ++i_;
        }
        return s_.substr(b, i_ - b);
    }

    // Converts an argument to plain text (flags dropped).
    std::wstring ConvertArgument(Font font, std::vector<MathPiece>* pieces = nullptr) {
        const std::wstring_view raw = RawArgument();
        Converter sub(raw, display_, depth_ + 1);
        sub.ParseSeq(false, 0, font);
        std::wstring text;
        for (const auto& p : sub.pieces_) text += p.text;
        if (pieces) *pieces = sub.pieces_;
        return text;
    }

    void Script(bool sup, uint32_t flags, Font font) {
        std::vector<MathPiece> parts;
        const std::wstring text = ConvertArgument(font, &parts);
        const uint32_t flag = sup ? kSup : kSub;
        std::wstring converted;
        const bool simple = std::all_of(parts.begin(), parts.end(), [](const MathPiece& p) { return p.flags == 0; });
        const bool nested = (flags & (kSup | kSub)) != 0;
        // Digits etc. use Unicode forms (nicer, and nest); the rest is drawn
        // as real superscript/subscript text.
        std::wstring plain;
        for (wchar_t c : text) plain.push_back(c);
        if (simple && ToScript(plain, sup, converted) && (nested || plain.size() <= 3)) {
            Emit(converted, flags);
            return;
        }
        if (nested) {
            Emit(text, flags);
            return;
        }
        for (const auto& p : parts) Emit(p.text, flags | flag | p.flags);
    }

    void Environment(const std::wstring& env, uint32_t flags) {
        // Body up to the matching \end{env}.
        const std::wstring end_tag = L"\\end{" + env + L"}";
        const size_t e = s_.find(end_tag, i_);
        const std::wstring_view body = s_.substr(i_, (e == std::wstring_view::npos ? s_.size() : e) - i_);
        i_ = e == std::wstring_view::npos ? s_.size() : e + end_tag.size();

        std::wstring open, close;
        if (env == L"pmatrix") { open = L"("; close = L")"; }
        else if (env == L"bmatrix") { open = L"["; close = L"]"; }
        else if (env == L"Bmatrix" || env == L"cases") { open = L"{"; close = env == L"cases" ? L"" : L"}"; }
        else if (env == L"vmatrix") { open = L"|"; close = L"|"; }
        else if (env == L"Vmatrix") { open = L"‖"; close = L"‖"; }
        const bool matrix = env.find(L"matrix") != std::wstring::npos;

        // Split rows on \\ and cells on &.
        std::vector<std::vector<std::wstring_view>> rows(1);
        std::vector<std::wstring_view>* row = &rows.back();
        size_t cell_begin = 0;
        int depth = 0;
        for (size_t k = 0; k <= body.size(); ++k) {
            const bool end = k == body.size();
            if (!end && body[k] == L'{') ++depth;
            if (!end && body[k] == L'}') --depth;
            if (end || (depth == 0 && body[k] == L'&') ||
                (depth == 0 && body[k] == L'\\' && k + 1 < body.size() && body[k + 1] == L'\\')) {
                row->push_back(body.substr(cell_begin, k - cell_begin));
                if (!end && body[k] == L'\\') {
                    rows.emplace_back();
                    row = &rows.back();
                    ++k;
                }
                cell_begin = k + 1;
            }
        }
        while (!rows.empty() && rows.back().size() == 1 && mdv::Trim(rows.back()[0]).empty()) rows.pop_back();

        Emit(open, flags);
        for (size_t r = 0; r < rows.size(); ++r) {
            // Matrices stay on one line ("(a b; c d)"); aligned/cases rows
            // become lines in display math.
            if (r > 0) Emit(matrix ? L"; " : (display_ ? L"\n" : L", "), flags);
            for (size_t c = 0; c < rows[r].size(); ++c) {
                if (c > 0) Emit(matrix ? L"  " : (env == L"cases" ? L",  " : L" "), flags);
                Converter sub(rows[r][c], false, depth_ + 1);
                sub.ParseSeq(false, flags, Font::Italic);
                sub.Trim();
                // Leading spaces of a cell are noise.
                for (auto& p : sub.pieces_) {
                    Emit(p.text, p.flags);
                }
            }
        }
        Emit(close, flags);
    }

    void Command(const std::wstring& name, uint32_t flags, Font font) {
        if (name == L"frac" || name == L"dfrac" || name == L"tfrac" || name == L"cfrac") {
            const std::wstring num = ConvertArgument(font);
            const std::wstring den = ConvertArgument(font);
            auto simple = [](const std::wstring& s) {
                return s.size() <= 2 || std::none_of(s.begin(), s.end(), [](wchar_t c) {
                           return c == L'+' || c == L'−' || c == L' ' || c == L'=' || c == L'/';
                       });
            };
            std::wstring sup, sub;
            if (!(flags & (kSup | kSub)) && ToScript(num, true, sup) && ToScript(den, false, sub)) {
                Emit(sup + L"⁄" + sub, flags);  // ½-style for numeric fractions
                return;
            }
            Emit(simple(num) ? num : L"(" + num + L")", flags);
            Emit(L"/", flags);
            Emit(simple(den) ? den : L"(" + den + L")", flags);
            return;
        }
        if (name == L"sqrt") {
            SkipSpaces();
            std::wstring index;
            if (!AtEnd() && s_[i_] == L'[') {
                const size_t e = s_.find(L']', i_);
                if (e != std::wstring_view::npos) {
                    index = std::wstring(s_.substr(i_ + 1, e - i_ - 1));
                    i_ = e + 1;
                }
            }
            const std::wstring arg = ConvertArgument(font);
            std::wstring root = L"√";
            if (index == L"3") root = L"∛";
            else if (index == L"4") root = L"∜";
            else if (!index.empty()) {
                std::wstring sup;
                if (ToScript(index, true, sup)) root = sup + L"√";
            }
            const bool single = arg.size() <= 1 || (arg.size() == 2 && arg[0] >= 0xD800 && arg[0] <= 0xDBFF);
            Emit(root + (single ? arg : L"(" + arg + L")"), flags);
            return;
        }
        if (name == L"binom" || name == L"dbinom" || name == L"tbinom") {
            const std::wstring n = ConvertArgument(font);
            const std::wstring k = ConvertArgument(font);
            Emit(L"(", flags);
            Emit(n, flags | kSup);
            Emit(k, flags | kSub);
            Emit(L")", flags);
            return;
        }
        static const struct {
            const wchar_t* name;
            Font font;
        } kFonts[] = {
            {L"text", Font::Upright}, {L"textrm", Font::Upright}, {L"mathrm", Font::Upright},
            {L"operatorname", Font::Upright}, {L"textnormal", Font::Upright}, {L"mbox", Font::Upright},
            {L"mathbf", Font::Bold}, {L"textbf", Font::Bold}, {L"boldsymbol", Font::BoldItalic},
            {L"bm", Font::BoldItalic}, {L"mathbb", Font::BlackBoard}, {L"mathcal", Font::Script},
            {L"mathscr", Font::Script}, {L"mathfrak", Font::Fraktur}, {L"mathsf", Font::Sans},
            {L"textsf", Font::Sans}, {L"mathtt", Font::Mono}, {L"texttt", Font::Mono}, {L"mathit", Font::Italic},
            {L"textit", Font::Italic},
        };
        for (const auto& f : kFonts) {
            if (name == f.name) {
                const bool is_text = name.rfind(L"text", 0) == 0 || name == L"mbox";
                if (is_text) {
                    // \text{...}: literal text, spaces preserved.
                    const std::wstring_view raw = RawArgument();
                    std::wstring t;
                    for (wchar_t c : raw) {
                        if (f.font == Font::Upright || f.font == Font::Italic) t.push_back(c);
                        else AppendStyled(t, c, f.font);
                    }
                    Emit(t, flags);
                } else {
                    const std::wstring_view raw = RawArgument();
                    Converter sub(raw, display_, depth_ + 1);
                    sub.ParseSeq(false, flags, f.font);
                    for (auto& p : sub.pieces_) Emit(p.text, p.flags);
                }
                return;
            }
        }
        static const struct {
            const wchar_t* name;
            wchar_t mark;
        } kAccents[] = {
            {L"vec", 0x20D7}, {L"hat", 0x0302}, {L"widehat", 0x0302}, {L"bar", 0x0304}, {L"overline", 0x0305},
            {L"dot", 0x0307}, {L"ddot", 0x0308}, {L"tilde", 0x0303}, {L"widetilde", 0x0303}, {L"check", 0x030C},
            {L"acute", 0x0301}, {L"grave", 0x0300}, {L"breve", 0x0306}, {L"underline", 0x0332},
            {L"overrightarrow", 0x20D7},
        };
        for (const auto& a : kAccents) {
            if (name == a.name) {
                const std::wstring arg = ConvertArgument(font);
                std::wstring r;
                const bool every = name == L"overline" || name == L"underline" || arg.size() > 2;
                for (size_t k = 0; k < arg.size(); ++k) {
                    r.push_back(arg[k]);
                    const bool high = arg[k] >= 0xD800 && arg[k] <= 0xDBFF;
                    if (high && k + 1 < arg.size()) r.push_back(arg[++k]);
                    if (every || k + 1 == arg.size()) r.push_back(a.mark);
                }
                Emit(r, flags);
                return;
            }
        }
        if (name == L"begin") {
            const std::wstring_view env = RawArgument();
            if (env == L"array") RawArgument();  // column spec
            Environment(std::wstring(env), flags);
            return;
        }
        if (name == L"end") {
            RawArgument();
            return;
        }
        if (name == L"left" || name == L"right" || name == L"big" || name == L"Big" || name == L"bigg" ||
            name == L"Bigg" || name == L"bigl" || name == L"bigr" || name == L"Bigl" || name == L"Bigr" ||
            name == L"biggl" || name == L"biggr" || name == L"middle" || name == L"displaystyle" ||
            name == L"textstyle" || name == L"scriptstyle" || name == L"limits" || name == L"nolimits" ||
            name == L"nonumber" || name == L"notag") {
            SkipSpaces();
            if (!AtEnd() && s_[i_] == L'.') ++i_;  // \left. = no delimiter
            return;
        }
        if (name == L"," || name == L":" || name == L">" || name == L";") {
            Emit(L" ", flags);
            return;
        }
        if (name == L"quad") { Emit(L" ", flags); return; }
        if (name == L"qquad") { Emit(L"  ", flags); return; }
        if (name == L" " || name == L"~") { Emit(L" ", flags); return; }
        if (name == L"!") return;
        if (name == L"\\") { Emit(display_ ? L"\n" : L" ", flags); return; }
        if (name == L"{" || name == L"}" || name == L"%" || name == L"$" || name == L"#" || name == L"&" ||
            name == L"_") {
            Emit(name, flags);
            return;
        }
        if (name == L"not") {
            // \not\in etc.: combining long solidus overlay.
            SkipSpaces();
            Converter next(RawArgument(), display_, depth_ + 1);
            next.ParseSeq(false, flags, font);
            for (auto& p : next.pieces_) Emit(p.text + L"̸", p.flags);
            return;
        }
        if (name == L"pmod") {
            Emit(L" (mod ", flags);
            Emit(ConvertArgument(font), flags);
            Emit(L")", flags);
            return;
        }
        if (name == L"bmod" || name == L"mod") {
            Emit(L" mod ", flags);
            return;
        }
        if (IsFunction(name)) {
            Emit(name, flags);
            // A thin space separates the operator from a following argument.
            SkipSpaces();
            if (!AtEnd() && s_[i_] != L'^' && s_[i_] != L'_' && s_[i_] != L'(' && s_[i_] != L'}') {
                Emit(L" ", flags);
            }
            return;
        }
        if (const wchar_t* sym = LookupSymbol(name)) {
            Emit(sym, flags);
            return;
        }
        // Unknown command: show its name upright so nothing silently vanishes.
        Emit(name, flags);
    }

    void ParseSeq(bool until_brace, uint32_t flags, Font font) {
        if (depth_ + nest_ > 40) {  // absurd nesting: show the rest verbatim
            Emit(s_.substr(i_), flags);
            i_ = s_.size();
            return;
        }
        while (!AtEnd()) {
            const wchar_t c = s_[i_];
            if (c == L'}') {
                ++i_;
                if (until_brace) return;
                continue;
            }
            if (c == L'{') {
                ++i_;
                ++nest_;
                ParseSeq(true, flags, font);
                --nest_;
                continue;
            }
            if (c == L'^' || c == L'_') {
                ++i_;
                Script(c == L'^', flags, font);
                continue;
            }
            if (c == L'\\') {
                ++i_;
                Command(ReadCommandName(), flags, font);
                continue;
            }
            if (c == L'&') {
                Emit(L"  ", flags);
                ++i_;
                continue;
            }
            if (IsSpace(c)) {
                ++i_;
                continue;  // spaces are insignificant in math mode
            }
            if (c == L'\'') {
                Emit(L"′", flags);
                ++i_;
                continue;
            }
            std::wstring t;
            if (c == L'-') t = L"−";
            else if (c == L'*') t = L"∗";
            else if (c == L'=' || c == L'<' || c == L'>' || c == L'+') t = std::wstring(L" ") + c + L" ";
            else if (c == L',') t = L", ";
            else AppendStyled(t, c, font);
            if (c == L'-' && !pieces_.empty() && !(flags & (kSup | kSub))) {
                // Binary minus gets spaces, unary minus doesn't.
                const std::wstring& prev = pieces_.back().text;
                const wchar_t p = prev.empty() ? L' ' : prev.back();
                if (p != L' ' && p != L'(' && p != L'[' && p != L'{' && p != L'=' && p != L'\n') t = L" − ";
            }
            if ((flags & (kSup | kSub)) && t.size() == 3 && t[0] == L' ') t = t.substr(1, 1);
            Emit(t, flags);
            ++i_;
            if (c >= 0xD800 && c <= 0xDBFF && !AtEnd()) {
                pieces_.back().text.push_back(s_[i_]);
                ++i_;
            }
        }
    }

    std::wstring_view s_;
    size_t i_ = 0;
    bool display_;
    int depth_ = 0;   // nesting of sub-converters
    int nest_ = 0;    // brace nesting inside this converter
    std::vector<MathPiece> pieces_;
};

}  // namespace

void TexToUnicode(std::wstring_view tex, bool display, std::vector<MathPiece>& out) {
    if (tex.size() > 20000) {  // pathological input: show as-is
        out.push_back(MathPiece{std::wstring(tex), 0});
        return;
    }
    Converter conv(tex, display);
    conv.Run(out);
}

}  // namespace mdv
