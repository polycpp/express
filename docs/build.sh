#!/usr/bin/env bash
# Build the documentation site locally. Assumes doxygen + pip -r
# requirements.txt have already run. Output: docs/build/html/
set -euo pipefail
cd "$(dirname "$0")"

mkdir -p build/doxygen/xml
echo "==> Doxygen"
doxygen Doxyfile

echo "==> Sphinx (html)"
sphinx-build -b html -W --keep-going sphinx build/html

echo
echo "Open docs/build/html/index.html in a browser."
