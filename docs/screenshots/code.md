## Settings: `blitzmd.ini`

```ini
[blitzmd]
; 0 = as Total Commander, 1 = light, 2 = dark
Theme=0
Font=Segoe UI
FontSize=16
Zoom=100
LineNumbers=1
RemoteImages=1
```

## The plugin's entry points: `src/plugin.cpp`

```cpp
constexpr char kDetectString[] =
    R"(EXT="MD" | EXT="MARKDOWN" | EXT="MDOWN" | EXT="MKD" | EXT="MKDN" | )"
    R"(EXT="MDWN" | EXT="MDTXT" | EXT="MDTEXT" | EXT="RMD")";

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    if (!DetectString || maxlen <= 0) return;
    const size_t n = std::min(std::strlen(kDetectString), static_cast<size_t>(maxlen - 1));
    std::memcpy(DetectString, kDetectString, n);
    DetectString[n] = '\0';
}

HWND __stdcall ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags) {
    try {
        EnsureSettings();
        if (!FileToLoad) return nullptr;
        return CreateViewer(ParentWin, FileToLoad, ShowFlags);
    } catch (...) {
        return nullptr;
    }
}
```
