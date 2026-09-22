# Tables Stress Test

This file stress-tests GFM pipe tables and HTML tables. It covers column alignment, tables far wider than the window (16 columns), long unbreakable content (URLs, hashes, identifiers), rich inline content in cells, empty cells, degenerate shapes (one column, header only, ragged rows), escaped pipes, tables nested in lists, quotes and alerts, a 200-row table, right-aligned numbers, several scripts in one table, and HTML tables with `colspan`/`rowspan`/`<caption>`. On github.com, wide tables get a horizontal scrollbar instead of squeezing, rows are zebra-striped, and header cells are bold.

## Contents

- [Alignment](#alignment)
- [Very wide table](#very-wide-table)
- [Long unbreakable content](#long-unbreakable-content)
- [Rich cell content](#rich-cell-content)
- [Empty cells](#empty-cells)
- [Degenerate shapes](#degenerate-shapes)
- [Escaped and unescaped pipes](#escaped-and-unescaped-pipes)
- [Tables inside other blocks](#tables-inside-other-blocks)
- [Numbers](#numbers)
- [Unicode content](#unicode-content)
- [HTML tables](#html-tables)
- [200-row table](#200-row-table)

---

## Alignment

Each column has a different alignment. The header cells follow their column's alignment too.

| Default | Left | Center | Right |
|---------|:-----|:------:|------:|
| a | a | a | a |
| longer text | longer text | longer text | longer text |
| the quick brown fox jumps over the lazy dog | 1 | 22 | 333 |
| x | x | x | 4444 |

The minimal delimiter row `:-|-:|:-:`, without leading or trailing pipes:

a|b|c
:-|-:|:-:
left|right|center
1|2|3

Alignment colons with extra spaces and long dashes:

|   Name   |   Qty   |   Price   |
| :------------------ | :-------------: | --------------------: |
| Widget | 3 | 9.99 |
| Gadget with a longer name | 12 | 1,249.00 |

---

## Very wide table

This table has 16 columns and is much wider than a typical window. github.com shows a horizontal scrollbar. A viewer may instead shrink columns and wrap text, but it must never clip content silently.

| ID | Name | Email | Department | Title | Location | Phone | Start date | Manager | Salary band | Status | Projects | Skills | Languages | Last login | Notes |
|---:|------|-------|------------|-------|----------|-------|------------|---------|:-----------:|:------:|---------:|--------|-----------|------------|-------|
| 1 | Ada Lovelace | ada@example.com | Engineering | Principal Engineer | London, UK | +44 20 7946 0001 | 2019-03-01 | Charles Babbage | L7 | :green_circle: Active | 12 | C, C++, Rust, analysis | en, fr | 2026-09-22 18:04 | Writes the best algorithms |
| 2 | Grace Hopper | grace@example.com | Compilers | Rear Admiral | Arlington, VA | +1 703 555 0142 | 2018-11-12 | — | L8 | :green_circle: Active | 31 | COBOL, compilers, debugging | en | 2026-09-23 07:30 | Found an actual bug |
| 3 | Линус Торвальдс | linus@example.com | Kernel | Maintainer | Portland, OR | +1 503 555 0199 | 2020-01-15 | — | L8 | :yellow_circle: Away | 2 | C, git, email | fi, sv, en | 2026-09-19 23:59 | Prefers plain text email |
| 4 | 山田 花子 | hanako@example.jp | Design | UX Lead | 東京 | +81 3-1234-5678 | 2021-07-01 | Ada Lovelace | L6 | :red_circle: Leave | 7 | Figma, typography, CJK layout | ja, en | 2026-08-30 09:12 | Owns the CJK line-breaking spec |

A table with only short cells but 24 columns:

| A | B | C | D | E | F | G | H | I | J | K | L | M | N | O | P | Q | R | S | T | U | V | W | X |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 |
| α | β | γ | δ | ε | ζ | η | θ | ι | κ | λ | μ | ν | ξ | ο | π | ρ | σ | τ | υ | φ | χ | ψ | ω |

---

## Long unbreakable content

Cells whose content has no break opportunities. With no horizontal scrolling, they force the table wider than the window, or they have to break anywhere.

| Kind | Value |
|------|-------|
| URL | https://github.com/mity/md4c/blob/0123456789abcdef0123456789abcdef01234567/src/md4c.c#L1234-L1300 |
| Linked URL | [https://example.com/a/very/long/path/that/keeps/going/and/going/index.html?query=string&and=more](https://example.com) |
| SHA-256 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| Identifier | `AbstractSingletonProxyFactoryBeanDefinitionRegistryPostProcessorConfigurationHolder` |
| Windows path | `C:\Program Files\Common Files\Microsoft Shared\ClickToRun\Updates\16.0.17928.20156\x64\setup.exe` |
| Base64 | `iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==` |
| German compound | Donaudampfschifffahrtselektrizitätenhauptbetriebswerkbauunterbeamtengesellschaft |
| Plain text | Short words wrap normally, so this ordinary sentence can wrap inside the cell without any trouble at all. |

---

## Rich cell content

| Content | Example | Notes |
|---------|---------|-------|
| **Bold** | **strong text** | |
| *Italic* | *emphasized text* | |
| ***Both*** | ***bold italic*** | |
| ~~Strike~~ | ~~obsolete~~ and ~single tilde~ | |
| `Code` | `const x = 42;` | inline code background inside a cell |
| Link | [md4c on GitHub](https://github.com/mity/md4c "tooltip in a cell") | |
| Autolink | https://example.com and www.example.com | extended autolinks |
| Image | ![avatar](../avatar.png) | 50×50 PNG |
| Big image | ![logo](../logo.png) | the row must grow to fit |
| SVG badge | ![build](img/badge-build-passing.svg) | |
| Emoji | :rocket: :tada: 👍🏽 🇺🇦 | shortcodes and Unicode |
| HTML | <kbd>Ctrl</kbd>+<kbd>S</kbd>, H<sub>2</sub>O, x<sup>2</sup> | inline HTML |
| Line breaks | first line<br>second line<br/>third line | `<br>` is the only way to break a line in a cell |
| Mixed | **bold `code` inside** and [*italic link*](#rich-cell-content) | nested inline markup |
| Footnote-like | text[^cell] | a GFM footnote reference inside a cell |
| Escapes | \*not italic\* and \`not code\` | backslash escapes |
| Entities | &copy; &mdash; &rarr; &#x1F600; | |

[^cell]: A footnote referenced from a table cell.

---

## Empty cells

| A | B | C |
|---|---|---|
| | filled | |
| filled | | filled |
| | | |
| last row | | |

Empty header cells:

| | Column 2 | |
|---|---|---|
| row 1 | x | y |
| row 2 | x | y |

---

## Degenerate shapes

A single-column table:

| Single column |
|---------------|
| one |
| two |
| three |

A single column with no body rows (header only):

| Lonely header |
|---|

A header-only table with three columns:

| Name | Value | Unit |
|------|------:|:----:|

Rows with **missing** cells are padded, and **extra** cells are dropped:

| A | B | C | D |
|---|---|---|---|
| 1 |
| 1 | 2 |
| 1 | 2 | 3 |
| 1 | 2 | 3 | 4 |
| 1 | 2 | 3 | 4 | 5 (dropped) | 6 (dropped) |

A 1×1 table:

|x|
|-|
|y|

A table right after a paragraph, with no blank line between them:
| a | b |
|---|---|
| 1 | 2 |

---

## Escaped and unescaped pipes

| Source | Renders as |
|--------|------------|
| `\|` in text | a \| b |
| `\|` inside code | `a \| b` |
| `\|\|` logical OR | `x \|\| y` |
| pipe inside a link | [a \| b](https://example.com) |

An **unescaped** pipe inside a code span still splits the cell, because the table is parsed before the inline code. Expected: the body row splits into the fragments `` `x `` and `` y` `` (each keeps a literal backtick), and the table has only two columns, so the third cell, z, is dropped.

| A | B |
|---|---|
| `x|y` | z |

---

## Tables inside other blocks

- A list item containing a table:

  | Key | Value |
  |-----|-------|
  | foo | bar |
  | baz | qux |

- A second item with a nested list, and a table inside that:
  1. Nested ordered item

     | Nested | Table |
     |:------:|:-----:|
     | 1 | 2 |

> A block quote containing a table:
>
> | Quote | Table |
> |-------|-------|
> | inside | a quote |
>
> > A nested quote with another table:
> >
> > | Deep | Table |
> > |------|-------|
> > | 2 levels | down |

> [!TIP]
> An alert containing a table:
>
> | Shortcut | Action |
> |----------|--------|
> | <kbd>Ctrl</kbd>+<kbd>F</kbd> | Find |

<details>
<summary>A table inside a <code>&lt;details&gt;</code> block</summary>

| Hidden | Table |
|--------|-------|
| shown | on expand |

</details>

---

## Numbers

Financial data with right-aligned numeric columns. The digits should line up, which works best with tabular (fixed-width) figures:

| Quarter | Revenue | Costs | Profit | Margin | YoY |
|:--------|--------:|------:|-------:|-------:|----:|
| Q1 2025 | 1,204,331.50 | 987,120.00 | 217,211.50 | 18.0% | +4.2% |
| Q2 2025 | 1,356,870.25 | 1,011,400.75 | 345,469.50 | 25.5% | +11.9% |
| Q3 2025 | 998,004.00 | 1,050,212.10 | −52,208.10 | −5.2% | −8.3% |
| Q4 2025 | 1,811,111.11 | 1,102,000.00 | 709,111.11 | 39.2% | +21.0% |
| **Total** | **5,370,316.86** | **4,150,732.85** | **1,219,584.01** | **22.7%** | |

Numbers of very different widths, which also exercises column sizing:

| n | n² | 2ⁿ | n! |
|--:|---:|---:|---:|
| 1 | 1 | 2 | 1 |
| 5 | 25 | 32 | 120 |
| 10 | 100 | 1,024 | 3,628,800 |
| 20 | 400 | 1,048,576 | 2,432,902,008,176,640,000 |
| 30 | 900 | 1,073,741,824 | 265,252,859,812,191,058,636,308,480,000,000 |

---

## Unicode content

| Language | Script | Sample | Direction |
|----------|--------|--------|:---------:|
| English | Latin | The quick brown fox jumps over the lazy dog | LTR |
| Russian | Cyrillic | Съешь же ещё этих мягких французских булок, да выпей чаю | LTR |
| Ukrainian | Cyrillic | Чуєш їх, доцю, га? Кумедна ж ти, прощайся без ґольфів! | LTR |
| Greek | Greek | Ξεσκεπάζω την ψυχοφθόρα βδελυγμία | LTR |
| Chinese | Han | 我能吞下玻璃而不伤身体。 | LTR |
| Japanese | Kana + Kanji | いろはにほへと ちりぬるを わかよたれそ | LTR |
| Korean | Hangul | 다람쥐 헌 쳇바퀴에 타고파 | LTR |
| Arabic | Arabic | نص حكيم له سر قاطع وذو شأن عظيم | RTL |
| Hebrew | Hebrew | דג סקרן שט בים מאוכזב ולפתע מצא חברה | RTL |
| Hindi | Devanagari | ऋषियों को सताने वाले दुष्ट राक्षसों के राजा रावण का सर्वनाश करने वाले | LTR |
| Thai | Thai | เป็นมนุษย์สุดประเสริฐเลิศคุณค่า | LTR |
| Emoji | — | 👨‍👩‍👧‍👦 🏳️‍🌈 👍🏿 🇯🇵 | — |

A table whose column headers are in several scripts:

| 名前 | Имя | الاسم | שם | Όνομα |
|------|-----|-------|----|-------|
| 太郎 | Иван | محمد | דוד | Νίκος |
| 花子 | Мария | فاطمة | שרה | Ελένη |

---

## HTML tables

A raw HTML table with a caption, `<thead>`, a `colspan`, a `rowspan` and `<th>` header cells:

<table>
  <caption>Release matrix (HTML table with caption)</caption>
  <thead>
    <tr>
      <th rowspan="2">Component</th>
      <th colspan="3">Platform</th>
      <th rowspan="2">Notes</th>
    </tr>
    <tr>
      <th>x86</th>
      <th>x64</th>
      <th>ARM64</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><b>Lister plugin</b></td>
      <td align="center">✅</td>
      <td align="center">✅</td>
      <td align="center">✅</td>
      <td>All three builds are signed</td>
    </tr>
    <tr>
      <td><code>md2html</code> CLI</td>
      <td align="center" colspan="2">✅ (single fat binary)</td>
      <td align="center">❌</td>
      <td>ARM64 is planned</td>
    </tr>
    <tr>
      <td rowspan="2">Tests</td>
      <td colspan="3" align="center"><i>run on CI</i></td>
      <td>unit + spec tests</td>
    </tr>
    <tr>
      <td colspan="3" align="right"><sub>ARM64 is cross-compiled only</sub></td>
      <td><a href="#200-row-table">see the big table</a></td>
    </tr>
  </tbody>
  <tfoot>
    <tr><td colspan="5" align="center"><small>Footer row spanning all five columns</small></td></tr>
  </tfoot>
</table>

HTML cells may contain markdown if blank lines surround it (github.com renders the list and the bold text below):

<table>
<tr>
<th>Markdown in HTML</th>
<th>Nested HTML table</th>
</tr>
<tr>
<td>

- **bold** item
- `code` item
- [link](#html-tables)

</td>
<td>

<table>
<tr><td>nested</td><td>table</td></tr>
<tr><td colspan="2" align="center">inside a cell</td></tr>
</table>

</td>
</tr>
<tr>
<td>Markdown <b>without</b> blank lines: *this stays literal*</td>
<td><img src="../avatar.png" width="32" alt="avatar"> image in a cell</td>
</tr>
</table>

An HTML table with `width` and `align` attributes (github.com strips `width` on tables, so this looks like a normal table there):

<table width="100%">
  <tr>
    <td align="left" width="33%">left</td>
    <td align="center" width="34%">center</td>
    <td align="right" width="33%">right</td>
  </tr>
</table>

---

## 200-row table

Generated data: 200 rows × 6 columns with mixed alignment. Use it to check scrolling performance, zebra striping, and that the column widths stay stable over the whole table.

| # | Package | Version | Downloads | License | Status | Description |
|--:|:--------|:-------:|----------:|:-------:|:------:|:------------|
| 1 | rusty-parse | 0.23.35 | 32,868,838 | MPL-2.0 | :green_circle: | A rusty parse library with no dependencies |
| 2 | rusty-date | 1.18.54 | 4,265,809 | MIT | :green_circle: | Bindings for the native date API |
| 3 | hyper-csv | 9.0.71 | 26,687,547 | Unlicense | :green_circle: | Zero-copy csv reader with SIMD acceleration |
| 4 | turbo-crypt | 0.24.20 | 93,702,693 | Unlicense | :green_circle: | Streaming crypt encoder/decoder |
| 5 | micro-log | 5.3.11 | 50,992,989 | Apache-2.0 | :green_circle: | Micro log utilities for embedded targets |
| 6 | turbo-crypt | 0.23.58 | 71,971,326 | Apache-2.0 | :yellow_circle: | A turbo crypt library with no dependencies |
| 7 | simple-diff | 9.28.46 | 77,490,903 | MPL-2.0 | :green_circle: | Parses diff input in a single pass |
| 8 | rusty-http | 4.2.29 | 13,556,192 | Unlicense | :green_circle: | Zero-copy http reader with SIMD acceleration |
| 9 | rusty-image | 2.11.45 | 28,119,567 | GPL-3.0 | :green_circle: | Command-line tool for image files |
| 10 | simple-http | 2.14.48 | 36,231,793 | MPL-2.0 | :green_circle: | Parses http input in a single pass |
| 11 | hyper-json | 5.12.34 | 8,883,694 | MPL-2.0 | :red_circle: | ~~Hyper json utilities for embedded targets~~ (deprecated) |
| 12 | hyper-zip | 6.28.82 | 61,586,063 | BSD-3-Clause | :green_circle: | Command-line tool for zip files |
| 13 | `hyper-date` | 8.8.95 | 78,461,813 | Unlicense | :red_circle: | Drop-in replacement for the standard date module |
| 14 | async-http | 2.16.63 | 12,201,664 | MIT | :green_circle: | Command-line tool for http files |
| 15 | rusty-queue | 6.19.8 | 51,642,604 | Unlicense | :red_circle: | Zero-copy queue reader with SIMD acceleration |
| 16 | simple-crypt | 8.27.1 | 91,306,103 | Apache-2.0 | :red_circle: | Streaming crypt encoder/decoder |
| 17 | [open-markdown](https://example.com/open-markdown) | 1.9.55 | 21,227,584 | LGPL-2.1 | :green_circle: | Streaming markdown encoder/decoder |
| 18 | simple-queue | 8.29.13 | 83,926,381 | GPL-3.0 | :red_circle: | Bindings for the native queue API |
| 19 | micro-image | 2.17.99 | 71,182,874 | MIT | :red_circle: | Micro image utilities for embedded targets |
| 20 | smart-grep | 1.29.46 | 41,273,857 | MPL-2.0 | :green_circle: | Bindings for the native grep API |
| 21 | zen-uuid | 1.2.93 | 65,228,545 | Apache-2.0 | :red_circle: | Command-line tool for uuid files |
| 22 | micro-zip | 8.5.33 | 70,823,186 | Unlicense | :green_circle: | ~~Bindings for the native zip API~~ (deprecated) |
| 23 | quick-diff | 6.21.83 | 50,119,661 | LGPL-2.1 | :red_circle: | Zero-copy diff reader with SIMD acceleration |
| 24 | tiny-http | 3.2.43 | 2,823,180 | MPL-2.0 | :red_circle: | Bindings for the native http API |
| 25 | **fast-yaml** | 0.7.8 | 4,216,185 | ISC | :green_circle: | Bindings for the native yaml API :star: |
| 26 | `lazy-zip` | 3.17.16 | 97,086,747 | LGPL-2.1 | :green_circle: | Zero-copy zip reader with SIMD acceleration |
| 27 | open-layout | 3.3.12 | 88,447,177 | Unlicense | :green_circle: | Drop-in replacement for the standard layout module |
| 28 | safe-regex | 0.21.83 | 86,728,788 | Apache-2.0 | :green_circle: | Drop-in replacement for the standard regex module |
| 29 | quick-markdown | 1.7.24 | 25,529,417 | LGPL-2.1 | :green_circle: | Drop-in replacement for the standard markdown module |
| 30 | micro-crypt | 7.7.9 | 59,476,011 | Apache-2.0 | :green_circle: | Parses crypt input in a single pass |
| 31 | tiny-http | 2.13.62 | 64,606,843 | MPL-2.0 | :yellow_circle: | Parses http input in a single pass |
| 32 | micro-font | 0.12.33 | 61,070,199 | GPL-3.0 | :yellow_circle: | Zero-copy font reader with SIMD acceleration |
| 33 | micro-log | 4.6.7 | 77,736,272 | MIT | :green_circle: | ~~Parses log input in a single pass~~ (deprecated) |
| 34 | [fast-uuid](https://example.com/fast-uuid) | 7.16.67 | 21,130,273 | MIT | :red_circle: | A fast uuid library with no dependencies |
| 35 | deep-queue | 1.19.8 | 90,625,504 | MPL-2.0 | :yellow_circle: | A deep queue library with no dependencies |
| 36 | zen-uuid | 3.18.76 | 5,334,045 | Apache-2.0 | :yellow_circle: | Zen uuid utilities for embedded targets |
| 37 | zen-crypt | 3.21.91 | 42,169,054 | MPL-2.0 | :green_circle: | Drop-in replacement for the standard crypt module |
| 38 | micro-diff | 7.10.96 | 9,736,582 | MIT | :yellow_circle: | A micro diff library with no dependencies |
| 39 | `tiny-date` | 3.16.33 | 17,778,029 | ISC | :green_circle: | Bindings for the native date API |
| 40 | async-diff | 2.14.69 | 94,427,540 | GPL-3.0 | :red_circle: | Parses diff input in a single pass |
| 41 | rusty-date | 4.29.84 | 13,903,154 | BSD-3-Clause | :green_circle: | A rusty date library with no dependencies |
| 42 | zen-parse | 8.4.34 | 37,816,696 | MPL-2.0 | :green_circle: | Bindings for the native parse API |
| 43 | rusty-crypt | 8.15.32 | 6,818,122 | Apache-2.0 | :yellow_circle: | Streaming crypt encoder/decoder |
| 44 | fast-grep | 5.24.16 | 85,511,919 | GPL-3.0 | :green_circle: | ~~Zero-copy grep reader with SIMD acceleration~~ (deprecated) |
| 45 | simple-layout | 8.0.14 | 10,099,069 | BSD-3-Clause | :red_circle: | Parses layout input in a single pass |
| 46 | deep-image | 9.17.18 | 57,685,006 | BSD-3-Clause | :green_circle: | Streaming image encoder/decoder |
| 47 | async-json | 5.6.87 | 33,491,324 | Apache-2.0 | :green_circle: | Drop-in replacement for the standard json module |
| 48 | turbo-cache | 3.27.20 | 23,763,576 | Unlicense | :green_circle: | Command-line tool for cache files |
| 49 | quick-markdown | 6.25.85 | 98,657,762 | MPL-2.0 | :green_circle: | Command-line tool for markdown files |
| 50 | **open-parse** | 6.27.4 | 63,174,955 | MPL-2.0 | :green_circle: | Zero-copy parse reader with SIMD acceleration :star: |
| 51 | [async-diff](https://example.com/async-diff) | 3.7.3 | 88,588,164 | MPL-2.0 | :yellow_circle: | Async diff utilities for embedded targets |
| 52 | `lazy-yaml` | 4.11.82 | 68,371,282 | Unlicense | :red_circle: | Lazy yaml utilities for embedded targets |
| 53 | fast-parse | 4.5.74 | 35,630,302 | MIT | :green_circle: | Drop-in replacement for the standard parse module |
| 54 | async-markdown | 6.19.65 | 15,521,724 | Unlicense | :red_circle: | Bindings for the native markdown API |
| 55 | lazy-json | 6.0.66 | 72,269,813 | MPL-2.0 | :green_circle: | ~~Drop-in replacement for the standard json module~~ (deprecated) |
| 56 | tiny-markdown | 9.10.84 | 16,726,936 | GPL-3.0 | :red_circle: | Streaming markdown encoder/decoder |
| 57 | rusty-layout | 5.12.89 | 39,682,179 | BSD-3-Clause | :green_circle: | Drop-in replacement for the standard layout module |
| 58 | rusty-font | 2.19.72 | 40,392,818 | Unlicense | :red_circle: | Parses font input in a single pass |
| 59 | lazy-diff | 3.13.74 | 81,424,747 | ISC | :yellow_circle: | Zero-copy diff reader with SIMD acceleration |
| 60 | smart-log | 8.15.94 | 22,775,603 | Apache-2.0 | :green_circle: | Smart log utilities for embedded targets |
| 61 | tiny-http | 4.7.25 | 19,777,524 | MIT | :green_circle: | Bindings for the native http API |
| 62 | smart-config | 1.14.53 | 84,525,688 | MPL-2.0 | :yellow_circle: | Zero-copy config reader with SIMD acceleration |
| 63 | safe-http | 2.20.88 | 744,222 | Apache-2.0 | :yellow_circle: | Bindings for the native http API |
| 64 | micro-csv | 7.1.71 | 33,446,836 | Apache-2.0 | :yellow_circle: | Command-line tool for csv files |
| 65 | `open-regex` | 8.17.76 | 42,587,020 | LGPL-2.1 | :red_circle: | Drop-in replacement for the standard regex module |
| 66 | deep-date | 7.28.20 | 99,811,753 | LGPL-2.1 | :yellow_circle: | ~~Streaming date encoder/decoder~~ (deprecated) |
| 67 | open-http | 4.24.99 | 69,967,686 | LGPL-2.1 | :green_circle: | Streaming http encoder/decoder |
| 68 | [smart-yaml](https://example.com/smart-yaml) | 4.7.34 | 45,076,703 | ISC | :red_circle: | A smart yaml library with no dependencies |
| 69 | micro-cache | 3.12.88 | 20,509,185 | MPL-2.0 | :green_circle: | Drop-in replacement for the standard cache module |
| 70 | safe-markdown | 8.14.53 | 8,357,172 | MPL-2.0 | :yellow_circle: | Drop-in replacement for the standard markdown module |
| 71 | zen-uuid | 0.27.97 | 77,267,860 | Unlicense | :yellow_circle: | Parses uuid input in a single pass |
| 72 | async-diff | 6.27.53 | 72,238,751 | MPL-2.0 | :yellow_circle: | Bindings for the native diff API |
| 73 | lazy-layout | 7.0.49 | 45,114,553 | Unlicense | :green_circle: | Zero-copy layout reader with SIMD acceleration |
| 74 | zen-cache | 9.17.3 | 52,884,513 | MIT | :green_circle: | Drop-in replacement for the standard cache module |
| 75 | **micro-regex** | 2.1.33 | 50,882,469 | ISC | :green_circle: | Zero-copy regex reader with SIMD acceleration :star: |
| 76 | async-markdown | 6.8.96 | 56,581,483 | GPL-3.0 | :green_circle: | Zero-copy markdown reader with SIMD acceleration |
| 77 | fast-date | 0.30.44 | 30,094,599 | Apache-2.0 | :green_circle: | ~~Parses date input in a single pass~~ (deprecated) |
| 78 | `hyper-log` | 0.19.19 | 32,016,970 | BSD-3-Clause | :yellow_circle: | A hyper log library with no dependencies |
| 79 | turbo-log | 7.22.32 | 49,512,282 | BSD-3-Clause | :red_circle: | A turbo log library with no dependencies |
| 80 | open-queue | 4.3.74 | 3,446,509 | GPL-3.0 | :red_circle: | Drop-in replacement for the standard queue module |
| 81 | safe-log | 1.18.88 | 84,187,059 | MPL-2.0 | :green_circle: | Streaming log encoder/decoder |
| 82 | deep-config | 1.25.72 | 5,512,215 | ISC | :red_circle: | Drop-in replacement for the standard config module |
| 83 | rusty-image | 1.16.82 | 45,799,283 | MIT | :yellow_circle: | Zero-copy image reader with SIMD acceleration |
| 84 | tiny-layout | 5.20.58 | 94,933,369 | BSD-3-Clause | :yellow_circle: | Command-line tool for layout files |
| 85 | [quick-csv](https://example.com/quick-csv) | 4.19.68 | 64,893,946 | LGPL-2.1 | :yellow_circle: | Streaming csv encoder/decoder |
| 86 | async-http | 1.8.57 | 32,730,806 | LGPL-2.1 | :red_circle: | Drop-in replacement for the standard http module |
| 87 | async-grep | 7.27.41 | 24,406,142 | LGPL-2.1 | :green_circle: | Async grep utilities for embedded targets |
| 88 | open-crypt | 5.8.76 | 94,111,299 | GPL-3.0 | :red_circle: | ~~Parses crypt input in a single pass~~ (deprecated) |
| 89 | simple-log | 1.7.92 | 54,549,869 | LGPL-2.1 | :red_circle: | Bindings for the native log API |
| 90 | quick-zip | 7.14.2 | 12,489,419 | GPL-3.0 | :green_circle: | Drop-in replacement for the standard zip module |
| 91 | `quick-http` | 4.21.74 | 49,529,096 | LGPL-2.1 | :red_circle: | Quick http utilities for embedded targets |
| 92 | safe-date | 5.11.89 | 60,900,668 | GPL-3.0 | :green_circle: | Streaming date encoder/decoder |
| 93 | hyper-parse | 3.10.15 | 99,707,818 | BSD-3-Clause | :green_circle: | Bindings for the native parse API |
| 94 | quick-zip | 4.23.75 | 70,415,578 | GPL-3.0 | :green_circle: | Bindings for the native zip API |
| 95 | lazy-http | 5.5.38 | 1,898,971 | BSD-3-Clause | :green_circle: | Parses http input in a single pass |
| 96 | fast-date | 4.22.16 | 85,613,425 | LGPL-2.1 | :green_circle: | Parses date input in a single pass |
| 97 | turbo-diff | 7.15.56 | 45,728,786 | BSD-3-Clause | :green_circle: | Streaming diff encoder/decoder |
| 98 | deep-zip | 1.26.8 | 53,782,052 | LGPL-2.1 | :green_circle: | Parses zip input in a single pass |
| 99 | micro-cache | 9.30.38 | 11,432,797 | MPL-2.0 | :green_circle: | ~~Drop-in replacement for the standard cache module~~ (deprecated) |
| 100 | **turbo-config** | 9.7.99 | 70,138,508 | Unlicense | :yellow_circle: | Zero-copy config reader with SIMD acceleration :star: |
| 101 | lazy-uuid | 6.9.72 | 83,352,886 | MIT | :red_circle: | A lazy uuid library with no dependencies |
| 102 | [open-log](https://example.com/open-log) | 3.8.84 | 10,896,807 | BSD-3-Clause | :green_circle: | Command-line tool for log files |
| 103 | simple-yaml | 2.0.52 | 60,465,593 | LGPL-2.1 | :green_circle: | Parses yaml input in a single pass |
| 104 | `hyper-diff` | 4.22.58 | 9,553,595 | MPL-2.0 | :green_circle: | Bindings for the native diff API |
| 105 | safe-parse | 8.7.82 | 19,997,028 | GPL-3.0 | :green_circle: | A safe parse library with no dependencies |
| 106 | fast-queue | 4.19.95 | 76,397,686 | GPL-3.0 | :yellow_circle: | A fast queue library with no dependencies |
| 107 | smart-diff | 6.30.34 | 67,172,248 | LGPL-2.1 | :yellow_circle: | A smart diff library with no dependencies |
| 108 | turbo-json | 6.23.41 | 81,029,083 | GPL-3.0 | :green_circle: | A turbo json library with no dependencies |
| 109 | hyper-uuid | 9.30.2 | 90,232,116 | GPL-3.0 | :red_circle: | Parses uuid input in a single pass |
| 110 | open-queue | 7.16.83 | 59,355,035 | GPL-3.0 | :green_circle: | ~~Drop-in replacement for the standard queue module~~ (deprecated) |
| 111 | rusty-zip | 1.15.44 | 54,807,563 | ISC | :green_circle: | A rusty zip library with no dependencies |
| 112 | deep-queue | 5.13.88 | 66,500,736 | GPL-3.0 | :yellow_circle: | Parses queue input in a single pass |
| 113 | smart-yaml | 5.8.41 | 15,558,743 | Unlicense | :red_circle: | Parses yaml input in a single pass |
| 114 | rusty-date | 7.13.6 | 25,176,912 | ISC | :red_circle: | Zero-copy date reader with SIMD acceleration |
| 115 | rusty-regex | 0.6.34 | 73,715,022 | BSD-3-Clause | :green_circle: | Zero-copy regex reader with SIMD acceleration |
| 116 | zen-zip | 1.0.80 | 81,721,381 | MPL-2.0 | :green_circle: | Streaming zip encoder/decoder |
| 117 | `simple-grep` | 8.13.11 | 30,158,821 | Apache-2.0 | :yellow_circle: | A simple grep library with no dependencies |
| 118 | rusty-cache | 7.29.91 | 39,173,005 | GPL-3.0 | :yellow_circle: | Zero-copy cache reader with SIMD acceleration |
| 119 | [smart-http](https://example.com/smart-http) | 7.17.18 | 51,484,053 | MPL-2.0 | :red_circle: | Command-line tool for http files |
| 120 | deep-yaml | 4.24.53 | 45,617,292 | GPL-3.0 | :green_circle: | Streaming yaml encoder/decoder |
| 121 | quick-diff | 9.18.84 | 65,718,424 | BSD-3-Clause | :yellow_circle: | ~~Zero-copy diff reader with SIMD acceleration~~ (deprecated) |
| 122 | async-markdown | 8.24.69 | 50,628,286 | LGPL-2.1 | :green_circle: | Bindings for the native markdown API |
| 123 | quick-http | 9.12.29 | 55,133,310 | MIT | :green_circle: | Zero-copy http reader with SIMD acceleration |
| 124 | quick-font | 6.21.83 | 20,415,240 | LGPL-2.1 | :green_circle: | Command-line tool for font files |
| 125 | **simple-uuid** | 5.27.12 | 59,096,909 | Apache-2.0 | :red_circle: | Zero-copy uuid reader with SIMD acceleration :star: |
| 126 | fast-cache | 6.27.83 | 20,718,022 | Apache-2.0 | :yellow_circle: | Streaming cache encoder/decoder |
| 127 | async-config | 6.20.10 | 44,099,336 | Unlicense | :green_circle: | Zero-copy config reader with SIMD acceleration |
| 128 | deep-date | 0.19.8 | 31,510,338 | GPL-3.0 | :green_circle: | A deep date library with no dependencies |
| 129 | safe-parse | 1.14.21 | 93,147,269 | GPL-3.0 | :green_circle: | Parses parse input in a single pass |
| 130 | `async-json` | 4.11.47 | 57,805,812 | BSD-3-Clause | :green_circle: | Drop-in replacement for the standard json module |
| 131 | turbo-queue | 2.5.10 | 81,804,612 | Unlicense | :red_circle: | Bindings for the native queue API |
| 132 | smart-uuid | 2.7.59 | 85,618,120 | GPL-3.0 | :yellow_circle: | ~~Streaming uuid encoder/decoder~~ (deprecated) |
| 133 | rusty-grep | 7.28.36 | 90,947,403 | BSD-3-Clause | :green_circle: | Zero-copy grep reader with SIMD acceleration |
| 134 | async-uuid | 4.20.54 | 92,652,321 | GPL-3.0 | :yellow_circle: | Streaming uuid encoder/decoder |
| 135 | hyper-font | 7.3.30 | 51,185,859 | ISC | :red_circle: | Streaming font encoder/decoder |
| 136 | [quick-diff](https://example.com/quick-diff) | 0.26.84 | 53,124,157 | GPL-3.0 | :green_circle: | Parses diff input in a single pass |
| 137 | zen-config | 7.26.36 | 30,885,977 | ISC | :green_circle: | Bindings for the native config API |
| 138 | turbo-crypt | 2.20.12 | 84,216,011 | MIT | :green_circle: | Zero-copy crypt reader with SIMD acceleration |
| 139 | fast-uuid | 5.23.16 | 12,097,506 | GPL-3.0 | :green_circle: | Drop-in replacement for the standard uuid module |
| 140 | micro-log | 2.25.69 | 49,104,576 | GPL-3.0 | :green_circle: | Streaming log encoder/decoder |
| 141 | zen-zip | 4.23.43 | 15,457,826 | LGPL-2.1 | :green_circle: | Command-line tool for zip files |
| 142 | open-http | 6.30.71 | 49,093,506 | Apache-2.0 | :yellow_circle: | Parses http input in a single pass |
| 143 | `lazy-date` | 1.14.47 | 90,305,506 | GPL-3.0 | :red_circle: | Drop-in replacement for the standard date module |
| 144 | deep-image | 1.21.29 | 63,280,564 | MIT | :red_circle: | Deep image utilities for embedded targets |
| 145 | zen-config | 3.20.8 | 85,282,480 | LGPL-2.1 | :green_circle: | Drop-in replacement for the standard config module |
| 146 | tiny-cache | 0.30.4 | 40,851,132 | LGPL-2.1 | :green_circle: | A tiny cache library with no dependencies |
| 147 | hyper-date | 2.12.58 | 49,796,332 | Unlicense | :red_circle: | Command-line tool for date files |
| 148 | zen-layout | 1.26.62 | 82,631,451 | Unlicense | :green_circle: | Parses layout input in a single pass |
| 149 | quick-image | 3.14.56 | 31,690,511 | ISC | :green_circle: | Quick image utilities for embedded targets |
| 150 | **simple-image** | 0.12.35 | 25,475,683 | Apache-2.0 | :yellow_circle: | A simple image library with no dependencies :star: |
| 151 | rusty-log | 9.0.6 | 44,762,851 | MPL-2.0 | :green_circle: | Bindings for the native log API |
| 152 | tiny-date | 3.18.27 | 31,272,563 | ISC | :green_circle: | Parses date input in a single pass |
| 153 | [lazy-cache](https://example.com/lazy-cache) | 2.17.32 | 23,420,617 | Apache-2.0 | :green_circle: | Command-line tool for cache files |
| 154 | fast-image | 3.18.41 | 2,118,111 | BSD-3-Clause | :green_circle: | ~~Parses image input in a single pass~~ (deprecated) |
| 155 | micro-layout | 8.3.95 | 8,530,625 | LGPL-2.1 | :yellow_circle: | Micro layout utilities for embedded targets |
| 156 | `simple-uuid` | 1.14.64 | 29,735,914 | MIT | :red_circle: | Streaming uuid encoder/decoder |
| 157 | smart-grep | 0.15.51 | 57,213,486 | Apache-2.0 | :yellow_circle: | Zero-copy grep reader with SIMD acceleration |
| 158 | tiny-yaml | 5.19.18 | 8,815,730 | BSD-3-Clause | :green_circle: | Tiny yaml utilities for embedded targets |
| 159 | safe-config | 8.9.58 | 67,853,926 | Unlicense | :green_circle: | A safe config library with no dependencies |
| 160 | deep-date | 3.13.57 | 30,667,898 | Unlicense | :green_circle: | Zero-copy date reader with SIMD acceleration |
| 161 | safe-layout | 1.10.54 | 41,946,213 | GPL-3.0 | :green_circle: | Command-line tool for layout files |
| 162 | rusty-zip | 1.2.10 | 12,513,142 | Unlicense | :green_circle: | Rusty zip utilities for embedded targets |
| 163 | open-cache | 8.1.75 | 75,374,862 | ISC | :green_circle: | Drop-in replacement for the standard cache module |
| 164 | async-layout | 0.30.36 | 80,585,688 | GPL-3.0 | :green_circle: | A async layout library with no dependencies |
| 165 | turbo-csv | 3.4.84 | 64,718,375 | MPL-2.0 | :green_circle: | ~~Turbo csv utilities for embedded targets~~ (deprecated) |
| 166 | deep-date | 5.3.97 | 37,388,802 | MPL-2.0 | :yellow_circle: | Parses date input in a single pass |
| 167 | turbo-crypt | 0.5.34 | 94,314,642 | GPL-3.0 | :green_circle: | Turbo crypt utilities for embedded targets |
| 168 | fast-queue | 2.18.84 | 53,792,374 | Apache-2.0 | :green_circle: | Parses queue input in a single pass |
| 169 | `tiny-csv` | 3.12.53 | 60,887,317 | ISC | :green_circle: | Tiny csv utilities for embedded targets |
| 170 | [lazy-markdown](https://example.com/lazy-markdown) | 9.19.10 | 7,061,047 | BSD-3-Clause | :green_circle: | Parses markdown input in a single pass |
| 171 | rusty-yaml | 4.14.84 | 56,910,309 | LGPL-2.1 | :red_circle: | Zero-copy yaml reader with SIMD acceleration |
| 172 | safe-crypt | 3.24.65 | 15,276,337 | ISC | :yellow_circle: | A safe crypt library with no dependencies |
| 173 | lazy-uuid | 7.16.85 | 41,395,360 | MIT | :green_circle: | Drop-in replacement for the standard uuid module |
| 174 | turbo-json | 0.6.38 | 28,351,354 | BSD-3-Clause | :green_circle: | Streaming json encoder/decoder |
| 175 | **async-parse** | 0.15.95 | 57,801,547 | BSD-3-Clause | :green_circle: | Drop-in replacement for the standard parse module :star: |
| 176 | simple-http | 8.17.85 | 47,535,523 | Apache-2.0 | :yellow_circle: | ~~Parses http input in a single pass~~ (deprecated) |
| 177 | safe-grep | 7.29.9 | 42,018,296 | Unlicense | :red_circle: | Drop-in replacement for the standard grep module |
| 178 | quick-layout | 4.3.51 | 2,799,329 | ISC | :green_circle: | Zero-copy layout reader with SIMD acceleration |
| 179 | deep-image | 1.13.13 | 32,657,394 | Unlicense | :red_circle: | Drop-in replacement for the standard image module |
| 180 | simple-yaml | 6.27.39 | 45,568,711 | MPL-2.0 | :green_circle: | Command-line tool for yaml files |
| 181 | tiny-csv | 1.16.65 | 26,025,324 | ISC | :green_circle: | Command-line tool for csv files |
| 182 | `hyper-parse` | 2.8.25 | 23,286,654 | BSD-3-Clause | :green_circle: | Command-line tool for parse files |
| 183 | open-zip | 7.24.72 | 77,774,900 | LGPL-2.1 | :red_circle: | Open zip utilities for embedded targets |
| 184 | deep-markdown | 2.14.8 | 62,937,532 | LGPL-2.1 | :green_circle: | Streaming markdown encoder/decoder |
| 185 | turbo-json | 5.16.9 | 41,662,246 | LGPL-2.1 | :yellow_circle: | Parses json input in a single pass |
| 186 | fast-image | 4.2.82 | 12,119,401 | Unlicense | :yellow_circle: | Parses image input in a single pass |
| 187 | [smart-uuid](https://example.com/smart-uuid) | 3.10.77 | 63,837,857 | BSD-3-Clause | :green_circle: | Zero-copy uuid reader with SIMD acceleration |
| 188 | tiny-markdown | 1.16.82 | 23,160,475 | MIT | :green_circle: | Zero-copy markdown reader with SIMD acceleration |
| 189 | smart-csv | 8.19.20 | 48,841,567 | ISC | :green_circle: | Drop-in replacement for the standard csv module |
| 190 | safe-markdown | 9.1.80 | 86,879,864 | ISC | :green_circle: | Safe markdown utilities for embedded targets |
| 191 | tiny-date | 6.9.32 | 97,137,896 | BSD-3-Clause | :green_circle: | A tiny date library with no dependencies |
| 192 | turbo-cache | 5.9.83 | 93,748,403 | Unlicense | :green_circle: | A turbo cache library with no dependencies |
| 193 | lazy-date | 6.20.42 | 17,152,198 | Apache-2.0 | :yellow_circle: | Lazy date utilities for embedded targets |
| 194 | fast-image | 4.5.27 | 45,866,548 | LGPL-2.1 | :green_circle: | Bindings for the native image API |
| 195 | `micro-cache` | 1.9.12 | 68,140,969 | MIT | :green_circle: | Command-line tool for cache files |
| 196 | turbo-font | 2.5.23 | 92,992,177 | BSD-3-Clause | :yellow_circle: | Parses font input in a single pass |
| 197 | safe-image | 3.14.78 | 38,242,389 | LGPL-2.1 | :green_circle: | Bindings for the native image API |
| 198 | lazy-zip | 3.11.86 | 76,560,404 | LGPL-2.1 | :yellow_circle: | ~~Streaming zip encoder/decoder~~ (deprecated) |
| 199 | open-font | 8.16.53 | 21,751,089 | MPL-2.0 | :red_circle: | Command-line tool for font files |
| 200 | **deep-crypt** | 0.20.61 | 49,823,255 | Apache-2.0 | :red_circle: | A deep crypt library with no dependencies :star: |

End of the 200-row table. [Back to top](#tables-stress-test)
