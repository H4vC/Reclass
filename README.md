# ReclassX

A hex editor for reading live process memory. It shows structs, pointers, arrays, and padding so you can see how data is laid out and how it connects.

Work in progress.

![screenshot](build/screenshot.png)

## Build

Requires Qt 6, QScintilla, and MinGW on Windows.

```
cmake -B build -G Ninja
cmake --build build
```

QScintilla must be built first as a static lib. There is a helper script:

```
powershell -File scripts/build_qscintilla.ps1
```
