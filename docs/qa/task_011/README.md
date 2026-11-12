# Task 011 QA evidence — Windows

These screenshots were captured from the Windows portable Release package on
Windows 11 with MSYS2 UCRT64, Qt 6.11.2. They are QA evidence, not a claim of
owner sign-off.

## Captures

| File | Meaning |
| --- | --- |
| `markdown.png` | Markdown mode, light theme, fixture loaded. |
| `continuous.png` | Continuous WYSIWYG, light theme, menu closed. |
| `paged.png` | Paged WYSIWYG, light theme, page count visible. |
| `settings.png` | Settings dialog with deferred Auto Save and Line Numbers controls. |
| `dark/*.png` | The same four views with the dark theme selected. |
| `print-to-pdf.pdf` | Output from Microsoft Print to PDF through the native print dialog. |
| `print-to-pdf.txt` | `pdftotext` extraction of the printed PDF for quick content review. |
| `fixture.md` | Source fixture used for the screenshots and print check. |
| `ci-artifact.txt` | Final GitHub Actions run, artifact checksum and clean-PATH launch evidence. |

## Findings and follow-up

- Dark-theme WYSIWYG initially used light editable-run, code, and table-header
  backgrounds while text stayed light. The palette-aware colors and a runtime
  re-render were added; `continuousDarkThemeColors` is the regression test.
- Markdown source highlighting previously matched the inner part of `**bold**`
  as italic. Strong spans are now highlighted separately from emphasis and are
  covered by `markdownHighlightingDistinguishesBoldAndItalic`.
- The Settings dialog is now organized as General, Editor, Page and Shortcut
  tabs; `settings.png` and `dark/settings.png` were recaptured after that change.
- The paged view reported **Page 1 of 3**, while Microsoft Print to PDF produced
  **2 A4 pages**. This is the documented Task 008 limitation: the paged status
  uses `PageLayoutEngine`, while Qt text layout performs the actual print/PDF
  pagination. Owner sign-off must accept this structural difference; pixel-perfect
  equality was never an acceptance criterion.
- Microsoft Print to PDF was exercised successfully. No physical printer job was
  sent. Linux CUPS/print-dialog QA was not run in this environment.
- Linux native visual QA was not captured; Linux automated CTest evidence is
  reported separately.
- `settings.png` confirms the deferred Auto Save and Line Numbers controls remain
  disabled with explanatory text.

Owner sign-off is recorded in [release_checklist.md](../../release_checklist.md),
not in this evidence file.
