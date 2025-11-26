#!/bin/bash

set -euo pipefail

# Determine repository root (fallback to script dir/.. if git not available)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if command -v git >/dev/null 2>&1 && git -C "$SCRIPT_DIR" rev-parse --show-toplevel >/dev/null 2>&1; then
  REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
else
  REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
fi

# Variable that will hold the name of the clang-format command
FMT=""

# Prefer unversioned clang-format, else try common versioned names
for clangfmt in clang-format \
                  clang-format-19 clang-format-18 clang-format-17 clang-format-16 \
                  clang-format-15 clang-format-14 clang-format-13 clang-format-12 \
                  clang-format-11 clang-format-10 clang-format-9 clang-format-8 \
                  clang-format-7 clang-format-6.0 clang-format-5.0 clang-format-4.0; do
  if command -v "$clangfmt" >/dev/null 2>&1; then
    FMT="$clangfmt"
    break
  fi
done

if [ -z "$FMT" ]; then
  echo "Error: failed to find clang-format in PATH" >&2
  exit 1
fi

echo "Using $FMT"

# Find all C++ headers/sources: .cpp, .hxx, .h (exclude vendor/build and VCS dirs)
echo "Scanning for .cpp, .hxx, .h files under $REPO_ROOT ..."
find "$REPO_ROOT" \
  -type d \( -name .git -o -name .vscode -o -name .augment -o -name ThirdParty -o -name venv -o -name build -o -name 'build_*' -o -name dist \) -prune -false -o \
  -type f \( -name "*.cpp" -o -name "*.hxx" -o -name "*.h" \) -print0 \
  | xargs -0 -I{} "$FMT" -i {}

echo "clang-format complete."
