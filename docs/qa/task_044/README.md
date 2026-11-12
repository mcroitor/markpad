# Task 044 page-sheet evidence

Windows 11, MSYS2 UCRT64, Qt 6.11.2, local Debug build.

| File | Contents |
|---|---|
| `paged.png` | Paged WYSIWYG opened from `fixture.md`; the A4 page is rendered at the screen's logical DPI (approximately 794 x 1123 device-independent pixels at 96 DPI), centered on the desk, with 20 mm content margins and justified body paragraphs. |
| `paged-page-gap.png` | Scrolled to the page break. Shows two separate page sheets, the gray gap, page numbers, and a table/code block kept whole on the first page. |
| `dark/*.png` | The same paged-mode views with the dark theme selected. |

The screenshots were produced by the local QA automation from the built
`build-task044/markpad.exe`; they are visual evidence only and do not replace
owner sign-off. The fixture is
[`../task_011/fixture.md`](../task_011/fixture.md).

Automated checks:

- `markpad_layout_tests`: page partition, view-DPI scaling, atomic table/code
  blocks, overflow warning, page-for-position.
- `markpad_smoke`: page sheet presence, A4-scale page width, equal left/right
  content margins, justified paragraph alignment, non-zero page margin on page
  2, source and token preservation, in-place editing and undo in paged mode.
- Windows Debug/Release CTest: 6/6.
