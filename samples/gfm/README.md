# GFM Test Corpus for mdview

This folder is a corpus of Markdown documents for checking the native (Direct2D/DirectWrite) renderer against GitHub's own rendering. Each file starts with a title and a short description of what it tests, and is split into labeled sections that you can compare one at a time. Where the correct result isn't obvious, the text says *Expected: …*. The tricky syntax was checked against **cmark-gfm**, GitHub's reference parser, and smoke-tested with the md4c fork in `third_party/md4c`.

## Files

| File | Encoding / EOL | Size | What it tests |
|------|----------------|-----:|---------------|
| [01-gfm-spec-tour.md](01-gfm-spec-tour.md) | UTF-8, LF | 33 KB | Every chapter of the [GFM spec](https://github.github.com/gfm/), in spec order: tabs, thematic breaks, ATX/setext headings, indented/fenced code, all 7 kinds of HTML block, link reference definitions, tables, block quotes, list items, task lists, escapes, entities, code spans, emphasis corner cases, strikethrough, links, images, autolinks and extended autolinks, raw HTML, the tag filter, hard/soft breaks. Includes a hand-written TOC with GitHub-style anchors. |
| [02-github-readme.md](02-github-readme.md) | UTF-8, LF | 16 KB | A realistic project README ("fastgrep"): centered `<div>`/`<p align>` header, logo, shields.io badge rows, `<picture>` dark/light sources, emoji shortcodes, `<details>`, all-contributors `<table>`, `<kbd>`, `<sup>`/`<sub>`, footnotes, alerts, FAQ, footer. |
| [03-alerts-footnotes-extras.md](03-alerts-footnotes-extras.md) | UTF-8, LF | 21 KB | GitHub extensions: the five alerts (rich content, case variants, invalid markers), footnotes (named, repeated, multi-block, unused), emoji shortcodes vs. look-alikes (`10:30:45`), ZWJ/flag/skin-tone emoji, math, Mermaid, non-GFM syntax that must stay literal (`==mark==`), inline HTML elements, `id`/`name` anchors, `align`, `<img>` sizing and floating, every image format, Cyrillic file names with spaces, cross-file links. |
| [04-code-highlighting.md](04-code-highlighting.md) | UTF-8, LF | 73 KB | Fenced code in 45+ languages with realistic snippets, language aliases, no/unknown language, info-string attributes, indented code, a 438-character line, TAB stops, box drawing, CJK/RTL/emoji in code, inline code with backticks and HTML, a 300-line block. |
| [05-tables.md](05-tables.md) | UTF-8, LF | 37 KB | Alignment, 16- and 24-column tables, unbreakable content, rich cells, empty cells, one-column, header-only and ragged tables, escaped/unescaped pipes, tables in lists/quotes/alerts/`<details>`, right-aligned numbers, multi-script tables, HTML tables with `colspan`/`rowspan`/`<caption>`, a 200-row table. |
| [06-lists-and-quotes.md](06-lists-and-quotes.md) | UTF-8, LF | 19 KB | Six-level mixed nesting, tight vs. loose, rich list items (code, quotes, tables, images, headings), start numbers 0/7/99/1000/999999999, marker-width changes, hanging indents, five-level quotes with rich content, lazy continuation, a 150-item list. |
| [07-typography-i18n.md](07-typography-i18n.md) | UTF-8, LF | 31 KB | 15+ languages (Cyrillic, Greek, CJK, Arabic, Hebrew, Devanagari, Thai, Georgian, Armenian, …), RTL and mixed-direction text, a font-fallback table of rare scripts, emoji, math/arrows/currency symbols, quotes and dashes, combining marks and Zalgo text, zero-width and special spaces, soft hyphens, a 300-character word, long URLs, headings in mixed scripts. |
| [08-edge-cases.md](08-edge-cases.md) | UTF-8, **CRLF** (+1 bare LF, +1 bare CR, no final EOL) | 126 KB | Pathological input: empty and 1000-character headings, 50 rules, 30-level and 250-level quotes, 100-level lists, deep emphasis and brackets, `javascript:`/`data:`/`file:` URLs, Windows and UNC paths, invalid entities, control characters, 10,000-character lines, TABs and trailing whitespace, cmark's pathological inputs, and unclosed `<table>`, `<div>`, `<details>` and code fence (runs to EOF). |
| [09-large-document.md](09-large-document.md) | UTF-8, LF | 2.5 MB | Performance: 60 chapters, about 22k lines, 270 tables, 420 code blocks, 90 images, 200 footnotes. Measure load time, scrolling, memory, and select-all/copy. |
| [10-encoding-utf8-bom.md](10-encoding-utf8-bom.md) | UTF-8 **with BOM**, CRLF | 1.5 KB | The BOM must be detected and hidden. |
| [11-encoding-utf16le.md](11-encoding-utf16le.md) | UTF-16 LE **with BOM**, CRLF | 2.5 KB | Notepad's "Unicode" encoding, including surrogate pairs. |
| [12-encoding-utf16be.md](12-encoding-utf16be.md) | UTF-16 BE **with BOM**, LF | 2.3 KB | Big-endian byte order. |
| [13-encoding-cp1251.md](13-encoding-cp1251.md) | **Windows-1251**, no BOM, CRLF | 1.3 KB | Russian text in a legacy ANSI code page. It is invalid as UTF-8, so the viewer must fall back to CP1251. |
| [14-encoding-utf16le-nobom.md](14-encoding-utf16le-nobom.md) | UTF-16 LE, **no BOM**, LF | 2.7 KB | Heuristic detection (a `00` byte at every odd offset). |
| [15-encoding-invalid-utf8.md](15-encoding-invalid-utf8.md) | UTF-8 with 7 invalid sequences, LF | 1.4 KB | A few bad bytes must become U+FFFD, not flip the whole document into a legacy code page. |
| [16-front-matter.md](16-front-matter.md) | UTF-8, LF | 1.3 KB | YAML front matter shown as a two-column key/value table (nested mappings as nested tables, short lists joined with commas), while `---` elsewhere stays a thematic break or setext underline. |
| [17-flags.md](17-flags.md) | UTF-8, LF | 1.7 KB | Flag emoji (regional indicator pairs, shortcodes, England/Scotland/Wales tag sequences) in text, headings, tables, lists, links, inline code and code blocks; odd counts, non-countries and superscript stay letters. |
| [18-math.md](18-math.md) | UTF-8, LF | 3 KB | Math typeset like MathJax: inline and display formulas, fractions, roots, limits, big operators, `\left…\right` and `\big` delimiters, matrices, `cases`, `aligned`, accents, braces, math alphabets, `\color`, `\boxed`, a ```` ```math ```` block and unknown commands. |
| [19-zoom.md](19-zoom.md) | UTF-8, LF | 2.4 KB | mdview-only: zooming images inside their box (Ctrl+wheel, drag, double click, the corner buttons) and the full-window view, on a photo, an SVG, a wide image, an animated GIF, a linked image, images in a table, badges too small to zoom and a tall image. |
| [20-mermaid-flowchart.md](20-mermaid-flowchart.md) | UTF-8, LF | 2.6 KB | Mermaid flowcharts drawn natively: a decision, every node shape, every kind of link (ends, strokes, lengths, labels), subgraphs with their own direction and subgraphs linked across, a titled pipeline with `classDef`/`style`/`linkStyle`, markdown and HTML in labels, and blocks that stay code with a note (an unsupported diagram type, a syntax error). |
| [21-mermaid-sequence.md](21-mermaid-sequence.md) | UTF-8, LF | 2.5 KB | Mermaid sequence diagrams drawn natively: every kind of arrow, a message to itself, actors, aliases, activations (`+`/`-` and `activate`), notes beside and over participants, `autonumber`, every block (`loop`, `alt`/`else`, `opt`, `par`/`and`, `critical`/`option`, `break`, `rect`), colored boxes around participants, `create`/`destroy`, and a front matter title. |
| [22-mermaid-class.md](22-mermaid-class.md) | UTF-8, LF | 2.8 KB | Mermaid class diagrams drawn natively: classes with members declared both ways, visibility, generics (`List~List~int~~`), static and abstract members, annotations, notes, every relation (inheritance, composition, aggregation, association, dependency, realization, links), cardinalities, two-way relations, lollipop interfaces, a namespace, `classDef`/`cssClass`, class labels and `direction LR`. |
| [23-mermaid-state.md](23-mermaid-state.md) | UTF-8, LF | 2.1 KB | Mermaid state diagrams drawn natively: start and end points, transitions with events, a choice, a fork and a join, descriptions, a transition to itself, notes (one line and several), a composite state with concurrent regions and one linked from outside, nested composites left to right, a `classDef` and a front matter title. |
| [24-mermaid-er.md](24-mermaid-er.md) | UTF-8, LF | 1.3 KB | Mermaid entity relationship diagrams drawn natively: every cardinality (symbols and words), identifying and non-identifying relationships, attributes with types (`string(99)`, `string[]`), keys (`PK, FK`, `UK`) and comments, entity aliases (`p[Person]`, quoted), a relationship to itself, `direction LR` and a front matter title. |

## Images (`img/`)

| File | Format | Purpose |
|------|--------|---------|
| `badge-build-passing.svg`, `badge-coverage.svg` | SVG | shields.io-style badges with `<text>`, `<linearGradient>`, `<clipPath>`, `transform="scale(.1)"` |
| `gradients.svg` | SVG | linear/radial gradients, Bézier paths, dashes, transforms, `<text>` |
| `viewbox-only.svg` | SVG | no `width`/`height`, only a `viewBox` |
| `transparent.png` | PNG RGBA | alpha channel over a fully transparent background |
| `indexed-trns.png` | PNG palette + tRNS | palette transparency |
| `hidpi@2x.png` | PNG | 400×160 image shown at 200 px wide; the hairlines must stay crisp |
| `wide-2400x300.png` | PNG | must be scaled down to the content width (with ruler ticks) |
| `tall-300x1200.png` | PNG | tall image, which drives scrolling |
| `photo.jpg` | JPEG baseline | photo-like gradients |
| `animated.gif` | GIF89a, 3 frames | animation (or at least the first frame) |
| `palette.bmp` | BMP 24-bit | BMP support |
| `картинка с пробелами.png` | PNG | a file name with Cyrillic letters and spaces (referenced raw, `%20`-encoded and fully percent-encoded) |

The files also reference `../logo.png`, `../avatar.png` and `../diagram.png` from the parent `samples/` folder, plus a few remote images (shields.io, GitHub assets) that only load with network access.

## Regenerating

The generated files are deterministic. The generators need only the Python 3 standard library:

```console
$ python samples/gfm/tools/gen_images.py       # img/*
$ python samples/gfm/tools/gen_edge_cases.py   # 08-edge-cases.md (CRLF)
$ python samples/gfm/tools/gen_large.py        # 09-large-document.md (use --size-mb N to scale)
$ python samples/gfm/tools/gen_encodings.py    # 10..15 encoding variants
```

Files 01–07 are hand-written. The 300-line block in 04, the 200-row table in 05 and the 150-item list in 06 were generated once and pasted in.

## Getting reference output

To see what GitHub would produce for any of these files, use GitHub's own parser:

```console
$ pip install cmarkgfm
$ python -c "import cmarkgfm,sys; print(cmarkgfm.github_flavored_markdown_to_html(open(sys.argv[1],encoding='utf-8').read()))" 01-gfm-spec-tour.md > 01.html
```

github.com adds more on top of cmark-gfm: alerts, emoji shortcodes, math, Mermaid, heading anchors, `dir="auto"`, and HTML sanitization. For those parts, the *Expected:* notes in the files describe github.com's behavior.
