# Markpad release checklist — Task 011

Status: in progress. Local automated gates and Windows captures are complete; remote CI and owner sign-off are still required.

## Release scope

- Source branch: `feature/task_011`
- Release deliverable: Windows portable binary plus source; installer/signing/update channel are out of scope.
- Supported editing modes: Markdown, continuous WYSIWYG, paged WYSIWYG.
- Required platforms: Windows UCRT64 and Ubuntu/GCC; CI is the remote evidence.
- Paginated-mode/PDF acceptance is structural (page count, content presence, overflow warnings), not pixel-perfect equality.

## Automated gates

- [x] Clean Windows UCRT64 Debug configure/build/CTest.
- [x] Windows Release configure/build/CTest.
- [x] Ubuntu clean Debug configure/build/CTest.
- [x] Ubuntu Release configure/build/CTest.
- [x] Linux ASan/UBSan/leaks CTest.
- [x] MD4C regression suite (part of CTest on both platforms).
- [x] GitHub Actions Linux and Windows jobs green on the pushed branch:
      run [43155710816](https://github.com/mcroitor/markpad/actions/runs/43155710816), commit `d769c68`.

## Native visual QA

Windows captures are in [docs/qa/task_011](qa/task_011/README.md). Linux evidence is limited to automated CTest in this environment.

- [x] Main window: File/Edit/Settings toolbar icons visible, tooltips present.
- [x] Light theme menu/toolbar/workspace layout.
- [x] Dark theme menu/toolbar/workspace layout; dark WYSIWYG contrast regression covered.
- [x] Markdown mode with syntax highlighting and line numbers/gutter per current contract.
- [x] Continuous WYSIWYG: headings, inline formatting, quote/list, code block, link/image labels, table, horizontal rule, protected HTML/nested table.
- [x] Paged WYSIWYG: page boundaries and page count visible.
- [ ] Paged WYSIWYG: overflow warning for an oversized block.
- [x] Settings dialog: all sketch groups visible; deferred Auto Save/Line Numbers clearly disabled or deferred.
- [x] Native File Open/Save/Save As dialogs exercised from the portable package: opened the fixture and saved an identical copy. Evidence: `docs/qa/task_011/release-qa/saved-copy.md`.
- [x] Unsaved-changes close confirmation exercised with Save, Discard and Cancel. Evidence: `docs/qa/task_011/release-qa/unsaved-close.png` and `close-save.md`.
- [x] Multi-window SDI behavior exercised: File > New produced two Markpad windows (`fixture.md` and `Untitled`).
- [ ] Linux native visual QA (not captured; automated tests pass).

Owner sign-off: ____________________  Date: __________

## Export and print

- [x] HTML export opens and matches supported GFM structure from the Windows portable package. Evidence: `docs/qa/task_011/release-qa/export.html`.
- [x] PDF export creates a readable file for a fixed fixture (automated suite).
- [x] Microsoft Print to PDF produces a readable A4 PDF on Windows. Evidence: `docs/qa/task_011/print-to-pdf.pdf`.
- [ ] Paged/PDF page-count divergence reviewed and accepted. The fixture reports **3 pages** in paged WYSIWYG while Microsoft Print to PDF produces **2 A4 pages**. This is the documented Task 008 engine-vs-Qt-layout limitation; owner sign-off required.
- [x] Oversized-block overflow is visible/warned in the paged status bar. Evidence: `docs/qa/task_011/overflow/paged.png` and `overflow.md`.
- [ ] Physical printer test: not run; no physical job was sent.
- [ ] Linux CUPS/print-dialog test: not run; limitation recorded.

## Windows portable package

- [x] Release executable built.
- [x] `windeployqt` deploys required Qt DLLs/plugins.
- [x] `MD4C-LICENSE.md` included; project MIT `LICENSE` and Qt/GCC/third-party notices available under `licenses/`.
- [x] Package runs from a clean directory with Qt removed from `PATH`.
- [x] Smoke-open, edit, save, switch modes, and export from the portable package.
- [x] CI artifact recorded: 176 files, `markpad.exe` SHA-256 `5162839752ea55857f97caf35f89b1fd78ff4b832eabdfa49ed47b9157fc6676`, from commit `d769c68` / Qt 6.12.0. The artifact launches with Qt removed from `PATH`.

## Known limitations accepted for this release

- Nested fenced-code and nested-table editing remain protected placeholders.
- Raw HTML is not rendered/executed; it remains protected text.
- Link/image destinations and titles are not editable in WYSIWYG.
- Rich/multiline table-cell content is unsupported.
- Syntax highlighting and language pickers for code blocks are unsupported.
- Auto Save is deferred.
- Installer, signing and update channels are out of scope.
- Paged/PDF page-count divergence (3 vs 2 on the QA fixture) follows the accepted
  Task 008 engine-vs-Qt-layout contract and requires owner sign-off.
- Physical printer and Linux CUPS checks may remain unrun and must be documented if so.

## Release decision

- [x] All mandatory automated gates pass.
- [ ] Owner visual sign-off recorded.
- [ ] Portable package smoke test passes.
- [ ] Known limitations reviewed and accepted.
- [ ] Final tag/version and artifact checksum approved.

Decision: ____ ready / _X__ blocked

Notes:

## Problem founded

### Dark theme

Tracking: [Task 045](tasks/task_045.md). Status: fixed on `feature/task_045`;
owner re-review required (`dark/menu.png`, `dark/settings.png`).

1. Any menu is not seen in the dark mode (text color is dark on dark background).
2. In the Settings window the most options are unseen in dark mode (text color is white on light background).

### Continuous WYSIWYG

Tracking: [Task 046](tasks/task_046.md).

1. The common text is written on light blue background, it looks unnecessary and distracting. Keep the default background color (white in light mode and dark in dark mode).
2. Code blocks are not shown as a blocks: exists white lines between the code lines. Let it be visually distinct with a proper block background.
3. Table rendering problem: table width is bigger than the viewport. No horizontal scrolling is available. But table must be fully visible, and the best solution is re-rendering it to fit within the viewport.

### Paged WYSIWYG

Tracking: [Task 044](tasks/task_044.md), with shared visual fixes from
[Task 045](tasks/task_045.md) and [Task 046](tasks/task_046.md).

1. Page layout does not match the expected output. Each page must be seen as a page. Take a look to the Microsoft Word or PDF rendering as a reference for the expected layout.
2. The same issues observed in Continuous WYSIWYG (background color, code block rendering, table width) also affect Paged WYSIWYG and must be addressed consistently.

### PDF export

Tracking: [Task 047](tasks/task_047.md).

1. The exported PDF does not match the expected layout. Or page size is incorrect, or text size is incorrect. The PDF rendering must be equivalent to the expected layout as seen in the Paged WYSIWYG or other reference renderings.

### Toolbar

Tracking: [Task 048](tasks/task_048.md).

1.The toolbar buttons are not workable in the Markdown editor. They do not respond to clicks or perform the expected actions.
2. Pressing to the header buttons or to the switch mode button does not activate drop-down menu.

### Settings

Tracking: [Task 049](tasks/task_049.md).

1. The shortcut tab is unuseful, because the User does not know any actions associated with the listed shortcuts. Must be completed with the default actions.
