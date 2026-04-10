#!/usr/bin/env bash
# run-llm-gui.sh — Launch the LLM-GUI Podman container
#
# Usage:
#   ./run-llm-gui.sh /path/to/model.gguf [extra podman run args...]
#
# Examples:
#   ./run-llm-gui.sh ~/models/qwen2.5-3b-q5.gguf
#   ./run-llm-gui.sh ~/models/qwen2.5-3b-q5.gguf --rm
#
# Environment overrides (set before calling this script):
#   LLM_GUI_MODEL_PATH   Override the in-container model path
#                        (default: /models/<selected file>)
#   LLM_GUI_MODEL_DIR    Optional extra in-container model search directory
#   LLM_GUI_IMAGE        Container image to use (default: llm-gui:latest)
#   LLM_GUI_CONFIG_DIR   Host dir for Qt config + session data
#                        (default: ~/.local/share/llm-gui-container)
#   LLM_GUI_LOG_DIR      Host dir for launcher log files
#                        (default: ~/.local/share/llm-gui-container/Logs)

set -euo pipefail

IMAGE="${LLM_GUI_IMAGE:-llm-gui:latest}"
CONFIG_DIR="${LLM_GUI_CONFIG_DIR:-${HOME}/.local/share/llm-gui-container}"
LOG_DIR="${LLM_GUI_LOG_DIR:-${CONFIG_DIR}/Logs}"

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 /path/to/model.gguf [extra podman args...]"
    echo ""
    echo "  The model file is bind-mounted read-only into the container at"
    echo "  /models/\$(basename model.gguf) and LLM_GUI_MODEL_PATH is set"
    echo "  automatically."
    exit 1
fi

MODEL_HOST_PATH="$(realpath "$1")"
shift

if [[ ! -f "${MODEL_HOST_PATH}" ]]; then
    echo "ERROR: Model file not found: ${MODEL_HOST_PATH}" >&2
    exit 1
fi

MODEL_FILENAME="$(basename "${MODEL_HOST_PATH}")"
MODEL_HOST_DIR="$(dirname "${MODEL_HOST_PATH}")"
MODEL_CONTAINER_PATH="${LLM_GUI_MODEL_PATH:-/models/${MODEL_FILENAME}}"

mkdir -p "${CONFIG_DIR}" "${LOG_DIR}"

DISPLAY_ARGS=()

if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
    WAYLAND_SOCK_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
    WAYLAND_SOCK="${WAYLAND_SOCK_DIR}/${WAYLAND_DISPLAY}"
    if [[ -S "${WAYLAND_SOCK}" ]]; then
        DISPLAY_ARGS+=(
            --env "WAYLAND_DISPLAY=${WAYLAND_DISPLAY}"
            --env "XDG_RUNTIME_DIR=/run/user/$(id -u)"
            --volume "${WAYLAND_SOCK_DIR}/${WAYLAND_DISPLAY}:/run/user/$(id -u)/${WAYLAND_DISPLAY}:z"
        )
    else
        echo "WARNING: WAYLAND_DISPLAY set but socket ${WAYLAND_SOCK} not found." >&2
    fi
fi

if [[ -n "${DISPLAY:-}" ]]; then
    DISPLAY_ARGS+=(
        --env "DISPLAY=${DISPLAY}"
    )
    X11_SOCK="/tmp/.X11-unix"
    if [[ -d "${X11_SOCK}" ]]; then
        DISPLAY_ARGS+=(--volume "${X11_SOCK}:/tmp/.X11-unix:ro")
    fi
    if [[ -n "${XAUTHORITY:-}" && -f "${XAUTHORITY}" ]]; then
        DISPLAY_ARGS+=(
            --env "XAUTHORITY=/tmp/.Xauthority"
            --volume "${XAUTHORITY}:/tmp/.Xauthority:ro"
        )
    elif [[ -f "${HOME}/.Xauthority" ]]; then
        DISPLAY_ARGS+=(
            --env "XAUTHORITY=/tmp/.Xauthority"
            --volume "${HOME}/.Xauthority:/tmp/.Xauthority:ro"
        )
    fi
fi

if [[ ${#DISPLAY_ARGS[@]} -eq 0 ]]; then
    echo "ERROR: Neither WAYLAND_DISPLAY nor DISPLAY is set." >&2
    echo "       Export one of these before running." >&2
    exit 1
fi

VULKAN_ARGS=()

for dev in /dev/dri/renderD*; do
    [[ -e "${dev}" ]] && VULKAN_ARGS+=(--device "${dev}")
done

for dev in /dev/dri/card*; do
    [[ -e "${dev}" ]] && VULKAN_ARGS+=(--device "${dev}")
done

if [[ ${#VULKAN_ARGS[@]} -eq 0 ]]; then
    echo "WARNING: No /dev/dri/* devices found. Vulkan will likely fail." >&2
    echo "         The integrated llama.cpp backend will likely fall back to CPU inference." >&2
fi

IPC_ARGS=(--ipc=host)

echo "==> Starting LLM-GUI container"
echo "    Image:  ${IMAGE}"
echo "    Model:  ${MODEL_HOST_PATH}"
echo "    Config: ${CONFIG_DIR}"
echo "    Logs:   ${LOG_DIR}"
echo ""

podman run \
    --name llm-gui-container \
    --rm \
    --security-opt label=disable \
    "${DISPLAY_ARGS[@]}" \
    "${VULKAN_ARGS[@]}" \
    "${IPC_ARGS[@]}" \
    --volume "${MODEL_HOST_DIR}:/models:ro" \
    --volume "${CONFIG_DIR}:/home/llmgui:z" \
    --volume "${LOG_DIR}:/home/llmgui/.local/state/LLM-GUI/Logs:z" \
    --env "LLM_GUI_MODEL_PATH=${MODEL_CONTAINER_PATH}" \
    ${LLM_GUI_MODEL_DIR:+--env "LLM_GUI_MODEL_DIR=${LLM_GUI_MODEL_DIR}"} \
    "$@" \
    "${IMAGE}"
