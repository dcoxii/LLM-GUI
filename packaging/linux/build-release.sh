#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cmake --preset dev-release -S "${PROJECT_ROOT}"
cmake --build --preset build-release
echo "Release build completed at ${PROJECT_ROOT}/build/release"
