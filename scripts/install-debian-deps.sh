#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="${ROOT_DIR}/Logs"
mkdir -p "${LOG_DIR}"

TS="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="${LOG_DIR}/install-debian-deps-${TS}.log"
exec > >(tee -a "${LOG_FILE}") 2>&1

echo "==> Logging to: ${LOG_FILE}"
echo "==> Installing Debian/Ubuntu dependencies for LLM-GUI"

if [[ "${EUID}" -ne 0 ]]; then
    SUDO="sudo"
else
    SUDO=""
fi

DEBIAN_FRONTEND=noninteractive ${SUDO} apt-get update

DEBIAN_FRONTEND=noninteractive ${SUDO} apt-get install -y \
    build-essential \
    pkg-config \
    git \
    ccache \
    qt6-base-dev \
    qt6-base-dev-tools \
    qt6-tools-dev-tools \
    libqtermwidget6-2 \
    libqtermwidget-dev \
    qtermwidget-data \
    bubblewrap \
    curl

echo "==> Dependency installation complete"
echo "==> Next steps:"
echo "==>   ./packaging/linux/configure-debug.sh"
echo "==>   cmake --build --preset build-debug"
echo "==>   ./packaging/linux/run-from-build.sh"
