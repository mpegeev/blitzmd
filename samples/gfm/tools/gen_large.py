#!/usr/bin/env python3
"""Generate samples/gfm/09-large-document.md: a ~2.5 MB document for performance testing.

The content is a realistic mix of everything a big README or documentation page contains:
headings at all levels, long paragraphs with inline formatting, links and emoji, bullet,
ordered and task lists, tables, fenced code in many languages, block quotes, alerts,
images, footnotes and a table of contents. Output is deterministic (fixed random seed).

Usage:  python samples/gfm/tools/gen_large.py [--size-mb 2.5] [-o OUTPUT]
"""
import argparse
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, "..", "09-large-document.md"))

WORDS = """lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod tempor incididunt ut
labore et dolore magna aliqua enim ad minim veniam quis nostrud exercitation ullamco laboris nisi
aliquip ex ea commodo consequat duis aute irure in reprehenderit voluptate velit esse cillum
fugiat nulla pariatur excepteur sint occaecat cupidatat non proident sunt culpa qui officia
deserunt mollit anim id est laborum curabitur pretium tincidunt lacus nulla gravida orci odio
tempus feugiat integer vitae libero risus egestas placerat""".split()

TECH = """parser renderer layout glyph shaping cache viewport scrollbar selection hyperlink anchor
table column row cell border padding margin baseline ascent descent kerning ligature fallback
bitmap texture swapchain device context factory brush geometry stroke clip transform DPI
scale thread queue mutex atomic buffer stream decoder encoder bidi paragraph heading list quote
footnote emoji image codec metadata""".split()

RUSSIAN = ("Съешь же ещё этих мягких французских булок, да выпей чаю. Широкая электрификация южных губерний "
           "даст мощный толчок подъёму сельского хозяйства. В чащах юга жил бы цитрус? Да, но фальшивый экземпляр!")
CHINESE = "天地玄黄，宇宙洪荒。日月盈昃，辰宿列张。寒来暑往，秋收冬藏。闰余成岁，律吕调阳。云腾致雨，露结为霜。金生丽水，玉出昆冈。"
JAPANESE = "いろはにほへと ちりぬるを わかよたれそ つねならむ うゐのおくやま けふこえて あさきゆめみし ゑひもせす"
EMOJI = [":rocket:", ":sparkles:", ":bug:", ":zap:", ":memo:", ":white_check_mark:", ":warning:", "🚀", "✨", "🐛", "📦", "🔥"]
LANGS = ["c", "cpp", "csharp", "python", "javascript", "typescript", "rust", "go", "java", "sql",
         "bash", "powershell", "json", "yaml", "html", "css", "diff", "text"]
