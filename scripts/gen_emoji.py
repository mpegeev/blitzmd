#!/usr/bin/env python3
"""Generates src/core/emoji_table.inc from GitHub's gemoji database.

Usage:
    python scripts/gen_emoji.py [SOURCE] [-o OUTPUT]

SOURCE is a local path to emoji.json or an http(s) URL; it defaults to the
upstream gemoji database on GitHub. OUTPUT defaults to
src/core/emoji_table.inc next to this script's repository.

Only the Python 3 standard library is used. The output is deterministic
(no timestamps), pure ASCII and uses LF line endings, so regenerating from
the same emoji.json produces a byte-identical file.
"""

import argparse
import hashlib
import json
import pathlib
import sys
import urllib.request

DEFAULT_URL = "https://raw.githubusercontent.com/github/gemoji/master/db/emoji.json"
REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT = REPO_ROOT / "src" / "core" / "emoji_table.inc"

# MSVC rejects string literals longer than 65535 bytes after concatenation
# (C1091). The names blob is emitted as a single literal, so guard it.
MSVC_MAX_STRING_LITERAL = 65535


def read_source(source: str) -> bytes:
    if source.startswith(("http://", "https://")):
        req = urllib.request.Request(source, headers={"User-Agent": "gen_emoji.py"})
        with urllib.request.urlopen(req, timeout=60) as resp:
            return resp.read()
    return pathlib.Path(source).read_bytes()


def utf16_units(text: str) -> list[int]:
    data = text.encode("utf-16-le")
    return [int.from_bytes(data[i:i + 2], "little") for i in range(0, len(data), 2)]


def c_string_body(name: str) -> str:
    """Escapes a name for use inside a narrow C++ string literal (ASCII out)."""
    out = []
    for byte in name.encode("utf-8"):
        ch = chr(byte)
        if ch in '"\\?':
            out.append("\\" + ch)
        elif 0x20 <= byte < 0x7F:
            out.append(ch)
        else:
            # Octal escapes take at most 3 digits, so they cannot swallow
            # the following character the way \x escapes can.
            out.append("\\%03o" % byte)
    return "".join(out)


def comment_safe(text: str) -> str:
    """Makes arbitrary text safe for an ASCII // comment."""
    return text.encode("ascii", "backslashreplace").decode("ascii").replace("\\", "/")


def offset_type(limit: int) -> str:
    if limit <= 0xFFFF:
        return "std::uint16_t"
    if limit <= 0xFFFFFFFF:
        return "std::uint32_t"
    raise SystemExit("error: blob too large")


def build(db: list) -> tuple[list[tuple[str, str]], list[str], dict[str, list[str]]]:
    """Returns (sorted (name, emoji) pairs, unique emoji in source order,
    emoji -> aliases actually kept)."""
    seen: dict[str, str] = {}
    unique_emoji: list[str] = []
    aliases_of: dict[str, list[str]] = {}
    for entry in db:
        emoji = entry.get("emoji")
        if not emoji:
            continue  # GitHub custom image emoji (no Unicode character).
        for name in entry.get("aliases") or []:
            if not name:
                continue
            if name in seen:
                if seen[name] != emoji:
                    print(f"warning: duplicate alias {name!r}; keeping the first",
                          file=sys.stderr)
                continue
            seen[name] = emoji
            if emoji not in aliases_of:
                aliases_of[emoji] = []
                unique_emoji.append(emoji)
            aliases_of[emoji].append(name)
    pairs = sorted(seen.items(), key=lambda kv: kv[0].encode("utf-8"))
    return pairs, unique_emoji, aliases_of


