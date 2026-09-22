# Encoding Test: UTF-8 with BOM

This file is encoded as **UTF-8 with a byte order mark** (`EF BB BF`) and uses **CRLF** line endings, like files saved by older Windows Notepad versions. The viewer must detect UTF-8 from the BOM, must not display the BOM (`ï»¿` or an invisible character) before the title, and must render every script below correctly.

## Multilingual paragraph

English, Русский: «Съешь же ещё этих мягких французских булок, да выпей чаю», Українська: ґанок, їжак, 中文：你好，世界！, 日本語：こんにちは, 한국어: 안녕하세요, Ελληνικά: Καλημέρα, עברית: שלום, العربية: مرحبا, emoji: 🚀✨👍🏽🇺🇦, and a supplementary-plane character 𝄞 (U+1D11E, which is a surrogate pair in UTF-16).

## Table

| Language | Greeting | Characters |
|----------|----------|-----------:|
| English | Hello | 5 |
| Русский | Привет | 6 |
| 中文 | 你好 | 2 |
| Emoji | 👋🌍 | 2 |

## Code

```python
# -*- coding: utf-8 -*-
greeting = "Привет, 世界! 👋"
print(greeting, len(greeting))
```

## Checklist

- [ ] No stray characters (such as `ï»¿`, `þÿ` or `ÿþ`) before the title
- [ ] The Cyrillic, CJK and emoji above are readable, not mojibake like `Ð¡ÑŠÐµÑˆÑŒ`
- [ ] The table has 4 rows and the code block is highlighted
- [ ] The document ends with the word КОНЕЦ

КОНЕЦ / END
