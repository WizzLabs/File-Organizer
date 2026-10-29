# File Organizer

A small, dependency-free Windows desktop utility, written in C++17, that sorts the files of one folder into category folders (Images, Documents, Applications, Code, ...) based on their extensions.

<!-- Add a screenshot at docs/screenshot.png, then uncomment the next line: -->
<!-- ![File Organizer](docs/screenshot.png) -->

## Features

- Pick a folder with **Browse**, see a **preview** of every planned move, then confirm.
- Case-insensitive matching (`.JPG` = `.jpg`) across 13 categories.
- Understands applications and installers (`.exe`, `.msi`, ...), Markdown (`.md`), ebooks, fonts, design files, disk images and more.
- Scanning and moving run on a background thread, so the window never freezes.
- Monochrome dark interface, built with the plain Win32 API: **no external libraries**.

## Safety

- Nothing moves until you click **Organize Files** and confirm.
- Files are **never deleted** and **never overwritten**. A name conflict is skipped and reported.
- Only files directly inside the chosen folder are touched. Subfolders, symbolic links, hidden/system files and the program's own files are left alone.
- A drive root such as `C:\` is refused.
- A failed move (for example a file in use) leaves the file where it was and shows the reason. Moves across drives are not attempted.
- Heads-up: moving a portable `.exe` away from the files it depends on, or one that a shortcut points to, can break it. Test on a copy first.

## Supported extensions

| Category | Extensions |
|---|---|
| **Images** | png, jpg, jpeg, jfif, gif, bmp, webp, svg, ico, tif, tiff, heic, heif, avif, raw, cr2, nef, arw, dng, orf |
| **Documents** | pdf, doc, docx, docm, dotx, txt, rtf, odt, odg, odf, md, markdown, rst, tex, log, xls, xlsx, xlsm, xltx, csv, tsv, ppt, pptx, pptm, potx, odp, ods, pages, numbers, key, wps, xps |
| **Audio** | mp3, wav, flac, aac, ogg, m4a, wma, opus, aiff, aif, mid, midi, amr, ape |
| **Videos** | mp4, mkv, avi, mov, wmv, webm, m4v, mpg, mpeg, 3gp, ogv, vob |
| **Code** | cpp, cc, cxx, c, h, hpp, hh, hxx, py, js, mjs, cjs, ts, jsx, tsx, vue, html, css, scss, sass, less, java, kt, scala, groovy, gradle, rs, go, php, rb, swift, dart, lua, r, pl, m, cs, vb, asm, json, xml, yaml, yml, toml, ini, cfg, conf, sql, sh, bash, zsh, ps1, bat, cmd, cmake, mk, ipynb |
| **Archives** | zip, rar, 7z, tar, gz, tgz, bz2, tbz2, xz, txz, zst, lz, lzma, cab |
| **Applications** | exe, msi, msix, appx, apk, jar, appimage, deb, rpm, pkg |
| **Ebooks** | epub, mobi, azw, azw3, fb2, djvu, cbz, cbr |
| **Fonts** | ttf, otf, ttc, woff, woff2, fon |
| **Design** | psd, ai, eps, indd, xd, fig, sketch, xcf, kra, cdr, blend, fbx, obj, stl, 3ds, dwg, dxf |
| **Disk Images** | iso, img, dmg, vhd, vhdx, vmdk |
| **Data** | db, sqlite, sqlite3, mdb, accdb, parquet, jsonl |
| **Others** | everything else, including files with no extension |

To add a type, put its extension in the matching list in `categoryTable()` in [`src/organizer_core.h`](src/organizer_core.h) and rebuild.

## Build (Windows, MinGW-w64, g++ with C++17)

```
build.bat D:\mingw64\bin
build\FileOrganizer.exe
```

`build.bat` takes the path to your MinGW `bin` folder (default `D:\mingw64\bin`). It uses `windres` to embed a manifest when available and still builds without it.

Manual command:

```
g++ -std=c++17 -O2 -mwindows -static src\gui_main.cpp -o FileOrganizer.exe -lcomctl32 -lshell32 -lole32 -luxtheme -ldwmapi -lgdi32
```

## Tests

The core logic is separate from the GUI and has automated tests that run on Windows or Linux:

```
g++ -std=c++17 -Isrc tests/test_core.cpp -o test_core
./test_core
```

They cover the category table, case-insensitive matching, conflict handling (no overwrite), subfolder and program-file protection.

## Project structure

```
src/organizer_core.h   scanning, categorizing, safe moving (no GUI code)
src/gui_main.cpp       Win32 window, preview list, worker threads
resources/             Windows manifest (modern controls, dark scrollbars)
tests/test_core.cpp    automated tests for the core logic
build.bat              Windows build script
```

## Limitations

- Windows GUI only; the core logic itself is portable.
- One folder at a time, not recursive.
- No undo yet.

## Roadmap

- Custom categories and rules
- Undo for the last run
- Optional recursive mode

## Development note

Started as a college mini-project: the console version was written first, and the Windows GUI was then built with AI assistance and reviewed and tested by the author.

## License

MIT, see [LICENSE](LICENSE).
