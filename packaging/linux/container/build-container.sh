#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
STAGING_DIR="${ROOT_DIR}/packaging/linux/staging"
CONTAINER_FILE="${SCRIPT_DIR}/Containerfile"

TAG="llm-gui:latest"
EXTRA_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --tag) TAG="$2"; shift 2 ;;
        --no-cache) EXTRA_ARGS+=(--no-cache); shift ;;
        *) EXTRA_ARGS+=("$1"); shift ;;
    esac
done

check_file() {
    if [[ ! -f "$1" ]]; then
        echo "ERROR: Expected file missing: $1" >&2
        exit 1
    fi
}

check_glob() {
    local pattern="$1"
    compgen -G "${pattern}" >/dev/null || {
        echo "ERROR: Expected files matching pattern are missing: ${pattern}" >&2
        exit 1
    }
}

check_file "${STAGING_DIR}/usr/bin/llm-gui"
check_glob "${STAGING_DIR}/usr/lib/libllama.so*"

echo "==> Staging tree verified"
echo "==> Building image: ${TAG}"
echo "    UID=$(id -u)  GID=$(id -g)"

podman build \
    -f "${CONTAINER_FILE}" \
    -t "${TAG}" \
    --build-arg "HOST_UID=$(id -u)" \
    --build-arg "HOST_GID=$(id -g)" \
    "${EXTRA_ARGS[@]}" \
    "${STAGING_DIR}"

echo ""
echo "==> Image built: ${TAG}"
echo "==> Run with: ${SCRIPT_DIR}/run-llm-gui.sh /path/to/model.gguf"
