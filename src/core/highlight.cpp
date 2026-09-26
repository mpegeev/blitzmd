// Fast table-driven syntax highlighter. See highlight.h.
//
// Design notes
// ------------
// * One generic "C-like" scanner (CScanner) configured by a LangDef (keyword /
//   type / constant word sets, comment delimiters and feature flags) covers
//   most programming languages. Dedicated small scanners handle markup, CSS,
//   JSON, YAML, TOML/INI, diff, markdown, shell, PowerShell, batch, Dockerfile,
//   Makefile, CMake, assembly and LaTeX.
// * Word sets are open-addressing hash tables built at compile time from a
//   space separated literal (constexpr), so static initialization is trivial.
// * Every scanner advances monotonically; lookaheads are either bounded or
//   guarded so that a failed lookahead is never repeated over the same text
//   (keeps everything linear, even on pathological input).
// * All spans go through Emitter::Add which clamps/drops anything that would
//   violate the ascending / non-overlapping / in-bounds invariants.
#include "highlight.h"

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string_view>
#include <vector>

namespace mdv {
namespace {

using wsv = std::wstring_view;
using K = TokenKind;

constexpr size_t kNpos = static_cast<size_t>(-1);

// ---------------------------------------------------------------------------
// Character classes
// ---------------------------------------------------------------------------
constexpr uint8_t kClsAlpha = 1, kClsDigit = 2, kClsBlank = 4, kClsUpper = 8,
                  kClsLower = 16, kClsHex = 32;

struct CharTable {
    uint8_t t[128];
    constexpr CharTable() : t{} {
        for (unsigned c = 0; c < 128; ++c) {
            unsigned r = 0;
            if (c >= 'a' && c <= 'z') r |= kClsAlpha | kClsLower;
            if (c >= 'A' && c <= 'Z') r |= kClsAlpha | kClsUpper;
            if (c >= '0' && c <= '9') r |= kClsDigit | kClsHex;
            if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) r |= kClsHex;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f') r |= kClsBlank;
            t[c] = static_cast<uint8_t>(r);
        }
    }
};
constexpr CharTable kCharTable{};

inline unsigned U(wchar_t c) { return static_cast<unsigned>(c); }
inline bool Cls(wchar_t c, uint8_t m) { return U(c) < 128 && (kCharTable.t[U(c)] & m) != 0; }

inline bool IsWideSpace(unsigned u) {
    return u == 0x85 || u == 0xA0 || u == 0x1680 || (u >= 0x2000 && u <= 0x200B) ||
           u == 0x2028 || u == 0x2029 || u == 0x202F || u == 0x205F || u == 0x3000 || u == 0xFEFF;
}
// Blank = whitespace other than '\n'.
inline bool IsBlank(wchar_t c) { return U(c) < 128 ? Cls(c, kClsBlank) : IsWideSpace(U(c)); }
inline bool IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }
inline bool IsHex(wchar_t c) { return Cls(c, kClsHex); }
inline bool IsAlpha(wchar_t c) { return Cls(c, kClsAlpha); }
inline bool IsUpper(wchar_t c) { return Cls(c, kClsUpper); }
inline bool IsLower(wchar_t c) { return Cls(c, kClsLower); }
inline bool IsAlnum(wchar_t c) { return Cls(c, kClsAlpha | kClsDigit); }
inline bool IsIdStart(wchar_t c) {
    unsigned u = U(c);
    if (u < 128) return (kCharTable.t[u] & kClsAlpha) != 0 || u == '_';
    return !IsWideSpace(u);
}
inline bool IsIdChar(wchar_t c) { return IsIdStart(c) || IsDigit(c); }
inline bool IsSpaceOrNl(wchar_t c) { return c == L'\n' || IsBlank(c); }

constexpr wchar_t Lower(wchar_t c) { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c; }

// ---------------------------------------------------------------------------
// Compile-time word sets (open addressing, load factor <= 0.5)
// ---------------------------------------------------------------------------
constexpr uint32_t HashCI(const wchar_t* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t k = 0; k < n; ++k) {
        h ^= static_cast<uint32_t>(Lower(p[k]));
        h *= 16777619u;
    }
    return h ^ (h >> 15);
}

struct WordSet {
    const wsv* slots = nullptr;
    uint32_t mask = 0;
};

constexpr size_t CountWords(wsv s) {
    size_t c = 0;
    bool in = false;
    for (wchar_t ch : s) {
        const bool sp = ch == L' ';
        if (!sp && !in) ++c;
        in = !sp;
    }
    return c;
}
constexpr size_t CapFor(size_t n) {
    size_t c = 8;
    while (c < n * 2 + 1) c <<= 1;
    return c;
}

template <size_t Cap>
struct WordStore {
    wsv slots[Cap];
    constexpr explicit WordStore(wsv list) : slots{} {
        size_t i = 0;
        while (i < list.size()) {
            while (i < list.size() && list[i] == L' ') ++i;
            const size_t b = i;
            while (i < list.size() && list[i] != L' ') ++i;
            if (i > b) {
                const wsv w = list.substr(b, i - b);
                uint32_t h = HashCI(w.data(), w.size()) & static_cast<uint32_t>(Cap - 1);
                while (!slots[h].empty() && slots[h] != w) h = (h + 1) & static_cast<uint32_t>(Cap - 1);
                slots[h] = w;
            }
        }
    }
};

