# Encoding Test: Invalid UTF-8 Sequences

This file is **UTF-8 without a BOM**, but the list below contains a few deliberately **invalid byte sequences**. Everything else is valid UTF-8, including plenty of Cyrillic, CJK and emoji. Browsers decode such a file as UTF-8 and show U+FFFD (�) for each bad sequence. A viewer that falls back to a legacy code page (such as CP1251) because of a few bad bytes garbles the *whole* page, which is the failure this file detects.

## Valid text before the bad bytes

Русский текст: «Съешь же ещё этих мягких французских булок». 中文：你好，世界！ Emoji: 🚀👍🏽

## Invalid sequences (each should show as one or more �)

- A lone continuation byte `80`: [�]
- An overlong encoding of `/` (`C0 AF`): [��]
- A truncated 3-byte sequence (`E2 82`, missing its last byte): [�]
- An encoded UTF-16 surrogate (`ED A0 80`): [���]
- The byte `FF`, which never appears in UTF-8: [�]
- A code point above U+10FFFF (`F4 90 80 80`): [����]
- A Latin-1 `é` as the single byte `E9`, in the word caf\xE9: [caf�]

## Valid text after the bad bytes

Українська: ґанок, їжак, європейський. Ελληνικά: Καλημέρα. 日本語：こんにちは。

| Check | Expected |
|---|---|
| Cyrillic above and below | readable |
| Bad bytes | shown as � |
| Table and list | intact |

КОНЕЦ / END
