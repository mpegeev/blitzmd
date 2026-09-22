// Standalone test / benchmark for src/core/highlight.{h,cpp}.
//
// Build (from PowerShell):
//   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\vcrun.ps1 cl /nologo /std:c++20 /W4 /WX
//     /permissive- /EHsc /utf-8 /GR- /O2 /Fe:out\hltest\highlight_test.exe /Fo:out\hltest\
//     tests\highlight_test.cpp src\core\highlight.cpp
//
// Usage: highlight_test [-q] [-dump <snippet-name>]
//   -q          do not print the token dump of every snippet
//   -dump name  print the token dump of one snippet only
// Exit code 0 only if every assertion passes.
#include "../src/core/highlight.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

using mdv::HighlightCode;
using mdv::HighlightSpan;
using mdv::IsKnownLanguage;
using mdv::TokenKind;
using K = TokenKind;

namespace {

int g_failures = 0;
int g_checks = 0;

const char* KindName(TokenKind k) {
    static const char* const kNames[] = {"Plain",     "Comment",  "Keyword",  "String",    "Number",
                                         "Constant",  "Function", "Type",     "Variable",  "Tag",
                                         "Attribute", "Property", "Meta",     "Regex",     "Escape",
                                         "Inserted",  "Deleted",  "DiffRange", "Heading"};
    const size_t i = static_cast<size_t>(k);
    return i < sizeof(kNames) / sizeof(kNames[0]) ? kNames[i] : "???";
}

// UTF-16 -> printable UTF-8 with escapes for control characters.
std::string Printable(std::wstring_view w, size_t maxLen = 200) {
    std::string out;
    size_t count = 0;
    for (size_t i = 0; i < w.size(); ++i) {
        if (++count > maxLen) {
            out += "...";
            break;
        }
        unsigned c = static_cast<unsigned>(w[i]);
        if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c < 0x20 || (c >= 0xD800 && c <= 0xDFFF)) {
            // Keep valid surrogate pairs, escape lone ones.
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size() && static_cast<unsigned>(w[i + 1]) >= 0xDC00 &&
                static_cast<unsigned>(w[i + 1]) <= 0xDFFF) {
                const unsigned cp = 0x10000 + ((c - 0xD800) << 10) + (static_cast<unsigned>(w[i + 1]) - 0xDC00);
                ++i;
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "\\u%04X", c);
                out += buf;
            }
        } else if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

void Fail(const char* fmt, const std::string& a, const std::string& b = std::string()) {
    ++g_failures;
    std::printf("FAIL: ");
    std::printf(fmt, a.c_str(), b.c_str());
    std::printf("\n");
}

// Verifies the output invariants for spans appended after index `base`.
bool CheckInvariants(const std::vector<HighlightSpan>& v, size_t base, size_t n, const std::string& ctx) {
    ++g_checks;
    uint64_t prevEnd = 0;
    for (size_t k = base; k < v.size(); ++k) {
        const HighlightSpan& sp = v[k];
        const char* why = nullptr;
        if (sp.length == 0) why = "zero length";
        else if (sp.kind == K::Plain) why = "Plain span";
        else if (sp.kind >= K::Count) why = "bad kind";
        else if (sp.start < prevEnd) why = "overlap / not ascending";
        else if (static_cast<uint64_t>(sp.start) + sp.length > n) why = "out of bounds";
        if (why) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "span #%zu [%u,+%u) %s (n=%zu)", k - base, sp.start, sp.length, why, n);
            Fail("invariant violated in %s: %s", ctx, buf);
            return false;
        }
        prevEnd = static_cast<uint64_t>(sp.start) + sp.length;
    }
    return true;
}

bool RunChecked(std::wstring_view code, std::wstring_view lang, std::vector<HighlightSpan>& out,
                const std::string& ctx) {
    out.clear();
    const bool ok = HighlightCode(code, lang, out);
    if (!ok) {
        Fail("HighlightCode returned false for %s", ctx);
        return false;
    }
    return CheckInvariants(out, 0, code.size(), ctx);
}

// ---------------------------------------------------------------------------
// Snippets
// ---------------------------------------------------------------------------
struct Snippet {
    const char* name;
    const wchar_t* lang;
    const wchar_t* code;
};

