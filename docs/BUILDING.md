# Building the LLM-GUI

## Host dependencies

Recommended Debian 13 packages:

```bash
sudo apt install -y   build-essential   cmake   ninja-build   qt6-base-dev   qt6-base-dev-tools   qt6-tools-dev-tools   qtermwidget6-dev
```

## Debug build

```bash
./packaging/linux/configure-debug.sh
cmake --build --preset build-debug
./packaging/linux/run-from-build.sh
```

## Release build

```bash
./packaging/linux/build-release.sh
```

## Debian package

```bash
./packaging/debian/build-package.sh
```

Notes:

- OpenAI defaults to `https://api.openai.com/v1`.
- Set a valid OpenAI API key in the settings dialog for hosted requests.
- Provider reachability can be refreshed from the UI.
- `llama.cpp` can be used as the local provider when `third_party/llama.cpp` has been bootstrapped with `./scripts/embed_llama_cpp.sh`.

## Embedded terminal

The app now prefers **QTermWidget** for its embedded terminal when the development package is installed. If `qtermwidget6` is not found at configure time, the build falls back to the simpler PTY terminal widget automatically.


## Dependency bootstrap script

A helper script is included for Debian-based systems:

```bash
./scripts/install-debian-deps.sh
```

It installs the recommended build/runtime packages for the current Qt6 + OpenAI + embedded `llama.cpp` + bubblewrap setup.
