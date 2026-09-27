# BlitzMD — fast Markdown viewer for Total Commander

**English** | [Русский](README_RU.md)

**BlitzMD** is a free, open-source **Markdown viewer plugin for [Total Commander](https://www.ghisler.com)**
(a Lister plugin, WLX). Press **F3** on a `.md` file, or open Quick View with **Ctrl+Q**, and the
file shows instantly, rendered the way GitHub renders it: tables, task lists, footnotes, alerts,
math formulas, Mermaid diagrams, syntax-highlighted code, emoji and dark mode. BlitzMD draws the
document itself with Direct2D and DirectWrite: no web browser engine, no WebView2, no .NET, nothing
else to install.

| Light theme | Dark theme |
|---|---|
| ![BlitzMD's overview in BlitzMD, light theme: logo, badges, a table, a task list and a tip](docs/screenshot-light.png) | ![A Mermaid flowchart, a formula and GitHub alerts in BlitzMD, dark theme](docs/screenshot-dark.png) |

![Syntax-highlighted code blocks with line numbers in BlitzMD](docs/screenshot-code.png)

## At a glance

| | |
|---|---|
| What it is | Lister plugin (WLX) for Total Commander that views Markdown files |
| Opens | `.md`, `.markdown`, `.mdown`, `.mkd`, `.mkdn`, `.mdwn`, `.mdtxt`, `.mdtext`, `.rmd` |
| Where | Lister (F3), Quick View panel (Ctrl+Q), thumbnail view, printing |
| Renders | GitHub Flavored Markdown (GFM 0.29) and GitHub's extensions: alerts, footnotes, math, Mermaid, emoji, the HTML READMEs use |
| Total Commander | 64-bit (`blitzmd.wlx64`) and 32-bit (`blitzmd.wlx`), both in one package |
| Windows | Windows 10 version 1703 or later, Windows 11 |
| Engine | Native C++: [md4c](https://github.com/mity/md4c) parser, Direct2D + DirectWrite rendering |
| Dependencies | None: only system libraries |
| Interface | English and Russian (follows the Windows display language) |
| License | MIT, free |

## Installation

1. Download `blitzmd.zip` from the [latest release](../../releases/latest).
2. Open the archive in Total Commander (Enter on it). Total Commander offers to install the plugin:
   confirm.
3. Press F3 or Ctrl+Q on any `.md` file.

Manual installation: unpack the archive to any folder and add `blitzmd.wlx64` (or `blitzmd.wlx`
for 32-bit Total Commander) under *Configuration → Options → Plugins → Lister plugins (WLX)*.

## Features

### GitHub Flavored Markdown

Everything in the GFM 0.29 specification: ATX and Setext headings, paragraphs, block quotes,
lists (bulleted, numbered from any number, nested, tight and loose), task lists `- [x]`, fenced
and indented code blocks, tables with column alignment, strikethrough, links of every kind
(inline, reference, autolinks, extended `www.` / `https://` / e-mail autolinks), images, HTML
entities, escapes and hard line breaks. Checked against the official specification tests (see
[Testing](#testing)).

### GitHub extensions

- **Alerts**: `> [!NOTE]`, `[!TIP]`, `[!IMPORTANT]`, `[!WARNING]`, `[!CAUTION]`, with their
  Octicons icons.
- **Footnotes** `[^1]`, including footnotes of several paragraphs with code and lists, with back
  links ↩.
- **Math**: `$…$`, `` $`…`$ ``, `$$…$$` and ```` ```math ```` blocks, typeset by TeX's rules with
  Cambria Math (its OpenType MATH table), as MathJax does on GitHub: fractions, roots, sub- and
  superscripts, limits on `\sum` / `\int` / `\lim`, growing delimiters `\left…\right`, matrices,
  `cases` / `aligned`, accents, over- and underbraces, `\text`, `\mathbb` / `\mathcal` /
  `\mathbf`, spacing, `\color`, `\boxed`. Unknown commands show as they are (in red, like
  MathJax); copying and searching use the TeX source.
- **Mermaid diagrams** (```` ```mermaid ```` blocks), laid out and drawn by the plugin itself,
  without a browser or JavaScript, in the colors of GitHub's light and dark Mermaid themes:
  - flowchart / graph: every node shape and link kind (ends, thickness, dotted, length, labels),
    subgraphs (with their own direction when no link crosses them), `classDef` / `class` / `:::` /
    `style` / `linkStyle`, Markdown and HTML in labels; layered (Sugiyama) layout;
  - sequenceDiagram: participants and actors, every arrow, activations, notes, `autonumber`,
    `loop` / `alt` / `opt` / `par` / `critical` / `break` / `rect`, `box`, `create` / `destroy`;
  - classDiagram, stateDiagram, erDiagram, requirementDiagram, C4 (Context, Container,
    Component, Dynamic, Deployment);
  - pie, journey, gantt, timeline, mindmap, quadrantChart, xychart-beta, sankey-beta, radar-beta,
    treemap-beta, packet-beta, gitGraph, kanban, block-beta, architecture-beta;
  - treeView-beta, ishikawa-beta, cynefin-beta, venn-beta, wardley-beta, eventmodeling,
    swimlane-beta, railroad-beta (and its EBNF, ABNF and PEG forms), info.

  Diagrams zoom and open in the whole window like pictures; copying gives their source, and
  search highlights text on their labels. A diagram type that isn't supported, or a diagram with
  an error, stays a code block with a note saying why. Mermaid themes (`%%{init: {theme: ...}}%%`)
  are not applied: the colors are always GitHub's, light or dark like the document. Limits are
  Mermaid's: 50 000 characters and 500 links in a flowchart (2000 links in the other types).
- **Emoji shortcodes** `:rocket:` → 🚀 (the whole GitHub gemoji set, 1900+ codes), color emoji.
- **Country flags** 🇷🇺 🇺🇸 🇯🇵 and 🏴󠁧󠁢󠁥󠁮󠁧󠁿 🏴󠁧󠁢󠁳󠁣󠁴󠁿 🏴󠁧󠁢󠁷󠁬󠁳󠁿: Windows' emoji font has none (it shows two letters), so BlitzMD draws
  them from an embedded Twemoji set (262 flags).
- **Heading anchors** as on GitHub (`#installation`, duplicates `-1`, `-2`), and links to them.
- **Syntax highlighting** in code blocks: about 70 languages (250+ names and extensions: C/C++,
  C#, Java, Kotlin, Swift, Go, Rust, JavaScript/TypeScript, Python, Ruby, PHP, Lua, SQL,
  Pascal/Delphi, Bash, PowerShell, batch files, HTML/XML, CSS, JSON, YAML, TOML, INI, diff,
  Dockerfile, Makefile, CMake, assembly, LaTeX and more), in GitHub's colors.
- **Theme-specific images**: `#gh-dark-mode-only`, `<picture><source media>`.
- **YAML front matter** (Jekyll, Hugo, Docusaurus) shows as a key–value table.

### HTML used in READMEs

`<div align="center">`, `<p align>`, `<h1 align>`, `<img width height align>`, `<picture>`,
`<details>` / `<summary>` (open on click), `<kbd>`, `<sup>`, `<sub>`, `<mark>`, `<ins>`, `<del>`,
`<br>`, `<hr>`, `<a name>`, HTML tables (`colspan`), `<dl>`, comments. `<script>`, `<style>`,
`<iframe>` and the like are neither run nor shown.

### Images

PNG, JPEG, GIF, BMP, ICO, TIFF, WebP (through Windows Imaging Component) and SVG, including text
and logos, so shields.io and GitHub Actions badges look as on GitHub. `data:` URIs, relative and
absolute paths, spaces and non-Latin letters in file names. Animated GIFs play while they are on
screen. Pictures from the internet load in the background and are cached (can be turned off);
for safety, they are never requested from this computer or the local network.

### Viewing

- The theme follows Total Commander's (including its dark mode, TC 10 and later), or is set to
  light or dark.
- Smooth scrolling, a scroll bar that marks search hits.
- Mouse selection (double click: a word, triple click: a paragraph), copying (Ctrl+C, Total
  Commander's menu).
- Search with Lister's dialog (F7, F3 / Shift+F3): match case, whole words, backwards; every
  match is highlighted, collapsed `<details>` open.
- Links: web links open in the browser, `#anchors` scroll the document, links to other `.md`
  files open in the same window (back: Backspace or Alt+←), other files open in their associated
  program, after a confirmation for programs and scripts. The link's address shows at the bottom
  of the window.
- Wide tables and long code lines (with wrapping off) scroll sideways (Shift+wheel); code blocks
  have a Copy button and line numbers (not copied).
- Auto-reload: when an editor saves the file, the view reads it again and stays at the same place.
- Zoom: Ctrl+wheel, Ctrl+`+`, Ctrl+`-`, Ctrl+0 (remembered).
- Pictures and diagrams zoom inside their frame (Ctrl+wheel over them, drag, double click to
  reset) or open in the whole window.
- Percent scrolled in Lister's title; going to a percent from Total Commander's menu.
- Printing (Lister's *File → Print*): page breaks never split lines or table rows.
- Thumbnails for Total Commander's thumbnail view.
- Context menu: table of contents (go to any heading), copy, link or picture address, copy code
  block, theme, code wrapping, line numbers, text width, pictures from the internet, reload (F5),
  auto-reload, open in editor, settings.
- Settings window: theme, fonts, size, zoom, text width, code wrapping and line numbers,
  highlighting, math, emoji codes, pictures from the internet, auto-reload, smooth scrolling,
  GPU rendering. Changes show at once in every open viewer; OK keeps them, Cancel restores.
- Text encodings: UTF-8 (with or without BOM), UTF-16 LE/BE (with or without BOM), ANSI code
  pages (such as Windows-1251).

## Performance

BlitzMD lays out exactly only the part of the document on screen and estimates the rest, refining
it in the background, so even a document of many megabytes opens in a fraction of a second.
Pictures decode in background threads, and their memory is bounded (192 MB, 96 MB in the 32-bit
version): pictures not shown for a while are dropped and decoded again when they come back.

Measured on a laptop (AMD Ryzen 9 5900HX, Windows 11), from Total Commander's call to the first
frame on screen:

| Document | Opens in | Memory |
|---|---|---|
| A GitHub-style README with badges and pictures (`samples/gfm/02-github-readme.md`) | 40 ms | +22 MB |
| 2.4 MB of Markdown, 18 000 paragraphs (`samples/gfm/09-large-document.md`) | 0.12 s | +51 MB |

## Keyboard and mouse

| Key / action | What it does |
|---|---|
| ↑ ↓, PgUp PgDn, Space / Shift+Space, Home End | Scroll |
| Wheel / Shift+wheel | Scroll / scroll a wide table or code block sideways |
| Ctrl+wheel, Ctrl+`+`, Ctrl+`-`, Ctrl+0 | Zoom |
| Ctrl+wheel over a picture, drag, double click | Zoom the picture in its frame, move it, reset |
| ⛶ button on a picture or diagram | Whole window: wheel zooms, `+` `-` `0`, arrows; Esc goes back |
| Ctrl+A, Ctrl+C | Select all, copy |
| Click a link | Open the link / go to the anchor / open the `.md` file |
| Backspace, Alt+← | Back after following a link |
| Click a `<summary>` | Open / close its `<details>` |
| F5 | Read the file again |
| F7, F3, Shift+F3 | Search (Lister's dialog) |
| N, P, Esc, 1–7 … | Lister's own keys (handled by Total Commander) |

## Settings

Change them in the settings window (context menu → *Settings…*), some straight from the context
menu. They are stored in `blitzmd.ini` next to `wincmd.ini` (section `[blitzmd]`); the file is
created on the first change, and every key is optional.

| Key | Default | Meaning |
|---|---|---|
| `Theme` | `0` | `0`: as Total Commander (or Windows), `1`: light, `2`: dark |
| `Font` | `Segoe UI` | Text font |
| `MonoFont` | *(auto)* | Monospaced font; Cascadia Mono if installed, else Consolas |
| `FontSize` | `16` | Base font size (as GitHub's 16 px) |
| `Zoom` | `100` | Zoom in percent (30–400) |
| `MaxWidth` | `900` | Maximum text width; `0`: the whole window |
| `WrapCode` | `-1` | Wrap long code lines: `-1` as Lister's *Wrap text*, `0` / `1` off / on |
| `LineNumbers` | `1` | Line numbers in code blocks of two lines or more |
| `AutoReload` | `1` | Read the file again when it changes on disk |
| `RemoteImages` | `1` | Load pictures from the internet (http/https) |
| `SmoothScroll` | `1` | Smooth scrolling |
| `EmojiShortcodes` | `1` | Replace `:code:` with emoji |
| `Math` | `1` | Math formulas `$…$` |
| `Mermaid` | `1` | Mermaid diagrams (`0`: show their source) |
| `SyntaxHighlight` | `1` | Syntax highlighting |
| `HardwareRendering` | `0` | `1`: draw with the GPU (for very large windows, 4K); software Direct2D opens faster and uses less memory |

## FAQ

**How do I view Markdown files in Total Commander?**
Install BlitzMD (see [Installation](#installation)), then press F3 on a `.md` file, or Ctrl+Q to
show it in the other panel.

**Does BlitzMD need a browser, Internet Explorer, WebView2 or .NET?**
No. It parses Markdown with md4c and draws the document with Windows' own Direct2D and
DirectWrite. The plugin is a single DLL with nothing else to install.

**Does it show Markdown like GitHub does?**
Yes, that is its aim: GitHub's typography and colors (light and dark), GFM tables and task
lists, alerts, footnotes, math, Mermaid diagrams, emoji, and the HTML that READMEs use.

**Does it work with 32-bit Total Commander?**
Yes. The package contains `blitzmd.wlx` (32-bit) and `blitzmd.wlx64` (64-bit); Total Commander
installs the right one.

**Does it support dark mode?**
Yes. By default the theme follows Total Commander's dark mode (or Windows'); it can also be set to
light or dark.

**Can it render Mermaid diagrams and LaTeX math?**
Yes, both are drawn by the plugin itself, without JavaScript: 30 Mermaid diagram types, and TeX
math typeset with Cambria Math.

**Is it safe to open untrusted Markdown files?**
Scripts in HTML are never run. Links to programs or scripts, and links to other computers'
shares, open only after a confirmation. Pictures from the internet are never requested from this
computer or the local network, and their download can be turned off.

**Does it edit Markdown?**
No, BlitzMD is a viewer. *Open in editor* in its context menu opens the file in the program
Windows uses to edit `.md` files, and F4 in Total Commander opens it in Total Commander's editor.

**Is BlitzMD free?**
Yes, it is open source under the MIT license.

## Building from source

Visual Studio 2022 or 2026 with the C++ workload, and CMake (comes with Visual Studio). Everything
else is in the repository (md4c in `third_party`); no network access is needed to build.

```powershell
# x64 Release -> out\build\x64-release\blitzmd.wlx64
powershell -File scripts\build.ps1

# x64 + x86, packaged as dist\blitzmd.zip (installs from Total Commander)
powershell -File scripts\build.ps1 -Arch all -Package
```

The project folder can also be opened in Visual Studio (CMake presets `x64-release`,
`x86-release`).

## Testing

The whole test suite (CI runs the same):

```powershell
powershell -File scripts\test.ps1                                          # x64
powershell -File scripts\test.ps1 -Build out\build\x86-release -Quick     # x86, less fuzzing
```

Single checks use `mdview_test.exe`, built with the plugin:

```powershell
$t = "out\build\x64-release\mdview_test.exe"
& $t spec tests\spec\gfm-spec.txt           # GFM 0.29 specification tests (672 examples)
& $t spec tests\spec\commonmark-spec.txt    # CommonMark 0.31.2
& $t render samples\gfm\02-github-readme.md out.png --width 900 [--dark] [--zoom 1.5]
& $t pages  samples\gfm\05-tables.md page   # pages as printed
& $t bench  samples\gfm\09-large-document.md
& $t fuzz   2000                            # random documents: parse + layout + drawing
& $t plugin out\build\x64-release\blitzmd.wlx64 README.md shot.png "search text"  # the DLL, as TC drives it
& $t settings out\build\x64-release\blitzmd.wlx64 README.md out\tmp\set\s        # settings window
& $t zoom out\build\x64-release\blitzmd.wlx64 tests\zoom.md out\tmp\zoom\z       # zooming pictures
& $t mermaid samples\gfm\20-mermaid-flowchart.md   # diagrams: parsing, layout, hostile input, fuzzing
& $t math samples\gfm\18-math.md                   # math: TeX parsing, typesetting, fuzzing
& $t mmd diagram.mmd out.png [--dark]              # one Mermaid diagram to PNG
out\build\x64-release\highlight_test.exe -q   # syntax highlighting: 1.4 million checks, fuzzing
```

GFM specification results: all 604 examples not about raw HTML match the reference output
(examples with HTML are interpreted on purpose, not shown as source). Runs under AddressSanitizer
(`-DMDVIEW_ASAN=ON`) and the fuzzers are clean.

`scripts\screenshots.ps1` takes the README screenshots again from the documents in
`docs/screenshots/`. `samples/gfm/` holds documents for checking by eye: a tour of the whole specification, a
GitHub-style README, alerts, footnotes and math, 45+ programming languages, tables, lists,
typography in 15+ languages (right-to-left included), edge cases and a 2.5 MB document.

## Continuous integration and releases

GitHub Actions (`.github/workflows/ci.yml`) builds x64 and x86 on every push and pull request,
runs `scripts\test.ps1` and attaches `blitzmd.zip` to the run.

A release:

1. Set the version in `CMakeLists.txt` (`project(blitzmd VERSION x.y.z ...)`); it goes into the
   DLL's version resource and into `pluginst.inf` when packaging.
2. Commit, tag and push the tag: `git tag vx.y.z`, `git push origin vx.y.z`.
3. CI checks that the tag matches the version, builds, runs the tests and publishes a GitHub
   Release with `blitzmd.zip`, which Total Commander installs by itself.

## Architecture

```
src/plugin.cpp        WLX exports: ListLoad(W), ListLoadNext(W), ListSearchText(W),
                      ListSendCommand, ListPrint(W), ListGetPreviewBitmap(W), ...
src/core/parser.*     md4c → block tree; GitHub's HTML subset, emoji, anchors, footnotes
src/core/html.*       HTML tokenizer
src/core/texmath.*    TeX formulas into atom trees
src/core/mathlayout.* math typesetting by TeX's rules with the OpenType MATH table (Cambria Math)
src/core/flags.*      flag emoji from the embedded Twemoji set (src/twemoji-flags.bin, made by
                      scripts/gen_flags.py)
src/core/highlight.*  syntax highlighting
src/core/layout.*     layout: DirectWrite, exact near the view, estimated elsewhere
src/core/render.*     Direct2D drawing (GitHub Primer themes)
src/core/images.*     WIC, SVG, background http(s) downloads with a cache
src/core/zoom.*       zooming pictures and diagrams in their frame and in the whole window
src/core/mermaid*     Mermaid diagrams: parsers (a file per type), scene building
src/core/graphlayout.* layered graph layout (Sugiyama) with clusters
src/core/diagram.*    diagram scene: shapes, paths, DirectWrite text; drawing by theme
src/core/viewer.*     the Lister window: scrolling, selection, links, search, menus
src/core/settings*    settings (blitzmd.ini) and the settings window
src/core/print.*      printing
third_party/md4c      md4c 0.6.0 with a few GitHub compatibility changes
```

## License

MIT (see [LICENSE](LICENSE)). Uses [md4c](https://github.com/mity/md4c) (MIT),
[gemoji](https://github.com/github/gemoji) data (MIT) and [Octicons](https://github.com/primer/octicons)
icons (MIT).

Flag images: [Twemoji](https://github.com/jdecked/twemoji) © Twitter, Inc. and other contributors,
[CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/); coordinates in the embedded set are
rounded to hundredths.
