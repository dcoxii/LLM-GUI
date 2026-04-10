#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BINARY="${PROJECT_ROOT}/build/debug/llm-gui"

if [[ ! -x "${BINARY}" ]]; then
    echo "Debug binary not found at ${BINARY}"
    echo "Run packaging/linux/configure-debug.sh and then cmake --build --preset build-debug"
    exit 1
fi

exec "${BINARY}"
