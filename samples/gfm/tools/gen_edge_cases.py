#!/usr/bin/env python3
"""Generate samples/gfm/08-edge-cases.md (pathological inputs, CRLF line endings).

The file is written with Windows CRLF line endings on purpose. A handful of lines
use a bare LF or a bare CR to test mixed line endings, and the file ends in an
unclosed code fence with no final newline.

Run:  python samples/gfm/tools/gen_edge_cases.py
Output is deterministic.
"""
import base64
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "08-edge-cases.md"))

LOREM = ("lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod tempor "
         "incididunt ut labore et dolore magna aliqua ut enim ad minim veniam quis nostrud "
         "exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat").split()


def lorem_chars(n, seed=0):
    """Deterministic lorem-ipsum text of exactly n characters."""
    words, i, length = [], seed, 0
    while length < n:
        w = LOREM[i % len(LOREM)]
        words.append(w)
        length += len(w) + 1
        i += 7
    return " ".join(words)[:n].rstrip().ljust(n, "x")


def tiny_png_b64():
    """16x16 RGBA PNG: a green check mark on a transparent background."""
    w = h = 16
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        for x in range(w):
            on = (y == x // 2 + 8 and x < 7) or (y == 16 - x and x >= 6) or (y == 15 - x and x >= 6)
            rows += bytes((0x1F, 0x88, 0x3D, 255)) if on else bytes((0, 0, 0, 0))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))
    return base64.b64encode(png).decode("ascii")


