# Markpad release checklist — Task 011

Status: in progress. Local automated gates and Windows captures are complete;
remote CI and owner sign-off are still required.

## Release scope

- Source branch: `feature/task_011`
- Release deliverable: Windows portable binary plus source; installer/signing/update
  channel are out of scope.
- Supported editing modes: Markdown, continuous WYSIWYG, paged WYSIWYG.
- Required platforms: Windows UCRT64 and Ubuntu/GCC; CI is the remote evidence.
- Paginated-mode/PDF acceptance is structural (page count, content presence,
  overflow warnings), not pixel-perfect equality.

## Automated gates

- [x] Clean Windows UCRT64 Debug configure/build/CTest.
- [x] Windows Release configure/build/CTest.
- [x] Ubuntu clean Debug configure/build/CTest.
- [x] Ubuntu Release configure/build/CTest.
- [x] Linux ASan/UBSan/leaks CTest.
- [x] MD4C regression suite (part of CTest on both platforms).
- [ ] GitHub Actions Linux and Windows jobs green on the pushed branch/PR.

## Native visual QA

Windows captures are in [docs/qa/task_011](qa/task_011/README.md). Linux evidence
is limited to automated CTest in this environment.

- [x] Main window: File/Edit/Settings toolbar icons visible, tooltips present.
- [x] Light theme menu/toolbar/workspace layout.
- [x] Dark theme menu/toolbar/workspace layout; dark WYSIWYG contrast regression covered.
- [x] Markdown mode with syntax highlighting and line numbers/gutter per current contract.
- [x] Continuous WYSIWYG: headings, inline formatting, quote/list, code block,
      link/image labels, table, horizontal rule, protected HTML/nested table.
- [x] Paged WYSIWYG: page boundaries and page count visible.
- [ ] Paged WYSIWYG: overflow warning for an oversized block.
- [x] Settings dialog: all sketch groups visible; deferred Auto Save/Line Numbers
      clearly disabled or deferred.
- [ ] Native File Open/Save/Save As dialogs open and cancel without data loss.
- [ ] Unsaved-changes close confirmation: Save, Discard, Cancel.
- [ ] Multi-window SDI behavior and file-open routing.
- [ ] Linux native visual QA (not captured; automated tests pass).

Owner sign-off: ____________________  Date: __________

## Export and print

- [ ] HTML export opens and matches supported GFM structure.
- [x] PDF export creates a readable file for a fixed fixture (automated suite).
- [x] Microsoft Print to PDF produces a readable A4 PDF on Windows.
      Evidence: `docs/qa/task_011/print-to-pdf.pdf`.
- [ ] Paged/PDF page-count divergence reviewed and accepted. The fixture reports
      **3 pages** in paged WYSIWYG while Microsoft Print to PDF produces **2 A4
      pages**. This is the documented Task 008 engine-vs-Qt-layout limitation;
      owner sign-off required.
- [ ] Oversized-block overflow is visible/warned.
- [ ] Physical printer test: not run; no physical job was sent.
- [ ] Linux CUPS/print-dialog test: not run; limitation recorded.

## Windows portable package

- [x] Release executable built.
- [x] `windeployqt` deploys required Qt DLLs/plugins.
- [x] `MD4C-LICENSE.md` included; project MIT `LICENSE` and Qt/GCC/third-party
      notices available under `licenses/`.
- [x] Package runs from a clean directory with Qt removed from `PATH`.
- [ ] Smoke-open, edit, save, switch modes, and export from the portable package.
- [x] Local artifact checksum generated (`markpad.exe.sha256`); final CI artifact
      checksum remains to be recorded.

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
- Physical printer and Linux CUPS checks may remain unrun and must be documented
  if so.

## Release decision

- [ ] All mandatory automated gates pass.
- [ ] Owner visual sign-off recorded.
- [ ] Portable package smoke test passes.
- [ ] Known limitations reviewed and accepted.
- [ ] Final tag/version and artifact checksum approved.

Decision: ____ ready / ____ blocked

Notes:
