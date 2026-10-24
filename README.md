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

The current project bootstrap is a minimal empty Qt window and a smoke test. Other editor features listed above are planned and are not implemented yet. Clean configure/build/test runs have passed locally on Windows UCRT64 and Ubuntu in WSL. The GitHub Actions workflow is configured for both platforms, and its CI/CD run has passed.

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