#define MDV_WORDS(name, list)                                                      \
    constexpr WordStore<CapFor(CountWords(list))> name##Store{list};               \
    constexpr WordSet name { name##Store.slots, static_cast<uint32_t>(CapFor(CountWords(list)) - 1) }

// Case-insensitive sets must be written in lower case.
bool InSet(const WordSet& ws, const wchar_t* p, size_t n, bool ci) {
    if (ws.slots == nullptr || n == 0 || n > 64) return false;
    uint32_t h = HashCI(p, n) & ws.mask;
    for (uint32_t probes = 0; probes <= ws.mask; ++probes) {
        const wsv& w = ws.slots[h];
        if (w.empty()) return false;
        if (w.size() == n) {
            size_t k = 0;
            if (ci) {
                while (k < n && Lower(p[k]) == w[k]) ++k;
            } else {
                while (k < n && p[k] == w[k]) ++k;
            }
            if (k == n) return true;
        }
        h = (h + 1) & ws.mask;
    }
    return false;
}
inline bool InSet(const WordSet& ws, wsv w, bool ci) { return InSet(ws, w.data(), w.size(), ci); }

bool EqCI(const wchar_t* p, size_t n, wsv lowerLit) {
    if (n != lowerLit.size()) return false;
    for (size_t k = 0; k < n; ++k)
        if (Lower(p[k]) != lowerLit[k]) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Span emitter: enforces invariants, merges adjacent spans of equal kind.
// ---------------------------------------------------------------------------
class Emitter {
public:
    Emitter(std::vector<HighlightSpan>& out, size_t limit)
        : out_(out), first_(out.size()), limit_(limit) {}

    void Add(size_t a, size_t b, K k) {
        if (k == K::Plain || k >= K::Count) return;
        a += base_;
        b += base_;
        if (b > limit_) b = limit_;
        if (a < last_) a = last_;
        if (a >= b) return;
        if (out_.size() > first_) {
            HighlightSpan& p = out_.back();
            if (p.kind == k && static_cast<size_t>(p.start) + p.length == a) {
                p.length += static_cast<uint32_t>(b - a);
                last_ = b;
                return;
            }
        }
        out_.push_back(HighlightSpan{static_cast<uint32_t>(a), static_cast<uint32_t>(b - a), k});
        last_ = b;
    }

    // Offset added to all positions (used for embedded sub-languages).
    size_t base_ = 0;

private:
    std::vector<HighlightSpan>& out_;
    size_t first_;
    size_t limit_;
    size_t last_ = 0;
};

// Temporarily shifts the emitter base for scanning a sub-range.
class BaseShift {
public:
    BaseShift(Emitter& em, size_t off) : em_(em), saved_(em.base_) { em_.base_ += off; }
    ~BaseShift() { em_.base_ = saved_; }
    BaseShift(const BaseShift&) = delete;
    BaseShift& operator=(const BaseShift&) = delete;

private:
    Emitter& em_;
    size_t saved_;
};

// ---------------------------------------------------------------------------
// Small text helpers (all bounds-checked)
// ---------------------------------------------------------------------------
inline size_t LineEnd(const wchar_t* s, size_t n, size_t i) {
    if (i >= n) return n;
    const wchar_t* p = std::wmemchr(s + i, L'\n', n - i);
    return p ? static_cast<size_t>(p - s) : n;
}
// End of line content, excluding trailing '\r'.
inline size_t ContentEnd(const wchar_t* s, size_t a, size_t le) {
    while (le > a && s[le - 1] == L'\r') --le;
    return le;
}
inline size_t TrimRight(const wchar_t* s, size_t a, size_t b) {
    while (b > a && IsBlank(s[b - 1])) --b;
    return b;
}
inline size_t SkipBlanks(const wchar_t* s, size_t n, size_t i) {
    while (i < n && IsBlank(s[i])) ++i;
    return i;
}
inline size_t SkipWs(const wchar_t* s, size_t n, size_t i) {
    while (i < n && IsSpaceOrNl(s[i])) ++i;
    return i;
}
inline size_t ScanIdent(const wchar_t* s, size_t n, size_t i) {
    while (i < n && IsIdChar(s[i])) ++i;
    return i;
}
inline bool At(const wchar_t* s, size_t n, size_t i, wsv lit) {
    if (i > n || n - i < lit.size()) return false;
    for (size_t k = 0; k < lit.size(); ++k)
        if (s[i + k] != lit[k]) return false;
    return true;
}
inline bool AtCI(const wchar_t* s, size_t n, size_t i, wsv lowerLit) {
    if (i > n || n - i < lowerLit.size()) return false;
    for (size_t k = 0; k < lowerLit.size(); ++k)
        if (Lower(s[i + k]) != lowerLit[k]) return false;
    return true;
}
// Finds `lit` at or after i; returns kNpos if absent.
size_t Find(const wchar_t* s, size_t n, size_t i, wsv lit) {
    if (lit.empty()) return i;
    while (i < n) {
        const wchar_t* p = std::wmemchr(s + i, lit[0], n - i);
        if (!p) return kNpos;
        i = static_cast<size_t>(p - s);
        if (At(s, n, i, lit)) return i;
        ++i;
    }
    return kNpos;
}
// Finds character c in [i, e); returns kNpos if absent.
inline size_t FindCh(const wchar_t* s, size_t i, size_t e, wchar_t c) {
    if (i >= e) return kNpos;
    const wchar_t* p = std::wmemchr(s + i, c, e - i);
    return p ? static_cast<size_t>(p - s) : kNpos;
}

// Memoized "find character on the current line" for forward-moving scanners.
// A result computed from position `lo` stays valid for any later query up to
// the found position (or the end of the line when not found), so repeated
// lookups for the same character (e.g. many unterminated "${" on one huge
// line) cost linear time overall instead of quadratic.
class LineFinder {
public:
    size_t Find(const wchar_t* s, size_t n, size_t i, wchar_t ch) {
        Slot& sl = slots_[U(ch) & 7];
        if (sl.ch == ch && i >= sl.lo && i <= sl.hi) return sl.res;
        size_t j = i;
        while (j < n && s[j] != ch && s[j] != L'\n') ++j;
        sl.ch = ch;
        sl.lo = i;
        sl.hi = j;
        sl.res = (j < n && s[j] == ch) ? j : kNpos;
        return sl.res;
    }

private:
    struct Slot {
        wchar_t ch = 0;
        size_t lo = 1, hi = 0, res = kNpos;
    };
    Slot slots_[8];
};

inline bool IsHighSurrogate(wchar_t c) { return U(c) >= 0xD800 && U(c) <= 0xDBFF; }
inline bool IsLowSurrogate(wchar_t c) { return U(c) >= 0xDC00 && U(c) <= 0xDFFF; }

// s[i] == '\\'. Returns the end of the escape sequence (> i).
size_t EscapeEnd(const wchar_t* s, size_t n, size_t i) {
    size_t j = i + 1;
    if (j >= n) return n;
    const wchar_t c = s[j++];
    auto hexRun = [&](size_t maxLen) {
        size_t k = 0;
        while (k < maxLen && j < n && IsHex(s[j])) {
            ++j;
            ++k;
        }
    };
    switch (c) {
    case L'x': hexRun(2); break;
    case L'u':
        if (j < n && s[j] == L'{') {
            size_t k = j + 1;
            while (k < n && k - j < 10 && IsHex(s[k])) ++k;
            if (k < n && s[k] == L'}') j = k + 1;
        } else {
            hexRun(4);
        }
        break;
    case L'U': hexRun(8); break;
    case L'0': case L'1': case L'2': case L'3': case L'4': case L'5': case L'6': case L'7':
        while (j < n && j - i < 4 && s[j] >= L'0' && s[j] <= L'7') ++j;
        break;
    case L'\r':
        if (j < n && s[j] == L'\n') ++j;
        break;
    default:
        if (IsHighSurrogate(c) && j < n && IsLowSurrogate(s[j])) ++j;
        break;
    }
    return j;
}

// Generic number scanner: s[i] is a digit or '.' followed by a digit.
size_t ScanNumber(const wchar_t* s, size_t n, size_t i, bool quoteSep) {
    size_t j = i;
    auto run = [&](bool (*pred)(wchar_t)) {
        while (j < n) {
            const wchar_t c = s[j];
            if (pred(c) || (c == L'_' && j > i)) {
                ++j;
            } else if (quoteSep && c == L'\'' && j + 1 < n && pred(s[j + 1]) && j > i) {
                ++j;
            } else {
                break;
            }
        }
    };
    if (s[j] == L'0' && j + 1 < n && (s[j + 1] == L'x' || s[j + 1] == L'X')) {
        j += 2;
        run(IsHex);
        if (j < n && s[j] == L'.') {
            ++j;
            run(IsHex);
        }
        if (j < n && (s[j] == L'p' || s[j] == L'P')) {
            size_t k = j + 1;
            if (k < n && (s[k] == L'+' || s[k] == L'-')) ++k;
            if (k < n && IsDigit(s[k])) {
                j = k;
                run(IsDigit);
            }
        }
    } else if (s[j] == L'0' && j + 1 < n && (s[j + 1] == L'b' || s[j + 1] == L'B' || s[j + 1] == L'o' ||
                                              s[j + 1] == L'O') &&
               j + 2 < n && IsDigit(s[j + 2])) {
        j += 2;
        run(IsDigit);
    } else {
        run(IsDigit);
        if (j < n && s[j] == L'.' && j + 1 < n && IsDigit(s[j + 1])) {
            ++j;
            run(IsDigit);
        } else if (j < n && s[j] == L'.' && j > i &&
                   !(j + 1 < n && (s[j + 1] == L'.' || IsIdStart(s[j + 1])))) {
            ++j;  // "1."
        }
        if (j < n && (s[j] == L'e' || s[j] == L'E')) {
            size_t k = j + 1;
            if (k < n && (s[k] == L'+' || s[k] == L'-')) ++k;
            if (k < n && IsDigit(s[k])) {
                j = k;
                run(IsDigit);
            }
        }
    }
    // Suffixes: u, l, f, n, i32, px, ...
    while (j < n && (IsAlnum(s[j]) || s[j] == L'_')) ++j;
    return j > i ? j : i + 1;
}

inline bool IsAllCaps(wsv w) {
    if (w.size() < 2 || !IsUpper(w[0])) return false;
    size_t letters = 0;
    for (wchar_t c : w) {
        if (IsUpper(c)) ++letters;
        else if (!IsDigit(c) && c != L'_') return false;
    }
    return letters >= 2;
}
inline bool IsPascalCase(wsv w) {
    if (w.empty() || !IsUpper(w[0])) return false;
    for (wchar_t c : w)
        if (IsLower(c)) return true;
    return false;
}
inline bool AllDigits(const wchar_t* s, size_t a, size_t b) {
    if (a >= b) return false;
    for (size_t k = a; k < b; ++k)
        if (!IsDigit(s[k])) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Word tables (case-sensitive unless noted; CI tables are lower case)
// ---------------------------------------------------------------------------
// Keywords after which the next identifier is a function / type name.
MDV_WORDS(kDefWords, L"def fn func function fun sub proc procedure constructor destructor method macro "
                     L"defp defmacro defmacrop defn defun iterator converter template");
MDV_WORDS(kTypeIntroWords, L"class struct enum interface trait impl type new extends implements record union "
                           L"object protocol extension actor typealias module defmodule defprotocol defimpl "
                           L"mixin data newtype message service contract library structure");
// Keywords after which dotted/qualified names are module paths (left plain).
MDV_WORDS(kNsWords, L"using namespace import package use imports open");
// Contextual keywords that are ordinary method names when directly called: get(...), set(...).
MDV_WORDS(kSoftWords, L"get set of from as type module declare value field init");

// C / C++ / ObjC / CUDA
MDV_WORDS(kCKw, L"auto break case const continue default do else enum extern for goto if inline register "
                L"restrict return sizeof static struct switch typedef union volatile while _Alignas _Alignof "
                L"_Atomic _Generic _Noreturn _Static_assert _Thread_local alignas alignof constexpr "
                L"static_assert thread_local typeof typeof_unqual __attribute__ __declspec __asm__ asm "
                L"__inline __forceinline __cdecl __stdcall __fastcall");
MDV_WORDS(kCTypes, L"char double float int long short signed unsigned void bool _Bool _Complex _BitInt size_t "
                   L"ssize_t ptrdiff_t intptr_t uintptr_t intmax_t uintmax_t int8_t int16_t int32_t int64_t "
                   L"uint8_t uint16_t uint32_t uint64_t wchar_t char8_t char16_t char32_t FILE va_list off_t "
                   L"time_t clock_t wint_t max_align_t nullptr_t __int8 __int16 __int32 __int64 BOOL BYTE WORD "
                   L"DWORD QWORD HANDLE HRESULT HWND LPARAM WPARAM LRESULT UINT ULONG LONG LPCWSTR LPWSTR "
                   L"LPCSTR LPSTR LPVOID PVOID SIZE_T DWORD_PTR UINT_PTR INT_PTR ULONG_PTR WCHAR TCHAR");
MDV_WORDS(kCConsts, L"true false NULL nullptr EOF stdin stdout stderr TRUE FALSE errno");
MDV_WORDS(kCppKw, L"alignas alignof and and_eq asm auto bitand bitor break case catch class co_await "
                  L"co_return co_yield compl concept const const_cast consteval constexpr constinit "
                  L"continue decltype default delete do dynamic_cast else enum explicit export extern for "
                  L"friend goto if inline mutable namespace new noexcept not not_eq operator or or_eq "
                  L"private protected public register reinterpret_cast requires return sizeof static "
                  L"static_assert static_cast struct switch template this thread_local throw try typedef "
                  L"typeid typename union using virtual volatile while xor xor_eq override final import "
                  L"module __global__ __device__ __host__ __shared__ __constant__ __restrict__ "
                  L"__forceinline__ __attribute__ __declspec __cdecl __stdcall __fastcall __forceinline");
MDV_WORDS(kCppTypes, L"bool char char8_t char16_t char32_t double float int long short signed unsigned void "
                     L"wchar_t size_t ssize_t ptrdiff_t intptr_t uintptr_t intmax_t uintmax_t int8_t int16_t "
                     L"int32_t int64_t uint8_t uint16_t uint32_t uint64_t nullptr_t max_align_t byte FILE "
                     L"va_list BOOL BYTE WORD DWORD QWORD HANDLE HRESULT HWND HINSTANCE HMODULE LPARAM WPARAM "
                     L"LRESULT UINT ULONG LONG LPCWSTR LPWSTR LPCSTR LPSTR LPVOID PVOID SIZE_T DWORD_PTR "
                     L"UINT_PTR INT_PTR ULONG_PTR WCHAR TCHAR __int8 __int16 __int32 __int64 dim3");
MDV_WORDS(kCppConsts, L"true false nullptr NULL TRUE FALSE EOF stdin stdout stderr");
MDV_WORDS(kObjCKw, L"auto break case const continue default do else enum extern for goto if inline register "
                   L"restrict return sizeof static struct switch typedef union volatile while self super "
                   L"interface implementation end property synthesize protocol class selector encode "
                   L"autoreleasepool try catch finally throw dynamic optional required public private "
                   L"protected package import nonatomic atomic strong weak copy assign retain readonly "
                   L"readwrite nullable nonnull __block __weak __strong __unsafe_unretained in out inout "
                   L"bycopy byref oneway instancetype");
MDV_WORDS(kObjCTypes, L"char double float int long short signed unsigned void bool BOOL id SEL IMP Class "
                      L"NSInteger NSUInteger CGFloat size_t int8_t int16_t int32_t int64_t uint8_t uint16_t "
                      L"uint32_t uint64_t");
MDV_WORDS(kObjCConsts, L"true false YES NO nil Nil NULL");

// C#
MDV_WORDS(kCsKw, L"abstract as base break case catch checked class const continue default delegate do else "
                 L"enum event explicit extern finally fixed for foreach goto if implicit in interface "
                 L"internal is lock namespace new operator out override params private protected public "
                 L"readonly ref return sealed sizeof stackalloc static struct switch this throw try typeof "
                 L"unchecked unsafe using virtual volatile while add alias ascending async await by "
                 L"descending equals from get global group init into join let managed nameof notnull on "
                 L"orderby partial record remove required scoped select set unmanaged value var when where "
                 L"with yield file");
MDV_WORDS(kCsTypes, L"bool byte char decimal double float int long object sbyte short string uint ulong ushort "
                    L"void dynamic nint nuint");
MDV_WORDS(kNullConsts, L"true false null");

// Java / Kotlin / Scala / Groovy / Dart
MDV_WORDS(kJavaKw, L"abstract assert break case catch class const continue default do else enum extends final "
                   L"finally for goto if implements import instanceof interface native new package private "
                   L"protected public return static strictfp super switch synchronized this throw throws "
                   L"transient try volatile while var record sealed permits non-sealed yield module requires "
                   L"exports opens uses provides to transitive");
MDV_WORDS(kJavaTypes, L"boolean byte char double float int long short void String Object");
MDV_WORDS(kKotlinKw, L"abstract actual annotation as break by catch class companion const constructor "
                     L"continue crossinline data delegate do dynamic else enum expect external field file "
                     L"final finally for fun get if import in infix init inline inner interface internal is "
                     L"lateinit noinline object open operator out override package param private property "
                     L"protected public receiver reified return sealed set setparam super suspend tailrec "
                     L"this throw try typealias typeof val value var vararg when where while");
MDV_WORDS(kKotlinTypes, L"Any Boolean Byte Char Double Float Int Long Nothing Short String Unit UInt ULong "
                        L"UByte UShort Array List Map Set MutableList MutableMap MutableSet");
MDV_WORDS(kScalaKw, L"abstract case catch class def do else enum export extends final finally for forSome "
                    L"given if implicit import lazy match new object override package private protected "
                    L"return sealed super then this throw trait try type using val var while with yield "
                    L"derives end extension inline opaque transparent");
MDV_WORDS(kScalaTypes, L"Int Long Short Byte Char Float Double Boolean Unit String Any AnyRef AnyVal Nothing "
                       L"Null");
MDV_WORDS(kScalaConsts, L"true false null None Nil");
MDV_WORDS(kGroovyKw, L"as assert break case catch class const continue def default do else enum extends final "
                     L"finally for goto if implements import in instanceof interface new package private "
                     L"protected public return static super switch synchronized this throw throws trait try "
                     L"var while");
MDV_WORDS(kGroovyTypes, L"boolean byte char double float int long short void String Object");
MDV_WORDS(kDartKw, L"abstract as assert async await base break case catch class const continue covariant "
                   L"default deferred do else enum export extends extension external factory final finally "
                   L"for get hide if implements import in interface is late library mixin new of on operator "
                   L"part required rethrow return sealed set show static super switch sync this throw try "
                   L"typedef var when with while yield");
MDV_WORDS(kDartTypes, L"void int double num bool String List Map Set dynamic Object Future Stream Never Null "
                      L"Iterable Function");

// Swift / Go / Rust / Zig / D
MDV_WORDS(kSwiftKw, L"associatedtype class deinit enum extension fileprivate func import init inout internal "
                    L"let open operator private precedencegroup protocol public rethrows static struct "
                    L"subscript typealias var break case catch continue default defer do else fallthrough "
                    L"for guard if in repeat return throw switch where while as is try throws async await "
                    L"self Self super associativity convenience didSet dynamic final get indirect lazy "
                    L"mutating nonmutating optional override postfix prefix required set some any unowned "
                    L"weak willSet actor nonisolated isolated consume borrowing consuming macro package");
MDV_WORDS(kSwiftTypes, L"Int Int8 Int16 Int32 Int64 UInt UInt8 UInt16 UInt32 UInt64 Float Double Bool String "
                       L"Character Void Any AnyObject Never Optional Array Dictionary Set");
MDV_WORDS(kSwiftConsts, L"true false nil");
MDV_WORDS(kGoKw, L"break case chan const continue default defer else fallthrough for func go goto if import "
                 L"interface map package range return select struct switch type var");
MDV_WORDS(kGoTypes, L"bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune "
                    L"string uint uint8 uint16 uint32 uint64 uintptr any comparable");
MDV_WORDS(kGoConsts, L"true false nil iota");
MDV_WORDS(kRustKw, L"as async await break const continue crate dyn else enum extern fn for if impl in let "
                   L"loop match mod move mut pub ref return self Self static struct super trait type union "
                   L"unsafe use where while abstract become box do final macro override priv try typeof "
                   L"unsized virtual yield macro_rules gen");
MDV_WORDS(kRustTypes, L"bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize");
MDV_WORDS(kRustConsts, L"true false None Some Ok Err");
MDV_WORDS(kZigKw, L"addrspace align allowzero and anyframe anytype asm async await break callconv catch "
                  L"comptime const continue defer else enum errdefer error export extern fn for if inline "
                  L"linksection noalias noinline nosuspend opaque or orelse packed pub resume return struct "
                  L"suspend switch test threadlocal try union unreachable usingnamespace var volatile while");
MDV_WORDS(kZigTypes, L"i8 u8 i16 u16 i32 u32 i64 u64 i128 u128 isize usize c_char c_short c_ushort c_int "
                     L"c_uint c_long c_ulong c_longlong c_ulonglong f16 f32 f64 f80 f128 bool void noreturn "
                     L"type anyerror anyopaque comptime_int comptime_float");
MDV_WORDS(kZigConsts, L"true false null undefined");
MDV_WORDS(kDKw, L"abstract alias align asm assert auto body break case cast catch class const continue debug "
                L"default delegate delete deprecated do else enum export extern final finally for foreach "
                L"foreach_reverse function goto if immutable import in inout interface invariant is lazy "
                L"macro mixin module new nothrow out override package pragma private protected public pure "
                L"ref return scope shared static struct super switch synchronized template this throw try "
                L"typeid typeof union unittest version while with __gshared __traits __vector");
MDV_WORDS(kDTypes, L"bool byte ubyte short ushort int uint long ulong cent ucent char wchar dchar float double "
                   L"real ifloat idouble ireal cfloat cdouble creal void string wstring dstring size_t "
                   L"ptrdiff_t");
MDV_WORDS(kDConsts, L"true false null __FILE__ __LINE__ __MODULE__ __FUNCTION__");

// Shaders
MDV_WORDS(kGlslKw, L"attribute const uniform varying buffer shared coherent volatile restrict readonly "
                   L"writeonly layout centroid flat smooth noperspective patch sample break continue do for "
                   L"while switch case default if else subroutine in out inout invariant precise discard "
                   L"return lowp mediump highp precision struct");
MDV_WORDS(kGlslTypes, L"float double int void bool uint vec2 vec3 vec4 dvec2 dvec3 dvec4 bvec2 bvec3 bvec4 "
                      L"ivec2 ivec3 ivec4 uvec2 uvec3 uvec4 mat2 mat3 mat4 mat2x2 mat2x3 mat2x4 mat3x2 "
                      L"mat3x3 mat3x4 mat4x2 mat4x3 mat4x4 dmat2 dmat3 dmat4 sampler1D sampler2D sampler3D "
                      L"samplerCube sampler2DShadow samplerCubeShadow sampler2DArray sampler2DMS "
                      L"isampler2D usampler2D image2D image3D uimage2D iimage2D atomic_uint");
MDV_WORDS(kHlslKw, L"break case cbuffer const continue default discard do else extern for groupshared if in "
                   L"inline inout interface namespace nointerpolation out packoffset precise register return "
                   L"row_major column_major shared static struct switch tbuffer technique typedef uniform "
                   L"volatile while linear centroid noperspective sample triangle line point lineadj "
                   L"triangleadj class template");
MDV_WORDS(kHlslTypes, L"bool int uint dword half float double min16float min10float min16int min12int "
                      L"min16uint float1 float2 float3 float4 float2x2 float3x3 float4x4 float3x4 float4x3 "
                      L"int2 int3 int4 uint2 uint3 uint4 half2 half3 half4 double2 double3 double4 bool2 "
                      L"bool3 bool4 matrix vector Texture1D Texture2D Texture3D TextureCube Texture2DArray "
                      L"SamplerState SamplerComparisonState Buffer StructuredBuffer RWStructuredBuffer "
                      L"RWTexture2D RWTexture3D ByteAddressBuffer RWByteAddressBuffer "
                      L"AppendStructuredBuffer ConsumeStructuredBuffer RWBuffer void string");
MDV_WORDS(kWgslKw, L"alias break case const const_assert continue continuing default diagnostic discard else "
                   L"enable fn for if let loop override requires return struct switch var while");
MDV_WORDS(kWgslTypes, L"bool f16 f32 i32 u32 vec2 vec3 vec4 vec2f vec3f vec4f vec2i vec3i vec4i vec2u vec3u "
                      L"vec4u vec2h vec3h vec4h mat2x2 mat2x3 mat2x4 mat3x2 mat3x3 mat3x4 mat4x2 mat4x3 mat4x4 "
                      L"mat2x2f mat3x3f mat4x4f array atomic ptr sampler sampler_comparison texture_1d "
                      L"texture_2d texture_2d_array texture_3d texture_cube texture_storage_2d "
                      L"texture_depth_2d texture_multisampled_2d");
MDV_WORDS(kTrueFalse, L"true false");

// Solidity / Verilog / VHDL
MDV_WORDS(kSolKw, L"abstract anonymous as assembly break catch constant constructor continue contract delete "
                  L"do else emit enum event external fallback for function if immutable import indexed "
                  L"interface internal is library mapping memory modifier new override payable pragma "
                  L"private public pure receive return returns revert require storage calldata struct try "
                  L"type unchecked using view virtual while assert error");
MDV_WORDS(kSolTypes, L"address bool string bytes byte int uint int8 int16 int32 int64 int128 int256 uint8 "
                     L"uint16 uint32 uint64 uint128 uint256 bytes1 bytes2 bytes4 bytes8 bytes16 bytes20 "
                     L"bytes32 fixed ufixed");
MDV_WORDS(kSolConsts, L"true false wei gwei ether seconds minutes hours days weeks msg block tx now");
MDV_WORDS(kVerilogKw, L"always always_comb always_ff always_latch and assign automatic begin case casex casez "
                      L"cell config deassign default defparam design disable edge else end endcase endconfig "
                      L"endfunction endgenerate endmodule endprimitive endspecify endtable endtask event for "
                      L"force forever fork function generate genvar if ifnone initial inout input instance "
                      L"join join_any join_none large liblist library localparam macromodule medium module "
                      L"nand negedge nmos nor not notif0 notif1 or output parameter pmos posedge primitive "
                      L"pull0 pull1 pulldown pullup rcmos release repeat rnmos rpmos rtran rtranif0 rtranif1 "
                      L"scalared signed small specify specparam strong0 strong1 supply0 supply1 table task "
                      L"tran tranif0 tranif1 unsigned use vectored wait wand weak0 weak1 while wor xnor xor "
                      L"class endclass package endpackage interface endinterface import export typedef enum "
                      L"struct packed return break continue unique priority modport virtual extends new");
MDV_WORDS(kVerilogTypes, L"wire reg logic bit byte int integer longint shortint real realtime time tri tri0 "
                         L"tri1 triand trior trireg shortreal string void");
MDV_WORDS(kVhdlKw, L"abs access after alias all and architecture array assert attribute begin block body "
                   L"buffer bus case component configuration constant disconnect downto else elsif end "
                   L"entity exit file for function generate generic group guarded if impure in inertial "
                   L"inout is label library linkage literal loop map mod nand new next nor not null of on "
                   L"open or others out package port postponed procedure process pure range record register "
                   L"reject rem report return rol ror select severity signal shared sla sll sra srl subtype "
                   L"then to transport type unaffected units until use variable wait when while with xnor "
                   L"xor");
MDV_WORDS(kVhdlTypes, L"bit bit_vector boolean character integer natural positive real std_logic "
                      L"std_logic_vector std_ulogic std_ulogic_vector signed unsigned string time line text");

// JavaScript / TypeScript
MDV_WORDS(kJsKw, L"async await break case catch class const continue debugger default delete do else export "
                 L"extends finally for from function get if import in instanceof let new of return set static "
                 L"super switch this throw try typeof var void while with yield as");
MDV_WORDS(kJsTypes, L"Array ArrayBuffer Boolean DataView Date Error EvalError Float32Array Float64Array "
                    L"Function Int8Array Int16Array Int32Array Intl JSON Map Math Number Object Promise Proxy "
                    L"RangeError ReferenceError Reflect RegExp Set String Symbol SyntaxError TypeError "
                    L"Uint8Array Uint16Array Uint32Array Uint8ClampedArray WeakMap WeakSet WeakRef BigInt "
                    L"URL URLSearchParams");
MDV_WORDS(kJsConsts, L"true false null undefined NaN Infinity");
MDV_WORDS(kTsKw, L"async await break case catch class const continue debugger default delete do else export "
                 L"extends finally for from function get if import in instanceof let new of return set static "
                 L"super switch this throw try typeof var void while with yield as abstract asserts declare "
                 L"enum implements infer interface is keyof module namespace override private protected "
                 L"public readonly require satisfies type unique accessor out");
MDV_WORDS(kTsTypes, L"any bigint boolean never number object string symbol unknown");

// Python
MDV_WORDS(kPyKw, L"and as assert async await break class continue def del elif else except finally for from "
                 L"global if import in is lambda nonlocal not or pass raise return try while with yield match "
                 L"case self cls");
MDV_WORDS(kPyTypes, L"int float str bool bytes bytearray list dict set frozenset tuple object complex "
                    L"memoryview range type");
MDV_WORDS(kPyConsts, L"True False None NotImplemented Ellipsis __name__ __file__ __doc__");

// Ruby / Crystal / Elixir / Erlang
MDV_WORDS(kRubyKw, L"BEGIN END alias and begin break case class def defined? do else elsif end ensure for if "
                   L"in module next not or redo rescue retry return self super then undef unless until when "
                   L"while yield __method__ require require_relative include extend prepend attr_accessor "
                   L"attr_reader attr_writer private protected public raise lambda proc loop module_function");
MDV_WORDS(kRubyBuiltins, L"puts print p pp printf sprintf gets format");
MDV_WORDS(kRubyConsts, L"true false nil __FILE__ __LINE__ __dir__");
MDV_WORDS(kCrystalKw, L"abstract alias annotation as as? asm begin break case class def do else elsif end "
                      L"ensure enum extend for fun if in include instance_sizeof is_a? lib macro module next "
                      L"nil? of out pointerof private protected require rescue responds_to? return select self "
                      L"sizeof struct super then type typeof uninitialized union unless until verbatim when "
                      L"while with yield property getter setter");
MDV_WORDS(kElixirKw, L"after alias and case catch cond def defdelegate defexception defguard defimpl defmacro "
                     L"defmacrop defmodule defoverridable defp defprotocol defstruct do else end fn for if "
                     L"import in not or quote raise receive require reraise rescue super throw try unless "
                     L"unquote unquote_splicing use when with");
MDV_WORDS(kNilConsts, L"true false nil");
MDV_WORDS(kErlangKw, L"after and andalso band begin bnot bor bsl bsr bxor case catch cond div end fun if let "
                     L"not of or orelse receive rem try when xor maybe else");
MDV_WORDS(kErlangConsts, L"true false undefined ok error");

// PHP / Perl / Lua / R / Julia
MDV_WORDS(kPhpKw, L"abstract and array as break callable case catch class clone const continue declare "
                  L"default do echo else elseif empty enddeclare endfor endforeach endif endswitch endwhile "
                  L"enum eval exit extends final finally fn for foreach function global goto if implements "
                  L"include include_once instanceof insteadof interface isset list match namespace new or "
                  L"print private protected public readonly require require_once return static switch throw "
                  L"trait try unset use var while xor yield from parent self die");
MDV_WORDS(kPhpTypes, L"int float bool string void mixed never iterable object");
MDV_WORDS(kPhpConsts, L"true false null TRUE FALSE NULL __CLASS__ __DIR__ __FILE__ __FUNCTION__ __LINE__ "
                      L"__METHOD__ __NAMESPACE__ __TRAIT__ PHP_EOL PHP_VERSION PHP_INT_MAX E_ALL");
MDV_WORDS(kPerlKw, L"my our local sub if elsif else unless while until for foreach do last next redo return "
                   L"use no require package and or not eq ne lt gt le ge cmp x qw q qq m s tr y __END__ "
                   L"__DATA__ BEGIN END");
MDV_WORDS(kPerlBuiltins, L"print say die warn defined undef ref bless eval chomp chop split join push pop "
                         L"shift unshift keys values each exists delete scalar sort map grep reverse open "
                         L"close printf sprintf length substr index lc uc wantarray");
MDV_WORDS(kLuaKw, L"and break do else elseif end for function goto if in local not or repeat return then "
                  L"until while self");
MDV_WORDS(kLuaConsts, L"true false nil");
MDV_WORDS(kRKw, L"if else repeat while function for next break in return");
MDV_WORDS(kRConsts, L"TRUE FALSE NULL NA NA_integer_ NA_real_ NA_character_ NA_complex_ Inf NaN T F");
MDV_WORDS(kJuliaKw, L"baremodule begin break catch const continue do else elseif end export finally for "
                    L"function global if import in isa let local macro module mutable quote return struct try "
                    L"using where while abstract primitive type outer public");
MDV_WORDS(kJuliaConsts, L"true false nothing missing Inf NaN pi im");

// Haskell / F# / OCaml / Nim / Lisp family
MDV_WORDS(kHaskellKw, L"case class data default deriving do else foreign if import in infix infixl infixr "
                      L"instance let module newtype of then type where qualified as hiding forall mdo family "
                      L"proc rec");
MDV_WORDS(kHaskellConsts, L"True False Nothing Just Left Right LT GT EQ otherwise");
MDV_WORDS(kFsKw, L"abstract and as assert base begin class default delegate do done downcast downto elif else "
                 L"end exception extern finally fixed for fun function global if in inherit inline interface "
                 L"internal lazy let match member module mutable namespace new not of open or override "
                 L"private public rec return select sig static struct then to try type upcast use val void "
                 L"when while with yield");
MDV_WORDS(kFsTypes, L"bool byte sbyte int16 uint16 int uint32 int64 uint64 nativeint unativeint char string "
                    L"decimal unit float32 float double single obj list option seq array exn");
MDV_WORDS(kMlConsts, L"true false null None Some");
MDV_WORDS(kOCamlKw, L"and as assert asr begin class constraint do done downto else end exception external for "
                    L"fun function functor if in include inherit initializer land lazy let lor lsl lsr lxor "
                    L"match method mod module mutable new nonrec object of open or private rec sig struct "
                    L"then to try type val virtual when while with");
MDV_WORDS(kOCamlTypes, L"int float bool char string unit list array option ref bytes exn int32 int64 "
                       L"nativeint");
MDV_WORDS(kNimKw, L"addr and as asm bind block break case cast concept const continue converter defer discard "
                  L"distinct div do elif else end enum except export finally for from func if import in "
                  L"include interface is isnot iterator let macro method mixin mod not notin object of or out "
                  L"proc ptr raise ref return shl shr static template try tuple type using var when while xor "
                  L"yield");
MDV_WORDS(kNimTypes, L"int int8 int16 int32 int64 uint uint8 uint16 uint32 uint64 float float32 float64 bool "
                     L"char string cstring pointer seq array set void auto untyped typed openArray");
MDV_WORDS(kClojureKw, L"def defn defn- defmacro defmulti defmethod defprotocol defrecord deftype defonce fn fn* "
                      L"let letfn loop recur if if-not if-let if-some when when-not when-let when-some cond "
                      L"condp case do doseq dotimes for ns require import use try catch finally throw quote "
                      L"var new set! and or not binding delay future monitor-enter monitor-exit -> ->> as-> "
                      L"cond-> some-> doto");
MDV_WORDS(kLispKw, L"define define-syntax define-record-type define-values lambda let let* letrec letrec* "
                   L"let-values if cond case and or when unless begin do quote quasiquote unquote set! defun "
                   L"defmacro defvar defparameter defconst defcustom defgroup defstruct defclass defmethod "
                   L"defgeneric setq setf progn prog1 prog2 require provide module import export loop while "
                   L"dolist dotimes return return-from block flet labels interactive lambda* else "
                   L"with-open-file handler-case unwind-protect declare");
MDV_WORDS(kLispConsts, L"t nil true false #t #f #true #false");

// Fortran / VB / Pascal (case-insensitive: lower case)
MDV_WORDS(kFortranKw, L"program end module use implicit none parameter dimension allocatable intent in out "
                      L"inout subroutine function call return if then else elseif endif do enddo while "
                      L"select case default contains interface type public private pure elemental recursive "
                      L"result stop print write read open close format allocate deallocate associate block "
                      L"cycle exit goto continue where forall only save data common equivalence external "
                      L"intrinsic optional pointer target and or not eqv neqv eq ne lt le gt ge");
MDV_WORDS(kFortranTypes, L"integer real double precision complex logical character");
MDV_WORDS(kVbKw, L"addhandler addressof alias and andalso as byref byval call case catch cbool cbyte cchar "
                 L"cdate cdbl cdec cint class clng cobj const continue csbyte cshort csng cstr ctype cuint "
                 L"culng cushort declare default delegate dim directcast do each else elseif end endif enum "
                 L"erase error event exit finally for friend function get gettype global gosub goto handles "
                 L"if implements imports in inherits interface is isnot let lib like loop me mod module "
                 L"mustinherit mustoverride mybase myclass namespace narrowing new next not of on operator "
                 L"option optional or orelse overloads overridable overrides paramarray partial private "
                 L"property protected public raiseevent readonly redim rem removehandler resume return "
                 L"select set shadows shared static step stop structure sub synclock then throw to try "
                 L"trycast typeof using wend when while widening with withevents writeonly xor async await "
                 L"iterator yield explicit strict compare infer");
MDV_WORDS(kVbTypes, L"boolean byte char date decimal double integer long object sbyte short single string "
                    L"uinteger ulong ushort variant currency");
MDV_WORDS(kVbConsts, L"true false nothing null empty vbcrlf vbnullstring vbtab vbcr vblf vbnewline");
MDV_WORDS(kPascalKw, L"and array as asm begin case class const constructor destructor dispinterface div do "
                     L"downto else end except exports file finalization finally for function goto if "
                     L"implementation in inherited initialization inline interface is label library mod not "
                     L"object of or out packed procedure program property raise record repeat resourcestring "
                     L"set shl shr then threadvar to try type unit until uses var while with xor absolute "
                     L"abstract assembler cdecl default deprecated dynamic export external forward helper "
                     L"implements index message name nodefault overload override pascal platform private "
                     L"protected public published read readonly reintroduce register safecall sealed static "
                     L"stdcall stored strict unsafe varargs virtual winapi write writeonly operator "
                     L"specialize generic self result exit break continue on at");
MDV_WORDS(kPascalTypes, L"integer cardinal shortint smallint longint int64 byte word longword dword uint64 "
                        L"nativeint nativeuint boolean bytebool wordbool longbool char ansichar widechar string "
                        L"ansistring widestring unicodestring shortstring pchar pansichar pwidechar pointer "
                        L"real single double extended currency comp variant olevariant tobject tclass thandle "
                        L"hwnd lparam wparam lresult tdatetime text textfile");
MDV_WORDS(kPascalConsts, L"true false nil maxint");

// SQL (case-insensitive)
MDV_WORDS(kSqlKw, L"abort action add after all alter analyze and any as asc at authorization auto_increment "
                  L"autoincrement before begin between both break by call cascade case cast check checkpoint "
                  L"close cluster clustered collate column comment commit concurrently conflict constraint "
                  L"contains continue convert copy create cross cube current current_date current_time "
                  L"current_timestamp current_user cursor database databases day declare default deferrable "
                  L"deferred definer delete deny desc describe detach distinct do domain drop each else "
                  L"elsif end engine escape except exclude exclusive exec execute exists exit explain "
                  L"extension fetch first following for force foreign from full function functions global "
                  L"go goto grant group grouping having hour identity if ignore ilike immediate immutable "
                  L"in index inherits initially inner inout insert instead intersect into invoker is "
                  L"isnull join key kill language last lateral leading left like limit local lock loop "
                  L"materialized merge minute month natural next no not nothing notnull nowait nulls of "
                  L"off offset on only open option or order out outer over overwrite owner partition "
                  L"perform pivot plan pragma preceding precision prepare primary print procedure public "
                  L"raise raiserror range read recursive references referencing reindex release rename "
                  L"repeat replace restrict return returning returns revoke right role rollback rollup row "
                  L"rows rule savepoint schema second security select sequence session session_user set "
                  L"setof sets show similar some stable start statement stdin stdout strict system_user "
                  L"table tables tablespace temp temporary then ties to top trailing tran transaction "
                  L"trigger truncate try unbounded union unique unlogged unpivot until update usage use "
                  L"user using vacuum values variadic verbose view views volatile when where while "
                  L"window with within without work year zone waitfor nocount proc returns output");
MDV_WORDS(kSqlTypes, L"bigint bigserial binary bit blob bool boolean bytea char character cidr citext clob "
                     L"date datetime datetime2 datetimeoffset dec decimal double enum float image inet int "
                     L"int2 int4 int8 integer interval json jsonb longblob longtext macaddr mediumint "
                     L"mediumtext money nchar nclob ntext number numeric nvarchar nvarchar2 raw real serial "
                     L"smalldatetime smallint smallmoney smallserial text time timestamp timestamptz "
                     L"tinyint tinytext tsvector uniqueidentifier uuid varbinary varchar varchar2 varying "
                     L"xml year2 sql_variant rowversion hierarchyid geography geometry");
MDV_WORDS(kSqlConsts, L"true false null unknown");

// GraphQL / Protobuf / HCL / Nix / Nginx
MDV_WORDS(kGraphqlKw, L"query mutation subscription fragment on type input enum interface union scalar schema "
                      L"extend directive implements repeatable");
MDV_WORDS(kGraphqlTypes, L"Int Float String Boolean ID");
MDV_WORDS(kProtoKw, L"syntax edition import weak public package option message enum service rpc returns "
                    L"stream oneof map reserved extensions extend to max repeated optional required group");
MDV_WORDS(kProtoTypes, L"double float int32 int64 uint32 uint64 sint32 sint64 fixed32 fixed64 sfixed32 "
                       L"sfixed64 bool string bytes");
MDV_WORDS(kHclKw, L"resource data variable output module provider locals terraform backend "
                  L"required_providers for in if for_each count depends_on lifecycle dynamic content moved "
                  L"import check removed");
MDV_WORDS(kHclTypes, L"string number bool list map set object tuple any");
MDV_WORDS(kNixKw, L"assert else if in inherit let or rec then with import");
MDV_WORDS(kNginxKw, L"server location listen server_name root index proxy_pass proxy_set_header "
                    L"proxy_http_version include return rewrite if set upstream http events worker_processes "
                    L"worker_connections error_log access_log ssl_certificate ssl_certificate_key "
                    L"ssl_protocols ssl_ciphers try_files add_header gzip gzip_types client_max_body_size "
                    L"keepalive_timeout sendfile default_type types user pid alias expires break map "
                    L"fastcgi_pass fastcgi_param limit_req deny allow auth_basic");
MDV_WORDS(kOnOff, L"on off true false");

// Shell (bash etc.)
MDV_WORDS(kShKw, L"if then else elif fi for while until do done case esac function select in time coproc "
                 L"return local export readonly declare typeset unset shift exit break continue source alias "
                 L"unalias eval exec trap let");
MDV_WORDS(kShCmdKw, L"if then else elif do while until time coproc ! {");
MDV_WORDS(kShDeclKw, L"export local readonly declare typeset let");
MDV_WORDS(kShBuiltins, L"echo printf cd pwd ls cat grep egrep fgrep sed awk find xargs sort uniq head tail cut "
                       L"tr wc tee touch mkdir rmdir rm cp mv ln chmod chown chgrp tar gzip gunzip zip unzip "
                       L"curl wget git ssh scp rsync sudo su apt apt-get yum dnf pacman brew npm npx yarn pnpm "
                       L"node python python3 pip pip3 make cmake ninja gcc g++ clang docker kubectl systemctl "
                       L"service kill killall ps top test read set shopt which whereis man less more date "
                       L"sleep basename dirname realpath mktemp diff patch go cargo rustc dotnet java mvn "
                       L"gradle composer php ruby gem bundle perl vim nano tmux jq helm terraform ansible az "
                       L"aws gcloud openssl ping ip ifconfig netstat mount umount df du free uname hostname "
                       L"whoami id useradd usermod passwd crontab journalctl sh bash zsh true false wait jobs "
                       L"bg fg pushd popd dirs history type command builtin getopts umask ulimit env nohup "
                       L"code msbuild cl winget choco scoop pwsh powershell");

// PowerShell (case-insensitive)
MDV_WORDS(kPsKw, L"begin break catch class continue data define do dynamicparam else elseif end enum exit "
                 L"filter finally for foreach from function hidden if in param process return static switch "
                 L"throw trap try until using var while workflow parallel sequence inlinescript "
                 L"configuration default");
MDV_WORDS(kPsOps, L"eq ne gt ge lt le like notlike match notmatch contains notcontains in notin replace split "
                  L"join and or not xor band bor bxor bnot shl shr is isnot as f ceq cne cgt cge clt cle "
                  L"clike cnotlike cmatch cnotmatch ccontains creplace csplit ieq ine igt ige ilt ile ilike "
                  L"imatch ireplace isplit");

// Batch (case-insensitive)
MDV_WORDS(kBatKw, L"assoc break call cd chdir cls color copy date del dir echo endlocal erase exit for ftype "
                  L"goto if md mkdir mklink move path pause popd prompt pushd rd rem ren rename rmdir set "
                  L"setlocal shift start time title type ver verify vol do in else not exist defined "
                  L"errorlevel equ neq lss leq gtr geq nul con enabledelayedexpansion enableextensions "
                  L"disabledelayedexpansion disableextensions choice timeout where findstr find xcopy "
                  L"robocopy taskkill tasklist reg sc net");

// Dockerfile (case-insensitive)
MDV_WORDS(kDockerKw, L"from run cmd copy add env arg workdir expose entrypoint label user volume healthcheck "
                     L"shell onbuild stopsignal maintainer");

// Makefile
MDV_WORDS(kMakeKw, L"ifeq ifneq ifdef ifndef else endif include -include sinclude define endef export "
                   L"unexport override vpath undefine private .PHONY .SUFFIXES .DEFAULT .PRECIOUS "
                   L".INTERMEDIATE .SECONDARY .SECONDEXPANSION .DELETE_ON_ERROR .IGNORE .LOW_RESOLUTION_TIME "
                   L".SILENT .EXPORT_ALL_VARIABLES .NOTPARALLEL .ONESHELL .POSIX");

// CMake
MDV_WORDS(kCMakeKw, L"if elseif else endif foreach endforeach while endwhile function endfunction macro "
                    L"endmacro return break continue block endblock");
MDV_WORDS(kCMakeConsts, L"ON OFF TRUE FALSE YES NO Y N NOTFOUND IGNORE AND OR NOT DEFINED EQUAL LESS GREATER "
                        L"LESS_EQUAL GREATER_EQUAL STREQUAL STRLESS STRGREATER VERSION_LESS VERSION_EQUAL "
                        L"VERSION_GREATER VERSION_GREATER_EQUAL VERSION_LESS_EQUAL MATCHES EXISTS COMMAND "
                        L"TARGET POLICY TEST IS_DIRECTORY IS_ABSOLUTE IN_LIST PUBLIC PRIVATE INTERFACE "
                        L"REQUIRED COMPONENTS OPTIONAL_COMPONENTS VERSION STATUS WARNING FATAL_ERROR "
                        L"SEND_ERROR AUTHOR_WARNING DEPRECATION NOTICE VERBOSE DEBUG TRACE CHECK_START "
                        L"CHECK_PASS CHECK_FAIL CACHE STRING BOOL PATH FILEPATH INTERNAL FORCE PARENT_SCOPE "
                        L"STATIC SHARED MODULE OBJECT IMPORTED ALIAS GLOBAL EXCLUDE_FROM_ALL PROPERTIES "
                        L"DESTINATION TARGETS FILES DIRECTORY PROGRAMS RUNTIME LIBRARY ARCHIVE INCLUDES "
                        L"CONFIGURATIONS COMPONENT NAMES HINTS PATHS PATH_SUFFIXES NO_DEFAULT_PATH LANGUAGES "
                        L"DESCRIPTION HOMEPAGE_URL APPEND PREPEND GLOB GLOB_RECURSE COPY WRITE READ REMOVE "
                        L"MAKE_DIRECTORY RELATIVE_PATH CONFIGURE_DEPENDS QUIET CONFIG NO_MODULE BEFORE AFTER "
                        L"SYSTEM OUTPUT_VARIABLE RESULT_VARIABLE WORKING_DIRECTORY DEPENDS VERBATIM ALL "
                        L"USES_TERMINAL BYPRODUCTS OUTPUT MAIN_DEPENDENCY ENV GIT_REPOSITORY GIT_TAG URL "
                        L"URL_HASH SOURCES RANGE ITEMS LISTS ZIP_LISTS LENGTH GET FIND INSERT REMOVE_ITEM "
                        L"REMOVE_DUPLICATES SORT REVERSE JOIN TOUPPER TOLOWER REPLACE REGEX MATCH MATCHALL "
                        L"SUBSTRING STRIP CONCAT IN FILE_SET HEADERS BASE_DIRS NAMESPACE EXPORT");

// Assembly (case-insensitive)
MDV_WORDS(kAsmDirectives, L"section segment global globl extern bits default db dw dd dq dt do dy dz resb "
                          L"resw resd resq rest reso resy resz equ times align org use16 use32 use64 proc "
                          L"endp ends assume model include incbin struc endstruc istruc iend at macro endm "
                          L"local public byte word dword qword tbyte real4 real8 invoke end cpu");
MDV_WORDS(kAsmSizes, L"byte word dword qword tword tbyte oword xmmword ymmword zmmword ptr near far short "
                     L"offset flat rel");
MDV_WORDS(kAsmDataDirs, L"db dw dd dq dt do dy dz ddq resb resw resd resq rest reso resy resz equ times label "
                        L"proc endp struc struct segment ends macro endm sbyte sword sdword real4 real8 real10 "
                        L"fword");
MDV_WORDS(kAsmRegs, L"al ah ax eax rax bl bh bx ebx rbx cl ch cx ecx rcx dl dh dx edx rdx si esi rsi sil di "
                    L"edi rdi dil sp esp rsp spl bp ebp rbp bpl ip eip rip cs ds es fs gs ss lr pc fp xzr wzr "
                    L"cpsr apsr spsr fpscr nzcv");

// YAML / INI constants
MDV_WORDS(kYamlConsts, L"true True TRUE false False FALSE yes Yes YES no No NO on On ON off Off OFF null Null "
                       L"NULL ~");
MDV_WORDS(kIniConsts, L"true false yes no on off none null enabled disabled");

// ---------------------------------------------------------------------------
// Generic C-like scanner configuration
// ---------------------------------------------------------------------------
constexpr uint64_t F_HASH_PREPROC = 1ull << 0;   // '#' first on line: preprocessor directive
constexpr uint64_t F_AT_META = 1ull << 1;        // @Annotation / @decorator
constexpr uint64_t F_PASCAL_TYPE = 1ull << 2;    // PascalCase identifiers are types
constexpr uint64_t F_CALL = 1ull << 3;           // ident followed by '(' is a function
constexpr uint64_t F_DOLLAR_IDENT = 1ull << 4;   // '$' is an identifier char (JS, Java)
constexpr uint64_t F_DOLLAR_VAR = 1ull << 5;     // $name is a variable (PHP, GraphQL, nginx)
constexpr uint64_t F_RUBYISH = 1ull << 6;        // :sym, @ivar, method?/method! suffixes
constexpr uint64_t F_SQ_STRING = 1ull << 7;      // '...' string with backslash escapes
constexpr uint64_t F_SQ_CHAR = 1ull << 8;        // 'c' char literals only (Rust lifetimes, ML type vars)
constexpr uint64_t F_SQ_DOUBLING = 1ull << 9;    // '...' without escapes, '' = quote (SQL, Pascal)
constexpr uint64_t F_DQ_STRING = 1ull << 10;     // "..." strings
constexpr uint64_t F_MULTILINE = 1ull << 11;     // plain quoted strings may span lines
constexpr uint64_t F_DQ_NOESC = 1ull << 12;      // "..." without escapes, "" = quote (VB, Fortran)
constexpr uint64_t F_TRIPLE_DQ = 1ull << 13;     // """..."""
constexpr uint64_t F_TRIPLE_SQ = 1ull << 14;     // '''...'''
constexpr uint64_t F_PY_PREFIX = 1ull << 15;     // r"", b"", f"" ... prefixes
constexpr uint64_t F_BT_TEMPLATE = 1ull << 16;   // `...${expr}...` (JS)
constexpr uint64_t F_BT_RAW = 1ull << 17;        // `...` raw string (Go, D, shell-like)
constexpr uint64_t F_CPP_PREFIX = 1ull << 18;    // L"", u8"", R"delim(...)delim"
constexpr uint64_t F_CS_STRINGS = 1ull << 19;    // @"", $"", """raw"""
constexpr uint64_t F_RUST = 1ull << 20;          // r#""#, b"", lifetimes, macros!, #[attr]
constexpr uint64_t F_LUA_LONG = 1ull << 21;      // [[...]], --[[...]]
constexpr uint64_t F_REGEX = 1ull << 22;         // /regex/ literals
constexpr uint64_t F_CAPS_CONST = 1ull << 23;    // ALL_CAPS identifiers are constants
constexpr uint64_t F_CI = 1ull << 24;            // case-insensitive keywords
constexpr uint64_t F_NESTED = 1ull << 25;        // block comments nest
constexpr uint64_t F_PRIME_IDENT = 1ull << 26;   // x' identifiers, 'a type variables
constexpr uint64_t F_LISP = 1ull << 27;          // Lisp symbols; head of list is a function
constexpr uint64_t F_SYMBOL = 1ull << 28;        // :keyword constants
constexpr uint64_t F_PASCAL = 1ull << 29;        // {$directives}, #13 char codes, $FF hex
constexpr uint64_t F_VB = 1ull << 30;            // REM comments, &H hex
constexpr uint64_t F_SQL = 1ull << 31;           // @var :param $1, "ident", N'...'
constexpr uint64_t F_PHP = 1ull << 32;           // <?php ?>, #[attr]
constexpr uint64_t F_PERL = 1ull << 33;          // $x @x %x sigils
constexpr uint64_t F_INTERP_DOLLAR = 1ull << 34; // "$x ${expr}"
constexpr uint64_t F_INTERP_HASH = 1ull << 35;   // "#{expr}"
constexpr uint64_t F_INTERP_SWIFT = 1ull << 36;  // "\(expr)"
constexpr uint64_t F_SWIFT_RAW = 1ull << 37;     // #"..."#, #available
constexpr uint64_t F_NUM_QUOTE = 1ull << 38;     // 1'000'000
constexpr uint64_t F_OBJC = 1ull << 39;          // @interface, @"str"
constexpr uint64_t F_AT_BUILTIN = 1ull << 40;    // @builtin() functions (Zig)
constexpr uint64_t F_VERILOG = 1ull << 41;       // `define, $display, 8'hFF
constexpr uint64_t F_ERLANG = 1ull << 42;        // Variables, -module, ?MACRO
constexpr uint64_t F_DOT_IDENT = 1ull << 43;     // '.' inside identifiers (R)
constexpr uint64_t F_ASSIGN_PROP = 1ull << 44;   // ident followed by '=' is a property (HCL, Nix)
constexpr uint64_t F_DELPHI_T = 1ull << 45;      // TFoo / EFoo / IFoo are types
constexpr uint64_t F_HEREDOC = 1ull << 47;       // <<~EOS (Ruby, Crystal), <<"END" (Perl), <<<EOT (PHP)
constexpr uint64_t F_SQ_INTERP = 1ull << 46;     // single-quoted strings interpolate too (Dart)

struct LangDef {
    WordSet kw{}, types{}, consts{}, builtins{};
    wsv line1{}, line2{}, line3{};
    wsv blockOpen{}, blockClose{}, block2Open{}, block2Close{};
    uint64_t f = 0;
};

enum class Prev : uint8_t { None, Ident, Keyword, Value, Close, Op };
enum Interp : uint8_t { kIpNone, kIpDollar, kIpHash, kIpSwift, kIpBrace };

struct StrSpec {
    wchar_t quote = L'"';
    uint8_t qlen = 1;       // 1 or 3
    uint8_t hashes = 0;     // closing quote must be followed by this many '#'
    Interp interp = kIpNone;
    bool esc = true;        // backslash escapes
    bool multiline = false;
    bool doubling = false;  // doubled quote is a literal quote
    bool dollarVar = false; // $name inside is a variable
    K kind = K::String;
};

inline bool IsLispSym(wchar_t c) {
    switch (c) {
    case L'-': case L'!': case L'?': case L'*': case L'+': case L'<': case L'>': case L'=':
    case L'/': case L'%': case L'&': case L'.':
        return true;
    default:
        return false;
    }
}
inline bool IsPerlSpecial(wchar_t c) {
    switch (c) {
    case L'_': case L'0': case L'1': case L'2': case L'3': case L'4': case L'5': case L'6': case L'7':
    case L'8': case L'9': case L'@': case L'!': case L'$': case L'&': case L'/': case L'\\': case L',':
    case L';': case L'.': case L'<': case L'>': case L'^':
        return true;
    default:
        return false;
    }
}

// s[i] == '\''; Verilog based literal ('hFF, 's'b1010). Returns i if not one.
size_t VerilogBase(const wchar_t* s, size_t n, size_t i) {
    size_t j = i + 1;
    if (j < n && (s[j] == L's' || s[j] == L'S')) ++j;
    if (j >= n) return i;
    const wchar_t b = Lower(s[j]);
    if (b != L'b' && b != L'o' && b != L'd' && b != L'h') return i;
    ++j;
    const size_t st = j;
    while (j < n && (IsHex(s[j]) || s[j] == L'_' || Lower(s[j]) == L'x' || Lower(s[j]) == L'z' || s[j] == L'?'))
        ++j;
    return j > st ? j : i;
}

class CScanner {
public:
    CScanner(const wchar_t* s, size_t n, Emitter& em, const LangDef& d)
        : s_(s), n_(n), em_(em), d_(d), ci_((d.f & F_CI) != 0) {}
    void Run();

private:
    struct Frame {
        StrSpec spec;
        uint32_t depth;
    };
    static constexpr int kMaxFrames = 8;

    bool Has(uint64_t f) const { return (d_.f & f) != 0; }
    size_t Token(size_t i, bool bol);
    size_t TryComment(size_t i);
    size_t BlockEnd(size_t j, wsv open, wsv close) const;
    size_t LongBracketLevel(size_t i) const;
    size_t LongBracketEnd(size_t j, size_t level) const;
    size_t Preproc(size_t i);
    StrSpec DqSpec() const;
    size_t TryString(size_t i);
    size_t ScanStr(size_t seg, size_t i, const StrSpec& sp);
    bool Push(const StrSpec& sp);
    size_t CharLiteral(size_t i);
    size_t CppRaw(size_t i, size_t e);
    size_t PrefixedString(size_t i, size_t e);
    bool IsWordStart(wchar_t c) const;
    size_t Word(size_t i) const;
    size_t Ident(size_t i, bool lispHead);
    size_t Special(size_t i, bool bol);
    size_t Regex(size_t i);
    size_t Attribute(size_t i);
    size_t HeredocStart(size_t i);
    size_t Heredoc(size_t i);
    size_t Emit(size_t a, size_t b, K k, Prev p) {
        em_.Add(a, b, k);
        prev_ = p;
        return b;
    }

    const wchar_t* s_;
    size_t n_;
    Emitter& em_;
    const LangDef& d_;
    bool ci_;
    Prev prev_ = Prev::None;
    K pending_ = K::Plain;
    size_t noRegexUntil_ = 0;
    LineFinder lf_;
    Frame frames_[kMaxFrames] = {};
    int nf_ = 0;
    bool lispHead_ = false;
    bool nsCtx_ = false;  // after using/import/namespace: module path, leave plain
    // A heredoc opened on this line: its body starts on the next one.
    wchar_t hd_[64] = {};
    size_t hdLen_ = 0;
    bool hdPending_ = false, hdIndent_ = false;
};

void CScanner::Run() {
    size_t i = 0;
    bool bol = true;
    if (n_ >= 2 && s_[0] == L'#' && s_[1] == L'!' && !(n_ >= 3 && s_[2] == L'[')) {
        i = LineEnd(s_, n_, 0);
        em_.Add(0, i, K::Comment);
    }
    while (i < n_) {
        const wchar_t c = s_[i];
        if (c == L'\n') {
            ++i;
            bol = true;
            pending_ = K::Plain;
            nsCtx_ = false;
            while (nf_ > 0 && !frames_[nf_ - 1].spec.multiline) --nf_;
            if (hdPending_) {
                i = Heredoc(i);
                bol = false;
            }
            continue;
        }
        if (IsBlank(c)) {
            ++i;
            continue;
        }
        const bool atBol = bol;
        bol = false;
        const size_t next = Token(i, atBol);
        i = next > i ? next : i + 1;
    }
}

size_t CScanner::Token(size_t i, bool bol) {
    const wchar_t c = s_[i];
    const bool lispHead = lispHead_;
    lispHead_ = false;
    if (!IsWordStart(c)) pending_ = K::Plain;
    if (c == L';' || c == L'{' || c == L'(' || c == L'=') nsCtx_ = false;
    if (const size_t e = TryComment(i)) return e;
    if (c == L'#' && bol && Has(F_HASH_PREPROC)) return Preproc(i);
    if (const size_t e = TryString(i)) return e;
    if (c == L'<' && Has(F_HEREDOC) && i + 1 < n_ && s_[i + 1] == L'<') {
        if (const size_t e = HeredocStart(i)) return e;
    }
    const bool signedLispNum = Has(F_LISP) && (c == L'-' || c == L'+') && i + 1 < n_ && IsDigit(s_[i + 1]);
    if (IsDigit(c) || signedLispNum ||
        (c == L'.' && i + 1 < n_ && IsDigit(s_[i + 1]) && !(i > 0 && (IsIdChar(s_[i - 1]) || s_[i - 1] == L'.')))) {
        size_t e = ScanNumber(s_, n_, signedLispNum ? i + 1 : i, Has(F_NUM_QUOTE));
        if (Has(F_VERILOG) && e < n_ && s_[e] == L'\'') e = VerilogBase(s_, n_, e);
        return Emit(i, e, K::Number, Prev::Value);
    }
    if (IsWordStart(c)) return Ident(i, lispHead);
    if (const size_t e = Special(i, bol)) return e;
    if (nf_ > 0 && (c == L'{' || c == L'}' || c == L'(' || c == L')')) {
        Frame& f = frames_[nf_ - 1];
        const bool paren = f.spec.interp == kIpSwift;
        if (c == (paren ? L'(' : L'{')) {
            ++f.depth;
        } else if (c == (paren ? L')' : L'}')) {
            if (f.depth == 0) {
                const StrSpec sp = f.spec;
                --nf_;
                return ScanStr(i + 1, i + 1, sp);
            }
            --f.depth;
        }
    }
    if (c == L'(') {
        lispHead_ = Has(F_LISP);
        prev_ = Prev::Op;
    } else if (c == L')' || c == L']' || c == L'}') {
        prev_ = Prev::Close;
    } else {
        prev_ = Prev::Op;
    }
    return i + 1;
}

size_t CScanner::TryComment(size_t i) {
    const wchar_t c = s_[i];
    if (Has(F_PHP) && c == L'#' && i + 1 < n_ && s_[i + 1] == L'[') return 0;  // PHP 8 attribute
    for (int b = 0; b < 2; ++b) {
        const wsv open = b == 0 ? d_.blockOpen : d_.block2Open;
        const wsv close = b == 0 ? d_.blockClose : d_.block2Close;
        if (open.empty() || close.empty() || c != open[0] || !At(s_, n_, i, open)) continue;
        K kind = K::Comment;
        if (Has(F_PASCAL) && i + open.size() < n_ && s_[i + open.size()] == L'$') kind = K::Meta;
        const size_t e = BlockEnd(i + open.size(), open, close);
        em_.Add(i, e, kind);
        return e;
    }
    if (Has(F_LUA_LONG) && c == L'-' && At(s_, n_, i, L"--")) {
        if (const size_t lvl = LongBracketLevel(i + 2)) {
            const size_t e = LongBracketEnd(i + 2 + lvl + 1, lvl - 1);
            em_.Add(i, e, K::Comment);
            return e;
        }
    }
    const wsv lines[3] = {d_.line1, d_.line2, d_.line3};
    for (const wsv& p : lines) {
        if (!p.empty() && c == p[0] && At(s_, n_, i, p)) {
            const size_t e = LineEnd(s_, n_, i);
            em_.Add(i, e, K::Comment);
            return e;
        }
    }
    return 0;
}

size_t CScanner::BlockEnd(size_t j, wsv open, wsv close) const {
    if (!Has(F_NESTED)) {
        const size_t p = Find(s_, n_, j, close);
        return p == kNpos ? n_ : p + close.size();
    }
    size_t depth = 1;
    while (j < n_) {
        const wchar_t c = s_[j];
        if (c == close[0] && At(s_, n_, j, close)) {
            j += close.size();
            if (--depth == 0) return j;
            continue;
        }
        if (c == open[0] && At(s_, n_, j, open)) {
            j += open.size();
            ++depth;
            continue;
        }
        ++j;
    }
    return n_;
}

// "[[" / "[==[" at i: returns level + 1, else 0.
size_t CScanner::LongBracketLevel(size_t i) const {
    if (i >= n_ || s_[i] != L'[') return 0;
    size_t k = i + 1;
    while (k < n_ && s_[k] == L'=') ++k;
    if (k < n_ && s_[k] == L'[') return k - i;  // (k - i - 1) '=' signs, +1
    return 0;
}

size_t CScanner::LongBracketEnd(size_t j, size_t level) const {
    while (j < n_) {
        if (s_[j] == L']') {
            size_t k = j + 1;
            size_t eq = 0;
            while (k < n_ && s_[k] == L'=') {
                ++k;
                ++eq;
            }
            if (eq == level && k < n_ && s_[k] == L']') return k + 1;
            j = k;
            continue;
        }
        ++j;
    }
    return n_;
}

size_t CScanner::Preproc(size_t i) {
    const size_t j = SkipBlanks(s_, n_, i + 1);
    const size_t w = ScanIdent(s_, n_, j);
    em_.Add(i, w, K::Meta);
    prev_ = Prev::Op;
    const wsv word(s_ + j, w - j);
    if (word == L"include" || word == L"import" || word == L"include_next") {
        const size_t k = SkipBlanks(s_, n_, w);
        if (k < n_ && s_[k] == L'<') {
            const size_t gt = FindCh(s_, k + 1, LineEnd(s_, n_, k), L'>');
            if (gt != kNpos) return Emit(k, gt + 1, K::String, Prev::Value);
        }
    } else if (word == L"region" || word == L"endregion") {
        const size_t le = ContentEnd(s_, w, LineEnd(s_, n_, w));
        return Emit(i, le, K::Meta, Prev::Op);
    }
    return w;
}

StrSpec CScanner::DqSpec() const {
    StrSpec sp;
    sp.multiline = Has(F_MULTILINE);
    if (Has(F_DQ_NOESC)) {
        sp.esc = false;
        sp.doubling = true;
    }
    if (Has(F_INTERP_HASH)) {
        sp.interp = kIpHash;
    } else if (Has(F_INTERP_DOLLAR)) {
        sp.interp = kIpDollar;
        sp.dollarVar = true;
    } else if (Has(F_INTERP_SWIFT)) {
        sp.interp = kIpSwift;
    }
    if (Has(F_SQL)) {
        sp.kind = K::Plain;  // "quoted identifier"
        sp.esc = false;
        sp.doubling = true;
    }
    return sp;
}

size_t CScanner::TryString(size_t i) {
    const wchar_t c = s_[i];
    if (c == L'"' && Has(F_DQ_STRING)) {
        StrSpec sp = DqSpec();
        if (Has(F_TRIPLE_DQ) && At(s_, n_, i, L"\"\"\"")) {
            sp.qlen = 3;
            sp.multiline = true;
            sp.doubling = false;
            if (Has(F_CS_STRINGS)) {
                sp.esc = false;
                sp.interp = kIpNone;
            }
            return ScanStr(i, i + 3, sp);
        }
        return ScanStr(i, i + 1, sp);
    }
    if (c == L'\'') {
        StrSpec sp;
        sp.quote = L'\'';
        if (Has(F_SQ_INTERP)) {
            sp.interp = kIpDollar;
            sp.dollarVar = true;
        }
        if (Has(F_TRIPLE_SQ) && At(s_, n_, i, L"'''")) {
            sp.qlen = 3;
            sp.multiline = true;
            return ScanStr(i, i + 3, sp);
        }
        if (Has(F_SQ_STRING)) {
            sp.multiline = Has(F_MULTILINE);
            return ScanStr(i, i + 1, sp);
        }
        if (Has(F_SQ_DOUBLING)) {
            sp.esc = false;
            sp.doubling = true;
            sp.multiline = Has(F_MULTILINE);
            return ScanStr(i, i + 1, sp);
        }
        if (Has(F_SQ_CHAR)) return CharLiteral(i);
        return 0;
    }
    if (c == L'`') {
        if (Has(F_VERILOG) && i + 1 < n_ && IsIdStart(s_[i + 1]))
            return Emit(i, ScanIdent(s_, n_, i + 1), K::Meta, Prev::Op);
        StrSpec sp;
        sp.quote = L'`';
        sp.multiline = true;
        if (Has(F_BT_TEMPLATE)) {
            sp.interp = kIpDollar;
            return ScanStr(i, i + 1, sp);
        }
        if (Has(F_BT_RAW)) {
            sp.esc = false;
            return ScanStr(i, i + 1, sp);
        }
        // `quoted identifier` (Kotlin, Scala, SQL, R, Nim...): plain.
        const size_t q = lf_.Find(s_, n_, i + 1, L'`');
        if (q != kNpos) {
            prev_ = Prev::Ident;
            return q + 1;
        }
        return 0;
    }
    return 0;
}

bool CScanner::Push(const StrSpec& sp) {
    if (nf_ >= kMaxFrames) return false;
    frames_[nf_].spec = sp;
    frames_[nf_].depth = 0;
    ++nf_;
    return true;
}

// Scans a string body from i; [seg, i) is the already consumed opening part.
size_t CScanner::ScanStr(size_t seg, size_t i, const StrSpec& sp) {
    prev_ = Prev::Value;
    while (i < n_) {
        const wchar_t c = s_[i];
        if (c == L'\n' && !sp.multiline) {
            em_.Add(seg, i, sp.kind);
            return i;
        }
        if (c == sp.quote) {
            if (sp.doubling && sp.qlen == 1 && i + 1 < n_ && s_[i + 1] == sp.quote) {
                i += 2;
                continue;
            }
            size_t q = i + 1;
            if (sp.qlen == 3) {
                if (!(i + 2 < n_ && s_[i + 1] == c && s_[i + 2] == c)) {
                    ++i;
                    continue;
                }
                q = i + 3;
            }
            size_t h = 0;
            while (h < sp.hashes && q + h < n_ && s_[q + h] == L'#') ++h;
            if (h == sp.hashes) {
                em_.Add(seg, q + h, sp.kind);
                return q + h;
            }
            ++i;
            continue;
        }
        if (c == L'\\' && sp.esc) {
            if (sp.interp == kIpSwift && i + 1 < n_ && s_[i + 1] == L'(' && Push(sp)) {
                em_.Add(seg, i, sp.kind);
                prev_ = Prev::Op;
                return i + 2;
            }
            const size_t e = EscapeEnd(s_, n_, i);
            em_.Add(seg, i, sp.kind);
            em_.Add(i, e, sp.kind == K::String ? K::Escape : sp.kind);
            i = seg = e;
            continue;
        }
        if (sp.interp != kIpNone && sp.interp != kIpSwift) {
            size_t open = 0;  // length of the interpolation opener
            if (sp.interp == kIpDollar && c == L'$' && i + 1 < n_ && s_[i + 1] == L'{') {
                open = 2;
            } else if (sp.interp == kIpHash && c == L'#' && i + 1 < n_ && s_[i + 1] == L'{') {
                open = 2;
            } else if (sp.interp == kIpBrace && c == L'{') {
                if (i + 1 < n_ && s_[i + 1] == L'{') {
                    i += 2;
                    continue;
                }
                open = 1;
            }
            if (open != 0 && Push(sp)) {
                em_.Add(seg, i, sp.kind);
                prev_ = Prev::Op;
                return i + open;
            }
        }
        if (sp.dollarVar && c == L'$' && i + 1 < n_ && IsIdStart(s_[i + 1])) {
            const size_t e = ScanIdent(s_, n_, i + 1);
            em_.Add(seg, i, sp.kind);
            em_.Add(i, e, K::Variable);
            i = seg = e;
            continue;
        }
        ++i;
    }
    em_.Add(seg, n_, sp.kind);
    return n_;
}

// s_[i] == '\'' in languages where it is not a general string delimiter.
size_t CScanner::CharLiteral(size_t i) {
    const size_t j = i + 1;
    if (j >= n_) return 0;
    if (s_[j] == L'\\') {
        const size_t e = EscapeEnd(s_, n_, j);
        if (e < n_ && s_[e] == L'\'') {
            em_.Add(i, j, K::String);
            em_.Add(j, e, K::Escape);
            return Emit(e, e + 1, K::String, Prev::Value);
        }
        return 0;
    }
    size_t k = j + 1;
    if (IsHighSurrogate(s_[j]) && k < n_ && IsLowSurrogate(s_[k])) ++k;
    if (s_[j] != L'\'' && s_[j] != L'\n' && k < n_ && s_[k] == L'\'') return Emit(i, k + 1, K::String, Prev::Value);
    if (IsIdStart(s_[j]) && !(i > 0 && IsIdChar(s_[i - 1]))) {
        if (Has(F_RUST)) return Emit(i, ScanIdent(s_, n_, j), K::Keyword, Prev::Ident);   // lifetime / label
        if (Has(F_PRIME_IDENT)) return Emit(i, ScanIdent(s_, n_, j), K::Type, Prev::Ident);  // 'a type var
    }
    return 0;
}

size_t CScanner::CppRaw(size_t i, size_t e) {
    size_t d = e + 1;
    while (d < n_ && d - (e + 1) <= 16 && s_[d] != L'(' && s_[d] != L'"' && s_[d] != L'\\' && s_[d] != L')' &&
           !IsSpaceOrNl(s_[d]))
        ++d;
    if (d >= n_ || s_[d] != L'(' || d - (e + 1) > 16) {
        StrSpec sp;
        return ScanStr(i, e + 1, sp);
    }
    const size_t dl = d - (e + 1);
    const wchar_t* delim = s_ + e + 1;
    for (size_t j = d + 1; j < n_; ++j) {
        if (s_[j] == L')' && j + 1 + dl < n_ && s_[j + 1 + dl] == L'"' &&
            (dl == 0 || std::wmemcmp(s_ + j + 1, delim, dl) == 0))
            return Emit(i, j + 2 + dl, K::String, Prev::Value);
    }
    return Emit(i, n_, K::String, Prev::Value);
}

// [i, e) is an identifier directly followed by a quote (or '#').
size_t CScanner::PrefixedString(size_t i, size_t e) {
    const wchar_t q = s_[e];
    const size_t len = e - i;
    const wsv w(s_ + i, len);
    if (Has(F_PY_PREFIX) && len <= 3 && (q == L'"' || q == L'\'')) {
        bool raw = false, fmt = false, ok = true;
        for (wchar_t ch : w) {
            switch (Lower(ch)) {
            case L'r': raw = true; break;
            case L'f': case L't': fmt = true; break;
            case L'b': case L'u': break;
            default: ok = false; break;
            }
        }
        if (ok) {
            StrSpec sp;
            sp.quote = q;
            sp.esc = !raw;
            if (fmt) sp.interp = kIpBrace;
            if (e + 2 < n_ && s_[e + 1] == q && s_[e + 2] == q) {
                sp.qlen = 3;
                sp.multiline = true;
                return ScanStr(i, e + 3, sp);
            }
            return ScanStr(i, e + 1, sp);
        }
    }
    if (Has(F_CPP_PREFIX) && (q == L'"' || q == L'\'')) {
        if (q == L'"' && (w == L"R" || w == L"LR" || w == L"uR" || w == L"UR" || w == L"u8R")) return CppRaw(i, e);
        if (w == L"L" || w == L"u" || w == L"U" || w == L"u8") {
            StrSpec sp;
            sp.quote = q;
            return ScanStr(i, e + 1, sp);
        }
    }
    if (Has(F_RUST) && len <= 2) {
        if (w == L"r" || w == L"br" || w == L"cr") {
            size_t h = e;
            while (h < n_ && s_[h] == L'#' && h - e < 200) ++h;
            if (h < n_ && s_[h] == L'"') {
                StrSpec sp;
                sp.esc = false;
                sp.multiline = true;
                sp.hashes = static_cast<uint8_t>(h - e);
                return ScanStr(i, h + 1, sp);
            }
        } else if ((w == L"b" || w == L"c") && q == L'"') {
            StrSpec sp;
            sp.multiline = true;
            return ScanStr(i, e + 1, sp);
        } else if (w == L"b" && q == L'\'' && e + 1 < n_ &&
                   (s_[e + 1] == L'\\' || (e + 2 < n_ && s_[e + 2] == L'\''))) {
            em_.Add(i, e, K::String);
            const size_t r = CharLiteral(e);
            return r ? r : e;
        }
    }
    if (Has(F_SQL) && len == 1 && q == L'\'') {
        const wchar_t l = Lower(w[0]);
        if (l == L'n' || l == L'e' || l == L'b' || l == L'x') {
            StrSpec sp;
            sp.quote = q;
            sp.esc = false;
            sp.doubling = true;
            sp.multiline = true;
            return ScanStr(i, e + 1, sp);
        }
    }
    return 0;
}

bool CScanner::IsWordStart(wchar_t c) const {
    if (IsIdStart(c)) return true;
    if (c == L'$' && Has(F_DOLLAR_IDENT)) return true;
    return Has(F_LISP) && IsLispSym(c);
}

size_t CScanner::Word(size_t i) const {
    size_t j = i + 1;
    const bool lisp = Has(F_LISP), dollar = Has(F_DOLLAR_IDENT), prime = Has(F_PRIME_IDENT),
               dot = Has(F_DOT_IDENT);
    while (j < n_) {
        const wchar_t c = s_[j];
        if (IsIdChar(c) || (dollar && c == L'$') || (prime && c == L'\'') ||
            (lisp && (IsLispSym(c) || c == L':' || c == L'#')) ||
            (dot && c == L'.' && j + 1 < n_ && IsIdChar(s_[j + 1]))) {
            ++j;
            continue;
        }
        break;
    }
    return j;
}

size_t CScanner::Ident(size_t i, bool lispHead) {
    size_t e = Word(i);
    if (e < n_ && (s_[e] == L'"' || s_[e] == L'\'' || (s_[e] == L'#' && Has(F_RUST)))) {
        if (const size_t r = PrefixedString(i, e)) {
            pending_ = K::Plain;
            return r;
        }
    }
    if (Has(F_RUBYISH) && e < n_ && (s_[e] == L'?' || s_[e] == L'!') && !(e + 1 < n_ && s_[e + 1] == L'='))
        ++e;
    const wsv w(s_ + i, e - i);
    const bool afterDot = i > 0 && s_[i - 1] == L'.' && !(i > 1 && s_[i - 2] == L'.');
    const K pend = pending_;
    pending_ = K::Plain;
    K kind = K::Plain;
    bool isKw = !afterDot && InSet(d_.kw, w, ci_);
    if (isKw && InSet(kSoftWords, w, ci_)) {  // get(...) / set(...) are method names
        const size_t k = SkipBlanks(s_, n_, e);
        if (k < n_ && s_[k] == L'(') isKw = false;
    }
    if (isKw) {
        kind = K::Keyword;
        if (Has(F_VB) && EqCI(w.data(), w.size(), L"rem")) {
            const size_t le = LineEnd(s_, n_, i);
            return Emit(i, le, K::Comment, Prev::Op);
        }
        if (InSet(kDefWords, w, ci_)) pending_ = K::Function;
        else if (InSet(kTypeIntroWords, w, ci_)) pending_ = K::Type;
        else pending_ = pend;
        if (InSet(kNsWords, w, ci_)) nsCtx_ = true;
    } else if (pend != K::Plain) {
        kind = pend;
        // "procedure TFoo.Bar" / "def self.foo": qualifier is a type, the name follows.
        if (pend == K::Function && e + 1 < n_ && s_[e] == L'.' && IsIdStart(s_[e + 1])) {
            em_.Add(i, e, IsPascalCase(w) ? K::Type : K::Plain);
            pending_ = K::Function;
            prev_ = Prev::Op;
            return e + 1;  // skip the '.', the method name follows
        }
        // "new std::vector" / "new java.util.List": namespace qualifiers stay plain.
        if (pend == K::Type && e + 1 < n_ && (s_[e] == L'.' || At(s_, n_, e, L"::"))) {
            const size_t q = s_[e] == L'.' ? e + 1 : e + 2;
            if (q < n_ && IsIdStart(s_[q])) {
                pending_ = K::Type;
                prev_ = Prev::Op;
                return q;
            }
        }
    } else if (nsCtx_) {
        kind = K::Plain;  // import / using / namespace path
    } else if (!afterDot && InSet(d_.consts, w, ci_)) {
        kind = K::Constant;
    } else if (!afterDot && InSet(d_.types, w, ci_)) {
        kind = K::Type;
    } else if (!afterDot && InSet(d_.builtins, w, ci_)) {
        kind = K::Function;
    } else {
        if (Has(F_RUST) && e + 1 < n_ && s_[e] == L'!' &&
            (s_[e + 1] == L'(' || s_[e + 1] == L'[' || s_[e + 1] == L'{'))
            return Emit(i, e + 1, K::Function, Prev::Ident);
        const size_t k = SkipBlanks(s_, n_, e);
        const wchar_t nc = k < n_ ? s_[k] : L'\0';
        const bool call = Has(F_SQL) ? (e < n_ && s_[e] == L'(') : nc == L'(';  // SQL: "t (a, b)" is a table
        if (Has(F_PRIME_IDENT) && !afterDot && IsUpper(w[0])) kind = K::Type;  // ML/Haskell: types, constructors
        else if (Has(F_CALL) && call) kind = K::Function;
        else if (lispHead) kind = K::Function;
        else if (Has(F_CAPS_CONST) && IsAllCaps(w)) kind = K::Constant;
        else if (Has(F_PASCAL_TYPE) && !afterDot && IsPascalCase(w)) kind = K::Type;
        else if (Has(F_DELPHI_T) && w.size() >= 3 && (w[0] == L'T' || w[0] == L'E' || w[0] == L'I') &&
                 IsUpper(w[1]) && IsPascalCase(w))
            kind = K::Type;
        else if (Has(F_ERLANG) && (IsUpper(w[0]) || w[0] == L'_')) kind = K::Variable;
        else if (Has(F_ASSIGN_PROP) && nc == L'=' && !(k + 1 < n_ && (s_[k + 1] == L'=' || s_[k + 1] == L'>')))
            kind = K::Property;
    }
    return Emit(i, e, kind, isKw ? Prev::Keyword : Prev::Ident);
}

size_t CScanner::Regex(size_t i) {
    size_t j = i + 1;
    bool cls = false;
    while (j < n_) {
        const wchar_t c = s_[j];
        if (c == L'\n') break;
        if (c == L'\\') {
            // A regex literal ends with its line, an escaped line break too
            // (skipping it would scan the rest of the block from every line).
            if (j + 1 < n_ && s_[j + 1] == L'\n') break;
            j += 2;
            continue;
        }
        if (c == L'[') {
            cls = true;
        } else if (c == L']') {
            cls = false;
        } else if (c == L'/' && !cls) {
            ++j;
            while (j < n_ && IsAlpha(s_[j])) ++j;
            return Emit(i, j, K::Regex, Prev::Value);
        }
        ++j;
    }
    noRegexUntil_ = LineEnd(s_, n_, i);  // no closing '/' on this line: stop trying here
    return 0;
}

// <<~EOS, <<-EOS, <<EOS, <<'EOS', <<"EOS" (Ruby, Crystal, Perl); PHP:
// <<<EOT, <<<"EOT", <<<'EOT'. A bare marker only in capitals and right
// after the <<: `list << item` and `x <<y` append or shift.
size_t CScanner::HeredocStart(size_t i) {
    size_t j = i + 2;
    bool indent = false;
    if (Has(F_PHP)) {
        if (j >= n_ || s_[j] != L'<') return 0;
        j = SkipBlanks(s_, n_, j + 1);
        indent = true;  // PHP 7.3: the closing marker may be indented
    } else if (j < n_ && (s_[j] == L'~' || s_[j] == L'-')) {
        indent = true;
        ++j;
    }
    size_t a = j, b = j;
    if (j < n_ && (s_[j] == L'"' || s_[j] == L'\'')) {
        const size_t q = lf_.Find(s_, n_, j + 1, s_[j]);
        if (q == kNpos) return 0;
        a = j + 1;
        b = q;
        j = q + 1;
    } else {
        if (j >= n_ || !IsIdStart(s_[j])) return 0;
        b = ScanIdent(s_, n_, j);
        if (!Has(F_PHP)) {
            for (size_t k = a; k < b; ++k) {
                if (s_[k] >= L'a' && s_[k] <= L'z') return 0;
            }
        }
        j = b;
    }
    if (b <= a || b - a > std::size(hd_)) return 0;
    std::copy(s_ + a, s_ + b, hd_);
    hdLen_ = b - a;
    hdIndent_ = indent;
    hdPending_ = true;
    return Emit(i, j, K::String, Prev::Value);
}

// The body of a heredoc, from the start of the line after its opener, up
// to its marker alone on a line (PHP: then ; , or ) may follow).
size_t CScanner::Heredoc(size_t i) {
    hdPending_ = false;
    const wsv marker(hd_, hdLen_);
    for (size_t line = i; line < n_;) {
        const size_t le = LineEnd(s_, n_, line);
        const size_t k = hdIndent_ ? SkipBlanks(s_, n_, line) : line;
        const size_t ce = ContentEnd(s_, k, le);
        if (ce - k >= hdLen_ && wsv(s_ + k, hdLen_) == marker &&
            (k + hdLen_ == ce || (Has(F_PHP) && !IsIdChar(s_[k + hdLen_])))) {
            if (line > i) em_.Add(i, line, K::String);
            return Emit(k, k + hdLen_, K::String, Prev::Value);
        }
        line = le < n_ ? le + 1 : n_;
    }
    em_.Add(i, n_, K::String);  // no marker: to the end
    return n_;
}

// #[attr] / #![attr]
size_t CScanner::Attribute(size_t i) {
    size_t j = i + 1;
    if (j < n_ && s_[j] == L'!') ++j;
    size_t depth = 0;
    while (j < n_ && s_[j] != L'\n') {
        const wchar_t c = s_[j++];
        if (c == L'[') {
            ++depth;
        } else if (c == L']') {
            if (depth <= 1) break;
            --depth;
        }
    }
    return Emit(i, j, K::Meta, Prev::Op);
}

size_t CScanner::Special(size_t i, bool bol) {
    const wchar_t c = s_[i];
    const wchar_t c1 = i + 1 < n_ ? s_[i + 1] : L'\0';
    const wchar_t c2 = i + 2 < n_ ? s_[i + 2] : L'\0';
    switch (c) {
    case L'@': {
        if (Has(F_CS_STRINGS)) {
            StrSpec sp;
            sp.esc = false;
            sp.doubling = true;
            sp.multiline = true;
            if (c1 == L'"') return ScanStr(i, i + 2, sp);
            if (c1 == L'$' && c2 == L'"') {
                sp.interp = kIpBrace;
                return ScanStr(i, i + 3, sp);
            }
            if (IsIdStart(c1)) {
                prev_ = Prev::Ident;
                return ScanIdent(s_, n_, i + 1);  // @verbatim identifier
            }
        }
        if (Has(F_OBJC)) {
            if (c1 == L'"') {
                StrSpec sp;
                return ScanStr(i, i + 2, sp);
            }
            if (IsIdStart(c1)) return Emit(i, ScanIdent(s_, n_, i + 1), K::Keyword, Prev::Keyword);
        }
        if (Has(F_RUBYISH | F_PERL | F_SQL) && (IsIdStart(c1) || (c1 == L'@' && IsIdStart(c2))))
            return Emit(i, ScanIdent(s_, n_, c1 == L'@' ? i + 2 : i + 1), K::Variable, Prev::Value);
        if (!IsIdStart(c1)) break;
        if (Has(F_AT_BUILTIN)) return Emit(i, ScanIdent(s_, n_, i + 1), K::Function, Prev::Ident);
        if (Has(F_AT_META) && !(i > 0 && IsIdChar(s_[i - 1]))) {
            size_t e = ScanIdent(s_, n_, i + 1);
            while (e + 1 < n_ && s_[e] == L'.' && IsIdStart(s_[e + 1])) e = ScanIdent(s_, n_, e + 1);
            return Emit(i, e, K::Meta, Prev::Op);
        }
        break;
    }
    case L'$': {
        if (Has(F_CS_STRINGS)) {
            size_t j = i;
            while (j < n_ && s_[j] == L'$' && j - i < 8) ++j;
            StrSpec sp;
            sp.interp = kIpBrace;
            if (j + 1 < n_ && s_[j] == L'@' && s_[j + 1] == L'"') {
                sp.esc = false;
                sp.doubling = true;
                sp.multiline = true;
                return ScanStr(i, j + 2, sp);
            }
            if (j < n_ && s_[j] == L'"') {
                if (At(s_, n_, j, L"\"\"\"")) {
                    sp.qlen = 3;
                    sp.esc = false;
                    sp.multiline = true;
                    return ScanStr(i, j + 3, sp);
                }
                return ScanStr(i, j + 1, sp);
            }
        }
        if (Has(F_DOLLAR_VAR | F_PERL)) {
            if (IsIdStart(c1)) return Emit(i, ScanIdent(s_, n_, i + 1), K::Variable, Prev::Value);
            if (c1 == L'$' && IsIdStart(c2)) return Emit(i, ScanIdent(s_, n_, i + 2), K::Variable, Prev::Value);
            if (Has(F_PERL)) {
                if (c1 == L'#' && IsIdStart(c2)) return Emit(i, ScanIdent(s_, n_, i + 2), K::Variable, Prev::Value);
                if (c1 == L'{') {
                    const size_t cb = lf_.Find(s_, n_, i + 2, L'}');
                    if (cb != kNpos) return Emit(i, cb + 1, K::Variable, Prev::Value);
                }
                if (IsPerlSpecial(c1)) return Emit(i, i + 2, K::Variable, Prev::Value);
            }
        }
        if (Has(F_PASCAL) && IsHex(c1)) {
            size_t e = i + 1;
            while (e < n_ && IsHex(s_[e])) ++e;
            return Emit(i, e, K::Number, Prev::Value);
        }
        if (Has(F_SQL) && IsDigit(c1)) {
            size_t e = i + 1;
            while (e < n_ && IsDigit(s_[e])) ++e;
            return Emit(i, e, K::Variable, Prev::Value);
        }
        if (Has(F_VERILOG) && IsIdStart(c1)) return Emit(i, ScanIdent(s_, n_, i + 1), K::Function, Prev::Ident);
        break;
    }
    case L'#': {
        if (Has(F_RUST | F_PHP) && (c1 == L'[' || (c1 == L'!' && c2 == L'['))) return Attribute(i);
        if (Has(F_SWIFT_RAW)) {
            size_t j = i;
            while (j < n_ && s_[j] == L'#' && j - i < 200) ++j;
            if (j < n_ && s_[j] == L'"') {
                StrSpec sp;
                sp.esc = false;
                sp.hashes = static_cast<uint8_t>(j - i);
                if (At(s_, n_, j, L"\"\"\"")) {
                    sp.qlen = 3;
                    sp.multiline = true;
                    return ScanStr(i, j + 3, sp);
                }
                return ScanStr(i, j + 1, sp);
            }
            if (IsIdStart(c1)) return Emit(i, ScanIdent(s_, n_, i + 1), K::Meta, Prev::Op);
        }
        if (Has(F_PASCAL) && (IsDigit(c1) || c1 == L'$')) {
            size_t e = i;
            while (e < n_ && s_[e] == L'#') {
                size_t k = e + 1;
                if (k < n_ && s_[k] == L'$') {
                    ++k;
                    while (k < n_ && IsHex(s_[k])) ++k;
                } else {
                    while (k < n_ && IsDigit(s_[k])) ++k;
                }
                if (k <= e + 1 || (k == e + 2 && s_[e + 1] == L'$')) break;
                e = k;
            }
            if (e > i) return Emit(i, e, K::String, Prev::Value);
        }
        if (Has(F_LISP)) {
            if (c1 == L'\\' && i + 2 < n_) return Emit(i, ScanIdent(s_, n_, i + 3), K::String, Prev::Value);
            if (IsAlpha(c1)) {
                const size_t e = ScanIdent(s_, n_, i + 1);
                const wsv w(s_ + i, e - i);
                if (w == L"#t" || w == L"#f" || w == L"#true" || w == L"#false")
                    return Emit(i, e, K::Constant, Prev::Value);
            }
        }
        break;
    }
    case L':':
        if (Has(F_SYMBOL | F_RUBYISH) && IsIdStart(c1) && !(i > 0 && (s_[i - 1] == L':' || IsIdChar(s_[i - 1])))) {
            size_t e = Has(F_LISP) ? Word(i + 1) : ScanIdent(s_, n_, i + 1);
            if (Has(F_RUBYISH) && e < n_ && (s_[e] == L'?' || s_[e] == L'!')) ++e;
            return Emit(i, e, K::Constant, Prev::Value);
        }
        if (Has(F_SQL) && IsIdStart(c1) && !(i > 0 && s_[i - 1] == L':'))
            return Emit(i, ScanIdent(s_, n_, i + 1), K::Variable, Prev::Value);
        break;
    case L'/':
        if (Has(F_REGEX) && i >= noRegexUntil_ &&
            (bol || prev_ == Prev::None || prev_ == Prev::Op || prev_ == Prev::Keyword))
            return Regex(i);
        break;
    case L'[':
        if (Has(F_LUA_LONG)) {
            if (const size_t lvl = LongBracketLevel(i))
                return Emit(i, LongBracketEnd(i + lvl + 1, lvl - 1), K::String, Prev::Value);
        }
        break;
    case L'%':
        if (Has(F_PERL) && IsIdStart(c1) && (prev_ == Prev::Op || prev_ == Prev::None || prev_ == Prev::Keyword))
            return Emit(i, ScanIdent(s_, n_, i + 1), K::Variable, Prev::Value);
        break;
    case L'\'':
        if (Has(F_VERILOG)) {
            const size_t e = VerilogBase(s_, n_, i);
            if (e > i) return Emit(i, e, K::Number, Prev::Value);
        }
        break;
    case L'?':
        if (Has(F_ERLANG) && IsIdStart(c1)) return Emit(i, ScanIdent(s_, n_, i + 1), K::Meta, Prev::Value);
        if (Has(F_PHP) && c1 == L'>') return Emit(i, i + 2, K::Meta, Prev::Op);
        break;
    case L'-':
        if (Has(F_ERLANG) && bol && IsLower(c1)) return Emit(i, ScanIdent(s_, n_, i + 1), K::Meta, Prev::Op);
        break;
    case L'&':
        if (Has(F_VB) && (c1 == L'H' || c1 == L'h' || c1 == L'O' || c1 == L'o') && IsHex(c2)) {
            size_t e = i + 2;
            while (e < n_ && (IsHex(s_[e]) || s_[e] == L'&')) ++e;
            return Emit(i, e, K::Number, Prev::Value);
        }
        break;
    case L'<':
        if (Has(F_PHP) && c1 == L'?') {
            size_t e = i + 2;
            if (AtCI(s_, n_, e, L"php")) e += 3;
            else if (e < n_ && s_[e] == L'=') ++e;
            return Emit(i, e, K::Meta, Prev::Op);
        }
        break;
    default:
        break;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Language definitions for the generic scanner
// ---------------------------------------------------------------------------
constexpr uint64_t kCFam = F_CALL | F_SQ_STRING | F_DQ_STRING;

constexpr LangDef kLangC{.kw = kCKw, .types = kCTypes, .consts = kCConsts, .line1 = L"//", .blockOpen = L"/*",
                         .blockClose = L"*/",
                         .f = kCFam | F_HASH_PREPROC | F_CAPS_CONST | F_CPP_PREFIX | F_NUM_QUOTE};
constexpr LangDef kLangCpp{.kw = kCppKw, .types = kCppTypes, .consts = kCppConsts, .line1 = L"//",
                           .blockOpen = L"/*", .blockClose = L"*/",
                           .f = kCFam | F_HASH_PREPROC | F_CAPS_CONST | F_CPP_PREFIX | F_NUM_QUOTE | F_PASCAL_TYPE};
constexpr LangDef kLangObjC{.kw = kObjCKw, .types = kObjCTypes, .consts = kObjCConsts, .line1 = L"//",
                            .blockOpen = L"/*", .blockClose = L"*/",
                            .f = kCFam | F_HASH_PREPROC | F_OBJC | F_PASCAL_TYPE | F_CAPS_CONST | F_CPP_PREFIX};
constexpr LangDef kLangCs{.kw = kCsKw, .types = kCsTypes, .consts = kNullConsts, .line1 = L"//",
                          .blockOpen = L"/*", .blockClose = L"*/",
                          .f = kCFam | F_HASH_PREPROC | F_CS_STRINGS | F_TRIPLE_DQ | F_PASCAL_TYPE};
constexpr LangDef kLangJava{.kw = kJavaKw, .types = kJavaTypes, .consts = kNullConsts, .line1 = L"//",
                            .blockOpen = L"/*", .blockClose = L"*/",
                            .f = kCFam | F_AT_META | F_TRIPLE_DQ | F_PASCAL_TYPE | F_CAPS_CONST | F_DOLLAR_IDENT};
constexpr LangDef kLangKotlin{.kw = kKotlinKw, .types = kKotlinTypes, .consts = kNullConsts, .line1 = L"//",
                              .blockOpen = L"/*", .blockClose = L"*/",
                              .f = kCFam | F_AT_META | F_TRIPLE_DQ | F_INTERP_DOLLAR | F_NESTED | F_PASCAL_TYPE |
                                   F_CAPS_CONST};
constexpr LangDef kLangScala{.kw = kScalaKw, .types = kScalaTypes, .consts = kScalaConsts, .line1 = L"//",
                             .blockOpen = L"/*", .blockClose = L"*/",
                             .f = F_CALL | F_DQ_STRING | F_SQ_CHAR | F_AT_META | F_TRIPLE_DQ | F_INTERP_DOLLAR |
                                  F_NESTED | F_PASCAL_TYPE};
constexpr LangDef kLangGroovy{.kw = kGroovyKw, .types = kGroovyTypes, .consts = kNullConsts, .line1 = L"//",
                              .blockOpen = L"/*", .blockClose = L"*/",
                              .f = kCFam | F_AT_META | F_TRIPLE_DQ | F_TRIPLE_SQ | F_INTERP_DOLLAR |
                                   F_PASCAL_TYPE | F_CAPS_CONST};
constexpr LangDef kLangDart{.kw = kDartKw, .types = kDartTypes, .consts = kNullConsts, .line1 = L"//",
                            .blockOpen = L"/*", .blockClose = L"*/",
                            .f = kCFam | F_AT_META | F_TRIPLE_DQ | F_TRIPLE_SQ | F_INTERP_DOLLAR | F_SQ_INTERP |
                                 F_PY_PREFIX | F_NESTED | F_PASCAL_TYPE};
constexpr LangDef kLangSwift{.kw = kSwiftKw, .types = kSwiftTypes, .consts = kSwiftConsts, .line1 = L"//",
                             .blockOpen = L"/*", .blockClose = L"*/",
                             .f = F_CALL | F_DQ_STRING | F_TRIPLE_DQ | F_INTERP_SWIFT | F_SWIFT_RAW |
                                  F_HASH_PREPROC | F_AT_META | F_NESTED | F_PASCAL_TYPE};
constexpr LangDef kLangGo{.kw = kGoKw, .types = kGoTypes, .consts = kGoConsts, .line1 = L"//", .blockOpen = L"/*",
                          .blockClose = L"*/", .f = F_CALL | F_DQ_STRING | F_SQ_CHAR | F_BT_RAW};
constexpr LangDef kLangRust{.kw = kRustKw, .types = kRustTypes, .consts = kRustConsts, .line1 = L"//",
                            .blockOpen = L"/*", .blockClose = L"*/",
                            .f = F_CALL | F_DQ_STRING | F_MULTILINE | F_SQ_CHAR | F_RUST | F_NESTED |
                                 F_PASCAL_TYPE | F_CAPS_CONST};
constexpr LangDef kLangZig{.kw = kZigKw, .types = kZigTypes, .consts = kZigConsts, .line1 = L"//",
                           .f = F_CALL | F_DQ_STRING | F_SQ_CHAR | F_AT_BUILTIN | F_PASCAL_TYPE};
constexpr LangDef kLangD{.kw = kDKw, .types = kDTypes, .consts = kDConsts, .line1 = L"//", .blockOpen = L"/*",
                         .blockClose = L"*/", .block2Open = L"/+", .block2Close = L"+/",
                         .f = F_CALL | F_DQ_STRING | F_MULTILINE | F_SQ_CHAR | F_BT_RAW | F_AT_META | F_NESTED |
                              F_PASCAL_TYPE};
constexpr LangDef kLangGlsl{.kw = kGlslKw, .types = kGlslTypes, .consts = kTrueFalse, .line1 = L"//",
                            .blockOpen = L"/*", .blockClose = L"*/",
                            .f = F_CALL | F_HASH_PREPROC | F_DQ_STRING | F_CAPS_CONST};
constexpr LangDef kLangHlsl{.kw = kHlslKw, .types = kHlslTypes, .consts = kTrueFalse, .line1 = L"//",
                            .blockOpen = L"/*", .blockClose = L"*/",
                            .f = F_CALL | F_HASH_PREPROC | F_DQ_STRING | F_CAPS_CONST};
constexpr LangDef kLangWgsl{.kw = kWgslKw, .types = kWgslTypes, .consts = kTrueFalse, .line1 = L"//",
                            .blockOpen = L"/*", .blockClose = L"*/",
                            .f = F_CALL | F_AT_META | F_NESTED | F_PASCAL_TYPE};
constexpr LangDef kLangSolidity{.kw = kSolKw, .types = kSolTypes, .consts = kSolConsts, .line1 = L"//",
                                .blockOpen = L"/*", .blockClose = L"*/",
                                .f = kCFam | F_PASCAL_TYPE | F_CAPS_CONST};
constexpr LangDef kLangVerilog{.kw = kVerilogKw, .types = kVerilogTypes, .line1 = L"//", .blockOpen = L"/*",
                               .blockClose = L"*/", .f = F_CALL | F_DQ_STRING | F_VERILOG};
constexpr LangDef kLangVhdl{.kw = kVhdlKw, .types = kVhdlTypes, .consts = kTrueFalse, .line1 = L"--",
                            .blockOpen = L"/*", .blockClose = L"*/",
                            .f = F_CI | F_CALL | F_DQ_STRING | F_DQ_NOESC | F_SQ_CHAR};
constexpr LangDef kLangJs{.kw = kJsKw, .types = kJsTypes, .consts = kJsConsts, .line1 = L"//", .blockOpen = L"/*",
                          .blockClose = L"*/",
                          .f = kCFam | F_BT_TEMPLATE | F_REGEX | F_DOLLAR_IDENT | F_CAPS_CONST | F_AT_META};
constexpr LangDef kLangTs{.kw = kTsKw, .types = kTsTypes, .consts = kJsConsts, .line1 = L"//", .blockOpen = L"/*",
                          .blockClose = L"*/",
                          .f = kCFam | F_BT_TEMPLATE | F_REGEX | F_DOLLAR_IDENT | F_CAPS_CONST | F_AT_META |
                               F_PASCAL_TYPE};
constexpr LangDef kLangPython{.kw = kPyKw, .types = kPyTypes, .consts = kPyConsts, .line1 = L"#",
                              .f = kCFam | F_TRIPLE_DQ | F_TRIPLE_SQ | F_PY_PREFIX | F_AT_META | F_CAPS_CONST};
constexpr LangDef kLangRuby{.kw = kRubyKw, .consts = kRubyConsts, .builtins = kRubyBuiltins, .line1 = L"#",
                            .blockOpen = L"=begin", .blockClose = L"=end",
                            .f = kCFam | F_MULTILINE | F_INTERP_HASH | F_RUBYISH | F_REGEX | F_BT_RAW |
                                 F_PASCAL_TYPE | F_CAPS_CONST | F_HEREDOC};
constexpr LangDef kLangCrystal{.kw = kCrystalKw, .consts = kNilConsts, .line1 = L"#",
                               .f = F_CALL | F_DQ_STRING | F_SQ_CHAR | F_MULTILINE | F_INTERP_HASH | F_RUBYISH |
                                    F_REGEX | F_PASCAL_TYPE | F_CAPS_CONST | F_HEREDOC};
constexpr LangDef kLangElixir{.kw = kElixirKw, .consts = kNilConsts, .line1 = L"#",
                              .f = kCFam | F_MULTILINE | F_TRIPLE_DQ | F_INTERP_HASH | F_RUBYISH | F_PASCAL_TYPE};
constexpr LangDef kLangErlang{.kw = kErlangKw, .consts = kErlangConsts, .line1 = L"%", .f = kCFam | F_ERLANG};
constexpr LangDef kLangPhp{.kw = kPhpKw, .types = kPhpTypes, .consts = kPhpConsts, .line1 = L"//", .line2 = L"#",
                           .blockOpen = L"/*", .blockClose = L"*/",
                           .f = kCFam | F_MULTILINE | F_INTERP_DOLLAR | F_DOLLAR_VAR | F_PHP | F_PASCAL_TYPE |
                                F_CAPS_CONST | F_HEREDOC};
constexpr LangDef kLangPerl{.kw = kPerlKw, .builtins = kPerlBuiltins, .line1 = L"#", .blockOpen = L"=pod",
                            .blockClose = L"=cut",
                            .f = kCFam | F_MULTILINE | F_INTERP_DOLLAR | F_PERL | F_REGEX | F_BT_RAW | F_HEREDOC};
constexpr LangDef kLangLua{.kw = kLuaKw, .consts = kLuaConsts, .line1 = L"--", .f = kCFam | F_LUA_LONG};
constexpr LangDef kLangR{.kw = kRKw, .consts = kRConsts, .line1 = L"#", .f = kCFam | F_MULTILINE | F_DOT_IDENT};
constexpr LangDef kLangJulia{.kw = kJuliaKw, .consts = kJuliaConsts, .line1 = L"#", .blockOpen = L"#=",
                             .blockClose = L"=#",
                             .f = F_CALL | F_DQ_STRING | F_TRIPLE_DQ | F_INTERP_DOLLAR | F_SQ_CHAR | F_AT_META |
                                  F_NESTED | F_PASCAL_TYPE | F_BT_RAW};
constexpr LangDef kLangHaskell{.kw = kHaskellKw, .consts = kHaskellConsts, .line1 = L"--", .blockOpen = L"{-",
                               .blockClose = L"-}",
                               .f = F_CALL | F_DQ_STRING | F_SQ_CHAR | F_PRIME_IDENT | F_NESTED | F_PASCAL_TYPE};
constexpr LangDef kLangFSharp{.kw = kFsKw, .types = kFsTypes, .consts = kMlConsts, .line1 = L"//",
                              .blockOpen = L"(*", .blockClose = L"*)",
                              .f = F_CALL | F_DQ_STRING | F_TRIPLE_DQ | F_CS_STRINGS | F_SQ_CHAR | F_PRIME_IDENT |
                                   F_HASH_PREPROC | F_PASCAL_TYPE};
constexpr LangDef kLangOCaml{.kw = kOCamlKw, .types = kOCamlTypes, .consts = kMlConsts, .blockOpen = L"(*",
                             .blockClose = L"*)",
                             .f = F_CALL | F_DQ_STRING | F_MULTILINE | F_SQ_CHAR | F_PRIME_IDENT | F_NESTED |
                                  F_PASCAL_TYPE};
constexpr LangDef kLangNim{.kw = kNimKw, .types = kNimTypes, .consts = kNilConsts, .line1 = L"#",
                           .blockOpen = L"#[", .blockClose = L"]#",
                           .f = F_CALL | F_DQ_STRING | F_TRIPLE_DQ | F_PY_PREFIX | F_SQ_CHAR | F_NESTED |
                                F_PASCAL_TYPE};
constexpr LangDef kLangClojure{.kw = kClojureKw, .consts = kNilConsts, .line1 = L";",
                               .f = F_LISP | F_SYMBOL | F_DQ_STRING | F_MULTILINE};
constexpr LangDef kLangLisp{.kw = kLispKw, .consts = kLispConsts, .line1 = L";", .blockOpen = L"#|",
                            .blockClose = L"|#", .f = F_LISP | F_SYMBOL | F_DQ_STRING | F_MULTILINE | F_NESTED};
constexpr LangDef kLangFortran{.kw = kFortranKw, .types = kFortranTypes, .consts = kTrueFalse, .line1 = L"!",
                               .f = F_CI | F_CALL | F_DQ_STRING | F_DQ_NOESC | F_SQ_DOUBLING};
constexpr LangDef kLangVb{.kw = kVbKw, .types = kVbTypes, .consts = kVbConsts, .line1 = L"'",
                          .f = F_CI | F_VB | F_CALL | F_DQ_STRING | F_DQ_NOESC | F_HASH_PREPROC};
constexpr LangDef kLangPascal{.kw = kPascalKw, .types = kPascalTypes, .consts = kPascalConsts, .line1 = L"//",
                              .blockOpen = L"{", .blockClose = L"}", .block2Open = L"(*", .block2Close = L"*)",
                              .f = F_CI | F_PASCAL | F_SQ_DOUBLING | F_CALL | F_DELPHI_T};
constexpr LangDef kLangSql{.kw = kSqlKw, .types = kSqlTypes, .consts = kSqlConsts, .line1 = L"--",
                           .blockOpen = L"/*", .blockClose = L"*/",
                           .f = F_CI | F_SQL | F_SQ_DOUBLING | F_MULTILINE | F_DQ_STRING | F_CALL};
constexpr LangDef kLangGraphql{.kw = kGraphqlKw, .types = kGraphqlTypes, .consts = kNullConsts, .line1 = L"#",
                               .f = F_DQ_STRING | F_TRIPLE_DQ | F_DOLLAR_VAR | F_AT_META | F_PASCAL_TYPE};
constexpr LangDef kLangProto{.kw = kProtoKw, .types = kProtoTypes, .consts = kTrueFalse, .line1 = L"//",
                             .blockOpen = L"/*", .blockClose = L"*/",
                             .f = F_DQ_STRING | F_SQ_STRING | F_PASCAL_TYPE};
constexpr LangDef kLangHcl{.kw = kHclKw, .types = kHclTypes, .consts = kNullConsts, .line1 = L"#", .line2 = L"//",
                           .blockOpen = L"/*", .blockClose = L"*/",
                           .f = F_CALL | F_DQ_STRING | F_INTERP_DOLLAR | F_ASSIGN_PROP};
constexpr LangDef kLangNix{.kw = kNixKw, .consts = kNullConsts, .line1 = L"#", .blockOpen = L"/*",
                           .blockClose = L"*/", .f = F_DQ_STRING | F_MULTILINE | F_INTERP_DOLLAR | F_ASSIGN_PROP};
constexpr LangDef kLangNginx{.kw = kNginxKw, .consts = kOnOff, .line1 = L"#",
                             .f = F_DQ_STRING | F_SQ_STRING | F_DOLLAR_VAR};

void RunGeneric(const wchar_t* s, size_t n, Emitter& em, const LangDef& d) {
    CScanner sc(s, n, em, d);
    sc.Run();
}

// CSS escape: '\' + 1..6 hex digits, or '\' + any single character.
size_t CssEscapeEnd(const wchar_t* s, size_t n, size_t i) {
    size_t j = i + 1;
    while (j < n && j - i <= 6 && IsHex(s[j])) ++j;
    if (j == i + 1 && j < n) ++j;
    return j;
}

// Emits a quoted string [a, e) with backslash escapes highlighted.
void EmitEscapedString(const wchar_t* s, size_t a, size_t e, Emitter& em, bool css = false) {
    size_t seg = a;
    for (size_t m = a + 1; m < e;) {
        if (s[m] == L'\\') {
            size_t ee = css ? CssEscapeEnd(s, e, m) : EscapeEnd(s, e, m);
            if (ee > e) ee = e;
            em.Add(seg, m, K::String);
            em.Add(m, ee, K::Escape);
            m = seg = ee;
        } else {
            ++m;
        }
    }
    em.Add(seg, e, K::String);
}

// Single-line quoted string starting at i (s[i] is the quote). Returns end.
size_t QuotedEnd(const wchar_t* s, size_t n, size_t i, bool escapes) {
    const wchar_t q = s[i];
    size_t j = i + 1;
    while (j < n && s[j] != q && s[j] != L'\n') {
        if (escapes && s[j] == L'\\' && j + 1 < n && s[j + 1] != L'\n') ++j;
        ++j;
    }
    if (j > n) j = n;
    return (j < n && s[j] == q) ? j + 1 : j;
}

// ---------------------------------------------------------------------------
// Markup (HTML / XML / SVG / Vue...)
// ---------------------------------------------------------------------------
void ScanCss(const wchar_t* s, size_t n, Emitter& em, bool lineComments, bool indentSyntax);

inline bool IsTagChar(wchar_t c) { return IsIdChar(c) || c == L'-' || c == L':' || c == L'.'; }

size_t FindCloseTag(const wchar_t* s, size_t n, size_t i, wsv tagLower) {
    while (i < n) {
        const size_t p = FindCh(s, i, n, L'<');
        if (p == kNpos) return kNpos;
        const size_t after = p + 2 + tagLower.size();
        if (p + 1 < n && s[p + 1] == L'/' && AtCI(s, n, p + 2, tagLower) && (after >= n || !IsTagChar(s[after])))
            return p;
        i = p + 1;
    }
    return kNpos;
}

void ScanMarkup(const wchar_t* s, size_t n, Emitter& em) {
    size_t i = 0;
    while (i < n) {
        const wchar_t c = s[i];
        if (c == L'&') {
            size_t j = i + 1;
            if (j < n && s[j] == L'#') {
                ++j;
                if (j < n && (s[j] == L'x' || s[j] == L'X')) ++j;
            }
            size_t k = j;
            while (k < n && k - j < 32 && IsAlnum(s[k])) ++k;
            if (k > j && k < n && s[k] == L';') {
                em.Add(i, k + 1, K::Constant);
                i = k + 1;
            } else {
                ++i;
            }
            continue;
        }
        if (c != L'<') {
            ++i;
            continue;
        }
        if (At(s, n, i, L"<!--")) {
            const size_t p = Find(s, n, i + 4, L"-->");
            const size_t e = p == kNpos ? n : p + 3;
            em.Add(i, e, K::Comment);
            i = e;
            continue;
        }
        if (At(s, n, i, L"<![CDATA[")) {
            const size_t p = Find(s, n, i + 9, L"]]>");
            const size_t e = p == kNpos ? n : p + 3;
            em.Add(i, e, K::String);
            i = e;
            continue;
        }
        if (i + 1 < n && (s[i + 1] == L'!' || s[i + 1] == L'?')) {
            const size_t p = FindCh(s, i + 2, n, L'>');
            const size_t e = p == kNpos ? n : p + 1;
            em.Add(i, e, K::Meta);
            i = e;
            continue;
        }
        size_t j = i + 1;
        const bool closing = j < n && s[j] == L'/';
        if (closing) ++j;
        if (j >= n || !IsIdStart(s[j])) {
            ++i;
            continue;
        }
        const size_t nameStart = j;
        while (j < n && IsTagChar(s[j])) ++j;
        em.Add(nameStart, j, K::Tag);
        const wsv tag(s + nameStart, j - nameStart);
        bool selfClose = false;
        size_t k = j;
        while (k < n) {
            const wchar_t d = s[k];
            if (d == L'>') {
                ++k;
                break;
            }
            if (d == L'/' && k + 1 < n && s[k + 1] == L'>') {
                selfClose = true;
                k += 2;
                break;
            }
            if (d == L'<') break;
            if (d == L'"' || d == L'\'') {
                const size_t q = FindCh(s, k + 1, n, d);
                const size_t e = q == kNpos ? n : q + 1;
                em.Add(k, e, K::String);
                k = e;
                continue;
            }
            if (d == L'=') {
                const size_t v = SkipWs(s, n, k + 1);
                if (v < n && s[v] != L'"' && s[v] != L'\'' && s[v] != L'>' && s[v] != L'{') {
                    size_t e = v;
                    while (e < n && !IsSpaceOrNl(s[e]) && s[e] != L'>') ++e;
                    em.Add(v, e, K::String);
                    k = e;
                    continue;
                }
                k = v;
                continue;
            }
            if (d == L'{') {  // JSX / Vue / Svelte expression
                size_t depth = 0, e = k;
                while (e < n) {
                    if (s[e] == L'{') {
                        ++depth;
                    } else if (s[e] == L'}' && depth > 0 && --depth == 0) {
                        ++e;
                        break;
                    }
                    ++e;
                }
                k = e;
                continue;
            }
            if (!IsSpaceOrNl(d) && d != L'/') {
                size_t e = k;
                while (e < n && !IsSpaceOrNl(s[e]) && s[e] != L'=' && s[e] != L'>' && s[e] != L'/' &&
                       s[e] != L'"' && s[e] != L'\'' && s[e] != L'<' && s[e] != L'{')
                    ++e;
                if (e == k) {
                    ++k;
                    continue;
                }
                em.Add(k, e, K::Attribute);
                k = e;
                continue;
            }
            ++k;
        }
        i = k;
        if (!closing && !selfClose) {
            const bool script = EqCI(tag.data(), tag.size(), L"script");
            const bool style = EqCI(tag.data(), tag.size(), L"style");
            if (script || style) {
                const size_t endTag = FindCloseTag(s, n, i, script ? L"script" : L"style");
                const size_t bodyEnd = endTag == kNpos ? n : endTag;
                if (bodyEnd > i) {
                    BaseShift shift(em, i);
                    if (script) RunGeneric(s + i, bodyEnd - i, em, kLangTs);
                    else ScanCss(s + i, bodyEnd - i, em, false, false);
                }
                i = bodyEnd;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// CSS / SCSS / LESS / Sass
// ---------------------------------------------------------------------------
inline size_t CssIdentEnd(const wchar_t* s, size_t n, size_t i) {
    while (i < n && (IsIdChar(s[i]) || s[i] == L'-')) ++i;
    return i;
}

void ScanCss(const wchar_t* s, size_t n, Emitter& em, bool lineComments, bool indentSyntax) {
    size_t i = 0;
    size_t depth = 0;
    bool inValue = false;
    bool prelude = false;
    bool lineStart = true, lineIndented = false;
    size_t laPos = 0;  // cached lookahead: first of ; { } at or after any position < laPos
    wchar_t laChar = 0;
    LineFinder lf;
    auto lookahead = [&](size_t from) -> wchar_t {
        if (from >= laPos) {
            size_t q = from;
            while (q < n && s[q] != L';' && s[q] != L'{' && s[q] != L'}' && !(indentSyntax && s[q] == L'\n')) ++q;
            laPos = q;
            laChar = q < n ? s[q] : L'\0';
        }
        return laChar;
    };
    // After a property/variable name: if a ':' follows, switch to value mode.
    auto afterName = [&](size_t e) -> size_t {
        const size_t k = SkipBlanks(s, n, e);
        if (k < n && s[k] == L':') {
            inValue = true;
            return k + 1;
        }
        return e;
    };
    while (i < n) {
        const wchar_t c = s[i];
        const wchar_t c1 = i + 1 < n ? s[i + 1] : L'\0';
        if (c == L'\n') {
            ++i;
            if (indentSyntax) inValue = false;
            lineStart = true;
            lineIndented = false;
            continue;
        }
        if (IsBlank(c)) {
            if (lineStart) lineIndented = true;
            ++i;
            continue;
        }
        lineStart = false;
        if (c == L'/' && c1 == L'*') {
            const size_t p = Find(s, n, i + 2, L"*/");
            const size_t e = p == kNpos ? n : p + 2;
            em.Add(i, e, K::Comment);
            i = e;
            continue;
        }
        if (c == L'/' && c1 == L'/' && lineComments && !(i > 0 && s[i - 1] == L':')) {
            const size_t e = LineEnd(s, n, i);
            em.Add(i, e, K::Comment);
            i = e;
            continue;
        }
        if (c == L'"' || c == L'\'') {
            const size_t e = QuotedEnd(s, n, i, true);
            EmitEscapedString(s, i, e, em, true);
            i = e;
            continue;
        }
        if (c == L'{' || c == L'}' || c == L';') {
            if (c == L'{') ++depth;
            else if (c == L'}' && depth > 0) --depth;
            inValue = false;
            prelude = false;
            ++i;
            continue;
        }
        const bool inBlock = depth > 0 || (indentSyntax && lineIndented);
        if (c == L'@' && (IsIdStart(c1) || c1 == L'-')) {
            const size_t e = CssIdentEnd(s, n, i + 1);
            const size_t k = SkipBlanks(s, n, e);
            if (inValue || (!prelude && k < n && s[k] == L':')) {  // LESS variable
                em.Add(i, e, K::Variable);
                i = inValue ? e : afterName(e);
                continue;
            }
            em.Add(i, e, K::Keyword);
            prelude = true;
            i = e;
            continue;
        }
        if ((c == L'$' && IsIdStart(c1)) || (c == L'-' && c1 == L'-' && i + 2 < n && (IsIdChar(s[i + 2]) || s[i + 2] == L'-'))) {
            const size_t e = CssIdentEnd(s, n, c == L'$' ? i + 1 : i + 2);
            em.Add(i, e, K::Variable);
            i = inValue ? e : afterName(e);
            continue;
        }
        if (c == L'!' && AtCI(s, n, i + 1, L"important")) {
            em.Add(i, i + 10, K::Keyword);
            i += 10;
            continue;
        }
        if (c == L'#') {
            if (c1 == L'{') {  // SCSS interpolation
                i += 2;
                continue;
            }
            const size_t e = CssIdentEnd(s, n, i + 1);
            if (e > i + 1) {
                if (inValue) {
                    const size_t len = e - i - 1;
                    bool hex = len == 3 || len == 4 || len == 6 || len == 8;
                    for (size_t m = i + 1; hex && m < e; ++m) hex = IsHex(s[m]);
                    if (hex) em.Add(i, e, K::Constant);
                } else {
                    em.Add(i, e, K::Attribute);
                }
                i = e;
                continue;
            }
            ++i;
            continue;
        }
        const bool signOrDot = c == L'.' || c == L'-' || c == L'+';
        if ((IsDigit(c) || (signOrDot && (inValue || prelude) &&
                            (IsDigit(c1) || (c1 == L'.' && i + 2 < n && IsDigit(s[i + 2]))))) &&
            !(i > 0 && (IsIdChar(s[i - 1]) || s[i - 1] == L'-'))) {
            size_t e = i;
            if (s[e] == L'-' || s[e] == L'+') ++e;
            while (e < n && (IsDigit(s[e]) || s[e] == L'.')) ++e;
            while (e < n && (IsAlpha(s[e]) || s[e] == L'%')) ++e;
            em.Add(i, e, K::Number);
            i = e;
            continue;
        }
        if (c == L'.' && !inValue && (IsIdStart(c1) || c1 == L'-')) {
            const size_t e = CssIdentEnd(s, n, i + 1);
            em.Add(i, e, K::Attribute);
            i = e;
            continue;
        }
        if (c == L':' && !inValue) {
            size_t j = i + 1;
            if (j < n && s[j] == L':') ++j;
            if (j < n && (IsIdStart(s[j]) || s[j] == L'-')) {
                const size_t e = CssIdentEnd(s, n, j);
                em.Add(i, e, K::Keyword);
                i = e;
                continue;
            }
            ++i;
            continue;
        }
        if (c == L'[' && !inValue) {
            const size_t j = SkipBlanks(s, n, i + 1);
            const size_t e = CssIdentEnd(s, n, j);
            em.Add(j, e, K::Attribute);
            i = e > j ? e : i + 1;
            continue;
        }
        if (IsIdStart(c) || (c == L'-' && (IsIdStart(c1) || c1 == L'-'))) {
            const size_t e = CssIdentEnd(s, n, i + 1);
            const size_t k = SkipBlanks(s, n, e);
            const wchar_t nc = k < n ? s[k] : L'\0';
            if (inValue) {
                if (nc == L'(') {
                    em.Add(i, e, K::Function);
                    if (EqCI(s + i, e - i, L"url")) {
                        const size_t p = SkipBlanks(s, n, k + 1);
                        if (p < n && s[p] != L'"' && s[p] != L'\'') {
                            const size_t cp = lf.Find(s, n, p, L')');
                            if (cp != kNpos) {
                                em.Add(p, cp, K::String);
                                i = cp;
                                continue;
                            }
                        }
                    }
                }
                i = e;
                continue;
            }
            if (prelude) {
                if (EqCI(s + i, e - i, L"and") || EqCI(s + i, e - i, L"not") || EqCI(s + i, e - i, L"only") ||
                    EqCI(s + i, e - i, L"or"))
                    em.Add(i, e, K::Keyword);
                else if (nc == L':') em.Add(i, e, K::Property);
                else if (nc == L'(') em.Add(i, e, K::Function);
                i = e;
                continue;
            }
            if (nc == L':' && lookahead(i) != L'{') {
                em.Add(i, e, K::Property);
                inValue = true;
                i = k + 1;
                continue;
            }
            em.Add(i, e, (inBlock && nc == L'(') ? K::Function : K::Tag);
            i = e;
            continue;
        }
        ++i;
    }
}

// ---------------------------------------------------------------------------
// JSON / JSONC / JSON5
// ---------------------------------------------------------------------------
MDV_WORDS(kJsonConsts, L"true false null NaN Infinity undefined");

void ScanJson(const wchar_t* s, size_t n, Emitter& em) {
    size_t i = 0;
    while (i < n) {
        const wchar_t c = s[i];
        const wchar_t c1 = i + 1 < n ? s[i + 1] : L'\0';
        if (c == L'/' && c1 == L'/') {
            const size_t e = LineEnd(s, n, i);
            em.Add(i, e, K::Comment);
            i = e;
            continue;
        }
        if (c == L'/' && c1 == L'*') {
            const size_t p = Find(s, n, i + 2, L"*/");
            const size_t e = p == kNpos ? n : p + 2;
            em.Add(i, e, K::Comment);
            i = e;
            continue;
        }
        if (c == L'"' || c == L'\'') {
            const size_t e = QuotedEnd(s, n, i, true);
            const size_t k = SkipWs(s, n, e);
            if (k < n && s[k] == L':') em.Add(i, e, K::Property);
            else EmitEscapedString(s, i, e, em);
            i = e;
            continue;
        }
        const size_t st = (c == L'-' || c == L'+') ? i + 1 : i;
        if (st < n && (IsDigit(s[st]) || (s[st] == L'.' && st + 1 < n && IsDigit(s[st + 1]))) &&
            !(i > 0 && IsIdChar(s[i - 1]))) {
            const size_t e = ScanNumber(s, n, st, false);
            em.Add(i, e, K::Number);
            i = e;
            continue;
        }
        if (IsIdStart(c)) {
            const size_t e = ScanIdent(s, n, i);
            if (InSet(kJsonConsts, s + i, e - i, false)) {
                em.Add(i, e, K::Constant);
            } else {
                const size_t k = SkipWs(s, n, e);
                if (k < n && s[k] == L':') em.Add(i, e, K::Property);
            }
            i = e;
            continue;
        }
        ++i;
    }
}

// ---------------------------------------------------------------------------
// YAML
// ---------------------------------------------------------------------------
bool IsYamlNumber(const wchar_t* s, size_t a, size_t b) {
    if (a >= b) return false;
    size_t i = a;
    if (s[i] == L'+' || s[i] == L'-') ++i;
    if (i >= b) return false;
    const wsv rest(s + i, b - i);
    if (rest == L".inf" || rest == L".Inf" || rest == L".INF" || rest == L".nan" || rest == L".NaN" ||
        rest == L".NAN")
        return true;
    if (b - i > 2 && s[i] == L'0' && (s[i + 1] == L'x' || s[i + 1] == L'o')) {
        for (size_t k = i + 2; k < b; ++k)
            if (!IsHex(s[k]) && s[k] != L'_') return false;
        return true;
    }
    bool digits = false, dot = false, exp = false;
    for (size_t k = i; k < b; ++k) {
        const wchar_t c = s[k];
        if (IsDigit(c)) {
            digits = true;
        } else if (c == L'_' && digits) {
        } else if (c == L'.' && !dot && !exp) {
            dot = true;
        } else if ((c == L'e' || c == L'E') && digits && !exp && k + 1 < b) {
            exp = true;
            if (s[k + 1] == L'+' || s[k + 1] == L'-') ++k;
        } else {
            return false;
        }
    }
    return digits;
}

inline bool IsYamlIndicator(wchar_t c) {
    switch (c) {
    case L'[': case L'{': case L'&': case L'*': case L'!': case L'|': case L'>': case L'%': case L'@':
    case L'`': case L'#': case L',': case L']': case L'}':
        return true;
    default:
        return false;
    }
}
inline bool IsFlowChar(wchar_t c) { return c == L',' || c == L'[' || c == L']' || c == L'{' || c == L'}'; }

class YamlScanner {
public:
    YamlScanner(const wchar_t* s, size_t n, Emitter& em) : s_(s), n_(n), em_(em) {}
    void Run() {
        size_t i = 0;
        while (i < n_) {
            const size_t le = LineEnd(s_, n_, i);
            const size_t ce = ContentEnd(s_, i, le);
            size_t j = i;
            while (j < ce && (s_[j] == L' ' || s_[j] == L'\t')) ++j;
            const size_t indent = j - i;
            if (blockParent_ != kNpos) {
                if (j == ce || indent > blockParent_) {
                    em_.Add(j, ce, K::String);
                    i = le + 1;
                    continue;
                }
                blockParent_ = kNpos;
            }
            Line(j, ce, indent);
            i = le + 1;
        }
    }

private:
    void Line(size_t j, size_t ce, size_t indent) {
        if (j >= ce) return;
        if (s_[j] == L'#') {
            em_.Add(j, ce, K::Comment);
            return;
        }
        if (indent == 0 && flow_ == 0) {
            if ((At(s_, ce, j, L"---") || At(s_, ce, j, L"...")) && (j + 3 == ce || IsBlank(s_[j + 3]))) {
                em_.Add(j, j + 3, K::Meta);
                j = SkipBlanks(s_, ce, j + 3);
            } else if (s_[j] == L'%') {
                em_.Add(j, ce, K::Meta);
                return;
            }
        }
        if (flow_ == 0) {
            while (j < ce && (s_[j] == L'-' || s_[j] == L'?') && (j + 1 == ce || IsBlank(s_[j + 1])))
                j = SkipBlanks(s_, ce, j + 1);
            if (j < ce && (s_[j] == L'"' || s_[j] == L'\'')) {
                const size_t qe = QuotedEnd(s_, ce, j, s_[j] == L'"');
                const size_t k = SkipBlanks(s_, ce, qe);
                if (k < ce && s_[k] == L':' && (k + 1 == ce || IsBlank(s_[k + 1]))) {
                    em_.Add(j, qe, K::Property);
                    j = k + 1;
                }
            } else if (j < ce && !IsYamlIndicator(s_[j])) {
                for (size_t k = j; k < ce; ++k) {
                    if (s_[k] == L':' && (k + 1 == ce || IsBlank(s_[k + 1]))) {
                        em_.Add(j, TrimRight(s_, j, k), K::Property);
                        j = k + 1;
                        break;
                    }
                    if (s_[k] == L'#' && k > j && IsBlank(s_[k - 1])) break;
                }
            }
        }
        Value(j, ce, indent);
    }

    void Value(size_t k, size_t ce, size_t indent) {
        bool first = true;
        while (k < ce) {
            const wchar_t c = s_[k];
            if (IsBlank(c)) {
                ++k;
                continue;
            }
            if (c == L'#' && (first || IsBlank(s_[k - 1]))) {
                em_.Add(k, ce, K::Comment);
                return;
            }
            const bool wasFirst = first;
            first = false;
            if (c == L'"' || c == L'\'') {
                const size_t e = QuotedEnd(s_, ce, k, c == L'"');
                const size_t after = SkipBlanks(s_, ce, e);
                if (flow_ > 0 && after < ce && s_[after] == L':') em_.Add(k, e, K::Property);
                else if (c == L'"') EmitEscapedString(s_, k, e, em_);
                else em_.Add(k, e, K::String);
                k = e;
                continue;
            }
            if ((c == L'&' || c == L'*' || c == L'!') && k + 1 < ce && !IsBlank(s_[k + 1])) {
                size_t e = k + 1;
                while (e < ce && !IsBlank(s_[e]) && !IsFlowChar(s_[e])) ++e;
                em_.Add(k, e, c == L'!' ? K::Type : K::Variable);
                k = e;
                continue;
            }
            if ((c == L'|' || c == L'>') && wasFirst && flow_ == 0) {
                size_t e = k + 1;
                while (e < ce && (s_[e] == L'-' || s_[e] == L'+' || IsDigit(s_[e]))) ++e;
                e = SkipBlanks(s_, ce, e);
                if (e == ce || s_[e] == L'#') {
                    blockParent_ = indent;
                    em_.Add(k, e, K::Meta);
                    if (e < ce) em_.Add(e, ce, K::Comment);
                    return;
                }
            }
            if (c == L'[' || c == L'{') {
                ++flow_;
                ++k;
                continue;
            }
            if (c == L']' || c == L'}') {
                if (flow_ > 0) --flow_;
                ++k;
                continue;
            }
            if (c == L',') {
                ++k;
                continue;
            }
            size_t e = k;
            while (e < ce) {
                const wchar_t d = s_[e];
                if (d == L'#' && e > k && IsBlank(s_[e - 1])) break;
                if (d == L':' && (e + 1 == ce || IsBlank(s_[e + 1]) || (flow_ > 0 && IsFlowChar(s_[e + 1])))) break;
                if (flow_ > 0 && IsFlowChar(d)) break;
                ++e;
            }
            const size_t te = TrimRight(s_, k, e);
            if (e < ce && s_[e] == L':') {
                em_.Add(k, te, K::Property);
                k = e + 1;
                continue;
            }
            if (IsYamlNumber(s_, k, te)) em_.Add(k, te, K::Number);
            else if (InSet(kYamlConsts, s_ + k, te - k, false)) em_.Add(k, te, K::Constant);
            k = e > k ? e : k + 1;
        }
    }

    const wchar_t* s_;
    size_t n_;
    Emitter& em_;
    size_t blockParent_ = kNpos;  // indentation of the line that opened a block scalar
    size_t flow_ = 0;             // flow collection nesting
};

// ---------------------------------------------------------------------------
// TOML
// ---------------------------------------------------------------------------
void ScanToml(const wchar_t* s, size_t n, Emitter& em) {
    size_t i = 0;
    bool lineStart = true;
    bool expectKey = true;
    uint64_t tableBits = 0;  // bit d set: bracket at depth d is '{'
    size_t depth = 0;
    while (i < n) {
        const wchar_t c = s[i];
        if (c == L'\n') {
            ++i;
            lineStart = true;
            if (depth == 0) expectKey = true;
            continue;
        }
        if (IsBlank(c)) {
            ++i;
            continue;
        }
        const bool atStart = lineStart;
        lineStart = false;
        if (c == L'#') {
            const size_t e = LineEnd(s, n, i);
            em.Add(i, e, K::Comment);
            i = e;
            continue;
        }
        if (c == L'[' && atStart && depth == 0) {
            const size_t ce = ContentEnd(s, i, LineEnd(s, n, i));
            const size_t close = FindCh(s, i + 1, ce, L']');
            size_t e = close == kNpos ? ce : close + 1;
            if (e < ce && s[e] == L']') ++e;
            em.Add(i, e, K::Meta);
            i = e;
            continue;
        }
        if (c == L'"' || c == L'\'') {
            size_t e;
            if (i + 2 < n && s[i + 1] == c && s[i + 2] == c) {
                const wchar_t tq[3] = {c, c, c};
                const size_t p = Find(s, n, i + 3, wsv(tq, 3));
                e = p == kNpos ? n : p + 3;
                if (c == L'"') EmitEscapedString(s, i, e, em);
                else em.Add(i, e, K::String);
            } else {
                e = QuotedEnd(s, n, i, c == L'"');
                if (expectKey) em.Add(i, e, K::Property);
                else if (c == L'"') EmitEscapedString(s, i, e, em);
                else em.Add(i, e, K::String);
            }
            i = e;
            continue;
        }
        if (c == L'=') {
            expectKey = false;
            ++i;
            continue;
        }
        if (c == L'[' || c == L'{') {
            if (depth < 64) {
                if (c == L'{') tableBits |= (1ull << depth);
                else tableBits &= ~(1ull << depth);
            }
            ++depth;
            expectKey = c == L'{';
            ++i;
            continue;
        }
        if (c == L']' || c == L'}') {
            if (depth > 0) --depth;
            expectKey = false;
            ++i;
            continue;
        }
        if (c == L',') {
            expectKey = depth > 0 && depth <= 64 && ((tableBits >> (depth - 1)) & 1u) != 0;
            ++i;
            continue;
        }
        if (IsAlnum(c) || c == L'_' || c == L'-' || c == L'+') {
            size_t e = i;
            if (expectKey) {
                while (e < n && (IsAlnum(s[e]) || s[e] == L'_' || s[e] == L'-')) ++e;
                if (e == i) e = i + 1;
                em.Add(i, e, K::Property);
                i = e;
                continue;
            }
            while (e < n && (IsAlnum(s[e]) || s[e] == L'_' || s[e] == L'-' || s[e] == L'+' || s[e] == L'.' ||
                             s[e] == L':'))
                ++e;
            const wsv w(s + i, e - i);
            if (w == L"true" || w == L"false") em.Add(i, e, K::Constant);
            else if (IsDigit(c) || c == L'+' || c == L'-' || w == L"inf" || w == L"nan") em.Add(i, e, K::Number);
            i = e;
            continue;
        }
        ++i;
    }
}

// ---------------------------------------------------------------------------
// INI / .properties / .reg / .editorconfig / .gitconfig / .env ...
// ---------------------------------------------------------------------------
enum class IniKind : uint8_t { Ini, Props, Reg };

// Emits `$VAR`, `${VAR}`, `%VAR%` references inside [a, b).
void EmitVarRefs(const wchar_t* s, size_t a, size_t b, Emitter& em) {
    bool noBrace = false;  // no '}' left in [.., b): stop searching
    for (size_t k = a; k < b;) {
        const wchar_t c = s[k];
        if (c == L'$' && k + 1 < b && s[k + 1] == L'{' && !noBrace) {
            const size_t cb = FindCh(s, k + 2, b, L'}');
            if (cb != kNpos) {
                em.Add(k, cb + 1, K::Variable);
                k = cb + 1;
                continue;
            }
            noBrace = true;
        } else if (c == L'$' && k + 1 < b && IsIdStart(s[k + 1])) {
            const size_t e = ScanIdent(s, b, k + 1);
            em.Add(k, e, K::Variable);
            k = e;
            continue;
        } else if (c == L'%' && k + 1 < b && IsIdStart(s[k + 1])) {
            const size_t e = ScanIdent(s, b, k + 1);
            if (e < b && s[e] == L'%') {
                em.Add(k, e + 1, K::Variable);
                k = e + 1;
                continue;
            }
        }
        ++k;
    }
}

void ScanIni(const wchar_t* s, size_t n, Emitter& em, IniKind kind) {
    size_t i = 0;
    bool regCont = false;
    while (i < n) {
        const size_t le = LineEnd(s, n, i);
        const size_t ce = ContentEnd(s, i, le);
        const size_t j = SkipBlanks(s, ce, i);
        const bool wasCont = regCont;
        regCont = false;
        if (j >= ce) {
            i = le + 1;
            continue;
        }
        const wchar_t c = s[j];
        const size_t te = TrimRight(s, j, ce);
        if (kind == IniKind::Reg && wasCont) {
            regCont = s[te - 1] == L'\\';
            em.Add(j, regCont ? te - 1 : te, K::Number);
        } else if (c == L'#' || c == L';' || (kind == IniKind::Props && c == L'!')) {
            em.Add(j, ce, K::Comment);
        } else if (c == L'[' && kind != IniKind::Props) {
            const size_t close = FindCh(s, j + 1, ce, L']');
            const size_t e = close == kNpos ? ce : close + 1;
            em.Add(j, e, K::Meta);
            const size_t r = SkipBlanks(s, ce, e);
            if (r < ce && (s[r] == L';' || s[r] == L'#')) em.Add(r, ce, K::Comment);
        } else if (kind == IniKind::Reg && At(s, ce, j, L"Windows Registry Editor")) {
            em.Add(j, ce, K::Meta);
        } else {
            size_t k = j;
            if (s[k] == L'"') k = QuotedEnd(s, ce, k, true);
            size_t sep = kNpos;
            for (; k < ce; ++k) {
                if (s[k] == L'=' || (kind == IniKind::Props && s[k] == L':')) {
                    sep = k;
                    break;
                }
            }
            if (sep != kNpos) {
                em.Add(j, TrimRight(s, j, sep), K::Property);
                size_t v = SkipBlanks(s, ce, sep + 1);
                size_t vend = te;
                size_t commentAt = kNpos;
                if (kind == IniKind::Ini) {  // inline comment
                    for (size_t m = v; m < te; ++m) {
                        if ((s[m] == L';' || s[m] == L'#') && m > v && IsBlank(s[m - 1])) {
                            commentAt = m;
                            vend = TrimRight(s, v, m);
                            break;
                        }
                    }
                }
                if (v < vend) {
                    const wchar_t vc = s[v];
                    if (kind == IniKind::Reg && vc != L'"') {
                        const size_t colon = FindCh(s, v, vend, L':');
                        if (colon != kNpos) {
                            em.Add(v, colon + 1, K::Type);
                            v = colon + 1;
                        }
                        regCont = s[vend - 1] == L'\\';
                        em.Add(v, regCont ? vend - 1 : vend, vc == L'-' ? K::Keyword : K::Number);
                    } else if ((vc == L'"' || vc == L'\'') && s[vend - 1] == vc && vend - v >= 2) {
                        if (vc == L'"') EmitEscapedString(s, v, vend, em);
                        else em.Add(v, vend, K::String);
                    } else if ((IsDigit(vc) || vc == L'-' || vc == L'+' || vc == L'.') &&
                               IsYamlNumber(s, v, vend)) {
                        em.Add(v, vend, K::Number);
                    } else if (InSet(kIniConsts, s + v, vend - v, true)) {
                        em.Add(v, vend, K::Constant);
                    } else {
                        EmitVarRefs(s, v, vend, em);
                    }
                }
                if (commentAt != kNpos) em.Add(commentAt, te, K::Comment);
            }
        }
        i = le + 1;
    }
}

// ---------------------------------------------------------------------------
// Diff / patch
// ---------------------------------------------------------------------------
void ScanDiff(const wchar_t* s, size_t n, Emitter& em) {
    static constexpr wsv kHeaders[] = {L"diff ",        L"index ",         L"new file mode",
                                       L"deleted file mode", L"similarity index", L"dissimilarity index",
                                       L"rename from",  L"rename to",      L"copy from",
                                       L"copy to",      L"old mode",       L"new mode",
                                       L"Binary files", L"Only in ",       L"*** "};
    size_t i = 0;
    while (i < n) {
        const size_t le = LineEnd(s, n, i);
        const size_t ce = ContentEnd(s, i, le);
        if (ce > i) {
            const wchar_t c = s[i];
            if ((At(s, ce, i, L"+++") || At(s, ce, i, L"---")) && (ce == i + 3 || IsBlank(s[i + 3]))) {
                em.Add(i, ce, K::Heading);
            } else if (At(s, ce, i, L"@@")) {
                const size_t p = Find(s, ce, i + 2, L"@@");
                em.Add(i, p == kNpos ? ce : p + 2, K::DiffRange);
            } else if (At(s, ce, i, L"***************")) {
                em.Add(i, ce, K::DiffRange);
            } else if (c == L'+' || (c == L'>' && (ce == i + 1 || s[i + 1] == L' '))) {
                em.Add(i, ce, K::Inserted);
            } else if (c == L'-' || (c == L'<' && (ce == i + 1 || s[i + 1] == L' '))) {
                em.Add(i, ce, K::Deleted);
            } else if (c == L'\\') {
                em.Add(i, ce, K::Comment);
            } else {
                for (const wsv& h : kHeaders) {
                    if (At(s, ce, i, h)) {
                        em.Add(i, ce, K::Heading);
                        break;
                    }
                }
            }
        }
        i = le + 1;
    }
}

// ---------------------------------------------------------------------------
// Markdown
// ---------------------------------------------------------------------------
class MarkdownScanner {
public:
    MarkdownScanner(const wchar_t* s, size_t n, Emitter& em) : s_(s), n_(n), em_(em) {}
    void Run() {
        size_t i = 0;
        while (i < n_) {
            const size_t le = LineEnd(s_, n_, i);
            Line(i, ContentEnd(s_, i, le));
            i = le + 1;
        }
    }

private:
    void Line(size_t i, size_t ce) {
        size_t j = i;
        while (j < ce && s_[j] == L' ' && j - i < 4) ++j;
        const bool indented = j - i >= 4;
        if (fenceCh_ != 0) {
            size_t k = j;
            while (k < ce && s_[k] == fenceCh_) ++k;
            if (!indented && k - j >= fenceLen_ && SkipBlanks(s_, ce, k) == ce) {
                em_.Add(i, ce, K::Meta);
                fenceCh_ = 0;
            }
            return;
        }
        if (inComment_) {
            const size_t p = Find(s_, ce, i, L"-->");
            if (p == kNpos) {
                em_.Add(i, ce, K::Comment);
                return;
            }
            em_.Add(i, p + 3, K::Comment);
            inComment_ = false;
            Inline(p + 3, ce);
            return;
        }
        if (!indented && j < ce) {
            const wchar_t c = s_[j];
            if (c == L'`' || c == L'~') {
                size_t k = j;
                while (k < ce && s_[k] == c) ++k;
                if (k - j >= 3 && !(c == L'`' && FindCh(s_, k, ce, L'`') != kNpos)) {
                    fenceCh_ = c;
                    fenceLen_ = k - j;
                    em_.Add(i, ce, K::Meta);
                    return;
                }
            }
            if (c == L'#') {
                size_t k = j;
                while (k < ce && s_[k] == L'#') ++k;
                if (k - j <= 6 && (k == ce || IsBlank(s_[k]))) {
                    em_.Add(j, ce, K::Heading);
                    return;
                }
            }
            if (c == L'=' || c == L'-' || c == L'*' || c == L'_') {
                size_t cnt = 0;
                bool only = true;
                for (size_t k = j; k < ce; ++k) {
                    if (s_[k] == c) ++cnt;
                    else if (!IsBlank(s_[k])) {
                        only = false;
                        break;
                    }
                }
                if (only && (c == L'=' || cnt >= 3)) {
                    em_.Add(j, ce, c == L'=' ? K::Heading : K::Meta);
                    return;
                }
            }
            while (j < ce && s_[j] == L'>') {
                em_.Add(j, j + 1, K::Keyword);
                j = SkipBlanks(s_, ce, j + 1);
            }
            if (j < ce && (s_[j] == L'-' || s_[j] == L'*' || s_[j] == L'+') && (j + 1 == ce || IsBlank(s_[j + 1]))) {
                em_.Add(j, j + 1, K::Keyword);
                ++j;
            } else if (j < ce && IsDigit(s_[j])) {
                size_t k = j;
                while (k < ce && IsDigit(s_[k]) && k - j < 9) ++k;
                if (k < ce && (s_[k] == L'.' || s_[k] == L')') && (k + 1 == ce || IsBlank(s_[k + 1]))) {
                    em_.Add(j, k + 1, K::Keyword);
                    j = k + 1;
                }
            }
        }
        Inline(j, ce);
    }

    void Inline(size_t k, size_t ce) {
        bool noCode = false, noBold = false, noLink = false, noAuto = false;
        while (k < ce) {
            const wchar_t c = s_[k];
            if (c == L'\\' && k + 1 < ce) {
                k += 2;
                continue;
            }
            if (c == L'`') {
                size_t r = k;
                while (r < ce && s_[r] == L'`') ++r;
                const size_t len = r - k;
                if (!noCode) {
                    size_t m = r;
                    while (m < ce) {
                        if (s_[m] != L'`') {
                            ++m;
                            continue;
                        }
                        size_t r2 = m;
                        while (r2 < ce && s_[r2] == L'`') ++r2;
                        if (r2 - m == len) break;
                        m = r2;
                    }
                    if (m < ce) {
                        em_.Add(k, m + len, K::String);
                        k = m + len;
                        continue;
                    }
                    noCode = true;
                }
                k = r;
                continue;
            }
            if (c == L'*' && k + 1 < ce && s_[k + 1] == L'*' && !noBold) {
                const size_t p = Find(s_, ce, k + 2, L"**");
                if (p != kNpos && p > k + 2) {
                    em_.Add(k, p + 2, K::Heading);
                    k = p + 2;
                    continue;
                }
                noBold = true;
                k += 2;
                continue;
            }
            if (c == L']' && k + 1 < ce && s_[k + 1] == L'(' && !noLink) {
                const size_t p = FindCh(s_, k + 2, ce, L')');
                if (p != kNpos) {
                    em_.Add(k + 2, p, K::String);
                    k = p + 1;
                    continue;
                }
                noLink = true;
            }
            if (c == L'<' && At(s_, ce, k, L"<!--")) {
                const size_t p = Find(s_, ce, k + 4, L"-->");
                if (p == kNpos) {
                    em_.Add(k, ce, K::Comment);
                    inComment_ = true;
                    return;
                }
                em_.Add(k, p + 3, K::Comment);
                k = p + 3;
                continue;
            }
            if (c == L'<' && !noAuto && (AtCI(s_, ce, k + 1, L"http://") || AtCI(s_, ce, k + 1, L"https://"))) {
                const size_t p = FindCh(s_, k, ce, L'>');
                if (p != kNpos) {
                    em_.Add(k, p + 1, K::String);
                    k = p + 1;
                    continue;
                }
                noAuto = true;
            }
            ++k;
        }
    }

    const wchar_t* s_;
    size_t n_;
    Emitter& em_;
    wchar_t fenceCh_ = 0;
    size_t fenceLen_ = 0;
    bool inComment_ = false;
};

// ---------------------------------------------------------------------------
// Shell (sh / bash / zsh ...)
// ---------------------------------------------------------------------------
class ShellScanner {
public:
    ShellScanner(const wchar_t* s, size_t n, Emitter& em) : s_(s), n_(n), em_(em) {}

    void Run() {
        size_t i = 0;
        while (i < n_) {
            const wchar_t c = s_[i];
            const wchar_t c1 = i + 1 < n_ ? s_[i + 1] : L'\0';
            if (c == L'\n') {
                ++i;
                cmdPos_ = true;
                declCtx_ = false;
                funcNext_ = false;
                inCount_ = 0;
                arith_ = 0;
                if (hdPending_) {
                    hdPending_ = false;
                    i = Heredoc(i);
                }
                continue;
            }
            if (IsBlank(c)) {
                ++i;
                continue;
            }
            if (c == L'\\') {  // escaped char or line continuation
                if (c1 == L'\r' && i + 2 < n_ && s_[i + 2] == L'\n') i += 3;
                else i = i + 2 < n_ ? i + 2 : n_;
                continue;
            }
            if (c == L'#' && (i == 0 || IsSpaceOrNl(s_[i - 1]) || s_[i - 1] == L';' || s_[i - 1] == L'&' ||
                              s_[i - 1] == L'|' || s_[i - 1] == L'(')) {
                const size_t e = LineEnd(s_, n_, i);
                em_.Add(i, e, K::Comment);
                i = e;
                continue;
            }
            if (c == L'\'') {
                const size_t q = FindCh(s_, i + 1, n_, L'\'');
                const size_t e = q == kNpos ? n_ : q + 1;
                em_.Add(i, e, K::String);
                i = e;
                cmdPos_ = false;
                continue;
            }
            if (c == L'"') {
                i = DqString(i);
                cmdPos_ = false;
                continue;
            }
            if (c == L'$') {
                if (c1 == L'\'') {
                    size_t j = i + 2;
                    while (j < n_ && s_[j] != L'\'') j += (s_[j] == L'\\') ? 2 : 1;
                    const size_t e = j < n_ ? j + 1 : n_;
                    EmitEscapedString(s_, i, e, em_);
                    i = e;
                    cmdPos_ = false;
                    continue;
                }
                if (c1 == L'"') {
                    em_.Add(i, i + 1, K::String);
                    i = DqString(i + 1);
                    cmdPos_ = false;
                    continue;
                }
                const size_t e = VarEnd(i);
                if (e > i) {
                    if (e == i + 3 && c1 == L'(') ++arith_;  // $((
                    em_.Add(i, e, K::Variable);
                    cmdPos_ = c1 == L'(';
                    i = e;
                    continue;
                }
                ++i;
                continue;
            }
            if (c == L'`') {
                cmdPos_ = true;
                ++i;
                continue;
            }
            // (Inside $(( )) or (( )), << shifts.)
            if (c == L'<' && c1 == L'<' && i + 2 < n_ && s_[i + 2] != L'<' && arith_ == 0) {
                if (const size_t e = HeredocStart(i)) {
                    i = e;
                    continue;
                }
            }
            if (c == L';' || c == L'&' || c == L'|' || c == L'(' || c == L')') {
                if (c == L'(' && c1 == L'(') ++arith_;
                else if (c == L')' && c1 == L')' && arith_ > 0) --arith_;
                cmdPos_ = true;
                declCtx_ = false;
                inCount_ = 0;
                ++i;
                continue;
            }
            if (IsWordChar(c)) {
                i = WordTok(i);
                continue;
            }
            ++i;
        }
    }

private:
    static bool IsWordChar(wchar_t c) {
        switch (c) {
        case L';': case L'&': case L'|': case L'(': case L')': case L'<': case L'>': case L'\'': case L'"':
        case L'$': case L'`': case L'\\': case L'\n':
            return false;
        default:
            return !IsBlank(c);
        }
    }

    size_t VarEnd(size_t i) const {
        const wchar_t c1 = i + 1 < n_ ? s_[i + 1] : L'\0';
        if (c1 == L'{') {
            size_t depth = 0, e = i + 1;
            while (e < n_ && s_[e] != L'\n') {
                if (s_[e] == L'{') {
                    ++depth;
                } else if (s_[e] == L'}' && depth > 0 && --depth == 0) {
                    ++e;
                    break;
                }
                ++e;
            }
            return e;
        }
        if (c1 == L'(') return (i + 2 < n_ && s_[i + 2] == L'(') ? i + 3 : i + 2;
        if (IsIdStart(c1)) return ScanIdent(s_, n_, i + 1);
        if (IsDigit(c1) || c1 == L'@' || c1 == L'*' || c1 == L'#' || c1 == L'?' || c1 == L'$' || c1 == L'!' ||
            c1 == L'-')
            return i + 2;
        return i;
    }

    size_t DqString(size_t i) {
        size_t seg = i;
        size_t j = i + 1;
        while (j < n_) {
            const wchar_t c = s_[j];
            if (c == L'"') {
                em_.Add(seg, j + 1, K::String);
                return j + 1;
            }
            if (c == L'\\' && j + 1 < n_) {
                const wchar_t d = s_[j + 1];
                if (d == L'"' || d == L'\\' || d == L'$' || d == L'`' || d == L'\n') {
                    em_.Add(seg, j, K::String);
                    em_.Add(j, j + 2, K::Escape);
                    seg = j + 2;
                }
                j += 2;
                continue;
            }
            if (c == L'$') {
                const size_t e = VarEnd(j);
                if (e > j) {
                    em_.Add(seg, j, K::String);
                    em_.Add(j, e, K::Variable);
                    j = seg = e;
                    continue;
                }
            }
            ++j;
        }
        em_.Add(seg, n_, K::String);
        return n_;
    }

    size_t HeredocStart(size_t i) {
        size_t j = i + 2;
        bool strip = false;
        if (j < n_ && (s_[j] == L'-' || s_[j] == L'~')) {
            strip = true;
            ++j;
        }
        j = SkipBlanks(s_, n_, j);
        bool quoted = false;
        size_t a = j, b = j;
        if (j < n_ && (s_[j] == L'\'' || s_[j] == L'"')) {
            const size_t qe = lf_.Find(s_, n_, j + 1, s_[j]);
            if (qe == kNpos) return 0;
            quoted = true;
            a = j + 1;
            b = qe;
            j = qe + 1;
        } else {
            if (j >= n_ || !IsIdStart(s_[j])) return 0;
            while (b < n_ && (IsIdChar(s_[b]) || s_[b] == L'-' || s_[b] == L'.')) ++b;
            j = b;
        }
        if (b <= a || b - a >= 64) return 0;
        for (size_t k = 0; k < b - a; ++k) hd_[k] = s_[a + k];
        hdLen_ = b - a;
        hdStrip_ = strip;
        hdQuoted_ = quoted;
        hdPending_ = true;
        em_.Add(i, j, K::Keyword);
        return j;
    }

    // i is the start of the first body line. Returns the end of the terminator line.
    size_t Heredoc(size_t i) {
        const wsv delim(hd_, hdLen_);
        while (i < n_) {
            const size_t le = LineEnd(s_, n_, i);
            const size_t ce = ContentEnd(s_, i, le);
            size_t a = i;
            if (hdStrip_)
                while (a < ce && (s_[a] == L'\t' || s_[a] == L' ')) ++a;
            if (ce - a == delim.size() && At(s_, ce, a, delim)) {
                em_.Add(a, ce, K::Keyword);
                return le;
            }
            if (hdQuoted_) {
                em_.Add(i, ce, K::String);
            } else {
                size_t seg = i;
                for (size_t k = i; k < ce;) {
                    if (s_[k] == L'$') {
                        const size_t e = VarEnd(k);
                        if (e > k && e <= ce) {
                            em_.Add(seg, k, K::String);
                            em_.Add(k, e, K::Variable);
                            k = seg = e;
                            continue;
                        }
                    }
                    ++k;
                }
                em_.Add(seg, ce, K::String);
            }
            i = le + 1;
        }
        return n_;
    }

    size_t WordTok(size_t i) {
        size_t e = i;
        while (e < n_ && IsWordChar(s_[e])) ++e;
        const wsv w(s_ + i, e - i);
        if ((cmdPos_ || declCtx_) && IsIdStart(s_[i])) {
            const size_t k = ScanIdent(s_, n_, i);
            if (k < e && (s_[k] == L'=' || s_[k] == L'[' || (s_[k] == L'+' && k + 1 < e && s_[k + 1] == L'='))) {
                em_.Add(i, k, K::Variable);
                cmdPos_ = e < n_ && IsBlank(s_[e]) && !declCtx_;
                return e;
            }
        }
        if (cmdPos_) {
            if (InSet(kShKw, w, false)) {
                em_.Add(i, e, K::Keyword);
                cmdPos_ = InSet(kShCmdKw, w, false);
                if (w == L"for" || w == L"select" || w == L"case") inCount_ = 2;
                if (w == L"function") funcNext_ = true;
                if (InSet(kShDeclKw, w, false)) declCtx_ = true;
                return e;
            }
            cmdPos_ = false;
            const size_t k = SkipBlanks(s_, n_, e);
            if (k + 1 < n_ && s_[k] == L'(' && s_[k + 1] == L')') em_.Add(i, e, K::Function);
            else if (InSet(kShBuiltins, w, false)) em_.Add(i, e, K::Function);
            else if (AllDigits(s_, i, e)) em_.Add(i, e, K::Number);
            return e;
        }
        if (funcNext_) {
            funcNext_ = false;
            em_.Add(i, e, K::Function);
            return e;
        }
        if (inCount_ > 0) {
            if (w == L"in") {
                inCount_ = 0;
                em_.Add(i, e, K::Keyword);
                return e;
            }
            --inCount_;
        }
        if (AllDigits(s_, i, e)) em_.Add(i, e, K::Number);
        return e;
    }

    const wchar_t* s_;
    size_t n_;
    Emitter& em_;
    bool cmdPos_ = true;
    bool declCtx_ = false;
    bool funcNext_ = false;
    int inCount_ = 0;
    int arith_ = 0;  // open $(( / (( on this line
    LineFinder lf_;
    wchar_t hd_[64] = {};
    size_t hdLen_ = 0;
    bool hdPending_ = false, hdStrip_ = false, hdQuoted_ = false;
};

void ScanShell(const wchar_t* s, size_t n, Emitter& em) {
    ShellScanner sc(s, n, em);
    sc.Run();
}

// ---------------------------------------------------------------------------
// PowerShell
// ---------------------------------------------------------------------------
class PsScanner {
public:
    PsScanner(const wchar_t* s, size_t n, Emitter& em) : s_(s), n_(n), em_(em) {}

    void Run() {
        size_t i = 0;
        while (i < n_) {
            const wchar_t c = s_[i];
            const wchar_t c1 = i + 1 < n_ ? s_[i + 1] : L'\0';
            if (IsSpaceOrNl(c)) {
                if (c == L'\n') funcNext_ = false;
                ++i;
                continue;
            }
            if (c == L'<' && c1 == L'#') {
                const size_t p = Find(s_, n_, i + 2, L"#>");
                const size_t e = p == kNpos ? n_ : p + 2;
                em_.Add(i, e, K::Comment);
                i = e;
                continue;
            }
            if (c == L'#') {
                const size_t e = LineEnd(s_, n_, i);
                em_.Add(i, e, K::Comment);
                i = e;
                continue;
            }
            if (c == L'@' && (c1 == L'"' || c1 == L'\'')) {  // here-string
                size_t end = n_;
                for (size_t p = i + 2;;) {
                    const size_t nl = FindCh(s_, p, n_, L'\n');
                    if (nl == kNpos) break;
                    const size_t ls = SkipBlanks(s_, n_, nl + 1);
                    if (ls + 1 < n_ && s_[ls] == c1 && s_[ls + 1] == L'@') {
                        end = ls + 2;
                        break;
                    }
                    p = nl + 1;
                }
                if (c1 == L'"') StringWithVars(i, end);
                else em_.Add(i, end, K::String);
                i = end;
                continue;
            }
            if (c == L'"') {
                size_t j = i + 1;
                size_t e = n_;
                while (j < n_) {
                    if (s_[j] == L'`') {
                        j += 2;
                        continue;
                    }
                    if (s_[j] == L'"') {
                        if (j + 1 < n_ && s_[j + 1] == L'"') {
                            j += 2;
                            continue;
                        }
                        e = j + 1;
                        break;
                    }
                    ++j;
                }
                StringWithVars(i, e);
                i = e;
                continue;
            }
            if (c == L'\'') {
                size_t j = i + 1;
                size_t e = n_;
                while (j < n_) {
                    if (s_[j] == L'\'') {
                        if (j + 1 < n_ && s_[j + 1] == L'\'') {
                            j += 2;
                            continue;
                        }
                        e = j + 1;
                        break;
                    }
                    ++j;
                }
                em_.Add(i, e, K::String);
                i = e;
                continue;
            }
            if (c == L'$') {
                const size_t e = VarEnd(i);
                if (e > i) {
                    const wsv w(s_ + i + 1, e - i - 1);
                    const bool cst = EqCI(w.data(), w.size(), L"true") || EqCI(w.data(), w.size(), L"false") ||
                                     EqCI(w.data(), w.size(), L"null");
                    em_.Add(i, e, cst ? K::Constant : K::Variable);
                    i = e;
                    continue;
                }
                ++i;
                continue;
            }
            if (c == L'[' && (IsIdStart(c1) || c1 == L' ')) {
                const size_t j = SkipBlanks(s_, n_, i + 1);
                if (j < n_ && IsIdStart(s_[j])) {
                    size_t k = j, depth = 1;
                    while (k < n_ && depth > 0 && k - j < 256) {  // bounded: type names are short
                        const wchar_t d = s_[k];
                        if (d == L'[') ++depth;
                        else if (d == L']') --depth;
                        else if (!(IsIdChar(d) || d == L'.' || d == L',' || d == L'`' || d == L' ')) break;
                        if (depth > 0) ++k;
                    }
                    if (k < n_ && depth == 0) {
                        em_.Add(j, TrimRight(s_, j, k), K::Type);
                        i = k + 1;
                        continue;
                    }
                }
                ++i;
                continue;
            }
            if (c == L'-' && IsAlpha(c1) && (i == 0 || !IsIdChar(s_[i - 1]))) {
                const size_t e = ScanIdent(s_, n_, i + 1);
                if (InSet(kPsOps, s_ + i + 1, e - i - 1, true)) em_.Add(i, e, K::Keyword);
                i = e;
                continue;
            }
            if (IsDigit(c) || (c == L'.' && IsDigit(c1) && !(i > 0 && IsIdChar(s_[i - 1])))) {
                const size_t e = ScanNumber(s_, n_, i, false);
                em_.Add(i, e, K::Number);
                i = e;
                continue;
            }
            if (IsIdStart(c)) {
                size_t e = i;
                bool dash = false;
                while (e < n_) {
                    if (IsIdChar(s_[e])) {
                        ++e;
                    } else if (s_[e] == L'-' && e + 1 < n_ && IsAlpha(s_[e + 1])) {
                        dash = true;
                        ++e;
                    } else {
                        break;
                    }
                }
                const bool afterDot = i > 0 && s_[i - 1] == L'.';
                K kind = K::Plain;
                if (dash && !afterDot) {
                    kind = K::Function;
                } else if (funcNext_) {
                    kind = K::Function;
                    funcNext_ = false;
                } else if (!afterDot && InSet(kPsKw, s_ + i, e - i, true)) {
                    kind = K::Keyword;
                    funcNext_ = EqCI(s_ + i, e - i, L"function") || EqCI(s_ + i, e - i, L"filter");
                } else {
                    const size_t k = SkipBlanks(s_, n_, e);
                    if (k < n_ && s_[k] == L'(' && afterDot) kind = K::Function;
                }
                em_.Add(i, e, kind);
                i = e;
                continue;
            }
            ++i;
        }
    }

private:
    size_t VarEnd(size_t i) {
        const wchar_t c1 = i + 1 < n_ ? s_[i + 1] : L'\0';
        if (c1 == L'{') {
            const size_t cb = lf_.Find(s_, n_, i + 2, L'}');
            return cb == kNpos ? i : cb + 1;
        }
        if (IsIdStart(c1)) {
            size_t e = ScanIdent(s_, n_, i + 1);
            if (e + 1 < n_ && s_[e] == L':' && IsIdStart(s_[e + 1])) e = ScanIdent(s_, n_, e + 1);
            return e;
        }
        if (c1 == L'$' || c1 == L'?' || c1 == L'^' || c1 == L'(') return i + 2;
        return i;
    }

    void StringWithVars(size_t a, size_t e) {
        size_t seg = a;
        for (size_t k = a + 1; k < e;) {
            const wchar_t c = s_[k];
            if (c == L'`' && k + 2 <= e) {
                em_.Add(seg, k, K::String);
                em_.Add(k, k + 2, K::Escape);
                k = seg = k + 2;
                continue;
            }
            if (c == L'$') {
                const size_t ve = VarEnd(k);
                if (ve > k && ve <= e) {
                    em_.Add(seg, k, K::String);
                    em_.Add(k, ve, K::Variable);
                    k = seg = ve;
                    continue;
                }
            }
            ++k;
        }
        em_.Add(seg, e, K::String);
    }

    const wchar_t* s_;
    size_t n_;
    Emitter& em_;
    bool funcNext_ = false;
    LineFinder lf_;
};

void ScanPowerShell(const wchar_t* s, size_t n, Emitter& em) {
    PsScanner sc(s, n, em);
    sc.Run();
}

// ---------------------------------------------------------------------------
// Batch (.bat / .cmd)
// ---------------------------------------------------------------------------
// s[k] is '%' or '!'. Returns the end of a variable reference, or k.
size_t BatVarEnd(const wchar_t* s, size_t k, size_t ce) {
    const wchar_t c1 = k + 1 < ce ? s[k + 1] : L'\0';
    if (s[k] == L'%') {
        if (c1 == L'%') return (k + 2 < ce && IsAlpha(s[k + 2])) ? k + 3 : k;
        if (c1 == L'~') {
            size_t e = k + 2;
            while (e < ce && IsAlpha(s[e])) ++e;
            if (e < ce && IsDigit(s[e])) return e + 1;
            return k;
        }
        if (IsDigit(c1) || c1 == L'*') return k + 2;
        size_t e = k + 1;
        while (e < ce && s[e] != L'%' && s[e] != L'"' && !IsBlank(s[e])) ++e;
        return (e < ce && s[e] == L'%' && e > k + 1) ? e + 1 : k;
    }
    size_t e = k + 1;
    while (e < ce && (IsIdChar(s[e]) || s[e] == L':' || s[e] == L'~' || s[e] == L',' || s[e] == L'-')) ++e;
    return (e < ce && s[e] == L'!' && e > k + 1) ? e + 1 : k;
}

void BatchLine(const wchar_t* s, size_t i, size_t ce, Emitter& em) {
    size_t j = SkipBlanks(s, ce, i);
    if (j < ce && s[j] == L'@') j = SkipBlanks(s, ce, j + 1);
    if (j >= ce) return;
    if (At(s, ce, j, L"::")) {
        em.Add(j, ce, K::Comment);
        return;
    }
    if (s[j] == L':') {
        size_t e = j + 1;
        while (e < ce && !IsBlank(s[e])) ++e;
        em.Add(j, e, K::Function);
        return;
    }
    bool cmdPos = true, text = false, setName = false;
    int labelNext = 0;  // 1 = after goto, 2 = after call
    size_t k = j;
    while (k < ce) {
        const wchar_t c = s[k];
        if (c == L'%' || c == L'!') {
            const size_t e = BatVarEnd(s, k, ce);
            if (e > k) {
                em.Add(k, e, K::Variable);
                k = e;
                continue;
            }
            ++k;
            continue;
        }
        if (c == L'&' || c == L'|' || c == L'(' || c == L')' || c == L'>' || c == L'<') {
            if (c != L'>' && c != L'<') cmdPos = true;
            if (c != L'(') text = false;
            ++k;
            continue;
        }
        if (text) {
            ++k;
            continue;
        }
        if (c == L'"') {
            size_t e = FindCh(s, k + 1, ce, L'"');
            e = e == kNpos ? ce : e + 1;
            size_t seg = k;
            for (size_t m = k + 1; m < e;) {
                if (s[m] == L'%' || s[m] == L'!') {
                    const size_t ve = BatVarEnd(s, m, e);
                    if (ve > m) {
                        em.Add(seg, m, K::String);
                        em.Add(m, ve, K::Variable);
                        m = seg = ve;
                        continue;
                    }
                }
                ++m;
            }
            em.Add(seg, e, K::String);
            k = e;
            cmdPos = false;
            continue;
        }
        if (IsBlank(c) || c == L'=' || c == L',' || c == L';') {
            ++k;
            continue;
        }
        size_t e = k;
        while (e < ce && !IsBlank(s[e]) && s[e] != L'&' && s[e] != L'|' && s[e] != L'(' && s[e] != L')' &&
               s[e] != L'<' && s[e] != L'>' && s[e] != L'=' && s[e] != L'"' && s[e] != L'%' && s[e] != L'!' &&
               s[e] != L',' && s[e] != L';')
            ++e;
        if (e == k) {
            ++k;
            continue;
        }
        const wsv w(s + k, e - k);
        if (labelNext != 0) {
            if (labelNext == 1 || s[k] == L':') em.Add(k, e, K::Function);
            labelNext = 0;
        } else if (setName && s[k] != L'/') {
            em.Add(k, e, K::Variable);
            setName = false;
            if (e < ce && s[e] == L'=') text = true;
        } else if (cmdPos && EqCI(w.data(), w.size(), L"rem") && (e == ce || IsBlank(s[e]))) {
            em.Add(k, ce, K::Comment);
            return;
        } else if (InSet(kBatKw, w, true)) {
            em.Add(k, e, K::Keyword);
            if (cmdPos && EqCI(w.data(), w.size(), L"echo")) text = true;
            else if (cmdPos && EqCI(w.data(), w.size(), L"set")) setName = true;
            else if (EqCI(w.data(), w.size(), L"goto")) labelNext = 1;
            else if (EqCI(w.data(), w.size(), L"call")) labelNext = 2;
        } else if (AllDigits(s, k, e)) {
            em.Add(k, e, K::Number);
        }
        cmdPos = false;
        k = e;
    }
}

void ScanBatch(const wchar_t* s, size_t n, Emitter& em) {
    size_t i = 0;
    while (i < n) {
        const size_t le = LineEnd(s, n, i);
        BatchLine(s, i, ContentEnd(s, i, le), em);
        i = le + 1;
    }
}

// ---------------------------------------------------------------------------
// Console / shell-session: prompt lines are commands, the rest is output.
// ---------------------------------------------------------------------------
// Returns the end of the prompt at the start of [a, ce), or 0. lang: 0 sh, 1 ps, 2 cmd.
size_t PromptEnd(const wchar_t* s, size_t a, size_t ce, int& lang) {
    lang = 0;
    if (a >= ce) return 0;
    const wchar_t c = s[a];
    if ((c == L'$' || c == L'#' || c == L'>' || c == L'%' || U(c) == 0x276F || U(c) == 0x279C || U(c) == 0x03BB) &&
        (a + 1 == ce || s[a + 1] == L' '))
        return a + 1 < ce ? a + 2 : a + 1;
    if (At(s, ce, a, L"PS ")) {
        const size_t gt = FindCh(s, a + 3, ce, L'>');
        if (gt == kNpos) return 0;
        lang = 1;
        return (gt + 1 < ce && s[gt + 1] == L' ') ? gt + 2 : gt + 1;
    }
    if (IsAlpha(c) && a + 2 < ce && s[a + 1] == L':' && s[a + 2] == L'\\') {  // cmd.exe: C:\dir>command
        const size_t lim = ce < a + 260 ? ce : a + 260;
        for (size_t k = a + 3; k < lim; ++k) {
            if (s[k] == L'>') {
                lang = 2;
                return (k + 1 < ce && s[k + 1] == L' ') ? k + 2 : k + 1;
            }
            if (s[k] == L'<' || s[k] == L'|' || s[k] == L'"') break;
        }
        return 0;
    }
    bool bracket = c == L'[';
    bool mark = bracket;
    const size_t lim = ce < a + 160 ? ce : a + 160;
    for (size_t k = a; k < lim; ++k) {
        const wchar_t d = s[k];
        if (bracket) {
            if (d == L']') bracket = false;
            continue;
        }
        if (d == L'@' || d == L':' || d == L'~' || d == L'\\') mark = true;
        if ((d == L'$' || d == L'#' || d == L'>' || d == L'%') && k > a && mark && (k + 1 == ce || s[k + 1] == L' '))
            return k + 1 < ce ? k + 2 : k + 1;
        if (IsBlank(d)) return 0;
    }
    return 0;
}

void ScanConsole(const wchar_t* s, size_t n, Emitter& em) {
    size_t i = 0;
    bool cont = false;
    int contLang = 0;
    while (i < n) {
        const size_t le = LineEnd(s, n, i);
        const size_t ce = ContentEnd(s, i, le);
        size_t start = kNpos;
        int lang = contLang;
        if (cont) {
            start = i;
        } else if (const size_t p = PromptEnd(s, i, ce, lang)) {
            em.Add(i, p, K::Meta);
            start = p;
        }
        cont = false;
        if (start != kNpos && start < ce) {
            {
                BaseShift shift(em, start);
                if (lang == 1) ScanPowerShell(s + start, ce - start, em);
                else if (lang == 2) ScanBatch(s + start, ce - start, em);
                else ScanShell(s + start, ce - start, em);
            }
            const size_t te = TrimRight(s, start, ce);
            cont = te > start && (s[te - 1] == L'\\' || (lang == 1 && s[te - 1] == L'`') ||
                                  (lang == 2 && s[te - 1] == L'^'));
        }
        contLang = lang;
        i = le + 1;
    }
}

// ---------------------------------------------------------------------------
// Dockerfile
// ---------------------------------------------------------------------------
void DockerArgs(const wchar_t* s, size_t a, size_t end, Emitter& em, bool isFrom, bool keyValue) {
    size_t k = a;
    bool noBrace = false;  // no '}' left before `end`
    while (k < end) {
        const wchar_t c = s[k];
        const wchar_t c1 = k + 1 < end ? s[k + 1] : L'\0';
        if (c == L'"' || c == L'\'') {
            const size_t e = QuotedEnd(s, end, k, c == L'"');
            if (c == L'"') EmitEscapedString(s, k, e, em);
            else em.Add(k, e, K::String);
            k = e;
            continue;
        }
        if (c == L'$') {
            if (c1 == L'{') {
                const size_t cb = noBrace ? kNpos : FindCh(s, k + 2, end, L'}');
                if (cb != kNpos) {
                    em.Add(k, cb + 1, K::Variable);
                    k = cb + 1;
                    continue;
                }
                noBrace = true;
            } else if (IsIdStart(c1)) {
                const size_t e = ScanIdent(s, end, k + 1);
                em.Add(k, e, K::Variable);
                k = e;
                continue;
            }
            ++k;
            continue;
        }
        if (c == L'-' && c1 == L'-' && (k == a || IsSpaceOrNl(s[k - 1]))) {
            size_t e = k;
            while (e < end && !IsSpaceOrNl(s[e]) && s[e] != L'=') ++e;
            em.Add(k, e, K::Constant);
            k = e;
            continue;
        }
        if (IsIdStart(c) && (k == a || !IsIdChar(s[k - 1]))) {
            size_t e = k;
            while (e < end && (IsIdChar(s[e]) || s[e] == L'.' || s[e] == L'-')) ++e;
            if (isFrom && EqCI(s + k, e - k, L"as")) em.Add(k, e, K::Keyword);
            else if (keyValue && e < end && s[e] == L'=') em.Add(k, e, K::Property);
            k = e;
            continue;
        }
        if (IsDigit(c) && (k == a || IsBlank(s[k - 1]) || s[k - 1] == L'[')) {
            size_t e = k;
            while (e < end && IsDigit(s[e])) ++e;
            if (e == end || (!IsIdChar(s[e]) && s[e] != L'.' && s[e] != L':')) em.Add(k, e, K::Number);
            k = e;
            continue;
        }
        ++k;
    }
}

void ScanDockerfile(const wchar_t* s, size_t n, Emitter& em) {
    size_t i = 0;
    while (i < n) {
        size_t le = LineEnd(s, n, i);
        const size_t ce = ContentEnd(s, i, le);
        const size_t j = SkipBlanks(s, ce, i);
        if (j >= ce) {
            i = le + 1;
            continue;
        }
        if (s[j] == L'#') {
            em.Add(j, ce, K::Comment);
            i = le + 1;
            continue;
        }
        // Extent of the instruction including '\' continuation lines.
        size_t ls = i, cur = ce;
        for (;;) {
            const size_t te = TrimRight(s, ls, cur);
            if (te == ls || s[te - 1] != L'\\' || le >= n) break;
            ls = le + 1;
            le = LineEnd(s, n, ls);
            cur = ContentEnd(s, ls, le);
        }
        const size_t end = cur;
        size_t w = j;
        while (w < end && IsAlpha(s[w])) ++w;
        wsv instr(s + j, w - j);
        if (InSet(kDockerKw, instr, true)) {
            em.Add(j, w, K::Keyword);
            size_t argStart = w;
            if (EqCI(instr.data(), instr.size(), L"onbuild") || EqCI(instr.data(), instr.size(), L"healthcheck")) {
                size_t k = SkipBlanks(s, end, w);
                while (k + 1 < end && s[k] == L'-' && s[k + 1] == L'-') {
                    while (k < end && !IsSpaceOrNl(s[k])) ++k;
                    k = SkipWs(s, end, k);
                }
                size_t w2 = k;
                while (w2 < end && IsAlpha(s[w2])) ++w2;
                if (w2 > k && InSet(kDockerKw, s + k, w2 - k, true)) {
                    if (k > w) DockerArgs(s, w, k, em, false, false);
                    em.Add(k, w2, K::Keyword);
                    instr = wsv(s + k, w2 - k);
                    argStart = w2;
                }
            }
            size_t a = SkipBlanks(s, end, argStart);
            const bool shellForm = EqCI(instr.data(), instr.size(), L"run") ||
                                   EqCI(instr.data(), instr.size(), L"cmd") ||
                                   EqCI(instr.data(), instr.size(), L"entrypoint");
            if (shellForm && a < end && s[a] != L'[') {
                const size_t optStart = a;
                while (a + 1 < end && s[a] == L'-' && s[a + 1] == L'-') {
                    while (a < end && !IsSpaceOrNl(s[a])) ++a;
                    a = SkipWs(s, end, a);
                }
                if (a > optStart) DockerArgs(s, optStart, a, em, false, false);
                if (a < end) {
                    BaseShift shift(em, a);
                    ScanShell(s + a, end - a, em);
                }
            } else {
                const bool kv = EqCI(instr.data(), instr.size(), L"env") || EqCI(instr.data(), instr.size(), L"arg") ||
                                EqCI(instr.data(), instr.size(), L"label");
                DockerArgs(s, a, end, em, EqCI(instr.data(), instr.size(), L"from"), kv);
            }
        } else {
            DockerArgs(s, j, end, em, false, false);
        }
        i = le + 1;
    }
}

// ---------------------------------------------------------------------------
// Makefile
// ---------------------------------------------------------------------------
inline bool IsMakeVarChar(wchar_t c) {
    return IsIdChar(c) || c == L'.' || c == L'-' || c == L'@' || c == L'<' || c == L'^' || c == L'*' ||
           c == L'?' || c == L'%' || c == L'+' || c == L'/';
}

// Variable references, comments and quoted strings inside [a, e).
void MakeText(const wchar_t* s, size_t a, size_t e, Emitter& em) {
    size_t k = a;
    while (k < e) {
        const wchar_t c = s[k];
        if (c == L'#' && (k == a || IsBlank(s[k - 1])) && !(k > 0 && s[k - 1] == L'\\')) {
            em.Add(k, e, K::Comment);
            return;
        }
        if (c == L'$' && k + 1 < e) {
            const wchar_t d = s[k + 1];
            if (d == L'$') {
                k += 2;
                continue;
            }
            if (d == L'(' || d == L'{') {
                const wchar_t close = d == L'(' ? L')' : L'}';
                size_t m = k + 2;
                while (m < e && (IsMakeVarChar(s[m]) || (m > k + 2 && (s[m] == L':' || s[m] == L'=')))) ++m;
                if (m < e && s[m] == close) {
                    em.Add(k, m + 1, K::Variable);
                    k = m + 1;
                    continue;
                }
                em.Add(k, k + 2, K::Variable);
                if (m > k + 2 && m < e && IsBlank(s[m])) em.Add(k + 2, m, K::Function);
                k = m > k + 2 ? m : k + 2;
                continue;
            }
            if (IsMakeVarChar(d) || d == L'|') {
                em.Add(k, k + 2, K::Variable);
                k += 2;
                continue;
            }
        }
        if (c == L'"' || c == L'\'') {
            const size_t q = FindCh(s, k + 1, e, c);
            if (q != kNpos) {
                em.Add(k, q + 1, K::String);
                k = q + 1;
                continue;
            }
        }
        ++k;
    }
}

void ScanMakefile(const wchar_t* s, size_t n, Emitter& em) {
    size_t i = 0;
    bool inDefine = false;
    while (i < n) {
        const size_t le = LineEnd(s, n, i);
        const size_t ce = ContentEnd(s, i, le);
        size_t k = SkipBlanks(s, ce, i);
        if (inDefine) {
            if (At(s, ce, k, L"endef")) {
                em.Add(k, k + 5, K::Keyword);
                inDefine = false;
            } else {
                MakeText(s, i, ce, em);
            }
        } else if (i < ce && s[i] == L'\t') {  // recipe line
            while (k < ce && (s[k] == L'@' || s[k] == L'-' || s[k] == L'+')) ++k;
            MakeText(s, k, ce, em);
        } else if (k < ce && s[k] == L'#') {
            em.Add(k, ce, K::Comment);
        } else if (k < ce) {
            // Directive keywords (possibly prefixing an assignment: export/override/define).
            for (int guard = 0; guard < 3; ++guard) {
                size_t w = k;
                while (w < ce && (IsIdChar(s[w]) || s[w] == L'-' || s[w] == L'.')) ++w;
                if (w == k || !InSet(kMakeKw, s + k, w - k, false) ||
                    !(w == ce || IsBlank(s[w]) || s[w] == L'(' || s[w] == L':'))
                    break;
                em.Add(k, w, K::Keyword);
                const wsv word(s + k, w - k);
                if (word == L"define") inDefine = true;
                k = SkipBlanks(s, ce, w);
                if (!(word == L"export" || word == L"override" || word == L"define" || word == L"private" ||
                      word == L"unexport" || word == L"undefine"))
                    break;
            }
            // Assignment?
            size_t m = k;
            while (m < ce && !IsBlank(s[m]) && s[m] != L'=' && s[m] != L':' && s[m] != L'?' && s[m] != L'+' &&
                   s[m] != L'!' && s[m] != L'#')
                ++m;
            const size_t o = SkipBlanks(s, ce, m);
            const bool assign = m > k && o < ce &&
                                (s[o] == L'=' || At(s, ce, o, L":=") || At(s, ce, o, L"::=") || At(s, ce, o, L"?=") ||
                                 At(s, ce, o, L"+=") || At(s, ce, o, L"!="));
            if (assign || (inDefine && m > k)) {
                em.Add(k, m, K::Variable);
                MakeText(s, o, ce, em);
            } else {
                size_t colon = kNpos;
                for (size_t q = k; q < ce; ++q) {
                    if (s[q] == L'#' || s[q] == L'=' || s[q] == L';') break;
                    if (s[q] == L':' && !(q + 1 < ce && s[q + 1] == L'=')) {
                        colon = q;
                        break;
                    }
                }
                if (colon != kNpos) {
                    for (size_t t = k; t < colon;) {  // targets
                        if (IsBlank(s[t])) {
                            ++t;
                            continue;
                        }
                        size_t te = t;
                        while (te < colon && !IsBlank(s[te])) ++te;
                        if (s[t] == L'$') MakeText(s, t, te, em);
                        else em.Add(t, te, InSet(kMakeKw, s + t, te - t, false) ? K::Keyword : K::Function);
                        t = te;
                    }
                    MakeText(s, colon + 1, ce, em);
                } else {
                    MakeText(s, k, ce, em);
                }
            }
        }
        i = le + 1;
    }
}

// ---------------------------------------------------------------------------
// CMake
// ---------------------------------------------------------------------------
// "[[" / "[==[" at i: returns level + 1, else 0.
size_t BracketLevelAt(const wchar_t* s, size_t n, size_t i) {
    if (i >= n || s[i] != L'[') return 0;
    size_t k = i + 1;
    while (k < n && s[k] == L'=') ++k;
    return (k < n && s[k] == L'[') ? k - i : 0;
}
size_t BracketEndAt(const wchar_t* s, size_t n, size_t j, size_t level) {
    while (j < n) {
        if (s[j] == L']') {
            size_t k = j + 1, eq = 0;
            while (k < n && s[k] == L'=') {
                ++k;
                ++eq;
            }
            if (eq == level && k < n && s[k] == L']') return k + 1;
            j = k;
            continue;
        }
        ++j;
    }
    return n;
}

void ScanCMake(const wchar_t* s, size_t n, Emitter& em) {
    LineFinder lf;
    // ${VAR}, $ENV{VAR}, $CACHE{VAR} at j (s[j] == '$'); returns end or j.
    auto varEnd = [&](size_t j) -> size_t {
        size_t b = kNpos;
        if (j + 1 < n && s[j + 1] == L'{') b = j + 2;
        else if (At(s, n, j, L"$ENV{")) b = j + 5;
        else if (At(s, n, j, L"$CACHE{")) b = j + 7;
        if (b == kNpos) return j;
        const size_t cb = lf.Find(s, n, b, L'}');
        return cb == kNpos ? j : cb + 1;
    };
    size_t i = 0;
    while (i < n) {
        const wchar_t c = s[i];
        const wchar_t c1 = i + 1 < n ? s[i + 1] : L'\0';
        if (c == L'#') {
            size_t e;
            if (const size_t lvl = BracketLevelAt(s, n, i + 1)) e = BracketEndAt(s, n, i + 1 + lvl + 1, lvl - 1);
            else e = LineEnd(s, n, i);
            em.Add(i, e, K::Comment);
            i = e;
            continue;
        }
        if (c == L'[') {
            if (const size_t lvl = BracketLevelAt(s, n, i)) {
                const size_t e = BracketEndAt(s, n, i + lvl + 1, lvl - 1);
                em.Add(i, e, K::String);
                i = e;
                continue;
            }
        }
        if (c == L'"') {
            size_t seg = i, j = i + 1;
            while (j < n && s[j] != L'"') {
                if (s[j] == L'\\') {
                    const size_t ee = j + 2 < n ? j + 2 : n;
                    em.Add(seg, j, K::String);
                    em.Add(j, ee, K::Escape);
                    j = seg = ee;
                    continue;
                }
                if (s[j] == L'$') {
                    const size_t ve = varEnd(j);
                    if (ve > j) {
                        em.Add(seg, j, K::String);
                        em.Add(j, ve, K::Variable);
                        j = seg = ve;
                        continue;
                    }
                }
                ++j;
            }
            const size_t e = j < n ? j + 1 : n;
            em.Add(seg, e, K::String);
            i = e;
            continue;
        }
        if (c == L'$') {
            const size_t ve = varEnd(i);
            if (ve > i) {
                em.Add(i, ve, K::Variable);
                i = ve;
                continue;
            }
            if (c1 == L'<') {
                const size_t e = ScanIdent(s, n, i + 2);
                em.Add(i, e, K::Meta);
                i = e > i + 2 ? e : i + 2;
                continue;
            }
            ++i;
            continue;
        }
        if (c == L'@' && IsIdStart(c1)) {
            const size_t e = ScanIdent(s, n, i + 1);
            if (e < n && s[e] == L'@') {
                em.Add(i, e + 1, K::Variable);
                i = e + 1;
                continue;
            }
        }
        if (IsDigit(c) && !(i > 0 && (IsIdChar(s[i - 1]) || s[i - 1] == L'-' || s[i - 1] == L'/' || s[i - 1] == L'.'))) {
            const size_t e = ScanNumber(s, n, i, false);
            em.Add(i, e, K::Number);
            i = e;
            continue;
        }
        if (IsIdStart(c)) {
            const size_t e = ScanIdent(s, n, i);
            const size_t k = SkipBlanks(s, n, e);
            if (k < n && s[k] == L'(') {
                em.Add(i, e, InSet(kCMakeKw, s + i, e - i, true) ? K::Keyword : K::Function);
            } else if (InSet(kCMakeConsts, s + i, e - i, false)) {
                em.Add(i, e, K::Constant);
            }
            i = e;
            continue;
        }
        ++i;
    }
}

// ---------------------------------------------------------------------------
// Assembly (NASM / MASM / GAS / ARM)
// ---------------------------------------------------------------------------
bool IsAsmRegister(const wchar_t* p, size_t len) {
    if (InSet(kAsmRegs, p, len, true)) return true;
    if (len < 2 || len > 7) return false;
    wchar_t buf[8] = {};
    for (size_t k = 0; k < len; ++k) buf[k] = Lower(p[k]);
    size_t a = 0;
    while (a < len && IsAlpha(buf[a])) ++a;
    size_t d = a;
    while (d < len && IsDigit(buf[d])) ++d;
    if (a == 0 || d == a || d - a > 2) return false;
    const wsv pre(buf, a);
    if (d < len) {
        if (len - d != 1 || pre != L"r" || (buf[d] != L'd' && buf[d] != L'w' && buf[d] != L'b')) return false;
    }
    return pre == L"r" || pre == L"x" || pre == L"w" || pre == L"v" || pre == L"q" || pre == L"d" ||
           pre == L"s" || pre == L"h" || pre == L"b" || pre == L"xmm" || pre == L"ymm" || pre == L"zmm" ||
           pre == L"mm" || pre == L"st" || pre == L"k" || pre == L"cr" || pre == L"dr";
}

inline bool IsAsmIdChar(wchar_t c) { return IsIdChar(c) || c == L'.' || c == L'@' || c == L'?' || c == L'$'; }

void ScanAsm(const wchar_t* s, size_t n, Emitter& em) {
    size_t i = 0;
    bool head = true;  // next word is a label / mnemonic / directive
    bool bol = true;
    while (i < n) {
        const wchar_t c = s[i];
        const wchar_t c1 = i + 1 < n ? s[i + 1] : L'\0';
        if (c == L'\n') {
            ++i;
            head = true;
            bol = true;
            continue;
        }
        if (IsBlank(c)) {
            ++i;
            continue;
        }
        const bool atBol = bol;
        bol = false;
        if (c == L';' || (c == L'/' && c1 == L'/') || (c == L'#' && atBol && !IsAlpha(c1))) {
            const size_t e = LineEnd(s, n, i);
            em.Add(i, e, K::Comment);
            i = e;
            continue;
        }
        if (c == L'/' && c1 == L'*') {
            const size_t p = Find(s, n, i + 2, L"*/");
            const size_t e = p == kNpos ? n : p + 2;
            em.Add(i, e, K::Comment);
            i = e;
            continue;
        }
        if ((c == L'#' || c == L'%') && atBol && IsAlpha(c1)) {  // #include / %define
            const size_t e = ScanIdent(s, n, i + 1);
            em.Add(i, e, K::Meta);
            head = false;
            i = e;
            continue;
        }
        if (c == L'"' || c == L'\'' || c == L'`') {
            const size_t e = QuotedEnd(s, n, i, true);
            em.Add(i, e, K::String);
            i = e;
            continue;
        }
        if (c == L'%' && IsAlpha(c1)) {  // AT&T register
            const size_t e = ScanIdent(s, n, i + 1);
            em.Add(i, e, K::Variable);
            i = e;
            continue;
        }
        if ((c == L'$' || c == L'#') && (IsDigit(c1) || c1 == L'-')) {  // immediates
            size_t e = i + 1;
            if (s[e] == L'-') ++e;
            if (e < n && IsDigit(s[e])) e = ScanNumber(s, n, e, false);
            em.Add(i, e, K::Number);
            i = e;
            continue;
        }
        if (IsDigit(c)) {
            size_t e = i;
            while (e < n && (IsAlnum(s[e]) || s[e] == L'_' || s[e] == L'.')) ++e;
            em.Add(i, e, K::Number);
            head = false;
            i = e;
            continue;
        }
        if (IsIdStart(c) || c == L'.' || c == L'@' || c == L'?' || c == L'$') {
            size_t e = i + 1;
            while (e < n && IsAsmIdChar(s[e])) ++e;
            const wsv w(s + i, e - i);
            if (head && e < n && s[e] == L':' && !(e + 1 < n && s[e + 1] == L':')) {
                em.Add(i, e, K::Function);
                i = e + 1;
                continue;
            }
            if (w[0] == L'.') {
                em.Add(i, e, w.size() > 1 ? K::Meta : K::Plain);
                head = false;
                i = e;
                continue;
            }
            if (head) {
                head = false;
                const bool dir = InSet(kAsmDirectives, w, true);
                if (!dir) {
                    // "msg db ...", "count dd 0", MASM "var DWORD 5": the head is a data label.
                    const size_t k = SkipBlanks(s, n, e);
                    size_t k2 = k;
                    while (k2 < n && IsAsmIdChar(s[k2])) ++k2;
                    bool label = k2 > k && InSet(kAsmDataDirs, s + k, k2 - k, true);
                    if (!label && k2 > k && InSet(kAsmSizes, s + k, k2 - k, true)) {
                        const size_t k3 = SkipBlanks(s, n, k2);
                        label = !(k3 < n && (s[k3] == L'[' || AtCI(s, n, k3, L"ptr") || s[k3] == L'\n' ||
                                             s[k3] == L';'));
                    }
                    if (label) {
                        em.Add(i, e, K::Function);
                        head = true;
                        i = e;
                        continue;
                    }
                }
                em.Add(i, e, dir ? K::Meta : K::Keyword);
                i = e;
                continue;
            }
            if (IsAsmRegister(w.data(), w.size())) em.Add(i, e, K::Variable);
            else if (InSet(kAsmSizes, w, true)) em.Add(i, e, K::Type);
            else if (InSet(kAsmDirectives, w, true)) em.Add(i, e, K::Meta);
            i = e;
            continue;
        }
        ++i;
    }
}

// ---------------------------------------------------------------------------
// LaTeX
// ---------------------------------------------------------------------------
void ScanLatex(const wchar_t* s, size_t n, Emitter& em) {
    LineFinder lf;
    size_t i = 0;
    while (i < n) {
        const wchar_t c = s[i];
        const wchar_t c1 = i + 1 < n ? s[i + 1] : L'\0';
        if (c == L'%') {
            const size_t e = LineEnd(s, n, i);
            em.Add(i, e, K::Comment);
            i = e;
            continue;
        }
        if (c == L'\\') {
            if (IsAlpha(c1)) {
                size_t e = i + 1;
                while (e < n && IsAlpha(s[e])) ++e;
                if (e < n && s[e] == L'*') ++e;
                em.Add(i, e, K::Keyword);
                const wsv w(s + i + 1, e - i - 1);
                if ((w == L"begin" || w == L"end") && e < n && s[e] == L'{') {
                    const size_t cb = lf.Find(s, n, e + 1, L'}');
                    if (cb != kNpos) {
                        em.Add(e + 1, cb, K::Type);
                        i = cb + 1;
                        continue;
                    }
                }
                i = e;
                continue;
            }
            if (c1 == L'(' || c1 == L'[') {
                const wchar_t close[2] = {L'\\', c1 == L'(' ? L')' : L']'};
                const size_t p = Find(s, n, i + 2, wsv(close, 2));
                const size_t e = p == kNpos ? n : p + 2;
                em.Add(i, e, K::String);
                i = e;
                continue;
            }
            if (c1 != L'\0' && c1 != L'\n') {
                em.Add(i, i + 2, K::Escape);
                i += 2;
                continue;
            }
            ++i;
            continue;
        }
        if (c == L'$') {
            size_t e;
            if (c1 == L'$') {
                size_t j = i + 2;
                e = n;
                while (j < n) {
                    if (s[j] == L'\\') {
                        j += 2;
                        continue;
                    }
                    if (s[j] == L'$' && j + 1 < n && s[j + 1] == L'$') {
                        e = j + 2;
                        break;
                    }
                    ++j;
                }
            } else {
                size_t j = i + 1;
                while (j < n && s[j] != L'$' && s[j] != L'\n') j += (s[j] == L'\\') ? 2 : 1;
                e = (j < n && s[j] == L'$') ? j + 1 : (j < n ? j : n);
            }
            em.Add(i, e, K::String);
            i = e;
            continue;
        }
        ++i;
    }
}

// ---------------------------------------------------------------------------
// Language table & dispatch
// ---------------------------------------------------------------------------
enum class Mode : uint8_t {
    Generic, Markup, Css, Scss, Sass, Json, Yaml, Toml, Ini, Props, Reg, Diff, Markdown,
    Shell, Console, PowerShell, Batch, Dockerfile, Makefile, CMake, Asm, Latex
};

struct Alias {
    wsv name;
    Mode mode;
    const LangDef* def;
};

#define MDV_G(name, def) Alias{name, Mode::Generic, &def}
#define MDV_M(name, mode) Alias{name, Mode::mode, nullptr}

constexpr Alias kAliases[] = {
    MDV_G(L"c", kLangC), MDV_G(L"h", kLangC),
    MDV_G(L"cpp", kLangCpp), MDV_G(L"c++", kLangCpp), MDV_G(L"cc", kLangCpp), MDV_G(L"cxx", kLangCpp),
    MDV_G(L"hpp", kLangCpp), MDV_G(L"hxx", kLangCpp), MDV_G(L"hh", kLangCpp), MDV_G(L"h++", kLangCpp),
    MDV_G(L"ino", kLangCpp), MDV_G(L"arduino", kLangCpp), MDV_G(L"cuda", kLangCpp), MDV_G(L"cu", kLangCpp),
    MDV_G(L"cuh", kLangCpp), MDV_G(L"ipp", kLangCpp), MDV_G(L"inl", kLangCpp), MDV_G(L"tpp", kLangCpp),
    MDV_G(L"objc", kLangObjC), MDV_G(L"objective-c", kLangObjC), MDV_G(L"objectivec", kLangObjC),
    MDV_G(L"obj-c", kLangObjC), MDV_G(L"m", kLangObjC), MDV_G(L"mm", kLangObjC),
    MDV_G(L"objective-c++", kLangObjC), MDV_G(L"objc++", kLangObjC), MDV_G(L"objcpp", kLangObjC),
    MDV_G(L"cs", kLangCs), MDV_G(L"csharp", kLangCs), MDV_G(L"c#", kLangCs), MDV_G(L"cake", kLangCs),
    MDV_G(L"csx", kLangCs), MDV_G(L"dotnet", kLangCs),
    MDV_G(L"java", kLangJava), MDV_G(L"jsh", kLangJava),
    MDV_G(L"kotlin", kLangKotlin), MDV_G(L"kt", kLangKotlin), MDV_G(L"kts", kLangKotlin),
    MDV_G(L"scala", kLangScala), MDV_G(L"sc", kLangScala), MDV_G(L"sbt", kLangScala),
    MDV_G(L"groovy", kLangGroovy), MDV_G(L"gradle", kLangGroovy), MDV_G(L"gvy", kLangGroovy),
    MDV_G(L"jenkinsfile", kLangGroovy),
    MDV_G(L"swift", kLangSwift), MDV_G(L"go", kLangGo), MDV_G(L"golang", kLangGo),
    MDV_G(L"rust", kLangRust), MDV_G(L"rs", kLangRust), MDV_G(L"zig", kLangZig), MDV_G(L"dart", kLangDart),
    MDV_G(L"d", kLangD), MDV_G(L"dlang", kLangD),
    MDV_G(L"glsl", kLangGlsl), MDV_G(L"vert", kLangGlsl), MDV_G(L"frag", kLangGlsl), MDV_G(L"hlsl", kLangHlsl),
    MDV_G(L"wgsl", kLangWgsl), MDV_G(L"solidity", kLangSolidity), MDV_G(L"sol", kLangSolidity),
    MDV_G(L"verilog", kLangVerilog), MDV_G(L"systemverilog", kLangVerilog), MDV_G(L"sv", kLangVerilog),
    MDV_G(L"svh", kLangVerilog), MDV_G(L"vhdl", kLangVhdl), MDV_G(L"vhd", kLangVhdl),
    MDV_G(L"js", kLangJs), MDV_G(L"javascript", kLangJs), MDV_G(L"jsx", kLangJs), MDV_G(L"mjs", kLangJs),
    MDV_G(L"cjs", kLangJs), MDV_G(L"node", kLangJs), MDV_G(L"nodejs", kLangJs), MDV_G(L"es6", kLangJs),
    MDV_G(L"ts", kLangTs), MDV_G(L"typescript", kLangTs), MDV_G(L"tsx", kLangTs), MDV_G(L"mts", kLangTs),
    MDV_G(L"cts", kLangTs),
    MDV_G(L"python", kLangPython), MDV_G(L"py", kLangPython), MDV_G(L"py3", kLangPython),
    MDV_G(L"python3", kLangPython), MDV_G(L"pyw", kLangPython), MDV_G(L"pyi", kLangPython),
    MDV_G(L"gyp", kLangPython), MDV_G(L"bazel", kLangPython), MDV_G(L"starlark", kLangPython),
    MDV_G(L"bzl", kLangPython), MDV_G(L"ipython", kLangPython), MDV_G(L"sage", kLangPython),
    MDV_G(L"ruby", kLangRuby), MDV_G(L"rb", kLangRuby), MDV_G(L"gemspec", kLangRuby),
    MDV_G(L"podspec", kLangRuby), MDV_G(L"rake", kLangRuby), MDV_G(L"gemfile", kLangRuby),
    MDV_G(L"crystal", kLangCrystal), MDV_G(L"cr", kLangCrystal),
    MDV_G(L"elixir", kLangElixir), MDV_G(L"ex", kLangElixir), MDV_G(L"exs", kLangElixir),
    MDV_G(L"erlang", kLangErlang), MDV_G(L"erl", kLangErlang), MDV_G(L"hrl", kLangErlang),
    MDV_G(L"php", kLangPhp), MDV_G(L"perl", kLangPerl), MDV_G(L"pl", kLangPerl), MDV_G(L"pm", kLangPerl),
    MDV_G(L"lua", kLangLua), MDV_G(L"luau", kLangLua), MDV_G(L"r", kLangR), MDV_G(L"rscript", kLangR),
    MDV_G(L"julia", kLangJulia), MDV_G(L"jl", kLangJulia),
    MDV_G(L"haskell", kLangHaskell), MDV_G(L"hs", kLangHaskell),
    MDV_G(L"fsharp", kLangFSharp), MDV_G(L"fs", kLangFSharp), MDV_G(L"f#", kLangFSharp),
    MDV_G(L"fsx", kLangFSharp), MDV_G(L"fsi", kLangFSharp),
    MDV_G(L"ocaml", kLangOCaml), MDV_G(L"ml", kLangOCaml), MDV_G(L"mli", kLangOCaml),
    MDV_G(L"nim", kLangNim), MDV_G(L"nims", kLangNim),
    MDV_G(L"clojure", kLangClojure), MDV_G(L"clj", kLangClojure), MDV_G(L"cljs", kLangClojure),
    MDV_G(L"cljc", kLangClojure), MDV_G(L"edn", kLangClojure),
    MDV_G(L"lisp", kLangLisp), MDV_G(L"elisp", kLangLisp), MDV_G(L"emacs-lisp", kLangLisp),
    MDV_G(L"el", kLangLisp), MDV_G(L"scheme", kLangLisp), MDV_G(L"scm", kLangLisp),
    MDV_G(L"racket", kLangLisp), MDV_G(L"rkt", kLangLisp), MDV_G(L"common-lisp", kLangLisp),
    MDV_G(L"commonlisp", kLangLisp),
    MDV_G(L"fortran", kLangFortran), MDV_G(L"f90", kLangFortran), MDV_G(L"f95", kLangFortran),
    MDV_G(L"f03", kLangFortran), MDV_G(L"f08", kLangFortran),
    MDV_G(L"vb", kLangVb), MDV_G(L"vbnet", kLangVb), MDV_G(L"vb.net", kLangVb), MDV_G(L"vba", kLangVb),
    MDV_G(L"vbs", kLangVb), MDV_G(L"vbscript", kLangVb), MDV_G(L"visualbasic", kLangVb), MDV_G(L"bas", kLangVb),
    MDV_G(L"pascal", kLangPascal), MDV_G(L"delphi", kLangPascal), MDV_G(L"pas", kLangPascal),
    MDV_G(L"dpr", kLangPascal), MDV_G(L"lpr", kLangPascal), MDV_G(L"dpk", kLangPascal),
    MDV_G(L"freepascal", kLangPascal), MDV_G(L"objectpascal", kLangPascal), MDV_G(L"lazarus", kLangPascal),
    MDV_G(L"sql", kLangSql), MDV_G(L"mysql", kLangSql), MDV_G(L"pgsql", kLangSql), MDV_G(L"postgres", kLangSql),
    MDV_G(L"postgresql", kLangSql), MDV_G(L"sqlite", kLangSql), MDV_G(L"tsql", kLangSql),
    MDV_G(L"t-sql", kLangSql), MDV_G(L"plsql", kLangSql), MDV_G(L"plpgsql", kLangSql), MDV_G(L"mssql", kLangSql),
    MDV_G(L"psql", kLangSql), MDV_G(L"mariadb", kLangSql),
    MDV_G(L"graphql", kLangGraphql), MDV_G(L"gql", kLangGraphql),
    MDV_G(L"protobuf", kLangProto), MDV_G(L"proto", kLangProto), MDV_G(L"proto3", kLangProto),
    MDV_G(L"hcl", kLangHcl), MDV_G(L"terraform", kLangHcl), MDV_G(L"tf", kLangHcl), MDV_G(L"tfvars", kLangHcl),
    MDV_G(L"nix", kLangNix), MDV_G(L"nginx", kLangNginx), MDV_G(L"nginxconf", kLangNginx),
    MDV_M(L"html", Markup), MDV_M(L"htm", Markup), MDV_M(L"xhtml", Markup), MDV_M(L"xml", Markup),
    MDV_M(L"svg", Markup), MDV_M(L"xaml", Markup), MDV_M(L"plist", Markup), MDV_M(L"vue", Markup),
    MDV_M(L"svelte", Markup), MDV_M(L"xsl", Markup), MDV_M(L"xslt", Markup), MDV_M(L"xsd", Markup),
    MDV_M(L"rss", Markup), MDV_M(L"atom", Markup), MDV_M(L"wsdl", Markup), MDV_M(L"csproj", Markup),
    MDV_M(L"vbproj", Markup), MDV_M(L"fsproj", Markup), MDV_M(L"vcxproj", Markup), MDV_M(L"props", Markup),
    MDV_M(L"targets", Markup), MDV_M(L"nuspec", Markup), MDV_M(L"resx", Markup), MDV_M(L"aspx", Markup),
    MDV_M(L"cshtml", Markup), MDV_M(L"razor", Markup), MDV_M(L"jsp", Markup), MDV_M(L"erb", Markup),
    MDV_M(L"ejs", Markup), MDV_M(L"hbs", Markup), MDV_M(L"handlebars", Markup), MDV_M(L"mustache", Markup),
    MDV_M(L"twig", Markup), MDV_M(L"jinja", Markup), MDV_M(L"jinja2", Markup), MDV_M(L"njk", Markup),
    MDV_M(L"liquid", Markup), MDV_M(L"astro", Markup), MDV_M(L"mathml", Markup), MDV_M(L"kml", Markup),
    MDV_M(L"gpx", Markup), MDV_M(L"wxs", Markup), MDV_M(L"xib", Markup), MDV_M(L"storyboard", Markup),
    MDV_M(L"css", Css), MDV_M(L"pcss", Css), MDV_M(L"postcss", Css), MDV_M(L"scss", Scss), MDV_M(L"less", Scss),
    MDV_M(L"sass", Sass), MDV_M(L"stylus", Sass), MDV_M(L"styl", Sass),
    MDV_M(L"json", Json), MDV_M(L"jsonc", Json), MDV_M(L"json5", Json), MDV_M(L"jsonl", Json),
    MDV_M(L"ndjson", Json), MDV_M(L"geojson", Json), MDV_M(L"topojson", Json), MDV_M(L"webmanifest", Json),
    MDV_M(L"har", Json), MDV_M(L"babelrc", Json), MDV_M(L"eslintrc", Json),
    MDV_M(L"yaml", Yaml), MDV_M(L"yml", Yaml), MDV_M(L"toml", Toml),
    MDV_M(L"ini", Ini), MDV_M(L"cfg", Ini), MDV_M(L"conf", Ini), MDV_M(L"dosini", Ini),
    MDV_M(L"editorconfig", Ini), MDV_M(L"gitconfig", Ini), MDV_M(L"gitmodules", Ini), MDV_M(L"desktop", Ini),
    MDV_M(L"inf", Ini), MDV_M(L"npmrc", Ini), MDV_M(L"env", Ini), MDV_M(L"dotenv", Ini),
    MDV_M(L"service", Ini), MDV_M(L"systemd", Ini), MDV_M(L"properties", Props),
    MDV_M(L"reg", Reg), MDV_M(L"registry", Reg),
    MDV_M(L"diff", Diff), MDV_M(L"patch", Diff), MDV_M(L"udiff", Diff),
    MDV_M(L"md", Markdown), MDV_M(L"markdown", Markdown), MDV_M(L"mdown", Markdown), MDV_M(L"mkd", Markdown),
    MDV_M(L"mkdn", Markdown), MDV_M(L"mdx", Markdown),
    MDV_M(L"sh", Shell), MDV_M(L"bash", Shell), MDV_M(L"zsh", Shell), MDV_M(L"ksh", Shell),
    MDV_M(L"shell", Shell), MDV_M(L"shellscript", Shell), MDV_M(L"ash", Shell), MDV_M(L"dash", Shell),
    MDV_M(L"fish", Shell), MDV_M(L"csh", Shell), MDV_M(L"tcsh", Shell),
    MDV_M(L"console", Console), MDV_M(L"shell-session", Console), MDV_M(L"shellsession", Console),
    MDV_M(L"sh-session", Console), MDV_M(L"bash-session", Console), MDV_M(L"zsh-session", Console),
    MDV_M(L"terminal", Console),
    MDV_M(L"powershell", PowerShell), MDV_M(L"ps", PowerShell), MDV_M(L"ps1", PowerShell),
    MDV_M(L"pwsh", PowerShell), MDV_M(L"psm1", PowerShell), MDV_M(L"psd1", PowerShell),
    MDV_M(L"posh", PowerShell),
    MDV_M(L"bat", Batch), MDV_M(L"cmd", Batch), MDV_M(L"batch", Batch), MDV_M(L"dosbatch", Batch),
    MDV_M(L"winbatch", Batch), MDV_M(L"btm", Batch),
    MDV_M(L"dockerfile", Dockerfile), MDV_M(L"docker", Dockerfile), MDV_M(L"containerfile", Dockerfile),
    MDV_M(L"make", Makefile), MDV_M(L"makefile", Makefile), MDV_M(L"mk", Makefile), MDV_M(L"mak", Makefile),
    MDV_M(L"gnumake", Makefile), MDV_M(L"bsdmake", Makefile),
    MDV_M(L"cmake", CMake),
    MDV_M(L"asm", Asm), MDV_M(L"assembly", Asm), MDV_M(L"nasm", Asm), MDV_M(L"masm", Asm), MDV_M(L"tasm", Asm),
    MDV_M(L"fasm", Asm), MDV_M(L"yasm", Asm), MDV_M(L"x86asm", Asm), MDV_M(L"x86", Asm), MDV_M(L"armasm", Asm),
    MDV_M(L"arm64asm", Asm), MDV_M(L"gas", Asm), MDV_M(L"avrasm", Asm), MDV_M(L"mips", Asm),
    MDV_M(L"riscv", Asm),
    MDV_M(L"latex", Latex), MDV_M(L"tex", Latex), MDV_M(L"context", Latex), MDV_M(L"sty", Latex),
};

#undef MDV_G
#undef MDV_M

const Alias* FindLang(wsv lang) {
    size_t i = 0;
    while (i < lang.size() && (IsSpaceOrNl(lang[i]) || lang[i] == L'{' || lang[i] == L'.')) ++i;
    if (AtCI(lang.data(), lang.size(), i, L"language-")) i += 9;
    size_t j = i;
    while (j < lang.size() && !IsSpaceOrNl(lang[j]) && lang[j] != L'{' && lang[j] != L'}' && lang[j] != L',' &&
           lang[j] != L';' && lang[j] != L':' && lang[j] != L'=')
        ++j;
    const size_t len = j - i;
    if (len == 0 || len > 32) return nullptr;
    for (const Alias& a : kAliases)
        if (EqCI(lang.data() + i, len, a.name)) return &a;
    return nullptr;
}

}  // namespace

bool IsKnownLanguage(std::wstring_view lang) { return FindLang(lang) != nullptr; }

bool HighlightCode(std::wstring_view code, std::wstring_view lang, std::vector<HighlightSpan>& out) {
    const Alias* a = FindLang(lang);
    if (a == nullptr) return false;
    size_t n = code.size();
    if (n > 0xFFFFFFF0u) n = 0xFFFFFFF0u;  // offsets are 32-bit
    if (n == 0) return true;
    const wchar_t* s = code.data();
    Emitter em(out, n);
    switch (a->mode) {
    case Mode::Generic: RunGeneric(s, n, em, *a->def); break;
    case Mode::Markup: ScanMarkup(s, n, em); break;
    case Mode::Css: ScanCss(s, n, em, false, false); break;
    case Mode::Scss: ScanCss(s, n, em, true, false); break;
    case Mode::Sass: ScanCss(s, n, em, true, true); break;
    case Mode::Json: ScanJson(s, n, em); break;
    case Mode::Yaml: {
        YamlScanner sc(s, n, em);
        sc.Run();
        break;
    }
    case Mode::Toml: ScanToml(s, n, em); break;
    case Mode::Ini: ScanIni(s, n, em, IniKind::Ini); break;
    case Mode::Props: ScanIni(s, n, em, IniKind::Props); break;
    case Mode::Reg: ScanIni(s, n, em, IniKind::Reg); break;
    case Mode::Diff: ScanDiff(s, n, em); break;
    case Mode::Markdown: {
        MarkdownScanner sc(s, n, em);
        sc.Run();
        break;
    }
    case Mode::Shell: ScanShell(s, n, em); break;
    case Mode::Console: ScanConsole(s, n, em); break;
    case Mode::PowerShell: ScanPowerShell(s, n, em); break;
    case Mode::Batch: ScanBatch(s, n, em); break;
    case Mode::Dockerfile: ScanDockerfile(s, n, em); break;
    case Mode::Makefile: ScanMakefile(s, n, em); break;
    case Mode::CMake: ScanCMake(s, n, em); break;
    case Mode::Asm: ScanAsm(s, n, em); break;
    case Mode::Latex: ScanLatex(s, n, em); break;
    }
    return true;
}

}  // namespace mdv

