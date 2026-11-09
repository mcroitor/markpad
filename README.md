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
A first limited continuous WYSIWYG slice is available through
**View > Switch mode**. Full WYSIWYG editing, pagination, export and persistent
settings are still in development/planned.

Pass file paths as command-line arguments (for example, `markpad document.md`).
Local `QFileOpenEvent` requests are handled too; OS file associations and
single-instance IPC are not installed. Clean builds and automated tests pass
locally on Windows UCRT64 and Ubuntu in WSL. Task 005 changes have not yet run in remote CI;
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

### Continuous WYSIWYG (first slice)

Continuous mode formats headings, paragraphs, emphasis/strong/strikethrough and
list/quote content. Blue-background text runs support local text edits; grey
fragments/placeholders are protected. **Format > Heading** and **Toggle task
checked** are available only where the source map proves the operation safe.
**Bold**, **Italic** and **Strikethrough** operate on a nonempty selection within
one editable text run; Bold/Italic also support their standard keyboard shortcuts.
Repeating a style removes it only when the entire simple span is selected.
Selections and their direction are retained after formatting. Partial removal,
cross-run formatting and future-input formatting are not supported yet.
Unavailable commands are disabled with an explanatory tooltip; unsupported keyboard
requests report an error. Delimiter collisions that would change the surrounding
Markdown semantics are rejected rather than normalized globally.
Other edits, including Enter, cross-run replacements and deleting an entire text
run, explicitly return you to Markdown for the change. Empty documents start in
Markdown. Images, code, HTML and tables are currently read-only placeholders,
not fully rendered/editable features. No external resources are loaded.

Links show their labels as inert text, never as clickable Qt anchors. You can
edit a mapped label run in inline/full-reference links while keeping the address,
title and reference definitions unchanged. Label formatting commands and
address edits still require Markdown. Autolink text is visible but read-only;
shortcut/collapsed reference edits that change the binding are explicitly
rejected. Click, Ctrl+click and keyboard do not open links, for any URL scheme.
See [Task 028](docs/tasks/task_028.md).

Both modes share one session, Save and undo/redo history. Switching modes does
not serialize or modify the source. Cursor/selection mapping is exact for mapped
text; protected positions use an explained approximation. Unchanged switch-back
restores the original selection, including delimiter-boundary affinity.
See [Task 026](docs/tasks/task_026.md) and [Task 027](docs/tasks/task_027.md);
the full [Task 005](docs/tasks/task_005.md)
is **not complete**.

### MD4C dependency and optional regression suite

The verified source-preserving API now lives in `markpad_markdown`
(`src/markdown_document.*`) and is used by the first WYSIWYG slice.
MD4C is a required build dependency, fetched from a pinned, SHA256-checked source
archive on the first configure; network access to GitHub is required then.
No system-wide package is installed. CMake copies the upstream MIT notice to
`MD4C-LICENSE.md` in the build directory: include it when distributing binaries.

The extended MD4C regression suite remains opt-in. Add
`-DMARKPAD_BUILD_MD4C_SPIKE=ON` (keeping the Windows Qt prefix and `PATH` when
applicable):

```sh
cmake -S . -B build-md4c -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_MD4C_SPIKE=ON
cmake --build build-md4c --parallel
ctest --test-dir build-md4c --output-on-failure
ctest --test-dir build-md4c -L md4c --verbose
```

The shared API builds a callback tree and checks localized text/task edits that preserve all other source
bytes. Task 027 adds selected inline style wrapping/simple unwrapping.
Tasks 012/013 add a partial block map and paragraph/ATX heading conversions,
including single-line blocks within explicit nested quotes. All edits use
`applyEdit` with document identity, revision and source digest; successful changes
return a reparsed snapshot and no-op preserves the original revision. Parent links
and evidence envelopes describe known source bytes, **not** complete container
ownership. Quote prefixes and bytes outside the changed block are preserved. Unsupported
forms return an explicit error. It is **not** a complete block serializer or a
full GFM conformance suite.
Task 016 adds read-only `mapList` ownership ranges for canonical top-level lists
and their items/content lines, including multiline paragraphs and closed fenced
code. Ranges are checked against both the parsed subtree and absolute callback
offsets; trailing separator lines remain outside the list. Nested containers,
lazy continuations, structural tabs, indented code and external definition
contexts outside the supported canonical form return diagnostics rather than
guessed ranges. Task 017 adds source-preserving `InsertListItem`,
`RemoveListItem`, `SetListKind` and `SetItemTask`. Ordered start is preserved and
changed lists renumber sequentially; task conversion does not renumber; removing
the last item deletes the list. Definitions use document-global MD4C/GFM label
equivalence: collisions and changes to surviving bindings are rejected atomically,
used definitions cannot be removed, and unused definitions are deleted with their
owner. There are still no list command entry points in the UI; internal
definitions before the first rendered paragraph cannot be combined with task
toggling. Task 018 adds exact top-level quote ranges and source-preserving
`WrapInQuote`/`UnwrapQuote` for the document parent: explicit `>` prefixes,
quoted blank lines, multiline paragraphs, closed fenced code and inline HTML are
supported; lazy continuations are rejected. Task 019 extends list/quote mapping
and the same commands to nested LI/QUOTE parents (depth 1–3) using a
prefix-preserving virtual content model; ordered numbering, tightness and
document-global definition bindings are preserved, and cascading quote deletion
is rejected.
See [Task 016](docs/tasks/task_016.md), [Task 017](docs/tasks/task_017.md),
[Task 018](docs/tasks/task_018.md) and [Task 019](docs/tasks/task_019.md).
The MD4C tests pass locally on Windows and Ubuntu, including Linux ASan/UBSan.
See [Task 002](docs/tasks/task_002.md) for limits, licensing and reproduction.
See [Task 012](docs/tasks/task_012.md) for structural edits and snapshot guarantees.
See [Task 013](docs/tasks/task_013.md) for the unified command contract and nested
mapping limits. Next: indent/outdent in [Task 020](docs/tasks/task_020.md), then
[GFM tables](docs/tasks/task_015.md); their remaining structural operations are
not implemented.
The agreed [reference-definition policy](docs/tasks/task_025.md) uses document-wide
scope: future structural edits reject normalized label collisions or changes to
surviving reference bindings, and reject removal of a definition still in use.
Definitions are not silently renamed or moved. Parser characterization is tested;
the definition inventory and structural commands are not yet implemented.
CI enables the MD4C regression suite on both platforms.

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
