# Markpad UI Sketch

Markpad uses three editable modes for the same Markdown document:

- **Markdown** — edit the Markdown source directly.
- **WYSIWYG (continuous layout)** — edit the formatted document without visible page boundaries.
- **WYSIWYG (page layout)** — edit the formatted document with page boundaries and pagination.

Both WYSIWYG modes are editable. Switching modes keeps the same document and unsaved changes. Formatting must be representable in the selected GFM dialect; the editor must not silently introduce formatting that cannot be saved to Markdown.

The supported dialect is GitHub Flavored Markdown (GFM). GFM extensions such as tables and strikethrough are supported. Underline is not part of GFM and is intentionally omitted.

```text
+---------------------------------------------------------------------------------------+
| File | Edit | Format | View | Help                                                    |
+---------------------------------------------------------------------------------------+
| <Tool Bar>                                                                | <Mode Switch> |
+---------------------------------------------------------------------------------------+
| <Editable Workspace: mode-dependent editor>                                          |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
|                                                                                       |
+---------------------------------------------------------------------------------------+
| <Status Bar>                                                                          |
+---------------------------------------------------------------------------------------+
```

## Elements

- Menu Bar
  - File
    - New
    - Open
    - Save
    - Save As
    - <SEPARATOR>
    - Export
      - PDF
      - HTML
    - Print
    - <SEPARATOR>
    - Exit
  - Edit
    - Undo
    - Redo
    - Cut
    - Copy
    - Paste
    - <SEPARATOR>
    - Settings
  - Format
    - Heading
      - H1
      - H2
      - H3
      - H4
      - H5
      - H6
    - Bold
    - Italic
    - Strikethrough
    - Blockquote
    - Code
    - Horizontal Line
    - Link
    - Image
    - Table
    - List
      - Ordered
      - Unordered
  - View
    - Full Screen
    - Zoom In
    - Zoom Out
    - Reset Zoom
    - <SEPARATOR>
    - Switch Mode
      - Markdown
      - WYSIWYG (Continuous layout)
      - WYSIWYG (Page layout)
  - Help
- Tool Bar
  - File.New
  - File.Open
  - File.Save
  - <SEPARATOR>
  - Edit.Undo
  - Edit.Redo
  - Edit.Settings
  - <SEPARATOR>
  - Format.Heading
  - Format.Bold
  - Format.Italic
  - Format.Strikethrough
  - Format.Blockquote
  - Format.Code
  - Format.Horizontal Line
  - Format.Link
  - Format.Image
  - Format.Table
  - Format.List
  - Format.List.Ordered
  - Format.List.Unordered
  - <SEPARATOR>
  - View.Switch Mode
- Editable Workspace
  - Markdown mode: Markdown source editor with syntax highlighting
  - WYSIWYG (Continuous layout): editable formatted document without page boundaries
  - WYSIWYG (Page layout): editable formatted document with page boundaries
  - All modes edit the same document; switching modes preserves unsaved changes
- Status Bar
  - Number of characters / Number of words / Number of lines
  - Number of pages (in paginated WYSIWYG mode)

## Settings

- General
  - Language
  - Theme
  - Auto Save
  - Default File Location
- Editor
  - Font Size
  - Font Family
  - Line Numbers
  - Word Wrap
- Page
  - Page Size
  - Orientation
  - Margins
- Shortcuts
  - Customize Shortcuts