ALERTS = ["NOTE", "TIP", "IMPORTANT", "WARNING", "CAUTION"]
IMAGES = ["../logo.png", "../avatar.png", "../diagram.png", "img/photo.jpg", "img/gradients.svg",
          "img/transparent.png", "img/badge-build-passing.svg"]


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)
        self.footnotes = []
        self.heading_ids = []

    # ---------------------------------------------------------------- text
    def words(self, n, pool=None):
        pool = pool or WORDS
        return " ".join(self.r.choice(pool) for _ in range(n))

    def sentence(self):
        n = self.r.randint(8, 22)
        parts = []
        i = 0
        while i < n:
            k = self.r.random()
            if k < 0.05:
                parts.append(f"**{self.words(self.r.randint(1, 3))}**")
            elif k < 0.10:
                parts.append(f"*{self.words(self.r.randint(1, 3))}*")
            elif k < 0.14:
                parts.append(f"`{self.r.choice(TECH)}_{self.r.choice(TECH)}()`")
            elif k < 0.17:
                w = self.words(self.r.randint(1, 4), TECH)
                parts.append(f"[{w}](https://example.com/docs/{w.replace(' ', '-')})")
            elif k < 0.18:
                parts.append(f"~~{self.words(2)}~~")
            elif k < 0.19:
                parts.append(self.r.choice(EMOJI))
            elif k < 0.195 and len(self.footnotes) < 200:
                self.footnotes.append(self.sentence_plain())
                parts.append(f"{self.r.choice(WORDS)}[^{len(self.footnotes)}]")
            elif k < 0.20:
                sid = self.r.choice(self.heading_ids) if self.heading_ids else "table-of-contents"
                parts.append(f"(see [above](#{sid}))")
            else:
                parts.append(self.r.choice(WORDS if self.r.random() < 0.8 else TECH))
            i += 1
        s = " ".join(parts)
        return s[0].upper() + s[1:] + self.r.choice([".", ".", ".", "!", "?", ";"])

    def sentence_plain(self):
        s = self.words(self.r.randint(8, 16))
        return s[0].upper() + s[1:] + "."

    def paragraph(self):
        k = self.r.random()
        if k < 0.04:
            return RUSSIAN + " " + self.sentence()
        if k < 0.06:
            return CHINESE + self.r.choice(["", JAPANESE])
        return " ".join(self.sentence() for _ in range(self.r.randint(3, 8)))

    # ---------------------------------------------------------------- blocks
    def bullet_list(self):
        out = []
        task = self.r.random() < 0.3
        for _ in range(self.r.randint(3, 8)):
            box = self.r.choice(["[ ] ", "[x] "]) if task else ""
            out.append(f"- {box}{self.sentence()}")
            if self.r.random() < 0.25:
                for _ in range(self.r.randint(1, 3)):
                    out.append(f"  - {self.sentence()}")
        return "\n".join(out)

    def ordered_list(self):
        start = self.r.choice([1, 1, 1, 3, 10])
        return "\n".join(f"{start + i}. {self.sentence()}" for i in range(self.r.randint(3, 7)))

    def table(self):
        cols = self.r.randint(3, 6)
        rows = self.r.randint(4, 12)
        heads = [self.r.choice(TECH).capitalize() for _ in range(cols)]
        aligns = [self.r.choice([":---", ":---:", "---:", "---"]) for _ in range(cols)]
        lines = ["| " + " | ".join(heads) + " |", "| " + " | ".join(aligns) + " |"]
        for _ in range(rows):
            cells = []
            for c in range(cols):
                k = self.r.random()
                if aligns[c] == "---:":
                    cells.append(f"{self.r.uniform(0, 100000):,.2f}")
                elif k < 0.1:
                    cells.append(f"`{self.r.choice(TECH)}`")
                elif k < 0.15:
                    cells.append(self.r.choice([":white_check_mark:", ":x:", ":warning:"]))
                else:
                    cells.append(self.words(self.r.randint(1, 5)))
            lines.append("| " + " | ".join(cells) + " |")
        return "\n".join(lines)

    def code(self):
        lang = self.r.choice(LANGS)
        n = self.r.randint(6, 30)
        ident = lambda: f"{self.r.choice(TECH)}_{self.r.choice(TECH)}"
        body = []
        if lang in ("c", "cpp", "csharp", "java", "javascript", "typescript", "rust", "go"):
            kw = {"c": "static int", "cpp": "auto", "csharp": "var", "java": "final int", "javascript": "const",
                  "typescript": "const", "rust": "let", "go": "var"}[lang]
            body.append(f"// {self.sentence_plain()}")
            for _ in range(n):
                body.append(f"    {kw} {ident()} = {ident()}({self.r.randint(0, 999)}, \"{self.r.choice(WORDS)}\");")
            body = ["{"] + body + ["}"]
        elif lang == "python":
            body.append(f"def {ident()}(value: int) -> int:")
            body.append(f"    \"\"\"{self.sentence_plain()}\"\"\"")
            for _ in range(n):
                body.append(f"    value = {ident()}(value, {self.r.randint(0, 99)})  # {self.r.choice(WORDS)}")
            body.append("    return value")
        elif lang == "sql":
            body.append(f"SELECT {', '.join(ident() for _ in range(4))}")
            body.append(f"FROM {ident()} AS t")
            body.append(f"WHERE t.{ident()} > {self.r.randint(1, 100)} AND t.{ident()} LIKE '%{self.r.choice(WORDS)}%'")
            body.append(f"ORDER BY {ident()} DESC;")
        elif lang in ("bash", "powershell"):
            for _ in range(n):
                cmd = self.r.choice(["echo", "cp", "mkdir -p", "git log --oneline", "Write-Host", "Get-ChildItem"])
                body.append(f"{cmd} \"{self.words(3)}\"  # {self.r.choice(TECH)}")
        elif lang in ("json", "yaml"):
            for i in range(n):
                k, v = ident(), self.r.choice([str(self.r.randint(0, 1000)), "true", "null", f"\"{self.r.choice(WORDS)}\""])
                body.append(f"  \"{k}\": {v}," if lang == "json" else f"{k}: {v}")
            if lang == "json":
                body[-1] = body[-1].rstrip(",")
                body = ["{"] + body + ["}"]
        elif lang == "html":
            body.append("<section class=\"content\">")
            for _ in range(n):
                body.append(f"  <p class=\"{self.r.choice(TECH)}\">{self.words(6)} &amp; <b>{self.r.choice(WORDS)}</b></p>")
            body.append("</section>")
        elif lang == "css":
            for _ in range(n // 3 + 1):
                body.append(f".{ident()} {{ margin: {self.r.randint(0, 32)}px; color: #{self.r.randint(0, 0xFFFFFF):06x}; }}")
        elif lang == "diff":
            body.append(f"--- a/src/{self.r.choice(TECH)}.cpp")
            body.append(f"+++ b/src/{self.r.choice(TECH)}.cpp")
            body.append(f"@@ -{self.r.randint(1, 900)},{n} +{self.r.randint(1, 900)},{n} @@")
            for _ in range(n):
                body.append(self.r.choice([" ", "-", "+"]) + f"    {ident()}();")
        else:
            for _ in range(n):
                body.append(self.words(self.r.randint(4, 12)))
        return f"```{lang}\n" + "\n".join(body) + "\n```"

    def quote(self):
        return "\n>\n".join("> " + self.sentence() for _ in range(self.r.randint(1, 3)))

    def alert(self):
        kind = self.r.choice(ALERTS)
        return f"> [!{kind}]\n> {self.sentence()} {self.sentence()}"

    def image(self):
        src = self.r.choice(IMAGES)
        return f"![{self.words(3)}]({src} \"{self.words(4)}\")"

    def block(self):
        k = self.r.random()
        if k < 0.42:
            return self.paragraph()
        if k < 0.52:
            return self.bullet_list()
        if k < 0.58:
            return self.ordered_list()
        if k < 0.68:
            return self.table()
        if k < 0.82:
            return self.code()
        if k < 0.88:
            return self.quote()
        if k < 0.92:
            return self.alert()
        if k < 0.95:
            return self.image()
        if k < 0.97:
            return "---"
        return f"<details>\n<summary>{self.words(4)}</summary>\n\n{self.paragraph()}\n\n</details>"


def slug(text):
    out = []
    for ch in text.lower():
        if ch.isalnum() or ch in "-_":
            out.append(ch)
        elif ch == " ":
            out.append("-")
    return "".join(out)


def generate(target_bytes, seed=20260923):
    g = Gen(seed)
    chapters = []
    size = 0
    ch = 0
    while size < target_bytes:
        ch += 1
        title = f"Chapter {ch}: {g.words(2, TECH).title()} and {g.words(1, TECH).title()}"
        g.heading_ids.append(slug(title))
        parts = [f"## {title}", g.paragraph()]
        for sec in range(1, g.r.randint(6, 10) + 1):
            stitle = f"{ch}.{sec} {g.words(g.r.randint(2, 4)).capitalize()}"
            g.heading_ids.append(slug(stitle))
            parts.append(f"### {stitle}")
            for _ in range(g.r.randint(3, 8)):
                parts.append(g.block())
            if g.r.random() < 0.3:
                parts.append(f"#### {g.words(3).capitalize()}")
                parts.append(g.paragraph())
                if g.r.random() < 0.3:
                    parts.append(f"##### {g.words(2).capitalize()}")
                    parts.append(g.paragraph())
                    parts.append(f"###### {g.words(2).capitalize()}")
                    parts.append(g.paragraph())
        parts.append("[Back to top](#large-document-performance-test)")
        text = "\n\n".join(parts)
        chapters.append((title, text))
        size += len(text.encode("utf-8"))

    head = [
        "# Large Document (Performance Test)",
        "This generated file (by `tools/gen_large.py`, deterministic seed) is about "
        f"{target_bytes / 1_000_000:.1f} MB of realistic mixed Markdown: {len(chapters)} chapters of headings at all "
        "levels, long paragraphs with inline formatting, links, emoji and footnotes, lists and task lists, "
        "tables, fenced code in many languages, block quotes, alerts, images and `<details>` blocks. Use it "
        "to measure load time, time to first paint, scrolling smoothness (drag the thumb and use Page Down), "
        "memory usage, search, and select-all/copy on a large document. The content is meaningless on purpose.",
        "## Table of contents",
        "\n".join(f"{i}. [{t}](#{slug(t)})" for i, (t, _) in enumerate(chapters, 1)),
    ]
    foot = [f"[^{i}]: {text}" for i, text in enumerate(g.footnotes, 1)]
    doc = "\n\n".join(head + [c for _, c in chapters]) + "\n\n" + "\n".join(foot) + "\n"
    return doc, len(chapters), len(g.footnotes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size-mb", type=float, default=2.5)
    ap.add_argument("-o", "--output", default=DEFAULT_OUT)
    a = ap.parse_args()
    doc, n_ch, n_fn = generate(int(a.size_mb * 1_000_000))
    with open(a.output, "w", encoding="utf-8", newline="\n") as f:
        f.write(doc)
    print(f"wrote {a.output}: {len(doc.encode('utf-8')):,} bytes, {doc.count(chr(10)):,} lines, "
          f"{n_ch} chapters, {n_fn} footnotes")


if __name__ == "__main__":
    main()
