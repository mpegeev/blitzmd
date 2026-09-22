# Realistic GitHub README

This file imitates the README of a popular open-source project (a fictional `fastgrep` CLI tool) and uses the HTML-heavy idioms real READMEs rely on. It covers a centered header built from `<div align="center">`, `<p align>` and `<h1 align>`, a logo `<img width>`, rows of shields.io badges (remote SVGs, plus local SVG copies for offline use), `<picture>` with dark/light `<source>`, emoji shortcodes, `<details>`/`<summary>`, an all-contributors `<table>`, `<kbd>`, `<sup>`/`<sub>`, `<br>`, `<hr>`, hidden HTML comments, footnotes and GitHub alerts. Everything below the rule is the README itself, and it should look like github.com's rendering of a project page.

---

<!--
  02-github-readme.md: a realistic open-source README (fictional "fastgrep" CLI).
  Tests the HTML-heavy idioms real READMEs use: centered <div>/<p align>, logo <img width>,
  badge rows (shields.io remote SVGs plus local SVG fallbacks), <picture> with dark/light
  <source>, emoji shortcodes, <details>/<summary>, all-contributors <table>, <kbd>, <sup>/<sub>,
  <br>, <hr>, HTML comments, footnotes and GitHub alerts. This comment must NOT be visible.
-->

<div align="center">

<img src="../logo.png" width="120" alt="fastgrep logo">

<h1 align="center">fastgrep</h1>

<p align="center">
  <b>A blazingly fast, Unicode-aware, recursive search tool that respects your <code>.gitignore</code>.</b><br>
  <sub>Written in Rust&nbsp;·&nbsp;Zero config&nbsp;·&nbsp;Works on Windows, Linux and macOS</sub>