def generate(raw: bytes) -> str:
    db = json.loads(raw.decode("utf-8"))
    pairs, unique_emoji, aliases_of = build(db)
    if not pairs:
        raise SystemExit("error: no emoji found in the source")

    # Emoji blob: every distinct emoji sequence once, in source order.
    emoji_off: dict[str, int] = {}
    emoji_units: list[list[int]] = []
    total_units = 0
    for emoji in unique_emoji:
        units = utf16_units(emoji)
        if len(units) > 0xFF:
            raise SystemExit(f"error: emoji for {aliases_of[emoji][0]!r} is too long")
        emoji_off[emoji] = total_units
        emoji_units.append(units)
        total_units += len(units)

    # Names blob: all names concatenated in sorted order.
    name_off: list[int] = []
    total_name_bytes = 0
    for name, _ in pairs:
        n = len(name.encode("utf-8"))
        if n > 0xFF:
            raise SystemExit(f"error: name {name!r} is too long")
        name_off.append(total_name_bytes)
        total_name_bytes += n
    if total_name_bytes + 1 > MSVC_MAX_STRING_LITERAL:
        raise SystemExit("error: names blob exceeds the MSVC string literal limit")

    name_t = offset_type(total_name_bytes)
    emoji_t = offset_type(total_units)
    max_name_len = max(len(name.encode("utf-8")) for name, _ in pairs)
    sha256 = hashlib.sha256(raw).hexdigest()
    ow = len(str(max(total_name_bytes, total_units)))

    out: list[str] = []
    w = out.append
    w("// GENERATED FILE - DO NOT EDIT.")
    w("//")
    w("// Generated by scripts/gen_emoji.py from GitHub's gemoji database:")
    w(f"//   {DEFAULT_URL}")
    w("// gemoji (https://github.com/github/gemoji) is MIT licensed,")
    w("// Copyright (c) 2019 GitHub, Inc.")
    w("//")
    w("// To regenerate (downloads the latest emoji.json):")
    w("//   python scripts/gen_emoji.py")
    w("// or from a local copy:")
    w("//   python scripts/gen_emoji.py path/to/emoji.json")
    w("//")
    w(f"// Source SHA-256: {sha256}")
    w(f"// Entries: {len(pairs)} shortcodes, {len(unique_emoji)} distinct emoji.")
    w("//")
    w("// Include only from inside an anonymous namespace, after <cstddef> and")
    w("// <cstdint>. Layout (no pointers, so no relocations and no dynamic")
    w("// initialization):")
    w("//   kEmojiNames   - all shortcode names concatenated, sorted byte-wise;")
    w("//   kEmojiChars   - UTF-16 code units of every distinct emoji;")
    w("//   kEmojiEntries - one entry per name (same order as kEmojiNames).")
    w("")
    w("// clang-format off")
    w("")
    w("struct EmojiEntry {")
    w(f"  {name_t} name_off;   // into kEmojiNames")
    w(f"  {emoji_t} emoji_off;  // into kEmojiChars")
    w("  std::uint8_t name_len;")
    w("  std::uint8_t emoji_len;")
    w("};")
    w("")
    w(f"constexpr std::size_t kEmojiCount = {len(pairs)};")
    w(f"constexpr std::size_t kEmojiMaxNameLen = {max_name_len};")
    w(f"constexpr std::size_t kEmojiNamesSize = {total_name_bytes};")
    w(f"constexpr std::size_t kEmojiCharsSize = {total_units};")
    w("")
    w("constexpr char kEmojiNames[] =")
    for name, _ in pairs:
        w(f'    "{c_string_body(name)}"')
    w("    ;")
    w("")
    w("constexpr wchar_t kEmojiChars[] = {")
    for emoji, units in zip(unique_emoji, emoji_units):
        cells = " ".join(f"0x{u:04X}," for u in units)
        names = " ".join(f":{comment_safe(a)}:" for a in aliases_of[emoji])
        w(f"    {cells}  // {names}")
    w("};")
    w("")
    w("constexpr EmojiEntry kEmojiEntries[] = {")
    for (name, emoji), noff in zip(pairs, name_off):
        n = len(name.encode("utf-8"))
        e = len(utf16_units(emoji))
        w(f"    {{{noff:>{ow}}, {emoji_off[emoji]:>{ow}}, {n:>2}, {e:>2}}},"
          f"  // {comment_safe(name)}")
    w("};")
    w("")
    w("static_assert(sizeof(kEmojiNames) == kEmojiNamesSize + 1);")
    w("static_assert(sizeof(kEmojiChars) / sizeof(kEmojiChars[0]) == kEmojiCharsSize);")
    w("static_assert(sizeof(kEmojiEntries) / sizeof(kEmojiEntries[0]) == kEmojiCount);")
    w("")
    w("// clang-format on")
    text = "\n".join(out) + "\n"
    text.encode("ascii")  # Must be pure ASCII; raises otherwise.
    return text


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", nargs="?", default=DEFAULT_URL,
                    help="path or URL of gemoji's emoji.json (default: upstream)")
    ap.add_argument("-o", "--output", default=str(DEFAULT_OUTPUT),
                    help="output file (default: src/core/emoji_table.inc)")
    args = ap.parse_args()

    raw = read_source(args.source)
    text = generate(raw)
    out = pathlib.Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(text.encode("ascii"))
    print(f"wrote {out} ({len(text)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
