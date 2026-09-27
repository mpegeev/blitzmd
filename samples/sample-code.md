# Блоки кода

## Inline code

В тексте может встретиться `inline_code`, ссылки на переменные типа `g_hInstance`,
имена функций `ListLoadW()`, и пути `C:\Windows\System32\msftedit.dll`.
Команда `cmake --build . --config Release` собирает проект,
а `Ctrl+Shift+B` это шорткат для Build All.

## C++

```cpp
#include <windows.h>
#include <string_view>
#include <expected>

namespace {
    constexpr std::string_view kVersion = "1.0.0";
}

extern "C" __declspec(dllexport)
HWND __stdcall ListLoadW(HWND parent, wchar_t* path, int flags) {
    auto content = ReadFileToUtf8(path);
    if (!content) {
        return ShowError(parent, content.error());
    }
    return RenderMarkdown(parent, *content);
}
```

## Python

```python
import os
from pathlib import Path
from typing import Optional

def find_plugin(tc_path: Path) -> Optional[Path]:
    """Find blitzmd.wlx64 in TC plugins folder."""
    candidates = [
        tc_path / "plugins" / "wlx" / "blitzmd" / "blitzmd.wlx64",
        tc_path / "wlx" / "blitzmd.wlx64",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None

if __name__ == "__main__":
    tc = Path(os.environ.get("COMMANDER_PATH", "C:/totalcmd"))
    print(f"Plugin: {find_plugin(tc)}")
```

## JavaScript

```javascript
const renderMarkdown = async (text) => {
    const tokens = parser.parse(text);
    return tokens.map(token => {
        switch (token.type) {
            case 'heading':
                return `<h${token.level}>${token.text}</h${token.level}>`;
            case 'code':
                return `<pre><code>${escape(token.text)}</code></pre>`;
            default:
                return `<p>${token.text}</p>`;
        }
    }).join('\n');
};
```

## Bash

```bash
#!/bin/bash
# Build script

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-out/build/x64-release}"

cmake -S . -B "${BUILD_DIR}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "${BUILD_DIR}" --parallel

echo "Built: ${BUILD_DIR}/blitzmd.wlx64"
```

## CMake

```cmake
cmake_minimum_required(VERSION 3.28)
project(blitzmd LANGUAGES C CXX)

include(FetchContent)
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG release-0.5.2
)
FetchContent_MakeAvailable(md4c)

add_library(blitzmd SHARED src/plugin.cpp)
target_link_libraries(blitzmd PRIVATE md4c)
```

## Без указания языка

```
обычный preformatted блок
с ASCII-art:

  ┌────────┐
  │ TC     │
  └───┬────┘
      │ F3
      ▼
  ┌────────┐
  │ plugin │
  └────────┘

или просто текст с сохранением переносов
        и    отступов
```

## С символами, опасными для RTF

```cpp
// Скобки фигурные { } и слеши \\ \\\\
auto path = R"(C:\Users\Test\file.md)";
auto json = R"({"name": "value", "list": [1, 2, 3]})";
auto regex = std::regex(R"(\d{3}-\d{4})");
```

## Длинные строки

```python
very_long_line = "Это очень длинная строка которая точно не поместится в одну линию даже на широком экране и должна либо переноситься либо обрезаться с горизонтальной прокруткой посмотрим как с этим справится RichEdit"
```

## С Unicode

```cpp
// Комментарий с эмодзи 🎉 и символами
const wchar_t* greeting = L"Привет, мир! 你好世界 مرحبا";
auto math = "α + β = γ, ∑(xᵢ) = ∞";
```
