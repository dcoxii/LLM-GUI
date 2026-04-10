#!/usr/bin/env bash
set -Eeuo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LOG_DIR="${PROJECT_ROOT}/Logs"
mkdir -p "${LOG_DIR}"

TS="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="${LOG_DIR}/debian-build-${TS}.log"
exec > >(tee -a "$LOG_FILE") 2>&1

echo "==> Logging to: ${LOG_FILE}"
echo "==> Project root: ${PROJECT_ROOT}"

if ! command -v dpkg-buildpackage >/dev/null 2>&1; then
    echo "dpkg-buildpackage is required"
    exit 1
fi

echo "==> Refreshing top-level debian/ packaging metadata"
rm -rf "${PROJECT_ROOT}/debian"
cp -a "${PROJECT_ROOT}/packaging/debian" "${PROJECT_ROOT}/debian"

echo "==> Removing stale build outputs"
rm -rf "${PROJECT_ROOT}/build" "${PROJECT_ROOT}/stage"

cd "${PROJECT_ROOT}"

echo "==> Building Debian binary package"
dpkg-buildpackage -us -uc -b

echo "==> Debian package build completed"