def build():
    L = []  # list of logical lines (joined with CRLF later)
    add = L.append

    def para(*lines):
        L.extend(lines)
        add("")

    para("# Edge Cases and Pathological Input")
    para("This file feeds the viewer degenerate, malicious and oversized input: empty and huge "
         "headings, 50 thematic breaks in a row, block quotes nested 30 levels deep, deeply nested "
         "emphasis and brackets, dangerous URLs (`javascript:`, `data:`, `file://`), Windows and UNC "
         "paths full of backslashes, invalid character references, 10,000-character lines, stray "
         "whitespace and TABs, and constructs that are never closed. **The file uses CRLF (`\\r\\n`) "
         "line endings on purpose**, with a few bare-LF and bare-CR lines mixed in. It was generated "
         "by `tools/gen_edge_cases.py`. The viewer must not crash, hang, execute anything, or show "
         "stray `\\r` glyphs. It ends with an unclosed code fence (and no final newline), which must "
         "run to the end of the document.")
    para("## Contents")
    for title, anchor in [
        ("Line endings", "line-endings"),
        ("Empty and odd headings", "empty-and-odd-headings"),
        ("A 1000-character heading", "a-1000-character-heading"),
        ("50 consecutive thematic breaks", "50-consecutive-thematic-breaks"),
        ("Deep nesting", "deep-nesting"),
        ("Dangerous and unusual URLs", "dangerous-and-unusual-urls"),
        ("Windows paths and backslashes", "windows-paths-and-backslashes"),
        ("Character references", "character-references"),
        ("Control characters", "control-characters"),
        ("Very long lines", "very-long-lines"),
        ("Whitespace and TABs", "whitespace-and-tabs"),
        ("Pathological parser inputs", "pathological-parser-inputs"),
        ("Unclosed constructs", "unclosed-constructs"),
    ]:
        add(f"- [{title}](#{anchor})")
    add("")
    para("---")

    # ------------------------------------------------------------------ line endings
    para("## Line endings")
    para("Every line in this file ends with CRLF, except for the marked lines below. A hard line "
         "break made with two trailing spaces or with a backslash must still work when CRLF follows it:")
    para("Two trailing spaces before CRLF  ", "gives a hard break here.")
    para("A backslash before CRLF\\", "gives a hard break here too.")
    # A paragraph whose internal line endings are bare LF / bare CR (fixed up in write()).
    para("The next paragraph mixes line endings: its first line ends with a bare LF and its second with a bare CR. "
         "It must still read as one paragraph of three lines joined by soft breaks:")
    add("<<LF>>This line ends with a bare LF,")
    add("<<CR>>this line ends with a bare CR (classic Mac OS),")
    add("and this line ends with CRLF again.")
    add("")
    para("---")

    # ------------------------------------------------------------------ headings
    para("## Empty and odd headings")
    para("Empty ATX headings of levels 1, 2 and 6 follow, then `# #` (also an empty h1, because the "
         "second `#` is a closing sequence). Each should render as an empty heading: vertical space, and "
         "an underline for h1/h2, but no text.")
    add("#")
    add("## ")
    add("######")
    add("# #")
    add("")
    para("A heading made only of emoji (its GitHub anchor is empty):")
    para("### 🚀🔥✨")
    para("A heading made only of punctuation:")
    para("### ?!…")
    para("A heading that is only whitespace after the marker, plus TABs:")
    para("###\t\t   ")
    para("Seven hashes is not a heading:")
    para("#######")
    para("A setext heading underlined with 1000 `=` characters:")
    para("Setext with a very long underline", "=" * 1000)
    para("---")

    # ------------------------------------------------------------------ 1000-char heading
    para("## A 1000-character heading")
    para("The next heading's text is exactly 1000 characters long. It must wrap across many lines:")
    para("### " + lorem_chars(1000, seed=3))
    para("And a 1000-character heading with **no spaces** at all:")
    para("#### " + ("Heading" * 143)[:1000])
    para("---")

    # ------------------------------------------------------------------ 50 hr
    para("## 50 consecutive thematic breaks")
    para("Fifty rules follow, with no blank lines between them and the style cycling through `***`, `---` "
         "and `___`. That means 50 horizontal lines, no text, and no setext headings:")
    styles = ["***", "---", "___", "* * *", "- - -", "_ _ _", "******", "----------"]
    for i in range(50):
        add(styles[i % len(styles)])
    add("")
    para("End of the 50 rules.")
    para("---")

    # ------------------------------------------------------------------ deep nesting
    para("## Deep nesting")
    para("### Block quotes, 30 levels")
    para("A single line with 30 `>` markers:")
    para(">" * 30 + " Thirty levels deep. Thirty nested left borders; the text may get very narrow.")
    para("A staircase from 1 to 30 levels, one line per level, all in one nested structure:")
    for i in range(1, 31):
        add("> " * i + f"Level {i}")
        add("> " * i)
    add("")
    para("### Lists, 30 levels")
    for i in range(30):
        add("  " * i + f"- list level {i + 1}")
    add("")
    for i in range(12):
        add("   " * i + f"1. ordered level {i + 1}")
    add("")
    para("### Nested emphasis, 20 levels")
    opens, closes = [], []
    for i in range(1, 21):
        m = "*" if i % 2 else "**"
        opens.append(f"{m}{i} ")
        closes.append(f" {i}{m}")
    para("".join(opens) + "core" + "".join(reversed(closes)))
    para("The same with underscores:")
    opens, closes = [], []
    for i in range(1, 21):
        m = "_" if i % 2 else "__"
        opens.append(f"{m}{i} ")
        closes.append(f" {i}{m}")
    para("".join(opens) + "core" + "".join(reversed(closes)))
    para("Emphasis, strike, code and links nested inside each other:")
    para("***~~[**_`deep`_** link *with* ~~more~~](https://example.com)~~***")
    para("### 100 nested brackets")
    para("Plain text in brackets, all literal:")
    para("[" * 100 + "text" + "]" * 100)
    para("The same followed by a link destination. Only the outermost bracket pair becomes a link, and its "
         "text is 99 brackets, `x`, then 99 more brackets:")
    para("[" * 100 + "x" + "]" * 100 + "(https://example.com/nested)")
    para("100 nested parentheses in a link destination (valid, because the parentheses are balanced):")
    para("[balanced](https://example.com/" + "(" * 32 + "x" + ")" * 32 + ")")
    para("---")

    # ------------------------------------------------------------------ URLs
    png = tiny_png_b64()
    para("## Dangerous and unusual URLs")
    para("Nothing in this section may execute script, open a program, or fetch a local file without the "
         "user's consent. The safe outcome is to show these as inert links or text, or not render them. "
         "cmark-gfm's safe mode, for example, replaces `javascript:`, `vbscript:`, `file:` and non-image "
         "`data:` URLs with an empty href.")
    para("### Images")
    add("- Empty source: ![empty src]() and ![empty angle src](<>) and <img src=\"\" alt=\"html empty src\">")
    add("- `javascript:` source: ![js image](javascript:alert('img')) and <img src=\"javascript:alert(1)\" alt=\"html js src\">")
    add("- `onerror` handler: <img src=\"img/does-not-exist.png\" onerror=\"alert('onerror')\" alt=\"onerror handler\">")
    add(f"- A 16×16 PNG as a `data:` URI (a green check mark): ![data uri png](data:image/png;base64,{png})")
    add("- An SVG as a `data:` URI (considered unsafe by some sanitizers): "
        "![data svg](data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxNiIgaGVpZ2h0PSIxNiI+PHJlY3Qgd2lkdGg9IjE2IiBoZWlnaHQ9IjE2IiBmaWxsPSIjY2YyMjJlIi8+PC9zdmc+)")
    add("- A `file://` image: ![file image](file:///C:/Windows/Web/Wallpaper/Windows/img0.jpg)")
    add("- An absolute Windows path as the image source: ![abs path](C:/Windows/Web/Wallpaper/Windows/img0.jpg)")
    add("- An image URL containing spaces, without angle brackets (so not an image at all): ![spaces](img/картинка с пробелами.png)")
    add("")
    para("### Links")
    add("- [javascript: link](javascript:alert(1))")
    add("- [mixed-case JaVaScRiPt: link](JaVaScRiPt:alert(1))")
    add("- [entity-obfuscated link](&#106;avascript:alert(1)) (decodes to `javascript:`)")
    add("- [vbscript: link](vbscript:msgbox(1))")
    add("- [data:text/html link](data:text/html;base64,PHNjcmlwdD5hbGVydCgxKTwvc2NyaXB0Pg==)")
    add("- <a href=\"javascript:alert(1)\">HTML javascript: link</a>")
    add("- <a href=\"https://example.com\" onclick=\"alert(1)\" onmouseover=\"alert(2)\">HTML link with event handlers</a>")
    add("- <javascript:alert(1)> (an autolink with the `javascript:` scheme)")
    add("- [file:// link to win.ini](file:///C:/Windows/win.ini)")
    add("- <file:///C:/Windows/System32/drivers/etc/hosts> (file autolink)")
    add("- [UNC file link](file://server/share/doc.md)")
    add("- [ms-settings: protocol handler](ms-settings:display) and [shell: link](shell:startup)")
    add("- [An empty link]() and [](https://example.com) (a link with no text) and [ ](https://example.com)")
    add("- [Link to an anchor that does not exist](#no-such-anchor)")
    add("- [Very long link text " + "x" * 300 + "](https://example.com)")
    add("")
    para("---")

    # ------------------------------------------------------------------ windows paths
    para("## Windows paths and backslashes")
    para("A backslash is only an escape before ASCII punctuation, and a backslash is itself punctuation. "
         "So `\\\\` turns into **one** backslash in normal text, while `\\P` stays as typed.")
    add("- Program Files: C:\\Program Files\\Total Commander\\TOTALCMD64.EXE (backslashes before letters stay)")
    add("- Double backslashes in text: C:\\\\Windows\\\\System32 (expected: single backslashes, because each `\\\\` is an escape)")
    add("- UNC path in text: \\\\server\\share\\folder\\file.md (expected: **one** leading backslash)")
    add("- UNC path in code: `\\\\server\\share\\folder\\file.md` (expected: two leading backslashes)")
    add("- A path whose backslash ends the paragraph stays literal (a hard break can't end a block): C:\\Temp\\")
    add("- Backslash before `*`: C:\\*.md shows `C:*.md`, and the star is escaped")
    add("- Registry: HKEY_LOCAL_MACHINE\\SOFTWARE\\Ghisler\\Total Commander (underscores inside words stay literal)")
    add("- [Link with a Windows path](C:\\Users\\Public\\Documents\\readme.md)")
    add("- [Relative link with backslashes](..\\sample-readme.md)")
    add("- [UNC link](\\\\server\\share\\doc.md) (the destination starts with a single backslash after escaping)")
    add("- <a href=\"file:///C:/Projects/mdview-wlx/README.md\">HTML file:/// link to the repository README</a>")
    add("")
    para("```bat", "rem In a code block every backslash is literal:",
         "copy \"C:\\Program Files\\Total Commander\\wincmd.ini\" \"\\\\backup-server\\configs\\tc\\\"",
         "dir \\\\?\\C:\\Very\\Long\\Path\\Prefix\\ /s", "```")
    para("---")

    # ------------------------------------------------------------------ entities
    para("## Character references")
    para("| Source | Result | Why |", "|--------|--------|-----|",
         "| `&#0;` | &#0; | NUL is replaced with U+FFFD |",
         "| `&#xD800;` | &#xD800; | a lone surrogate becomes U+FFFD |",
         "| `&#x110000;` | &#x110000; | beyond U+10FFFF: U+FFFD (or literal, depending on digit count) |",
         "| `&#xFFFFFFFF;` | &#xFFFFFFFF; | 8 hex digits, more than the spec's 6: literal per spec |",
         "| `&#99999999;` | &#99999999; | 8 decimal digits, more than the spec's 7: literal per spec (cmark-gfm prints U+FFFD) |",
         "| `&#65;` `&#x41;` | &#65; &#x41; | both are the letter A |",
         "| `&foo;` | &foo; | unknown name: literal |",
         "| `&amp` | &amp | no semicolon: literal |",
         "| `&AMP;` | &AMP; | valid HTML5 name: & |",
         "| `&Amp;` | &Amp; | wrong case: literal |",
         "| `&#;` `&#x;` | &#; &#x; | no digits: literal |",
         "| `&#x1F600;` | &#x1F600; | 😀 |",
         "| `&nbsp;&nbsp;&nbsp;` | [&nbsp;&nbsp;&nbsp;] | three no-break spaces |",
         "| `&#42;&#42;x&#42;&#42;` | &#42;&#42;x&#42;&#42; | entities never create markup: literal `**x**` |")
    para("---")

    # ------------------------------------------------------------------ control chars
    para("## Control characters")
    para("Each character sits between brackets. A viewer should show nothing, a replacement glyph, or "
         "a visible control picture, but never break the layout:")
    add("- SOH U+0001: [\x01], BEL U+0007: [\x07], BS U+0008: [\x08], VT U+000B: [\x0b], FF U+000C: [\x0c]")
    add("- ESC U+001B: [\x1b] and an ANSI color sequence: [\x1b[31mred?\x1b[0m]")
    add("- DEL U+007F: [\x7f], NEL U+0085: [\x85]")
    add("- LINE SEPARATOR U+2028: [\u2028] and PARAGRAPH SEPARATOR U+2029: [\u2029] (these are not line endings in Markdown)")
    add("- Noncharacters U+FFFE: [\ufffe] and U+FFFF: [\uffff], replacement character U+FFFD: [\ufffd]")
    add("- A private-use character U+E000: [\ue000], and plane-15 PUA U+F0000: [\U000F0000]")
    add("")
    para("---")

    # ------------------------------------------------------------------ long lines
    para("## Very long lines")
    para("A paragraph that is one source line of 10,000 characters (with spaces, so it wraps):")
    para(lorem_chars(10000, seed=11))
    para("A 10,000-character line with **no spaces**, in a code block:")
    para("```text", ("0123456789ABCDEF" * 700)[:10000], "```")
    para("A 10,000-character line with no spaces, in a plain paragraph (the worst case for line breaking):")
    para(("abcdefghij" * 1000)[:10000])
    para("---")

    # ------------------------------------------------------------------ whitespace
    para("## Whitespace and TABs")
    para("Words\tseparated\tby\tTABs\tin\ta\tparagraph render like spaces.")
    para("-\tA list item whose marker is followed by a TAB", "-\tAnother one")
    para("#\tHeading after a TAB (valid ATX heading)")
    para("| TAB\tin\tcell | x |", "|---|---|", "| a\tb | c\td |")
    para("A line with trailing TABs is not a hard break:\t\t", "so this continues the same line.")
    para("A line with one trailing space is not a hard break: ", "so this continues the same line.")
    para("A line that ends with two spaces and then a TAB  \t", "is a soft break in cmark-gfm, because the character right before the line ending is a TAB, not a space.")
    para("The next line holds only spaces and TABs, which makes it blank:")
    add(" \t  \t ")
    para("This paragraph follows a whitespace-only line.")
    para("\tA TAB-indented line after a blank line is an indented code block.")
    para("   Three leading spaces: a normal paragraph.")
    para("Unicode spaces at line start (U+3000 and U+00A0) are **not** indentation:")
    para("\u3000\u3000Ideographic spaces at the start of this paragraph.")
    para("\u00a0\u00a0\u00a0\u00a0Four no-break spaces: still a paragraph, not code.")
    para("---")

    # ------------------------------------------------------------------ pathological
    para("## Pathological parser inputs")
    para("These inputs are known to trigger quadratic or exponential behavior in naive Markdown parsers "
         "(see cmark's `test/pathological_tests.py`). Each must render as mostly literal text, and fast.")
    n = 1000
    cases = [
        ("Nested strong emphasis", "*a **a " * 200 + "b" + " a** a*" * 200),
        ("Many emphasis closers with no openers", "a_ " * n),
        ("Many emphasis openers with no closers", "_a " * n),
        ("Many link closers with no openers", "a]" * n),
        ("Many link openers with no closers", "[a" * n),
        ("Mismatched openers and closers", "*a_ " * n),
        ("Openers and closers in multiples of 3", "a**b" + "c* " * n),
        ("Link openers and emphasis closers", "[ a_" * n),
        ("`[ (](` repeated", "[ (](" * n),
        ("Nested brackets, 1000 deep", "[" * n + "a" + "]" * n),
        ("Unclosed inline links", "[a](<b" * n),
        ("Backtick runs of increasing length", " ".join("e" + "`" * i for i in range(1, 80))),
        ("Unclosed HTML comments inline", "a <!-- " * n),
        ("Unclosed autolinks", "<http://" * n),
        ("Many entity-like strings", "&a" * n),
    ]
    for title, text in cases:
        para(f"**{title}:**")
        para(text.rstrip())
    para("A block quote nested 250 levels deep, in one line:")
    para("> " * 250 + "deep")
    para("A list nested 100 levels deep:")
    for i in range(100):
        add("  " * i + "- " + str(i + 1))
    add("")
    para("A table with 64 columns and 3 rows:")
    add("|" + "|".join(f" c{i} " for i in range(64)) + "|")
    add("|" + "|".join("---" for _ in range(64)) + "|")
    for r in range(3):
        add("|" + "|".join(f" {r}.{i} " for i in range(64)) + "|")
    add("")
    para("A link reference label of 999 characters (the maximum), which is valid:")
    label = ("label" * 200)[:999]
    para(f"[{label}]", "", f"[{label}]: https://example.com/max-label")
    para("A label of 1000 characters is too long, so the reference and the definition both stay literal:")
    label = ("label" * 200)[:1000]
    para(f"[{label}]", "", f"[{label}]: https://example.com/too-long")
    para("---")

    # ------------------------------------------------------------------ unclosed
    para("## Unclosed constructs")
    para("These are the last items in the file, because each can swallow whatever follows it.")
    para("**An HTML table that is never closed.** Markdown ends the HTML block at the next blank line, "
         "but an HTML engine may keep the table open:")
    para("<table><tr><td>cell in an unclosed table")
    para("**A `<div align=\"center\">` that is never closed.** On github.com, the browser may center "
         "*everything* below this point:")
    para("<div align=\"center\">", "", "Centered text inside an unclosed div.")
    para("**A `<details>` that is never closed.** On github.com, everything below this point may end up "
         "inside the collapsed block:")
    para("<details>", "<summary>Unclosed details (click to expand)</summary>", "",
         "Content inside the unclosed details block.")
    para("**A code fence that is never closed.** The fenced block runs to the end of the document, and "
         "everything in it, including the Markdown-looking lines, must render as code:")
    add("```python")
    add("def never_closed():")
    add("    return 'this fence has no closing line'")
    add("")
    add("# This looks like a heading, but it is code.")
    add("**not bold**, [not a link](https://example.com), <!-- not a comment")
    add("> not a quote")
    add("| not | a | table |")
    add("|-----|---|-------|")
    add("")
    add("The document ends here, without a closing fence and without a final newline.")
    return L


def write(lines):
    text = "\r\n".join(lines)
    # apply the deliberate bare-LF / bare-CR line endings
    text = text.replace("<<LF>>This line ends with a bare LF,\r\n", "This line ends with a bare LF,\n")
    text = text.replace("<<CR>>this line ends with a bare CR (classic Mac OS),\r\n",
                        "this line ends with a bare CR (classic Mac OS),\r")
    assert "<<LF>>" not in text and "<<CR>>" not in text
    with open(OUT, "wb") as f:
        f.write(text.encode("utf-8"))  # no final newline on purpose
    return len(text.encode("utf-8"))


if __name__ == "__main__":
    size = write(build())
    print(f"wrote {OUT} ({size:,} bytes)")
