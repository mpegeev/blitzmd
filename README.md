# mdview-wlx — Stage 1

WLX-плагин для Total Commander, показывающий markdown-файлы по F3.
Этот этап — голый скелет: проверяем, что TC видит экспорты и плагин
открывает окно с RichEdit. Парсинг markdown появится на этапе 3.

## Сборка (Visual Studio + CMake)

VS 2019/2022 умеет CMake из коробки. Самый простой путь — командой:

```bat
:: x64 (для современного TC)
mkdir build-x64
cd build-x64
cmake .. -A x64
cmake --build . --config Release
:: → build-x64\Release\mdview.wlx64

:: x86 (если у тебя 32-битный TC)
cd ..
mkdir build-x86
cd build-x86
cmake .. -A Win32
cmake --build . --config Release
:: → build-x86\Release\mdview.wlx
```

Либо открыть папку проекта прямо в Visual Studio (File → Open → Folder),
он сам подцепит CMakeLists.txt и предложит конфигурации x64-Release / x86-Release.

## Установка в Total Commander

1. Скопируй `mdview.wlx64` (и `mdview.wlx` если нужна 32-битная версия)
   в любую папку, например `%COMMANDER_PATH%\plugins\wlx\mdview\`.
2. В TC: **Configuration → Options → Edit/View → Configure internal viewer →
   Lister Plugins → Add**.
3. Укажи путь до `mdview.wlx64`.
4. В диалоге настроек плагина detect-string должен подтянуться автоматически
   (`EXT="MD" | EXT="MARKDOWN" | …`).
5. Открой любой `.md` файл и нажми **F3**.

## Что должно произойти

Откроется встроенный Lister с заголовком «MD Viewer — Stage 1», ниже —
путь к файлу. Если видишь это — экспорты работают, окно создаётся, RTF
грузится. Можно идти дальше.

## Если не работает

- **Плагин не появляется в списке детекта** — почти всегда это проблема
  с экспортами. Открой получившийся `.wlx64` через `dumpbin /exports`
  или [Dependencies](https://github.com/lucasg/Dependencies) и убедись,
  что видны имена `ListLoad`, `ListLoadW`, `ListCloseWindow`,
  `ListGetDetectString` **без** декорации (`_ListLoad@12` — это плохо).
  Если декорация осталась — что-то не так с `plugin.def`.
- **TC говорит «не лоадится»** — скорее всего разрядность не совпала.
  64-битный TC грузит только `.wlx64`, 32-битный — только `.wlx`.
- **Окно открывается пустое** — `msftedit.dll` не загрузилась. Бывает на
  очень старых системах (XP без обновлений), но на Win 10/11 проблем нет.

## Что дальше

- **Этап 2**: вытащить настоящее содержимое файла (читаем `FileToLoad`,
  кладём в RichEdit как plain text).
- **Этап 3**: подключить [md4c](https://github.com/mity/md4c), сконвертить
  md → RTF в callback'ах парсера.
- **Этап 4**: тёмная тема, гиперссылки, шрифты.
- **Этап 5**: упаковка в `.zip` с `pluginst.inf` для one-click установки.
