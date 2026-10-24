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

The current project bootstrap is a minimal empty Qt window and a smoke test. Other editor features listed above are planned and are not implemented yet. Clean configure/build/test runs have passed locally on Windows UCRT64 and Ubuntu in WSL. The GitHub Actions workflow is configured for both platforms; its remote run is pending.

## License

Markpad is licensed under the MIT License.

For more details, see the [LICENSE](LICENSE) file.

## Design documents

- [UI sketch](sketch.md)
- [Architecture](ARCHITECTURE.md)
- [Development task backlog](docs/tasks/README.md)
