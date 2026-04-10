# Architecture

## Core direction

This payload represents the native Qt evolution path for the LLM-GUI.

## Layers

- `src/app`: widgets, dialogs, and main application window
- `src/core`: persistent settings and session model/store
- `src/services`: provider transport and parsing code
- `src/util`: transcript formatting and view helpers
- `packaging`: Debian and AppArmor integration scaffolds

## Session model

Chat sessions are stored as JSON files in the configured session directory.
The main window loads, saves, lists, and deletes sessions through `SessionStore`.

## Provider model

`ProviderClient` defines the common signal and call shape.

- `DeepSeekClient` implements the hosted OpenAI request flow
- `LlamaCppClient` implements the local in-process llama.cpp request flow
- `ProviderHealthProbe` checks both providers independently

## Transcript rendering

The transcript renderer converts chat messages to rich HTML for `QTextBrowser`.
It supports:

- role cards
- inline code
- fenced code blocks
- basic bold formatting

## Integration hooks

This step adds:

- Debian packaging scaffold
- desktop entry scaffold
- AppArmor profile scaffold
- embedded llama.cpp runtime integration

## Current provider surface

- hosted ChatGPT / OpenAI via `https://api.openai.com/v1` using the Responses API
- local llama.cpp via embedded `libllama`
- provider health probing
- request timeout configuration
- improved transcript formatting for code blocks, inline code, emphasis, and bullets
