# Plugin System

Plugins are discovered from the app plugin folder as JSON manifests. A plugin can expose one or more tools, each backed by a local executable that reads JSON from stdin and writes JSON to stdout.

## Trust and permissions

External plugins are never exposed automatically. A plugin must be:

1. trusted
2. enabled
3. granted every scope required by the plugin tool

Recognized scopes:

- `filesystem.read`
- `filesystem.write`
- `network`
- `process`

Scope gating is a policy layer inside the app. It helps prevent accidental exposure of tools to the model, but it is **not** a full sandbox for malicious executables.

## Manifest example

```json
{
  "name": "Sample Echo Plugin",
  "requested_scopes": ["process"],
  "tools": [
    {
      "name": "echo_text",
      "description": "Echo text back to the model for testing plugin execution.",
      "command": "python3",
      "args": ["sample_echo_tool.py"],
      "working_directory": ".",
      "timeout_ms": 300000,
      "scopes": ["process"],
      "input_schema": {
        "type": "object",
        "properties": {
          "text": {"type": "string"}
        },
        "required": ["text"]
      }
    }
  ]
}
```

## Plugin protocol

The app sends a JSON object like this to the plugin process:

```json
{
  "tool": "echo_text",
  "plugin": "Sample Echo Plugin",
  "arguments": {"text": "hello"},
  "granted_scopes": ["process"]
}
```

The plugin should return JSON like:

```json
{
  "success": true,
  "output": "hello"
}
```


## Sandboxed plugin execution on Linux

External plugins are now launched through a sandbox runner. On Linux, the app uses **bubblewrap** (`bwrap`) when it is available on `PATH`.

### Current behavior

- External plugin tools **fail closed** when bubblewrap is missing.
- The sandbox passes tool JSON through stdin and reads stdout/stderr back from the plugin.
- The plugin manager shows the active sandbox backend and whether it is available.

### Scope to sandbox mapping

- `filesystem.read` → bind the app's allowed roots read-only
- `filesystem.write` → bind the app's allowed roots read-write
- `network` absent → launch with `--unshare-net`
- `network` granted → network namespace is left available

Allowed roots for built-in and sandboxed plugin file access remain the user's home, Documents, and Downloads paths configured by the app.

### Important limitations

This is a meaningful security improvement, but it is not a complete, policy-perfect sandbox yet.

- Process creation inside the sandbox is not fully blocked.
- The plugin executable still needs access to standard system libraries and binaries to run.
- macOS and Windows do not yet have equivalent sandbox backends in this patch.

### Runtime requirement

Install bubblewrap on Linux so external plugins can run:

- Debian/Ubuntu: `sudo apt install bubblewrap`
- Fedora: `sudo dnf install bubblewrap`
- Arch: `sudo pacman -S bubblewrap`
