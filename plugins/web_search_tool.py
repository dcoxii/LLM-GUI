#!/usr/bin/env python3
import html
import json
import re
import sys
import urllib.parse
import urllib.request

SEARCH_URL = "https://html.duckduckgo.com/html/?q={query}"
USER_AGENT = "LLM-GUI-WebSearch/1.0 (+desktop plugin)"

def strip_tags(text: str) -> str:
    return re.sub(r"<[^>]+>", "", text or "").strip()

def fetch(query: str, max_results: int) -> dict:
    url = SEARCH_URL.format(query=urllib.parse.quote_plus(query))
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=12) as response:
        body = response.read().decode("utf-8", errors="replace")

    pattern = re.compile(
        r'<a[^>]*class="[^"]*result__a[^"]*"[^>]*href="(?P<href>[^"]+)"[^>]*>(?P<title>.*?)</a>.*?'
        r'(?:<a[^>]*class="[^"]*result__snippet[^"]*"[^>]*>|<div[^>]*class="[^"]*result__snippet[^"]*"[^>]*>)'
        r'(?P<snippet>.*?)'
        r'(?:</a>|</div>)',
        re.IGNORECASE | re.DOTALL,
    )

    results = []
    for match in pattern.finditer(body):
        href = html.unescape(match.group("href"))
        title = strip_tags(html.unescape(match.group("title")))
        snippet = strip_tags(html.unescape(match.group("snippet")))
        if not title:
            continue
        results.append({
            "title": title,
            "url": href,
            "snippet": snippet,
        })
        if len(results) >= max_results:
            break

    return {"results": results}

def main() -> int:
    try:
        payload = json.load(sys.stdin)
        arguments = payload.get("arguments", {})
        query = str(arguments.get("query", "")).strip()
        max_results = int(arguments.get("max_results", 5) or 5)
        max_results = max(1, min(max_results, 10))

        if not query:
            print(json.dumps({"success": False, "output": "Missing required argument: query"}))
            return 0

        search_data = fetch(query, max_results)
        results = search_data.get("results", [])
        if not results:
            print(json.dumps({
                "success": True,
                "output": json.dumps({"query": query, "results": []}, indent=2)
            }))
            return 0

        output = {
            "query": query,
            "results": results,
        }
        print(json.dumps({"success": True, "output": json.dumps(output, indent=2)}))
        return 0
    except Exception as exc:
        print(json.dumps({"success": False, "output": f"Web search failed: {exc}"}))
        return 0

if __name__ == "__main__":
    raise SystemExit(main())
