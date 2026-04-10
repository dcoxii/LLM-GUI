#!/usr/bin/env python3
import asyncio
import html
import json
import re
import sys
import urllib.parse
import urllib.request


USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) LLM-GUI/1.0 Safari/537.36"


def strip_tags(text: str) -> str:
    text = re.sub(r"<[^>]+>", " ", text)
    return re.sub(r"\s+", " ", html.unescape(text)).strip()


def fetch_text(url: str) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=15) as resp:
        return resp.read().decode("utf-8", errors="replace")


def search_once(query: str, max_results: int):
    encoded = urllib.parse.quote_plus(query)
    url = f"https://html.duckduckgo.com/html/?q={encoded}"
    html_text = fetch_text(url)
    blocks = re.findall(r'<a[^>]+class="result__a"[^>]+href="(.*?)"[^>]*>(.*?)</a>(.*?)(?=<a[^>]+class="result__a"|$)', html_text, flags=re.S)
    results = []
    for href, title_html, tail in blocks:
        title = strip_tags(title_html)
        snippet_match = re.search(r'<a[^>]+class="result__snippet"[^>]*>(.*?)</a>|<div[^>]+class="result__snippet"[^>]*>(.*?)</div>', tail, flags=re.S)
        snippet = ""
        if snippet_match:
            snippet = strip_tags(next(g for g in snippet_match.groups() if g))
        results.append({"title": title, "url": html.unescape(href), "snippet": snippet})
        if len(results) >= max_results:
            break
    return {"query": query, "results": results}


async def search_async(query: str, max_results: int):
    return await asyncio.to_thread(search_once, query, max_results)


def main():
    payload = json.load(sys.stdin)
    args = payload.get("arguments", {}) or {}
    queries = args.get("queries", [])
    if not isinstance(queries, list) or not queries:
        json.dump({"success": False, "output": "queries must be a non-empty array."}, sys.stdout)
        return
    max_results = int(args.get("max_results", 5))
    max_results = max(1, min(max_results, 10))

    async def runner():
        return await asyncio.gather(*(search_async(str(q), max_results) for q in queries))

    grouped = asyncio.run(runner())
    lines = []
    for group in grouped:
        lines.append(f"Query: {group['query']}")
        if not group["results"]:
            lines.append("  No results found.")
            continue
        for idx, item in enumerate(group["results"], start=1):
            lines.append(f"  {idx}. {item['title']}")
            lines.append(f"     URL: {item['url']}")
            if item["snippet"]:
                lines.append(f"     Snippet: {item['snippet']}")
    json.dump({"success": True, "output": "\n".join(lines), "groups": grouped}, sys.stdout, ensure_ascii=False)


if __name__ == "__main__":
    main()
