#include "core/texmath.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <cwchar>

#include "core/util.h"

namespace mdv {
namespace {

constexpr int kMaxDepth = 40;          // nesting beyond this is shown as source
constexpr size_t kMaxSource = 20000;   // longer formulas are shown as source

// ---------------------------------------------------------------------------
// Symbols
// ---------------------------------------------------------------------------

// Class codes: 0 from the character, o Ord, L large operator, b Bin, r Rel,
// ( Open, ) Close, p Punct, i Inner.
struct Symbol {
    const wchar_t* name;
    const wchar_t* text;
    char cls;
};

constexpr Symbol kSymbols[] = {
    // Greek (lowercase italic, uppercase upright unless an alphabet says otherwise)
    {L"alpha", L"α", 0}, {L"beta", L"β", 0}, {L"gamma", L"γ", 0}, {L"delta", L"δ", 0},
    {L"epsilon", L"ϵ", 0}, {L"varepsilon", L"ε", 0}, {L"zeta", L"ζ", 0}, {L"eta", L"η", 0},
    {L"theta", L"θ", 0}, {L"vartheta", L"ϑ", 0}, {L"iota", L"ι", 0}, {L"kappa", L"κ", 0},
    {L"varkappa", L"ϰ", 0}, {L"lambda", L"λ", 0}, {L"mu", L"μ", 0}, {L"nu", L"ν", 0}, {L"xi", L"ξ", 0},
    {L"omicron", L"ο", 0}, {L"pi", L"π", 0}, {L"varpi", L"ϖ", 0}, {L"rho", L"ρ", 0}, {L"varrho", L"ϱ", 0},
    {L"sigma", L"σ", 0}, {L"varsigma", L"ς", 0}, {L"tau", L"τ", 0}, {L"upsilon", L"υ", 0},
    {L"phi", L"ϕ", 0}, {L"varphi", L"φ", 0}, {L"chi", L"χ", 0}, {L"psi", L"ψ", 0}, {L"omega", L"ω", 0},
    {L"digamma", L"ϝ", 0}, {L"Alpha", L"Α", 0}, {L"Beta", L"Β", 0}, {L"Gamma", L"Γ", 0},
    {L"Delta", L"Δ", 0}, {L"Epsilon", L"Ε", 0}, {L"Zeta", L"Ζ", 0}, {L"Eta", L"Η", 0}, {L"Theta", L"Θ", 0},
    {L"Iota", L"Ι", 0}, {L"Kappa", L"Κ", 0}, {L"Lambda", L"Λ", 0}, {L"Mu", L"Μ", 0}, {L"Nu", L"Ν", 0},
    {L"Xi", L"Ξ", 0}, {L"Omicron", L"Ο", 0}, {L"Pi", L"Π", 0}, {L"Rho", L"Ρ", 0}, {L"Sigma", L"Σ", 0},
    {L"Tau", L"Τ", 0}, {L"Upsilon", L"Υ", 0}, {L"Phi", L"Φ", 0}, {L"Chi", L"Χ", 0}, {L"Psi", L"Ψ", 0},
    {L"Omega", L"Ω", 0}, {L"varGamma", L"𝛤", 0}, {L"varDelta", L"𝛥", 0}, {L"varTheta", L"𝛩", 0},
    {L"varLambda", L"𝛬", 0}, {L"varXi", L"𝛯", 0}, {L"varPi", L"𝛱", 0}, {L"varSigma", L"𝛴", 0},
    {L"varUpsilon", L"𝛶", 0}, {L"varPhi", L"𝛷", 0}, {L"varPsi", L"𝛹", 0}, {L"varOmega", L"𝛺", 0},
    // Letter-like and miscellaneous
    {L"aleph", L"ℵ", 0}, {L"beth", L"ℶ", 0}, {L"gimel", L"ℷ", 0}, {L"daleth", L"ℸ", 0}, {L"hbar", L"ℏ", 0},
    {L"hslash", L"ℏ", 0}, {L"ell", L"ℓ", 0}, {L"wp", L"℘", 0}, {L"Re", L"ℜ", 0}, {L"Im", L"ℑ", 0},
    {L"imath", L"𝚤", 0}, {L"jmath", L"𝚥", 0}, {L"partial", L"∂", 0}, {L"nabla", L"∇", 0},
    {L"infty", L"∞", 0}, {L"emptyset", L"∅", 0}, {L"varnothing", L"∅", 0}, {L"forall", L"∀", 0},
    {L"exists", L"∃", 0}, {L"nexists", L"∄", 0}, {L"neg", L"¬", 0}, {L"lnot", L"¬", 0}, {L"top", L"⊤", 0},
    {L"bot", L"⊥", 0}, {L"angle", L"∠", 0}, {L"measuredangle", L"∡", 0}, {L"sphericalangle", L"∢", 0},
    {L"triangle", L"△", 'o'}, {L"square", L"□", 0}, {L"Box", L"□", 0}, {L"blacksquare", L"■", 0},
    {L"Diamond", L"◇", 0}, {L"lozenge", L"◊", 0}, {L"blacklozenge", L"⧫", 0}, {L"bigstar", L"★", 0},
    {L"clubsuit", L"♣", 0}, {L"diamondsuit", L"♢", 0}, {L"heartsuit", L"♡", 0}, {L"spadesuit", L"♠", 0},
    {L"flat", L"♭", 0}, {L"natural", L"♮", 0}, {L"sharp", L"♯", 0}, {L"prime", L"′", 0},
    {L"backprime", L"‵", 0}, {L"degree", L"°", 0}, {L"surd", L"√", 0}, {L"mho", L"℧", 0},
    {L"eth", L"ð", 0}, {L"complement", L"∁", 0}, {L"Finv", L"Ⅎ", 0}, {L"Game", L"⅁", 0},
    {L"checkmark", L"✓", 0}, {L"S", L"§", 0}, {L"P", L"¶", 0}, {L"copyright", L"©", 0},
    {L"pounds", L"£", 0}, {L"yen", L"¥", 0}, {L"euro", L"€", 0}, {L"circledR", L"®", 0},
    {L"maltese", L"✠", 0}, {L"diagup", L"╱", 0}, {L"diagdown", L"╲", 0}, {L"backslash", L"∖", 'o'},
    {L"vert", L"|", 'o'}, {L"Vert", L"‖", 'o'}, {L"|", L"‖", 'o'}, {L"lvert", L"|", '('},
    {L"rvert", L"|", ')'}, {L"lVert", L"‖", '('}, {L"rVert", L"‖", ')'},
    // Dots
    {L"ldots", L"…", 'i'}, {L"dots", L"…", 'i'}, {L"dotsc", L"…", 'i'}, {L"dotso", L"…", 'i'},
    {L"cdots", L"⋯", 'i'}, {L"dotsb", L"⋯", 'i'}, {L"dotsm", L"⋯", 'i'}, {L"dotsi", L"⋯", 'i'},
    {L"vdots", L"⋮", 'o'}, {L"ddots", L"⋱", 'i'}, {L"iddots", L"⋰", 'i'},
    // Delimiters
    {L"langle", L"⟨", '('}, {L"rangle", L"⟩", ')'}, {L"lceil", L"⌈", '('}, {L"rceil", L"⌉", ')'},
    {L"lfloor", L"⌊", '('}, {L"rfloor", L"⌋", ')'}, {L"lbrace", L"{", '('}, {L"rbrace", L"}", ')'},
    {L"lbrack", L"[", '('}, {L"rbrack", L"]", ')'}, {L"lgroup", L"⟮", '('}, {L"rgroup", L"⟯", ')'},
    {L"llbracket", L"⟦", '('}, {L"rrbracket", L"⟧", ')'}, {L"{", L"{", '('}, {L"}", L"}", ')'},
    // Punctuation (\colon is handled with its spacing)
    {L"ldotp", L".", 'p'}, {L"cdotp", L"⋅", 'p'},
    // Binary operators
    {L"pm", L"±", 'b'}, {L"mp", L"∓", 'b'}, {L"times", L"×", 'b'}, {L"div", L"÷", 'b'}, {L"cdot", L"⋅", 'b'},
    {L"centerdot", L"⋅", 'b'}, {L"ast", L"∗", 'b'}, {L"star", L"⋆", 'b'}, {L"circ", L"∘", 'b'},
    {L"bullet", L"∙", 'b'}, {L"cap", L"∩", 'b'}, {L"cup", L"∪", 'b'}, {L"Cap", L"⋒", 'b'}, {L"Cup", L"⋓", 'b'},
    {L"uplus", L"⊎", 'b'}, {L"sqcap", L"⊓", 'b'}, {L"sqcup", L"⊔", 'b'}, {L"vee", L"∨", 'b'},
    {L"lor", L"∨", 'b'}, {L"wedge", L"∧", 'b'}, {L"land", L"∧", 'b'}, {L"setminus", L"∖", 'b'},
    {L"smallsetminus", L"∖", 'b'}, {L"wr", L"≀", 'b'}, {L"diamond", L"⋄", 'b'},
    {L"bigtriangleup", L"△", 'b'}, {L"bigtriangledown", L"▽", 'b'}, {L"triangleleft", L"◁", 'b'},
    {L"triangleright", L"▷", 'b'}, {L"oplus", L"⊕", 'b'}, {L"ominus", L"⊖", 'b'}, {L"otimes", L"⊗", 'b'},
    {L"oslash", L"⊘", 'b'}, {L"odot", L"⊙", 'b'}, {L"bigcirc", L"◯", 'b'}, {L"dagger", L"†", 'b'},
    {L"dag", L"†", 'b'}, {L"ddagger", L"‡", 'b'}, {L"ddag", L"‡", 'b'}, {L"amalg", L"⨿", 'b'},
    {L"dotplus", L"∔", 'b'}, {L"ltimes", L"⋉", 'b'}, {L"rtimes", L"⋊", 'b'}, {L"leftthreetimes", L"⋋", 'b'},
    {L"rightthreetimes", L"⋌", 'b'}, {L"curlywedge", L"⋏", 'b'}, {L"curlyvee", L"⋎", 'b'},
    {L"barwedge", L"⌅", 'b'}, {L"veebar", L"⊻", 'b'}, {L"boxplus", L"⊞", 'b'}, {L"boxminus", L"⊟", 'b'},
    {L"boxtimes", L"⊠", 'b'}, {L"boxdot", L"⊡", 'b'}, {L"circledast", L"⊛", 'b'},
    {L"circledcirc", L"⊚", 'b'}, {L"circleddash", L"⊝", 'b'}, {L"intercal", L"⊺", 'b'},
    {L"divideontimes", L"⋇", 'b'}, {L"lhd", L"⊲", 'b'}, {L"rhd", L"⊳", 'b'}, {L"unlhd", L"⊴", 'b'},
    {L"unrhd", L"⊵", 'b'},
    // Relations
    {L"leq", L"≤", 'r'}, {L"le", L"≤", 'r'}, {L"geq", L"≥", 'r'}, {L"ge", L"≥", 'r'}, {L"neq", L"≠", 'r'},
    {L"ne", L"≠", 'r'}, {L"leqslant", L"⩽", 'r'}, {L"geqslant", L"⩾", 'r'}, {L"leqq", L"≦", 'r'},
    {L"geqq", L"≧", 'r'}, {L"lesssim", L"≲", 'r'}, {L"gtrsim", L"≳", 'r'}, {L"ll", L"≪", 'r'},
    {L"gg", L"≫", 'r'}, {L"lll", L"⋘", 'r'}, {L"ggg", L"⋙", 'r'}, {L"nless", L"≮", 'r'}, {L"ngtr", L"≯", 'r'},
    {L"nleq", L"≰", 'r'}, {L"ngeq", L"≱", 'r'}, {L"approx", L"≈", 'r'}, {L"approxeq", L"≊", 'r'},
    {L"equiv", L"≡", 'r'}, {L"cong", L"≅", 'r'}, {L"ncong", L"≇", 'r'}, {L"sim", L"∼", 'r'},
    {L"thicksim", L"∼", 'r'}, {L"nsim", L"≁", 'r'}, {L"simeq", L"≃", 'r'}, {L"backsim", L"∽", 'r'},
    {L"asymp", L"≍", 'r'}, {L"doteq", L"≐", 'r'}, {L"doteqdot", L"≑", 'r'}, {L"triangleq", L"≜", 'r'},
    {L"coloneqq", L"≔", 'r'}, {L"eqqcolon", L"≕", 'r'}, {L"circeq", L"≗", 'r'}, {L"bumpeq", L"≏", 'r'},
    {L"Bumpeq", L"≎", 'r'}, {L"propto", L"∝", 'r'}, {L"varpropto", L"∝", 'r'}, {L"prec", L"≺", 'r'},
    {L"succ", L"≻", 'r'}, {L"preceq", L"⪯", 'r'}, {L"succeq", L"⪰", 'r'}, {L"nprec", L"⊀", 'r'},
    {L"nsucc", L"⊁", 'r'}, {L"subset", L"⊂", 'r'}, {L"supset", L"⊃", 'r'}, {L"subseteq", L"⊆", 'r'},
    {L"supseteq", L"⊇", 'r'}, {L"subsetneq", L"⊊", 'r'}, {L"supsetneq", L"⊋", 'r'},
    {L"nsubseteq", L"⊈", 'r'}, {L"nsupseteq", L"⊉", 'r'}, {L"sqsubset", L"⊏", 'r'}, {L"sqsupset", L"⊐", 'r'},
    {L"sqsubseteq", L"⊑", 'r'}, {L"sqsupseteq", L"⊒", 'r'}, {L"Subset", L"⋐", 'r'}, {L"Supset", L"⋑", 'r'},
    {L"in", L"∈", 'r'}, {L"notin", L"∉", 'r'}, {L"ni", L"∋", 'r'}, {L"owns", L"∋", 'r'},
    {L"vdash", L"⊢", 'r'}, {L"dashv", L"⊣", 'r'}, {L"models", L"⊨", 'r'}, {L"vDash", L"⊨", 'r'},
    {L"Vdash", L"⊩", 'r'}, {L"nvdash", L"⊬", 'r'}, {L"nvDash", L"⊭", 'r'}, {L"perp", L"⊥", 'r'},
    {L"mid", L"∣", 'r'}, {L"nmid", L"∤", 'r'}, {L"parallel", L"∥", 'r'}, {L"nparallel", L"∦", 'r'},
    {L"smile", L"⌣", 'r'}, {L"frown", L"⌢", 'r'}, {L"bowtie", L"⋈", 'r'}, {L"Join", L"⋈", 'r'},
    {L"vartriangleleft", L"⊲", 'r'}, {L"vartriangleright", L"⊳", 'r'}, {L"trianglelefteq", L"⊴", 'r'},
    {L"trianglerighteq", L"⊵", 'r'}, {L"therefore", L"∴", 'r'}, {L"because", L"∵", 'r'},
    // Arrows
    {L"leftarrow", L"←", 'r'}, {L"gets", L"←", 'r'}, {L"rightarrow", L"→", 'r'}, {L"to", L"→", 'r'},
    {L"leftrightarrow", L"↔", 'r'}, {L"Leftarrow", L"⇐", 'r'}, {L"Rightarrow", L"⇒", 'r'},
    {L"Leftrightarrow", L"⇔", 'r'}, {L"longleftarrow", L"⟵", 'r'}, {L"longrightarrow", L"⟶", 'r'},
    {L"longleftrightarrow", L"⟷", 'r'}, {L"Longleftarrow", L"⟸", 'r'}, {L"Longrightarrow", L"⟹", 'r'},
    {L"Longleftrightarrow", L"⟺", 'r'}, {L"mapsto", L"↦", 'r'}, {L"longmapsto", L"⟼", 'r'},
    {L"hookleftarrow", L"↩", 'r'}, {L"hookrightarrow", L"↪", 'r'}, {L"uparrow", L"↑", 'r'},
    {L"downarrow", L"↓", 'r'}, {L"updownarrow", L"↕", 'r'}, {L"Uparrow", L"⇑", 'r'},
    {L"Downarrow", L"⇓", 'r'}, {L"Updownarrow", L"⇕", 'r'}, {L"nearrow", L"↗", 'r'}, {L"searrow", L"↘", 'r'},
    {L"swarrow", L"↙", 'r'}, {L"nwarrow", L"↖", 'r'}, {L"leftharpoonup", L"↼", 'r'},
    {L"leftharpoondown", L"↽", 'r'}, {L"rightharpoonup", L"⇀", 'r'}, {L"rightharpoondown", L"⇁", 'r'},
    {L"rightleftharpoons", L"⇌", 'r'}, {L"leftrightharpoons", L"⇋", 'r'}, {L"leftleftarrows", L"⇇", 'r'},
    {L"rightrightarrows", L"⇉", 'r'}, {L"leftrightarrows", L"⇆", 'r'}, {L"rightleftarrows", L"⇄", 'r'},
    {L"twoheadrightarrow", L"↠", 'r'}, {L"twoheadleftarrow", L"↞", 'r'}, {L"rightarrowtail", L"↣", 'r'},
    {L"leftarrowtail", L"↢", 'r'}, {L"dashrightarrow", L"⇢", 'r'}, {L"dashleftarrow", L"⇠", 'r'},
    {L"leadsto", L"⇝", 'r'}, {L"rightsquigarrow", L"⇝", 'r'}, {L"curvearrowright", L"↷", 'r'},
    {L"curvearrowleft", L"↶", 'r'}, {L"circlearrowright", L"↻", 'r'}, {L"circlearrowleft", L"↺", 'r'},
    {L"nleftarrow", L"↚", 'r'}, {L"nrightarrow", L"↛", 'r'}, {L"nleftrightarrow", L"↮", 'r'},
    {L"nLeftarrow", L"⇍", 'r'}, {L"nRightarrow", L"⇏", 'r'}, {L"nLeftrightarrow", L"⇎", 'r'},
    {L"Lleftarrow", L"⇚", 'r'}, {L"Rrightarrow", L"⇛", 'r'}, {L"upuparrows", L"⇈", 'r'},
    {L"downdownarrows", L"⇊", 'r'}, {L"restriction", L"↾", 'r'}, {L"multimap", L"⊸", 'r'},
    // Big operators
    {L"sum", L"∑", 'L'}, {L"prod", L"∏", 'L'}, {L"coprod", L"∐", 'L'}, {L"int", L"∫", 'L'},
    {L"intop", L"∫", 'L'}, {L"iint", L"∬", 'L'}, {L"iiint", L"∭", 'L'}, {L"iiiint", L"⨌", 'L'},
    {L"oint", L"∮", 'L'}, {L"oiint", L"∯", 'L'}, {L"oiiint", L"∰", 'L'}, {L"bigcup", L"⋃", 'L'},
    {L"bigcap", L"⋂", 'L'}, {L"bigoplus", L"⨁", 'L'}, {L"bigotimes", L"⨂", 'L'}, {L"bigodot", L"⨀", 'L'},
    {L"biguplus", L"⨄", 'L'}, {L"bigsqcup", L"⨆", 'L'}, {L"bigvee", L"⋁", 'L'}, {L"bigwedge", L"⋀", 'L'},
    {L"smallint", L"∫", 'o'},
};

const Symbol* FindSymbol(std::wstring_view name) {
    static const std::vector<const Symbol*> sorted = [] {
        std::vector<const Symbol*> v;
        for (const Symbol& s : kSymbols) v.push_back(&s);
        std::sort(v.begin(), v.end(), [](const Symbol* a, const Symbol* b) { return wcscmp(a->name, b->name) < 0; });
        return v;
    }();
    auto it = std::lower_bound(sorted.begin(), sorted.end(), name,
                               [](const Symbol* s, std::wstring_view n) { return n.compare(s->name) > 0; });
    return it != sorted.end() && name == (*it)->name ? *it : nullptr;
}

// Operator names; `limits`: scripts go above/below in display style.
struct Function {
    const wchar_t* name;
    bool limits;
};
constexpr Function kFunctions[] = {
    {L"Pr", true}, {L"arccos", false}, {L"arcsin", false}, {L"arctan", false}, {L"arg", false},
    {L"cos", false}, {L"cosh", false}, {L"cot", false}, {L"coth", false}, {L"csc", false}, {L"deg", false},
    {L"det", true}, {L"dim", false}, {L"exp", false}, {L"gcd", true}, {L"hom", false}, {L"inf", true},
    {L"injlim", true}, {L"ker", false}, {L"lg", false}, {L"lim", true}, {L"liminf", true}, {L"limsup", true},
    {L"ln", false}, {L"log", false}, {L"max", true}, {L"min", true}, {L"projlim", true}, {L"sec", false},
    {L"sgn", false}, {L"sin", false}, {L"sinh", false}, {L"sup", true}, {L"tan", false}, {L"tanh", false},
    {L"argmax", true}, {L"argmin", true},
};

const Function* FindFunction(std::wstring_view name) {
    for (const Function& f : kFunctions) {
        if (name == f.name) return &f;
    }
    return nullptr;
}

bool InSet(uint32_t c, std::initializer_list<uint32_t> set) {
    return std::find(set.begin(), set.end(), c) != set.end();
}

bool IsLargeOperator(uint32_t c) {
    return c == 0x2211 || c == 0x220F || c == 0x2210 || (c >= 0x222B && c <= 0x2233) ||
           (c >= 0x22C0 && c <= 0x22C3) || (c >= 0x2A00 && c <= 0x2A1C);
}

bool IsIntegral(uint32_t c) { return (c >= 0x222B && c <= 0x2233) || (c >= 0x2A0B && c <= 0x2A1C); }

// The class TeX gives a character typed in math mode.
MathClass ClassOf(uint32_t c) {
    if (IsLargeOperator(c)) return MathClass::Op;
    if (InSet(c, {'+', 0x2212, 0x00B1, 0x2213, 0x00D7, 0x00F7, 0x22C5, 0x00B7, 0x2217, 0x2218, 0x2219, 0x2022,
                  0x2229, 0x222A, 0x228E, 0x2293, 0x2294, 0x2228, 0x2227, 0x2295, 0x2296, 0x2297, 0x2298, 0x2299,
                  0x25EF, 0x22C4, 0x22C6, 0x2020, 0x2021, 0x2240, 0x2A3F, 0x2216, 0x25BD, 0x25B3, 0x25C1, 0x25B7,
                  0x2214, 0x22C9, 0x22CA, 0x22BB, 0x229E, 0x229F, 0x22A0, 0x22A1})) {
        return MathClass::Bin;
    }
    if (InSet(c, {'=', '<', '>', ':', 0x2260, 0x2264, 0x2265, 0x2A7D, 0x2A7E, 0x2248, 0x2261, 0x2262, 0x2245,
                  0x223C, 0x2243, 0x224D, 0x2250, 0x221D, 0x227A, 0x227B, 0x2AAF, 0x2AB0, 0x226A, 0x226B, 0x2223,
                  0x2224, 0x2225, 0x2226, 0x2322, 0x2323, 0x22C8, 0x2254, 0x2255, 0x225C, 0x2A87, 0x2A88}) ||
        (c >= 0x2190 && c <= 0x21FF) || (c >= 0x27F0 && c <= 0x27FF) || (c >= 0x2900 && c <= 0x297F) ||
        (c >= 0x2208 && c <= 0x220D) || (c >= 0x2241 && c <= 0x224C) || (c >= 0x2251 && c <= 0x2253) ||
        (c >= 0x2266 && c <= 0x2281) || (c >= 0x2282 && c <= 0x228B) || (c >= 0x228F && c <= 0x2292) ||
        (c >= 0x22A2 && c <= 0x22A3) || (c >= 0x22A8 && c <= 0x22AF) || (c >= 0x22B2 && c <= 0x22B5) ||
        (c >= 0x22D0 && c <= 0x22D1) || (c >= 0x22DA && c <= 0x22E9)) {
        return MathClass::Rel;
    }
    if (InSet(c, {'(', '[', '{', 0x27E8, 0x2308, 0x230A, 0x27E6, 0x27EE, 0x2983})) return MathClass::Open;
    if (InSet(c, {')', ']', '}', 0x27E9, 0x2309, 0x230B, 0x27E7, 0x27EF, 0x2984, '!', '?'})) return MathClass::Close;
    if (c == ',' || c == ';') return MathClass::Punct;
    return MathClass::Ord;
}

MathClass ClassFromCode(char code, uint32_t c) {
    switch (code) {
    case 'o': return MathClass::Ord;
    case 'L': return MathClass::Op;
    case 'b': return MathClass::Bin;
    case 'r': return MathClass::Rel;
    case '(': return MathClass::Open;
    case ')': return MathClass::Close;
    case 'p': return MathClass::Punct;
    case 'i': return MathClass::Inner;
    default: return ClassOf(c);
    }
}

// ---------------------------------------------------------------------------
// Math alphabets
// ---------------------------------------------------------------------------

enum class Alphabet : uint8_t { Normal, Upright, Italic, Bold, BoldItalic, DoubleStruck, Script, Fraktur, Sans, Mono };

// Greek in the Mathematical Alphanumeric Symbols block: capitals (Α..Ω, ∇),
// small letters (α..ω) and ∂ ϵ ϑ ϰ ϕ ϱ ϖ, per style.
bool AppendGreek(std::wstring& out, uint32_t c, uint32_t capitals) {
    static const uint32_t kExtra[] = {0x2202, 0x03F5, 0x03D1, 0x03F0, 0x03D5, 0x03F1, 0x03D6};
    if (c >= 0x0391 && c <= 0x03A9 && c != 0x03A2) {
        AppendCodepoint(out, capitals + (c - 0x0391));
        return true;
    }
    if (c == 0x2207) {
        AppendCodepoint(out, capitals + 25);
        return true;
    }
    if (c >= 0x03B1 && c <= 0x03C9) {
        AppendCodepoint(out, capitals + 26 + (c - 0x03B1));
        return true;
    }
    for (size_t k = 0; k < std::size(kExtra); ++k) {
        if (c == kExtra[k]) {
            AppendCodepoint(out, capitals + 51 + static_cast<uint32_t>(k));
            return true;
        }
    }
    return false;
}

void AppendLatin(std::wstring& out, uint32_t c, uint32_t upper, uint32_t lower) {
    AppendCodepoint(out, (c >= 'A' && c <= 'Z') ? upper + (c - 'A') : lower + (c - 'a'));
}

// A character typed in math mode, in the current alphabet.
std::wstring Styled(uint32_t c, Alphabet a) {
    std::wstring out;
    const bool latin = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    const bool digit = c >= '0' && c <= '9';
    auto exception = [&](const wchar_t* from, const wchar_t* to) {
        for (int i = 0; from[i]; ++i) {
            if (static_cast<uint32_t>(from[i]) == c) {
                out.push_back(to[i]);
                return true;
            }
        }
        return false;
    };
    switch (a) {
    case Alphabet::Normal:
        // TeX: italic letters and small Greek, upright digits and capital Greek.
        if (c == 'h') out.push_back(0x210E);
        else if (latin) AppendLatin(out, c, 0x1D434, 0x1D44E);
        else if ((c >= 0x03B1 && c <= 0x03C9) || InSet(c, {0x2202, 0x03F5, 0x03D1, 0x03F0, 0x03D5, 0x03F1, 0x03D6})) {
            AppendGreek(out, c, 0x1D6E2);
        } else AppendCodepoint(out, c);
        return out;
    case Alphabet::Italic:
        if (c == 'h') out.push_back(0x210E);
        else if (latin) AppendLatin(out, c, 0x1D434, 0x1D44E);
        else if (!AppendGreek(out, c, 0x1D6E2)) AppendCodepoint(out, c);
        return out;
    case Alphabet::Bold:
        if (latin) AppendLatin(out, c, 0x1D400, 0x1D41A);
        else if (digit) AppendCodepoint(out, 0x1D7CE + (c - '0'));
        else if (!AppendGreek(out, c, 0x1D6A8)) AppendCodepoint(out, c);
        return out;
    case Alphabet::BoldItalic:
        // \boldsymbol: what is italic becomes bold italic, capital Greek bold upright.
        if (latin) AppendLatin(out, c, 0x1D468, 0x1D482);
        else if (digit) AppendCodepoint(out, 0x1D7CE + (c - '0'));
        else if ((c >= 0x0391 && c <= 0x03A9) || c == 0x2207) AppendGreek(out, c, 0x1D6A8);
        else if (!AppendGreek(out, c, 0x1D71C)) AppendCodepoint(out, c);
        return out;
    case Alphabet::DoubleStruck:
        if (exception(L"CHNPQRZ", L"ℂℍℕℙℚℝℤ")) return out;
        if (latin) AppendLatin(out, c, 0x1D538, 0x1D552);
        else if (digit) AppendCodepoint(out, 0x1D7D8 + (c - '0'));
        else AppendCodepoint(out, c);
        return out;
    case Alphabet::Script:
        if (exception(L"BEFHILMRego", L"ℬℰℱℋℐℒℳℛℯℊℴ")) return out;
        if (latin) AppendLatin(out, c, 0x1D49C, 0x1D4B6);
        else AppendCodepoint(out, c);
        return out;
    case Alphabet::Fraktur:
        if (exception(L"CHIRZ", L"ℭℌℑℜℨ")) return out;
        if (latin) AppendLatin(out, c, 0x1D504, 0x1D51E);
        else AppendCodepoint(out, c);
        return out;
    case Alphabet::Sans:
        if (latin) AppendLatin(out, c, 0x1D5A0, 0x1D5BA);
        else if (digit) AppendCodepoint(out, 0x1D7E2 + (c - '0'));
        else AppendCodepoint(out, c);
        return out;
    case Alphabet::Mono:
        if (latin) AppendLatin(out, c, 0x1D670, 0x1D68A);
        else if (digit) AppendCodepoint(out, 0x1D7F6 + (c - '0'));
        else AppendCodepoint(out, c);
        return out;
    default:
        AppendCodepoint(out, c);
        return out;
    }
}

bool IsGreekOrLatin(uint32_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           (c >= 0x0391 && c <= 0x03C9) || InSet(c, {0x2202, 0x2207, 0x03F5, 0x03D1, 0x03F0, 0x03D5, 0x03F1, 0x03D6});
}

// ---------------------------------------------------------------------------
// Colors (\color, \textcolor): CSS names, #rgb, #rrggbb and xcolor models
// ---------------------------------------------------------------------------

uint32_t ParseHex(std::wstring_view h) {
    uint32_t v = 0;
    for (wchar_t c : h) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return 0;
    }
    if (h.size() == 3) {
        v = ((v & 0xF00) << 12) | ((v & 0xF00) << 8) | ((v & 0x0F0) << 8) | ((v & 0x0F0) << 4) | ((v & 0x00F) << 4) |
            (v & 0x00F);
    } else if (h.size() != 6) {
        return 0;
    }
    return 0xFF000000u | v;
}

uint32_t ParseColor(std::wstring_view model, std::wstring_view spec) {
    spec = Trim(spec);
    auto numbers = [&](float* out, int n) {
        int got = 0;
        size_t p = 0;
        while (got < n && p < spec.size()) {
            wchar_t* end = nullptr;
            const std::wstring part(spec.substr(p));
            out[got++] = std::wcstof(part.c_str(), &end);
            if (end == part.c_str()) return false;  // not a number ("red" in \color[rgb]{red})
            p += static_cast<size_t>(end - part.c_str());
            while (p < spec.size() && (spec[p] == ',' || IsSpace(spec[p]))) ++p;
        }
        return got == n;
    };
    auto rgb = [](float r, float g, float b) {
        auto c = [](float v) {
            return std::isfinite(v) ? static_cast<uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f) : 0u;
        };
        return 0xFF000000u | (c(r) << 16) | (c(g) << 8) | c(b);
    };
    float v[3] = {};
    if (EqualsNoCase(model, L"HTML")) return ParseHex(spec);
    if (model == L"rgb") return numbers(v, 3) ? rgb(v[0], v[1], v[2]) : 0;
    if (model == L"RGB") return numbers(v, 3) ? rgb(v[0] / 255.f, v[1] / 255.f, v[2] / 255.f) : 0;
    if (EqualsNoCase(model, L"gray")) return numbers(v, 1) ? rgb(v[0], v[0], v[0]) : 0;
    if (!spec.empty() && spec[0] == '#') return ParseHex(spec.substr(1));
    static const struct {
        const wchar_t* name;
        uint32_t rgb;
    } kNames[] = {
        {L"black", 0x000000}, {L"white", 0xFFFFFF}, {L"red", 0xFF0000}, {L"green", 0x008000}, {L"blue", 0x0000FF},
        {L"cyan", 0x00FFFF}, {L"aqua", 0x00FFFF}, {L"magenta", 0xFF00FF}, {L"fuchsia", 0xFF00FF},
        {L"yellow", 0xFFFF00}, {L"gray", 0x808080}, {L"grey", 0x808080}, {L"darkgray", 0xA9A9A9},
        {L"lightgray", 0xD3D3D3}, {L"silver", 0xC0C0C0}, {L"orange", 0xFFA500}, {L"purple", 0x800080},
        {L"violet", 0xEE82EE}, {L"brown", 0xA52A2A}, {L"pink", 0xFFC0CB}, {L"lime", 0x00FF00},
        {L"olive", 0x808000}, {L"teal", 0x008080}, {L"navy", 0x000080}, {L"maroon", 0x800000},
        {L"gold", 0xFFD700}, {L"indigo", 0x4B0082}, {L"darkgreen", 0x006400}, {L"darkblue", 0x00008B},
        {L"darkred", 0x8B0000}, {L"orangered", 0xFF4500}, {L"royalblue", 0x4169E1}, {L"skyblue", 0x87CEEB},
        {L"forestgreen", 0x228B22}, {L"crimson", 0xDC143C}, {L"tomato", 0xFF6347}, {L"coral", 0xFF7F50},
        {L"salmon", 0xFA8072}, {L"turquoise", 0x40E0D0}, {L"goldenrod", 0xDAA520}, {L"limegreen", 0x32CD32},
        {L"deepskyblue", 0x00BFFF}, {L"dodgerblue", 0x1E90FF}, {L"steelblue", 0x4682B4},
    };
    for (const auto& n : kNames) {
        if (EqualsNoCase(spec, n.name)) return 0xFF000000u | n.rgb;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

MathNode MakeList(MathClass cls = MathClass::Ord) {
    MathNode n;
    n.kind = MathKind::List;
    n.cls = cls;
    return n;
}

MathNode MakeGlyphs(std::wstring text, MathClass cls) {
    MathNode n;
    n.kind = MathKind::Glyphs;
    n.cls = cls;
    n.text = std::move(text);
    return n;
}

MathNode MakeText(std::wstring text, MathClass cls = MathClass::Ord) {
    MathNode n;
    n.kind = MathKind::Text;
    n.cls = cls;
    n.text = std::move(text);
    return n;
}

MathNode MakeSpace(float mu) {
    MathNode n;
    n.kind = MathKind::Space;
    n.value = mu;
    return n;
}

class Parser {
public:
    explicit Parser(std::wstring_view s) : s_(s), scan_budget_(2 * s.size() + 64) {}

    MathNode Top() {
        // Rows separated by \\ outside environments (display math) stack
        // like `gathered`.
        std::vector<MathNode> rows;
        for (;;) {
            Stop stop = kEnd;
            rows.push_back(List(kModeTop, &stop));
            if (stop != kRow) break;
            RawOptional(nullptr, true);  // \\[4pt]
        }
        while (rows.size() > 1 && rows.back().kids.empty()) rows.pop_back();
        if (rows.size() == 1) return std::move(rows[0]);
        MathNode a;
        a.kind = MathKind::Array;
        a.env = MathEnv::Gathered;
        a.text = L"c";
        for (MathNode& r : rows) {
            MathNode row = MakeList();
            row.kids.push_back(std::move(r));
            a.kids.push_back(std::move(row));
        }
        MathNode top = MakeList();
        top.kids.push_back(std::move(a));
        return top;
    }

private:
    enum Stop : uint8_t { kEnd, kBrace, kCell, kRow, kEndEnv, kRight, kMiddle, kBracket };
    enum Mode : uint8_t { kModeTop, kModeGroup, kModeCell, kModeFenced, kModeOptional };

    bool AtEnd() const { return i_ >= s_.size(); }
    wchar_t Peek(size_t k = 0) const { return i_ + k < s_.size() ? s_[i_ + k] : 0; }

    void SkipSpaces() {
        while (!AtEnd()) {
            if (IsSpace(s_[i_])) {
                ++i_;
            } else if (s_[i_] == '%') {  // comment to the end of the line
                while (!AtEnd() && s_[i_] != '\n') ++i_;
            } else {
                break;
            }
        }
    }

    uint32_t ReadCodepoint() {
        uint32_t c = s_[i_++];
        if (c >= 0xD800 && c <= 0xDBFF && !AtEnd() && s_[i_] >= 0xDC00 && s_[i_] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s_[i_++] - 0xDC00);
        }
        return c;
    }

    // After '\': a name of letters, or one other character.
    std::wstring ReadName() {
        const size_t b = i_;
        auto letter = [](wchar_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
        if (!AtEnd() && letter(s_[i_])) {
            while (!AtEnd() && letter(s_[i_])) ++i_;
        } else if (!AtEnd()) {
            ReadCodepoint();
        }
        return std::wstring(s_.substr(b, i_ - b));
    }

    // Name of the command at i_ (a backslash), without consuming it.
    std::wstring PeekName() const {
        Parser probe(*this);
        ++probe.i_;
        return probe.ReadName();
    }

    // The source of a {…} group (or of one token), unparsed.
    std::wstring RawGroup() {
        SkipSpaces();
        if (AtEnd()) return {};
        if (s_[i_] != '{') {
            const size_t b = i_;
            if (s_[i_] == '\\') {
                ++i_;
                ReadName();
            } else {
                ReadCodepoint();
            }
            return std::wstring(s_.substr(b, i_ - b));
        }
        const size_t b = ++i_;
        int depth = 1;
        while (!AtEnd()) {
            if (s_[i_] == '\\' && i_ + 1 < s_.size()) {
                i_ += 2;
                continue;
            }
            if (s_[i_] == '{') ++depth;
            if (s_[i_] == '}' && --depth == 0) break;
            ++i_;
        }
        const std::wstring r(s_.substr(b, i_ - b));
        if (!AtEnd()) ++i_;
        return r;
    }

    // [...] after a command, raw. Nothing is consumed unless a matching ]
    // follows (and, for \\[4pt], unless it holds a dimension).
    bool RawOptional(std::wstring* out, bool dimension = false) {
        const size_t save = i_;
        SkipSpaces();
        if (Peek() != '[') {
            i_ = save;
            return false;
        }
        const size_t b = ++i_;
        int depth = 0;
        while (!AtEnd()) {
            // A failed scan is undone: bound them, or "\\[" repeated would
            // rescan the rest of the formula every time.
            if (scan_budget_ == 0 || (dimension && i_ - b > 32)) break;
            --scan_budget_;
            const wchar_t c = s_[i_];
            if (dimension && !(iswalnum(c) || c == '.' || c == '-' || c == '+' || c == ' ' || c == ']')) break;
            if (c == '\\' && i_ + 1 < s_.size()) {
                i_ += 2;
                continue;
            }
            if (c == '{') ++depth;
            else if (c == '}' && --depth < 0) break;
            else if (c == ']' && depth == 0) break;
            ++i_;
        }
        if (AtEnd() || s_[i_] != ']' || (dimension && !IsDimension(s_.substr(b, i_ - b)))) {
            i_ = save;
            return false;
        }
        if (out) *out = std::wstring(s_.substr(b, i_ - b));
        ++i_;
        return true;
    }

    static bool IsDimension(std::wstring_view d) {
        d = Trim(d);
        size_t k = 0;
        if (k < d.size() && (d[k] == '-' || d[k] == '+')) ++k;
        bool digits = false;
        while (k < d.size() && ((d[k] >= '0' && d[k] <= '9') || d[k] == '.')) {
            digits = digits || d[k] != '.';
            ++k;
        }
        d = Trim(d.substr(k));
        return digits && (d.empty() || (d.size() == 2 && iswlower(d[0]) && iswlower(d[1])));
    }

    void Push(MathNode& list, MathNode node) {
        if (!node.color) node.color = color_;
        list.kids.push_back(std::move(node));
    }

    // Everything else of the formula, shown as source (too deep / too long).
    void Bail(MathNode& list) {
        MathNode t = MakeText(std::wstring(s_.substr(i_)));
        t.error = true;
        Push(list, std::move(t));
        i_ = s_.size();
    }

    MathNode List(Mode mode, Stop* stop) {
        MathNode list = MakeList();
        *stop = kEnd;
        if (depth_ >= kMaxDepth) {
            Bail(list);
            return list;
        }
        ++depth_;
        if (mode == kModeGroup) ++groups_;
        if (mode == kModeCell) ++cells_;
        const Alphabet saved_alphabet = alphabet_;
        const uint32_t saved_color = color_;
        MathNode numerator;
        int infix = 0;  // \over, \atop, \choose
        while (!AtEnd()) {
            const wchar_t c = s_[i_];
            if (c == '}') {
                if (mode == kModeGroup) {
                    ++i_;
                    *stop = kBrace;
                    break;
                }
                if (groups_ > 0) break;  // closes an enclosing group
                ++i_;                    // stray
                continue;
            }
            if (c == ']' && mode == kModeOptional) {
                ++i_;
                *stop = kBracket;
                break;
            }
            if (c == '&') {
                if (mode == kModeCell) {
                    ++i_;
                    *stop = kCell;
                    break;
                }
                if (cells_ > 0 && mode != kModeGroup) break;
                ++i_;
                continue;
            }
            if (c == '\\' && Peek(1) == '\\') {
                if (mode == kModeCell || mode == kModeTop) {
                    i_ += 2;
                    *stop = kRow;
                    break;
                }
                if (cells_ > 0 && mode == kModeFenced) break;
                i_ += 2;
                continue;
            }
            if (c == '\\') {
                const std::wstring name = PeekName();
                if (name == L"end") {
                    if (mode == kModeCell) {
                        ++i_;
                        ReadName();
                        RawGroup();
                        *stop = kEndEnv;
                        break;
                    }
                    if (cells_ > 0 && mode == kModeFenced) break;
                }
                if ((name == L"right" || name == L"middle") && mode == kModeFenced) {
                    i_ += 1 + name.size();
                    *stop = name == L"right" ? kRight : kMiddle;
                    break;
                }
                if (name == L"over" || name == L"atop" || name == L"choose") {
                    i_ += 1 + name.size();
                    if (!infix) {
                        numerator = std::move(list);
                        list = MakeList();
                        infix = name == L"over" ? 1 : name == L"atop" ? 2 : 3;
                    }
                    continue;
                }
            }
            Step(list);
        }
        alphabet_ = saved_alphabet;
        color_ = saved_color;
        if (mode == kModeGroup) --groups_;
        if (mode == kModeCell) --cells_;
        --depth_;
        if (infix) {
            MathNode f;
            f.kind = MathKind::Frac;
            f.cls = MathClass::Inner;
            f.value = infix == 1 ? -1.f : 0.f;
            if (infix == 3) {
                f.left = '(';
                f.right = ')';
            }
            f.kids.push_back(std::move(numerator));
            f.kids.push_back(std::move(list));
            MathNode wrap = MakeList();
            wrap.kids.push_back(std::move(f));
            return wrap;
        }
        return list;
    }

    // {…} or a single token (a character or a command with its arguments).
    MathNode Argument() {
        SkipSpaces();
        MathNode list = MakeList();
        if (AtEnd()) return list;
        const wchar_t c = s_[i_];
        if (c == '{') {
            ++i_;
            Stop stop;
            return List(kModeGroup, &stop);
        }
        if (c == '}' || c == '&' || (c == '\\' && Peek(1) == '\\')) return list;
        if (c == '\\') {
            const std::wstring name = PeekName();
            if (name == L"end" || name == L"right" || name == L"middle") return list;  // not an argument
        }
        if (depth_ >= kMaxDepth) {
            Bail(list);
            return list;
        }
        // One token: a switch in it (\color, \rm) ends with it, as in braces.
        const Alphabet saved_alphabet = alphabet_;
        const uint32_t saved_color = color_;
        ++depth_;
        Step(list);
        --depth_;
        alphabet_ = saved_alphabet;
        color_ = saved_color;
        return list;
    }

    // One token of math: appends to `list`.
    void Step(MathNode& list) {
        const wchar_t c = s_[i_];
        if (IsSpace(c) || c == '%') {
            SkipSpaces();
            return;
        }
        if (c == '{') {
            ++i_;
            Stop stop;
            MathNode group = List(kModeGroup, &stop);
            Push(list, std::move(group));
            return;
        }
        if (c == '^' || c == '_') {
            ++i_;
            AddScript(list, c == '^', Argument());
            return;
        }
        if (c == '\'') {
            // x' is x^{\prime}: the prime is a superscript (layout draws it big).
            std::wstring primes;
            while (Peek() == '\'') {
                primes.push_back(0x2032);
                ++i_;
            }
            if (primes.size() == 2) primes = L"″";
            else if (primes.size() == 3) primes = L"‴";
            MathNode sup = MakeList();
            Push(sup, MakeGlyphs(primes, MathClass::Ord));
            AddScript(list, true, std::move(sup));
            return;
        }
        if (operator_name_ && (c == '-' || c == '*')) {  // text characters in \operatorname
            ++i_;
            Push(list, MakeGlyphs(c == '-' ? L"-" : L"*", MathClass::Ord));
            return;
        }
        if (c == '~') {
            ++i_;
            Push(list, MakeSpace(6));
            return;
        }
        if (c == '\\') {
            ++i_;
            Command(ReadName(), list);
            return;
        }
        if (c == '&' || c == '#' || c == '$') {
            ++i_;
            if (c == '#') Push(list, MakeGlyphs(L"#", MathClass::Ord));
            return;
        }
        const uint32_t cp = ReadCodepoint();
        uint32_t shown = cp;
        if (cp == '-') shown = 0x2212;
        else if (cp == '*') shown = 0x2217;
        MathNode g = MakeGlyphs(IsGreekOrLatin(cp) ? Styled(cp, alphabet_) : std::wstring(), ClassOf(shown));
        if (g.text.empty()) AppendCodepoint(g.text, shown);
        if (IsLargeOperator(cp)) {
            g.large = true;
            if (IsIntegral(cp)) g.limits = 2;
        }
        Push(list, std::move(g));
    }

    void AddScript(MathNode& list, bool sup, MathNode script) {
        if (list.kids.empty() || list.kids.back().kind == MathKind::Space || list.kids.back().kind == MathKind::Style) {
            list.kids.push_back(MakeList());
        }
        MathNode* last = &list.kids.back();
        const size_t slot = sup ? 2 : 1;
        if (sup && last->kind == MathKind::Scripts && (IsPrimes(last->kids[2]) || IsPrimes(script))) {
            // x'^2 = x^{\prime 2}: primes and a superscript share the slot.
            for (MathNode& k : script.kids) last->kids[2].kids.push_back(std::move(k));
            return;
        }
        if (last->kind == MathKind::Scripts && !last->kids[slot].kids.empty()) {
            // Double superscript: TeX complains; start over on an empty base.
            list.kids.push_back(MakeList());
            last = &list.kids.back();
        }
        if (last->kind != MathKind::Scripts) {
            MathNode s;
            s.kind = MathKind::Scripts;
            s.cls = last->cls;
            s.kids.push_back(std::move(*last));
            s.kids.push_back(MakeList());
            s.kids.push_back(MakeList());
            *last = std::move(s);
        }
        last->kids[slot] = std::move(script);
    }

    static MathNode* FirstGlyphs(MathNode& n) {
        for (MathNode& k : n.kids) {
            if (k.kind == MathKind::Glyphs && !k.text.empty()) return &k;
            if (k.kind == MathKind::List) {
                if (MathNode* g = FirstGlyphs(k)) return g;
            }
        }
        return nullptr;
    }

    static bool IsPrimes(const MathNode& list) {
        if (list.kids.empty()) return false;
        for (const MathNode& k : list.kids) {
            if (k.kind != MathKind::Glyphs || k.text.empty() || k.text[0] < 0x2032 || k.text[0] > 0x2034) return false;
        }
        return true;
    }

    // Delimiter after \left, \right, \middle, \big…: its character, 0 for none.
    wchar_t Delimiter() {
        SkipSpaces();
        if (AtEnd()) return 0;
        const wchar_t c = s_[i_];
        if (c == '.') {
            ++i_;
            return 0;
        }
        if (wcschr(L"()[]|/", c)) {
            ++i_;
            return c;
        }
        if (c == '<' || c == '>') {
            ++i_;
            return c == '<' ? 0x27E8 : 0x27E9;
        }
        if (c == '\\') {
            const size_t save = i_;
            ++i_;
            const std::wstring name = ReadName();
            static const struct {
                const wchar_t* name;
                wchar_t c;
            } kDelims[] = {
                {L"{", '{'}, {L"}", '}'}, {L"|", 0x2016}, {L"lbrace", '{'}, {L"rbrace", '}'}, {L"lbrack", '['},
                {L"rbrack", ']'}, {L"langle", 0x27E8}, {L"rangle", 0x27E9}, {L"vert", '|'}, {L"lvert", '|'},
                {L"rvert", '|'}, {L"Vert", 0x2016}, {L"lVert", 0x2016}, {L"rVert", 0x2016}, {L"lfloor", 0x230A},
                {L"rfloor", 0x230B}, {L"lceil", 0x2308}, {L"rceil", 0x2309}, {L"backslash", '\\'},
                {L"uparrow", 0x2191}, {L"downarrow", 0x2193}, {L"updownarrow", 0x2195}, {L"Uparrow", 0x21D1},
                {L"Downarrow", 0x21D3}, {L"Updownarrow", 0x21D5}, {L"lgroup", 0x27EE}, {L"rgroup", 0x27EF},
                {L"llbracket", 0x27E6}, {L"rrbracket", 0x27E7}, {L"mid", 0x2223},
            };
            for (const auto& d : kDelims) {
                if (name == d.name) return d.c;
            }
            i_ = save;  // not a delimiter: leave it to the list
        }
        return 0;
    }

    MathNode Fenced() {
        MathNode f;
        f.kind = MathKind::Fenced;
        f.cls = MathClass::Inner;
        f.text.push_back(Delimiter());
        for (;;) {
            Stop stop = kEnd;
            f.kids.push_back(List(kModeFenced, &stop));
            if (stop == kMiddle) {
                f.text.push_back(Delimiter());
                continue;
            }
            f.text.push_back(stop == kRight ? Delimiter() : 0);
            break;
        }
        return f;
    }

    MathNode Environment(const std::wstring& name) {
        MathNode a;
        a.kind = MathKind::Array;
        std::wstring align = L"c";
        if (name == L"matrix") {
        } else if (name == L"pmatrix") {
            a.left = '(', a.right = ')';
        } else if (name == L"bmatrix") {
            a.left = '[', a.right = ']';
        } else if (name == L"Bmatrix") {
            a.left = '{', a.right = '}';
        } else if (name == L"vmatrix") {
            a.left = '|', a.right = '|';
        } else if (name == L"Vmatrix") {
            a.left = 0x2016, a.right = 0x2016;
        } else if (name == L"smallmatrix" || name == L"subarray" || name == L"substack") {
            a.env = MathEnv::Small;
            if (name == L"subarray") align = RawGroup();
        } else if (name == L"cases" || name == L"dcases" || name == L"rcases") {
            a.env = MathEnv::Cases;
            align = L"ll";
            (name == L"rcases" ? a.right : a.left) = name == L"rcases" ? '}' : '{';
            if (name == L"dcases") a.style = 0;
        } else if (name == L"aligned" || name == L"align" || name == L"align*" || name == L"split" ||
                   name == L"alignat" || name == L"alignat*" || name == L"alignedat" || name == L"flalign" ||
                   name == L"flalign*" || name == L"eqnarray" || name == L"eqnarray*") {
            a.env = MathEnv::Aligned;
            align = L"rl";
            if (name.rfind(L"alignat", 0) == 0 || name == L"alignedat") RawGroup();
        } else if (name == L"gathered" || name == L"gather" || name == L"gather*" || name == L"multline" ||
                   name == L"multline*" || name == L"equation" || name == L"equation*" || name == L"displaymath") {
            a.env = MathEnv::Gathered;
        } else if (name == L"array" || name == L"darray") {
            a.env = MathEnv::Array;
            align.clear();
            for (wchar_t c : RawGroup()) {
                if (c == 'l' || c == 'c' || c == 'r') align.push_back(c);
            }
            if (align.empty()) align = L"c";
        }
        a.text = align;
        for (;;) {
            MathNode row = MakeList();
            Stop stop = kEnd;
            for (;;) {
                row.kids.push_back(List(kModeCell, &stop));
                if (stop != kCell) break;
            }
            a.kids.push_back(std::move(row));
            if (stop != kRow) break;
            RawOptional(nullptr, true);  // \\[4pt]
        }
        // A trailing \\ leaves an empty row.
        if (a.kids.size() > 1 && a.kids.back().kids.size() == 1 && a.kids.back().kids[0].kids.empty()) {
            a.kids.pop_back();
        }
        a.cls = a.left || a.right ? MathClass::Inner : MathClass::Ord;
        return a;
    }

    // \text{...}: upright text; $…$ inside switches back to math.
    MathNode TextArgument(Alphabet alphabet) {
        const std::wstring raw = RawGroup();
        MathNode list = MakeList();
        if (depth_ >= kMaxDepth) {
            Push(list, MakeText(raw));
            return list;
        }
        std::wstring text;
        auto flush = [&] {
            if (!text.empty()) Push(list, MakeText(std::move(text)));
            text.clear();
        };
        bool space = false;
        for (size_t k = 0; k < raw.size();) {
            wchar_t c = raw[k];
            if (IsSpace(c)) {
                space = true;
                ++k;
                continue;
            }
            if (space) text.push_back(' ');
            space = false;
            if (c == '$') {
                const size_t e = raw.find('$', k + 1);
                if (e != std::wstring::npos) {
                    flush();
                    Parser sub(std::wstring_view(raw).substr(k + 1, e - k - 1));
                    sub.depth_ = depth_ + 1;
                    sub.color_ = color_;
                    MathNode m = sub.Top();
                    for (MathNode& n : m.kids) Push(list, std::move(n));
                    k = e + 1;
                    continue;
                }
            }
            if (c == '\\' && k + 1 < raw.size()) {
                const wchar_t n = raw[k + 1];
                if (wcschr(L"{}$%&_#", n)) {
                    text.push_back(n);
                    k += 2;
                    continue;
                }
                if (n == ' ' || n == ',' || n == ';' || n == '\\') {
                    text.push_back(n == ',' ? 0x2009 : ' ');
                    k += 2;
                    continue;
                }
                size_t e = k + 1;
                while (e < raw.size() && ((raw[e] >= 'a' && raw[e] <= 'z') || (raw[e] >= 'A' && raw[e] <= 'Z'))) ++e;
                const std::wstring name = raw.substr(k + 1, e - k - 1);
                static const struct {
                    const wchar_t* name;
                    Alphabet alphabet;
                } kTextFonts[] = {{L"textbf", Alphabet::Bold}, {L"textit", Alphabet::Italic},
                                  {L"emph", Alphabet::Italic}, {L"textrm", Alphabet::Upright},
                                  {L"textsf", Alphabet::Sans},  {L"texttt", Alphabet::Mono},
                                  {L"text", Alphabet::Upright}, {L"textnormal", Alphabet::Upright}};
                bool handled = false;
                for (const auto& f : kTextFonts) {
                    if (name == f.name) {
                        flush();
                        Parser sub(std::wstring_view(raw).substr(e));
                        sub.depth_ = depth_ + 1;
                        sub.color_ = color_;
                        MathNode inner = sub.TextArgument(f.alphabet);
                        for (MathNode& node : inner.kids) Push(list, std::move(node));
                        k = e + sub.i_;
                        handled = true;
                        break;
                    }
                }
                if (handled) continue;
                text.append(raw, k, e - k);
                k = e;
                continue;
            }
            if (c == '{' || c == '}') {
                ++k;
                continue;
            }
            if (c == '~') c = 0x00A0;
            size_t len = (c >= 0xD800 && c <= 0xDBFF && k + 1 < raw.size()) ? 2 : 1;
            if (alphabet != Alphabet::Upright && len == 1 && IsGreekOrLatin(c)) text += Styled(c, alphabet);
            else text.append(raw, k, len);
            k += len;
        }
        if (space) text.push_back(' ');
        flush();
        return list;
    }

    // Puts `node` on the list as an atom; an argument list with one item
    // becomes that item. (Not a space or a style switch: only a list lays
    // those out, alone they would have no width.)
    static MathNode Single(MathNode list) {
        if (list.kind == MathKind::List && list.kids.size() == 1 && !list.color &&
            list.kids[0].kind != MathKind::Space && list.kids[0].kind != MathKind::Style) {
            return std::move(list.kids[0]);
        }
        return list;
    }

    void SetLimits(MathNode& list, uint8_t limits) {
        if (list.kids.empty()) return;
        MathNode* n = &list.kids.back();
        if (n->kind == MathKind::Scripts) n = &n->kids[0];
        if (n->cls == MathClass::Op) n->limits = limits;
    }

    void Command(const std::wstring& name, MathNode& list) {
        // Spacing.
        static const struct {
            const wchar_t* name;
            float mu;
        } kSpaces[] = {
            {L",", 3}, {L":", 4}, {L">", 4}, {L";", 5}, {L"!", -3}, {L" ", 6}, {L"quad", 18}, {L"qquad", 36},
            {L"thinspace", 3}, {L"medspace", 4}, {L"thickspace", 5}, {L"enspace", 9}, {L"enskip", 9},
            {L"negthinspace", -3}, {L"negmedspace", -4}, {L"negthickspace", -5}, {L"space", 6}, {L"nobreakspace", 6},
        };
        for (const auto& sp : kSpaces) {
            if (name == sp.name) {
                Push(list, MakeSpace(sp.mu));
                return;
            }
        }
        if (name == L"hspace" || name == L"hskip" || name == L"kern" || name == L"mkern" || name == L"mskip") {
            if (Peek() == '*') ++i_;
            std::wstring dim = name == L"hspace" ? RawGroup() : ReadDimension();
            Push(list, MakeSpace(DimensionMu(dim)));
            return;
        }
        // Styles.
        static const wchar_t* kStyles[] = {L"displaystyle", L"textstyle", L"scriptstyle", L"scriptscriptstyle"};
        for (int k = 0; k < 4; ++k) {
            if (name == kStyles[k]) {
                MathNode s;
                s.kind = MathKind::Style;
                s.value = static_cast<float>(k);
                Push(list, std::move(s));
                return;
            }
        }
        // Old-style alphabet switches, for the rest of the group.
        static const struct {
            const wchar_t* name;
            Alphabet alphabet;
        } kSwitches[] = {{L"rm", Alphabet::Upright}, {L"bf", Alphabet::Bold}, {L"it", Alphabet::Italic},
                         {L"cal", Alphabet::Script}, {L"sf", Alphabet::Sans},  {L"tt", Alphabet::Mono},
                         {L"mit", Alphabet::Italic}};
        for (const auto& sw : kSwitches) {
            if (name == sw.name) {
                alphabet_ = sw.alphabet;
                return;
            }
        }
        static const struct {
            const wchar_t* name;
            Alphabet alphabet;
        } kFonts[] = {
            {L"mathrm", Alphabet::Upright}, {L"mathup", Alphabet::Upright}, {L"mathit", Alphabet::Italic},
            {L"mathnormal", Alphabet::Normal}, {L"mathbf", Alphabet::Bold}, {L"mathbfit", Alphabet::BoldItalic},
            {L"boldsymbol", Alphabet::BoldItalic}, {L"bm", Alphabet::BoldItalic}, {L"mathbb", Alphabet::DoubleStruck},
            {L"Bbb", Alphabet::DoubleStruck}, {L"mathcal", Alphabet::Script}, {L"mathscr", Alphabet::Script},
            {L"mathfrak", Alphabet::Fraktur}, {L"mathsf", Alphabet::Sans}, {L"mathtt", Alphabet::Mono},
        };
        for (const auto& f : kFonts) {
            if (name == f.name) {
                const Alphabet saved = alphabet_;
                alphabet_ = f.alphabet;
                MathNode arg = Argument();
                alphabet_ = saved;
                for (MathNode& n : arg.kids) Push(list, std::move(n));
                return;
            }
        }
        static const struct {
            const wchar_t* name;
            Alphabet alphabet;
        } kTexts[] = {
            {L"text", Alphabet::Upright},   {L"textrm", Alphabet::Upright}, {L"textnormal", Alphabet::Upright},
            {L"textup", Alphabet::Upright}, {L"mbox", Alphabet::Upright},   {L"hbox", Alphabet::Upright},
            {L"textbf", Alphabet::Bold},    {L"textit", Alphabet::Italic},  {L"emph", Alphabet::Italic},
            {L"textsf", Alphabet::Sans},    {L"texttt", Alphabet::Mono},
        };
        for (const auto& t : kTexts) {
            if (name == t.name) {
                Push(list, TextArgument(t.alphabet));
                return;
            }
        }
        if (name == L"operatorname") {
            const bool star = Peek() == '*';
            if (star) ++i_;
            const Alphabet saved = alphabet_;
            const bool saved_op = operator_name_;
            alphabet_ = Alphabet::Upright;
            operator_name_ = true;
            MathNode op = Argument();
            alphabet_ = saved;
            operator_name_ = saved_op;
            op.cls = MathClass::Op;
            op.limits = star ? 0 : 2;
            Push(list, std::move(op));
            return;
        }
        if (const Function* f = FindFunction(name)) {
            std::wstring text = name;
            if (name == L"liminf") text = L"lim inf";
            else if (name == L"limsup") text = L"lim sup";
            else if (name == L"injlim") text = L"inj lim";
            else if (name == L"projlim") text = L"proj lim";
            else if (name == L"argmax") text = L"arg max";
            else if (name == L"argmin") text = L"arg min";
            MathNode op = MakeText(text, MathClass::Op);
            op.limits = f->limits ? 0 : 2;
            Push(list, std::move(op));
            return;
        }
        if (name == L"frac" || name == L"dfrac" || name == L"tfrac" || name == L"cfrac" || name == L"binom" ||
            name == L"dbinom" || name == L"tbinom") {
            MathNode f;
            f.kind = MathKind::Frac;
            f.cls = MathClass::Inner;
            f.value = name.find(L"binom") != std::wstring::npos ? 0.f : -1.f;
            if (f.value == 0) {
                f.left = '(';
                f.right = ')';
            }
            if (name[0] == 'd' || name == L"cfrac") f.style = 0;
            if (name[0] == 't') f.style = 1;
            f.kids.push_back(Argument());
            f.kids.push_back(Argument());
            Push(list, std::move(f));
            return;
        }
        if (name == L"sqrt") {
            MathNode r;
            r.kind = MathKind::Radical;
            MathNode degree = MakeList();
            SkipSpaces();
            if (Peek() == '[') {
                ++i_;
                Stop stop;
                degree = List(kModeOptional, &stop);
            }
            r.kids.push_back(Argument());
            r.kids.push_back(std::move(degree));
            Push(list, std::move(r));
            return;
        }
        static const struct {
            const wchar_t* name;
            wchar_t accent;
            bool stretchy;
        } kAccents[] = {
            {L"hat", 0x0302, false}, {L"widehat", 0x0302, true}, {L"check", 0x030C, false},
            {L"widecheck", 0x030C, true}, {L"tilde", 0x0303, false}, {L"widetilde", 0x0303, true},
            {L"acute", 0x0301, false}, {L"grave", 0x0300, false}, {L"dot", 0x0307, false}, {L"ddot", 0x0308, false},
            {L"dddot", 0x20DB, false}, {L"breve", 0x0306, false}, {L"bar", 0x0304, false}, {L"vec", 0x20D7, false},
            {L"mathring", 0x030A, false}, {L"overrightarrow", 0x2192, true}, {L"overleftarrow", 0x2190, true},
            {L"overleftrightarrow", 0x2194, true}, {L"Overrightarrow", 0x21D2, true},
        };
        for (const auto& a : kAccents) {
            if (name == a.name) {
                MathNode n;
                n.kind = MathKind::Accent;
                n.text.push_back(a.accent);
                n.stretchy = a.stretchy;
                n.kids.push_back(Single(Argument()));
                Push(list, std::move(n));
                return;
            }
        }
        if (name == L"overline" || name == L"underline") {
            MathNode n;
            n.kind = MathKind::Overline;
            n.value = name == L"overline" ? 0.f : 1.f;
            n.kids.push_back(Argument());
            Push(list, std::move(n));
            return;
        }
        static const struct {
            const wchar_t* name;
            wchar_t brace;
            bool over;
        } kBraces[] = {
            {L"overbrace", 0x23DE, true},    {L"underbrace", 0x23DF, false},  {L"overparen", 0x23DC, true},
            {L"underparen", 0x23DD, false},  {L"overbracket", 0x23B4, true},  {L"underbracket", 0x23B5, false},
        };
        for (const auto& b : kBraces) {
            if (name == b.name) {
                MathNode n;
                n.kind = b.over ? MathKind::Over : MathKind::Under;
                n.cls = MathClass::Op;
                n.limits = 1;  // its label (^ or _) goes above / below
                n.text.push_back(b.brace);
                n.kids.push_back(Argument());
                n.kids.push_back(MakeList());
                Push(list, std::move(n));
                return;
            }
        }
        if (name == L"overset" || name == L"underset" || name == L"stackrel") {
            MathNode n;
            n.kind = name == L"underset" ? MathKind::Under : MathKind::Over;
            MathNode label = Argument();
            MathNode base = Single(Argument());
            n.cls = name == L"stackrel" ? MathClass::Rel : base.cls;
            n.kids.push_back(std::move(base));
            n.kids.push_back(std::move(label));
            Push(list, std::move(n));
            return;
        }
        if (name.size() > 1 && name[0] == 'x' && FindSymbol(name.substr(1)) &&
            ClassOf(FindSymbol(name.substr(1))->text[0]) == MathClass::Rel) {
            // \xrightarrow[below]{above}
            MathNode n;
            n.kind = MathKind::Arrow;
            n.cls = MathClass::Rel;
            n.text = FindSymbol(name.substr(1))->text;
            std::wstring below;
            const bool has_below = RawOptional(&below);
            n.kids.push_back(Argument());
            MathNode under = MakeList();
            if (has_below) under = Parser::Sub(below, depth_, color_);
            n.kids.push_back(std::move(under));
            Push(list, std::move(n));
            return;
        }
        if (name == L"left") {
            Push(list, Fenced());
            return;
        }
        if (name == L"right" || name == L"middle") {  // unpaired
            MathNode d;
            d.kind = MathKind::Delim;
            d.text.push_back(Delimiter());
            Push(list, std::move(d));
            return;
        }
        static const struct {
            const wchar_t* name;
            float size;
            MathClass cls;
        } kBig[] = {
            {L"big", 1.2f, MathClass::Ord},    {L"Big", 1.8f, MathClass::Ord},    {L"bigg", 2.4f, MathClass::Ord},
            {L"Bigg", 3.f, MathClass::Ord},    {L"bigl", 1.2f, MathClass::Open},  {L"Bigl", 1.8f, MathClass::Open},
            {L"biggl", 2.4f, MathClass::Open}, {L"Biggl", 3.f, MathClass::Open},  {L"bigr", 1.2f, MathClass::Close},
            {L"Bigr", 1.8f, MathClass::Close}, {L"biggr", 2.4f, MathClass::Close}, {L"Biggr", 3.f, MathClass::Close},
            {L"bigm", 1.2f, MathClass::Rel},   {L"Bigm", 1.8f, MathClass::Rel},   {L"biggm", 2.4f, MathClass::Rel},
            {L"Biggm", 3.f, MathClass::Rel},
        };
        for (const auto& b : kBig) {
            if (name == b.name) {
                MathNode d;
                d.kind = MathKind::Delim;
                d.cls = b.cls;
                d.value = b.size;
                d.text.push_back(Delimiter());
                Push(list, std::move(d));
                return;
            }
        }
        if (name == L"begin") {
            const std::wstring env = RawGroup();
            MathNode a = Environment(env);
            Push(list, std::move(a));
            return;
        }
        if (name == L"end") {  // unpaired
            RawGroup();
            return;
        }
        if (name == L"substack") {
            const std::wstring rows = RawGroup();  // the sub-parser views it
            Parser sub(rows);
            sub.depth_ = depth_ + 1;
            sub.color_ = color_;
            MathNode a = sub.Environment(L"substack");
            Push(list, std::move(a));
            return;
        }
        if (name == L"color") {
            if (const uint32_t c = ColorArgument()) color_ = c;
            return;
        }
        if (name == L"textcolor") {
            const uint32_t c = ColorArgument();
            const uint32_t saved = color_;
            if (c) color_ = c;
            MathNode arg = Argument();
            color_ = saved;
            for (MathNode& n : arg.kids) Push(list, std::move(n));
            return;
        }
        if (name == L"colorbox" || name == L"fcolorbox") {
            ColorArgument();
            if (name == L"fcolorbox") ColorArgument();
            Push(list, TextArgument(Alphabet::Upright));
            return;
        }
        if (name == L"boxed" || name == L"fbox") {
            MathNode b;
            b.kind = MathKind::Boxed;
            b.kids.push_back(name == L"fbox" ? TextArgument(Alphabet::Upright) : Argument());
            Push(list, std::move(b));
            return;
        }
        if (name == L"phantom" || name == L"hphantom" || name == L"vphantom") {
            MathNode p;
            p.kind = MathKind::Phantom;
            p.value = name[0] == 'h' ? 1.f : name[0] == 'v' ? 2.f : 0.f;
            p.kids.push_back(Argument());
            Push(list, std::move(p));
            return;
        }
        if (name == L"mathstrut" || name == L"strut") {
            MathNode p;
            p.kind = MathKind::Phantom;
            p.value = 2;
            MathNode paren = MakeList();
            paren.kids.push_back(MakeGlyphs(L"(", MathClass::Open));
            p.kids.push_back(std::move(paren));
            Push(list, std::move(p));
            return;
        }
        if (name == L"smash" || name == L"cancel" || name == L"bcancel" || name == L"xcancel" || name == L"mathclap" ||
            name == L"mathllap" || name == L"mathrlap" || name == L"displaylines") {
            RawOptional(nullptr);
            MathNode arg = Argument();
            for (MathNode& n : arg.kids) Push(list, std::move(n));
            return;
        }
        if (name == L"not") {
            MathNode next = Argument();  // counts depth; leaves }, & and \\ alone
            static const wchar_t kPairs[][2] = {
                {'=', 0x2260},    {0x2208, 0x2209}, {0x2282, 0x2284}, {0x2283, 0x2285}, {0x2286, 0x2288},
                {0x2287, 0x2289}, {0x2261, 0x2262}, {0x2203, 0x2204}, {0x223C, 0x2241}, {0x2243, 0x2244},
                {0x2245, 0x2247}, {0x2248, 0x2249}, {'<', 0x226E},    {'>', 0x226F},    {0x2264, 0x2270},
                {0x2265, 0x2271}, {0x2223, 0x2224}, {0x2225, 0x2226}, {0x227A, 0x2280}, {0x227B, 0x2281},
                {0x2190, 0x219A}, {0x2192, 0x219B}, {0x2194, 0x21AE}, {0x21D0, 0x21CD}, {0x21D2, 0x21CF},
                {0x21D4, 0x21CE}, {0x220B, 0x220C}, {0x22A2, 0x22AC}, {0x22A8, 0x22AD},
            };
            if (next.kids.empty()) {
                Push(list, MakeGlyphs(L"/", MathClass::Rel));
                return;
            }
            // The first symbol (also inside a group) gets its negated form,
            // else a slash over it; without any, a slash in front.
            if (MathNode* k = FirstGlyphs(next)) {
                bool mapped = false;
                if (k->text.size() == 1) {
                    for (const auto& p : kPairs) {
                        if (k->text[0] == p[0]) {
                            k->text[0] = p[1];
                            mapped = true;
                            break;
                        }
                    }
                }
                if (!mapped) k->text.push_back(0x0338);  // combining long solidus overlay
            } else {
                Push(list, MakeGlyphs(L"/", MathClass::Rel));
            }
            for (MathNode& k : next.kids) Push(list, std::move(k));
            return;
        }
        if (name == L"limits" || name == L"nolimits" || name == L"displaylimits") {
            SetLimits(list, name == L"limits" ? 1 : name == L"nolimits" ? 2 : 0);
            return;
        }
        static const struct {
            const wchar_t* name;
            MathClass cls;
        } kClasses[] = {
            {L"mathord", MathClass::Ord},   {L"mathop", MathClass::Op},       {L"mathbin", MathClass::Bin},
            {L"mathrel", MathClass::Rel},   {L"mathopen", MathClass::Open},   {L"mathclose", MathClass::Close},
            {L"mathpunct", MathClass::Punct}, {L"mathinner", MathClass::Inner},
        };
        for (const auto& k : kClasses) {
            if (name == k.name) {
                MathNode arg = Argument();
                arg.cls = k.cls;
                Push(list, std::move(arg));
                return;
            }
        }
        if (name == L"bmod" || name == L"mod" || name == L"pmod" || name == L"pod") {
            if (name == L"bmod") {
                Push(list, MakeText(L"mod", MathClass::Bin));
                return;
            }
            Push(list, MakeSpace(name == L"mod" ? 12.f : 18.f));
            if (name != L"mod") Push(list, MakeGlyphs(L"(", MathClass::Open));
            if (name != L"pod") {
                Push(list, MakeText(L"mod"));
                Push(list, MakeSpace(6));
            }
            MathNode arg = Argument();
            for (MathNode& n : arg.kids) Push(list, std::move(n));
            if (name != L"mod") Push(list, MakeGlyphs(L")", MathClass::Close));
            return;
        }
        if (name == L"tag") {
            const bool star = Peek() == '*';
            if (star) ++i_;
            Push(list, MakeSpace(36));
            MathNode tag = TextArgument(Alphabet::Upright);
            if (!star) Push(list, MakeText(L"("));
            for (MathNode& n : tag.kids) Push(list, std::move(n));
            if (!star) Push(list, MakeText(L")"));
            return;
        }
        if (name == L"label" || name == L"eqref" || name == L"ref") {
            RawGroup();
            return;
        }
        if (name == L"nonumber" || name == L"notag" || name == L"hline" || name == L"allowbreak" ||
            name == L"nolinebreak" || name == L"newline" || name == L"/" || name == L"relax") {
            return;
        }
        if (name == L"cline") {
            RawGroup();
            return;
        }
        if (name == L"colon") {  // amsmath: 2mu, the colon, then 6mu (thin comes from Punct)
            Push(list, MakeSpace(2));
            Push(list, MakeGlyphs(L":", MathClass::Punct));
            Push(list, MakeSpace(3));
            return;
        }
        if (name == L"iff" || name == L"implies" || name == L"impliedby") {
            Push(list, MakeSpace(5));
            Push(list, MakeGlyphs(name == L"iff" ? L"⟺" : name == L"implies" ? L"⟹" : L"⟸", MathClass::Rel));
            Push(list, MakeSpace(5));
            return;
        }
        if (name.size() == 1 && wcschr(L"#$%&_", name[0])) {
            Push(list, MakeGlyphs(name, MathClass::Ord));
            return;
        }
        if (const Symbol* sym = FindSymbol(name)) {
            uint32_t cp = static_cast<uint32_t>(sym->text[0]);
            if (cp >= 0xD800 && cp <= 0xDBFF && sym->text[1]) cp = 0x10000 + ((cp - 0xD800) << 10) + (sym->text[1] - 0xDC00);
            MathNode g = MakeGlyphs(IsGreekOrLatin(cp) ? Styled(cp, alphabet_) : sym->text, ClassFromCode(sym->cls, cp));
            if (sym->cls == 'L') {
                g.large = true;
                if (IsIntegral(cp)) g.limits = 2;
            }
            Push(list, std::move(g));
            return;
        }
        // Unknown command: its source, in red like MathJax shows it.
        MathNode t = MakeText(L"\\" + name);
        t.error = true;
        Push(list, std::move(t));
    }

    static MathNode Sub(const std::wstring& source, int depth, uint32_t color) {
        Parser sub(source);
        sub.depth_ = depth + 1;
        sub.color_ = color;
        Stop stop;
        return sub.List(kModeGroup, &stop);
    }

    uint32_t ColorArgument() {
        std::wstring model;
        RawOptional(&model);
        return ParseColor(model, RawGroup());
    }

    // A TeX dimension after \kern and friends: digits and a unit.
    std::wstring ReadDimension() {
        SkipSpaces();
        const size_t b = i_;
        if (Peek() == '-' || Peek() == '+') ++i_;
        const size_t digits = i_;
        while (!AtEnd() && ((s_[i_] >= '0' && s_[i_] <= '9') || s_[i_] == '.')) ++i_;
        if (i_ == digits) {  // no number: whatever follows is not a unit (\kern xy)
            i_ = b;
            return {};
        }
        SkipSpaces();
        const auto letter = [](wchar_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
        if (i_ + 1 < s_.size() && letter(s_[i_]) && letter(s_[i_ + 1])) i_ += 2;
        return std::wstring(s_.substr(b, i_ - b));
    }

    static float DimensionMu(std::wstring_view d) {
        d = Trim(d);
        wchar_t* end = nullptr;
        const std::wstring s(d);
        const float v = std::wcstof(s.c_str(), &end);
        if (!std::isfinite(v)) return 0;
        std::wstring unit(Trim(std::wstring_view(end)));
        for (wchar_t& c : unit) {
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';  // TeX takes \kern3PT as well
        }
        float mu = v * 18.f;                      // em
        if (unit == L"mu") mu = v;
        else if (unit == L"ex") mu = v * 8.f;
        else if (unit == L"pt") mu = v * 1.8f;    // 10pt = 1em
        else if (unit == L"pc") mu = v * 21.6f;   // 12pt
        else if (unit == L"bp") mu = v * 1.807f;  // 72.27pt / 72
        else if (unit == L"dd") mu = v * 1.926f;  // 1238pt / 1157
        else if (unit == L"cc") mu = v * 23.11f;  // 12dd
        else if (unit == L"sp") mu = v * 1.8f / 65536.f;
        else if (unit == L"px") mu = v * 18.f / 16.f;
        else if (unit == L"cm") mu = v * 51.f;
        else if (unit == L"mm") mu = v * 5.1f;
        else if (unit == L"in") mu = v * 130.f;
        return std::clamp(mu, -360.f, 360.f);
    }

    std::wstring_view s_;
    size_t i_ = 0;
    int depth_ = 0;
    int groups_ = 0;   // open {groups}
    int cells_ = 0;    // open array cells
    Alphabet alphabet_ = Alphabet::Normal;
    uint32_t color_ = 0;
    bool operator_name_ = false;  // inside \operatorname: - and * are text
    size_t scan_budget_;          // characters [optional] arguments may still scan
};

void Dump(const MathNode& n, std::wstring& out) {
    static const wchar_t* kKinds[] = {L"list", L"g", L"text", L"space", L"style", L"scripts", L"frac", L"sqrt",
                                      L"accent", L"over", L"under", L"line", L"fenced", L"delim", L"array",
                                      L"arrow", L"boxed", L"phantom"};
    out += kKinds[static_cast<int>(n.kind)];
    if (!n.text.empty() || n.kind == MathKind::Glyphs || n.kind == MathKind::Text) {
        out += L":";
        for (wchar_t c : n.text) out.push_back(c ? c : L'.');  // no delimiter
    }
    if (n.left || n.right) {
        out += L"[";
        out.push_back(n.left ? n.left : L'.');
        out.push_back(n.right ? n.right : L'.');
        out += L"]";
    }
    if (n.color) {
        wchar_t hex[16];
        swprintf_s(hex, L"#%06x", n.color & 0xFFFFFF);
        out += hex;
    }
    if (n.kind == MathKind::Space) out += L":" + std::to_wstring(static_cast<int>(n.value));
    if (n.error) out += L"!";
    if (!n.kids.empty()) {
        out += L"(";
        for (size_t k = 0; k < n.kids.size(); ++k) {
            if (k) out += L",";
            Dump(n.kids[k], out);
        }
        out += L")";
    }
}

}  // namespace

MathNode ParseTex(std::wstring_view tex) {
    if (tex.size() > kMaxSource) {
        // Its start, in red: all of it would be a glyph per character of a
        // formula that may be megabytes long.
        size_t n = kMaxSource;
        if (tex[n - 1] >= 0xD800 && tex[n - 1] <= 0xDBFF) --n;  // (not half a pair)
        MathNode list = MakeList();
        MathNode t = MakeText(std::wstring(tex.substr(0, n)) + L"…");
        t.error = true;
        list.kids.push_back(std::move(t));
        return list;
    }
    Parser p(tex);
    return p.Top();
}

std::wstring DumpMath(const MathNode& node) {
    std::wstring out;
    Dump(node, out);
    return out;
}

}  // namespace mdv
