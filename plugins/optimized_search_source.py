"""
title: Optimized Search
author: @cooooookiecrisp
description: Combines SearXNG (simple) and Vane (deep) searches with automatic mode selection based on query complexity. Injects current date/time for temporal queries.
version: 1.5
license: MIT
"""

import json
import logging
import re
from datetime import datetime
from typing import Optional, List, Dict, Any, Tuple

import requests
from bs4 import BeautifulSoup
from pydantic import BaseModel, Field

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class Tools:
    """
    Optimized Search Tool that combines SearXNG and Vane search engines.
    Automatically selects the appropriate search mode based on query complexity.
    Injects current date/time for temporal queries.
    """

    # ========================================================================
    # ADMIN VALVES - System Configuration (Tools Menu)
    # ========================================================================

    class Valves(BaseModel):
        """
        System configuration for the tool.
        Configure in Admin Panel -> Tools -> Gear Icon.
        """

        # --------------------------------------------------------------------
        # Backend URLs (Admin Only)
        # --------------------------------------------------------------------
        SEARXNG_URL: str = Field(
            default="http://searxng:8080", description="URL to your SearXNG instance"
        )
        VANE_URL: str = Field(
            default="http://localhost:3000", description="URL to your Vane instance"
        )

        # --------------------------------------------------------------------
        # Vane LLM Configuration (Admin Only)
        # --------------------------------------------------------------------
        VANE_CHAT_MODEL_PROVIDER_ID: str = Field(
            default="",
            description="Vane chat model provider UUID (get from /api/providers)",
        )
        VANE_CHAT_MODEL_KEY: str = Field(
            default="gpt-4o-mini",
            description="Chat model key (e.g., gpt-4o-mini, gpt-4o)",
        )
        VANE_EMBEDDING_MODEL_PROVIDER_ID: str = Field(
            default="", description="Vane embedding model provider UUID"
        )
        VANE_EMBEDDING_MODEL_KEY: str = Field(
            default="text-embedding-3-small", description="Embedding model key"
        )

        # --------------------------------------------------------------------
        # Timeout Settings (Admin Only)
        # --------------------------------------------------------------------
        VANE_TIMEOUT: int = Field(
            default=30,
            description="Timeout in seconds for Vane deep searches",
            ge=10,
            le=120,
        )

        # --------------------------------------------------------------------
        # Date/Time Injection (Admin Only)
        # --------------------------------------------------------------------
        INJECT_DATETIME: bool = Field(
            default=True,
            description="Inject current date/time into queries for temporal awareness",
        )
        DATETIME_FORMAT: str = Field(
            default="%Y-%m-%d %A %B %d",
            description="Date/time format (Python strftime). Default: 'YYYY-MM-DD DayName MonthName Day'",
        )
        TIMEZONE: str = Field(
            default="America/Los_Angeles",
            description="Timezone for date/time injection (e.g., America/Los_Angeles, UTC)",
        )

        # --------------------------------------------------------------------
        # Search Behavior (Available in both Admin & Chat Interface)
        # --------------------------------------------------------------------
        DEFAULT_MODE: str = Field(
            default="auto", description="Default search mode: auto, simple, or deep"
        )
        DEFAULT_SOURCES: str = Field(
            default="web",
            description="Default sources (comma-separated): web, academia, social",
        )
        ENABLE_FULL_FETCH: bool = Field(
            default=True,
            description="Enable full page content extraction in simple mode",
        )
        MAX_FULL_FETCH_RESULTS: int = Field(
            default=3,
            description="Number of top results to fully fetch (1-10)",
            ge=1,
            le=10,
        )
        SHOW_SELECTION_REASONING: bool = Field(
            default=True, description="Show mode selection reasoning in output"
        )
        DEFAULT_MAX_RESULTS: int = Field(
            default=5,
            description="Maximum number of search results to return (1-20)",
            ge=1,
            le=20,
        )

    # ========================================================================
    # USER VALVES - Chat Interface Settings (Overrides Admin Valves)
    # PERSISTS ACROSS CHATS
    # ========================================================================

    class UserValves(BaseModel):
        """
        Per-chat search behavior settings.
        These override the admin settings for your chat session.

        Available in the chat interface when using the tool.
        NOTE: These settings persist across all your chats.
        """

        # --------------------------------------------------------------------
        # Search Mode
        # --------------------------------------------------------------------
        mode: str = Field(
            default="auto",
            description="Search mode: 'auto' (smart selection), 'simple' (SearXNG only), or 'deep' (Vane synthesis)",
        )

        # --------------------------------------------------------------------
        # Search Sources
        # --------------------------------------------------------------------
        sources: str = Field(
            default="web",
            description="Sources to search (comma-separated): 'web', 'academia', 'social'. Example: 'web,academia'",
        )

        # --------------------------------------------------------------------
        # Full Fetch Settings
        # --------------------------------------------------------------------
        full_fetch: bool = Field(
            default=True,
            description="Enable full page content extraction (simple mode only)",
        )

        full_fetch_results: int = Field(
            default=3,
            description="Number of top results to fully fetch (1-10)",
            ge=1,
            le=10,
        )

        # --------------------------------------------------------------------
        # Output Settings
        # --------------------------------------------------------------------
        show_reasoning: bool = Field(
            default=True, description="Display mode selection reasoning in output"
        )

        max_results: int = Field(
            default=5,
            description="Maximum number of search results to return (1-20)",
            ge=1,
            le=20,
        )

    # ========================================================================
    # INITIALIZATION
    # ========================================================================

    def __init__(self):
        self.valves = self.Valves()

    def _get_config(self, key: str) -> Any:
        """
        Get configuration value.
        Priority: UserValves (chat settings) > Valves (admin settings)
        """
        # Map UserValves keys to Valves keys
        user_to_valve_map = {
            "mode": "DEFAULT_MODE",
            "sources": "DEFAULT_SOURCES",
            "full_fetch": "ENABLE_FULL_FETCH",
            "full_fetch_results": "MAX_FULL_FETCH_RESULTS",
            "show_reasoning": "SHOW_SELECTION_REASONING",
            "max_results": "DEFAULT_MAX_RESULTS",
        }

        # Get the corresponding Valves key (use key as-is if not in map)
        valve_key = user_to_valve_map.get(key, key)
        user_key = key  # UserValves uses the short names

        # Priority: UserValves > Valves
        if hasattr(self, "user_valves") and self.user_valves:
            user_value = getattr(self.user_valves, user_key, None)
            if user_value is not None:
                return user_value

        # Use the mapped valve_key, not the original key
        return getattr(self.valves, valve_key)

    def _parse_sources(self, sources_str: str) -> List[str]:
        """Parse comma-separated sources string into list."""
        if isinstance(sources_str, list):
            return sources_str
        return [s.strip() for s in sources_str.split(",") if s.strip()]

    # ========================================================================
    # SNIPPET ANALYSIS
    # ========================================================================

    def _analyze_snippet_consistency(
        self, snippets: List[str], query: str
    ) -> Dict[str, Any]:
        """
        Analyze snippets for contradictions or inconclusiveness.

        Returns:
            Dict with:
            - needs_full_fetch: bool
            - reason: str
            - confidence: float (0-1)
        """
        if not snippets:
            return {
                "needs_full_fetch": True,
                "reason": "No snippets available",
                "confidence": 0.0,
            }

        # Filter out empty snippets
        valid_snippets = [s for s in snippets if s and len(s.strip()) > 20]

        if not valid_snippets:
            return {
                "needs_full_fetch": True,
                "reason": "All snippets are too short",
                "confidence": 0.0,
            }

        query_lower = query.lower()

        # Check for temporal queries - snippets are often outdated
        temporal_patterns = [
            r"\b(today|tomorrow|yesterday)\b",
            r"\b(current|latest|now)\b",
            r"\bweather\b",
            r"\bnews\b",
        ]
        is_temporal = any(re.search(p, query_lower) for p in temporal_patterns)

        if is_temporal:
            # Temporal queries often have outdated snippets
            return {
                "needs_full_fetch": True,
                "reason": "Temporal query - snippets may be outdated",
                "confidence": 0.7,
            }

        # Check snippet lengths
        avg_length = sum(len(s) for s in valid_snippets) / len(valid_snippets)

        if avg_length < 100:
            return {
                "needs_full_fetch": True,
                "reason": f"Snippets too short (avg {int(avg_length)} chars)",
                "confidence": 0.6,
            }

        # Check for contradiction indicators
        contradiction_patterns = [
            (r"\byes\b.*\bno\b", "yes/no contradiction"),
            (r"\btrue\b.*\bfalse\b", "true/false contradiction"),
            (r"\b(?:is|are)\s+not\b.*\b(?:is|are)\b", "negation contradiction"),
        ]

        combined_text = " ".join(valid_snippets).lower()

        for pattern, name in contradiction_patterns:
            if re.search(pattern, combined_text):
                return {
                    "needs_full_fetch": True,
                    "reason": f"Detected {name} in snippets",
                    "confidence": 0.8,
                }

        # Check for numeric contradictions (different numbers for same metric)
        numbers_in_snippets = []
        for snippet in valid_snippets:
            numbers = re.findall(
                r"\b\d+(?:\.\d+)?(?:%|\s*(?:percent|million|billion|thousand))?\b",
                snippet.lower(),
            )
            numbers_in_snippets.append(set(numbers))

        # If multiple snippets have different numbers, might be contradictory
        if len(numbers_in_snippets) >= 2:
            unique_numbers = set()
            for num_set in numbers_in_snippets:
                unique_numbers.update(num_set)

            # Check for conflicting percentages or measurements
            percentages = [n for n in unique_numbers if "%" in n or "percent" in n]
            if len(percentages) > 2:
                return {
                    "needs_full_fetch": True,
                    "reason": "Conflicting numeric data in snippets",
                    "confidence": 0.7,
                }

        # Check for uncertainty indicators
        uncertainty_patterns = [
            r"\bmay\b",
            r"\bmight\b",
            r"\bcould\b",
            r"\bpossibly\b",
            r"\ballegedly\b",
            r"\breportedly\b",
            r"\bunconfirmed\b",
            r"\bit\s+is\s+(?:not\s+)?clear\b",
            r"\bunclear\b",
        ]

        uncertainty_count = sum(
            1 for p in uncertainty_patterns if re.search(p, combined_text)
        )

        if uncertainty_count >= 2:
            return {
                "needs_full_fetch": True,
                "reason": "High uncertainty in snippet language",
                "confidence": 0.65,
            }

        # Check for missing key information
        # Extract key terms from query
        query_terms = set(re.findall(r"\b[a-zA-Z]{4,}\b", query_lower))
        query_terms.discard("what")
        query_terms.discard("when")
        query_terms.discard("where")
        query_terms.discard("which")
        query_terms.discard("how")
        query_terms.discard("does")

        if query_terms:
            terms_in_snippets = sum(1 for term in query_terms if term in combined_text)
            coverage = terms_in_snippets / len(query_terms) if query_terms else 1.0

            if coverage < 0.5:
                return {
                    "needs_full_fetch": True,
                    "reason": f"Query terms poorly covered in snippets ({int(coverage*100)}%)",
                    "confidence": 0.6,
                }

        # Snippets appear consistent and complete
        return {
            "needs_full_fetch": False,
            "reason": "Snippets appear consistent and complete",
            "confidence": 0.8,
        }

    # ========================================================================
    # DATE/TIME INJECTION
    # ========================================================================

    def _get_current_datetime(self) -> Dict[str, str]:
        """
        Get current date and time in configured timezone.

        Returns:
            Dict with various date/time formats for query enrichment.
        """
        try:
            from zoneinfo import ZoneInfo

            timezone_str = self._get_config("TIMEZONE")
            tz = ZoneInfo(timezone_str)
            now = datetime.now(tz)
        except Exception:
            now = datetime.utcnow()

        date_format = self._get_config("DATETIME_FORMAT")

        return {
            "iso": now.isoformat(),
            "date": now.strftime("%Y-%m-%d"),
            "time": now.strftime("%H:%M:%S"),
            "formatted": now.strftime(date_format),
            "day_name": now.strftime("%A"),
            "month_name": now.strftime("%B"),
            "year": now.strftime("%Y"),
            "timezone": self._get_config("TIMEZONE"),
            "full_context": now.strftime(
                f"Today is {date_format}, and the current time is %I:%M %p %Z"
            ),
        }

    def _is_temporal_query(self, query: str) -> bool:
        """
        Detect if query requires temporal context.
        """
        temporal_patterns = [
            r"\btoday\b",
            r"\btomorrow\b",
            r"\byesterday\b",
            r"\bthis\s+(week|month|year)\b",
            r"\bnext\s+(week|month|year)\b",
            r"\blast\s+(week|month|year)\b",
            r"\bnow\b",
            r"\bcurrent\b",
            r"\blatest\b",
            r"\brecent\b",
            r"\bupcoming\b",
            r"\bthis\s+weekend\b",
            r"\bweather\b",
            r"\bnews\b",
            r"\bscore\b",
            r"\bgame\s+(today|tonight|this\s+week)\b",
            r"\bwhat\s+day\b",
            r"\bwhat\s+date\b",
            r"\bwhat\s+time\b",
        ]

        query_lower = query.lower()

        for pattern in temporal_patterns:
            if re.search(pattern, query_lower):
                return True

        return False

    def _inject_datetime(self, query: str) -> Tuple[str, Dict[str, str]]:
        """
        Inject current date/time into query if it's temporal.

        Uses multiple injection strategies to ensure search engines
        and LLMs recognize the correct temporal context.
        """
        datetime_info = self._get_current_datetime()

        if not self._get_config("INJECT_DATETIME"):
            return query, datetime_info

        if not self._is_temporal_query(query):
            return query, datetime_info

        # Build explicit date context in multiple formats
        today_date = datetime_info["date"]  # 2026-03-17
        day_name = datetime_info["day_name"]  # Tuesday
        month_name = datetime_info["month_name"]  # March
        year = datetime_info["year"]  # 2026
        current_time = datetime_info["time"]  # 17:05:00

        # Create enriched query with embedded date
        # Format: explicit date + query + date suffix for reinforcement
        enriched_query = f"{today_date} {day_name} {month_name} {year}: {query}"

        # For tomorrow/yesterday queries, calculate the target date
        query_lower = query.lower()

        if "tomorrow" in query_lower:
            # Add tomorrow's date explicitly
            from datetime import timedelta

            try:
                from zoneinfo import ZoneInfo

                tz = ZoneInfo(self._get_config("TIMEZONE"))
                now = datetime.now(tz)
            except:
                now = datetime.utcnow()
            tomorrow = now + timedelta(days=1)
            tomorrow_date = tomorrow.strftime("%Y-%m-%d")
            tomorrow_day = tomorrow.strftime("%A")
            enriched_query = f"{today_date} (today) → {tomorrow_date} {tomorrow_day} (tomorrow): {query}"

        elif "yesterday" in query_lower:
            from datetime import timedelta

            try:
                from zoneinfo import ZoneInfo

                tz = ZoneInfo(self._get_config("TIMEZONE"))
                now = datetime.now(tz)
            except:
                now = datetime.utcnow()
            yesterday = now - timedelta(days=1)
            yesterday_date = yesterday.strftime("%Y-%m-%d")
            yesterday_day = yesterday.strftime("%A")
            enriched_query = f"{today_date} (today) → {yesterday_date} {yesterday_day} (yesterday): {query}"

        return enriched_query, datetime_info

    # ========================================================================
    # QUERY ANALYSIS
    # ========================================================================

    def _analyze_query_complexity(self, query: str) -> Tuple[bool, Dict[str, Any]]:
        """Analyze query complexity and return detailed reasoning."""
        reasoning = {
            "query": query,
            "token_count": 0,
            "triggers": [],
            "matched_patterns": [],
            "decision": "simple",
            "is_temporal": False,
        }

        tokens = query.split()
        token_count = len(tokens)
        reasoning["token_count"] = token_count
        reasoning["is_temporal"] = self._is_temporal_query(query)

        if token_count > 15:
            reasoning["triggers"].append(
                {
                    "type": "token_count",
                    "value": token_count,
                    "threshold": 15,
                    "reason": f"Query has {token_count} tokens (threshold: 15)",
                }
            )

        query_lower = query.lower()

        comparative_patterns = {
            r"\bvs\b": "vs (comparison)",
            r"\bcompare\b": "compare",
            r"\bdifference\b": "difference",
            r"\bpros\b": "pros",
            r"\bcons\b": "cons",
            r"\bbetter\b": "better",
            r"\bworse\b": "worse",
            r"\bversus\b": "versus",
            r"\balternative\b": "alternative",
        }

        for pattern, name in comparative_patterns.items():
            if re.search(pattern, query_lower):
                reasoning["triggers"].append(
                    {
                        "type": "comparative_keyword",
                        "pattern": name,
                        "reason": f"Contains comparative keyword: '{name}'",
                    }
                )
                reasoning["matched_patterns"].append(name)

        interrogative_patterns = {
            r"\bhow\s+(does|do|can|should|would|could)\b": "complex 'how'",
            r"\bwhy\s+(does|do|is|are|should|would|could)\b": "complex 'why'",
            r"\bwhat\s+are\s+the\s+(best|top|differences|pros|cons)\b": "complex 'what are the...'",
            r"\bexplain\s+(how|why|what)\b": "explain + interrogative",
        }

        for pattern, name in interrogative_patterns.items():
            if re.search(pattern, query_lower):
                reasoning["triggers"].append(
                    {
                        "type": "complex_interrogative",
                        "pattern": name,
                        "reason": f"Contains complex interrogative structure: '{name}'",
                    }
                )
                reasoning["matched_patterns"].append(name)

        reasoning_patterns = {
            r"\bbest\b.*\bfor\b": "best for",
            r"\btop\s+\d+\b": "top N",
            r"\brecommend\w*\b": "recommend",
            r"\bshould\s+i\b": "should I",
            r"\bwhich\s+(is|are)\s+(better|best)\b": "which is better/best",
            r"\bguide\b": "guide",
            r"\btutorial\b": "tutorial",
            r"\bhow\s+to\b": "how to",
        }

        for pattern, name in reasoning_patterns.items():
            if re.search(pattern, query_lower):
                reasoning["triggers"].append(
                    {
                        "type": "reasoning_indicator",
                        "pattern": name,
                        "reason": f"Contains reasoning/recommendation indicator: '{name}'",
                    }
                )
                reasoning["matched_patterns"].append(name)

        is_complex = len(reasoning["triggers"]) > 0
        reasoning["decision"] = "deep" if is_complex else "simple"
        reasoning["trigger_count"] = len(reasoning["triggers"])

        return is_complex, reasoning

    def _format_reasoning_message(
        self,
        reasoning: Dict[str, Any],
        selected_mode: str,
        datetime_info: Dict[str, str] = None,
    ) -> str:
        """Format the reasoning into a human-readable message."""
        lines = []
        lines.append(f"🔍 **Query Analysis**")
        lines.append(f"")
        lines.append(f"| Metric | Value |")
        lines.append(f"|--------|-------|")
        lines.append(
            f"| Query | \"{reasoning['query'][:50]}{'...' if len(reasoning['query']) > 50 else ''}\" |"
        )
        lines.append(f"| Token Count | {reasoning['token_count']} (threshold: 15) |")
        lines.append(f"| Triggers Found | {reasoning['trigger_count']} |")
        lines.append(f"| Selected Mode | **{selected_mode.upper()}** |")

        if reasoning.get("is_temporal"):
            lines.append(f"| Temporal Query | ✅ Yes |")

        # Add explicit date context for LLM to see
        if datetime_info:
            lines.append(
                f"| **Current Date** | **{datetime_info['date']}** ({datetime_info['day_name']}) |"
            )
            lines.append(
                f"| **Current Time** | {datetime_info['time']} {datetime_info['timezone']} |"
            )
            lines.append(f"")
            lines.append(
                f"📅 **IMPORTANT: Today is {datetime_info['date']} ({datetime_info['day_name']}, {datetime_info['month_name']} {datetime_info['year']}). All search results are current as of this date.**"
            )

        if reasoning["triggers"]:
            lines.append(f"")
            lines.append(f"**Triggers Detected:**")
            for trigger in reasoning["triggers"]:
                lines.append(f"- [{trigger['type']}] {trigger['reason']}")
        else:
            lines.append(f"")
            lines.append(f"**No complexity triggers detected** → Simple search")

        return "\n".join(lines)

    def _map_sources_to_vane(self, sources: List[str]) -> List[str]:
        """Map user-facing source names to Vane API source names."""
        source_mapping = {
            "web": "web",
            "academia": "academic",
            "academic": "academic",
            "social": "discussions",
            "discussions": "discussions",
        }

        vane_sources = []
        for source in sources:
            mapped = source_mapping.get(source.lower())
            if mapped and mapped not in vane_sources:
                vane_sources.append(mapped)

        return vane_sources if vane_sources else ["web"]

    def _map_depth_to_optimization(self, depth: str) -> str:
        """Map user-facing depth names to Vane optimizationMode values."""
        depth_mapping = {
            "quick": "speed",
            "speed": "speed",
            "balanced": "balanced",
            "quality": "quality",
        }
        return depth_mapping.get(depth.lower(), "balanced")

    # ========================================================================
    # SEARCH IMPLEMENTATIONS
    # ========================================================================

    async def _simple_search(
        self,
        query: str,
        sources: List[str],
        reasoning: Dict[str, Any],
        datetime_info: Dict[str, str],
        emit_status: callable = None,
    ) -> Dict[str, Any]:
        """Perform a simple search using SearXNG with intelligent full-fetch."""
        searxng_url = self._get_config("SEARXNG_URL")
        enable_full_fetch = self._get_config("full_fetch")
        max_full_fetch = self._get_config("full_fetch_results")
        show_reasoning = self._get_config("show_reasoning")

        user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
        fetch_timeout = 5

        if emit_status:
            await emit_status(f"🔍 Simple search via SearXNG for: {query}")

        results = []

        try:
            search_url = f"{searxng_url}/search"
            params = {"q": query, "format": "json"}

            response = requests.get(
                search_url,
                params=params,
                timeout=10,
                headers={"User-Agent": user_agent},
            )
            response.raise_for_status()

            data = response.json()
            raw_results = data.get("results", [])

            if emit_status:
                await emit_status(f"✅ Found {len(raw_results)} results from SearXNG")

            # Limit results to max_results
            max_results = self._get_config("max_results")
            limited_results = raw_results[:max_results]

            # First pass: collect all snippets
            snippets = [r.get("content", "") for r in limited_results]

            # Analyze if full fetch is needed
            should_full_fetch = False
            fetch_analysis = {
                "needs_full_fetch": False,
                "reason": "Full fetch disabled",
                "confidence": 0.0,
            }

            if enable_full_fetch:
                fetch_analysis = self._analyze_snippet_consistency(snippets, query)
                should_full_fetch = fetch_analysis["needs_full_fetch"]

                if emit_status:
                    if should_full_fetch:
                        await emit_status(
                            f"📊 Snippet analysis: {fetch_analysis['reason']} → fetching full content"
                        )
                    else:
                        await emit_status(
                            f"📊 Snippet analysis: {fetch_analysis['reason']} → skipping full fetch"
                        )

            for idx, result in enumerate(limited_results):
                processed = {
                    "title": result.get("title", "Untitled"),
                    "url": result.get("url", ""),
                    "snippet": result.get("content", ""),
                    "content": None,
                }

                # Only fetch full content if analysis says it's needed
                if should_full_fetch and idx < max_full_fetch and processed["url"]:
                    if emit_status:
                        await emit_status(
                            f"📄 Fetching full content ({idx+1}/{max_full_fetch})..."
                        )

                    try:
                        fetch_response = requests.get(
                            processed["url"],
                            timeout=fetch_timeout,
                            headers={"User-Agent": user_agent},
                        )
                        fetch_response.raise_for_status()

                        soup = BeautifulSoup(fetch_response.text, "html.parser")

                        for element in soup(
                            ["script", "style", "nav", "footer", "header"]
                        ):
                            element.decompose()

                        paragraphs = soup.find_all("p")
                        text_content = " ".join(
                            [p.get_text(strip=True) for p in paragraphs]
                        )

                        text_content = " ".join(text_content.split())
                        if len(text_content) > 2000:
                            text_content = text_content[:2000] + "..."

                        if text_content:
                            processed["content"] = text_content

                    except Exception as e:
                        logger.warning(f"Failed to fetch {processed['url']}: {e}")

                results.append(processed)

        except requests.exceptions.Timeout:
            logger.error("SearXNG search timed out")
            return {"error": "Search timed out", "results": [], "reasoning": reasoning}
        except requests.exceptions.RequestException as e:
            logger.error(f"SearXNG search failed: {e}")
            return {
                "error": f"Search failed: {str(e)}",
                "results": [],
                "reasoning": reasoning,
            }
        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse SearXNG response: {e}")
            return {
                "error": "Invalid response from search engine",
                "results": [],
                "reasoning": reasoning,
            }

        response_data = {
            "results": results,
            "mode": "simple",
            "search_engine": "SearXNG",
            "datetime_context": datetime_info,
            "query_used": query,
            "fetch_analysis": fetch_analysis,
        }

        if show_reasoning:
            response_data["selection_reasoning"] = reasoning
            response_data["selection_message"] = self._format_reasoning_message(
                reasoning, "simple", datetime_info
            )

        return response_data

    async def _deep_search(
        self,
        query: str,
        sources: List[str],
        depth: str,
        reasoning: Dict[str, Any],
        datetime_info: Dict[str, str],
        emit_status: callable = None,
    ) -> Dict[str, Any]:
        """Perform a deep search using Vane."""
        vane_url = self._get_config("VANE_URL")
        vane_timeout = self._get_config("VANE_TIMEOUT")
        show_reasoning = self._get_config("show_reasoning")

        chat_provider_id = self._get_config("VANE_CHAT_MODEL_PROVIDER_ID")
        chat_model_key = self._get_config("VANE_CHAT_MODEL_KEY")
        embed_provider_id = self._get_config("VANE_EMBEDDING_MODEL_PROVIDER_ID")
        embed_model_key = self._get_config("VANE_EMBEDDING_MODEL_KEY")

        if not chat_provider_id or not embed_provider_id:
            error_msg = "Vane not configured. Set VANE_CHAT_MODEL_PROVIDER_ID and VANE_EMBEDDING_MODEL_PROVIDER_ID in admin valves."
            return {
                "error": error_msg,
                "results": [],
                "reasoning": reasoning,
                "selection_message": (
                    self._format_reasoning_message(reasoning, "deep", datetime_info)
                    if show_reasoning
                    else None
                ),
            }

        if emit_status:
            await emit_status(f"🧠 Deep search via Vane for: {query}")

        try:
            vane_sources = self._map_sources_to_vane(sources)
            optimization_mode = self._map_depth_to_optimization(depth)

            if emit_status:
                await emit_status(
                    f"⚙️ Mode: {optimization_mode} | Sources: {vane_sources}"
                )

            payload = {
                "query": query,
                "sources": vane_sources,
                "optimizationMode": optimization_mode,
                "stream": False,
                "chatModel": {"providerId": chat_provider_id, "key": chat_model_key},
                "embeddingModel": {
                    "providerId": embed_provider_id,
                    "key": embed_model_key,
                },
            }

            response = requests.post(
                f"{vane_url}/api/search",
                json=payload,
                timeout=vane_timeout,
                headers={"Content-Type": "application/json"},
            )
            response.raise_for_status()

            data = response.json()

            message = data.get("message", "")
            vane_sources_data = data.get("sources", [])

            if emit_status:
                await emit_status(
                    f"✅ Vane synthesized response with {len(vane_sources_data)} sources"
                )

            results = []
            for source in vane_sources_data:
                metadata = source.get("metadata", {})
                results.append(
                    {
                        "title": metadata.get("title", "Untitled"),
                        "url": metadata.get("url", ""),
                        "snippet": (
                            source.get("content", "")[:500]
                            if source.get("content")
                            else ""
                        ),
                        "content": source.get("content", ""),
                    }
                )

            response_data = {
                "message": message,
                "results": results,
                "mode": "deep",
                "optimization": optimization_mode,
                "search_engine": "Vane",
                "datetime_context": datetime_info,
                "query_used": query,
            }

            if show_reasoning:
                response_data["selection_reasoning"] = reasoning
                response_data["selection_message"] = self._format_reasoning_message(
                    reasoning, "deep", datetime_info
                )

            return response_data

        except requests.exceptions.Timeout:
            logger.error("Vane search timed out")
            return {
                "error": "Deep search timed out",
                "results": [],
                "reasoning": reasoning,
            }
        except requests.exceptions.RequestException as e:
            logger.error(f"Vane search failed: {e}")
            return {
                "error": f"Deep search failed: {str(e)}",
                "results": [],
                "reasoning": reasoning,
            }
        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse Vane response: {e}")
            return {
                "error": "Invalid response from Vane",
                "results": [],
                "reasoning": reasoning,
            }

    # ========================================================================
    # MAIN FUNCTION
    # ========================================================================

    async def optimized_search(
        self, query: str, __event_emitter__: callable = None
    ) -> Dict[str, Any]:
        """
        Perform an optimized search using SearXNG (simple) or Vane (deep).

        Automatically injects current date/time for temporal queries like
        "what's the weather today" or "latest news".

        **UserValves Settings (configure in chat interface):**
        - mode: Search mode - 'auto', 'simple', or 'deep'
        - sources: Comma-separated sources (web, academia, social)
        - full_fetch: Enable full page content extraction
        - full_fetch_results: Number of results to fully fetch (1-10)
        - max_results: Maximum number of search results to return (1-20, default: 5)
        - show_reasoning: Display mode selection reasoning

        Returns:
            Dictionary with results, mode used, datetime context, and optional reasoning.
        """

        async def emit_status(message: str):
            if __event_emitter__:
                await __event_emitter__(
                    {"type": "status", "data": {"description": message}}
                )

        # Get settings (UserValves overrides Valves)
        mode = self._get_config("mode")
        sources_str = self._get_config("sources")
        show_reasoning = self._get_config("show_reasoning")
        max_results = self._get_config("max_results")

        sources_list = self._parse_sources(sources_str)

        # Inject date/time for temporal queries
        enriched_query, datetime_info = self._inject_datetime(query)

        reasoning = {
            "query": query,
            "enriched_query": enriched_query if enriched_query != query else None,
            "mode_requested": mode,
            "mode_selected": mode,
            "selection_type": "manual" if mode != "auto" else "automatic",
            "triggers": [],
            "trigger_count": 0,
            "token_count": len(query.split()),
            "is_temporal": self._is_temporal_query(query),
            "user_settings": {
                "mode": mode,
                "sources": sources_str,
                "full_fetch": self._get_config("full_fetch"),
                "show_reasoning": show_reasoning,
                "max_results": max_results,
            },
        }

        if mode == "auto":
            is_complex, auto_reasoning = self._analyze_query_complexity(query)
            reasoning = auto_reasoning
            reasoning["mode_requested"] = "auto"
            reasoning["selection_type"] = "automatic"
            reasoning["enriched_query"] = (
                enriched_query if enriched_query != query else None
            )
            reasoning["user_settings"] = {
                "mode": mode,
                "sources": sources_str,
                "full_fetch": self._get_config("full_fetch"),
                "show_reasoning": show_reasoning,
                "max_results": max_results,
            }

            selected_mode = reasoning["decision"]

            if __event_emitter__ and show_reasoning:
                await __event_emitter__(
                    {
                        "type": "message",
                        "data": {
                            "content": self._format_reasoning_message(
                                reasoning, selected_mode, datetime_info
                            )
                        },
                    }
                )

            if emit_status:
                temporal_indicator = " 📅" if reasoning["is_temporal"] else ""
                await emit_status(
                    f"🎯 Auto-selected: {selected_mode.upper()} mode ({reasoning['trigger_count']} triggers){temporal_indicator}"
                )
        else:
            selected_mode = mode
            reasoning["mode_selected"] = selected_mode

            if __event_emitter__ and show_reasoning:
                temporal_indicator = " 📅" if reasoning["is_temporal"] else ""
                await __event_emitter__(
                    {
                        "type": "message",
                        "data": {
                            "content": f"📌 **Mode manually set to: {selected_mode.upper()}**{temporal_indicator}\n\nSources: {sources_str}\n\n**Current Time:** {datetime_info['full_context']}"
                        },
                    }
                )

        # Use enriched query for actual search
        search_query = enriched_query if reasoning.get("is_temporal") else query

        if selected_mode == "simple":
            return await self._simple_search(
                search_query, sources_list, reasoning, datetime_info, emit_status
            )
        elif selected_mode == "deep":
            return await self._deep_search(
                search_query,
                sources_list,
                "balanced",
                reasoning,
                datetime_info,
                emit_status,
            )
        else:
            return {
                "error": f"Invalid mode: {mode}. Use 'auto', 'simple', or 'deep'.",
                "results": [],
                "reasoning": reasoning,
            }
