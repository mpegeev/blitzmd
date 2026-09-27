# blitzmd

> Lister-плагин для Total Commander, показывающий markdown-файлы по F3.
> Парсинг через md4c, рендеринг через RichEdit с RTF.

[![Логотип](logo.png)](https://www.ghisler.com/plugins.htm)

## Возможности

- ✅ Заголовки H1-H6
- ✅ **Жирный** и *курсивный* текст, ~~зачёркнутый~~
- ✅ Списки (упорядоченные и нет, с вложенностью)
- ✅ Блоки кода с моноширинным шрифтом
- ✅ [Кликабельные ссылки](https://github.com/mity/md4c)
- ✅ Цитаты
- ✅ Горизонтальные разделители
- ⚠️ Таблицы — **в работе** (Stage 4)
- ⚠️ Картинки — **в работе** (Stage 4)
- ⚠️ Тёмная тема — **в работе** (Stage 4)
- ❌ MathJax — не планируется

## Установка

1. Скачать `blitzmd.wlx64` из релизов
2. Положить в любую папку, например:
   ```
   %COMMANDER_PATH%\plugins\wlx\blitzmd\
   ```
3. В TC: **Configuration → Options → Edit/View → Configure internal viewer → Lister Plugins → Add**
4. Указать путь к `.wlx64`
5. Готово — теперь `F3` на `.md` файле открывает рендеринг

## Поддерживаемые расширения

| Расширение  | Описание |
|:------------|:---------|
| `.md`       | Markdown (стандарт) |
| `.markdown` | Полное имя |
| `.mdown`    | Альтернативный |
| `.mkd`      | Сокращённый |

## Сборка из исходников

### Требования

- Visual Studio 2026 (или 2022 17.10+)
- CMake 3.28+
- Windows 11 SDK
- Доступ в интернет (для FetchContent md4c)

### Команды

```bash
git clone https://github.com/example/blitzmd.git
cd blitzmd
cmake -S . -B build --preset x64-release
cmake --build build --config Release
```

Готовый файл: `out/build/x64-release/blitzmd.wlx64`.

### Структура проекта

```
blitzmd/
├── CMakeLists.txt          # сборка
├── CMakePresets.json       # пресеты x64/x86
├── src/
│   ├── plugin.cpp          # основной код
│   └── plugin.def          # экспорты DLL
├── test-files/             # тестовые .md
└── README.md               # вы здесь
```

## Архитектура

![Pipeline](diagram.png)

Поток обработки:

1. TC вызывает `ListLoadW(parent, path, flags)`
2. Плагин читает файл с диска через `CreateFileW` + `ReadFile`
3. Декодирует BOM (UTF-8/UTF-16 LE/BE)
4. Парсит markdown через `md_parse()` из md4c
5. Callback'и `OnEnterBlock`/`OnText`/etc. собирают RTF в `std::string`
6. RTF загружается в RichEdit через `EM_STREAMIN`
7. RichEdit отдаётся обратно TC как HWND

## Ограничения

> ⚠️ **Внимание**: текущая реализация имеет следующие ограничения:
>
> - Максимальный размер файла — **50 МБ**
> - Картинки **не отображаются** (только alt-текст)
> - Таблицы рендерятся как plain text с пайпами `|`
> - Inline HTML игнорируется

## Производительность

| Размер файла | Время рендеринга |
|-------------:|:-----------------|
| < 10 КБ      | < 50 мс          |
| 100 КБ       | ~200 мс          |
| 1 МБ         | ~1 с             |
| 10 МБ        | ~5-8 с           |

*Замеры на Intel Core i7-12700, Windows 11 23H2.*

## Известные баги

- [ ] Очень длинные строки в блоках кода обрезаются по горизонтали
- [ ] Эмодзи иногда отображаются как `?` если шрифт не поддерживает
- [x] ESC не закрывал окно при фокусе на тексте (исправлено в Stage 2)

## Лицензия

MIT. См. [LICENSE](LICENSE).

## Благодарности

- [md4c](https://github.com/mity/md4c) by Martin Mitáš — отличный markdown-парсер
- [Total Commander](https://www.ghisler.com/) by Christian Ghisler — лучший файл-менеджер
- Christian Ghisler за [WLX SDK](https://github.com/ghisler/WLX-SDK) и подробную документацию

---

*Этот README сам по себе — тестовый файл для плагина. Открой его через F3, чтобы посмотреть, как плагин рендерит свою же документацию.* 🎉
