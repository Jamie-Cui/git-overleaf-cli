#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$repo_root"

clang_format="${CLANG_FORMAT:-clang-format}"

usage() {
  cat <<'EOF'
Usage: ./clang-format-all.sh [--check]

Formats all C/C++ source and header files under include/, src/, and tests/.

Options:
  --check    Check formatting without modifying files.
EOF
}

check_mode=0
case "${1:-}" in
  "")
    ;;
  --check)
    check_mode=1
    ;;
  -h | --help)
    usage
    exit 0
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

if [[ $# -gt 1 ]]; then
  usage >&2
  exit 2
fi

if ! command -v "$clang_format" >/dev/null 2>&1; then
  echo "clang-format-all.sh: clang-format not found: $clang_format" >&2
  echo "Set CLANG_FORMAT=/path/to/clang-format to use a custom binary." >&2
  exit 127
fi

mapfile -d '' files < <(
  find include src tests -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.cc' -o -name '*.hh' -o \
       -name '*.cpp' -o -name '*.hpp' \) \
    -print0 | sort -z
)

if [[ ${#files[@]} -eq 0 ]]; then
  echo "clang-format-all.sh: no source files found"
  exit 0
fi

if [[ $check_mode -eq 1 ]]; then
  "$clang_format" --dry-run --Werror "${files[@]}"
else
  "$clang_format" -i "${files[@]}"
fi
