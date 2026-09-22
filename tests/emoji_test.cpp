// Standalone test for mdv::LookupEmojiShortcode (src/core/emoji.cpp).
//
// Build (from PowerShell, MSVC environment via scripts/vcrun.ps1):
//   cl /nologo /std:c++20 /W4 /WX /permissive- /EHsc /utf-8 /GR- /O2
//      /Fe:out\emojitest\emoji_test.exe /Fo:out\emojitest\
//      tests\emoji_test.cpp src\core\emoji.cpp
//
// Exit code 0 only if every check passes.

#include "../src/core/emoji.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

// A private copy of the generated table, used to validate its invariants and
// to cross-check every entry against the real lookup in emoji.cpp.
#include "../src/core/emoji_table.inc"

int g_failures = 0;
int g_checks = 0;

std::string Describe(std::wstring_view s) {
  if (s.empty()) return "(empty)";
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    std::uint32_t cp = s[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 &&
        s[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<std::uint32_t>(s[i + 1]) - 0xDC00);
      ++i;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%sU+%04X", out.empty() ? "" : " ",
                  static_cast<unsigned>(cp));
    out += buf;
  }
  return out;
}

void Fail(const char* what, std::string_view name, const std::string& detail) {
  ++g_failures;
  std::printf("FAIL: %s \"%.*s\": %s\n", what, static_cast<int>(name.size()), name.data(),
              detail.c_str());
}

void ExpectEmoji(std::string_view name, std::wstring_view expected) {
  ++g_checks;
  const std::wstring_view got = mdv::LookupEmojiShortcode(name);
  if (got != expected) {
    Fail("lookup", name, "got " + Describe(got) + ", expected " + Describe(expected));
    return;
  }
  std::printf("  ok  :%.*s: -> %s\n", static_cast<int>(name.size()), name.data(),
              Describe(got).c_str());
}

void ExpectMissing(std::string_view name) {
  ++g_checks;
  const std::wstring_view got = mdv::LookupEmojiShortcode(name);
  if (!got.empty()) Fail("expected no match for", name, "got " + Describe(got));
}

constexpr std::string_view NameAt(std::size_t i) {
  return {kEmojiNames + kEmojiEntries[i].name_off, kEmojiEntries[i].name_len};
}

constexpr std::wstring_view EmojiAt(std::size_t i) {
  return {kEmojiChars + kEmojiEntries[i].emoji_off, kEmojiEntries[i].emoji_len};
}

bool IsWellFormedUtf16(std::wstring_view s) {
  for (std::size_t i = 0; i < s.size(); ++i) {
    const wchar_t c = s[i];
    if (c >= 0xD800 && c <= 0xDBFF) {
      if (i + 1 >= s.size() || s[i + 1] < 0xDC00 || s[i + 1] > 0xDFFF) return false;
      ++i;
    } else if (c >= 0xDC00 && c <= 0xDFFF) {
      return false;
    }
  }
  return true;
}

void CheckTable() {
  std::size_t max_len = 0;
  std::size_t mismatched = 0;
  for (std::size_t i = 0; i < kEmojiCount; ++i) {
    ++g_checks;
    const EmojiEntry& e = kEmojiEntries[i];
    const std::string_view name = NameAt(i);
    if (e.name_len == 0 || std::size_t{e.name_off} + e.name_len > kEmojiNamesSize) {
      Fail("table: bad name range at", name, std::to_string(i));
      continue;
    }
    if (e.emoji_len == 0 || std::size_t{e.emoji_off} + e.emoji_len > kEmojiCharsSize) {
      Fail("table: bad emoji range for", name, std::to_string(i));
      continue;
    }
    if (i > 0 && !(NameAt(i - 1) < name)) {
      Fail("table: not strictly sorted / duplicate at", name,
           "previous is \"" + std::string(NameAt(i - 1)) + "\"");
    }
    for (const char c : name) {
      // Names must be scannable between colons: printable ASCII, no ':'.
      if (c <= 0x20 || c >= 0x7F || c == ':') {
        Fail("table: unexpected character in name", name, std::to_string(int{c}));
        break;
      }
    }
    if (!IsWellFormedUtf16(EmojiAt(i))) {
      Fail("table: ill-formed UTF-16 for", name, Describe(EmojiAt(i)));
    }
    if (mdv::LookupEmojiShortcode(name) != EmojiAt(i)) {
      ++mismatched;
      Fail("round-trip lookup", name, Describe(mdv::LookupEmojiShortcode(name)));
    }
    if (name.size() > max_len) max_len = name.size();
  }
  ++g_checks;
  if (max_len != kEmojiMaxNameLen) {
    Fail("table: kEmojiMaxNameLen mismatch", "", std::to_string(max_len));
  }
  std::printf("  table: %zu entries, %zu name bytes, %zu UTF-16 units, max name %zu; "
              "sorted/unique/ranges/UTF-16 checked; round-trip mismatches: %zu\n",
              kEmojiCount, kEmojiNamesSize, kEmojiCharsSize, max_len, mismatched);
}

}  // namespace

