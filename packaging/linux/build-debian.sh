#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STAGING_DIR="${ROOT_DIR}/packaging/linux/staging"
BUILD_DIR="${ROOT_DIR}/build-release"
VERSION="${VERSION:-1.0.0}"
BUILD_CONTAINER=0
CONTAINER_TAG="${CONTAINER_TAG:-llm-gui:latest}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --container) BUILD_CONTAINER=1; shift ;;
        --container-tag) CONTAINER_TAG="$2"; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

echo "==> Building LLM-GUI Debian package with embedded llama.cpp"

rm -rf "${STAGING_DIR}"
mkdir -p "${STAGING_DIR}"

echo "==> Step 1: Building llama.cpp (prefer Vulkan, fallback to CPU)"
"${ROOT_DIR}/scripts/embed_llama_cpp.sh" "${STAGING_DIR}"

echo "==> Step 2: Building LLM-GUI"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_BINDIR=bin \
    -DLLM_GUI_ENABLE_EMBEDDED_LLAMA_CPP=ON \
    -DLLM_GUI_LLAMA_CPP_SOURCE_DIR="${ROOT_DIR}/third_party/llama.cpp" \
    -DLLM_GUI_BUNDLE_QT=OFF

cmake --build "${BUILD_DIR}" --config Release -j"$(nproc)"

echo "==> Step 3: Installing LLM-GUI"
cmake --install "${BUILD_DIR}" --prefix "${STAGING_DIR}/usr"

mkdir -p     "${STAGING_DIR}/usr/share/applications"     "${STAGING_DIR}/usr/share/icons/hicolor/scalable/apps"     "${STAGING_DIR}/usr/bin"

# Files are already installed into their final paths by cmake --install.
# Avoid moving a file onto itself, which fails under `mv`.
if [[ -f "${STAGING_DIR}/usr/share/applications/llm-gui.desktop" ]]; then
    chmod 644 "${STAGING_DIR}/usr/share/applications/llm-gui.desktop" || true
fi

if [[ -f "${STAGING_DIR}/usr/share/icons/hicolor/scalable/apps/llm-gui.svg" ]]; then
    chmod 644 "${STAGING_DIR}/usr/share/icons/hicolor/scalable/apps/llm-gui.svg" || true
fi

echo "==> Step 4: Creating package metadata"
mkdir -p "${STAGING_DIR}/DEBIAN"
SIZE=$(du -sk "${STAGING_DIR}" | cut -f1)

if dpkg -s mesa-vulkan-drivers &>/dev/null 2>&1; then
    VULKAN_DRIVER="mesa-vulkan-drivers"
elif dpkg -s nvidia-vulkan-common &>/dev/null 2>&1; then
    VULKAN_DRIVER="nvidia-vulkan-common"
else
    VULKAN_DRIVER="vulkan-tools | mesa-vulkan-drivers | nvidia-vulkan-common"
fi

cat > "${STAGING_DIR}/DEBIAN/control" << EOF
Package: llm-gui
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: amd64
Installed-Size: ${SIZE}
Maintainer: LLM-GUI Team dcoxii1979@gmail.com
Depends: libc6 (>= 2.34),
         libstdc++6 (>= 11),
         libqt6core6 | libqt6core6t64,
         libqt6gui6 | libqt6gui6t64,
         libqt6widgets6 | libqt6widgets6t64,
         libqt6network6 | libqt6network6t64,
         bubblewrap (>= 0.5.0),
         curl
Recommends: libvulkan1 (>= 1.2), vulkan-tools, glslc, ${VULKAN_DRIVER}
Description: Native Qt6 desktop client for LLMs with embedded llama.cpp
 LLM-GUI is a desktop client for local and cloud LLMs with plugin system,
 tool calling, and sandboxed execution.
 .
 This package includes a bundled llama.cpp build. When Vulkan build tools
 are available at package build time it uses the Vulkan backend; otherwise
 it falls back to a CPU build.
 .
 Features:
  * Embedded llama.cpp backend
  * Plugin system with bubblewrap sandboxing
  * Providers: local llama.cpp and ChatGPT/OpenAI
  * Session persistence and file attachments
EOF

cat > "${STAGING_DIR}/DEBIAN/postinst" << 'EOF'
#!/bin/sh
set -e

case "$1" in
    configure)
        ldconfig 2>/dev/null || true
        if command -v vulkaninfo >/dev/null 2>&1; then
            if vulkaninfo --summary 2>/dev/null | grep -q "GPU"; then
                echo "Vulkan GPU detected - LLM-GUI will use hardware acceleration"
            else
                echo "Vulkan installed but no GPU found - will use CPU fallback"
            fi
        else
            echo "Install vulkan-tools for GPU detection info"
        fi
        ;;
esac

update-desktop-database -q || true
gtk-update-icon-cache -q /usr/share/icons/hicolor || true

exit 0
EOF
chmod 755 "${STAGING_DIR}/DEBIAN/postinst"

cat > "${STAGING_DIR}/DEBIAN/prerm" << 'EOF'
#!/bin/sh
set -e
exit 0
EOF
chmod 755 "${STAGING_DIR}/DEBIAN/prerm"

echo "==> Step 5: Building .deb package"
dpkg-deb --build "${STAGING_DIR}" "${ROOT_DIR}/llm-gui-${VERSION}.deb"

echo "==> Package: ${ROOT_DIR}/llm-gui-${VERSION}.deb"
echo "==> Size: $(du -h "${ROOT_DIR}/llm-gui-${VERSION}.deb" | cut -f1)"

if [[ "${BUILD_CONTAINER}" -eq 1 ]]; then
    echo "==> Step 6: Building Podman image"
    "${ROOT_DIR}/packaging/linux/container/build-container.sh" --tag "${CONTAINER_TAG}"
fi