</p>

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](#license)
[![GitHub stars](https://img.shields.io/github/stars/mity/md4c?style=social)](https://github.com/mity/md4c/stargazers)
[![CI](https://img.shields.io/github/actions/workflow/status/BurntSushi/ripgrep/ci.yml?branch=master&label=CI&logo=github)](https://github.com/BurntSushi/ripgrep/actions)
[![Crates.io](https://img.shields.io/crates/v/ripgrep.svg?logo=rust)](https://crates.io/crates/ripgrep)
[![Downloads](https://img.shields.io/github/downloads/BurntSushi/ripgrep/total?color=success)](https://github.com/BurntSushi/ripgrep/releases)
[![Platforms](https://img.shields.io/badge/platform-windows%20%7C%20linux%20%7C%20macos-lightgrey)](#installation)
[![Discord](https://img.shields.io/badge/chat-on%20discord-7289da.svg?logo=discord&logoColor=white)](https://discord.com)

<!-- Local copies of two badges, so they render even when offline: -->
<img src="img/badge-build-passing.svg" alt="build: passing">
<img src="img/badge-coverage.svg" alt="coverage: 97%">

<p>
  <a href="#installation">Installation</a> •
  <a href="#usage">Usage</a> •
  <a href="#configuration">Configuration</a> •
  <a href="#benchmarks">Benchmarks</a> •
  <a href="#faq">FAQ</a> •
  <a href="#contributing">Contributing</a>
</p>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="img/gradients.svg">
  <source media="(prefers-color-scheme: light)" srcset="img/photo.jpg">
  <img alt="fastgrep screenshot: a gradient picture in dark mode, a landscape photo in light mode" src="img/photo.jpg" width="480">
</picture>

</div>

<hr>

**fastgrep** (`fg`) is a line-oriented search tool that recursively searches the current directory for a regex pattern. By default it respects `.gitignore`, skips hidden files and directories, and ignores binary files. It is a demo project: *every* number in this README is made up.

> [!NOTE]
> fastgrep 2.0 changed the default regex engine. If you rely on backreferences, pass `--engine=pcre2` or read the [migration guide](#upgrading-from-1x).

## Table of Contents

- [Features](#features)
- [Installation](#installation)
  - [Pre-built binaries](#pre-built-binaries)
  - [Package managers](#package-managers)
  - [Building from source](#building-from-source)
- [Usage](#usage)
  - [Keyboard shortcuts (interactive mode)](#keyboard-shortcuts-interactive-mode)
- [Configuration](#configuration)
- [Benchmarks](#benchmarks)
- [Upgrading from 1.x](#upgrading-from-1x)
- [FAQ](#faq)
- [Contributing](#contributing)
- [Contributors](#contributors-)
- [License](#license)

## Features

- :rocket: **Fast.** It's built on finite automata, SIMD and aggressive literal optimizations[^perf].
- :sparkles: **Smart defaults.** It respects `.gitignore`, `.ignore` and `.fgignore`, and skips hidden and binary files.
- :zap: **Parallel.** Directory traversal uses every core; use `-j` to limit it.
- :white_check_mark: **Correct Unicode.** Case folding and `\w` are fully Unicode-aware, for example `Straße` matches `STRASSE`.
- :globe_with_meridians: **Encodings.** It searches UTF-16 and legacy code pages (`--encoding cp1251`) transparently.
- :package: **Compressed files.** Use `-z` to search inside `.gz`, `.bz2`, `.xz`, `.zst` and `.lz4`.
- :art: **Colored output**, with hyperlinks for terminals that support OSC 8.
- :warning: **Not POSIX grep.** Some flags differ; see the [FAQ](#faq).
- :x: No support for searching inside `.zip` archives (yet).
- :tada: Over **10k** stars on GitHub! Thank you! :heart:
- :+1: Contributions are welcome, see [CONTRIBUTING](#contributing).

| Feature | fastgrep | grep | ag | ack |
|:--------|:--------:|:----:|:--:|:---:|
| Respects `.gitignore` | :white_check_mark: | :x: | :white_check_mark: | :x: |
| Unicode by default | :white_check_mark: | :warning: | :x: | :white_check_mark: |
| Parallel search | :white_check_mark: | :x: | :white_check_mark: | :x: |
| PCRE2 | :white_check_mark: | :white_check_mark: | :white_check_mark: | :x: |
| Windows native | :white_check_mark: | :warning: | :warning: | :white_check_mark: |

## Installation

### Pre-built binaries

Archives of pre-compiled binaries for **Windows**, **macOS** and **Linux** are on the [releases page](https://github.com/BurntSushi/ripgrep/releases). Pick the file that matches your platform:

| Platform | Architecture | File |
|----------|--------------|------|
| Windows | x86_64 | `fastgrep-2.3.1-x86_64-pc-windows-msvc.zip` |
| Windows | ARM64 | `fastgrep-2.3.1-aarch64-pc-windows-msvc.zip` |
| macOS | Apple Silicon | `fastgrep-2.3.1-aarch64-apple-darwin.tar.gz` |
| Linux | x86_64 (static) | `fastgrep-2.3.1-x86_64-unknown-linux-musl.tar.gz` |

### Package managers

<table>
<tr><th>Windows</th><th>macOS / Linux</th></tr>
<tr>
<td>

```powershell
# winget
winget install fastgrep.fastgrep

# Scoop
scoop install fastgrep

# Chocolatey
choco install fastgrep
```

</td>
<td>

```bash
# Homebrew
brew install fastgrep

# Debian / Ubuntu
sudo apt install fastgrep

# Arch Linux
sudo pacman -S fastgrep
```

</td>
</tr>
</table>

Using Docker:

```dockerfile
FROM alpine:3.20
RUN apk add --no-cache fastgrep
ENTRYPOINT ["fg"]
```

### Building from source

fastgrep needs Rust **1.74** or newer.

```console
$ git clone https://github.com/example/fastgrep
$ cd fastgrep
$ cargo build --release
   Compiling memchr v2.7.4
   Compiling regex-automata v0.4.8
   Compiling fastgrep v2.3.1 (/home/user/fastgrep)
    Finished `release` profile [optimized + debuginfo] target(s) in 41.27s
$ ./target/release/fg --version
fastgrep 2.3.1 (rev 9f2c1e4a)
```

To enable PCRE2 support:

```sh
cargo build --release --features 'pcre2'
```

## Usage

```text
USAGE:
    fg [OPTIONS] PATTERN [PATH ...]
    fg [OPTIONS] -e PATTERN ... [PATH ...]
    fg [OPTIONS] --files [PATH ...]

ARGS:
    <PATTERN>    A regular expression used for searching.
    <PATH>...    A file or directory to search. Directories are searched recursively.
```

Common examples:

```bash
# Search for "TODO" in all Rust files
fg -t rust TODO

# Case-insensitive search, show 2 lines of context
fg -i -C2 'connection (refused|reset)'

# List files that would be searched, without searching them
fg --files | wc -l

# Replace in output (does not modify files)
fg 'fast(\w+)' -r 'quick$1'
```

| Option | Short | Description | Default |
|--------|:-----:|-------------|--------:|
| `--ignore-case` | `-i` | Case-insensitive search | off |
| `--smart-case` | `-S` | Case-insensitive unless the pattern has uppercase | **on** |
| `--context <N>` | `-C` | Show *N* lines before and after each match | 0 |
| `--threads <N>` | `-j` | Number of worker threads | #cores |
| `--max-filesize <SIZE>` | | Skip files larger than *SIZE* (e.g. `50M`) | ∞ |
| `--encoding <ENC>` | `-E` | Force an encoding (`utf-8`, `utf-16le`, `cp1251`, …) | auto |

### Keyboard shortcuts (interactive mode)

Run `fg --interactive` for a TUI with live results:

| Action | Shortcut |
|--------|----------|
| Next match | <kbd>↓</kbd> or <kbd>Ctrl</kbd>+<kbd>N</kbd> |
| Previous match | <kbd>↑</kbd> or <kbd>Ctrl</kbd>+<kbd>P</kbd> |
| Open in editor | <kbd>Enter</kbd> |
| Copy path | <kbd>Ctrl</kbd>+<kbd>C</kbd> |
| Toggle case sensitivity | <kbd>Alt</kbd>+<kbd>C</kbd> |
| Quit | <kbd>Esc</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Q</kbd> |

> [!TIP]
> Press <kbd>F1</kbd> at any time to see every shortcut. On macOS use <kbd>⌘ Cmd</kbd> instead of <kbd>Ctrl</kbd>.

## Configuration

fastgrep reads the file named in `FASTGREP_CONFIG_PATH`. Each line is one argument, and lines starting with `#` are comments:

```ini
# ~/.config/fastgrep/config
--smart-case
--max-columns=150
--max-columns-preview
--glob=!.git/*
--type-add=web:*.{html,css,js,ts,tsx}
```

<details>
<summary>Click to expand: <b>all environment variables</b></summary>

<br>

These environment variables are read at startup. **Markdown works inside `<details>`** as long as blank lines separate it from the HTML tags.

| Variable | Meaning |
|----------|---------|
| `FASTGREP_CONFIG_PATH` | Path to the config file |
| `FASTGREP_COLORS` | Color spec, e.g. `match:fg:magenta` |
| `NO_COLOR` | Disable colors ([no-color.org](https://no-color.org)) |

Example PowerShell profile snippet:

```powershell
$env:FASTGREP_CONFIG_PATH = "$HOME\.fastgreprc"
Set-Alias -Name grep -Value fg
```

1. Nested lists work too
   - with *emphasis*
   - and `code`

</details>

<details open>
<summary>This one is <code>open</code> by default</summary>

Content of an initially expanded `<details>` block. A viewer that cannot collapse sections should at least show the summary and the content.

</details>

## Benchmarks

The benchmark searched the Linux kernel source tree (about 75k files, 1.2 GB) on a Ryzen 9 7950X with a warm cache. Times are the median of 10 runs[^bench].

| Command | Tool | Time | Speedup |
|---------|------|-----:|--------:|
| `fg -n PM_RESUME` | **fastgrep** | **0.082 s** | **1.00×** |
| `rg -n PM_RESUME` | ripgrep | 0.089 s | 0.92× |
| `ag PM_RESUME` | The Silver Searcher | 0.641 s | 0.13× |
| `git grep PM_RESUME` | git grep | 0.273 s | 0.30× |
| `grep -rn PM_RESUME` | GNU grep | 1.874 s | 0.04× |

<p align="center">
  <img src="../diagram.png" width="400" alt="Architecture diagram"><br>
  <sub><i>Figure 1. How the searcher pipeline is laid out (placeholder diagram).</i></sub>
</p>

## Upgrading from 1.x

> [!IMPORTANT]
> Version 2.0 contains **breaking changes**:
>
> 1. `--no-ignore` was split into `--no-ignore-vcs` and `--no-ignore-dot`.
> 2. Exit code `2` now means *error*. Before 2.0 it meant *no match*, which is now exit code `1`.
> 3. The deprecated `--mmap` flag has been removed.

> [!WARNING]
> Config files written for 1.x that use `--mmap` will make fastgrep 2.x exit with an error.

> [!CAUTION]
> `fg --replace` never modifies files, but `fg --replace --write` (experimental) **does**. Commit your work first.

## FAQ

**Q: Why is it called `fg` when that's also a shell built-in?**<br>
A: Good question. Most users alias it: `alias fgr=fg`. On Windows `fg` isn't a built-in.

**Q: Does it support multiline search?**<br>
A: Yes, use `-U` / `--multiline`. For example, `fg -U 'fn main\(\)\s*\{'` finds `main` even when the brace sits on the next line.

**Q: How do I search a file named `-foo`?**<br>
A: Use `--` to end option parsing: `fg pattern -- -foo`.

<details>
<summary><b>Q: Why are some files skipped?</b></summary>

Run with `--debug` to see why each file was skipped. The common reasons are:

- the file is listed in a `.gitignore`,
- the file is hidden (its name starts with `.`), or
- the file looks binary (it contains a `NUL` byte).

</details>

## Contributing

Contributions of all kinds are welcome. Please read the contributing guide (CONTRIBUTING.md, not included in this demo) and our code of conduct first.

1. Fork the repo and create your branch: `git checkout -b feature/amazing-thing`
2. Commit your changes: `git commit -m 'Add amazing thing'`
3. Push the branch: `git push origin feature/amazing-thing`
4. Open a pull request :rocket:

<!-- ALL-CONTRIBUTORS-LIST:START - Do not remove or modify this section -->
## Contributors ✨

Thanks goes to these wonderful people ([emoji key](https://allcontributors.org/docs/en/emoji-key)):

<table>
  <tbody>
    <tr>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/octocat"><img src="../avatar.png" width="100px;" alt=""/><br /><sub><b>Mona Octocat</b></sub></a><br /><a href="https://github.com/example/fastgrep/commits?author=octocat" title="Code">💻</a> <a href="https://github.com/example/fastgrep/commits?author=octocat" title="Documentation">📖</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/torvalds"><img src="../avatar.png" width="100px;" alt=""/><br /><sub><b>Linus T.</b></sub></a><br /><a href="https://github.com/example/fastgrep/pulls?q=is%3Apr+reviewed-by%3Atorvalds" title="Reviewed Pull Requests">👀</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/gvanrossum"><img src="../avatar.png" width="100px;" alt=""/><br /><sub><b>Guido</b></sub></a><br /><a href="#ideas-gvanrossum" title="Ideas, Planning, & Feedback">🤔</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/mity"><img src="../avatar.png" width="100px;" alt=""/><br /><sub><b>Martin Mitáš</b></sub></a><br /><a href="https://github.com/example/fastgrep/commits?author=mity" title="Code">💻</a> <a href="https://github.com/example/fastgrep/commits?author=mity" title="Tests">⚠️</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://example.com"><img src="../avatar.png" width="100px;" alt=""/><br /><sub><b>Анна Кузнецова</b></sub></a><br /><a href="#translation-akuznetsova" title="Translation">🌍</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://example.com"><img src="../avatar.png" width="100px;" alt=""/><br /><sub><b>山田 太郎</b></sub></a><br /><a href="https://github.com/example/fastgrep/issues?q=author%3Ayamada" title="Bug reports">🐛</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://example.com"><img src="../avatar.png" width="100px;" alt=""/><br /><sub><b>A Contributor With A Very Long Name</b></sub></a><br /><a href="#infra-longname" title="Infrastructure (Hosting, Build-Tools, etc)">🚇</a></td>
    </tr>
  </tbody>
  <tfoot>
    <tr>
      <td align="center" size="13px" colspan="7">
        <img src="https://raw.githubusercontent.com/all-contributors/all-contributors-cli/1b8533af435da9854653492b1327a23a4dbd0a10/assets/logo-small.svg">
          <a href="https://all-contributors.js.org/docs/en/bot/usage">Add your contributions</a>
        </img>
      </td>
    </tr>
  </tfoot>
</table>

<!-- ALL-CONTRIBUTORS-LIST:END -->

This project follows the [all-contributors](https://github.com/all-contributors/all-contributors) specification.

### Star history

<a href="https://star-history.com/#mity/md4c&Date">
  <img alt="Star history chart" src="https://api.star-history.com/svg?repos=mity/md4c&type=Date" width="600">
</a>

## License

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](../../LICENSE) or <https://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](../../LICENSE) or <https://opensource.org/licenses/MIT>)

at your option. Copyright © 2021&ndash;2026 The fastgrep developers. fastgrep&trade; is not a registered trademark.

Water is H<sub>2</sub>O, the area of a circle is πr<sup>2</sup>, and this sentence has a footnote-style superscript<sup>[3]</sup> written in plain HTML.

[^perf]: See the [Benchmarks](#benchmarks) section. The literal optimizations are described in *"Regex engine internals as a library"*, a blog post by Andrew Gallant.
[^bench]: Reproduce them with `./benchsuite/run --dir linux --warmup 3`. The numbers vary by ±5% between runs.

---

<p align="center">
  <sub>Made with :heart: and :coffee: by the fastgrep team&nbsp;&nbsp;|&nbsp;&nbsp;<a href="#realistic-github-readme">Back to top ↑</a></sub>
</p>
<p align="right"><a href="#table-of-contents">⬆ table of contents</a></p>