const Snippet kSnippets[] = {
{"cpp", L"cpp", LR"~~(#include <iostream>
#include "config.h"
#define MAX_SIZE 1024
// Line comment
/* Block
   comment */
namespace demo {
template <typename T>
class Widget : public Base {
public:
    explicit Widget(int count) : count_(count) {}
    virtual ~Widget() override = default;
    static constexpr double kRatio = 1.5e-3;
    std::string name() const { return u8"widget\t\x41\u00e9"; }
private:
    int count_ = 0x1F'FF;
    char c = '\n';
    const char* raw = R"delim(raw "string" \n)delim";
};
enum class Color : uint8_t { Red, Green = 2 };
struct Point { float x, y; };
}  // namespace demo

int main(int argc, char** argv) {
    auto w = new demo::Widget<int>(42);
    if (argc > 1 && argv[1] != nullptr) {
        std::cout << "Hello, " << argv[1] << std::endl;
    }
    for (size_t i = 0; i < MAX_SIZE; ++i) co_await something();
    bool ok = true || false;
    return 0;
}
)~~"},
{"python", L"python", LR"~~(#!/usr/bin/env python3
"""Module docstring."""
import os
from typing import List, Optional

MAX_RETRIES = 3

@dataclass(frozen=True)
class Point(Base):
    x: int = 0
    y: float = 1.5e10

    def distance(self, other: "Point") -> float:
        return ((self.x - other.x) ** 2 + (self.y - other.y) ** 2) ** 0.5

async def main(argv: List[str]) -> None:
    name = f"Hello {argv[0]!r}, {len(argv):>5} args"
    raw = r'C:\path\n'
    data = b"\x00\xff"
    if name is None or not argv:
        print('empty', end='\n')
    for i in range(10):
        pass
    lam = lambda x: x + 1
    return True
# comment
text = '''multi
line'''
)~~"},
{"js", L"js", LR"~~(// JavaScript sample
import { readFile } from 'fs/promises';
const re = /ab+c\/[a-z]*/gi;
const tpl = `Hello ${user.name}, you have ${count + 1} messages\n`;
export default class Foo extends Bar {
  #priv = 0x10;
  constructor(a, b = 42) { super(a); this.b = b; }
  async load(path) {
    const data = await readFile(path, "utf8");
    return data.split('\n').map((l) => l.trim()).filter(Boolean);
  }
}
function add(x, y) { return x / y / 2; }
let v = null ?? undefined;
if (typeof v === 'undefined' && !NaN) console.log(JSON.stringify({ a: 1 }));
/* block */
const MAX_COUNT = 1_000_000n;
)~~"},
{"ts", L"TypeScript", LR"~~(interface User { id: number; name: string; tags?: string[] }
type Handler<T> = (event: T) => Promise<void>;
enum Direction { Up = 1, Down }
@Component({ selector: 'app' })
export class UserService implements OnInit {
  private readonly cache = new Map<string, User>();
  constructor(private http: HttpClient) {}
  public async get(id: number): Promise<User | undefined> {
    const key = `user:${id}`;
    return this.cache.get(key) as User;
  }
}
declare module "foo" { export function bar(x: unknown): never; }
)~~"},
{"rust", L"rust", LR"~~(// Rust sample
use std::collections::HashMap;
#[derive(Debug, Clone)]
pub struct Config<'a> {
    name: &'a str,
    values: HashMap<String, i32>,
}
impl<'a> Config<'a> {
    pub fn new(name: &'a str) -> Self {
        let raw = r#"raw "quoted" string"#;
        let bytes = b"bytes\n";
        let ch = 'x';
        let esc = '\u{1F600}';
        println!("name = {} {}", name, raw.len());
        Self { name, values: HashMap::new() }
    }
}
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut v: Vec<u64> = vec![1, 2, 3_000];
    /* block /* nested */ still comment */
    'outer: for x in v.iter_mut() { if *x > 2 { break 'outer; } }
    let f = 1.5e3_f64;
    Ok(())
}
)~~"},
{"go", L"go", LR"~~(package main

import (
	"fmt"
	"strings"
)

// Greeter greets.
type Greeter struct {
	Name  string
	Count int
}

func (g *Greeter) Greet(msg string) error {
	raw := `raw \n string`
	r := 'x'
	fmt.Printf("Hello, %s! %d\n", g.Name, len(msg))
	if g == nil || !strings.HasPrefix(msg, "x") {
		return nil
	}
	for i := 0; i < 10; i++ {
		defer func() { _ = recover() }()
	}
	var ok bool = true
	_, _, _ = raw, r, ok
	return fmt.Errorf("err: %v", 0x1F)
}
)~~"},
{"csharp", L"c#", LR"~~(using System;
using System.Collections.Generic;
#region Demo
namespace Demo.App
{
    [Serializable]
    public sealed class Person : IComparable<Person>
    {
        public string Name { get; init; } = "";
        private readonly int _age = 42;
        public static async Task Main(string[] args)
        {
            var list = new List<Person>();
            string path = @"C:\temp\file.txt";
            string msg = $"Hello {args[0]}, {{literal}} {list.Count:D2}";
            string raw = """raw "text" """;
            char c = '\t';
            Console.WriteLine(msg + path);
            if (list is null) throw new ArgumentNullException(nameof(list));
            foreach (var p in list) { await Task.Delay(10); }
            double d = 1.5e3d; decimal m = 10.5m;
        }
        public int CompareTo(Person? other) => _age.CompareTo(other?._age ?? 0);
    }
}
#endregion
)~~"},
{"java", L"java", LR"~~(package com.example;

import java.util.*;

/** Javadoc comment */
@SuppressWarnings("unchecked")
public class Main extends Base implements Runnable {
    private static final int MAX_VALUE = 100;
    private final Map<String, Integer> counts = new HashMap<>();

    @Override
    public void run() {
        String s = "text \"quoted\"";
        char c = 'c';
        long big = 123_456L;
        double d = .5e-3;
        if (s != null && !s.isEmpty()) {
            System.out.println(s.length());
        }
        String block = """
            text block
            """;
    }
}
)~~"},
{"sql", L"sql", LR"~~(-- Query sample
SELECT u.id, u.name, COUNT(o.id) AS order_count
FROM users u
LEFT JOIN orders o ON o.user_id = u.id
WHERE u.created_at >= '2024-01-01' AND u.status <> 'it''s'
GROUP BY u.id, u.name
HAVING COUNT(o.id) > 5
ORDER BY order_count DESC
LIMIT 10;
/* create table */
CREATE TABLE IF NOT EXISTS "orders" (
    id BIGSERIAL PRIMARY KEY,
    amount NUMERIC(10, 2) NOT NULL DEFAULT 0.00,
    note VARCHAR(255) NULL
);
insert into t (a, b) values (@param, :name), ($1, N'unicode');
UPDATE t SET flag = TRUE WHERE id IN (1, 2, 3);
)~~"},
{"bash", L"bash", LR"~~(#!/usr/bin/env bash
# Build script
set -euo pipefail
readonly VERSION="1.2.3"
export PATH="$HOME/bin:$PATH"

function build() {
  local target=${1:-release}
  echo "Building $target (v${VERSION})..."
  if [[ -d build ]]; then
    rm -rf build/*
  fi
  for f in src/*.c; do
    gcc -O2 -c "$f" -o "${f%.c}.o" || exit 1
  done
  count=$(ls | wc -l)
  echo 'single $quoted' # trailing comment
}

case "$1" in
  clean) rm -rf build ;;
  *) build "$@" ;;
esac
cat <<EOF > out.txt
Hello $USER
EOF
echo done
)~~"},
{"console", L"console", LR"~~($ npm install --save left-pad
added 1 package in 2.1s
$ echo "hello $USER" | tr a-z A-Z
HELLO MIKHAIL
user@host:~/project$ git status
On branch main
PS C:\Users\me> Get-ChildItem -Path . -Recurse | Select-Object Name
C:\>dir /b
# apt-get update
)~~"},
{"powershell", L"powershell", LR"~~(# PowerShell sample
<# Block
   comment #>
param([string]$Name = "World", [int]$Count = 3)
function Get-Greeting {
    param([Parameter(Mandatory)][string]$Who)
    return "Hello, $Who! `n Today is $(Get-Date)"
}
$items = Get-ChildItem -Path $env:TEMP -Filter *.log
foreach ($item in $items) {
    if ($item.Length -gt 1MB -and $true) {
        Write-Host 'Big file:' $item.Name -ForegroundColor Red
    }
}
$hash = @{ Key = 'value'; Num = 42 }
[System.IO.File]::WriteAllText("out.txt", $hash.Key)
$here = @"
Here-string with $Name
"@
)~~"},
{"batch", L"bat", LR"~~(@echo off
REM Batch sample
:: another comment
setlocal enabledelayedexpansion
set NAME=World
set /a COUNT=%COUNT%+1
if exist "%~dp0config.ini" (
    echo Found config for %NAME%
) else (
    goto :error
)
for %%i in (*.txt) do echo %%i
call :sub !NAME!
exit /b 0
:error
echo Error & exit /b 1
)~~"},
{"html", L"html", LR"~~(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Test &amp; demo</title>
  <style>
    body { color: #333; margin: 0 auto; }
  </style>
</head>
<body class="main" data-id=42>
  <!-- comment -->
  <p id="intro">Hello &lt;world&gt; &#169;</p>
  <img src="a.png" alt='pic' />
  <script>
    const x = document.getElementById("intro");
    if (x) { x.textContent = `Hi ${1 + 2}`; }
  </script>
</body>
</html>
)~~"},
{"css", L"css", LR"~~(/* Stylesheet */
@import url("reset.css");
:root { --main-color: #ff6600; --pad: 4px; }
body, html {
  margin: 0;
  font-family: "Segoe UI", sans-serif;
  color: var(--main-color) !important;
}
.container > .item:hover::before {
  content: "\201C";
  width: calc(100% - 2 * var(--pad));
  background: url(data/img.png) no-repeat;
  transform: rotate(-45deg) scale(1.5);
}
#header a[href^="http"] { padding: .5em 1rem; }
@media screen and (max-width: 600px) {
  .item { display: none; }
}
)~~"},
{"json", L"json", LR"~~({
  "name": "mdview",
  "version": "1.0.0",
  "private": true,
  "count": -12.5e3,
  "tags": ["a", "b\n\u00e9"],
  "nested": { "key": null, "ok": false }
}
)~~"},
{"yaml", L"yaml", LR"~~(# Config
---
name: my-app
version: 1.2
enabled: true
ports:
  - 8080
  - "9090"
env:
  DEBUG: "false"
  PATH: /usr/bin:/bin  # comment
anchors:
  base: &base
    timeout: 30
  derived:
    <<: *base
    tag: !!str 123
script: |
  echo "hello"
  exit 0
empty: ~
list: [1, two, {k: v}]
)~~"},
{"toml", L"toml", LR"~~(# TOML sample
title = "TOML Example"
[owner]
name = "Tom"
dob = 1979-05-27T07:32:00-08:00
[database]
enabled = true
ports = [ 8000, 8001, 8002 ]
data = [ ["delta", "phi"], [3.14] ]
temp_targets = { cpu = 79.5, case = 72.0 }
[[products]]
name = 'literal \no escape'
desc = """
multi-line
"""
)~~"},
{"ini", L"ini", LR"~~(; INI sample
[General]
Name = My App
Version=1.0
Enabled = yes
Path = %APPDATA%\MyApp  ; inline comment
# hash comment
[Section.Sub]
Key="quoted value"
)~~"},
{"reg", L"reg", LR"~~(Windows Registry Editor Version 5.00

[HKEY_CURRENT_USER\Software\Ghisler\Total Commander]
"InstallDir"="C:\\totalcmd"
"Flags"=dword:00000001
@="default"
"Bin"=hex:01,02,03,\
  04,05
)~~"},
{"diff", L"diff", LR"~~(diff --git a/src/main.cpp b/src/main.cpp
index 83db48f..bf2a3c1 100644
--- a/src/main.cpp
+++ b/src/main.cpp
@@ -1,5 +1,6 @@ int main()
 #include <cstdio>
-int old = 1;
+int updated = 2;
+// added
 return 0;
\ No newline at end of file
)~~"},
{"markdown", L"markdown", LR"~~(# Title
Some **bold** text and `inline code` and a [link](https://example.com).
## Subtitle
- item one
1. first
> quote
```js
const x = 1;
```
---
<!-- comment -->
)~~"},
{"dockerfile", L"Dockerfile", LR"~~(# syntax=docker/dockerfile:1
FROM node:20-alpine AS build
ARG VERSION=1.0
ENV NODE_ENV=production PORT=8080
WORKDIR /app
COPY package*.json ./
RUN npm ci && \
    npm cache clean --force  # cleanup
COPY --from=build /app/dist /usr/share/nginx/html
EXPOSE 8080/tcp
CMD ["node", "server.js"]
HEALTHCHECK --interval=30s CMD curl -f http://localhost/ || exit 1
)~~"},
{"makefile", L"makefile", LR"~~(# Makefile
CC := gcc
CFLAGS ?= -O2 -Wall
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
export PATH

.PHONY: all clean
all: app

app: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "Built $@"

%.o: %.c
	$(CC) -c $< -o $@  # compile

ifeq ($(OS),Windows_NT)
  EXE = .exe
endif
clean:
	-rm -f $(OBJ) app
)~~"},
{"cmake", L"cmake", LR"~~(cmake_minimum_required(VERSION 3.20)
project(mdview LANGUAGES CXX VERSION 1.0)
# comment
#[[ bracket
comment ]]
set(CMAKE_CXX_STANDARD 20)
option(BUILD_TESTS "Build tests" ON)
if(MSVC AND NOT BUILD_TESTS)
  add_compile_options(/W4 /utf-8)
endif()
add_library(core STATIC src/a.cpp "${CMAKE_SOURCE_DIR}/src/b.cpp")
target_link_libraries(core PUBLIC d2d1 $<$<CONFIG:Debug>:dbghelp>)
message(STATUS "Version: ${PROJECT_VERSION} env=$ENV{PATH}")
)~~"},
{"pascal", L"pascal", LR"~~(program Hello;
{$APPTYPE CONSOLE}
uses
  SysUtils, Classes;
type
  TGreeter = class(TObject)
  private
    FName: string;
  public
    constructor Create(const AName: string);
    procedure Greet; virtual;
  end;
(* old style comment *)
constructor TGreeter.Create(const AName: string);
begin
  inherited Create;
  FName := AName;
end;
procedure TGreeter.Greet;
var
  i: Integer;
begin
  for i := 1 to 3 do
    WriteLn('Hello, ' + FName + '!' + #13#10 + 'It''s ' + IntToStr($FF));
  // line comment
  if Assigned(Self) and (i > 0) then Exit;
end;
begin
  TGreeter.Create('World').Greet;
end.
)~~"},
{"lua", L"lua", LR"~~(-- Lua sample
--[[ multi
line comment ]]
local M = {}
function M.greet(name, ...)
  local s = [[long
string]]
  if name == nil then return false end
  for i = 1, #name do print(i, name:sub(i, i)) end
  return string.format("Hello %s\n", name), 0x1F, 1.5e3
end
local t = setmetatable({}, { __index = M })
return M
)~~"},
{"php", L"php", LR"~~(<?php
namespace App\Http;
use Foo\Bar;
// comment
# hash comment
final class UserController extends Controller {
    private const MAX = 10;
    public function show(int $id): ?User {
        $user = User::find($id);
        $msg = "Hello {$user->name}, id $id\n";
        $s = 'single $notvar';
        if ($user === null || !isset($this->cache[$id])) { return null; }
        echo strlen($msg);
        return $user;
    }
}
?>
)~~"},
{"ruby", L"ruby", LR"~~(# Ruby sample
require 'json'
module Greeter
  class Person < Base
    attr_accessor :name, :age
    DEFAULT_AGE = 30
    def initialize(name, age = DEFAULT_AGE)
      @name = name
      @@count ||= 0
    end
    def greet!
      puts "Hello, #{@name.capitalize}! #{1 + 2}"
      return nil if @name.empty?
      %w[a b].each { |x| p x }
      /ab+c/i =~ @name
    end
  end
end
=begin
block comment
=end
)~~"},
{"kotlin", L"kotlin", LR"~~(package demo
import kotlinx.coroutines.*
data class User(val name: String, val age: Int = 0)
@JvmStatic
fun main(args: Array<String>) {
    val user = User("Ann", 30)
    println("Name: ${user.name}, age $args")
    val raw = """raw ${user.age} text"""
    when (user.age) { in 0..17 -> println("minor") else -> println('x') }
    /* nested /* comment */ still */
}
)~~"},
{"swift", L"swift", LR"~~(import Foundation
@MainActor
struct Point: Equatable {
    var x: Double = 0.0
    let label = "pt \(x + 1) done"
    func distance(to other: Point) -> Double {
        guard x != other.x else { return 0 }
        return (x - other.x).magnitude
    }
}
let raw = #"raw \(not) "quoted""#
#if DEBUG
print("debug", nil ?? true)
#endif
)~~"},
{"asm", L"nasm", LR"~~(; NASM sample
section .data
msg db "Hello", 10, 0
len equ $ - msg
section .text
global _start
_start:
    mov rax, 1          ; sys_write
    mov rdi, 1
    lea rsi, [rel msg]
    mov rdx, len
    syscall
    mov dword [rbp-4], 0FFh
    jmp .done
.done:
    ret
)~~"},
{"latex", L"latex", LR"~~(% LaTeX sample
\documentclass{article}
\begin{document}
\section*{Intro}
Inline math $E = mc^2$ and display:
\[ \int_0^1 x\,dx \]
Escaped \% percent and \\ newline.
\end{document}
)~~"},
{"haskell", L"haskell", LR"~~(module Main where
import qualified Data.Map as M
-- comment
{- block {- nested -} -}
data Shape = Circle Double | Rect Double Double deriving (Show, Eq)
area :: Shape -> Double
area (Circle r) = pi * r ^ 2
area (Rect w h) = w * h
main :: IO ()
main = do
  let xs' = map area [Circle 1.0, Rect 2 3]
  print xs' >> putStrLn "done" >> print 'c'
)~~"},
{"clojure", L"clojure", LR"~~(; Clojure sample
(ns demo.core (:require [clojure.string :as str]))
(defn greet [name & {:keys [loud]}]
  (let [msg (str "Hello, " name)]
    (if loud (str/upper-case msg) msg)))
(println (greet "World" :loud true) nil 42)
)~~"},
};

