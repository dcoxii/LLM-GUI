# LLM-GUI Podman Container

Runs the Qt6 LLM-GUI desktop application inside a Podman container on
Debian Trixie. The GUI displays on the host via Wayland socket passthrough
(XWayland fallback). The app uses the integrated llama.cpp backend inside the
container and can access Vulkan-capable GPUs through DRI device passthrough.

## Prerequisites

- Podman 4.0+
- Host running Wayland (XWayland available as fallback)
- `/dev/dri/renderD*` nodes accessible (Intel, AMD, or Virtio-GPU)
- A built staging tree from `packaging/linux/build-debian.sh`

## Directory layout

```
packaging/linux/container/
    Containerfile         # Image definition
    build-container.sh    # Build the image
    run-llm-gui.sh        # Launch the container
    README.md             # This file
```

## Build

From the project root, after running `build-debian.sh` to populate the
staging tree:

```bash
./packaging/linux/container/build-container.sh
```

Pass `--tag` to override the image name, `--no-cache` to force a clean build:

```bash
./packaging/linux/container/build-container.sh --tag llm-gui:1.0.0 --no-cache
```

The build script bakes your host UID/GID into the image so mounted volumes
are writable without permission issues.

## Run

```bash
./packaging/linux/container/run-llm-gui.sh /path/to/model.gguf
```

The model directory is bind-mounted read-only at `/models/` inside the
container. Qt config and session data persist in
`~/.local/share/llm-gui-container/` on the host.

### Environment overrides

| Variable             | Default                            | Purpose                         |
|---------------------|------------------------------------|---------------------------------|
| `LLM_GUI_IMAGE`     | `llm-gui:latest`                   | Image to run                    |
| `LLM_GUI_CONFIG_DIR`| `~/.local/share/llm-gui-container` | Qt config + session persistence |
| `LLM_GUI_LOG_DIR`   | `$LLM_GUI_CONFIG_DIR/Logs`         | Launcher log files              |
| `LLM_GUI_MODEL_PATH`| `/models/<selected model>`         | Optional explicit model path    |
| `LLM_GUI_MODEL_DIR` | unset                              | Optional extra model search dir |

### Wayland / X11

The run script auto-detects display environment:

- **Wayland**: if `WAYLAND_DISPLAY` is set and the socket exists, the
  compositor socket is bind-mounted into the container.
- **X11 / XWayland**: if `DISPLAY` is set, the X11 socket and
  `XAUTHORITY` file are forwarded.
- Qt is told to prefer Wayland with XCB as fallback via
  `QT_QPA_PLATFORM=wayland;xcb`.

### Vulkan / GPU

All `/dev/dri/renderD*` and `/dev/dri/card*` nodes are passed through
automatically. Mesa Vulkan drivers are installed in the image. No NVIDIA
proprietary driver support — Vulkan-only, vendor-agnostic.

If no DRI nodes are found the script prints a warning and the integrated
llama.cpp backend will likely fall back to CPU inference.

## Persistence

| What                       | Where on host                                  |
|----------------------------|------------------------------------------------|
| Qt settings (ini)          | `$LLM_GUI_CONFIG_DIR/.config/LLM-GUI/`         |
| Session files (JSON)       | `$LLM_GUI_CONFIG_DIR/.local/share/LLM-GUI/`    |
| Launcher logs              | `$LLM_GUI_LOG_DIR/`                            |

To start fresh, delete `$LLM_GUI_CONFIG_DIR`.

## Security notes

- Container runs as a non-root user matching host UID/GID.
- `--security-opt label=disable` is required for the Wayland socket and
  DRI device mounts to work under SELinux/AppArmor without custom policies.
  Tighten this if your threat model requires it.
- `--ipc=host` is set for Qt shared memory (XCB platform plugin).
  Drop it if you hit no issues — it works without it on pure Wayland.
- The plugin sandbox (bubblewrap) runs inside the container. Nested
  namespaces require the container to have `CAP_SYS_ADMIN` or the host
  kernel to have `kernel.unprivileged_userns_clone=1`. On Debian Trixie
  the latter is enabled by default.
