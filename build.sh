#!/usr/bin/env bash

set -Eeuo pipefail

# ---- logging ----
LOG_DIR="/opt/build/LLM-GUI/Logs"
mkdir -p "$LOG_DIR"

TS="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="$LOG_DIR/LLM-GUI-${TS}.log"

# Log everything (stdout+stderr) to file AND console
exec > >(tee -a "$LOG_FILE") 2>&1

echo "==> Logging to: $LOG_FILE"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_ROOT="/opt/build"
GUI_DIR="$BUILD_ROOT/LLM-GUI"
LLAMA_SRC="/opt/llama.cpp"
THIRD_PARTY_DIR="$GUI_DIR/third_party"
LLAMA_DST="$THIRD_PARTY_DIR/llama.cpp"

echo "==> Resolving repository root"
if git -C "$SCRIPT_DIR" rev-parse --show-toplevel >/dev/null 2>&1; then
    REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
else
    REPO_ROOT="$SCRIPT_DIR"
fi
echo "==> Repository root: $REPO_ROOT"

# Reset source timestamps to now.
# Prevents ninja re-running cmake in an infinite loop when the build machine
# clock lags behind the timestamps embedded in the source archive.
echo "==> Normalising source timestamps"
find "$REPO_ROOT" \
    \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -o -name "*.c" \
       -o -name "*.cc" -o -name "*.qrc" -o -name "*.ui" \
       -o -name "CMakeLists.txt" -o -name "CMakePresets.json" \) \
    -print0 | xargs -0r touch
echo "==> Timestamps normalised"

echo "==> Restoring llama.cpp into third_party if available"
mkdir -p "$THIRD_PARTY_DIR"

if [[ -e "$LLAMA_SRC" ]]; then
    rm -rf "$LLAMA_DST"
    mv "$LLAMA_SRC" "$LLAMA_DST"
    echo "==> Restored: $LLAMA_DST"
elif [[ -e "$LLAMA_DST" ]]; then
    echo "==> llama.cpp already present at: $LLAMA_DST"
else
    echo "==> /opt/Build/llama.cpp not present; continuing"
fi

echo "==> Running Debian packaging build"
cd "$GUI_DIR"
VERSION="${VERSION:-1.0.0}" ./packaging/linux/build-debian.sh

echo "==> Done"
