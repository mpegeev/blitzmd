# Code Blocks and Syntax Highlighting

This file tests fenced code blocks in over 45 languages, with realistic, non-trivial snippets for judging syntax highlighting, monospace font rendering, line height and block padding. It also covers the awkward cases: a block without a language, an unknown language, an info string with extra attributes, indented code, a ~400-character line (wrapping or horizontal scrolling), TAB indentation, Unicode/CJK/RTL/emoji and box-drawing art, a 300-line block, and inline code containing backticks and HTML special characters. On github.com, highlighting comes from Linguist/TextMate grammars, and unknown languages render as plain monospace text.

## Contents

- [Systems languages](#systems-languages): C, C++, C#, Go, Rust, Assembly, Pascal
- [JVM and mobile](#jvm-and-mobile): Java, Kotlin, Swift
- [Web](#web): JavaScript, TypeScript, TSX, HTML, XML, CSS, SCSS, GraphQL
- [Scripting](#scripting): Python, Ruby, PHP, Lua, Perl, R
- [Shells and terminals](#shells-and-terminals): Bash, console, shell-session, PowerShell, Batch
- [Data and configuration](#data-and-configuration): SQL, JSON, YAML, TOML, INI, Protobuf
- [Build and infrastructure](#build-and-infrastructure): Dockerfile, Makefile, CMake, HCL/Terraform, diff
- [Functional languages](#functional-languages): Haskell, Elixir, Clojure
- [Documents](#documents): LaTeX, Markdown
- [Special cases](#special-cases)

Duplicate-anchor check: the headings "C", "C++" and "C#" all reduce to the slug `c`, so GitHub numbers them: [C](#c) · [C++](#c-1) · [C#](#c-2).

---

## Systems languages

### C

```c
#include <stdint.h>
#include <stddef.h>

/* Decode one UTF-8 sequence. Returns the number of bytes consumed,
 * or 0 on malformed input. The code point is stored in *out. */
static size_t utf8_decode(const unsigned char *s, size_t len, uint32_t *out)
{
    if (len == 0) return 0;
    unsigned char c = s[0];
    if (c < 0x80) { *out = c; return 1; }

    size_t n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 0;
    if (n == 0 || n > len) return 0;

    uint32_t cp = c & (0x7F >> n);
    for (size_t i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    static const uint32_t min_cp[] = { 0, 0, 0x80, 0x800, 0x10000 };
    if (cp < min_cp[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return 0;   /* overlong, out of range, or surrogate */
    *out = cp;
    return n;
}
```

### C++

```cpp
#include <iostream>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

template <typename Key, typename Value>
class LruCache {
public:
    explicit LruCache(std::size_t capacity) : capacity_(capacity) {}

    std::optional<Value> get(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) return std::nullopt;
        items_.splice(items_.begin(), items_, it->second);   // move to front
        return it->second->second;
    }

    void put(const Key& key, Value value) {
        if (auto it = index_.find(key); it != index_.end()) {
            it->second->second = std::move(value);
            items_.splice(items_.begin(), items_, it->second);
            return;
        }
        if (items_.size() == capacity_) {
            index_.erase(items_.back().first);
            items_.pop_back();
        }
        items_.emplace_front(key, std::move(value));
        index_[key] = items_.begin();
    }

private:
    std::size_t capacity_;
    std::list<std::pair<Key, Value>> items_;
    std::unordered_map<Key, typename decltype(items_)::iterator> index_;
};

int main() {
    LruCache<std::string, int> cache{2};
    cache.put("a", 1);
    cache.put("b", 2);
    cache.get("a");
    cache.put("c", 3);  // evicts "b"
    std::cout << std::boolalpha << cache.get("b").has_value() << '\n';  // false
}
```

### C#

```csharp
using System.Collections.Concurrent;
using System.Security.Cryptography;

namespace MdView.Tools;

public sealed record FileDigest(string Path, long Size, string Sha256);

public static class Hasher
{
    /// <summary>Hashes every Markdown file under <paramref name="root"/> in parallel.</summary>
    public static async Task<IReadOnlyList<FileDigest>> HashAllAsync(
        string root, CancellationToken ct = default)
    {
        var files = Directory.EnumerateFiles(root, "*.md", SearchOption.AllDirectories);
        var results = new ConcurrentBag<FileDigest>();

        await Parallel.ForEachAsync(files, ct, async (path, token) =>
        {
            await using var stream = File.OpenRead(path);
            var hash = await SHA256.HashDataAsync(stream, token);
            results.Add(new FileDigest(path, stream.Length, Convert.ToHexString(hash)));
        });

        return results.OrderByDescending(d => d.Size)
                      .ThenBy(d => d.Path, StringComparer.OrdinalIgnoreCase)
                      .ToList();
    }
}

// Usage:
// foreach (var d in await Hasher.HashAllAsync(@"C:\Projects\mdview-wlx\samples"))
//     Console.WriteLine($"{d.Sha256[..12]}  {d.Size,10:N0}  {d.Path}");
```

### Go

This snippet uses real TAB characters for indentation, as `gofmt` produces.

```go
package main

import (
	"bufio"
	"fmt"
	"os"
	"sort"
	"strings"
	"unicode"
)

// main counts the words read from stdin and prints the 10 most frequent.
func main() {
	counts := make(map[string]int)
	scanner := bufio.NewScanner(os.Stdin)
	scanner.Split(bufio.ScanWords)
	for scanner.Scan() {
		w := strings.ToLower(strings.TrimFunc(scanner.Text(), func(r rune) bool {
			return !unicode.IsLetter(r) && !unicode.IsNumber(r)
		}))
		if w != "" {
			counts[w]++
		}
	}
	if err := scanner.Err(); err != nil {
		fmt.Fprintln(os.Stderr, "read error:", err)
		os.Exit(1)
	}

	type kv struct {
		Word  string
		Count int
	}
	var list []kv
	for w, c := range counts {
		list = append(list, kv{w, c})
	}
	sort.Slice(list, func(i, j int) bool { return list[i].Count > list[j].Count })
	for i := 0; i < len(list) && i < 10; i++ {
		fmt.Printf("%3d. %-20s %6d\n", i+1, list[i].Word, list[i].Count)
	}
}
```

### Rust

```rust
use std::collections::HashMap;
use std::fmt;

#[derive(Debug, Clone, PartialEq)]
enum Token<'a> {
    Number(f64),
    Ident(&'a str),
    Op(char),
    LParen,
    RParen,
}

impl fmt::Display for Token<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Token::Number(n) => write!(f, "{n}"),
            Token::Ident(s) => write!(f, "{s}"),
            Token::Op(c) => write!(f, "{c}"),
            Token::LParen => f.write_str("("),
            Token::RParen => f.write_str(")"),
        }
    }
}

fn tokenize(src: &str) -> Result<Vec<Token<'_>>, String> {
    let mut out = Vec::new();
    let mut chars = src.char_indices().peekable();
    while let Some(&(i, c)) = chars.peek() {
        match c {
            ' ' | '\t' => { chars.next(); }
            '0'..='9' | '.' => {
                while matches!(chars.peek(), Some(&(_, d)) if d.is_ascii_digit() || d == '.') {
                    chars.next();
                }
                let end = chars.peek().map_or(src.len(), |&(j, _)| j);
                out.push(Token::Number(src[i..end].parse().map_err(|e| format!("{e}"))?));
            }
            c if c.is_alphabetic() || c == '_' => {
                while matches!(chars.peek(), Some(&(_, d)) if d.is_alphanumeric() || d == '_') {
                    chars.next();
                }
                let end = chars.peek().map_or(src.len(), |&(j, _)| j);
                out.push(Token::Ident(&src[i..end]));
            }
            '+' | '-' | '*' | '/' | '^' => { out.push(Token::Op(c)); chars.next(); }
            '(' => { out.push(Token::LParen); chars.next(); }
            ')' => { out.push(Token::RParen); chars.next(); }
            other => return Err(format!("unexpected character {other:?} at byte {i}")),
        }
    }
    Ok(out)
}

fn main() {
    let vars: HashMap<&str, f64> = [("pi", std::f64::consts::PI)].into();
    match tokenize("2 * (pi + 1.5) ^ 2") {
        Ok(tokens) => println!("{} tokens: {:?} (vars: {vars:?})", tokens.len(), tokens),
        Err(e) => eprintln!("error: {e}"),
    }
}
```

### Assembly

```asm
; Linux x86-64: write "Hello, Lister!" and exit (NASM syntax)
; build: nasm -f elf64 hello.asm && ld -o hello hello.o

section .rodata
    msg     db  "Hello, Lister!", 10
    msglen  equ $ - msg

section .text
    global _start

_start:
    mov     rax, 1          ; sys_write
    mov     rdi, 1          ; fd = stdout
    lea     rsi, [rel msg]  ; buffer
    mov     rdx, msglen     ; length
    syscall

    test    rax, rax
    js      .error          ; negative return value = -errno

    xor     edi, edi        ; status = 0
    jmp     .exit
.error:
    mov     edi, 1
.exit:
    mov     eax, 60         ; sys_exit
    syscall
```

### Pascal / Delphi

```pascal
unit MarkdownStats;

interface

uses
  System.SysUtils, System.Classes, System.Generics.Collections;

type
  TMarkdownStats = class
  private
    FHeadings: TDictionary<Integer, Integer>;
    FWordCount: Int64;
  public
    constructor Create;
    destructor Destroy; override;
    procedure LoadFromFile(const AFileName: string);
    function HeadingCount(ALevel: Integer): Integer;
    property WordCount: Int64 read FWordCount;
  end;

implementation

constructor TMarkdownStats.Create;
begin
  inherited Create;
  FHeadings := TDictionary<Integer, Integer>.Create;
end;

destructor TMarkdownStats.Destroy;
begin
  FHeadings.Free;
  inherited;
end;

procedure TMarkdownStats.LoadFromFile(const AFileName: string);
var
  Lines: TStringList;
  Line: string;
  Level: Integer;
begin
  Lines := TStringList.Create;
  try
    Lines.LoadFromFile(AFileName, TEncoding.UTF8);
    for Line in Lines do
    begin
      Level := 0;
      while (Level < Length(Line)) and (Line[Level + 1] = '#') do
        Inc(Level);
      if (Level in [1..6]) and (Length(Line) > Level) and (Line[Level + 1] = ' ') then
        FHeadings.AddOrSetValue(Level, HeadingCount(Level) + 1);
      Inc(FWordCount, Length(Line.Split([' ', #9], TStringSplitOptions.ExcludeEmpty)));
    end;
  finally
    Lines.Free;
  end;
end;

function TMarkdownStats.HeadingCount(ALevel: Integer): Integer;
begin
  if not FHeadings.TryGetValue(ALevel, Result) then
    Result := 0;
end;

end.
```

The same language through the `delphi` alias:

```delphi
procedure TMainForm.FormCreate(Sender: TObject);
begin
  Caption := Format('mdview %s (%d-bit)', [Version, SizeOf(Pointer) * 8]);
  {$IFDEF DEBUG} ReportMemoryLeaksOnShutdown := True; {$ENDIF}
end;
```

---

## JVM and mobile

### Java

```java
package com.example.shapes;

import java.util.Comparator;
import java.util.List;

public class Shapes {
    sealed interface Shape permits Circle, Rectangle, Triangle {}
    record Circle(double radius) implements Shape {}
    record Rectangle(double width, double height) implements Shape {}
    record Triangle(double a, double b, double c) implements Shape {}

    static double area(Shape shape) {
        return switch (shape) {
            case Circle c -> Math.PI * c.radius() * c.radius();
            case Rectangle r -> r.width() * r.height();
            case Triangle t -> {
                double s = (t.a() + t.b() + t.c()) / 2.0;   // Heron's formula
                yield Math.sqrt(s * (s - t.a()) * (s - t.b()) * (s - t.c()));
            }
        };
    }

    @SuppressWarnings("unused")
    public static void main(String[] args) {
        List<Shape> shapes = List.of(new Circle(1.5), new Rectangle(2, 3), new Triangle(3, 4, 5));
        shapes.stream()
              .sorted(Comparator.comparingDouble(Shapes::area).reversed())
              .forEach(s -> System.out.printf("%-40s %8.3f%n", s, area(s)));
    }
}
```

### Kotlin

```kotlin
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

data class Reading(val sensor: String, val celsius: Double)

fun sensorFlow(name: String, periodMs: Long): Flow<Reading> = flow {
    var t = 20.0
    while (true) {
        t += (-5..5).random() / 10.0
        emit(Reading(name, t))
        delay(periodMs)
    }
}

fun main() = runBlocking {
    val merged = merge(
        sensorFlow("kitchen", 300),
        sensorFlow("garage", 500),
    )
    merged
        .filter { it.celsius > 19.5 }
        .map { "${it.sensor.padEnd(8)} ${"%.1f".format(it.celsius)} °C" }
        .take(10)
        .collect(::println)
}
```

### Swift

```swift
import Foundation

enum NetworkError: Error, CustomStringConvertible {
    case badStatus(Int)
    case decoding(underlying: Error)

    var description: String {
        switch self {
        case .badStatus(let code): return "HTTP \(code)"
        case .decoding(let e):     return "Decoding failed: \(e.localizedDescription)"
        }
    }
}

struct Repo: Decodable, Identifiable {
    let id: Int
    let fullName: String
    let stargazersCount: Int
}

func fetchRepos(user: String) async throws -> [Repo] {
    let url = URL(string: "https://api.github.com/users/\(user)/repos?per_page=100")!
    let (data, response) = try await URLSession.shared.data(from: url)
    guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
        throw NetworkError.badStatus((response as? HTTPURLResponse)?.statusCode ?? -1)
    }
    let decoder = JSONDecoder()
    decoder.keyDecodingStrategy = .convertFromSnakeCase
    do {
        return try decoder.decode([Repo].self, from: data)
            .sorted { $0.stargazersCount > $1.stargazersCount }
    } catch {
        throw NetworkError.decoding(underlying: error)
    }
}
```

---

## Web

### JavaScript

```javascript
// Debounced search box with AbortController and a tiny cache
const cache = new Map();

function debounce(fn, ms = 250) {
  let timer;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), ms);
  };
}

let controller = null;

async function search(query) {
  if (!query.trim()) return render([]);
  if (cache.has(query)) return render(cache.get(query));

  controller?.abort();
  controller = new AbortController();
  try {
    const res = await fetch(`/api/search?q=${encodeURIComponent(query)}`, {
      signal: controller.signal,
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const { items = [] } = await res.json();
    cache.set(query, items);
    render(items);
  } catch (err) {
    if (err.name !== 'AbortError') console.error('search failed:', err);
  }
}

function render(items) {
  const list = document.querySelector('#results');
  list.innerHTML = items
    .map(({ title, url }) => `<li><a href="${url}">${title}</a></li>`)
    .join('');
}

document.querySelector('#q').addEventListener('input', debounce((e) => search(e.target.value)));
```

### TypeScript

```typescript
type Result<T, E = Error> = { ok: true; value: T } | { ok: false; error: E };

interface User {
  id: number;
  name: string;
  email?: string;
  roles: ReadonlyArray<'admin' | 'editor' | 'viewer'>;
}

export class Repository<T extends { id: number }> {
  private readonly items = new Map<number, T>();

  add(item: T): Result<T> {
    if (this.items.has(item.id)) {
      return { ok: false, error: new Error(`duplicate id ${item.id}`) };
    }
    this.items.set(item.id, item);
    return { ok: true, value: item };
  }

  find<K extends keyof T>(key: K, value: T[K]): T[] {
    return [...this.items.values()].filter((it) => it[key] === value);
  }
}

const users = new Repository<User>();
const r = users.add({ id: 1, name: 'Ada', roles: ['admin'] });
if (r.ok) {
  console.log(`added ${r.value.name}`);
} else {
  console.error(r.error.message);
}
```

### TSX (React)

```tsx
import { useEffect, useState } from 'react';

type Props = { src: string; title?: string };

export function MarkdownPreview({ src, title = 'Preview' }: Props) {
  const [html, setHtml] = useState<string>('');
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    fetch(src)
      .then((r) => (r.ok ? r.text() : Promise.reject(new Error(r.statusText))))
      .then((md) => import('marked').then(({ marked }) => marked.parse(md)))
      .then((out) => !cancelled && setHtml(out as string))
      .catch((e: Error) => !cancelled && setError(e.message));
    return () => { cancelled = true; };
  }, [src]);

  if (error) return <p className="error">Failed to load: {error}</p>;
  return (
    <section aria-label={title}>
      <h2>{title}</h2>
      {html ? <article dangerouslySetInnerHTML={{ __html: html }} /> : <Spinner size={24} />}
    </section>
  );
}

const Spinner = ({ size }: { size: number }) => (
  <svg width={size} height={size} viewBox="0 0 24 24" className="spin">
    <circle cx="12" cy="12" r="10" strokeWidth="3" fill="none" />
  </svg>
);
```

### HTML

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Markdown preview</title>
  <link rel="stylesheet" href="github-markdown.css">
  <style>
    .markdown-body { box-sizing: border-box; max-width: 980px; margin: 0 auto; padding: 45px; }
    @media (max-width: 767px) { .markdown-body { padding: 15px; } }
  </style>
</head>
<body>
  <!-- rendered markdown goes here -->
  <article class="markdown-body" id="content">
    <h1>Hello &amp; welcome</h1>
    <p>Press <kbd>F3</kbd> to open the <a href="https://www.ghisler.com/">Lister</a>.</p>
    <img src="logo.png" alt="Logo" width="120" loading="lazy">
  </article>
  <script type="module">
    import { render } from './render.js';
    render(document.getElementById('content'), { theme: 'auto' });
  </script>
</body>
</html>
```

### XML

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!-- MSBuild project for a companion tool -->
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup Condition="'$(Configuration)' == 'Release'">
    <TargetFramework>net8.0-windows</TargetFramework>
    <Nullable>enable</Nullable>
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>
    <Description><![CDATA[Markdown viewer for <Total Commander> & friends]]></Description>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Markdig" Version="0.37.0" />
    <None Include="samples\**\*.md" CopyToOutputDirectory="PreserveNewest" />
  </ItemGroup>
  <Target Name="PrintVersion" AfterTargets="Build">
    <Message Importance="high" Text="Built $(AssemblyName) v$(Version) &#x2713;" />
  </Target>
</Project>
```

### CSS

```css
:root {
  --fg: #1f2328;
  --bg: #ffffff;
  --border: #d1d9e0;
  --accent: #0969da;
  color-scheme: light dark;
}

@media (prefers-color-scheme: dark) {
  :root { --fg: #f0f6fc; --bg: #0d1117; --border: #3d444d; --accent: #4493f8; }
}

.markdown-body {
  font: 16px/1.5 -apple-system, BlinkMacSystemFont, "Segoe UI", "Noto Sans", Helvetica, Arial, sans-serif;
  color: var(--fg);
  background: var(--bg);
}

.markdown-body h1,
.markdown-body h2 {
  padding-bottom: .3em;
  border-bottom: 1px solid var(--border);
}

.markdown-body a:not([href^="#"])::after { content: " ↗"; font-size: 0.75em; }

.markdown-body table tr:nth-child(2n) {
  background-color: color-mix(in srgb, var(--fg) 4%, transparent);
}

@supports (text-wrap: pretty) {
  .markdown-body p { text-wrap: pretty; }
}
```

### SCSS

```scss
@use 'sass:math';
@use 'sass:map';

$breakpoints: (sm: 544px, md: 768px, lg: 1012px, xl: 1280px);
$alert-colors: (
  note: #0969da,
  tip: #1a7f37,
  important: #8250df,
  warning: #9a6700,
  caution: #d1242f,
);

@mixin respond-to($name) {
  @media (min-width: map.get($breakpoints, $name)) { @content; }
}

@function rem($px, $base: 16px) {
  @return math.div($px, $base) * 1rem;
}

.markdown-alert {
  padding: rem(8px) rem(16px);
  margin-bottom: rem(16px);
  border-left: .25em solid;

  @each $type, $color in $alert-colors {
    &.markdown-alert-#{$type} {
      border-left-color: $color;
      .markdown-alert-title { color: $color; }
    }
  }

  > :last-child { margin-bottom: 0; }

  @include respond-to(md) {
    padding: rem(12px) rem(20px);
  }
}
```

### GraphQL

```graphql
# Fetch the latest releases and open bugs of a repository
query RepoOverview($owner: String!, $name: String!, $first: Int = 5) {
  repository(owner: $owner, name: $name) {
    nameWithOwner
    stargazerCount
    licenseInfo { spdxId }
    releases(first: $first, orderBy: { field: CREATED_AT, direction: DESC }) {
      nodes {
        tagName
        publishedAt
        ...ReleaseAssets
      }
    }
    issues(states: OPEN, labels: ["bug"], first: $first) {
      totalCount
      nodes { number title author { login } }
    }
  }
}

fragment ReleaseAssets on Release {
  releaseAssets(first: 10) {
    nodes { name size downloadCount }
  }
}

mutation AddStar($id: ID!) {
  addStar(input: { starrableId: $id }) {
    starrable { stargazerCount }
  }
}
```

---

## Scripting

### Python

```python
from __future__ import annotations

import asyncio
import dataclasses
import pathlib
import re
from collections import Counter
from typing import Iterable

HEADING = re.compile(r"^(#{1,6})\s+(.+?)\s*#*\s*$", re.MULTILINE)


@dataclasses.dataclass(frozen=True, slots=True)
class Heading:
    level: int
    text: str

    @property
    def anchor(self) -> str:
        """GitHub-style anchor: lowercase, punctuation removed, spaces to hyphens."""
        slug = re.sub(r"[^\w\- ]", "", self.text.lower())
        return slug.replace(" ", "-")


def headings(markdown: str) -> Iterable[Heading]:
    for m in HEADING.finditer(markdown):
        yield Heading(len(m.group(1)), m.group(2))


async def scan(folder: pathlib.Path) -> Counter[int]:
    levels: Counter[int] = Counter()
    for path in sorted(folder.glob("*.md")):
        text = await asyncio.to_thread(path.read_text, encoding="utf-8", errors="replace")
        for h in headings(text):
            levels[h.level] += 1
            print(f"{'  ' * (h.level - 1)}- [{h.text}](#{h.anchor})")
    return levels


if __name__ == "__main__":
    stats = asyncio.run(scan(pathlib.Path(".")))
    print({f"h{k}": v for k, v in sorted(stats.items())})
```

### Ruby

```ruby
require 'json'
require 'time'

module Changelog
  Entry = Struct.new(:version, :date, :changes, keyword_init: true) do
    def to_markdown
      lines = ["## #{version} (#{date.strftime('%Y-%m-%d')})", '']
      changes.group_by { |c| c[:type] }.each do |type, items|
        lines << "### #{type.capitalize}"
        items.each { |c| lines << "- #{c[:text]}#{c[:pr] ? " (##{c[:pr]})" : ''}" }
        lines << ''
      end
      lines.join("\n")
    end
  end

  def self.load(path)
    JSON.parse(File.read(path), symbolize_names: true).map do |h|
      Entry.new(version: h[:version], date: Time.parse(h[:date]), changes: h[:changes])
    end
  end
end

if $PROGRAM_NAME == __FILE__
  entries = Changelog.load(ARGV.fetch(0, 'changelog.json'))
  puts entries.sort_by(&:date).reverse.map(&:to_markdown).join("\n")
end
```

### PHP

```php
<?php
declare(strict_types=1);

namespace App\Http\Controllers;

use App\Models\Post;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

final class PostController
{
    public function index(Request $request): JsonResponse
    {
        $validated = $request->validate([
            'tag'      => ['nullable', 'string', 'max:50'],
            'per_page' => ['nullable', 'integer', 'between:1,100'],
        ]);

        $posts = Post::query()
            ->with('author:id,name')
            ->when($validated['tag'] ?? null, fn ($q, string $tag) => $q->whereJsonContains('tags', $tag))
            ->latest('published_at')
            ->paginate($validated['per_page'] ?? 20);

        return response()->json([
            'data' => $posts->items(),
            'meta' => ['total' => $posts->total(), 'page' => $posts->currentPage()],
        ]);
    }
}
```

### Lua

```lua
-- A queue class built on metatables, fed by a coroutine-based generator
local Queue = {}
Queue.__index = Queue

function Queue.new()
  return setmetatable({ first = 1, last = 0, items = {} }, Queue)
end

function Queue:push(value)
  self.last = self.last + 1
  self.items[self.last] = value
end

function Queue:pop()
  if self.first > self.last then return nil end
  local value = self.items[self.first]
  self.items[self.first] = nil
  self.first = self.first + 1
  return value
end

local function fibonacci(limit)
  return coroutine.wrap(function()
    local a, b = 0, 1
    while a <= limit do
      coroutine.yield(a)
      a, b = b, a + b
    end
  end)
end

local q = Queue.new()
for n in fibonacci(100) do q:push(n) end
local parts = {}
for v in function() return q:pop() end do parts[#parts + 1] = tostring(v) end
print(table.concat(parts, ", "))  --> 0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89
```

### Perl

```perl
#!/usr/bin/env perl
use strict;
use warnings;
use utf8;
use open qw(:std :encoding(UTF-8));

# Summarise an Apache access log: requests per status code and top 5 paths.
my (%status, %paths);
my $re = qr{^(\S+) \S+ \S+ \[([^\]]+)\] "(\w+) (\S+) [^"]*" (\d{3}) (\d+|-)};

while (my $line = <>) {
    next unless $line =~ $re;
    my ($ip, $time, $method, $path, $code, $bytes) = ($1, $2, $3, $4, $5, $6);
    $status{$code}++;
    $paths{$path} += $bytes eq '-' ? 0 : $bytes;
}

printf "%-6s %8s\n", 'Status', 'Count';
printf "%-6s %8d\n", $_, $status{$_} for sort keys %status;

my @top = (sort { $paths{$b} <=> $paths{$a} } keys %paths)[0 .. 4];
print "\nTop paths by bytes:\n";
printf "  %-40s %12s\n", $_, commify($paths{$_}) for grep { defined } @top;

sub commify {
    my $n = reverse shift;
    $n =~ s/(\d{3})(?=\d)/$1,/g;
    return scalar reverse $n;
}
```

### R

```r
library(dplyr)
library(ggplot2)

# Monthly downloads per package, with a 3-month rolling mean
downloads <- read.csv("downloads.csv", stringsAsFactors = FALSE) |>
  mutate(month = as.Date(paste0(month, "-01"))) |>
  arrange(package, month) |>
  group_by(package) |>
  mutate(rolling = stats::filter(count, rep(1 / 3, 3), sides = 1)) |>
  ungroup()

summary_tbl <- downloads |>
  group_by(package) |>
  summarise(total = sum(count), peak = max(count), .groups = "drop") |>
  arrange(desc(total))

print(summary_tbl, n = 10)

ggplot(downloads, aes(month, count, colour = package)) +
  geom_line(alpha = 0.4) +
  geom_line(aes(y = rolling), linewidth = 1) +
  scale_y_continuous(labels = scales::comma) +
  labs(title = "Monthly downloads", x = NULL, y = "Downloads") +
  theme_minimal()
```

---

## Shells and terminals

### Bash

```bash
#!/usr/bin/env bash
set -euo pipefail

# Build every CMake preset and collect the artifacts into ./dist
readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly DIST="${ROOT}/dist"
presets=("x64-release" "x86-release" "arm64-release")

log() { printf '\e[1;34m==>\e[0m %s\n' "$*"; }
die() { printf '\e[1;31merror:\e[0m %s\n' "$*" >&2; exit 1; }

command -v cmake >/dev/null 2>&1 || die "cmake not found in PATH"
mkdir -p "$DIST"

for preset in "${presets[@]}"; do
  log "Configuring ${preset}"
  cmake --preset "$preset" > "${DIST}/${preset}.log" 2>&1 \
    || die "configure failed, see ${DIST}/${preset}.log"
  log "Building ${preset}"
  cmake --build --preset "$preset" --parallel "$(nproc 2>/dev/null || echo 4)"
  find "${ROOT}/out/build/${preset}" -name '*.wlx*' -exec cp -v {} "$DIST/" \;
done

count=$(find "$DIST" -type f -name '*.wlx*' | wc -l)
log "Done: ${count} artifact(s) in ${DIST}"
[[ $count -gt 0 ]] || die "no artifacts produced"
```

### Console session (`console`)

Lines starting with `$ ` are commands and the rest is output. GitHub highlights prompts and commands differently from output.

```console
$ python -m venv .venv
$ source .venv/bin/activate
(.venv) $ pip install --upgrade cmarkgfm
Collecting cmarkgfm
  Downloading cmarkgfm-2024.11.20-cp312-cp312-manylinux_2_17_x86_64.whl (435 kB)
     ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ 435.2/435.2 kB 4.1 MB/s eta 0:00:00
Installing collected packages: cmarkgfm
Successfully installed cmarkgfm-2024.11.20
(.venv) $ python -c 'import cmarkgfm; print(cmarkgfm.github_flavored_markdown_to_html("~~hi~~"))'
<p><del>hi</del></p>
(.venv) $ echo $?
0
$ ls -la /nonexistent
ls: cannot access '/nonexistent': No such file or directory
```

### Shell session (`shell-session`)

```shell-session
user@host:~$ uname -srm
Linux 6.8.0-45-generic x86_64
user@host:~$ df -h /
Filesystem      Size  Used Avail Use% Mounted on
/dev/nvme0n1p2  468G  201G  244G  46% /
user@host:~$ sudo -i
root@host:~# systemctl status nginx --no-pager | head -3
● nginx.service - A high performance web server and a reverse proxy server
     Loaded: loaded (/lib/systemd/system/nginx.service; enabled; preset: enabled)
     Active: active (running) since Mon 2026-09-21 09:12:44 UTC; 2 days ago
```

### PowerShell

```powershell
#Requires -Version 7.2
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)]
    [ValidateScript({ Test-Path $_ -PathType Container })]
    [string] $Path,

    [ValidateRange(1, 3650)]
    [int] $OlderThanDays = 30
)

$cutoff = (Get-Date).AddDays(-$OlderThanDays)
$files = Get-ChildItem -LiteralPath $Path -Recurse -File -Include *.log, *.tmp |
    Where-Object { $_.LastWriteTime -lt $cutoff }

$totalMB = [math]::Round(($files | Measure-Object Length -Sum).Sum / 1MB, 2)
Write-Host "Found $($files.Count) file(s), $totalMB MB older than $($cutoff.ToString('yyyy-MM-dd'))" -ForegroundColor Cyan

foreach ($f in $files) {
    if ($PSCmdlet.ShouldProcess($f.FullName, 'Remove')) {
        try {
            Remove-Item -LiteralPath $f.FullName -ErrorAction Stop
        }
        catch {
            Write-Warning "Could not delete '$($f.Name)': $($_.Exception.Message)"
        }
    }
}
```

### Batch (`batch` / `cmd`)

```batch
@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Install the Lister plugin into Total Commander's plugin folder
set "TC_DIR=%COMMANDER_PATH%"
if not defined TC_DIR set "TC_DIR=C:\Program Files\totalcmd"
set "DEST=%TC_DIR%\Plugins\wlx\mdview"

if not exist "%~dp0mdview.wlx64" (
    echo [ERROR] mdview.wlx64 not found next to this script. 1>&2
    exit /b 1
)

if not exist "%DEST%" mkdir "%DEST%" || goto :fail

set /a COUNT=0
for %%F in ("%~dp0*.wlx" "%~dp0*.wlx64" "%~dp0*.ini") do (
    copy /y "%%~F" "%DEST%\" >nul && set /a COUNT+=1
    echo   copied %%~nxF
)

echo Installed !COUNT! file(s) to "%DEST%".
endlocal & exit /b 0

:fail
echo [ERROR] Could not create "%DEST%" (errorlevel %ERRORLEVEL%). 1>&2
exit /b 2
```

```cmd
C:\> dir /b *.md | find /c /v ""
14
```

---

## Data and configuration

### SQL

```sql
-- Top customers by revenue in the last 90 days, with rank and share of total
WITH recent_orders AS (
    SELECT o.customer_id,
           SUM(oi.quantity * oi.unit_price) AS revenue,
           COUNT(DISTINCT o.id)             AS orders
    FROM orders o
    JOIN order_items oi ON oi.order_id = o.id
    WHERE o.created_at >= CURRENT_DATE - INTERVAL '90 days'
      AND o.status NOT IN ('cancelled', 'refunded')
    GROUP BY o.customer_id
)
SELECT c.id,
       c.name,
       r.orders,
       ROUND(r.revenue, 2)                                   AS revenue,
       RANK() OVER (ORDER BY r.revenue DESC)                 AS revenue_rank,
       ROUND(100.0 * r.revenue / SUM(r.revenue) OVER (), 1)  AS pct_of_total
FROM recent_orders r
JOIN customers c ON c.id = r.customer_id
WHERE c.email NOT LIKE '%@example.com'
ORDER BY revenue_rank
LIMIT 20;

/* Partial index to speed up the query above */
CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_orders_created_status
    ON orders (created_at, status)
    WHERE status <> 'cancelled';
```

### JSON

```json
{
  "$schema": "https://json.schemastore.org/package.json",
  "name": "@example/markdown-viewer",
  "version": "2.3.1",
  "private": false,
  "description": "Render GitHub Flavored Markdown with \"pixel-perfect\" fidelity",
  "keywords": ["markdown", "gfm", "viewer", "预览", "просмотр"],
  "main": "dist/index.js",
  "scripts": {
    "build": "tsc -p tsconfig.json",
    "test": "vitest run --coverage",
    "lint": "eslint . --max-warnings=0"
  },
  "engines": { "node": ">=20.0.0" },
  "dependencies": {
    "marked": "^14.1.2",
    "dompurify": "~3.1.7"
  },
  "features": {
    "tables": true,
    "maxNesting": 32,
    "ratio": 1.618,
    "exponent": 6.02e23,
    "nothing": null,
    "emptyArray": [],
    "emptyObject": {}
  }
}
```

JSON with comments (`jsonc`):

```jsonc
{
  // VS Code settings for this repository
  "editor.rulers": [100],
  "files.eol": "\n",
  /* trailing commas are allowed in JSONC */
  "cmake.configureOnOpen": true,
}
```

### YAML

```yaml
# GitHub Actions workflow: build the plugin on every push
name: build

on:
  push:
    branches: [master, 'feature/**']
  pull_request:
  workflow_dispatch:

env:
  BUILD_TYPE: Release

jobs:
  build:
    name: ${{ matrix.arch }} build
    runs-on: windows-2022
    strategy:
      fail-fast: false
      matrix:
        arch: [x64, x86, arm64]
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - name: Configure
        run: cmake --preset ${{ matrix.arch }}-release
      - name: Build
        run: cmake --build --preset ${{ matrix.arch }}-release --parallel
      - name: Test
        if: matrix.arch != 'arm64'   # cannot run ARM64 binaries on x64 runners
        run: ctest --preset ${{ matrix.arch }}-release --output-on-failure
      - uses: actions/upload-artifact@v4
        with:
          name: mdview-${{ matrix.arch }}
          path: |
            out/build/**/*.wlx
            out/build/**/*.wlx64
          retention-days: 14
```

### TOML

```toml
[package]
name = "fastgrep"
version = "2.3.1"
edition = "2021"
authors = ["Jane Doe <jane@example.com>"]
description = """
A fast, Unicode-aware grep \
with sensible defaults."""
license = "MIT OR Apache-2.0"

[dependencies]
regex = { version = "1.10", default-features = false, features = ["std", "unicode"] }
memchr = "2.7"
clap = { version = "4.5", features = ["derive", "wrap_help"] }

[target.'cfg(windows)'.dependencies]
windows-sys = { version = "0.59", features = ["Win32_Storage_FileSystem"] }

[profile.release]
lto = "fat"
codegen-units = 1
strip = true
opt-level = 3

[[bin]]
name = "fg"
path = "src/main.rs"

[package.metadata.release]
publish-date = 2026-09-23T10:00:00Z
ratios = [0.5, 1.0, 1.5e3, inf, nan]
```

### INI

```ini
; mdview.ini: Lister plugin settings
[General]
Theme=auto              ; auto | light | dark
FontFace=Segoe UI
FontSize=11
CodeFontFace=Cascadia Mono
WrapCode=0
LoadRemoteImages=1

[Extensions]
Detect=EXT="MD" | EXT="MARKDOWN" | EXT="MDOWN" | EXT="MKD"

[Colors]
; #RRGGBB, used only when Theme=light
Link=#0969DA
CodeBackground=#F6F8FA

[Paths\Recent]
1=C:\Projects\mdview-wlx\README.md
2=\\server\share\docs\CHANGELOG.md
```

### Protocol Buffers

```protobuf
syntax = "proto3";

package mdview.render.v1;

import "google/protobuf/timestamp.proto";

option go_package = "github.com/example/mdview/gen/renderv1";
option csharp_namespace = "MdView.Render.V1";

// A request to render a Markdown document.
message RenderRequest {
  string path = 1;
  bytes content = 2;              // raw file bytes; encoding is auto-detected
  Theme theme = 3;
  uint32 viewport_width_px = 4;
  map<string, string> options = 5;
  reserved 6, 7;
  reserved "legacy_css";
}

enum Theme {
  THEME_UNSPECIFIED = 0;
  THEME_LIGHT = 1;
  THEME_DARK = 2;
}

message RenderResponse {
  oneof result {
    string html = 1;
    RenderError error = 2;
  }
  google.protobuf.Timestamp rendered_at = 3;
  repeated string warnings = 4;
}

message RenderError {
  int32 line = 1;
  string message = 2;
}

service Renderer {
  rpc Render(RenderRequest) returns (RenderResponse);
  rpc Watch(RenderRequest) returns (stream RenderResponse);
}
```

---

## Build and infrastructure

### Dockerfile

```dockerfile
# syntax=docker/dockerfile:1.7
FROM python:3.12-slim AS build
WORKDIR /src
COPY requirements.txt .
RUN --mount=type=cache,target=/root/.cache/pip \
    pip wheel --wheel-dir /wheels -r requirements.txt

FROM python:3.12-slim
LABEL org.opencontainers.image.source="https://github.com/example/md-render" \
      org.opencontainers.image.licenses="MIT"
ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PORT=8080
RUN useradd --create-home --uid 10001 app
COPY --from=build /wheels /wheels
RUN pip install --no-cache-dir /wheels/* && rm -rf /wheels
WORKDIR /app
COPY --chown=app:app . .
USER app
EXPOSE ${PORT}
HEALTHCHECK --interval=30s --timeout=3s CMD python -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/health')"
ENTRYPOINT ["gunicorn", "app:create_app()"]
CMD ["--bind", "0.0.0.0:8080", "--workers", "4"]
```

### Makefile

Recipe lines in a Makefile **must** start with a TAB character. This block contains real TABs.

```makefile
CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -std=c11
LDLIBS  := -lm
PREFIX  ?= /usr/local

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)
DEP := $(OBJ:.o=.d)
BIN := build/md2html

.PHONY: all clean install test

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

build:
	@mkdir -p $@

test: $(BIN)
	@for f in tests/*.md; do \
		./$(BIN) "$$f" | diff -q - "$${f%.md}.html" >/dev/null \
			&& echo "PASS $$f" || echo "FAIL $$f"; \
	done

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/md2html

clean:
	$(RM) -r build

-include $(DEP)
```

### CMake

```cmake
cmake_minimum_required(VERSION 3.21)
project(mdview-wlx VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(MDVIEW_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(third_party/md4c EXCLUDE_FROM_ALL)

add_library(mdview SHARED
    src/plugin.cpp
    src/render/layout.cpp
    src/render/d2d_renderer.cpp
    src/plugin.def
)
target_link_libraries(mdview PRIVATE md4c d2d1 dwrite windowscodecs shlwapi)
target_compile_definitions(mdview PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set_target_properties(mdview PROPERTIES SUFFIX ".wlx64" PREFIX "")
else()
    set_target_properties(mdview PROPERTIES SUFFIX ".wlx" PREFIX "")
endif()

if(MSVC)
    target_compile_options(mdview PRIVATE /W4 /permissive- /utf-8 $<$<CONFIG:Release>:/GL>)
    target_link_options(mdview PRIVATE $<$<CONFIG:Release>:/LTCG>)
endif()

if(MDVIEW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

### HCL / Terraform

```hcl
terraform {
  required_version = ">= 1.6"
  required_providers {
    azurerm = {
      source  = "hashicorp/azurerm"
      version = "~> 3.110"
    }
  }
}

variable "environment" {
  type        = string
  description = "Deployment environment"
  default     = "staging"
  validation {
    condition     = contains(["staging", "production"], var.environment)
    error_message = "Environment must be staging or production."
  }
}

locals {
  prefix = "mdview-${var.environment}"
  tags = {
    project     = "mdview"
    environment = var.environment
    managed_by  = "terraform"
  }
}

resource "azurerm_storage_account" "docs" {
  name                     = replace("${local.prefix}docs", "-", "")
  resource_group_name      = azurerm_resource_group.main.name
  location                 = azurerm_resource_group.main.location
  account_tier             = "Standard"
  account_replication_type = var.environment == "production" ? "GRS" : "LRS"
  tags                     = local.tags

  static_website {
    index_document = "index.html"
  }
}

output "docs_url" {
  value = azurerm_storage_account.docs.primary_web_endpoint
}
```

The `terraform` alias:

```terraform
module "cdn" {
  source  = "./modules/cdn"
  enabled = var.environment == "production"
  origins = [for s in azurerm_storage_account.docs[*] : s.primary_web_host]
}
```

### Diff

```diff
diff --git a/src/render/layout.cpp b/src/render/layout.cpp
index 3f2a1c9..8b7e4d2 100644
--- a/src/render/layout.cpp
+++ b/src/render/layout.cpp
@@ -112,11 +112,15 @@ void Layout::placeTable(TableBox& table, float availableWidth)
 {
     const size_t cols = table.columnCount();
-    float colWidth = availableWidth / cols;
-    for (auto& cell : table.cells())
-        cell.width = colWidth;
+    // Distribute width proportionally to each column's preferred width,
+    // but never below its minimum (the longest unbreakable word).
+    std::vector<float> pref(cols), minw(cols);
+    table.measureColumns(pref, minw);
+    const float total = std::accumulate(pref.begin(), pref.end(), 0.0f);
+    for (size_t c = 0; c < cols; ++c)
+        table.setColumnWidth(c, std::max(minw[c], availableWidth * pref[c] / total));
 
     if (table.totalWidth() > availableWidth)
-        table.setOverflow(Overflow::Clip);
+        table.setOverflow(Overflow::Scroll);   // like GitHub: horizontal scrollbar
 
     layoutRows(table);
 }
\ No newline at end of file
```

---

## Functional languages

### Haskell

```haskell
{-# LANGUAGE LambdaCase #-}
module Main (main) where

import Data.Char (isDigit, isSpace)
import qualified Data.Map.Strict as Map

-- | A tiny RPN calculator with variables.
data Token = TNum Double | TOp Char | TVar String
  deriving (Show, Eq)

tokenize :: String -> [Token]
tokenize [] = []
tokenize s@(c:cs)
  | isSpace c = tokenize cs
  | isDigit c = let (n, rest) = span (\x -> isDigit x || x == '.') s
                in TNum (read n) : tokenize rest
  | c `elem` "+-*/" = TOp c : tokenize cs
  | otherwise = let (v, rest) = break isSpace s in TVar v : tokenize rest

eval :: Map.Map String Double -> [Token] -> Either String Double
eval env = go []
  where
    go [x] [] = Right x
    go st [] = Left ("stack has " ++ show (length st) ++ " items")
    go st (t:ts) = case t of
      TNum n -> go (n : st) ts
      TVar v -> maybe (Left ("unbound: " ++ v)) (\n -> go (n : st) ts) (Map.lookup v env)
      TOp o -> case st of
        (b:a:rest) -> go (apply o a b : rest) ts
        _          -> Left ("not enough operands for " ++ [o])
    apply = \case
      '+' -> (+); '-' -> (-); '*' -> (*); _ -> (/)

main :: IO ()
main = mapM_ (print . eval (Map.fromList [("pi", pi)]) . tokenize)
  ["3 4 + 2 *", "pi 2 /", "1 +", "x 1 +"]
```

### Elixir

```elixir
defmodule RateLimiter do
  @moduledoc """
  Token-bucket rate limiter backed by a GenServer.
  """
  use GenServer

  @type key :: String.t()

  def start_link(opts) do
    GenServer.start_link(__MODULE__, opts, name: Keyword.get(opts, :name, __MODULE__))
  end

  @spec allow?(key, pos_integer) :: boolean
  def allow?(key, cost \\ 1), do: GenServer.call(__MODULE__, {:take, key, cost})

  @impl true
  def init(opts) do
    state = %{capacity: opts[:capacity] || 10, refill_per_sec: opts[:refill] || 1, buckets: %{}}
    {:ok, state}
  end

  @impl true
  def handle_call({:take, key, cost}, _from, state) do
    now = System.monotonic_time(:millisecond)
    {tokens, last} = Map.get(state.buckets, key, {state.capacity, now})
    refilled = min(state.capacity, tokens + (now - last) / 1000 * state.refill_per_sec)

    if refilled >= cost do
      {:reply, true, put_in(state.buckets[key], {refilled - cost, now})}
    else
      {:reply, false, put_in(state.buckets[key], {refilled, now})}
    end
  end
end

# iex> RateLimiter.start_link(capacity: 3)
# iex> for _ <- 1..5, do: RateLimiter.allow?("user:42")
# [true, true, true, false, false]
```

### Clojure

```clojure
(ns markdown.toc
  (:require [clojure.string :as str]))

(defn slugify
  "GitHub-style anchor for a heading text."
  [text]
  (-> text
      str/lower-case
      (str/replace #"[^\p{L}\p{N}\s_-]" "")
      (str/replace #"\s" "-")))

(defn headings [markdown]
  (for [[_ hashes text] (re-seq #"(?m)^(#{1,6})\s+(.+?)\s*#*$" markdown)]
    {:level (count hashes) :text text :anchor (slugify text)}))

(defn toc [markdown]
  (->> (headings markdown)
       (map (fn [{:keys [level text anchor]}]
              (str (apply str (repeat (* 2 (dec level)) \space))
                   "- [" text "](#" anchor ")")))
       (str/join \newline)))

(comment
  (println (toc "# Title\n## Getting started\n### Install (Windows)\n## FAQ"))
  ;; - [Title](#title)
  ;;   - [Getting started](#getting-started)
  ;;     - [Install (Windows)](#install-windows)
  ;;   - [FAQ](#faq)
  )
```

---

## Documents

### LaTeX

```latex
\documentclass[11pt,a4paper]{article}
\usepackage[utf8]{inputenc}
\usepackage{amsmath,amssymb}
\usepackage{hyperref}

\title{Rendering Markdown with Direct2D}
\author{Jane Doe \and John Smith}
\date{\today}

\begin{document}
\maketitle

\section{Introduction}\label{sec:intro}
Line breaking follows the Unicode algorithm~\cite{uax14}; see Section~\ref{sec:intro}.

\begin{equation}
  w_{\text{col}} = \max\left(w_{\min},\; W \cdot \frac{p_i}{\sum_{j=1}^{n} p_j}\right)
\end{equation}

\begin{itemize}
  \item Text: \textbf{DirectWrite} with \emph{font fallback}
  \item Graphics: Direct2D, 100\% hardware accelerated
\end{itemize}

% TODO: add benchmark table
\end{document}
```

### Markdown inside a code block

Everything in this block must be shown **literally**: no heading, no bold, no checkbox, no clickable link, no alert. The outer fence uses four backticks, so the inner three-backtick fence is just content.

````markdown
# Heading inside a code block

This **bold** and *italic* text must be shown *literally*, with the asterisks.

- [ ] a task list item that must NOT become a checkbox
- [link](https://example.com) that must NOT be clickable

```python
print("a nested fence, shown literally too")
```

> [!NOTE]
> Not an alert: this is just text in a code block.
````

---

## Special cases

### No language

```
This block has no info string, so it gets no highlighting.
    Indentation inside is preserved exactly.
Symbols stay literal: <div> & "quotes" & 'apostrophes' & \backslashes\ & *stars* & _underscores_
```

### Unknown language

```foobar
This block claims to be "foobar", which no highlighter knows.
It must render exactly like a block without a language:
fn main() { println!("not highlighted"); }
```

### Info string with extra attributes

The language is the first word (`js`). The rest (`{1,3} title="x.js"`) comes from other tools such as Docusaurus and VitePress. github.com ignores it, and it must not break the block.

```js {1,3} title="x.js"
const a = 1;            // line 1 (some tools highlight it)
const b = 2;
const c = a + b;        // line 3 (some tools highlight it)
```

### Language aliases and capitalization

Common short aliases and capitalized names. Each one should highlight the same way as the canonical name.

```py
print("py alias")  # Python
```

```js
console.log("js alias");
```

```ts
const n: number = 42; // TypeScript
```

```sh
echo "sh alias" && exit 0
```

```yml
alias: yml
list: [1, 2, 3]
```

```rb
puts "rb alias"
```

```rs
fn main() { println!("rs alias"); }
```

```c++
std::cout << "c++ alias" << std::endl;
```

```cs
Console.WriteLine("cs alias");
```

```ps1
Write-Output "ps1 alias"
```

```Python
print("capitalized Python")
```

```JSON
{ "capitalized": "JSON" }
```

### Indented code block

    // An indented (4-space) code block: no language, no highlighting.
    int main(void)
    {
        return 0;
    }

### A very long line

The line below is about 400 characters long. It must not overflow the page. Depending on the viewer's design, the block either scrolls horizontally (github.com's behavior) or wraps.

```javascript
const LONG_LINE = { id: "c8f1e2d4-7b3a-4f9e-a1d2-3c4b5a6f7e8d", url: "https://example.com/api/v2/repositories/mity/md4c/contents/src/md4c.c?ref=0123456789abcdef0123456789abcdef01234567&recursive=true", tags: ["markdown", "parser", "commonmark", "gfm", "c", "fast", "compliant", "sax-like", "streaming", "no-dependencies"], description: "MD4C is a Markdown parser implementation in C, compliant to CommonMark specification.", stars: 868 };
```

A long line with no spaces at all (it can only break mid-token, or scroll):

```text
aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaEND
```

### TAB characters

Every indented line below starts with TABs, not spaces, and the aligned columns in the comments use TABs too. Tabs must expand to consistent tab stops (github.com uses a tab width of 8 unless the user changes it), and the columns should line up:

```c
struct entity {
	const char	*name;		/* entity name without & and ; */
	uint32_t	codepoint;	/* Unicode scalar value */
	uint8_t		len;		/* UTF-8 length */
};

static int lookup(const char *s)
{
	for (int i = 0; i < N; i++) {
		if (strcmp(s, table[i].name) == 0)
			return i;
	}
	return -1;
}
```

TAB-separated values (TSV):

```tsv
Name	Size	Modified	Type
README.md	3598	2026-05-10	Markdown
logo.png	2236	2026-05-10	PNG image
sample-large.md	178915	2026-05-11	Markdown
```

### Unicode, emoji, CJK, RTL and box drawing

A directory tree drawn with box-drawing characters. Vertical lines must connect without gaps, which requires the line height to match the font's box-drawing glyphs:

```text
mdview-wlx/
├── CMakeLists.txt
├── src/
│   ├── plugin.cpp          ← Lister entry points (ListLoadW, ListCloseWindow, …)
│   ├── render/
│   │   ├── layout.cpp
│   │   └── d2d_renderer.cpp
│   └── core/
│       └── emoji_table.inc
├── samples/
│   └── gfm/                ← you are here 📍
└── third_party/
    └── md4c/
```

A box diagram, with double lines and shading:

```text
╔══════════════════════╗      ┌───────────────┐      ┏━━━━━━━━━━━━━━━┓
║  Total Commander     ║─────▶│  mdview.wlx64 │─────▶┃  Direct2D     ┃
║  Lister (F3)         ║◀─────│  md4c parser  │      ┃  DirectWrite  ┃
╚══════════════════════╝      └───────────────┘      ┗━━━━━━━━━━━━━━━┛
 ░░░░ light  ▒▒▒▒ medium  ▓▓▓▓ dark  ████ full   ▁▂▃▄▅▆▇█ bars
```

Mixed scripts in code. CJK characters are double-width in most monospace fonts, so the trailing `|` characters show whether the columns stay aligned:

```python
greetings = {
    "en": "Hello, world!",        # English     |
    "ru": "Привет, мир!",         # Russian     |
    "uk": "Привіт, світе!",       # Ukrainian   |
    "zh": "你好，世界！",           # Chinese     |
    "ja": "こんにちは世界",         # Japanese    |
    "ko": "안녕하세요, 세계!",       # Korean      |
    "ar": "مرحبا بالعالم",         # Arabic (RTL)
    "he": "שלום עולם",             # Hebrew (RTL)
    "hi": "नमस्ते दुनिया",           # Hindi       |
    "emoji": "👋🌍 ✨🚀 👨‍👩‍👧‍👦",   # emoji + ZWJ |
}
```

### Inline code with special characters

- Backticks inside inline code: `` `code` `` and ``` ``double`` ``` and `` a`b ``
- HTML must be escaped, not rendered: `<div class="x">&amp;</div>` and `<script>alert(1)</script>`
- Entities stay literal: `&copy; &#169; &lt;`
- Operators: `a && b || !c`, `x <= y >= z`, `i++ + ++i`, `p->next`, `a ?? b`, `x => x * 2`
- Quotes: `"double"` and `'single'` and `` `back` ``
- Markdown characters: `*not emphasis*`, `_not emphasis_`, `[not](a link)`, `# not a heading`
- Unicode: `λx. x → x`, `Привет`, `你好`, `👍`
- Space stripping: `` ` `` shows a single backtick (one space is stripped from each side), and `  two spaces  ` keeps one space on each side
- Very long inline code: `this_is_a_very_long_identifier_that_may_need_to_break_across_lines_in_a_narrow_window_0123456789_abcdefghijklmnopqrstuvwxyz`

### 300-line code block

A generated C source file of exactly 300 lines (a lookup table of HTML5 named character references). Use it to judge scrolling performance and highlighting speed on long blocks.

```c
/*
 * entities.c - HTML5 named character references (subset).
 *
 * GENERATED FILE - DO NOT EDIT.
 * Source: https://html.spec.whatwg.org/entities.json
 * Regenerate with: python tools/gen_entities.py > src/entities.c
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const char *name;     /* without the leading '&', with the trailing ';' */
    uint32_t    cp1;      /* first code point */
    uint32_t    cp2;      /* second code point, or 0 */
} entity_t;

static const entity_t k_entities[] = {
    { "AElig;",                          0x000C6, 0x0000 },  /* U+00C6 */
    { "Alpha;",                          0x00391, 0x0000 },  /* U+0391 */
    { "Atilde;",                         0x000C3, 0x0000 },  /* U+00C3 */
    { "Beta;",                           0x00392, 0x0000 },  /* U+0392 */
    { "Cap;",                            0x022D2, 0x0000 },  /* U+22D2 */
    { "Cedilla;",                        0x000B8, 0x0000 },  /* U+00B8 */
    { "CloseCurlyDoubleQuote;",          0x0201D, 0x0000 },  /* U+201D */
    { "Coproduct;",                      0x02210, 0x0000 },  /* U+2210 */
    { "DScy;",                           0x00405, 0x0000 },  /* U+0405 */
    { "Dfr;",                            0x1D507, 0x0000 },  /* U+1D507 */
    { "Dopf;",                           0x1D53B, 0x0000 },  /* U+1D53B */
    { "DoubleLeftTee;",                  0x02AE4, 0x0000 },  /* U+2AE4 */
    { "DoubleVerticalBar;",              0x02225, 0x0000 },  /* U+2225 */
    { "DownRightTeeVector;",             0x0295F, 0x0000 },  /* U+295F */
    { "ENG;",                            0x0014A, 0x0000 },  /* U+014A */
    { "Element;",                        0x02208, 0x0000 },  /* U+2208 */
    { "Equilibrium;",                    0x021CC, 0x0000 },  /* U+21CC */
    { "Ffr;",                            0x1D509, 0x0000 },  /* U+1D509 */
    { "Gamma;",                          0x00393, 0x0000 },  /* U+0393 */
    { "Gg;",                             0x022D9, 0x0000 },  /* U+22D9 */
    { "Gscr;",                           0x1D4A2, 0x0000 },  /* U+1D4A2 */
    { "Hopf;",                           0x0210D, 0x0000 },  /* U+210D */
    { "Iacute;",                         0x000CD, 0x0000 },  /* U+00CD */
    { "Implies;",                        0x021D2, 0x0000 },  /* U+21D2 */
    { "Iota;",                           0x00399, 0x0000 },  /* U+0399 */
    { "Jscr;",                           0x1D4A5, 0x0000 },  /* U+1D4A5 */
    { "Kfr;",                            0x1D50E, 0x0000 },  /* U+1D50E */
    { "Larr;",                           0x0219E, 0x0000 },  /* U+219E */
    { "LeftCeiling;",                    0x02308, 0x0000 },  /* U+2308 */
    { "LeftTeeArrow;",                   0x021A4, 0x0000 },  /* U+21A4 */
    { "LeftVector;",                     0x021BC, 0x0000 },  /* U+21BC */
    { "LessSlantEqual;",                 0x02A7D, 0x0000 },  /* U+2A7D */
    { "Longleftarrow;",                  0x027F8, 0x0000 },  /* U+27F8 */
    { "Lstrok;",                         0x00141, 0x0000 },  /* U+0141 */
    { "Mscr;",                           0x02133, 0x0000 },  /* U+2133 */
    { "NegativeThickSpace;",             0x0200B, 0x0000 },  /* U+200B */
    { "Nopf;",                           0x02115, 0x0000 },  /* U+2115 */
    { "NotGreater;",                     0x0226F, 0x0000 },  /* U+226F */
    { "NotHumpEqual;",                   0x0224F, 0x0338 },  /* U+224F U+0338 */
    { "NotLessTilde;",                   0x02274, 0x0000 },  /* U+2274 */
    { "NotRightTriangleBar;",            0x029D0, 0x0338 },  /* U+29D0 U+0338 */
    { "NotSucceedsEqual;",               0x02AB0, 0x0338 },  /* U+2AB0 U+0338 */
    { "NotTildeTilde;",                  0x02249, 0x0000 },  /* U+2249 */
    { "Odblac;",                         0x00150, 0x0000 },  /* U+0150 */
    { "Or;",                             0x02A54, 0x0000 },  /* U+2A54 */
    { "OverBracket;",                    0x023B4, 0x0000 },  /* U+23B4 */
    { "Popf;",                           0x02119, 0x0000 },  /* U+2119 */
    { "Proportion;",                     0x02237, 0x0000 },  /* U+2237 */
    { "REG;",                            0x000AE, 0x0000 },  /* U+00AE */
    { "Re;",                             0x0211C, 0x0000 },  /* U+211C */
    { "RightArrowLeftArrow;",            0x021C4, 0x0000 },  /* U+21C4 */
    { "RightTeeArrow;",                  0x021A6, 0x0000 },  /* U+21A6 */
    { "RightVector;",                    0x021C0, 0x0000 },  /* U+21C0 */
    { "SHCHcy;",                         0x00429, 0x0000 },  /* U+0429 */
    { "Scy;",                            0x00421, 0x0000 },  /* U+0421 */
    { "Sqrt;",                           0x0221A, 0x0000 },  /* U+221A */
    { "Sscr;",                           0x1D4AE, 0x0000 },  /* U+1D4AE */
    { "SuchThat;",                       0x0220B, 0x0000 },  /* U+220B */
    { "TSHcy;",                          0x0040B, 0x0000 },  /* U+040B */
    { "Theta;",                          0x00398, 0x0000 },  /* U+0398 */
    { "Tscr;",                           0x1D4AF, 0x0000 },  /* U+1D4AF */
    { "Ucy;",                            0x00423, 0x0000 },  /* U+0423 */
    { "Union;",                          0x022C3, 0x0000 },  /* U+22C3 */
    { "UpEquilibrium;",                  0x0296E, 0x0000 },  /* U+296E */
    { "Uring;",                          0x0016E, 0x0000 },  /* U+016E */
    { "Vdashl;",                         0x02AE6, 0x0000 },  /* U+2AE6 */
    { "Vfr;",                            0x1D519, 0x0000 },  /* U+1D519 */
    { "Xfr;",                            0x1D51B, 0x0000 },  /* U+1D51B */
    { "Ycirc;",                          0x00176, 0x0000 },  /* U+0176 */
    { "Zcy;",                            0x00417, 0x0000 },  /* U+0417 */
    { "abreve;",                         0x00103, 0x0000 },  /* U+0103 */
    { "afr;",                            0x1D51E, 0x0000 },  /* U+1D51E */
    { "and;",                            0x02227, 0x0000 },  /* U+2227 */
    { "angmsdaa;",                       0x029A8, 0x0000 },  /* U+29A8 */
    { "angrtvb;",                        0x022BE, 0x0000 },  /* U+22BE */
    { "apE;",                            0x02A70, 0x0000 },  /* U+2A70 */
    { "ast;",                            0x0002A, 0x0000 },  /* U+002A */
    { "backcong;",                       0x0224C, 0x0000 },  /* U+224C */
    { "bbrktbrk;",                       0x023B6, 0x0000 },  /* U+23B6 */
    { "bernou;",                         0x0212C, 0x0000 },  /* U+212C */
    { "bigoplus;",                       0x02A01, 0x0000 },  /* U+2A01 */
    { "bkarow;",                         0x0290D, 0x0000 },  /* U+290D */
    { "blk12;",                          0x02592, 0x0000 },  /* U+2592 */
    { "bottom;",                         0x022A5, 0x0000 },  /* U+22A5 */
    { "boxHU;",                          0x02569, 0x0000 },  /* U+2569 */
    { "boxVL;",                          0x02563, 0x0000 },  /* U+2563 */
    { "boxdl;",                          0x02510, 0x0000 },  /* U+2510 */
    { "boxtimes;",                       0x022A0, 0x0000 },  /* U+22A0 */
    { "boxvh;",                          0x0253C, 0x0000 },  /* U+253C */
    { "bsim;",                           0x0223D, 0x0000 },  /* U+223D */
    { "bumpe;",                          0x0224F, 0x0000 },  /* U+224F */
    { "capdot;",                         0x02A40, 0x0000 },  /* U+2A40 */
    { "ccupssm;",                        0x02A50, 0x0000 },  /* U+2A50 */
    { "check;",                          0x02713, 0x0000 },  /* U+2713 */
    { "circledR;",                       0x000AE, 0x0000 },  /* U+00AE */
    { "clubs;",                          0x02663, 0x0000 },  /* U+2663 */
    { "compfn;",                         0x02218, 0x0000 },  /* U+2218 */
    { "copysr;",                         0x02117, 0x0000 },  /* U+2117 */
    { "ctdot;",                          0x022EF, 0x0000 },  /* U+22EF */
    { "cupcap;",                         0x02A46, 0x0000 },  /* U+2A46 */
    { "curlyeqsucc;",                    0x022DF, 0x0000 },  /* U+22DF */
    { "cwint;",                          0x02231, 0x0000 },  /* U+2231 */
    { "dashv;",                          0x022A3, 0x0000 },  /* U+22A3 */
    { "deg;",                            0x000B0, 0x0000 },  /* U+00B0 */
    { "diamondsuit;",                    0x02666, 0x0000 },  /* U+2666 */
    { "divonx;",                         0x022C7, 0x0000 },  /* U+22C7 */
    { "dotminus;",                       0x02238, 0x0000 },  /* U+2238 */
    { "drbkarow;",                       0x02910, 0x0000 },  /* U+2910 */
    { "dtrif;",                          0x025BE, 0x0000 },  /* U+25BE */
    { "eacute;",                         0x000E9, 0x0000 },  /* U+00E9 */
    { "efDot;",                          0x02252, 0x0000 },  /* U+2252 */
    { "els;",                            0x02A95, 0x0000 },  /* U+2A95 */
    { "emsp;",                           0x02003, 0x0000 },  /* U+2003 */
    { "epsilon;",                        0x003B5, 0x0000 },  /* U+03B5 */
    { "equest;",                         0x0225F, 0x0000 },  /* U+225F */
    { "eta;",                            0x003B7, 0x0000 },  /* U+03B7 */
    { "fallingdotseq;",                  0x02252, 0x0000 },  /* U+2252 */
    { "flat;",                           0x0266D, 0x0000 },  /* U+266D */
    { "frac12;",                         0x000BD, 0x0000 },  /* U+00BD */
    { "frac34;",                         0x000BE, 0x0000 },  /* U+00BE */
    { "fscr;",                           0x1D4BB, 0x0000 },  /* U+1D4BB */
    { "gcirc;",                          0x0011D, 0x0000 },  /* U+011D */
    { "gescc;",                          0x02AA9, 0x0000 },  /* U+2AA9 */
    { "ggg;",                            0x022D9, 0x0000 },  /* U+22D9 */
    { "gnapprox;",                       0x02A8A, 0x0000 },  /* U+2A8A */
    { "gsime;",                          0x02A8E, 0x0000 },  /* U+2A8E */
    { "gtrapprox;",                      0x02A86, 0x0000 },  /* U+2A86 */
    { "hArr;",                           0x021D4, 0x0000 },  /* U+21D4 */
    { "hbar;",                           0x0210F, 0x0000 },  /* U+210F */
    { "hoarr;",                          0x021FF, 0x0000 },  /* U+21FF */
    { "hstrok;",                         0x00127, 0x0000 },  /* U+0127 */
    { "iff;",                            0x021D4, 0x0000 },  /* U+21D4 */
    { "imacr;",                          0x0012B, 0x0000 },  /* U+012B */
    { "incare;",                         0x02105, 0x0000 },  /* U+2105 */
    { "intprod;",                        0x02A3C, 0x0000 },  /* U+2A3C */
    { "isin;",                           0x02208, 0x0000 },  /* U+2208 */
    { "iuml;",                           0x000EF, 0x0000 },  /* U+00EF */
    { "jukcy;",                          0x00454, 0x0000 },  /* U+0454 */
    { "kopf;",                           0x1D55C, 0x0000 },  /* U+1D55C */
    { "lacute;",                         0x0013A, 0x0000 },  /* U+013A */
    { "laquo;",                          0x000AB, 0x0000 },  /* U+00AB */
    { "larrtl;",                         0x021A2, 0x0000 },  /* U+21A2 */
    { "lbrack;",                         0x0005B, 0x0000 },  /* U+005B */
    { "ldca;",                           0x02936, 0x0000 },  /* U+2936 */
    { "leftarrowtail;",                  0x021A2, 0x0000 },  /* U+21A2 */
    { "leg;",                            0x022DA, 0x0000 },  /* U+22DA */
    { "lesg;",                           0x022DA, 0xFE00 },  /* U+22DA U+FE00 */
    { "lfisht;",                         0x0297C, 0x0000 },  /* U+297C */
    { "ljcy;",                           0x00459, 0x0000 },  /* U+0459 */
    { "lmoustache;",                     0x023B0, 0x0000 },  /* U+23B0 */
    { "loarr;",                          0x021FD, 0x0000 },  /* U+21FD */
    { "lopar;",                          0x02985, 0x0000 },  /* U+2985 */
    { "lpar;",                           0x00028, 0x0000 },  /* U+0028 */
    { "lsaquo;",                         0x02039, 0x0000 },  /* U+2039 */
    { "lstrok;",                         0x00142, 0x0000 },  /* U+0142 */
    { "ltrPar;",                         0x02996, 0x0000 },  /* U+2996 */
    { "mDDot;",                          0x0223A, 0x0000 },  /* U+223A */
    { "mapstoup;",                       0x021A5, 0x0000 },  /* U+21A5 */
    { "micro;",                          0x000B5, 0x0000 },  /* U+00B5 */
    { "mlcp;",                           0x02ADB, 0x0000 },  /* U+2ADB */
    { "mu;",                             0x003BC, 0x0000 },  /* U+03BC */
    { "nLt;",                            0x0226A, 0x20D2 },  /* U+226A U+20D2 */
    { "napE;",                           0x02A70, 0x0338 },  /* U+2A70 U+0338 */
    { "nbump;",                          0x0224E, 0x0338 },  /* U+224E U+0338 */
    { "ndash;",                          0x02013, 0x0000 },  /* U+2013 */
    { "nesear;",                         0x02928, 0x0000 },  /* U+2928 */
    { "ngeqslant;",                      0x02A7E, 0x0338 },  /* U+2A7E U+0338 */
    { "ni;",                             0x0220B, 0x0000 },  /* U+220B */
    { "nle;",                            0x02270, 0x0000 },  /* U+2270 */
    { "nlt;",                            0x0226E, 0x0000 },  /* U+226E */
    { "notindot;",                       0x022F5, 0x0338 },  /* U+22F5 U+0338 */
    { "nparallel;",                      0x02226, 0x0000 },  /* U+2226 */
    { "npreceq;",                        0x02AAF, 0x0338 },  /* U+2AAF U+0338 */
    { "nsccue;",                         0x022E1, 0x0000 },  /* U+22E1 */
    { "nsmid;",                          0x02224, 0x0000 },  /* U+2224 */
    { "nsubseteqq;",                     0x02AC5, 0x0338 },  /* U+2AC5 U+0338 */
    { "ntgl;",                           0x02279, 0x0000 },  /* U+2279 */
    { "num;",                            0x00023, 0x0000 },  /* U+0023 */
    { "nvinfin;",                        0x029DE, 0x0000 },  /* U+29DE */
    { "nwArr;",                          0x021D6, 0x0000 },  /* U+21D6 */
    { "ocirc;",                          0x000F4, 0x0000 },  /* U+00F4 */
    { "ofcir;",                          0x029BF, 0x0000 },  /* U+29BF */
    { "olcir;",                          0x029BE, 0x0000 },  /* U+29BE */
    { "oopf;",                           0x1D560, 0x0000 },  /* U+1D560 */
    { "orderof;",                        0x02134, 0x0000 },  /* U+2134 */
    { "osol;",                           0x02298, 0x0000 },  /* U+2298 */
    { "parallel;",                       0x02225, 0x0000 },  /* U+2225 */
    { "pertenk;",                        0x02031, 0x0000 },  /* U+2031 */
    { "piv;",                            0x003D6, 0x0000 },  /* U+03D6 */
    { "plusdu;",                         0x02A25, 0x0000 },  /* U+2A25 */
    { "pr;",                             0x0227A, 0x0000 },  /* U+227A */
    { "preceq;",                         0x02AAF, 0x0000 },  /* U+2AAF */
    { "prnsim;",                         0x022E8, 0x0000 },  /* U+22E8 */
    { "prurel;",                         0x022B0, 0x0000 },  /* U+22B0 */
    { "quaternions;",                    0x0210D, 0x0000 },  /* U+210D */
    { "rBarr;",                          0x0290F, 0x0000 },  /* U+290F */
    { "rangle;",                         0x027E9, 0x0000 },  /* U+27E9 */
    { "rarrlp;",                         0x021AC, 0x0000 },  /* U+21AC */
    { "rbarr;",                          0x0290D, 0x0000 },  /* U+290D */
    { "rceil;",                          0x02309, 0x0000 },  /* U+2309 */
    { "real;",                           0x0211C, 0x0000 },  /* U+211C */
    { "rhard;",                          0x021C1, 0x0000 },  /* U+21C1 */
    { "rightharpoonup;",                 0x021C0, 0x0000 },  /* U+21C0 */
    { "rlhar;",                          0x021CC, 0x0000 },  /* U+21CC */
    { "ropar;",                          0x02986, 0x0000 },  /* U+2986 */
    { "rscr;",                           0x1D4C7, 0x0000 },  /* U+1D4C7 */
    { "rtrif;",                          0x025B8, 0x0000 },  /* U+25B8 */
    { "scap;",                           0x02AB8, 0x0000 },  /* U+2AB8 */
    { "scpolint;",                       0x02A13, 0x0000 },  /* U+2A13 */
    { "searr;",                          0x02198, 0x0000 },  /* U+2198 */
    { "sfrown;",                         0x02322, 0x0000 },  /* U+2322 */
    { "sigmaf;",                         0x003C2, 0x0000 },  /* U+03C2 */
    { "simlE;",                          0x02A9F, 0x0000 },  /* U+2A9F */
    { "smile;",                          0x02323, 0x0000 },  /* U+2323 */
    { "sopf;",                           0x1D564, 0x0000 },  /* U+1D564 */
    { "sqsube;",                         0x02291, 0x0000 },  /* U+2291 */
    { "square;",                         0x025A1, 0x0000 },  /* U+25A1 */
    { "starf;",                          0x02605, 0x0000 },  /* U+2605 */
    { "subedot;",                        0x02AC3, 0x0000 },  /* U+2AC3 */
    { "subsetneq;",                      0x0228A, 0x0000 },  /* U+228A */
    { "succnapprox;",                    0x02ABA, 0x0000 },  /* U+2ABA */
    { "sup3;",                           0x000B3, 0x0000 },  /* U+00B3 */
    { "suplarr;",                        0x0297B, 0x0000 },  /* U+297B */
    { "supsetneq;",                      0x0228B, 0x0000 },  /* U+228B */
    { "swnwar;",                         0x0292A, 0x0000 },  /* U+292A */
    { "tdot;",                           0x020DB, 0x0000 },  /* U+20DB */
    { "thicksim;",                       0x0223C, 0x0000 },  /* U+223C */
    { "timesd;",                         0x02A30, 0x0000 },  /* U+2A30 */
    { "tosa;",                           0x02929, 0x0000 },  /* U+2929 */
    { "trianglerighteq;",                0x022B5, 0x0000 },  /* U+22B5 */
    { "tscr;",                           0x1D4C9, 0x0000 },  /* U+1D4C9 */
    { "uacute;",                         0x000FA, 0x0000 },  /* U+00FA */
    { "udhar;",                          0x0296E, 0x0000 },  /* U+296E */
    { "ulcrop;",                         0x0230F, 0x0000 },  /* U+230F */
    { "upharpoonright;",                 0x021BE, 0x0000 },  /* U+21BE */
    { "urcrop;",                         0x0230E, 0x0000 },  /* U+230E */
    { "uuml;",                           0x000FC, 0x0000 },  /* U+00FC */
    { "varkappa;",                       0x003F0, 0x0000 },  /* U+03F0 */
    { "varsubsetneqq;",                  0x02ACB, 0xFE00 },  /* U+2ACB U+FE00 */
    { "vee;",                            0x02228, 0x0000 },  /* U+2228 */
    { "vnsup;",                          0x02283, 0x20D2 },  /* U+2283 U+20D2 */
    { "vzigzag;",                        0x0299A, 0x0000 },  /* U+299A */
    { "wp;",                             0x02118, 0x0000 },  /* U+2118 */
    { "xhArr;",                          0x027FA, 0x0000 },  /* U+27FA */
    { "xopf;",                           0x1D569, 0x0000 },  /* U+1D569 */
    { "xvee;",                           0x022C1, 0x0000 },  /* U+22C1 */
    { "yicy;",                           0x00457, 0x0000 },  /* U+0457 */
    { "zeetrf;",                         0x02128, 0x0000 },  /* U+2128 */
};

#define ENTITY_COUNT (sizeof(k_entities) / sizeof(k_entities[0]))

/* Binary search; k_entities is sorted by strcmp(). */
const entity_t *entity_lookup(const char *name, size_t len)
{
    size_t lo = 0, hi = ENTITY_COUNT;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strncmp(name, k_entities[mid].name, len);
        if (cmp == 0 && k_entities[mid].name[len] != '\0')
            cmp = -1;
        if (cmp == 0)
            return &k_entities[mid];
        if (cmp < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

/* Encode a code point as UTF-8; returns the number of bytes written. */
size_t entity_utf8(uint32_t cp, char out[4])
{
    if (cp < 0x80)    { out[0] = (char)cp; return 1; }
    if (cp < 0x800)   { out[0] = (char)(0xC0 | cp >> 6);  out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { out[0] = (char)(0xE0 | cp >> 12); out[1] = (char)(0x80 | (cp >> 6 & 0x3F));
                        out[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    out[0] = (char)(0xF0 | cp >> 18); out[1] = (char)(0x80 | (cp >> 12 & 0x3F));
    out[2] = (char)(0x80 | (cp >> 6 & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}
```
