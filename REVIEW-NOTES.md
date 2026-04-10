# Review Notes

## What was checked

- Project structure and current build path
- Plugin manifest/tool protocol compatibility
- Current documentation and packaging consistency
- Container packaging consistency with the embedded llama.cpp architecture

## What was set up

- Added `plugins/sample-echo.json`
- Added `plugins/sample_echo_tool.py`
- Added `scripts/install-debian-deps.sh`

## What was verified

- `plugins/sample-echo.json` is valid JSON
- `plugins/sample_echo_tool.py` runs correctly with the app's JSON stdin/stdout protocol
- Updated shell scripts pass `bash -n`

## Current blocker in this environment

This container does not have the Qt6 development packages installed, so CMake configure stops at `find_package(Qt6 ...)`. Use the dependency install script on a Debian/Ubuntu machine, then run:

```bash
./scripts/install-debian-deps.sh
./packaging/linux/configure-debug.sh
cmake --build --preset build-debug
./packaging/linux/run-from-build.sh
```

## Notes

The normal desktop build path is current. The older container files were partially stale and have been updated to match the integrated llama.cpp layout.
