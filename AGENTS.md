# AGENTS.md

## Project Overview
This project runs in a custom Linux-based DevOS toolchain environment with full local build access.

Primary workspace:
- /opt/Build/LLM-GUI

Shell:
- bash

---

## Environment Summary

### Core Toolchains
- LLVM/Clang: /opt/toolchains/LLVM-21
- CMake: /opt/toolchains/cmake/latest
- Rust: /opt/toolchains/rust (stable-x86_64-unknown-linux-gnu)
- Node.js (NVM): v24.14.1
- Java: JDK 21 (/opt/toolchains/Java/jdk-21)

### Android Build Environment
- ANDROID_HOME: /opt/toolchains/android/sdk
- ANDROID_SDK_ROOT: /opt/toolchains/android/sdk
- ANDROID_NDK: /opt/toolchains/android/sdk/ndk/29.0.14206865
- Build Tools: 36.1.0
- Platform Tools available

### Graphics / Native
- Vulkan SDK: /opt/toolchains/vulkan/1.4.341.1/x86_64

---

## Compiler Configuration

Default compilers:
- CC: clang (via ccache)
- CXX: clang++
- LD: ld.lld

LLVM toolchain is preferred for ALL builds.

Flags:
- KCFLAGS: -O2 -pipe
- KLDFLAGS: -fuse-ld=lld

---

## PATH Priority

Critical binaries are located in:
- /opt/toolchains/LLVM-21/bin
- /opt/toolchains/android/sdk/platform-tools
- /opt/toolchains/android/sdk/build-tools/36.1.0
- /opt/toolchains/cmake/latest/bin
- /opt/toolchains/rust/bin
- /opt/toolchains/nvm/versions/node/v24.14.1/bin

Always prefer these over system defaults.

---

## Build Instructions

### General Native Build
```bash
cmake -S . -B build
cmake --build build -j$(nproc)

### Android Build
./gradlew assembleDebug

### Node / JS
```bash
npm install
npm run build

## Rules for Codex

### DO
- Use existing toolchains (LLVM, Android SDK, Rust, Node)
- Prefer clang/llvm over gcc
- Use cmake for native builds unless specified otherwise
- Use project-local build scripts when available
- Respect environment variables (ANDROID_HOME, JAVA_HOME, etc.)
- Use ccache-enabled compilation when possible

### DO NOT
- Do NOT install new toolchains unless explicitly asked
- Do NOT replace build systems (cmake, gradle, cargo, etc.)
- Do NOT downgrade toolchain versions
- Do NOT use system default compilers if LLVM is available

## Execution Permissions
Allowed without asking:

Running builds (cmake, gradle, cargo, npm)
Reading/writing project files
Using compilers and SDK tools

Ask before:

Installing packages
Modifying system-level paths
Deleting large directories
Network-heavy operations

## Debugging Guidelines
When builds fail:

Check PATH resolution (which clang, which cmake, etc.)
Verify environment variables

Prefer verbose builds:

cmake --build build --verbose
Do NOT guess missing dependencies — inspect errors first

