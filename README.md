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

For a redistributable Release directory, build with `-DCMAKE_BUILD_TYPE=Release` and then run the packaging script from an MSYS2 UCRT64 shell:

```sh
bash scripts/package_windows.sh build-release package-windows 0.1.0
```

The script deploys the Qt/GCC runtime DLLs, copies `LICENSE`, `MD4C-LICENSE.md`, README and third-party license texts, and writes `markpad.exe.sha256`. CI uploads the same directory as the `markpad-0.1.0-windows-ucrt64` artifact.

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
The main toolbar uses theme-aware icon-only buttons with tooltips; menu actions
and object names remain unchanged.
Continuous and paginated WYSIWYG modes are available through **View > Switch
mode**, alongside HTML/PDF export and persistent settings. The Task 005
continuous scope is accepted for supported top-level GFM constructs; nested
code/table editing, rich HTML rendering and native visual release QA remain
documented follow-ups.

Pass file paths as command-line arguments (for example, `markpad document.md`).
Local `QFileOpenEvent` requests are handled too; OS file associations and
single-instance IPC are not installed. Clean builds and automated tests pass
locally on Windows UCRT64 and Ubuntu in WSL. Task 005 is accepted for the
supported top-level scope; remote CI, native-dialog and visual manual QA remain
release checks.

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

### Continuous WYSIWYG

Continuous mode formats headings, paragraphs, emphasis/strong/strikethrough and
list/quote content. Blue-background text runs support local text edits; grey
fragments/placeholders are protected. **Format > Heading** and **Toggle task
checked** are available only where the source map proves the operation safe.
**Format > Blockquote** toggles a quote around the current paragraph, heading,
code block or containing list. **Format > List > Ordered/Unordered** converts a
top-level paragraph into a list or changes an existing list kind. These actions
are enabled contextually; multi-block selections and unsupported containers are
rejected or disabled with an explanation. Top-level fenced code bodies are shown
in a monospace frame and can be edited, including adding/removing lines; fences
and info strings stay protected.
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
Markdown. HTML and unsupported/nested tables remain read-only placeholders;
top-level GFM tables render as visual grids, their existing cell text is
editable, and row/column/alignment/delete actions are available from the
continuous-editor context menu. Empty cells in top-level outer-piped tables
accept plain single-line text, including newly inserted rows/columns. Format >
Table/toolbar Table prompts for a table size (columns and body rows) and
inserts it with editable body cells. Format > Code inserts an empty fenced code
block, and Horizontal Line inserts and renders `---`. Inline code spans are
shown in monospace and their content is locally editable. Multiline/rich cell
content, drag-grid size selection, indented/nested code, code language pickers
and syntax highlighting are not supported yet. No external resources are
loaded.

