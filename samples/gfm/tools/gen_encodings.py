#!/usr/bin/env python3
"""Generate the encoding-detection samples 10..15 in samples/gfm/.

  10-encoding-utf8-bom.md       UTF-8 with BOM (EF BB BF), CRLF
  11-encoding-utf16le.md        UTF-16 LE with BOM (FF FE), CRLF (like Notepad's "Unicode")
  12-encoding-utf16be.md        UTF-16 BE with BOM (FE FF), LF
  13-encoding-cp1251.md         Windows-1251 (Cyrillic), no BOM, CRLF; invalid as UTF-8
  14-encoding-utf16le-nobom.md  UTF-16 LE without BOM, LF (heuristic detection)
  15-encoding-invalid-utf8.md   UTF-8 without BOM containing a few invalid byte sequences, LF

Run:  python samples/gfm/tools/gen_encodings.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, ".."))

MULTILINGUAL = """\
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
"""

CP1251_BODY = """\
## Текст

«Съешь же ещё этих мягких французских булок, да выпей чаю» — эта фраза содержит все буквы русского алфавита, включая «ё» и «ъ». Номер документа: № 42; цена: 100 €; многоточие… и «кавычки-ёлочки», “лапки”.

Широкая электрификация южных губерний даст мощный толчок подъёму сельского хозяйства. В чащах юга жил бы цитрус? Да, но фальшивый экземпляр!

## Таблица

| Буква | Код CP1251 | Юникод |
|:-----:|:----------:|:------:|
| А | C0 | U+0410 |
| я | FF | U+044F |
| Ё | A8 | U+0401 |
| ё | B8 | U+0451 |
| № | B9 | U+2116 |
| € | 88 | U+20AC |

## Код

```ini
; конфигурация в кодировке Windows-1251
[Общие]
Язык=Русский
Тема=Светлая
```

## Проверка

- [ ] Кириллица читается, а не превращается в «кракозябры» или `????`
- [ ] Символы «», —, №, € и … отображаются правильно
- [ ] Последнее слово документа: КОНЕЦ

