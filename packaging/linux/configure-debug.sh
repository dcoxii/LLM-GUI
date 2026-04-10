#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cmake --preset dev-debug -S "${PROJECT_ROOT}"
echo "Configured debug build at ${PROJECT_ROOT}/build/debug"
