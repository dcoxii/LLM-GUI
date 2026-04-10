# LLM-GUI (Qt / C++)

Native Qt6 desktop client for ChatGPT / OpenAI and local llama.cpp models. Features plugin system with sandboxed execution (bubblewrap), tool calling, session persistence, file attachments, embedded terminal, and Debian packaging.

## Features

- native Qt Widgets GUI
- JSON session persistence
- OpenAI streaming chat
- provider health probing
- QSettings-backed configuration
- Debian-oriented packaging scaffold
- embedded terminal dock (QTermWidget when available, PTY fallback otherwise)
- AppArmor profile scaffold
- embedded llama.cpp library integration

## Quick start

```bash
./packaging/linux/configure-debug.sh
cmake --build --preset build-debug
./packaging/linux/run-from-build.sh
```

See `docs/BUILDING.md` for package dependencies and Debian packaging notes.


## Plugin system

Plugins are discovered from JSON manifests and executed as local tools through the sandbox runner. This tree now includes a working sample plugin:

- manifest: `plugins/sample-echo.json`
- tool: `plugins/sample_echo_tool.py`

To enable it inside the app:

1. Open **Plugin Manager**
2. Trust **Sample Echo Plugin**
3. Enable it
4. Grant its requested `process` scope

On Linux, external plugins require **bubblewrap** (`bwrap`) on `PATH` or they will remain blocked.


## ChatGPT / OpenAI provider

The cloud provider is now **ChatGPT / OpenAI** using `https://api.openai.com/v1`. Configure the Base URL, model, and API key in Settings, then select **ChatGPT / OpenAI** as the default provider.

The app uses OpenAI's Responses API for text generation and the model listing endpoint for model discovery.


## Embedded llama.cpp provider

This project now treats **llama.cpp as an in-process inference backend**.

The GUI links against `libllama` from `third_party/llama.cpp`, loads the GGUF
directly, and runs generation inside the Qt application. There is no local
HTTP server and no `llama-server` process in the provider path anymore.

Bootstrap the embedded checkout:

```bash
./scripts/embed_llama_cpp.sh
```

Then open **Settings** and configure:

- **Default Provider**: `llama.cpp (integrated)`
- **llama.cpp GGUF Model Path**: path to your `.gguf`
- **llama.cpp Model Label**: friendly label shown in the UI/session metadata
- **llama.cpp Extra Args**: optional `--n-predict <N>` and `--threads <N>`

## License


This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.