Links show their labels as inert text, never as clickable Qt anchors. You can
edit a mapped label run in inline/full-reference links while keeping the address,
title and reference definitions unchanged. Format > Link/toolbar Link creates an
inline link from a selection inside one editable text run after prompting for a
URL; titles and reference-style creation remain unsupported. Format >
Image/toolbar Image similarly creates an inert inline image using the selected
text as alt text and a prompted source URL. Images are rendered as inert styled
alt labels: alt text is locally editable, while source, title and reference
definitions stay unchanged and resources are never loaded. Label/alt formatting
commands and
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
owner. Task 030 exposes Blockquote and Ordered/Unordered in the continuous UI;
list item insert/remove/indent/outdent and list removal remain internal or
require Markdown. Task 031 adds top-level fenced-code body editing while keeping
fence/info bytes protected. Task 033 renders top-level tables as QTextTable and
allows existing cell text edits without rewriting the delimiter row. Task 034
adds continuous context-menu row/column insertion/removal, column alignment and
Delete Table through the existing engine commands. Task 035 adds plain-text
entry for empty outer-piped cells. Task 036 enables Format > Table/toolbar
Table to insert a default 2-column table in continuous mode. Task 037 adds
Format > Code and Horizontal Line insertion for empty fenced code blocks and
`---`. Task 038 adds inline-link creation from a single editable selection via
URL prompt; Task 039 adds inline-image creation from a selected alt label via
source prompt; Task 040 adds a table size prompt; Task 041 adds inline code
rendering and editing. Multiline/rich cell content, drag-grid size selection,
code language pickers, link/image titles and reference-style links remain
planned.
Definitions before the first rendered paragraph cannot be combined with task
toggling. Task 018 adds exact top-level quote
ranges and
source-preserving `WrapInQuote`/`UnwrapQuote` for the document parent: explicit
`>` prefixes,
quoted blank lines, multiline paragraphs, closed fenced code and inline HTML are
supported; lazy continuations are rejected. Task 019 extends list/quote mapping
and the same commands to nested LI/QUOTE parents (depth 1–3) using a
prefix-preserving virtual content model; ordered numbering, tightness and
document-global definition bindings are preserved, and cascading quote deletion
is rejected. Task 020 adds atomic `IndentListItem`/`OutdentListItem`: the selected
item moves with its subtree, ordered starts are preserved, only affected lists
are renumbered, and empty nested lists are removed without moving siblings or
cascading parent containers. Task 021 adds exact top-level GFM table mapping:
table/header/delimiter/body-row ranges, raw cell fragments with escaped pipes and
inline spelling, dimensions and alignment; bound references are validated in
document context. Task 022 adds `InsertTableRow`, `RemoveTableRow` and
`SetTableAlignment` with bounded normalization, plain-text escaping for new
cells, raw preservation of existing cells and byte-exact alignment no-ops.
Task 023 adds `InsertTableColumn`, `RemoveTableColumn` and explicit
`RemoveTable`; last-column removal and cascading container deletion are rejected,
and table removal leaves neighbouring bytes untouched. Task 024 extends table
mapping and all six table commands to tables nested in LI/QUOTE containers,
including depth 1–3, while enforcing the required RemoveTable refusals for LI
marker-line and sole-block tables.
See [Task 016](docs/tasks/task_016.md), [Task 017](docs/tasks/task_017.md),
[Task 018](docs/tasks/task_018.md), [Task 019](docs/tasks/task_019.md),
[Task 020](docs/tasks/task_020.md), [Task 021](docs/tasks/task_021.md),
[Task 022](docs/tasks/task_022.md), [Task 023](docs/tasks/task_023.md) and
[Task 024](docs/tasks/task_024.md).
Task 007 adds safe HTML export through File → Export HTML. The renderer walks the
shared GFM AST, supports headings, paragraphs, lists/tasks, tables, code,
emphasis, links and images, escapes raw HTML, and omits unsafe URI schemes
(`javascript:`, `data:`, `file:`, etc.). Export writes UTF-8 through `QSaveFile`
and reports open/write/commit failures. Task 009 adds a versioned user settings
service under the platform Markpad config directory: theme, language, editor
font/size, word wrap, default file location, validated shortcut overrides and
global page defaults (A4 portrait, 20 mm). The Page tab also exposes per-block
WYSIWYG typography: body/heading/code/quote font families and sizes, heading
bold and quote italic. English is built in; extra languages are loaded from
`lang/*.ini` with English fallback. Settings are edited in Edit → Settings using
General, Editor, Page and Shortcut tabs. Changes apply immediately except the
language, which takes effect after restart. Task 006 adds an editable paged
WYSIWYG mode with a deterministic page-layout engine: A4/Letter/Legal,
portrait/landscape, configurable margins, block-based page placement, page
count/navigation in the status bar and an overflow warning for blocks taller than
the page. The current page view is a guided editable rich-text surface rather
than true page sheets; the block-based page-surface redesign is tracked in
[Task 044](docs/tasks/task_044.md). Task 008 adds PDF export and printing through Qt
PrintSupport using the same page geometry and safe HTML rendering policy; PDF
output is written atomically and errors are reported. Native visual PDF QA and
real printer hardware remain manual checks.
The MD4C tests pass locally on Windows and Ubuntu, including Linux ASan/UBSan.
See [Task 002](docs/tasks/task_002.md) for limits, licensing and reproduction.
See [Task 012](docs/tasks/task_012.md) for structural edits and snapshot guarantees.
See [Task 013](docs/tasks/task_013.md) for the unified command contract and nested
mapping limits. Task 015 (tables) is accepted. The current release deliverable is
a Windows portable binary; the exact gates, screenshots, print checks, packaging
steps and known limitations are tracked in the
[release checklist](docs/release_checklist.md). Installer/signing/update channels
remain out of scope.
The agreed [reference-definition policy](docs/tasks/task_025.md) uses document-wide
scope: future structural edits reject normalized label collisions or changes to
surviving reference bindings, and reject removal of a definition still in use.
Definitions are not silently renamed or moved; the definition inventory and
structural commands are part of the supported source-preserving edit pipeline.
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