const Snippet* FindSnippet(const char* name) {
    for (const Snippet& s : kSnippets)
        if (std::strcmp(s.name, name) == 0) return &s;
    return nullptr;
}

void Dump(const Snippet& sn) {
    std::vector<HighlightSpan> spans;
    const std::wstring_view code(sn.code);
    HighlightCode(code, sn.lang, spans);
    std::printf("=== %s (%s) : %zu spans ===\n", sn.name, Printable(sn.lang).c_str(), spans.size());
    for (const HighlightSpan& sp : spans) {
        std::printf("  %-9s \"%s\"\n", KindName(sp.kind), Printable(code.substr(sp.start, sp.length), 80).c_str());
    }
}

// ---------------------------------------------------------------------------
// Spot checks: `target` (or the whole `context` if null) inside the first
// occurrence of `context` must be entirely covered by spans of `kind`
// (Plain = not covered by any span).
// ---------------------------------------------------------------------------
struct Expectation {
    const char* snippet;
    const wchar_t* context;
    const wchar_t* target;
    TokenKind kind;
};

#define E3(sn, ctx, kind) Expectation{sn, ctx, nullptr, K::kind}
#define E4(sn, ctx, tgt, kind) Expectation{sn, ctx, tgt, K::kind}

const Expectation kExpect[] = {
    // C++
    E3("cpp", L"#include", Meta), E3("cpp", L"<iostream>", String), E3("cpp", L"\"config.h\"", String),
    E3("cpp", L"MAX_SIZE", Constant), E3("cpp", L"// Line comment", Comment),
    E3("cpp", L"/* Block\n   comment */", Comment), E3("cpp", L"namespace", Keyword), E3("cpp", L"typename", Keyword),
    E4("cpp", L"class Widget", L"Widget", Type), E3("cpp", L"explicit", Keyword), E3("cpp", L"1.5e-3", Number),
    E4("cpp", L"std::string name()", L"name", Function), E3("cpp", L"\\x41", Escape), E3("cpp", L"u8\"widget", String),
    E3("cpp", L"0x1F'FF", Number), E4("cpp", L"'\\n'", L"\\n", Escape),
    E3("cpp", L"R\"delim(raw \"string\" \\n)delim\"", String), E4("cpp", L"enum class Color", L"Color", Type),
    E3("cpp", L"uint8_t", Type), E4("cpp", L"int main", L"main", Function), E3("cpp", L"nullptr", Constant),
    E3("cpp", L"true", Constant), E3("cpp", L"co_await", Keyword), E3("cpp", L"something", Function),
    E4("cpp", L"std::endl", L"endl", Plain), E4("cpp", L"return 0", L"0", Number), E4("cpp", L"float x", L"float", Type),
    // Python
    E3("python", L"#!/usr/bin/env python3", Comment), E3("python", L"\"\"\"Module docstring.\"\"\"", String),
    E4("python", L"import os", L"import", Keyword), E3("python", L"MAX_RETRIES", Constant),
    E3("python", L"@dataclass", Meta), E4("python", L"class Point", L"Point", Type),
    E4("python", L"def distance", L"distance", Function), E4("python", L"(self,", L"self", Keyword),
    E3("python", L"f\"Hello ", String), E4("python", L"{argv[0]!r}", L"argv", Plain),
    E3("python", L"r'C:\\path\\n'", String), E3("python", L"\\x00", Escape), E3("python", L"None", Constant),
    E3("python", L"print", Function), E3("python", L"lambda", Keyword), E3("python", L"'''multi\nline'''", String),
    E4("python", L"return True", L"True", Constant), E3("python", L"1.5e10", Number), E3("python", L"# comment", Comment),
    E4("python", L"is None", L"is", Keyword),
    // JavaScript
    E3("js", L"/ab+c\\/[a-z]*/gi", Regex), E3("js", L"`Hello ", String), E4("js", L"${user.name}", L"user", Plain),
    E4("js", L" messages\\n`", L" messages", String), E4("js", L"class Foo", L"Foo", Type),
    E4("js", L"extends Bar", L"extends", Keyword), E3("js", L"constructor", Function), E3("js", L"super", Keyword),
    E4("js", L"await readFile", L"readFile", Function), E4("js", L"x / y / 2", L"y", Plain),
    E4("js", L"function add", L"add", Function), E4("js", L"null ??", L"null", Constant),
    E4("js", L"undefined;", L"undefined", Constant), E3("js", L"typeof", Keyword), E3("js", L"JSON", Type),
    E3("js", L"MAX_COUNT", Constant), E3("js", L"1_000_000n", Number), E3("js", L"'fs/promises'", String),
    E3("js", L"/* block */", Comment), E3("js", L"0x10", Number),
    // TypeScript
    E4("ts", L"interface User", L"interface", Keyword), E4("ts", L"interface User", L"User", Type),
    E4("ts", L"id: number", L"number", Type), E4("ts", L"string[]", L"string", Type),
    E4("ts", L"Promise<void>", L"Promise", Type), E3("ts", L"@Component", Meta), E3("ts", L"UserService", Type),
    E3("ts", L"implements", Keyword), E3("ts", L"OnInit", Type), E3("ts", L"`user:", String),
    E3("ts", L"unknown", Type), E3("ts", L"never", Type), E3("ts", L"readonly", Keyword),
    // Rust
    E3("rust", L"#[derive(Debug, Clone)]", Meta), E4("rust", L"Config<'a>", L"'a", Keyword),
    E4("rust", L"HashMap<String", L"HashMap", Type), E4("rust", L"pub fn new", L"fn", Keyword),
    E4("rust", L"pub fn new", L"new", Function), E3("rust", L"r#\"raw \"quoted\" string\"#", String),
    E4("rust", L"b\"bytes\\n\"", L"\\n", Escape), E3("rust", L"'x'", String), E3("rust", L"\\u{1F600}", Escape),
    E3("rust", L"println!", Function), E4("rust", L"Self {", L"Self", Keyword), E3("rust", L"vec!", Function),
    E3("rust", L"3_000", Number), E3("rust", L"/* block /* nested */ still comment */", Comment),
    E4("rust", L"'outer:", L"'outer", Keyword), E3("rust", L"1.5e3_f64", Number), E4("rust", L"Ok(())", L"Ok", Constant),
    E3("rust", L"u64", Type), E3("rust", L"i32", Type), E4("rust", L"let mut", L"mut", Keyword),
    // Go
    E4("go", L"package main", L"package", Keyword), E3("go", L"\"fmt\"", String), E4("go", L"type Greeter", L"Greeter", Type),
    E4("go", L"Name  string", L"string", Type), E4("go", L"func (g", L"func", Keyword),
    E4("go", L"Greet(msg", L"Greet", Function), E3("go", L"`raw \\n string`", String), E3("go", L"'x'", String),
    E3("go", L"Printf", Function), E4("go", L"%d\\n", L"\\n", Escape), E4("go", L"g == nil", L"nil", Constant),
    E3("go", L"HasPrefix", Function), E3("go", L"defer", Keyword), E3("go", L"recover", Function),
    E4("go", L"bool = true", L"true", Constant), E3("go", L"0x1F", Number), E3("go", L"// Greeter greets.", Comment),
    // C#
    E4("csharp", L"using System;", L"using", Keyword), E3("csharp", L"#region Demo", Meta),
    E4("csharp", L"class Person", L"Person", Type), E3("csharp", L"IComparable", Type),
    E4("csharp", L"public string Name", L"string", Type), E4("csharp", L"get;", L"get", Keyword),
    E3("csharp", L"@\"C:\\temp\\file.txt\"", String), E3("csharp", L"$\"Hello ", String),
    E3("csharp", L"{{literal}}", String), E4("csharp", L"{args[0]}", L"args", Plain),
    E3("csharp", L"\"\"\"raw \"text\" \"\"\"", String), E4("csharp", L"'\\t'", L"\\t", Escape),
    E3("csharp", L"WriteLine", Function), E4("csharp", L"is null", L"null", Constant), E3("csharp", L"nameof", Keyword),
    E4("csharp", L"await Task", L"await", Keyword), E3("csharp", L"1.5e3d", Number), E3("csharp", L"10.5m", Number),
    E4("csharp", L"public int CompareTo", L"CompareTo", Function), E3("csharp", L"#endregion", Meta),
    // Java
    E4("java", L"package com", L"package", Keyword), E3("java", L"/** Javadoc comment */", Comment),
    E3("java", L"@SuppressWarnings", Meta), E4("java", L"class Main", L"Main", Type), E4("java", L"extends Base", L"Base", Type),
    E3("java", L"MAX_VALUE", Constant), E4("java", L"new HashMap", L"HashMap", Type), E3("java", L"@Override", Meta),
    E4("java", L"void run", L"void", Type), E4("java", L"\\\"quoted", L"\\\"", Escape), E3("java", L"123_456L", Number),
    E3("java", L".5e-3", Number), E3("java", L"println", Function),
    E3("java", L"\"\"\"\n            text block\n            \"\"\"", String), E3("java", L"'c'", String),
    // SQL
    E3("sql", L"-- Query sample", Comment), E3("sql", L"SELECT", Keyword), E3("sql", L"COUNT", Function),
    E4("sql", L"AS order_count", L"AS", Keyword), E3("sql", L"'2024-01-01'", String), E3("sql", L"'it''s'", String),
    E3("sql", L"/* create table */", Comment), E3("sql", L"\"orders\"", Plain), E3("sql", L"BIGSERIAL", Type),
    E3("sql", L"NUMERIC", Type), E4("sql", L"NOT NULL", L"NULL", Constant), E3("sql", L"0.00", Number),
    E3("sql", L"VARCHAR", Type), E4("sql", L"insert into", L"insert", Keyword), E3("sql", L"@param", Variable),
    E3("sql", L":name", Variable), E3("sql", L"$1", Variable), E3("sql", L"N'unicode'", String),
    E3("sql", L"TRUE", Constant), E4("sql", L"LEFT JOIN", L"JOIN", Keyword),
    // Bash
    E3("bash", L"#!/usr/bin/env bash", Comment), E3("bash", L"# Build script", Comment),
    E4("bash", L"set -euo", L"set", Function), E3("bash", L"readonly", Keyword),
    E4("bash", L"readonly VERSION", L"VERSION", Variable), E3("bash", L"\"1.2.3\"", String),
    E4("bash", L"export PATH", L"export", Keyword), E4("bash", L"export PATH", L"PATH", Variable),
    E3("bash", L"$HOME", Variable), E4("bash", L"function build", L"function", Keyword),
    E4("bash", L"function build", L"build", Function), E4("bash", L"local target", L"local", Keyword),
    E3("bash", L"${1:-release}", Variable), E4("bash", L"echo \"Building", L"echo", Function),
    E4("bash", L"$target (", L"$target", Variable), E3("bash", L"\"Building ", String),
    E4("bash", L"if [[", L"if", Keyword), E3("bash", L"then", Keyword), E4("bash", L"rm -rf build/*", L"rm", Function),
    E4("bash", L"fi\n", L"fi", Keyword), E4("bash", L"for f in", L"for", Keyword), E4("bash", L"for f in", L"in", Keyword),
    E4("bash", L"; do", L"do", Keyword), E3("bash", L"gcc", Function), E4("bash", L"\"$f\"", L"$f", Variable),
    E4("bash", L"exit 1", L"exit", Keyword), E4("bash", L"done\n", L"done", Keyword), E4("bash", L"$(ls", L"$(", Variable),
    E4("bash", L"$(ls", L"ls", Function), E4("bash", L"| wc", L"wc", Function), E3("bash", L"'single $quoted'", String),
    E3("bash", L"# trailing comment", Comment), E3("bash", L"case", Keyword), E3("bash", L"esac", Keyword),
    E3("bash", L"<<EOF", Keyword), E4("bash", L"Hello $USER", L"Hello ", String),
    E4("bash", L"Hello $USER", L"$USER", Variable), E4("bash", L"\nEOF\n", L"EOF", Keyword),
    E4("bash", L"echo done", L"done", Plain),
    // Console
    E4("console", L"$ npm", L"$ ", Meta), E4("console", L"npm install", L"npm", Function),
    E3("console", L"added 1 package in 2.1s", Plain), E4("console", L"\"hello $USER\"", L"$USER", Variable),
    E4("console", L"| tr", L"tr", Function), E3("console", L"HELLO MIKHAIL", Plain),
    E3("console", L"user@host:~/project$ ", Meta), E4("console", L"git status", L"git", Function),
    E3("console", L"On branch main", Plain), E3("console", L"PS C:\\Users\\me> ", Meta),
    E3("console", L"Get-ChildItem", Function), E3("console", L"C:\\>", Meta), E4("console", L"dir /b", L"dir", Keyword),
    E4("console", L"# apt-get", L"# ", Meta), E3("console", L"apt-get", Function),
    // PowerShell
    E3("powershell", L"# PowerShell sample", Comment), E3("powershell", L"<# Block\n   comment #>", Comment),
    E4("powershell", L"param(", L"param", Keyword), E4("powershell", L"[string]$Name", L"string", Type),
    E4("powershell", L"[string]$Name", L"$Name", Variable), E3("powershell", L"\"World\"", String),
    E4("powershell", L"function Get", L"function", Keyword), E3("powershell", L"Get-Greeting", Function),
    E4("powershell", L"Hello, $Who", L"$Who", Variable), E3("powershell", L"`n", Escape),
    E4("powershell", L"= Get-ChildItem", L"Get-ChildItem", Function), E3("powershell", L"$env:TEMP", Variable),
    E3("powershell", L"foreach", Keyword), E3("powershell", L"-gt", Keyword), E3("powershell", L"1MB", Number),
    E3("powershell", L"-and", Keyword), E3("powershell", L"$true", Constant), E3("powershell", L"'Big file:'", String),
    E3("powershell", L"System.IO.File", Type), E3("powershell", L"@\"\nHere-string with ", String),
    E4("powershell", L"with $Name", L"$Name", Variable), E4("powershell", L"-Path $env", L"-Path", Plain),
    E3("powershell", L"42", Number),
    // Batch
    E4("batch", L"@echo off", L"echo", Keyword), E3("batch", L"REM Batch sample", Comment),
    E3("batch", L":: another comment", Comment), E3("batch", L"setlocal", Keyword), E4("batch", L"set NAME", L"set", Keyword),
    E4("batch", L"set NAME", L"NAME", Variable), E3("batch", L"%COUNT%", Variable), E4("batch", L"if exist", L"if", Keyword),
    E4("batch", L"if exist", L"exist", Keyword), E3("batch", L"%~dp0", Variable),
    E4("batch", L"\"%~dp0config.ini\"", L"config.ini\"", String), E3("batch", L"goto", Keyword),
    E4("batch", L"goto :error", L":error", Function), E4("batch", L"%%i in", L"%%i", Variable),
    E4("batch", L"%%i in", L"in", Keyword), E4("batch", L"do echo", L"do", Keyword), E4("batch", L"call :sub", L":sub", Function),
    E3("batch", L"!NAME!", Variable), E4("batch", L"\n:error", L":error", Function), E4("batch", L"exit /b 0", L"0", Number),
    E3("batch", L"Found config for", Plain),
    // HTML
    E3("html", L"<!DOCTYPE html>", Meta), E4("html", L"<html lang", L"html", Tag), E3("html", L"lang", Attribute),
    E3("html", L"\"en\"", String), E3("html", L"&amp;", Constant), E4("html", L"<style>", L"style", Tag),
    E4("html", L"color: #333", L"color", Property), E3("html", L"#333", Constant), E3("html", L"data-id", Attribute),
    E4("html", L"=42", L"42", String), E3("html", L"<!-- comment -->", Comment), E3("html", L"&#169;", Constant),
    E4("html", L"const x", L"const", Keyword), E3("html", L"getElementById", Function), E3("html", L"`Hi ", String),
    E4("html", L"</body>", L"body", Tag), E4("html", L"Hello &lt;", L"Hello ", Plain),
    // CSS
    E3("css", L"/* Stylesheet */", Comment), E3("css", L"@import", Keyword), E4("css", L"url(\"reset", L"url", Function),
    E3("css", L"\"reset.css\"", String), E3("css", L":root", Keyword), E4("css", L"--main-color:", L"--main-color", Variable),
    E3("css", L"#ff6600", Constant), E4("css", L"body, html", L"body", Tag), E4("css", L"margin: 0;", L"margin", Property),
    E4("css", L"margin: 0;", L"0", Number), E3("css", L"\"Segoe UI\"", String), E4("css", L"var(--main", L"var", Function),
    E3("css", L"!important", Keyword), E3("css", L".container", Attribute), E3("css", L":hover", Keyword),
    E3("css", L"::before", Keyword), E3("css", L"\\201C", Escape), E3("css", L"calc", Function), E3("css", L"100%", Number),
    E3("css", L"data/img.png", String), E3("css", L"-45deg", Number), E3("css", L"#header", Attribute),
    E3("css", L"href", Attribute), E3("css", L".5em", Number), E3("css", L"@media", Keyword), E3("css", L"max-width", Property),
    E3("css", L"600px", Number), E3("css", L"sans-serif", Plain), E4("css", L"display: none", L"display", Property),
    // JSON
    E3("json", L"\"name\"", Property), E3("json", L"\"mdview\"", String), E3("json", L"true", Constant),
    E3("json", L"-12.5e3", Number), E3("json", L"\\n", Escape), E3("json", L"\\u00e9", Escape), E3("json", L"null", Constant),
    E3("json", L"\"key\"", Property),
    // YAML
    E3("yaml", L"# Config", Comment), E3("yaml", L"---", Meta), E4("yaml", L"name:", L"name", Property),
    E3("yaml", L"my-app", Plain), E3("yaml", L"1.2", Number), E4("yaml", L"enabled: true", L"true", Constant),
    E4("yaml", L"- 8080", L"8080", Number), E3("yaml", L"\"9090\"", String), E3("yaml", L"DEBUG", Property),
    E3("yaml", L"\"false\"", String), E3("yaml", L"/usr/bin:/bin", Plain), E3("yaml", L"# comment", Comment),
    E3("yaml", L"&base", Variable), E4("yaml", L"<<:", L"<<", Property), E3("yaml", L"*base", Variable),
    E3("yaml", L"!!str", Type), E4("yaml", L"script: |", L"|", Meta), E3("yaml", L"echo \"hello\"", String),
    E3("yaml", L"~", Constant), E3("yaml", L"two", Plain), E4("yaml", L"{k: v}", L"k", Property),
    E3("yaml", L"timeout", Property),
    // TOML
    E3("toml", L"# TOML sample", Comment), E3("toml", L"title", Property), E3("toml", L"\"TOML Example\"", String),
    E3("toml", L"[owner]", Meta), E3("toml", L"1979-05-27T07:32:00-08:00", Number),
    E4("toml", L"enabled = true", L"true", Constant), E3("toml", L"8000", Number), E3("toml", L"\"delta\"", String),
    E3("toml", L"3.14", Number), E3("toml", L"cpu", Property), E3("toml", L"79.5", Number), E3("toml", L"[[products]]", Meta),
    E3("toml", L"'literal \\no escape'", String), E3("toml", L"\"\"\"\nmulti-line\n\"\"\"", String),
    E3("toml", L"temp_targets", Property),
    // INI
    E3("ini", L"; INI sample", Comment), E3("ini", L"[General]", Meta), E4("ini", L"Name = ", L"Name", Property),
    E3("ini", L"My App", Plain), E3("ini", L"1.0", Number), E3("ini", L"yes", Constant), E3("ini", L"%APPDATA%", Variable),
    E3("ini", L"; inline comment", Comment), E3("ini", L"# hash comment", Comment), E3("ini", L"\"quoted value\"", String),
    E3("ini", L"[Section.Sub]", Meta),
    // .reg
    E3("reg", L"Windows Registry Editor Version 5.00", Meta),
    E3("reg", L"[HKEY_CURRENT_USER\\Software\\Ghisler\\Total Commander]", Meta), E3("reg", L"\"InstallDir\"", Property),
    E4("reg", L"\"C:\\\\totalcmd\"", L"\\\\", Escape), E3("reg", L"dword:", Type), E3("reg", L"00000001", Number),
    E4("reg", L"@=", L"@", Property), E3("reg", L"hex:", Type), E3("reg", L"01,02,03,", Number), E3("reg", L"04,05", Number),
    // Diff
    E3("diff", L"diff --git a/src/main.cpp b/src/main.cpp", Heading), E3("diff", L"index 83db48f..bf2a3c1 100644", Heading),
    E3("diff", L"--- a/src/main.cpp", Heading), E3("diff", L"+++ b/src/main.cpp", Heading),
    E3("diff", L"@@ -1,5 +1,6 @@", DiffRange), E3("diff", L" int main()", Plain), E3("diff", L"-int old = 1;", Deleted),
    E3("diff", L"+int updated = 2;", Inserted), E3("diff", L"\\ No newline at end of file", Comment),
    E3("diff", L" #include <cstdio>", Plain),
    // Markdown
    E3("markdown", L"# Title", Heading), E3("markdown", L"**bold**", Heading), E3("markdown", L"`inline code`", String),
    E3("markdown", L"https://example.com", String), E3("markdown", L"## Subtitle", Heading),
    E4("markdown", L"- item", L"-", Keyword), E4("markdown", L"1. first", L"1.", Keyword),
    E4("markdown", L"> quote", L">", Keyword), E3("markdown", L"```js", Meta), E3("markdown", L"const x = 1;", Plain),
    E4("markdown", L"\n---\n", L"---", Meta), E3("markdown", L"<!-- comment -->", Comment),
    E3("markdown", L" text and ", Plain),
    // Dockerfile
    E3("dockerfile", L"# syntax=docker/dockerfile:1", Comment), E3("dockerfile", L"FROM", Keyword),
    E4("dockerfile", L" AS build", L"AS", Keyword), E3("dockerfile", L"ARG", Keyword),
    E4("dockerfile", L"VERSION=", L"VERSION", Property), E3("dockerfile", L"NODE_ENV", Property),
    E3("dockerfile", L"RUN", Keyword), E4("dockerfile", L"npm ci", L"npm", Function), E3("dockerfile", L"# cleanup", Comment),
    E3("dockerfile", L"--from", Constant), E4("dockerfile", L"8080/tcp", L"8080", Number),
    E4("dockerfile", L"CMD [", L"CMD", Keyword), E3("dockerfile", L"\"node\"", String), E3("dockerfile", L"HEALTHCHECK", Keyword),
    E4("dockerfile", L"30s CMD", L"CMD", Keyword), E3("dockerfile", L"curl", Function),
    E4("dockerfile", L"exit 1", L"exit", Keyword),
    // Makefile
    E3("makefile", L"# Makefile", Comment), E4("makefile", L"CC :=", L"CC", Variable),
    E4("makefile", L"CFLAGS ?=", L"CFLAGS", Variable), E3("makefile", L"wildcard", Function),
    E4("makefile", L"SRC =", L"SRC", Variable), E4("makefile", L"export PATH", L"export", Keyword),
    E3("makefile", L".PHONY", Keyword), E4("makefile", L"all: app", L"all", Function),
    E4("makefile", L"app: $(OBJ)", L"app", Function), E4("makefile", L"$(CC) $(CFLAGS)", L"$(CC)", Variable),
    E4("makefile", L"$@ $^", L"$@", Variable), E4("makefile", L"$@ $^", L"$^", Variable),
    E3("makefile", L"\"Built $@\"", String), E4("makefile", L"%.o: %.c", L"%.o", Function), E3("makefile", L"$<", Variable),
    E3("makefile", L"# compile", Comment), E3("makefile", L"ifeq", Keyword), E3("makefile", L"$(OS)", Variable),
    E4("makefile", L"EXE =", L"EXE", Variable), E3("makefile", L"endif", Keyword), E4("makefile", L"clean:", L"clean", Function),
    // CMake
    E3("cmake", L"cmake_minimum_required", Function), E4("cmake", L"(VERSION 3.20)", L"VERSION", Constant),
    E3("cmake", L"3.20", Number), E3("cmake", L"project", Function), E3("cmake", L"# comment", Comment),
    E3("cmake", L"#[[ bracket\ncomment ]]", Comment), E4("cmake", L"CMAKE_CXX_STANDARD 20", L"CMAKE_CXX_STANDARD", Plain),
    E4("cmake", L"ON)", L"ON", Constant), E4("cmake", L"if(MSVC", L"if", Keyword), E4("cmake", L"AND NOT", L"AND", Constant),
    E3("cmake", L"endif", Keyword), E3("cmake", L"STATIC", Constant), E3("cmake", L"${CMAKE_SOURCE_DIR}", Variable),
    E3("cmake", L"PUBLIC", Constant), E4("cmake", L"$<$<CONFIG", L"$<", Meta), E3("cmake", L"STATUS", Constant),
    E3("cmake", L"$ENV{PATH}", Variable), E3("cmake", L"\"Build tests\"", String),
    // Pascal
    E4("pascal", L"program Hello", L"program", Keyword), E3("pascal", L"{$APPTYPE CONSOLE}", Meta),
    E3("pascal", L"uses", Keyword), E4("pascal", L"type\n", L"type", Keyword),
    E4("pascal", L"TGreeter = class", L"TGreeter", Type), E4("pascal", L"= class(", L"class", Keyword),
    E3("pascal", L"TObject", Type), E3("pascal", L"private", Keyword), E4("pascal", L"FName: string", L"string", Type),
    E4("pascal", L"constructor Create", L"constructor", Keyword), E4("pascal", L"constructor Create", L"Create", Function),
    E4("pascal", L"procedure Greet", L"Greet", Function), E3("pascal", L"virtual", Keyword),
    E3("pascal", L"(* old style comment *)", Comment), E4("pascal", L"constructor TGreeter.Create", L"TGreeter", Type),
    E4("pascal", L"TGreeter.Create(const", L"Create", Function), E3("pascal", L"begin", Keyword),
    E3("pascal", L"inherited", Keyword), E3("pascal", L"Integer", Type), E4("pascal", L"for i", L"for", Keyword),
    E3("pascal", L"WriteLn", Function), E3("pascal", L"'Hello, '", String), E3("pascal", L"#13#10", String),
    E3("pascal", L"'It''s '", String), E3("pascal", L"IntToStr", Function), E3("pascal", L"$FF", Number),
    E3("pascal", L"// line comment", Comment), E3("pascal", L"Assigned", Function), E4("pascal", L"(Self)", L"Self", Keyword),
    E4("pascal", L"Exit;", L"Exit", Keyword), E4("pascal", L"end.", L"end", Keyword),
    E4("pascal", L".Greet;\nvar", L"Greet", Function),
    // Lua
    E3("lua", L"-- Lua sample", Comment), E3("lua", L"--[[ multi\nline comment ]]", Comment),
    E4("lua", L"local M", L"local", Keyword), E4("lua", L"function M.greet", L"function", Keyword),
    E4("lua", L"M.greet", L"greet", Function), E3("lua", L"[[long\nstring]]", String), E3("lua", L"nil", Constant),
    E3("lua", L"print", Function), E4("lua", L"sub(i", L"sub", Function), E4("lua", L"%s\\n", L"\\n", Escape),
    E3("lua", L"0x1F", Number), E3("lua", L"setmetatable", Function), E4("lua", L"return false", L"false", Constant),
    // PHP
    E3("php", L"<?php", Meta), E3("php", L"namespace", Keyword), E3("php", L"// comment", Comment),
    E3("php", L"# hash comment", Comment), E3("php", L"final", Keyword), E3("php", L"UserController", Type),
    E4("php", L"extends Controller", L"Controller", Type), E3("php", L"MAX", Constant),
    E4("php", L"function show", L"show", Function), E4("php", L"int $id", L"int", Type), E4("php", L"int $id", L"$id", Variable),
    E4("php", L"User::find", L"find", Function), E4("php", L"{$user->name}", L"$user", Variable),
    E4("php", L"id $id\\n", L"$id", Variable), E3("php", L"'single $notvar'", String), E4("php", L"=== null", L"null", Constant),
    E3("php", L"isset", Keyword), E3("php", L"$this", Variable), E3("php", L"echo", Keyword), E3("php", L"strlen", Function),
    E3("php", L"?>", Meta),
    // Ruby
    E3("ruby", L"# Ruby sample", Comment), E3("ruby", L"require", Keyword), E3("ruby", L"'json'", String),
    E4("ruby", L"module Greeter", L"module", Keyword), E3("ruby", L"Greeter", Type), E4("ruby", L"class Person", L"Person", Type),
    E3("ruby", L"attr_accessor", Keyword), E3("ruby", L":name", Constant), E3("ruby", L"DEFAULT_AGE", Constant),
    E4("ruby", L"def initialize", L"initialize", Function), E4("ruby", L"@name =", L"@name", Variable),
    E3("ruby", L"@@count", Variable), E4("ruby", L"def greet!", L"greet!", Function), E3("ruby", L"puts", Function),
    E4("ruby", L"#{@name", L"@name", Variable), E3("ruby", L"\"Hello, ", String), E3("ruby", L"nil", Constant),
    E3("ruby", L"/ab+c/i", Regex), E3("ruby", L"=begin\nblock comment\n=end", Comment),
    // Kotlin
    E3("kotlin", L"package", Keyword), E4("kotlin", L"data class", L"data", Keyword), E4("kotlin", L"class User", L"User", Type),
    E4("kotlin", L"val name", L"val", Keyword), E3("kotlin", L"String", Type), E3("kotlin", L"Int", Type),
    E3("kotlin", L"@JvmStatic", Meta), E4("kotlin", L"fun main", L"main", Function), E3("kotlin", L"Array", Type),
    E3("kotlin", L"\"Name: ", String), E3("kotlin", L"$args", Variable), E3("kotlin", L"\"\"\"raw ", String),
    E3("kotlin", L"when", Keyword), E3("kotlin", L"'x'", String), E3("kotlin", L"/* nested /* comment */ still */", Comment),
    E4("kotlin", L"${user.name}", L"user", Plain),
    // Swift
    E3("swift", L"import", Keyword), E3("swift", L"@MainActor", Meta), E3("swift", L"struct", Keyword),
    E4("swift", L"struct Point", L"Point", Type), E3("swift", L"Double", Type), E3("swift", L"\"pt ", String),
    E4("swift", L"(x + 1)", L"x", Plain), E3("swift", L" done\"", String), E4("swift", L"func distance", L"distance", Function),
    E3("swift", L"guard", Keyword), E3("swift", L"#\"raw \\(not) \"quoted\"\"#", String), E4("swift", L"#if DEBUG", L"#if", Meta),
    E3("swift", L"nil", Constant), E3("swift", L"#endif", Meta),
    // Assembly
    E3("asm", L"; NASM sample", Comment), E4("asm", L"section .data", L"section", Meta), E3("asm", L".data", Meta),
    E4("asm", L"msg db", L"msg", Function), E4("asm", L"msg db", L"db", Meta), E3("asm", L"\"Hello\"", String),
    E4("asm", L", 10,", L"10", Number), E3("asm", L"equ", Meta), E3("asm", L"global", Meta),
    E4("asm", L"_start:", L"_start", Function), E4("asm", L"mov rax", L"mov", Keyword), E3("asm", L"rax", Variable),
    E3("asm", L"; sys_write", Comment), E3("asm", L"lea", Keyword), E4("asm", L"[rel msg]", L"rel", Type),
    E3("asm", L"syscall", Keyword), E3("asm", L"dword", Type), E3("asm", L"rbp", Variable), E3("asm", L"0FFh", Number),
    E4("asm", L"jmp .done", L".done", Meta), E4("asm", L".done:", L".done", Function), E3("asm", L"ret", Keyword),
    // LaTeX
    E3("latex", L"% LaTeX sample", Comment), E3("latex", L"\\documentclass", Keyword), E3("latex", L"\\begin", Keyword),
    E4("latex", L"{document}", L"document", Type), E3("latex", L"\\section*", Keyword), E3("latex", L"$E = mc^2$", String),
    E3("latex", L"\\[ \\int_0^1 x\\,dx \\]", String), E3("latex", L"\\%", Escape), E4("latex", L"\\\\ newline", L"\\\\", Escape),
    // Haskell
    E3("haskell", L"module", Keyword), E3("haskell", L"Main", Type), E3("haskell", L"qualified", Keyword),
    E3("haskell", L"-- comment", Comment), E3("haskell", L"{- block {- nested -} -}", Comment),
    E4("haskell", L"data Shape", L"data", Keyword), E4("haskell", L"data Shape", L"Shape", Type),
    E3("haskell", L"Circle", Type), E3("haskell", L"deriving", Keyword), E4("haskell", L"do\n", L"do", Keyword),
    E3("haskell", L"xs'", Plain), E3("haskell", L"\"done\"", String), E3("haskell", L"'c'", String),
    E3("haskell", L"1.0", Number),
    // Clojure
    E3("clojure", L"; Clojure sample", Comment), E4("clojure", L"(ns", L"ns", Keyword), E3("clojure", L":require", Constant),
    E3("clojure", L"defn", Keyword), E4("clojure", L"defn greet", L"greet", Function), E3("clojure", L":keys", Constant),
    E4("clojure", L"(let", L"let", Keyword), E4("clojure", L"(str \"Hello", L"str", Function),
    E3("clojure", L"\"Hello, \"", String), E4("clojure", L"(if", L"if", Keyword), E3("clojure", L"str/upper-case", Function),
    E3("clojure", L"println", Function), E3("clojure", L":loud", Constant), E3("clojure", L"true", Constant),
    E3("clojure", L"nil", Constant), E3("clojure", L"42", Number),
    // Heuristic refinements
    E4("csharp", L"using System.Collections.Generic;", L"Collections", Plain),
    E4("csharp", L"list.Count:D2", L"Count", Plain), E4("ts", L"public async get(", L"get", Function),
    E4("cpp", L"new demo::Widget", L"demo", Plain), E4("cpp", L"new demo::Widget", L"Widget", Type),
    E4("sql", L"t (a, b)", L"t", Plain), E4("kotlin", L"0..17", L"17", Number),
    E4("kotlin", L"0..17", L"..", Plain), E4("css", L"screen and (", L"and", Keyword),
    E4("haskell", L"IO ()", L"IO", Type), E4("go", L"Name  string", L"Name", Plain),
    E4("lua", L"function M.greet", L"M", Plain), E4("powershell", L"$(Get-Date)", L"$(", Variable),
    E4("cmake", L"/utf-8", L"8", Plain), E3("makefile", L"$(SRC:.c=.o)", Variable),
    E4("java", L"System.out.println", L"out", Plain), E4("rust", L"use std::collections::HashMap;", L"HashMap", Plain),
    E4("php", L"namespace App\\Http;", L"App", Plain),
};

