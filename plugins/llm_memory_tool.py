#!/usr/bin/env python3
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

STORE_PATH = Path.home() / ".llm-gui" / "memory_store.json"


def ensure_store():
    STORE_PATH.parent.mkdir(parents=True, exist_ok=True)
    if not STORE_PATH.exists():
        STORE_PATH.write_text(json.dumps({"profiles": {}}, indent=2), encoding="utf-8")


def load_store():
    ensure_store()
    try:
        data = json.loads(STORE_PATH.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            return {"profiles": {}}
        data.setdefault("profiles", {})
        return data
    except Exception:
        return {"profiles": {}}


def save_store(data):
    STORE_PATH.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")


def get_profile(data, profile):
    profiles = data.setdefault("profiles", {})
    items = profiles.setdefault(profile, [])
    if not isinstance(items, list):
        profiles[profile] = []
    return profiles[profile]


def now_iso():
    return datetime.now(timezone.utc).isoformat()


def result(success, output, **extra):
    payload = {"success": success, "output": output}
    payload.update(extra)
    json.dump(payload, sys.stdout, ensure_ascii=False)


def normalize_list(value):
    if isinstance(value, list):
        return value
    return [value]


def main():
    payload = json.load(sys.stdin)
    tool = payload.get("tool", "")
    args = payload.get("arguments", {}) or {}

    store = load_store()
    profile = str(args.get("profile") or "default").strip() or "default"
    entries = get_profile(store, profile)

    if tool == "recall_memories":
        query = str(args.get("query") or "").strip().lower()
        filtered = entries
        if query:
            filtered = [e for e in entries if query in str(e.get("content", "")).lower()]
        if not filtered:
            result(True, "No memory stored.", profile=profile, count=0, items=[])
            return
        lines = [f"{idx}. {item.get('content', '')}" for idx, item in enumerate(filtered, start=1)]
        result(True, "\n".join(lines), profile=profile, count=len(filtered), items=filtered)
        return

    if tool == "add_memory":
        raw_items = args.get("items")
        if raw_items is None:
            result(False, "Missing required argument: items")
            return
        items = [str(x).strip() for x in normalize_list(raw_items) if str(x).strip()]
        added = []
        for content in items:
            entry = {"content": content, "created_at": now_iso(), "updated_at": now_iso()}
            entries.append(entry)
            added.append(entry)
        save_store(store)
        result(True, f"Added {len(added)} memories.", profile=profile, added=added, count=len(entries))
        return

    if tool == "update_memory":
        updates = args.get("updates")
        if not isinstance(updates, list) or not updates:
            result(False, "Missing required argument: updates")
            return
        messages = []
        changed = 0
        for upd in updates:
            try:
                index = int(upd.get("index"))
            except Exception:
                messages.append("Invalid index.")
                continue
            content = str(upd.get("content", "")).strip()
            if index < 1 or index > len(entries):
                messages.append(f"Memory index {index} does not exist.")
                continue
            entries[index - 1]["content"] = content
            entries[index - 1]["updated_at"] = now_iso()
            messages.append(f"Memory at index {index} updated.")
            changed += 1
        save_store(store)
        result(True, "\n".join(messages) if messages else "No updates applied.", profile=profile, updated=changed, count=len(entries))
        return

    if tool == "delete_memory":
        raw_indices = args.get("indices")
        if raw_indices is None:
            result(False, "Missing required argument: indices")
            return
        indices = sorted({int(x) for x in normalize_list(raw_indices)}, reverse=True)
        messages = []
        deleted = 0
        for index in indices:
            if index < 1 or index > len(entries):
                messages.append(f"Memory index {index} does not exist.")
                continue
            entries.pop(index - 1)
            messages.append(f"Memory at index {index} deleted.")
            deleted += 1
        save_store(store)
        result(True, "\n".join(messages) if messages else "No deletions applied.", profile=profile, deleted=deleted, count=len(entries))
        return

    result(False, f"Unknown tool: {tool}")


if __name__ == "__main__":
    main()
