# Markpad - Yet Another Markdown Editor

Markpad is a Markdown editor designed to provide a possibility to write multipage documents with ease. It is developed in C++.

## Features

- Three editable modes for the same GitHub Flavored Markdown (GFM) document: Markdown source, WYSIWYG continuous layout, and paginated WYSIWYG layout
- Multipage document editing (Microsoft Word / Wordpad style)
- Syntax highlighting
- Open / save Markdown documents
- Export to PDF
- Export to HTML
- Settings:
  - Layout options / page setup
  - Theme customization
  - Editor preferences
  - Keyboard shortcuts

## Build and run

### Windows (MSYS2 UCRT64)

Install the UCRT64 toolchain, CMake, Ninja, and Qt 6 Base from an **MSYS2 UCRT64** terminal:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base
```

Then, from PowerShell at the repository root, configure, build, test, and run:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
& "C:\msys64\ucrt64\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="C:\msys64\ucrt64"
& "C:\msys64\ucrt64\bin\cmake.exe" --build build --parallel
& "C:\msys64\ucrt64\bin\ctest.exe" --test-dir build --output-on-failure
.\build\markpad.exe
```

Keep `C:\msys64\ucrt64\bin` on `PATH` when running the application or tests so Windows can find Qt runtime DLLs.

### Linux (Ubuntu)

Install the build dependencies:

```sh
sudo apt-get update
sudo apt-get install --yes cmake ninja-build g++ qt6-base-dev
```

From the repository root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/markpad
```

The application provides an SDI Markdown source editor: each document has its own
window, session and undo/redo history. File commands include New, Open, Save,
Save As and guarded Close/Exit. New always creates a window; Open reuses only a
clean, empty, unnamed window. The editor includes basic visual syntax highlighting,
standard editing shortcuts, an Edit context menu and a cursor/dirty status bar.
WYSIWYG, pagination, export and persistent settings are still planned.

Pass file paths as command-line arguments (for example, `markpad document.md`).
Local `QFileOpenEvent` requests are handled too; OS file associations and
single-instance IPC are not installed. Clean builds and automated tests pass
locally on Windows UCRT64 and Ubuntu in WSL. Task 004 has not yet run in remote CI;
native-dialog and visual manual QA remain to be performed.

### Document core and file formats

`markpad_document` is a standard C++20 library without Qt dependencies.
`markpad_files` provides a Qt Core adapter using `QSaveFile` with direct-write
fallback disabled. Tests run without a GUI (`markpad_document` in CTest).

The model stores strict UTF-8, preserving line endings. Files support UTF-8,
UTF-16 LE/BE and UTF-32 LE/BE. BOM detection takes precedence; without a BOM,
valid UTF-8 is preferred, otherwise a conservative zero-byte-pattern heuristic
is used. Uncertain detection returns an error; callers may specify an encoding.
Detection is not guaranteed for every BOM-less file, especially non-ASCII text.
Invalid Unicode and U+0000 are rejected rather than replaced.

Save preserves encoding/BOM; Save As may select a different format. A literal
leading U+FEFF requires a transport BOM to avoid losing it on the next open.
Dirty compares source/format against the last successful open/save. Failed
operations preserve session state. Opening a dirty session requires explicit
discard permission. The UI offers explicit encoding retry on decode errors and
encoding/BOM selection on Save As. No external-file conflict detection is
implemented. See [Task 003](docs/tasks/task_003.md).

The source editor preserves untouched Unicode and line endings, including mixed
CRLF/CR/LF files. Newly inserted line breaks use the source's first EOL sequence,
or LF for a new document. Undo restores exact source content; the widget is a
projection, not a second canonical document. History currently stores full source
snapshots per edit, without a large-file performance guarantee. Highlighting is
heuristic, not a GFM parser; raw HTML remains inert text and no external resources
are loaded. GUI tests inject dialogs and run offscreen. See
[Task 004](docs/tasks/task_004.md).

### Optional MD4C source-preserving experiment

Task 002 focuses on MD4C and a custom document model, not editor functionality or
an approved production integration. The experiment is disabled by default. Use a
separate build directory and add `-DMARKPAD_BUILD_MD4C_SPIKE=ON` to the configure
command above (keeping the Windows Qt prefix and `PATH` when applicable):

```sh
cmake -S . -B build-md4c -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_MD4C_SPIKE=ON
cmake --build build-md4c --parallel
ctest --test-dir build-md4c --output-on-failure
ctest --test-dir build-md4c -L md4c --verbose
```

The first opt-in configure requires network access to fetch pinned, SHA256-checked
MD4C sources from GitHub. No system-wide package is installed. The prototype builds
a callback tree and checks localized text/task edits that preserve all other source
bytes. Tasks 012/013 add a partial block map and paragraph/ATX heading conversions,
including single-line blocks within explicit nested quotes. All edits use
`applyEdit` with document identity, revision and source digest; successful changes
return a reparsed snapshot and no-op preserves the original revision. Parent links
and evidence envelopes describe known source bytes, **not** complete container
ownership. Quote prefixes and bytes outside the changed block are preserved. Unsupported
forms return an explicit error. It is **not** a complete block serializer or a
full GFM conformance suite.
The MD4C tests pass locally on Windows and Ubuntu, including Linux ASan/UBSan.
See [Task 002](docs/tasks/task_002.md) for limits, licensing and reproduction.
See [Task 012](docs/tasks/task_012.md) for structural edits and snapshot guarantees.
See [Task 013](docs/tasks/task_013.md) for the unified command contract and nested
mapping limits. Next: [lists/quotes](docs/tasks/task_014.md), then
[GFM tables](docs/tasks/task_015.md); their structural operations are not implemented.
CI now enables the MD4C experiment on both platforms; the updated workflow has not
yet been run remotely.

The earlier `cmark-gfm`/Qt comparison remains available separately through
`-DMARKPAD_BUILD_GFM_SPIKE=ON`. It is disabled by default, is not required by MD4C,
and is no longer the focus of development.

## License

Markpad is licensed under the MIT License.

For more details, see the [LICENSE](LICENSE) file.

## Design documents

- [UI sketch](sketch.md)
- [Architecture](ARCHITECTURE.md)
- [Development task backlog](docs/tasks/README.md)