#undef E3
#undef E4

void TestExpectations() {
    int bad = 0;
    for (const Expectation& ex : kExpect) {
        ++g_checks;
        const Snippet* sn = FindSnippet(ex.snippet);
        if (!sn) {
            Fail("unknown snippet %s%s", ex.snippet);
            ++bad;
            continue;
        }
        const std::wstring_view code(sn->code);
        const size_t ctx = code.find(ex.context);
        size_t pos = ctx;
        size_t len = std::wstring_view(ex.context).size();
        if (ctx != std::wstring_view::npos && ex.target) {
            const std::wstring_view c(ex.context);
            const size_t t = c.find(ex.target);
            pos = t == std::wstring_view::npos ? t : ctx + t;
            len = std::wstring_view(ex.target).size();
        }
        if (pos == std::wstring_view::npos) {
            Fail("[%s] text not found: %s", ex.snippet, Printable(ex.target ? ex.target : ex.context));
            ++bad;
            continue;
        }
        std::vector<HighlightSpan> spans;
        HighlightCode(code, sn->lang, spans);
        std::string actual;
        bool ok = true;
        for (size_t p = pos; p < pos + len; ++p) {
            TokenKind k = K::Plain;
            for (const HighlightSpan& sp : spans) {
                if (p >= sp.start && p < static_cast<size_t>(sp.start) + sp.length) {
                    k = sp.kind;
                    break;
                }
            }
            if (k != ex.kind) ok = false;
            if (actual.empty() || actual.rfind(KindName(k)) != actual.size() - std::strlen(KindName(k))) {
                if (!actual.empty()) actual += ",";
                actual += KindName(k);
            }
        }
        if (!ok) {
            ++bad;
            ++g_failures;
            std::printf("FAIL: [%s] \"%s\" expected %s, got %s\n", ex.snippet,
                        Printable(code.substr(pos, len)).c_str(), KindName(ex.kind), actual.c_str());
        }
    }
    std::printf("expectations: %zu checked, %d failed\n", sizeof(kExpect) / sizeof(kExpect[0]), bad);
}

