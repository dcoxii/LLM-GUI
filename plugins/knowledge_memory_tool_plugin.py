#!/usr/bin/env python3
import asyncio
import importlib.util
import inspect
import json
import os
import sys
import traceback

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

def _load_module(path):
    spec = importlib.util.spec_from_file_location("plugin_module", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load module from {path}")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

async def _maybe_await(value):
    if inspect.isawaitable(value):
        return await value
    return value

def _jsonable(value):
    try:
        json.dumps(value)
        return value
    except Exception:
        if isinstance(value, (str, int, float, bool)) or value is None:
            return value
        if isinstance(value, dict):
            return {str(k): _jsonable(v) for k, v in value.items()}
        if isinstance(value, (list, tuple, set)):
            return [_jsonable(v) for v in value]
        return str(value)

async def main():
    payload = json.load(sys.stdin)
    tool_name = payload.get("tool", "")
    arguments = payload.get("arguments", {}) or {}
    module_path = os.path.join(BASE_DIR, 'knowledge_memory_tool_source.py')
    try:
        mod = _load_module(module_path)
        tools_obj = getattr(mod, 'Tools')()
        fn = getattr(tools_obj, tool_name, None)
        if fn is None:
            json.dump({"success": False, "output": f"Unknown tool: {tool_name}" }, sys.stdout)
            return
        result = await _maybe_await(fn(**arguments))
        if isinstance(result, dict) and "success" in result and "output" in result:
            json.dump(result, sys.stdout)
            return
        json.dump({"success": True, "output": _jsonable(result)}, sys.stdout)
    except TypeError as e:
        json.dump({"success": False, "output": f"Argument error: {e}" }, sys.stdout)
    except Exception as e:
        json.dump({"success": False, "output": f"Execution failed: {e}\n\n{traceback.format_exc()}" }, sys.stdout)

if __name__ == "__main__":
    asyncio.run(main())
