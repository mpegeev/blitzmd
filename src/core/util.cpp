#include "core/util.h"

#include <cstring>
#include <vector>

extern "C" {
#include "entity.h"
}

namespace mdv {

void AppendCodepoint(std::wstring& out, uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
    if (cp < 0x10000) {
        out.push_back(static_cast<wchar_t>(cp));
    } else {
        cp -= 0x10000;
        out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
    }
}

void AppendUtf8(std::wstring& out, std::string_view s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const size_t n = s.size();
    out.reserve(out.size() + n);
    size_t i = 0;
    while (i < n) {
        const unsigned char c = p[i];
        if (c < 0x80) {
            out.push_back(static_cast<wchar_t>(c));
            ++i;
            continue;
        }
        uint32_t cp = 0;
        size_t len = 0;
        uint32_t min = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; min = 0x80; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; min = 0x800; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; min = 0x10000; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + len > n) { out.push_back(0xFFFD); ++i; continue; }
        bool ok = true;
        for (size_t k = 1; k < len; ++k) {
            const unsigned char cc = p[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok || cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        AppendCodepoint(out, cp);
        i += len;
    }
}

std::wstring Utf8ToWide(std::string_view utf8) {
    std::wstring out;
    AppendUtf8(out, utf8);
    return out;
}

std::string WideToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n,
                            nullptr, nullptr);
    }
    return out;
}

bool AppendEntity(std::wstring& out, std::string_view e) {
    if (e.size() < 3 || e.front() != '&' || e.back() != ';') return false;
    if (e[1] == '#') {
        uint64_t cp = 0;
        size_t i = 2;
        const bool hex = (e[i] == 'x' || e[i] == 'X');
        if (hex) ++i;
        if (i >= e.size() - 1) return false;
        for (; i < e.size() - 1; ++i) {
            const char c = e[i];
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (hex && c >= 'a' && c <= 'f') d = 10 + c - 'a';
            else if (hex && c >= 'A' && c <= 'F') d = 10 + c - 'A';
            else return false;
            cp = cp * (hex ? 16 : 10) + static_cast<uint64_t>(d);
            if (cp > 0x10FFFF) cp = 0x110000;  // saturate: invalid -> U+FFFD
        }
        AppendCodepoint(out, static_cast<uint32_t>(cp));
        return true;
    }
    const ENTITY* ent = entity_lookup(e.data(), e.size());
    if (!ent) return false;
    AppendCodepoint(out, ent->codepoints[0]);
    if (ent->codepoints[1]) AppendCodepoint(out, ent->codepoints[1]);
    return true;
}

bool AppendEntity(std::wstring& out, std::wstring_view e) {
    if (e.size() > 64) return false;
    char buf[80];
    for (size_t i = 0; i < e.size(); ++i) {
        if (e[i] >= 0x80) return false;
        buf[i] = static_cast<char>(e[i]);
    }
    return AppendEntity(out, std::string_view(buf, e.size()));
}

std::wstring ToLower(std::wstring_view s) {
    if (s.empty()) return {};
    std::wstring out(s.size(), L'\0');
    const int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, s.data(),
                                static_cast<int>(s.size()), out.data(),
                                static_cast<int>(out.size()), nullptr, nullptr, 0);
    if (n <= 0) {
        for (size_t i = 0; i < s.size(); ++i) out[i] = AsciiLower(s[i]);
    } else {
        out.resize(static_cast<size_t>(n));
    }
    return out;
}

bool EqualsNoCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (AsciiLower(a[i]) != AsciiLower(b[i])) return false;
    }
    return true;
}

bool StartsWithNoCase(std::wstring_view s, std::wstring_view prefix) {
    return s.size() >= prefix.size() && EqualsNoCase(s.substr(0, prefix.size()), prefix);
}

std::wstring_view Trim(std::wstring_view s) {
    size_t b = 0, e = s.size();
    while (b < e && IsSpace(s[b])) ++b;
    while (e > b && IsSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring PercentDecode(std::wstring_view in) {
    auto hex = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
        if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
        return -1;
    };
    std::wstring out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        if (in[i] == L'%' && i + 2 < in.size() && hex(in[i + 1]) >= 0 && hex(in[i + 2]) >= 0) {
            std::string bytes;
            while (i + 2 < in.size() && in[i] == L'%' && hex(in[i + 1]) >= 0 && hex(in[i + 2]) >= 0) {
                bytes.push_back(static_cast<char>((hex(in[i + 1]) << 4) | hex(in[i + 2])));
                i += 3;
            }
            AppendUtf8(out, bytes);
            continue;
        }
        out.push_back(in[i]);
        ++i;
    }
    return out;
}

