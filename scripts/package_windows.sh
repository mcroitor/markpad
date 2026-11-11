#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"
output_dir="${2:-package-windows}"
version="${3:-0.1.0}"
prefix="${MINGW_PREFIX:-/ucrt64}"

if [[ ! -f "$build_dir/markpad.exe" ]]; then
  echo "missing $build_dir/markpad.exe; build the Release target first" >&2
  exit 1
fi
for required in "$build_dir/MD4C-LICENSE.md" LICENSE README.md; do
  if [[ ! -f "$required" ]]; then
    echo "missing required release file: $required" >&2
    exit 1
  fi
done

rm -rf "$output_dir"
mkdir -p "$output_dir/licenses"
cp "$build_dir/markpad.exe" "$output_dir/"
cp "$build_dir/MD4C-LICENSE.md" "$output_dir/"
cp LICENSE README.md "$output_dir/"
if [[ -f docs/release_checklist.md ]]; then
  cp docs/release_checklist.md "$output_dir/"
fi

windeployqt="${prefix}/bin/windeployqt.exe"
if [[ ! -x "$windeployqt" ]]; then
  echo "missing windeployqt: $windeployqt" >&2
  exit 1
fi
"$windeployqt" --release --no-translations --dir "$output_dir" \
  "$output_dir/markpad.exe"

# windeployqt does not deploy the MSYS2 GCC/ICU/font/runtime DLL closure.
queue=()
while IFS= read -r -d '' file; do
  queue+=("$file")
done < <(find "$output_dir" -type f \( -name '*.exe' -o -name '*.dll' \) -print0)
index=0
while (( index < ${#queue[@]} )); do
  file="${queue[index]}"
  ((index += 1))
  while IFS= read -r dependency; do
    if [[ "$dependency" != "$prefix/bin/"* ]]; then
      continue
    fi
    base="$(basename "$dependency")"
    if [[ -f "$output_dir/$base" ]]; then
      continue
    fi
    cp "$dependency" "$output_dir/$base"
    queue+=("$output_dir/$base")
  done < <(PATH="$output_dir:$prefix/bin:$PATH" ldd "$file" 2>/dev/null |
           awk '$3 ~ /^\// {print $3}')
done

if [[ -d "$prefix/share/licenses" ]]; then
  cp -R "$prefix/share/licenses/." "$output_dir/licenses/"
fi

qt_version="unknown"
if [[ -x "${prefix}/bin/qtpaths.exe" ]]; then
  qt_version="$("${prefix}/bin/qtpaths.exe" --qt-version 2>/dev/null || true)"
fi
commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
cat > "$output_dir/VERSION.txt" <<EOF
Markpad $version
Commit: $commit
Build type: Release
Target: Windows UCRT64
Qt: $qt_version
Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

(cd "$output_dir" && sha256sum markpad.exe > markpad.exe.sha256)

echo "portable package: $output_dir"
echo "CI uploads this directory as a zip artifact; zip it locally for manual distribution."