КОНЕЦ
"""


def doc(title, description, body=MULTILINGUAL):
    return f"# {title}\n\n{description}\n\n{body}"


def write(name, text, encoding, bom=b"", crlf=False):
    if crlf:
        text = text.replace("\n", "\r\n")
    data = bom + text.encode(encoding)
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(data)
    return name, len(data), data[:8].hex(" ")


def main():
    results = []
    results.append(write(
        "10-encoding-utf8-bom.md",
        doc("Encoding Test: UTF-8 with BOM",
            "This file is encoded as **UTF-8 with a byte order mark** (`EF BB BF`) and uses **CRLF** line "
            "endings, like files saved by older Windows Notepad versions. The viewer must detect UTF-8 from the BOM, "
            "must not display the BOM (`ï»¿` or an invisible character) before the title, and must render every "
            "script below correctly."),
        "utf-8", bom=b"\xef\xbb\xbf", crlf=True))
    results.append(write(
        "11-encoding-utf16le.md",
        doc("Encoding Test: UTF-16 LE with BOM",
            "This file is encoded as **UTF-16 little-endian with a BOM** (`FF FE`) and uses **CRLF** line endings. "
            "Windows Notepad calls this encoding \"Unicode\". Every character takes two bytes (four for "
            "supplementary-plane characters, stored as surrogate pairs), so a viewer that assumes UTF-8 shows "
            "garbage or stops at the first zero byte."),
        "utf-16-le", bom=b"\xff\xfe", crlf=True))
    results.append(write(
        "12-encoding-utf16be.md",
        doc("Encoding Test: UTF-16 BE with BOM",
            "This file is encoded as **UTF-16 big-endian with a BOM** (`FE FF`) and uses **LF** line endings. "
            "It is rare on Windows (Notepad calls it \"Unicode big endian\"), but a correct viewer detects it from "
            "the BOM and swaps the bytes."),
        "utf-16-be", bom=b"\xfe\xff"))
    results.append(write(
        "13-encoding-cp1251.md",
        doc("Проверка кодировки: Windows-1251",
            "Этот файл сохранён в кодировке **Windows-1251** (кириллица, ANSI-кодовая страница 1251) **без BOM** и "
            "с окончаниями строк **CRLF**. Как UTF-8 он некорректен: байты русских букв (C0–FF) не образуют "
            "допустимых последовательностей UTF-8. Просмотрщик должен понять, что это не UTF-8, и "
            "декодировать файл как CP1251 (системная ANSI-кодировка на русской Windows), а не показывать "
            "символы-заменители (U+FFFD).",
            CP1251_BODY),
        "cp1251", crlf=True))
    results.append(write(
        "14-encoding-utf16le-nobom.md",
        doc("Encoding Test: UTF-16 LE without BOM",
            "This file is encoded as **UTF-16 little-endian WITHOUT a BOM** and uses **LF** line endings. Nothing "
            "in the file declares the encoding, so it has to be guessed from the byte pattern: ASCII text in "
            "UTF-16 LE has a `00` byte at every odd offset (compare the Windows `IsTextUnicode()` heuristics). "
            "It's a heuristic-detection test, and a viewer that shows the raw bytes as text with gaps, or as "
            "one line of garbage, has failed it."),
        "utf-16-le"))

    # 15: UTF-8 with a few invalid sequences. Build as bytes.
    head = doc("Encoding Test: Invalid UTF-8 Sequences",
               "This file is **UTF-8 without a BOM**, but the list below contains a few deliberately **invalid byte "
               "sequences**. Everything else is valid UTF-8, including plenty of Cyrillic, CJK and emoji. Browsers "
               "decode such a file as UTF-8 and show U+FFFD (�) for each bad sequence. A viewer that falls back to "
               "a legacy code page (such as CP1251) because of a few bad bytes garbles the *whole* page, which is "
               "the failure this file detects.", body="").encode("utf-8")
    lines = [
        "## Valid text before the bad bytes\n\n",
        "Русский текст: «Съешь же ещё этих мягких французских булок». 中文：你好，世界！ Emoji: 🚀👍🏽\n\n",
        "## Invalid sequences (each should show as one or more �)\n\n",
    ]
    body = b"".join(s.encode("utf-8") for s in lines)
    bad = [
        ("A lone continuation byte `80`", b"[\x80]"),
        ("An overlong encoding of `/` (`C0 AF`)", b"[\xc0\xaf]"),
        ("A truncated 3-byte sequence (`E2 82`, missing its last byte)", b"[\xe2\x82]"),
        ("An encoded UTF-16 surrogate (`ED A0 80`)", b"[\xed\xa0\x80]"),
        ("The byte `FF`, which never appears in UTF-8", b"[\xff]"),
        ("A code point above U+10FFFF (`F4 90 80 80`)", b"[\xf4\x90\x80\x80]"),
        ("A Latin-1 `é` as the single byte `E9`, in the word caf\\xE9", b"[caf\xe9]"),
    ]
    for label, raw in bad:
        body += b"- " + label.encode("utf-8") + b": " + raw + b"\n"
    body += ("\n## Valid text after the bad bytes\n\n"
             "Українська: ґанок, їжак, європейський. Ελληνικά: Καλημέρα. 日本語：こんにちは。\n\n"
             "| Check | Expected |\n|---|---|\n"
             "| Cyrillic above and below | readable |\n"
             "| Bad bytes | shown as � |\n"
             "| Table and list | intact |\n\n"
             "КОНЕЦ / END\n").encode("utf-8")
    data = head + body
    try:
        data.decode("utf-8")
        raise SystemExit("15 should be invalid UTF-8")
    except UnicodeDecodeError:
        pass
    with open(os.path.join(OUT, "15-encoding-invalid-utf8.md"), "wb") as f:
        f.write(data)
    results.append(("15-encoding-invalid-utf8.md", len(data), data[:8].hex(" ")))

    for name, size, first in results:
        print(f"{name:32} {size:6} bytes  first bytes: {first}")


if __name__ == "__main__":
    main()