// ---------------------------------------------------------------------------
// Language names
// ---------------------------------------------------------------------------
const wchar_t* const kAllAliases[] = {
    L"c", L"h", L"cpp", L"c++", L"cc", L"cxx", L"hpp", L"hxx", L"hh", L"h++", L"ino", L"arduino", L"cuda", L"cu",
    L"cuh", L"objc", L"objective-c", L"objectivec", L"obj-c", L"m", L"mm", L"cs", L"csharp", L"c#", L"cake",
    L"java", L"kotlin", L"kt", L"kts", L"scala", L"sc", L"groovy", L"gradle", L"swift", L"go", L"golang", L"rust",
    L"rs", L"zig", L"dart", L"d", L"glsl", L"hlsl", L"wgsl", L"solidity", L"sol", L"verilog", L"systemverilog",
    L"vhdl", L"js", L"javascript", L"jsx", L"mjs", L"cjs", L"node", L"ts", L"typescript", L"tsx", L"mts", L"cts",
    L"python", L"py", L"py3", L"python3", L"pyw", L"gyp", L"bazel", L"starlark", L"bzl", L"ruby", L"rb",
    L"gemspec", L"podspec", L"rake", L"crystal", L"elixir", L"ex", L"exs", L"erlang", L"erl", L"php", L"perl",
    L"pl", L"pm", L"lua", L"r", L"julia", L"jl", L"haskell", L"hs", L"fsharp", L"fs", L"f#", L"ocaml", L"ml",
    L"nim", L"clojure", L"clj", L"cljs", L"lisp", L"elisp", L"emacs-lisp", L"scheme", L"racket", L"fortran",
    L"f90", L"vb", L"vbnet", L"vba", L"vbs", L"visualbasic", L"pascal", L"delphi", L"pas", L"dpr", L"lpr",
    L"freepascal", L"objectpascal", L"sql", L"mysql", L"pgsql", L"postgres", L"postgresql", L"sqlite", L"tsql",
    L"plsql", L"mssql", L"graphql", L"gql", L"protobuf", L"proto", L"hcl", L"terraform", L"tf", L"nix", L"nginx",
    L"html", L"htm", L"xhtml", L"xml", L"svg", L"xaml", L"plist", L"vue", L"svelte", L"css", L"scss", L"sass",
    L"less", L"stylus", L"json", L"jsonc", L"json5", L"geojson", L"webmanifest", L"yaml", L"yml", L"toml", L"ini",
    L"cfg", L"conf", L"properties", L"editorconfig", L"gitconfig", L"desktop", L"reg", L"diff", L"patch", L"md",
    L"markdown", L"sh", L"bash", L"zsh", L"ksh", L"shell", L"console", L"shell-session", L"terminal",
    L"powershell", L"ps", L"ps1", L"pwsh", L"psm1", L"bat", L"cmd", L"batch", L"dockerfile", L"docker",
    L"containerfile", L"make", L"makefile", L"mk", L"mak", L"cmake", L"asm", L"nasm", L"x86asm", L"masm",
    L"armasm", L"latex", L"tex",
};

