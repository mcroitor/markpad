# Task 044 page-sheet evidence

Windows 11, MSYS2 UCRT64, Qt 6.11.2, local Debug build.

| File | Contents |
|---|---|
| `paged.png` | Paged WYSIWYG opened from `fixture.md`; the page is centered on the desk, with the page content inset by the configured margins. |
| `paged-page-gap.png` | Scrolled to the page break. Shows two separate page sheets, the gray gap, page numbers, and a table/code block kept whole on the first page. |
| `dark/*.png` | The same paged-mode views with the dark theme selected. |

The screenshots were produced by the local QA automation from the built
`build-task044/markpad.exe`; they are visual evidence only and do not replace
owner sign-off. The fixture is
[`../task_011/fixture.md`](../task_011/fixture.md).

Automated checks:

- `markpad_layout_tests`: page partition, atomic table/code blocks, overflow
  warning, page-for-position.
- `markpad_smoke`: page sheet presence, non-zero page margin on page 2, source
  and token preservation, in-place editing and undo in paged mode.
- Windows Debug CTest: 6/6.