bool ParseLength(std::wstring_view s, float& value, bool& percent) {
    s = Trim(s);
    percent = false;
    if (s.empty()) return false;
    double v = 0;
    size_t i = 0;
    bool any = false;
    while (i < s.size() && s[i] >= L'0' && s[i] <= L'9') { v = v * 10 + (s[i] - L'0'); ++i; any = true; }
    if (i < s.size() && s[i] == L'.') {
        ++i;
        double f = 0.1;
        while (i < s.size() && s[i] >= L'0' && s[i] <= L'9') { v += (s[i] - L'0') * f; f *= 0.1; ++i; any = true; }
    }
    if (!any) return false;
    std::wstring_view unit = Trim(s.substr(i));
    if (unit == L"%") percent = true;
    else if (!(unit.empty() || EqualsNoCase(unit, L"px") || unit == L";")) return false;
    value = static_cast<float>(v);
    return true;
}

double NowMs() {
    static const double freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / freq;
}

std::wstring DirectoryOf(std::wstring_view path) {
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring_view::npos) return {};
    return std::wstring(path.substr(0, pos + 1));
}

bool ReadFileBytes(const wchar_t* path, std::string& out, uint64_t max_bytes) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart >= 0 &&
              static_cast<uint64_t>(size.QuadPart) <= max_bytes;
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        size_t done = 0;
        while (ok && done < out.size()) {
            DWORD chunk = static_cast<DWORD>(std::min<size_t>(out.size() - done, 1u << 30));
            DWORD got = 0;
            ok = ReadFile(h, out.data() + done, chunk, &got, nullptr) && got > 0;
            done += got;
        }
        out.resize(done);
    }
    CloseHandle(h);
    return ok;
}

namespace {

bool IsValidUtf8(std::string_view s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        // Fast path over ASCII, 8 bytes at a time.
        while (i + 8 <= n) {
            uint64_t v;
            std::memcpy(&v, p + i, 8);
            if (v & 0x8080808080808080ull) break;
            i += 8;
        }
        if (i >= n) break;
        const unsigned char c = p[i];
        if (c < 0x80) { ++i; continue; }
        size_t len;
        if ((c & 0xE0) == 0xC0 && c >= 0xC2) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) len = 4;
        else return false;
        if (i + len > n) {
            // A multi-byte sequence cut at the end of a buffer is tolerated
            // (thumbnail previews pass truncated content).
            return true;
        }
        for (size_t k = 1; k < len; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) return false;
        }
        i += len;
    }
    return true;
}

std::string Utf16ToUtf8(const unsigned char* p, size_t bytes, bool big_endian) {
    std::wstring w(bytes / 2, L'\0');
    for (size_t i = 0; i < w.size(); ++i) {
        const unsigned char a = p[i * 2], b = p[i * 2 + 1];
        w[i] = static_cast<wchar_t>(big_endian ? (a << 8) | b : (b << 8) | a);
    }
    return WideToUtf8(w);
}

// Heuristic for BOM-less UTF-16: lots of zero bytes at odd (LE) or even (BE)
// positions within the first few KB, and no zero-zero pairs.
int DetectBomlessUtf16(std::string_view s) {
    const size_t n = std::min<size_t>(s.size() & ~size_t(1), 4096);
    if (n < 4) return 0;
    size_t zero_even = 0, zero_odd = 0;
    for (size_t i = 0; i < n; i += 2) {
        const bool z0 = s[i] == 0, z1 = s[i + 1] == 0;
        if (z0 && z1) return 0;
        zero_even += z0;
        zero_odd += z1;
    }
    const size_t pairs = n / 2;
    if (zero_odd * 10 >= pairs * 3 && zero_even == 0) return 1;   // LE
    if (zero_even * 10 >= pairs * 3 && zero_odd == 0) return 2;   // BE
    return 0;
}

}  // namespace

std::string DecodeTextToUtf8(std::string_view bytes) {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const size_t n = bytes.size();
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        std::string_view rest = bytes.substr(3);
        if (IsValidUtf8(rest)) return std::string(rest);
        bytes = rest;  // broken UTF-8 after a BOM: fall through to ANSI
    } else if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        return Utf16ToUtf8(p + 2, n - 2, false);
    } else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        return Utf16ToUtf8(p + 2, n - 2, true);
    } else if (const int u16 = DetectBomlessUtf16(bytes)) {
        return Utf16ToUtf8(p, n, u16 == 2);
    }
    if (IsValidUtf8(bytes)) return std::string(bytes);
    // Legacy 8-bit text: interpret in the system ANSI code page.
    const int wn = MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wn <= 0) return std::string(bytes);
    std::wstring w(static_cast<size_t>(wn), L'\0');
    MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), w.data(), wn);
    return WideToUtf8(w);
}

bool CopyTextToClipboard(HWND owner, std::wstring_view text) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    bool ok = false;
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* dst = GlobalLock(mem)) {
            std::memcpy(dst, text.data(), text.size() * sizeof(wchar_t));
            static_cast<wchar_t*>(dst)[text.size()] = L'\0';
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

}  // namespace mdv