// One representative name per distinct scanner configuration (used for fuzzing).
const wchar_t* const kFuzzLangs[] = {
    L"c", L"cpp", L"objc", L"cs", L"java", L"kotlin", L"scala", L"groovy", L"dart", L"swift", L"go", L"rust",
    L"zig", L"d", L"glsl", L"hlsl", L"wgsl", L"solidity", L"verilog", L"vhdl", L"js", L"ts", L"python", L"ruby",
    L"crystal", L"elixir", L"erlang", L"php", L"perl", L"lua", L"r", L"julia", L"haskell", L"fsharp", L"ocaml",
    L"nim", L"clojure", L"lisp", L"fortran", L"vb", L"pascal", L"sql", L"graphql", L"proto", L"hcl", L"nix",
    L"nginx", L"html", L"css", L"scss", L"sass", L"json", L"yaml", L"toml", L"ini", L"properties", L"reg",
    L"diff", L"md", L"sh", L"console", L"powershell", L"bat", L"dockerfile", L"makefile", L"cmake", L"asm",
    L"latex",
};

void TestLanguageNames() {
    int bad = 0;
    for (const wchar_t* a : kAllAliases) {
        ++g_checks;
        if (!IsKnownLanguage(a)) {
            Fail("alias not known: %s%s", Printable(a));
            ++bad;
        }
    }
    const wchar_t* const kPositive[] = {L"Python", L"C++", L"JSON", L"  rust  ", L"python title=\"x.py\"",
                                        L"{.python}", L"language-rust", L"js,linenos", L"CPP", L"Shell-Session",
                                        L"C#", L"F#", L"PowerShell", L"Dockerfile", L"Makefile"};
    for (const wchar_t* a : kPositive) {
        ++g_checks;
        if (!IsKnownLanguage(a)) {
            Fail("info string not recognized: %s%s", Printable(a));
            ++bad;
        }
    }
    const wchar_t* const kNegative[] = {L"text", L"plain", L"plaintext", L"txt", L"output", L"log", L"",
                                        L"   ", L"unknownlang", L"c+++", L"{}", L"language-"};
    for (const wchar_t* a : kNegative) {
        ++g_checks;
        std::vector<HighlightSpan> out;
        out.push_back(HighlightSpan{0, 1, K::Comment});
        const bool r = HighlightCode(L"int x = 1; // hi", a, out);
        if (IsKnownLanguage(a) || r || out.size() != 1) {
            Fail("language should be unknown: \"%s\"%s", Printable(a));
            ++bad;
        }
    }
    std::printf("language names: %d failed\n", bad);
}

