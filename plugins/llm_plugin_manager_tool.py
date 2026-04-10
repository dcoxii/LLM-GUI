#!/usr/bin/env python3
import json
import sys
import urllib.request
from pathlib import Path
from urllib.parse import urlparse

PLUGIN_DIR = Path(__file__).resolve().parent


def result(success, output, **extra):
    payload = {"success": success, "output": output}
    payload.update(extra)
    json.dump(payload, sys.stdout, ensure_ascii=False)


def safe_name(name: str) -> str:
    cleaned = "".join(c for c in name if c.isalnum() or c in ("-", "_", "."))
    return cleaned or "plugin.json"


def manifest_files():
    return sorted(PLUGIN_DIR.glob("*.json"))


def read_manifest(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def manifest_summary(path: Path):
    try:
        data = read_manifest(path)
        return {
            "file": path.name,
            "name": data.get("name", path.stem),
            "requested_scopes": data.get("requested_scopes", []),
            "tool_names": [t.get("name", "") for t in data.get("tools", []) if isinstance(t, dict)],
            "tool_count": len(data.get("tools", [])),
        }
    except Exception as exc:
        return {"file": path.name, "name": path.stem, "error": str(exc)}


def find_manifest(name: str):
    target = (name or "").strip().lower()
    for path in manifest_files():
        if path.name.lower() == target:
            return path
        try:
            data = read_manifest(path)
            if str(data.get("name", "")).strip().lower() == target:
                return path
        except Exception:
            continue
    return None


def download(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "LLM-GUI-PluginManager/1.0"})
    with urllib.request.urlopen(req, timeout=20) as resp:
        return resp.read()


def validate_manifest_text(text: str):
    data = json.loads(text)
    if not isinstance(data, dict):
        raise ValueError("Manifest root must be an object.")
    if "name" not in data:
        raise ValueError("Manifest must include a plugin name.")
    if "tools" not in data or not isinstance(data["tools"], list):
        raise ValueError("Manifest must include a tools array.")
    return data


def companion_targets(manifest_data):
    targets = []
    for tool in manifest_data.get("tools", []):
        if not isinstance(tool, dict):
            continue
        for arg in tool.get("args", []):
            if isinstance(arg, str) and arg.endswith(".py"):
                targets.append(arg)
    return sorted(set(targets))


def main():
    payload = json.load(sys.stdin)
    tool = payload.get("tool", "")
    args = payload.get("arguments", {}) or {}

    if tool == "list_plugins":
        items = [manifest_summary(p) for p in manifest_files()]
        lines = [f"{item.get('name')} [{item.get('file')}]" for item in items]
        result(True, "\n".join(lines) if lines else "No plugins found.", plugins=items, count=len(items))
        return

    if tool == "show_plugin":
        path = find_manifest(str(args.get("name", "")))
        if not path:
            result(False, "Plugin manifest not found.")
            return
        data = read_manifest(path)
        result(True, json.dumps(data, indent=2, ensure_ascii=False), file=path.name, manifest=data)
        return

    if tool == "install_plugin_from_url":
        url = str(args.get("url", "")).strip()
        if not url.startswith(("http://", "https://")):
            result(False, "url must be an http(s) URL.")
            return
        manifest_text = download(url).decode("utf-8")
        manifest = validate_manifest_text(manifest_text)
        parsed = urlparse(url)
        manifest_filename = safe_name(Path(parsed.path).name or f"{manifest.get('name','plugin')}.json")
        manifest_path = PLUGIN_DIR / manifest_filename
        overwrite = bool(args.get("overwrite", False))
        if manifest_path.exists() and not overwrite:
            result(False, f"{manifest_filename} already exists. Set overwrite=true to replace it.")
            return
        manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")
        installed = [manifest_filename]

        for extra_url in args.get("companion_files", []) or []:
            extra_name = safe_name(Path(urlparse(extra_url).path).name)
            extra_path = PLUGIN_DIR / extra_name
            if extra_path.exists() and not overwrite:
                continue
            extra_path.write_bytes(download(extra_url))
            installed.append(extra_name)

        result(True, f"Installed plugin manifest {manifest_filename}.", installed_files=installed, plugin_name=manifest.get("name"))
        return

    if tool == "create_plugin":
        manifest_filename = safe_name(str(args.get("manifest_filename", "")))
        if not manifest_filename.endswith(".json"):
            manifest_filename += ".json"
        manifest_data = validate_manifest_text(str(args.get("manifest_json", "")))
        manifest_path = PLUGIN_DIR / manifest_filename
        manifest_path.write_text(json.dumps(manifest_data, indent=2, ensure_ascii=False), encoding="utf-8")
        written = [manifest_filename]

        script_filename = str(args.get("script_filename", "")).strip()
        script_content = args.get("script_content")
        if script_filename and script_content is not None:
            script_path = PLUGIN_DIR / safe_name(script_filename)
            script_path.write_text(str(script_content), encoding="utf-8")
            written.append(script_path.name)

        result(True, f"Created plugin {manifest_data.get('name')}.", written_files=written)
        return

    if tool == "update_plugin":
        manifest_filename = safe_name(str(args.get("manifest_filename", "")))
        if not manifest_filename:
            result(False, "manifest_filename is required.")
            return
        manifest_path = PLUGIN_DIR / manifest_filename
        if not manifest_path.exists():
            result(False, "Plugin manifest not found.")
            return

        written = []
        manifest_json = args.get("manifest_json")
        if manifest_json:
            manifest_data = validate_manifest_text(str(manifest_json))
            manifest_path.write_text(json.dumps(manifest_data, indent=2, ensure_ascii=False), encoding="utf-8")
            written.append(manifest_path.name)

        script_filename = str(args.get("script_filename", "")).strip()
        script_content = args.get("script_content")
        if script_filename and script_content is not None:
            script_path = PLUGIN_DIR / safe_name(script_filename)
            script_path.write_text(str(script_content), encoding="utf-8")
            written.append(script_path.name)

        result(True, "Plugin updated." if written else "No changes supplied.", written_files=written)
        return

    if tool == "delete_plugin":
        manifest_filename = safe_name(str(args.get("manifest_filename", "")))
        manifest_path = PLUGIN_DIR / manifest_filename
        if not manifest_path.exists():
            result(False, "Plugin manifest not found.")
            return

        removed = [manifest_path.name]
        companion = []
        if bool(args.get("delete_companion_files", False)):
            try:
                manifest = read_manifest(manifest_path)
                companion = companion_targets(manifest)
            except Exception:
                companion = []
        manifest_path.unlink()
        for name in companion:
            path = PLUGIN_DIR / safe_name(name)
            if path.exists():
                path.unlink()
                removed.append(path.name)

        result(True, f"Deleted {manifest_filename}.", removed_files=removed)
        return

    result(False, f"Unknown tool: {tool}")


if __name__ == "__main__":
    main()
