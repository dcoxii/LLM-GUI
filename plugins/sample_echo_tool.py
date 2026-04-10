#!/usr/bin/env python3
import json, sys

payload = json.load(sys.stdin)
args = payload.get("arguments", {})
text = str(args.get("text", ""))
json.dump({"success": True, "output": f"Echo: {text}"}, sys.stdout)