// Invariants, determinism and "append, don't clear" on all snippets, also
// cross-highlighting every snippet with every language.
void TestSnippets() {
    std::vector<HighlightSpan> a, b;
    for (const Snippet& sn : kSnippets) {
        const std::wstring_view code(sn.code);
        RunChecked(code, sn.lang, a, std::string("snippet ") + sn.name);
        RunChecked(code, sn.lang, b, std::string("snippet ") + sn.name);
        ++g_checks;
        bool same = a.size() == b.size();
        for (size_t k = 0; same && k < a.size(); ++k)
            same = a[k].start == b[k].start && a[k].length == b[k].length && a[k].kind == b[k].kind;
        if (!same) Fail("non-deterministic output for %s%s", sn.name);
        // Appends without clearing.
        std::vector<HighlightSpan> c;
        c.push_back(HighlightSpan{7, 3, K::Heading});
        HighlightCode(code, sn.lang, c);
        ++g_checks;
        if (c.size() != a.size() + 1 || c[0].start != 7 || c[0].length != 3 || c[0].kind != K::Heading)
            Fail("output not appended for %s%s", sn.name);
        CheckInvariants(c, 1, code.size(), std::string("append ") + sn.name);
        for (const wchar_t* lang : kFuzzLangs)
            RunChecked(code, lang, a, std::string("cross ") + sn.name + " as " + Printable(lang));
    }
    // Empty input is fine for every language.
    for (const wchar_t* lang : kFuzzLangs) RunChecked(std::wstring_view(), lang, a, "empty input");
}

// Every prefix of every snippet (truncations mid-token).
void TestTruncations() {
    std::vector<HighlightSpan> out;
    size_t runs = 0;
    for (const Snippet& sn : kSnippets) {
        const std::wstring_view code(sn.code);
        for (size_t len = 0; len <= code.size(); ++len) {
            out.clear();
            HighlightCode(code.substr(0, len), sn.lang, out);
            if (!CheckInvariants(out, 0, len, std::string("prefix of ") + sn.name)) break;
            // Also suffixes (start mid-token).
            out.clear();
            HighlightCode(code.substr(len), sn.lang, out);
            if (!CheckInvariants(out, 0, code.size() - len, std::string("suffix of ") + sn.name)) break;
            runs += 2;
        }
    }
    std::printf("truncations: %zu runs\n", runs);
}

