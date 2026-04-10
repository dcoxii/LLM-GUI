#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR_DIR="${ROOT_DIR}/third_party/llama.cpp"
BUILD_DIR="${VENDOR_DIR}/build"
STAGING_DIR="${1:-}"

PREFERRED_BACKEND="${LLAMA_BACKEND:-vulkan}"
GGML_VULKAN="${GGML_VULKAN:-AUTO}"
LLAMA_BUILD_TYPE="${LLAMA_BUILD_TYPE:-Release}"
LLAMA_CPP_REF="${LLAMA_CPP_REF:-b8739}"

echo "==> Preparing embedded llama.cpp for in-process GUI integration"
echo "==> Target revision: ${LLAMA_CPP_REF}"

vulkan_ready=0

check_vulkan() {
    if [[ "${GGML_VULKAN}" == "OFF" ]]; then
        return 1
    fi
    if ! command -v glslc >/dev/null 2>&1; then
        echo "==> Vulkan shader compiler not found. Falling back to CPU build."
        return 1
    fi
    if ldconfig -p 2>/dev/null | grep -Eq 'libvulkan\.so(\.1)?'; then
        return 0
    fi
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists vulkan; then
        return 0
    fi
    echo "==> Vulkan loader not found. Falling back to CPU build."
    return 1
}

prepare_vendor_checkout() {
    mkdir -p "$(dirname "${VENDOR_DIR}")"
    if [[ ! -d "${VENDOR_DIR}/.git" ]]; then
        echo "==> Cloning llama.cpp"
        git clone https://github.com/ggml-org/llama.cpp.git "${VENDOR_DIR}"
    fi

    echo "==> Syncing llama.cpp checkout"
    git -C "${VENDOR_DIR}" fetch --force --tags origin
    git -C "${VENDOR_DIR}" checkout --force "${LLAMA_CPP_REF}"
}

stage_runtime_libraries() {
    local lib_dest="$1"
    mkdir -p "${lib_dest}"

    mapfile -t runtime_libs < <(
        find "${BUILD_DIR}" \
            \( -type f -o -type l \) \
            \( -name 'libllama.so*' -o -name 'libggml*.so*' -o -name 'libmtmd.so*' \) \
            | sort -u
    )

    if [[ ${#runtime_libs[@]} -eq 0 ]]; then
        echo "ERROR: No llama.cpp shared libraries were produced in ${BUILD_DIR}" >&2
        exit 1
    fi

    echo "==> Staging llama.cpp runtime libraries"
    for path in "${runtime_libs[@]}"; do
        cp -a "${path}" "${lib_dest}/"
    done
}

prepare_vendor_checkout

if [[ "${PREFERRED_BACKEND}" == "vulkan" || "${GGML_VULKAN}" == "ON" || "${GGML_VULKAN}" == "AUTO" ]]; then
    if check_vulkan; then
        vulkan_ready=1
    elif [[ "${GGML_VULKAN}" == "ON" ]]; then
        echo "==> GGML_VULKAN=ON was requested but Vulkan prerequisites are missing."
        exit 1
    fi
fi

CMAKE_ARGS=(
    -S "${VENDOR_DIR}"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${LLAMA_BUILD_TYPE}"
    -DBUILD_SHARED_LIBS=ON
    -DLLAMA_BUILD_COMMON=OFF
    -DLLAMA_BUILD_TOOLS=OFF
    -DLLAMA_BUILD_EXAMPLES=OFF
    -DLLAMA_BUILD_SERVER=OFF
    -DLLAMA_BUILD_TESTS=OFF
    -DLLAMA_BUILD_WEBUI=OFF
    -DLLAMA_BUILD_HTML=OFF
    -DLLAMA_OPENSSL=OFF
    -DGGML_CPU=ON
    -DGGML_CCACHE=ON
    -DGGML_NATIVE=ON
    -DGGML_LLAMAFILE=OFF
    -DGGML_METAL=OFF
)

if (( vulkan_ready )); then
    echo "==> Configuring llama.cpp with Vulkan backend"
    CMAKE_ARGS+=(-DGGML_VULKAN=ON)
else
    echo "==> Configuring llama.cpp with CPU backend"
    CMAKE_ARGS+=(-DGGML_VULKAN=OFF)
fi

cmake "${CMAKE_ARGS[@]}"
echo "==> Building llama.cpp libraries for GUI integration"
cmake --build "${BUILD_DIR}" --config "${LLAMA_BUILD_TYPE}" -j"$(nproc)"

if [[ -n "${STAGING_DIR}" ]]; then
    echo "==> Copying llama.cpp runtime files into ${STAGING_DIR}"
    stage_runtime_libraries "${STAGING_DIR}/usr/lib"
fi

echo "==> Embedded llama.cpp is ready in ${VENDOR_DIR}"
echo "==> Reconfigure the GUI project so CMake picks up third_party/llama.cpp"
