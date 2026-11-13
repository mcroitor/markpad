# Markpad - Yet Another Markdown Editor

Markpad is a C++ Markdown editor for writing multipage documents. It uses a
single GFM document with three editable views and exports the result to HTML and
PDF.

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Build and run](#build-and-run)
- [Using Markpad](#using-markpad)
- [Markdown engine and source-preserving edits](#markdown-engine-and-source-preserving-edits)
- [Testing and CI](#testing-and-ci)
- [Known limitations](#known-limitations)
- [Release status](#release-status)
- [License](#license)
- [Design documents](#design-documents)

## Features

- Three editable modes for one GitHub Flavored Markdown (GFM) document:
  Markdown source, continuous WYSIWYG and paginated WYSIWYG.
- Multipage editing in a Word/WordPad-like page layout.
- Markdown source highlighting and contextual formatting actions.
- Open, save and Save As with explicit handling of encoding, BOM and errors.
- Safe HTML export, PDF export and printing.
- Settings for page layout, theme, editor preferences and keyboard shortcuts.
- Optional display of spaces, tabs, line breaks and paragraph ends in WYSIWYG.

## Requirements

- C++20 compiler, CMake and Ninja.
- Qt 6 Widgets, Test and PrintSupport modules.
- MD4C, fetched by CMake from a pinned source archive during the first
  configure. That step requires network access to GitHub.
- Supported build environments: Windows with MSYS2 UCRT64 and Ubuntu with the
  Qt 6 packages listed below.

## Build and run

### Windows (MSYS2 UCRT64)

Install the UCRT64 toolchain, CMake, Ninja and Qt 6 Base from an **MSYS2 UCRT64**
terminal:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base
```

From PowerShell at the repository root, configure, build, test and run:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;$env:PATH"
& "C:\msys64\ucrt64\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="C:\msys64\ucrt64"
& "C:\msys64\ucrt64\bin\cmake.exe" --build build --parallel
& "C:\msys64\ucrt64\bin\ctest.exe" --test-dir build --output-on-failure
.\build\markpad.exe
```

Keep `C:\msys64\ucrt64\bin` on `PATH` when running the application or tests so
Windows can find the Qt runtime DLLs.

For a redistributable Release directory, build with
`-DCMAKE_BUILD_TYPE=Release`, then run the packaging script from an MSYS2 UCRT64
shell:

```sh
bash scripts/package_windows.sh build-release package-windows 0.1.0
```

The script deploys the Qt/GCC runtime DLLs, copies `LICENSE`, `MD4C-LICENSE.md`,
README and third-party license texts, and writes `markpad.exe.sha256`. CI uploads
the same directory as the `markpad-0.1.0-windows-ucrt64` artifact.

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

### Run with a file

Pass file paths as command-line arguments:

```sh
markpad document.md
```

Local `QFileOpenEvent` requests are handled. OS file associations and
single-instance IPC are not installed.

## Using Markpad

### Documents and windows

Markpad uses a single-document interface (SDI): each document has its own
window, session and undo/redo history. The available file commands are New,
Open, Save, Save As and guarded Close/Exit.

- **New** always creates a window.
- **Open** reuses only a clean, empty, unnamed window.
- Opening a dirty session requires explicit discard permission.
- Failed open, save, export and print operations preserve the current session
  state and report an error.

The Markdown editor provides visual syntax highlighting, standard editing
shortcuts, an Edit context menu and a cursor/dirty status bar. The main toolbar
uses theme-aware icon-only buttons with tooltips. Formatting actions apply to the
Markdown source and to editable WYSIWYG content where supported.

### Modes

Use **View > Switch Mode** to select one of three editable views:

1. **Markdown** - the GFM source with heuristic highlighting.
2. **WYSIWYG (Continuous layout)** - formatted content in a continuous view.
3. **WYSIWYG (Page layout)** - the same content laid out on page sheets.

All modes share one session, Save and undo/redo history. Switching modes does not
serialize or modify the source. Cursor and selection mapping is exact for mapped
text; protected positions use an explained approximation. Switching back without
changes restores the original selection, including delimiter-boundary affinity.
See [Task 026](docs/tasks/task_026.md), [Task 027](docs/tasks/task_027.md) and
[Task 005](docs/tasks/task_005.md).

Continuous WYSIWYG is accepted for the supported top-level GFM scope
([Task 042](docs/tasks/task_042.md)). It is not a complete GFM WYSIWYG
implementation: features outside that scope remain follow-ups.

### Continuous WYSIWYG

#### Text and block formatting

Continuous mode formats headings, paragraphs, emphasis/strong/strikethrough,
lists and quotes.

- **Bold**, **Italic** and **Strikethrough** operate on a nonempty selection
  within one editable text run. Bold and Italic also have standard keyboard
  shortcuts.
- Repeating a style removes it only when the entire simple span is selected.
  Selections and their direction are retained after formatting.
- Partial style removal, cross-run formatting and formatting of future input are
  not supported yet.
- **Format > Heading** and **Toggle task checked** are available only where the
  source map proves the operation safe.
- **Format > Blockquote** toggles a quote around the current paragraph, heading,
  code block or containing list.
- **Format > List > Ordered/Unordered** converts a top-level paragraph into a
  list or changes an existing list kind.

Unavailable commands are disabled with an explanatory tooltip. Unsupported
keyboard requests report an error. Multi-block selections and unsupported
containers are rejected or disabled with an explanation. Delimiter collisions
that would change the surrounding Markdown semantics are rejected rather than
normalized globally.

Other edits, including Enter, cross-run replacements and deleting an entire text
run, explicitly return the user to Markdown. Empty documents start in Markdown.

#### Lists and line breaks

- List markers use the system text color without a background. Each nesting level
  adds one indentation step.
- Deleting a list marker detaches a top-level simple or task item into a
  paragraph and splits the list. Nested lists and multi-block items still require
  Markdown.
- Soft and hard line breaks are editable. Deleting the break character joins the
  lines with a space.
- **View > Show Special Characters** (default off, `Ctrl+Shift+8`) overlays spaces
  as centered dots, tabs as arrows, line breaks as `↵` and paragraph ends as `¶`.
  This is a view-only overlay and does not change the document.
- The permanent limited-WYSIWYG banner has been removed. Approximate cursor
  mapping and other situational notices appear in the status bar.

#### Code

- Top-level fenced code bodies appear in a monospace frame and are editable,
  including adding and removing lines.
- Fences and info strings stay protected.
- **Format > Code** inserts an empty fenced code block.
- **Horizontal Line** inserts and renders `---`.
- Inline code spans are shown in monospace and their content is locally editable.

#### Tables

- Top-level GFM tables render as visual grids. Existing cell text is editable.
- Row/column insertion and removal, column alignment and Delete Table are
  available from the continuous-editor context menu.
- Empty cells in top-level outer-piped tables accept plain single-line text,
  including newly inserted rows and columns.
- **Format > Table** and the toolbar Table action prompt for a table size
  (columns and body rows), then insert editable body cells.
- HTML and unsupported/nested tables remain read-only placeholders.

Multiline or rich cell content, drag-grid size selection, indented/nested code,
code language pickers and code syntax highlighting are not supported yet.

#### Links and images

Links show their labels as inert text, never as clickable Qt anchors. An editable
mapped label run can be changed in inline and full-reference links while the
address, title and reference definitions remain unchanged.

- **Format > Link** and the toolbar Link action create an inline link from a
  selection inside one editable text run after prompting for a URL.
- **Format > Image** and the toolbar Image action create an inert inline image
  using the selected text as alt text and a prompted source URL.
- Images are rendered as inert styled alt labels. Alt text is locally editable;
  source, title and reference definitions stay unchanged.
- Label/alt formatting commands and address edits require Markdown.
- Autolink text is visible but read-only. Shortcut/collapsed reference edits that
  change the binding are explicitly rejected.
- Click, Ctrl+click and keyboard navigation do not open links, for any URL
  scheme. External resources are never loaded.

Link and image titles and reference-style creation remain unsupported. See
[Task 028](docs/tasks/task_028.md).

### Document model and file formats

`markpad_document` is a standard C++20 library without Qt dependencies.
`markpad_files` provides a Qt Core adapter using `QSaveFile` with direct-write
fallback disabled. Document tests run without a GUI.

- The model stores strict UTF-8 and preserves line endings.
- Files support UTF-8, UTF-16 LE/BE and UTF-32 LE/BE.
- BOM detection takes precedence. Without a BOM, valid UTF-8 is preferred;
  otherwise a conservative zero-byte-pattern heuristic is used.
- Uncertain detection returns an error, and callers may specify an encoding.
  Detection is not guaranteed for every BOM-less file, especially non-ASCII text.
- Invalid Unicode and U+0000 are rejected rather than replaced.
- Save preserves encoding and BOM; Save As may select a different format.
- A literal leading U+FEFF requires a transport BOM to avoid losing it on the
  next open.
- Dirty state compares source and format with the last successful open/save.
  Failed operations preserve session state.
- The UI offers explicit encoding retry on decode errors and encoding/BOM
  selection on Save As.
- External-file conflict detection is not implemented.

See [Task 003](docs/tasks/task_003.md).

The source editor preserves untouched Unicode and line endings, including mixed
CRLF/CR/LF files. Newly inserted line breaks use the source's first EOL sequence,
or LF for a new document. Undo restores exact source content; the widget is a
projection, not a second canonical document.

History currently stores full source snapshots per edit, without a large-file
performance guarantee. Highlighting is heuristic, not a GFM parser. Raw HTML
remains inert text and no external resources are loaded. GUI tests inject dialogs
and run offscreen. See [Task 004](docs/tasks/task_004.md).

### Paged WYSIWYG

Paged mode shows the document on centered page sheets with desk gaps, page
numbers and an overflow warning where needed.

- Page sizes: A4, Letter and Legal.
- Orientation: portrait or landscape.
- Configurable margins; page counts and current page are shown in the status bar.
- Page geometry scales to screen DPI. Body paragraphs are justified; headings,
  code and table cells retain their own alignment.
- Tables and fenced code blocks are placed as whole page units.
- The paged canvas uses one editable `QTextDocument` and the shared `PagePlan`.
  Pages do not add spacer blocks or change document formatting.
- Editing and selection work across page sheets.
- An oversized block expands its sheet with an explicit warning instead of being
  clipped. A too-wide block is also reported.
- Page layout and PDF use the same plan, so page count and block order match on
  supported fixtures.

The paged canvas uses the native `QTextCursor` for keyboard and selection, but a
full `QAccessibleTextInterface` is not implemented yet. System IME and native
visual checks remain release checks.

See [Task 054](docs/tasks/task_054.md) for the current page canvas,
[Task 044](docs/tasks/task_044.md) for the earlier page-sheet presentation, and
[Task 006](docs/tasks/task_006.md) for the original paged mode. Task 054 replaced
the spacer/margin-based page surface with the shared `PagePlan`; Task 047 aligned
PDF and print with the same plan.

### Export and printing

#### HTML

**File > Export HTML** writes a safe HTML rendition of the shared GFM AST.

- Supported structures include headings, paragraphs, lists/tasks, tables, code,
  emphasis, links and images.
- Raw HTML is escaped, and unsafe URI schemes such as `javascript:`, `data:` and
  `file:` are omitted.
- Export writes UTF-8 through `QSaveFile` and reports open, write and commit
  failures.

See [Task 007](docs/tasks/task_007.md).

#### PDF and print

PDF export and printing use the same `PagePlan` and WYSIWYG projection as the
paged canvas.

- Page size, margins, typography, block order and page breaks follow the paged
  layout.
- An oversized or too-wide block is refused with an explicit error instead of
  being clipped or split.
- Raw HTML remains inert; unsafe content and external resources are not
  executed.
- PDF output is written atomically; existing files are not damaged on failure.
- Print errors and cancellation are reported.

Native visual PDF QA and real printer hardware remain manual release checks. See
[Task 008](docs/tasks/task_008.md) and
[Task 047](docs/tasks/task_047.md).

### Settings and localization

Settings are edited in **Edit > Settings** using the General, Editor, Page and
Shortcut tabs. The versioned settings service stores:

- theme and language;
- editor font and size;
- word wrap and default file location;
- validated keyboard shortcut overrides;
- global page defaults (A4 portrait, 20 mm margins);
- per-block WYSIWYG typography: body/heading/code/quote font families and sizes,
  heading bold and quote italic.

Settings are stored under the platform Markpad config directory. English is
built in; additional languages are loaded from `lang/*.ini` with English fallback.
Changes apply immediately except language, which takes effect after restart.

See [Task 009](docs/tasks/task_009.md).

## Markdown engine and source-preserving edits

The verified source-preserving API lives in `markpad_markdown`
(`src/markdown_document.*`) and is used by both the WYSIWYG UI and exports.

### MD4C dependency

MD4C is a required build dependency. CMake fetches a pinned, SHA256-checked source
archive during the first configure; network access to GitHub is required then. No
system-wide package is installed. CMake copies the upstream MIT notice to
`MD4C-LICENSE.md` in the build directory; include it when distributing binaries.

### Edit contract

- The shared API builds a callback tree and checks localized text and task edits,
  preserving all source bytes outside the changed range.
- `applyEdit` checks document identity, revision and source digest.
- Successful changes return a reparsed snapshot. A no-op preserves the original
  revision.
- Selected inline-style wrapping and simple unwrapping are supported
  ([Task 027](docs/tasks/task_027.md)).
- Parent links and evidence envelopes describe known source bytes, **not**
  complete container ownership.
- Quote prefixes and bytes outside the changed block are preserved.
- Unsupported forms return an explicit error.
- This is **not** a complete block serializer or a full GFM conformance suite.

See [Task 012](docs/tasks/task_012.md) and
[Task 013](docs/tasks/task_013.md).

### Supported structures and commands

- **Paragraphs and headings:** partial block map and paragraph/ATX heading
  conversions, including single-line blocks within explicit nested quotes.
- **Lists:** read-only `mapList` ownership ranges for canonical top-level lists
  and their items/content lines, including multiline paragraphs and closed fenced
  code. Ranges are checked against the parsed subtree and absolute callback
  offsets; trailing separator lines remain outside the list. Nested containers,
  lazy continuations, structural tabs, indented code and external definition
  contexts outside the supported canonical form return diagnostics.
- **List commands:** source-preserving `InsertListItem`, `RemoveListItem`,
  `SetListKind` and `SetItemTask`. Ordered start is preserved and changed lists
  renumber sequentially; task conversion does not renumber; removing the last item
  deletes the list.
- **Quotes:** exact top-level quote ranges and source-preserving
  `WrapInQuote`/`UnwrapQuote` for the document parent. Explicit `>` prefixes,
  quoted blank lines, multiline paragraphs, closed fenced code and inline HTML are
  supported; lazy continuations are rejected.
- **Nested containers:** list/quote mapping and the same commands extend to nested
  LI/QUOTE parents at depth 1-3 using a prefix-preserving virtual content model.
  Ordered numbering, tightness and document-global definition bindings are
  preserved; cascading quote deletion is rejected.
- **List indentation:** atomic `IndentListItem`/`OutdentListItem` move the selected
  item with its subtree. Ordered starts are preserved, only affected lists are
  renumbered, and empty nested lists are removed without moving siblings or
  cascading parent containers.
- **Tables:** exact top-level GFM table mapping covers table, header, delimiter
  and body-row ranges, raw cell fragments with escaped pipes and inline spelling,
  dimensions and alignment. Bound references are validated in document context.
- **Table commands:** `InsertTableRow`, `RemoveTableRow` and `SetTableAlignment`
  use bounded normalization, plain-text escaping for new cells, raw preservation
  of existing cells and byte-exact alignment no-ops. `InsertTableColumn`,
  `RemoveTableColumn` and explicit `RemoveTable` leave neighbouring bytes
  untouched. Last-column removal and cascading container deletion are rejected.
- **Nested tables:** table mapping and all six table commands also work for tables
  nested in LI/QUOTE containers at depth 1-3, with the required `RemoveTable`
  refusals for LI marker-line and sole-block tables.
- **Definitions:** document-global MD4C/GFM label equivalence is enforced.
  Collisions and changes to surviving reference bindings are rejected atomically;
  used definitions cannot be removed, and unused definitions are deleted with
  their owner.

See Tasks [016](docs/tasks/task_016.md), [017](docs/tasks/task_017.md),
[018](docs/tasks/task_018.md), [019](docs/tasks/task_019.md),
[020](docs/tasks/task_020.md), [021](docs/tasks/task_021.md),
[022](docs/tasks/task_022.md), [023](docs/tasks/task_023.md) and
[024](docs/tasks/task_024.md).

### WYSIWYG commands built on the engine

- **Blockquote** and **Ordered/Unordered** are exposed in the continuous UI.
  List item insert/remove/indent/outdent and list removal remain internal or
  require Markdown.
- Top-level fenced-code body editing keeps fence and info bytes protected.
- Top-level tables render as `QTextTable`; existing cell text can be edited
  without rewriting the delimiter row.
- Continuous table context-menu actions use the engine commands for row/column
  insertion and removal, alignment and Delete Table.
- Empty outer-piped cells accept plain-text entry.
- **Format > Table** inserts a default 2-column table in continuous mode and
  prompts for a size when requested.
- **Format > Code**, **Horizontal Line** and inline-code rendering/editing are
  supported.
- Inline-link and inline-image creation use the selected text and a URL/source
  prompt.

See Tasks [030](docs/tasks/task_030.md),
[031](docs/tasks/task_031.md), [033](docs/tasks/task_033.md),
[034](docs/tasks/task_034.md), [035](docs/tasks/task_035.md),
[036](docs/tasks/task_036.md), [037](docs/tasks/task_037.md),
[038](docs/tasks/task_038.md), [039](docs/tasks/task_039.md),
[040](docs/tasks/task_040.md) and [041](docs/tasks/task_041.md).

Definitions before the first rendered paragraph cannot be combined with task
toggling. Multiline/rich cell content, drag-grid size selection, code language
pickers, link/image titles and reference-style links remain planned.

### Reference-definition policy

The agreed [reference-definition policy](docs/tasks/task_025.md) uses
document-wide scope. Future structural edits reject normalized label collisions
or changes to surviving reference bindings, and reject removal of a definition
still in use. Definitions are not silently renamed or moved. The definition
inventory and structural commands are part of the supported source-preserving
edit pipeline.

## Testing and CI

- Clean builds and automated tests pass locally on Windows UCRT64 and Ubuntu in
  WSL.
- GUI tests run offscreen and inject dialogs.
- CI enables the MD4C regression suite on both platforms.
- Linux sanitizer runs include ASan/UBSan.
- Remote CI, native-dialog checks and visual manual QA remain release checks.

### Optional MD4C regression suite

The extended MD4C regression suite is opt-in. Add
`-DMARKPAD_BUILD_MD4C_SPIKE=ON` (keeping the Windows Qt prefix and `PATH` when
applicable):

```sh
cmake -S . -B build-md4c -G Ninja -DCMAKE_BUILD_TYPE=Debug -DMARKPAD_BUILD_MD4C_SPIKE=ON
cmake --build build-md4c --parallel
ctest --test-dir build-md4c --output-on-failure
ctest --test-dir build-md4c -L md4c --verbose
```

The MD4C tests pass locally on Windows and Ubuntu, including Linux ASan/UBSan.
See [Task 002](docs/tasks/task_002.md) for limits, licensing and reproduction.
[Task 015](docs/tasks/task_015.md) (tables) is accepted.

### Earlier cmark-gfm spike

The earlier `cmark-gfm`/Qt comparison remains available separately through
`-DMARKPAD_BUILD_GFM_SPIKE=ON`. It is disabled by default, is not required by
MD4C, and is no longer the focus of development.

## Known limitations

- Continuous WYSIWYG does not cover every GFM construct. Nested code/table
  editing and rich HTML remain follow-ups.
- Some formatting operations require Markdown: partial/cross-run formatting,
  link/image addresses and titles, and reference-style creation.
- Multiline/rich table cells, drag-grid selection, indented/nested code, code
  language pickers and code syntax highlighting are unsupported.
- Detaching nested-list markers and multi-block list items requires Markdown.
- The paged canvas has no full accessible text interface; system IME and native
  visual checks remain manual.
- PDF/print visual parity is structural, not pixel-perfect. Native visual QA and
  physical printers remain manual checks.
- Encoding detection is not guaranteed for every BOM-less file, especially
  non-ASCII text.
- Editor history stores full source snapshots per edit; no large-file performance
  guarantee is made.
- External-file conflict detection, OS file associations and single-instance IPC
  are not implemented.
- Installer, signing and update channels are out of scope.

## Release status

The current release deliverable is a Windows portable binary. The exact gates,
screenshots, print checks, packaging steps and remaining limitations are tracked
in the [release checklist](docs/release_checklist.md).

## License

Markpad is licensed under the MIT License.

For more details, see the [LICENSE](LICENSE) file.

## Design documents

- [UI sketch](sketch.md)
- [Architecture](ARCHITECTURE.md)
- [Development task backlog](docs/tasks/README.md)