// ---------------------------------------------------------------------------
// Fuzzing
// ---------------------------------------------------------------------------
struct Rng {
    uint64_t s;
    uint64_t Next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    uint32_t Below(uint32_t n) { return static_cast<uint32_t>(Next() % n); }
};

const wchar_t* const kFuzzTokens[] = {
    L"/*", L"*/", L"//", L"\"\"\"", L"'''", L"<!--", L"-->", L"${", L"#{", L"\\(", L"R\"x(", L")x\"", L"[[",
    L"]]", L"--[[", L"[==[", L"]==]", L"<<EOF", L"EOF", L"@\"", L"$\"", L"r#\"", L"\"#", L"{-", L"-}", L"(*",
    L"*)", L"{$", L"#[", L"```", L"---", L"@@", L"+++", L"if", L"class", L"def", L"fn", L"function", L"procedure",
    L"rem", L"echo", L"set", L"RUN", L"FROM", L"\n\t", L"PS C:\\> ", L"\n$ ", L"%%i", L"%VAR%", L"!x!", L"$(",
    L"$((", L"`", L"\\\n", L"\n=begin", L"\n=end", L"#!", L"<script>", L"</script>", L"<style>", L"</style>",
    L"a:", L"- ", L"key: |", L"0x", L"1e", L"'a", L"b'", L"::", L"\n#if", L"#include <", L"@Override", L"&amp;",
    L"<a href=", L"\\u{", L"\\x", L"#=", L"=#", L"#|", L"|#", L"u8R\"", L"$@\"", L"#\"", L"@'", L"'@", L"<#",
    L"#>", L"f\"", L"**", L"](", L"<?php", L"?>", L"\n:", L"%~dp0", L"<<-", L"<<'E'", L"\nE\n", L"$<", L"#[[",
    L"=", L"return /", L"(defn ", L"'\\", L"\"\\", L"\n@@ -1 +1 @@", L"\n[", L"\n- ", L"\n  ", L"\r\n",
};
const wchar_t kFuzzChars[] = L"\"'`/\\*#<>${}()[]\n\r \t@%!:;=-+.,?&|~^_aZ09xX";
const wchar_t kFuzzSpecial[] = {0x0000, 0xD800, 0xDC00, 0xDBFF, 0xFFFF, 0x0416, 0x00A0, 0x3000, 0xFEFF, 0x2028};

void TestFuzz(int iterations) {
    Rng rng{0x9E3779B97F4A7C15ull};
    std::wstring s;
    std::vector<HighlightSpan> out;
    const uint32_t nTok = sizeof(kFuzzTokens) / sizeof(kFuzzTokens[0]);
    const uint32_t nChr = sizeof(kFuzzChars) / sizeof(kFuzzChars[0]) - 1;
    const uint32_t nSpc = sizeof(kFuzzSpecial) / sizeof(kFuzzSpecial[0]);
    const auto t0 = std::chrono::steady_clock::now();
    size_t chars = 0;
    for (const wchar_t* lang : kFuzzLangs) {
        const int failuresBefore = g_failures;
        for (int it = 0; it < iterations; ++it) {
            const uint32_t len = rng.Below(301);
            s.clear();
            while (s.size() < len) {
                const uint32_t r = rng.Below(10);
                if (r < 3) s += kFuzzTokens[rng.Below(nTok)];
                else if (r < 4) s += kFuzzSpecial[rng.Below(nSpc)];
                else s += kFuzzChars[rng.Below(nChr)];
            }
            s.resize(len);
            chars += len;
            out.clear();
            HighlightCode(s, lang, out);
            if (!CheckInvariants(out, 0, s.size(), "fuzz " + Printable(lang) + " input \"" + Printable(s, 400) + "\"")) break;
        }
        if (g_failures != failuresBefore) std::printf("  (fuzz failures in %s)\n", Printable(lang).c_str());
    }
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("fuzz: %d inputs x %zu languages, %.1f M chars in %.2f s\n", iterations,
                sizeof(kFuzzLangs) / sizeof(kFuzzLangs[0]), static_cast<double>(chars) / 1e6, sec);
}

// ---------------------------------------------------------------------------
// Pathological inputs (must stay linear)
// ---------------------------------------------------------------------------
void TestPathological() {
    struct Patho {
        const wchar_t* unit;
        size_t total;
    };
    const Patho kPatho[] = {
        {L"/*\"'", 2000000}, {L"\"", 2000000}, {L"`${", 262144}, {L"/[", 262144}, {L"x = /a/ /", 262144},
        {L"** ` ``` ", 262144}, {L"<a b=", 262144}, {L"{-(*#[", 262144}, {L"#{\\(", 262144}, {L"'", 262144},
        {L"$(", 262144}, {L"%x", 262144}, {L"[==[", 262144}, {L"\\", 262144}, {L"R\"x(", 262144},
        {L"@\"", 262144}, {L"a: [", 262144}, {L"<<EOF ", 262144}, {L"{{{{", 262144}, {L"((((", 262144},
        {L"${${", 262144}, {L"-- ", 262144}, {L"](x ", 262144}, {L"#$", 262144}, {L"&#", 262144},
        {L"int x = 1; ", 1000000}, {L"<!--", 262144}, {L"@\"\n", 262144}, {L"a.b(", 262144},
    };
    std::vector<HighlightSpan> out;
    out.reserve(1 << 20);
    double worst = 0, total = 0;
    std::string worstWhat;
    auto run = [&](const std::wstring& s, const std::string& what) {
        for (const wchar_t* lang : kFuzzLangs) {
            out.clear();
            const auto t0 = std::chrono::steady_clock::now();
            HighlightCode(s, lang, out);
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            total += sec;
            if (sec > worst) {
                worst = sec;
                worstWhat = what + " as " + Printable(lang);
            }
            CheckInvariants(out, 0, s.size(), "pathological " + what + " as " + Printable(lang));
            ++g_checks;
            if (sec > 0.25) std::printf("  slow: %s as %s: %.2f s\n", what.c_str(), Printable(lang).c_str(), sec);
            if (sec > 2.0) Fail("too slow (%s): %s", what + " as " + Printable(lang), std::to_string(sec) + " s");
        }
    };
    for (const Patho& p : kPatho) {
        std::wstring s;
        s.reserve(p.total + 16);
        while (s.size() < p.total) s += p.unit;
        run(s, "\"" + Printable(p.unit) + "\" x " + std::to_string(p.total / std::wcslen(p.unit)));
    }
    {  // backtick runs of increasing length on one line (markdown code spans)
        std::wstring s;
        for (size_t len = 1; s.size() < 1000000; ++len) {
            s.append(len % 3000 + 1, L'`');
            s += L'x';
        }
        run(s, "backtick runs");
    }
    std::printf("pathological: total %.2f s, worst %.3f s (%s)\n", total, worst, worstWhat.c_str());
}

// ---------------------------------------------------------------------------
// Benchmark
// ---------------------------------------------------------------------------
double BenchOne(const wchar_t* lang, const wchar_t* unit, size_t targetChars, bool print) {
    std::wstring s;
    s.reserve(targetChars + 4096);
    while (s.size() < targetChars) s += unit;
    std::vector<HighlightSpan> out;
    out.reserve(s.size() / 3);
    double best = 1e9;
    for (int rep = 0; rep < 5; ++rep) {
        out.clear();
        const auto t0 = std::chrono::steady_clock::now();
        HighlightCode(s, lang, out);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (sec < best) best = sec;
    }
    const double mbps = static_cast<double>(s.size()) / 1e6 / best;
    if (print)
        std::printf("  %-10s %6.1f M chars: %7.1f MB/s (source chars), %7.1f MB/s (UTF-16 bytes), %zu spans\n",
                    Printable(lang).c_str(), static_cast<double>(s.size()) / 1e6, mbps, mbps * 2, out.size());
    return mbps;
}

void Benchmark() {
    std::printf("benchmark (best of 5):\n");
    const double cpp = BenchOne(L"cpp", FindSnippet("cpp")->code, 10u * 1000 * 1000, true);
    const char* const others[] = {"python", "js", "rust", "csharp", "json", "html", "css", "yaml",
                                  "markdown", "bash", "sql", "pascal"};
    for (const char* name : others) {
        const Snippet* sn = FindSnippet(name);
        BenchOne(sn->lang, sn->code, 4u * 1000 * 1000, true);
    }
    if (cpp >= 50.0) std::printf("speed target (>= 50 MB/s on C++): met (%.1f MB/s)\n", cpp);
    else std::printf("WARNING: speed target (>= 50 MB/s on C++) NOT met: %.1f MB/s\n", cpp);
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool quiet = false;
    const char* dumpOne = nullptr;
    int fuzzIterations = 20000;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-q") == 0) quiet = true;
        else if (std::strcmp(argv[i], "-dump") == 0 && i + 1 < argc) dumpOne = argv[++i];
        else if (std::strcmp(argv[i], "-fuzz") == 0 && i + 1 < argc) fuzzIterations = std::atoi(argv[++i]);
    }
    if (dumpOne) {
        const Snippet* sn = FindSnippet(dumpOne);
        if (!sn) {
            std::printf("no snippet named %s\n", dumpOne);
            return 2;
        }
        Dump(*sn);
        return 0;
    }
    if (!quiet)
        for (const Snippet& sn : kSnippets) Dump(sn);

    const auto t0 = std::chrono::steady_clock::now();
    TestLanguageNames();
    TestSnippets();
    TestExpectations();
    TestTruncations();
    TestFuzz(fuzzIterations);
    TestPathological();
    Benchmark();
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%d checks, %d failures (%.1f s)\n%s\n", g_checks, g_failures, sec,
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return g_failures == 0 ? 0 : 1;
}
