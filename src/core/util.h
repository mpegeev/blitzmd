// Small helpers shared by all modules: string conversion, entity decoding,
// case folding and COM smart pointers.
#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace mdv {

using Microsoft::WRL::ComPtr;

// UTF-8 -> UTF-16. Invalid sequences become U+FFFD.
void AppendUtf8(std::wstring& out, std::string_view utf8);
std::wstring Utf8ToWide(std::string_view utf8);
std::string WideToUtf8(std::wstring_view wide);

// Appends a Unicode scalar value as UTF-16 (invalid values become U+FFFD).
void AppendCodepoint(std::wstring& out, uint32_t cp);

// Decodes an HTML character reference like "&amp;", "&#35;" or "&#x1F600;"
// (including the leading '&' and trailing ';') and appends the result.
// Returns false (appending nothing) if the reference is not valid.
bool AppendEntity(std::wstring& out, std::string_view entity);
bool AppendEntity(std::wstring& out, std::wstring_view entity);

// Locale-independent lowercase (full Unicode, via LCMapStringEx).
std::wstring ToLower(std::wstring_view s);

// ASCII-only case-insensitive comparison (for tag names, schemes, languages).
bool EqualsNoCase(std::wstring_view a, std::wstring_view b);
bool StartsWithNoCase(std::wstring_view s, std::wstring_view prefix);
inline wchar_t AsciiLower(wchar_t c) { return (c >= L'A' && c <= L'Z') ? wchar_t(c + 32) : c; }

inline bool IsSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\f' || c == L'\v';
}
std::wstring_view Trim(std::wstring_view s);

// Percent-decodes a URL component ("%20" -> " ", UTF-8 aware).
std::wstring PercentDecode(std::wstring_view s);

// Parses a CSS-ish length: "120", "120px", "50%". Returns false if invalid.
bool ParseLength(std::wstring_view s, float& value, bool& percent);

// Milliseconds from a monotonic high-resolution clock.
double NowMs();

// Directory part of a path including the trailing backslash.
std::wstring DirectoryOf(std::wstring_view path);

// Reads a whole file (up to max_bytes). Returns false on failure/too large.
bool ReadFileBytes(const wchar_t* path, std::string& out, uint64_t max_bytes);

// Converts raw file bytes to UTF-8: handles UTF-8 (with/without BOM),
// UTF-16 LE/BE (with BOM, or detected heuristically) and falls back to the
// ANSI code page for text that is not valid UTF-8.
std::string DecodeTextToUtf8(std::string_view bytes);

// Places Unicode text on the clipboard.
bool CopyTextToClipboard(HWND owner, std::wstring_view text);

}  // namespace mdv