int main() {
  std::printf("emoji shortcode table: %zu entries\n", kEmojiCount);
  std::printf("data size: names %zu + emoji %zu + entries %zu = %zu bytes\n",
              sizeof(kEmojiNames), sizeof(kEmojiChars), sizeof(kEmojiEntries),
              sizeof(kEmojiNames) + sizeof(kEmojiChars) + sizeof(kEmojiEntries));

  std::printf("known shortcodes:\n");
  ExpectEmoji("rocket", L"\U0001F680");
  ExpectEmoji("+1", L"\U0001F44D");
  ExpectEmoji("thumbsup", L"\U0001F44D");
  ExpectEmoji("-1", L"\U0001F44E");
  ExpectEmoji("heart", L"\u2764\uFE0F");
  ExpectEmoji("smile", L"\U0001F604");
  ExpectEmoji("tada", L"\U0001F389");
  ExpectEmoji("white_check_mark", L"\u2705");
  ExpectEmoji("warning", L"\u26A0\uFE0F");
  ExpectEmoji("x", L"\u274C");
  ExpectEmoji("100", L"\U0001F4AF");
  ExpectEmoji("1234", L"\U0001F522");
  ExpectEmoji("zzz", L"\U0001F4A4");
  ExpectEmoji("hash", L"#\uFE0F\u20E3");  // keycap: VS16 + U+20E3 preserved
  ExpectEmoji("copyright", L"\u00A9\uFE0F");
  // gemoji names flags after the country, not "flag-xx".
  ExpectEmoji("ukraine", L"\U0001F1FA\U0001F1E6");
  ExpectEmoji("us", L"\U0001F1FA\U0001F1F8");
  ExpectEmoji("gb", L"\U0001F1EC\U0001F1E7");
  ExpectEmoji("uk", L"\U0001F1EC\U0001F1E7");
  // ZWJ sequences.
  ExpectEmoji("rainbow_flag", L"\U0001F3F3\uFE0F\u200D\U0001F308");
  ExpectEmoji("pirate_flag", L"\U0001F3F4\u200D\u2620\uFE0F");
  ExpectEmoji("eye_speech_bubble", L"\U0001F441\uFE0F\u200D\U0001F5E8\uFE0F");
  ExpectEmoji("family_man_woman_girl_boy",
              L"\U0001F468\u200D\U0001F469\u200D\U0001F467\u200D\U0001F466");
  // Tag sequence (longest entry, 14 UTF-16 units).
  ExpectEmoji("scotland", L"\U0001F3F4\U000E0067\U000E0062\U000E0073\U000E0063\U000E0074"
                          L"\U000E007F");
  // Names containing '-' and '+'.
  ExpectEmoji("t-rex", L"\U0001F996");
  ExpectEmoji("e-mail", L"\U0001F4E7");
  // First and last names in the table (binary-search boundaries).
  ExpectEmoji(NameAt(0), EmojiAt(0));
  ExpectEmoji(NameAt(kEmojiCount - 1), EmojiAt(kEmojiCount - 1));

  std::printf("flag-ua: %s\n", mdv::LookupEmojiShortcode("flag-ua").empty()
                                   ? "not a gemoji alias (flags use country names, "
                                     "e.g. :ukraine:)"
                                   : "present");

  std::printf("unknown shortcodes:\n");
  const int checks_before = g_checks;
  const int failures_before = g_failures;
  ExpectMissing("");
  ExpectMissing("not_an_emoji");
  ExpectMissing("Rocket");
  ExpectMissing("ROCKET");
  ExpectMissing(":rocket:");
  ExpectMissing("rocket ");
  ExpectMissing(" rocket");
  ExpectMissing("rocke");
  ExpectMissing("rockets");
  ExpectMissing(std::string_view("rocket\0", 7));
  ExpectMissing("+");
  ExpectMissing("+10");
  ExpectMissing("octocat");  // GitHub custom image emoji, not Unicode.
  ExpectMissing("!");        // sorts before every name
  ExpectMissing("~");        // sorts after every name
  ExpectMissing(std::string(kEmojiMaxNameLen + 1, 'a'));
  ExpectMissing(std::string(1000, 'z'));
  std::printf("  %d of %d negative cases returned empty\n",
              (g_checks - checks_before) - (g_failures - failures_before),
              g_checks - checks_before);

  std::printf("table invariants:\n");
  CheckTable();

  std::printf("%s: %d checks, %d failures\n", g_failures == 0 ? "PASS" : "FAIL", g_checks,
              g_failures);
  return g_failures == 0 ? 0 : 1;
}
