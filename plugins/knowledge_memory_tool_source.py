"""
title: Knowledge Memory Tool
author: normen
description: Direct tool access to the auto-memory system so LLMs can search, read, add, update, and delete user memories.
version: 0.4.2
changelog:
    - v0.4.2: (codex+normen) Fixed direct knowledge route call by injecting an explicit DB session, preventing "'Depends' object has no attribute 'commit'" errors
    - v0.4.1: (codex+normen) Await knowledge base queries now that retrieval API is async, restoring dedup results
    - v0.4.0: (copilot+normen) Unified search_kb_memories function with knowledge_memory.py in shared helper section
    - v0.3.1: (codex+normen) Removed unused user valve toggle and synchronized helper stack
    - v0.3.0: (codex+normen) Synced helper stack with knowledge_memory.py and clarified KB update workflow
    - v0.2.0: (codex+normen) Removed filter dependency by implementing standalone memory management helpers
    - v0.1.3: (codex+normen) Replaced filter valves with a lightweight tool-specific configuration set
    - v0.1.2: (codex+normen) Moved helper utilities out of the Tools class so only CRUD/search APIs surface to the loader
    - v0.1.1: (codex+normen) Clarified tool method contracts for LLM callers and hid internal helpers from the public API
    - v0.1.0: (codex+normen) Added knowledge memory management tool built on top of the auto-memory filter helpers
"""

import io
import traceback
from datetime import datetime
from typing import Any, Optional, Sequence, List

from fastapi import Request, UploadFile
from fastapi.concurrency import run_in_threadpool
from pydantic import BaseModel, Field, ValidationError, model_validator

from open_webui.models.memories import Memories
from open_webui.models.users import Users
from open_webui.models.files import Files
from open_webui.models.knowledge import Knowledges, KnowledgeForm
from open_webui.storage.provider import Storage
from open_webui.internal.db import get_db
from open_webui.routers.knowledge import (
    add_file_to_knowledge_by_id,
    remove_file_from_knowledge_by_id,
    KnowledgeFileIdForm,
)
from open_webui.routers.files import upload_file_handler
from open_webui.routers.retrieval import query_collection_handler, QueryCollectionsForm


# === Memory Storage Helpers (keep this section in sync across knowledge_memory.py and knowledge_memory_tool.py) ===

class MemoryOperation(BaseModel):
    """Model for memory operations"""

    operation: str
    id: Optional[str] = None
    content: Optional[str] = None
    tags: List[str] = Field(default_factory=list)

    @model_validator(mode="after")
    def validate_fields(self) -> "MemoryOperation":
        """Validate required fields based on operation"""
        op = (self.operation or "").upper()
        if op not in {"NEW", "UPDATE", "DELETE"}:
            raise ValueError("operation must be NEW, UPDATE, or DELETE")
        self.operation = op

        if self.operation in {"UPDATE", "DELETE"} and not self.id:
            raise ValueError("id is required for UPDATE and DELETE operations")
        if self.operation in {"NEW", "UPDATE"} and not self.content:
            raise ValueError("content is required for NEW and UPDATE operations")
        return self


def format_memory_content(operation: MemoryOperation) -> str:
    """Format memory content with tag prefix removal and rehydration."""
    content = operation.content or ""

    import re

    content = re.sub(r"^\[Tags:[^\]]*\]\s*", "", content)

    if not operation.tags:
        return content
    return f"[Tags: {', '.join(operation.tags)}] {content}"


async def resolve_memory_kb_id(
    valves: BaseModel,
    user_obj: Any,
    *,
    log_prefix: str,
) -> Optional[str]:
    """Resolve or create the knowledge base ID for the active user."""
    if valves.knowledge_base_id:
        print(f"[{log_prefix}] Using configured knowledge base ID: {valves.knowledge_base_id}\n")
        return valves.knowledge_base_id

    kb_name = valves.knowledge_base_name
    if not kb_name:
        return None

    try:
        knowledge_bases = await run_in_threadpool(
            Knowledges.get_knowledge_bases_by_user_id,
            user_id=str(user_obj.id),
            permission="write",
        )
    except Exception as exc:
        print(f"[{log_prefix}] Failed to fetch knowledge bases: {exc}\n")
        return None

    for kb in knowledge_bases or []:
        if getattr(kb, "name", None) == kb_name:
            kb_id = getattr(kb, "id", None)
            print(f"[{log_prefix}] Resolved knowledge base '{kb_name}' to ID: {kb_id}\n")
            return kb_id

    try:
        created = await run_in_threadpool(
            Knowledges.insert_new_knowledge,
            user_id=str(user_obj.id),
            form_data=KnowledgeForm(
                name=kb_name,
                description="Knowledge base created automatically for memory storage",
                data={"file_ids": []},
                access_control={},
            ),
        )
        if created:
            kb_id = getattr(created, "id", None)
            print(f"[{log_prefix}] Created knowledge base '{kb_name}' with ID: {kb_id}\n")
            return kb_id
        print(f"[{log_prefix}] ERROR: Failed to create knowledge base '{kb_name}'\n")
    except Exception as exc:
        print(f"[{log_prefix}] Error resolving/creating knowledge base: {exc}\n{traceback.format_exc()}\n")
    return None


async def create_memory_file(
    operation: MemoryOperation,
    user_obj: Any,
    request: Request,
    *,
    source_label: str,
) -> Optional[str]:
    """Upload a memory payload as a knowledge base file and return its ID."""
    formatted_content = format_memory_content(operation)

    file_content = f"Memory created: {datetime.now().isoformat()}\n\n{formatted_content}"
    filename_base = operation.tags[0] if operation.tags else "memory"
    filename_base = "".join(
        c for c in filename_base if c.isalnum() or c in (" ", "-", "_")
    ).strip()
    if not filename_base:
        filename_base = "memory"
    filename = f"{filename_base}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"

    upload_file_obj = UploadFile(
        file=io.BytesIO(file_content.encode("utf-8")),
        filename=filename,
        headers={"content-type": "text/plain"},
    )

    metadata = {
        "name": filename,
        "content_type": "text/plain",
        "source": source_label,
    }

    file_model = await run_in_threadpool(
        upload_file_handler,
        request=request,
        file=upload_file_obj,
        user=user_obj,
        metadata=metadata,
        process=True,
        process_in_background=False,
        background_tasks=None,
    )

    if isinstance(file_model, dict):
        return file_model.get("id")
    return getattr(file_model, "id", None)


async def store_memory_to_kb(
    operation: MemoryOperation,
    valves: BaseModel,
    user_obj: Any,
    request: Request,
    *,
    log_prefix: str,
    source_label: str,
) -> Optional[str]:
    """Persist a memory to the knowledge base and return the new file ID."""
    kb_id = await resolve_memory_kb_id(valves, user_obj, log_prefix=log_prefix)
    if not kb_id:
        print(f"[{log_prefix}] No knowledge base available for storage\n")
        return None

    file_id = await create_memory_file(
        operation, user_obj, request, source_label=source_label
    )
    if not file_id:
        print(f"[{log_prefix}] Failed to create memory file\n")
        return None

    try:
        def _add_file_to_kb_with_session() -> Any:
            with get_db() as db:
                return add_file_to_knowledge_by_id(
                    request=request,
                    id=kb_id,
                    form_data=KnowledgeFileIdForm(file_id=file_id),
                    user=user_obj,
                    db=db,
                )

        await run_in_threadpool(_add_file_to_kb_with_session)
    except Exception as exc:
        print(f"[{log_prefix}] Failed to add file to knowledge base: {exc}\n")
        return None

    print(f"[{log_prefix}] Memory stored in knowledge base (file_id: {file_id})\n")
    return file_id


async def delete_memory_from_kb(
    file_id: str,
    valves: BaseModel,
    user_obj: Any,
    request: Request,
    *,
    log_prefix: str,
) -> bool:
    """Remove a memory file from the knowledge base and clean up storage."""
    kb_id = await resolve_memory_kb_id(valves, user_obj, log_prefix=log_prefix)
    if not kb_id:
        print(f"[{log_prefix}] No knowledge base found for deletion\n")
        return False

    file = await run_in_threadpool(Files.get_file_by_id, file_id)
    file_path = getattr(file, "path", None) if file else None

    try:
        await run_in_threadpool(
            remove_file_from_knowledge_by_id,
            id=kb_id,
            form_data=KnowledgeFileIdForm(file_id=file_id),
            user=user_obj,
        )
    except Exception as exc:
        print(f"[{log_prefix}] Failed to remove file from knowledge base: {exc}\n")
        return False

    if file_path:
        try:
            await run_in_threadpool(Storage.delete_file, file_path)
        except Exception as exc:
            print(f"[{log_prefix}] Failed to delete physical file {file_path}: {exc}\n")

    print(f"[{log_prefix}] Memory deleted from knowledge base (file_id: {file_id})\n")
    return True


async def update_memory_in_kb(
    operation: MemoryOperation,
    valves: BaseModel,
    user_obj: Any,
    request: Request,
    *,
    log_prefix: str,
    source_label: str,
) -> Optional[str]:
    """Replace an existing knowledge base memory with updated content."""
    old_file_id = operation.id
    if not old_file_id:
        print(f"[{log_prefix}] Cannot update memory without an existing file ID\n")
        return None

    # NOTE: We intentionally create a brand-new file before removing the old one.
    # Updating in place via process_file spreads file.meta (with None fields) into
    # ChromaDB's metadata and triggers TypeErrors. The create-then-delete pattern
    # matches knowledge_memory.py's behaviour and avoids that upstream bug.
    new_file_id = await store_memory_to_kb(
        operation,
        valves,
        user_obj,
        request,
        log_prefix=log_prefix,
        source_label=source_label,
    )
    if not new_file_id:
        return None

    await delete_memory_from_kb(
        old_file_id,
        valves,
        user_obj,
        request,
        log_prefix=log_prefix,
    )
    return new_file_id


async def fetch_legacy_memory_content(
    memory_id: str,
    user_obj: Any,
    *,
    log_prefix: str,
) -> Optional[str]:
    """Retrieve memory content from the legacy database storage."""
    user_id = str(getattr(user_obj, "id", "")) or None
    if not user_id:
        return None

    try:
        memory = await run_in_threadpool(
            Memories.get_memory_by_id_and_user_id,
            id=memory_id,
            user_id=user_id,
        )
    except Exception as exc:
        print(f"[{log_prefix}] Failed to fetch legacy memory by id/user: {exc}\n")
        memory = None

    if not memory and getattr(user_obj, "role", "user") == "admin":
        try:
            memory = await run_in_threadpool(Memories.get_memory_by_id, memory_id)
        except Exception as exc:
            print(f"[{log_prefix}] Failed to fetch legacy memory by id: {exc}\n")
            return None

    if not memory:
        return None

    if str(getattr(memory, "user_id", "")) != user_id and getattr(
        user_obj, "role", "user"
    ) != "admin":
        return None

    return getattr(memory, "content", None)


async def search_legacy_memories(
    query: str,
    limit: int,
    user_obj: Any,
    *,
    log_prefix: str,
) -> list[dict]:
    """Simple substring search over legacy memories."""
    user_id = str(getattr(user_obj, "id", "")) or None
    if not user_id:
        return []

    try:
        memory_objs = await run_in_threadpool(
            Memories.get_memories_by_user_id,
            user_id=user_id,
        )
    except Exception as exc:
        print(f"[{log_prefix}] Failed to fetch legacy memories: {exc}\n")
        memory_objs = None

    if not memory_objs:
        return []

    results: list[dict] = []
    needle = query.lower()
    for mem in memory_objs:
        content = getattr(mem, "content", "") or ""
        if needle in content.lower():
            results.append(
                {
                    "id": getattr(mem, "id", None),
                    "content": content,
                }
            )
            if len(results) >= limit:
                break
    return results


async def search_kb_memories(
    valves: BaseModel,
    user_obj: Any,
    request: Request,
    query: str,
    limit: int,
    *,
    log_prefix: str,
) -> list[dict]:
    """Search knowledge base memories using provided query string."""
    kb_id = await resolve_memory_kb_id(valves, user_obj, log_prefix=log_prefix)
    if not kb_id:
        return []

    form = QueryCollectionsForm(
        collection_names=[kb_id],
        query=query,
        k=limit,
    )

    try:
        results = await query_collection_handler(
            request=request,
            form_data=form,
            user=user_obj,
        )
    except Exception as exc:
        print(f"[{log_prefix}] KB query failed: {exc}\n{traceback.format_exc()}\n")
        return []

    if not isinstance(results, dict):
        return []

    ids = results.get("ids", [[]])
    docs = results.get("documents", [[]])
    metas = results.get("metadatas", [[]])
    distances = results.get("distances", [[]])

    kb_matches: list[dict] = []
    for idx, doc_text in enumerate(docs[0] if docs else []):
        metadata = metas[0][idx] if metas and metas[0] and idx < len(metas[0]) else {}
        file_id = metadata.get("file_id") if isinstance(metadata, dict) else ""
        distance = distances[0][idx] if distances and distances[0] and idx < len(distances[0]) else None

        if doc_text and "\n\n" in doc_text:
            content = doc_text.split("\n\n", 1)[1]
        else:
            content = doc_text

        kb_matches.append(
            {
                "id": file_id or None,
                "content": content or "",
                "score": distance,
            }
        )
    return kb_matches


async def execute_legacy_memory_operation(
    operation: MemoryOperation,
    user_obj: Any,
    *,
    log_prefix: str,
) -> Optional[str]:
    """Persist legacy memories using the classic database tables."""
    user_id = str(getattr(user_obj, "id", "")) or None
    if not user_id:
        raise ValueError("User identifier is required for legacy memory operations.")

    formatted = format_memory_content(operation)

    try:
        if operation.operation == "NEW":
            await run_in_threadpool(
                Memories.insert_new_memory,
                user_id=user_id,
                content=formatted,
            )
            return "legacy"

        if operation.operation == "UPDATE" and operation.id:
            await run_in_threadpool(
                Memories.update_memory_by_id_and_user_id,
                id=operation.id,
                user_id=user_id,
                content=formatted,
            )
            return operation.id

        if operation.operation == "DELETE" and operation.id:
            await run_in_threadpool(
                Memories.delete_memory_by_id_and_user_id,
                id=operation.id,
                user_id=user_id,
            )
            return operation.id
    except Exception as exc:
        print(f"[{log_prefix}] Legacy memory operation failed: {exc}\n{traceback.format_exc()}\n")
        return None

    return None


# === End shared memory helper section ===


def _normalize_tags(tags: Optional[Sequence[str] | str]) -> list[str]:
    if tags is None:
        return []

    if isinstance(tags, str):
        pieces = [piece.strip() for piece in tags.split(",")]
    else:
        pieces = [str(item).strip() for item in tags]

    return [piece for piece in pieces if piece]


async def _resolve_user(__user__: Optional[dict]) -> Any:
    if not __user__ or not __user__.get("id"):
        raise ValueError("User context with an id is required.")
    user_id = str(__user__["id"])
    user_obj = await run_in_threadpool(Users.get_user_by_id, user_id)
    if not user_obj:
        raise ValueError(f"User '{user_id}' could not be resolved.")
    return user_obj


async def _fetch_kb_memory_content(file_id: str, user_obj: Any) -> Optional[str]:
    try:
        file = await run_in_threadpool(Files.get_file_by_id, file_id)
    except Exception as exc:
        print(f"[knowledge-memory-tool] Failed to fetch file metadata: {exc}\n")
        return None

    if not file:
        return None

    if str(getattr(file, "user_id", "")) != str(getattr(user_obj, "id", "")) and getattr(
        user_obj, "role", "user"
    ) != "admin":
        return None

    file_path = Storage.get_file(getattr(file, "path", ""))
    if not file_path:
        return None

    try:
        with open(file_path, "r", encoding="utf-8", errors="ignore") as handle:
            return handle.read()
    except Exception as exc:
        print(f"[knowledge-memory-tool] Failed to read memory file: {exc}\n")
        return None


async def _handle_search_memories(
    valves: BaseModel,
    query: str,
    limit: Optional[int],
    user_obj: Any,
    request: Request,
) -> str:
    resolved_limit = (
        limit if isinstance(limit, int) and limit > 0 else valves.max_memories_for_context
    )

    results: list[dict] = []

    if not valves.use_legacy_memory_system:
        kb_results = await search_kb_memories(
            valves,
            user_obj,
            request,
            query,
            resolved_limit,
            log_prefix="knowledge-memory-tool",
        )
        results.extend(kb_results)

    legacy_results = await search_legacy_memories(
        query,
        resolved_limit,
        user_obj,
        log_prefix="knowledge-memory-tool",
    )
    results.extend(legacy_results)

    if not results:
        return "No memories matched your query."

    seen_ids: set[str] = set()
    deduped: list[dict] = []
    for item in results:
        memory_id = item.get("id")
        if memory_id and memory_id in seen_ids:
            continue
        deduped.append(item)
        if memory_id:
            seen_ids.add(memory_id)

    return _format_search_results(deduped[:resolved_limit])


async def _handle_get_memory_content(
    valves: BaseModel,
    memory_id: str,
    user_obj: Any,
) -> str:
    content = None

    if not valves.use_legacy_memory_system:
        content = await _fetch_kb_memory_content(memory_id, user_obj)

    if content is None:
        content = await fetch_legacy_memory_content(
            memory_id, user_obj, log_prefix="knowledge-memory-tool"
        )

    if content is None:
        return "Memory not found or inaccessible."

    return _truncate_text(content, 4000)


async def _execute_memory_operation(
    valves: BaseModel,
    operation: MemoryOperation,
    user_obj: Any,
    request: Optional[Request],
) -> Optional[str]:
    if valves.use_legacy_memory_system:
        return await execute_legacy_memory_operation(
            operation, user_obj, log_prefix="knowledge-memory-tool"
        )

    if request is None:
        raise ValueError("Request context is required for knowledge base operations.")

    if operation.operation == "NEW":
        return await store_memory_to_kb(
            operation,
            valves,
            user_obj,
            request,
            log_prefix="knowledge-memory-tool",
            source_label="memory-tool",
        )

    if operation.operation == "UPDATE" and operation.id:
        # NOTE: Knowledge base updates create a fresh file and delete the old one.
        # The resulting memory identifier can therefore change after a successful update.
        return await update_memory_in_kb(
            operation,
            valves,
            user_obj,
            request,
            log_prefix="knowledge-memory-tool",
            source_label="memory-tool",
        )

    if operation.operation == "DELETE" and operation.id:
        await delete_memory_from_kb(
            operation.id,
            valves,
            user_obj,
            request,
            log_prefix="knowledge-memory-tool",
        )
        return operation.id

    return None


def _truncate_text(text: str, limit: int) -> str:
    if limit > 0 and len(text) > limit:
        return text[:limit].rstrip() + "…"
    return text.strip()


def _format_search_results(results: list[dict]) -> str:
    lines: list[str] = []
    for idx, item in enumerate(results, start=1):
        content = _truncate_text(item.get("content", ""), 400)
        memory_id = item.get("id") or "unknown"
        score = item.get("score")
        score_text = (
            f", score: {score:.4f}"
            if isinstance(score, (int, float))
            else ""
        )
        lines.append(f"{idx}. (id: {memory_id}{score_text}) {content}")
    return "\n".join(lines)


class Tools:
    """
    Exposes CRUD-style helpers for managing memories via tool calls.
    """

    class Valves(BaseModel):
        knowledge_base_id: Optional[str] = Field(
            default=None,
            description="Static knowledge base ID. Overrides per-user resolution when set.",
        )
        knowledge_base_name: Optional[str] = Field(
            default="memory-base",
            description="Knowledge base name to resolve per user when no ID is provided.",
        )
        max_memories_for_context: int = Field(
            default=10,
            ge=1,
            description="Maximum number of records returned by `search_memories` in knowledge-base mode.",
        )
        use_legacy_memory_system: bool = Field(
            default=False,
            description="Enable the legacy memory tables instead of the knowledge base workflow.",
        )

    def __init__(self) -> None:
        self.valves = self.Valves()

    async def search_memories(
        self,
        query: str,
        limit: Optional[int] = None,
        __user__: Optional[dict] = None,
        __request__: Optional[Request] = None,
    ) -> str:
        """
        Search stored memories for snippets containing the provided query text.

        Args:
            query: Plain-text search term or phrase.
            limit: Optional maximum number of results (defaults to the valve setting).
            __user__: Injected OpenWebUI user context (must include an ``id``).
            __request__: FastAPI request object required for knowledge-base calls.

        Returns:
            A numbered list of matches with IDs and truncated content, or an
            explanatory error string.
        """
        query_value = (query or "").strip()
        if not query_value:
            return "Search query cannot be empty."
        if __request__ is None and not self.valves.use_legacy_memory_system:
            return "Request context is required to search memories."

        try:
            user_obj = await _resolve_user(__user__)
        except Exception as exc:
            return f"Unable to resolve user: {exc}"

        try:
            return await _handle_search_memories(
                self.valves, query_value, limit, user_obj, __request__
            )
        except Exception as exc:
            return f"Memory search failed: {exc}"

    async def get_memory_content(
        self,
        memory_id: str,
        __user__: Optional[dict] = None,
    ) -> str:
        """
        Fetch the full text of a single stored memory.

        Args:
            memory_id: Identifier returned by `search_memories` (legacy or KB ID).
            __user__: Injected OpenWebUI user context (must include an ``id``).

        Returns:
            The memory body limited to 4000 characters, or an error string.
        """
        memory_value = (memory_id or "").strip()
        if not memory_value:
            return "Memory ID is required."

        try:
            user_obj = await _resolve_user(__user__)
        except Exception as exc:
            return f"Unable to resolve user: {exc}"

        try:
            return await _handle_get_memory_content(
                self.valves, memory_value, user_obj
            )
        except Exception as exc:
            return f"Failed to read memory: {exc}"

    async def create_memory(
        self,
        content: str,
        tags: Optional[Sequence[str] | str] = None,
        __user__: Optional[dict] = None,
        __request__: Optional[Request] = None,
    ) -> str:
        """
        Store a new memory entry on behalf of the active user.

        Args:
            content: The text to remember (must be non-empty).
            tags: Iterable or comma-delimited tags describing the memory.
            __user__: Injected OpenWebUI user context (must include an ``id``).
            __request__: FastAPI request object required for knowledge-base writes.

        Returns:
            Confirmation with the resulting memory ID (knowledge-base) or
            ``legacy`` when the legacy store is active. Error details on failure.
        """
        normalized_content = (content or "").strip()
        if not normalized_content:
            return "Memory content cannot be empty."
        if __request__ is None and not self.valves.use_legacy_memory_system:
            return "Request context is required to create memories."

        try:
            user_obj = await _resolve_user(__user__)
        except Exception as exc:
            return f"Unable to resolve user: {exc}"

        tag_list = _normalize_tags(tags)

        try:
            operation = MemoryOperation(
                operation="NEW",
                content=normalized_content,
                tags=tag_list,
            )
        except ValidationError as exc:
            return f"Invalid memory parameters: {exc}"

        try:
            result_id = await _execute_memory_operation(
                self.valves, operation, user_obj, __request__
            )
        except Exception as exc:
            return f"Failed to create memory: {exc}"

        identifier = result_id or "legacy"
        return f"Memory stored successfully (id: {identifier})."

    async def update_memory(
        self,
        memory_id: str,
        content: str,
        tags: Optional[Sequence[str] | str] = None,
        __user__: Optional[dict] = None,
        __request__: Optional[Request] = None,
    ) -> str:
        """
        Replace the content (and optionally tags) of an existing memory.

        Args:
            memory_id: Identifier of the memory being updated.
            content: Replacement text for the memory.
            tags: Iterable or comma-delimited tags describing the updated memory.
            __user__: Injected OpenWebUI user context (must include an ``id``).
            __request__: FastAPI request object required for knowledge-base writes.

        Returns:
            Confirmation with the effective memory ID, or an error string.
        """
        memory_value = (memory_id or "").strip()
        if not memory_value:
            return "Memory ID is required."
        normalized_content = (content or "").strip()
        if not normalized_content:
            return "Updated memory content cannot be empty."
        if __request__ is None and not self.valves.use_legacy_memory_system:
            return "Request context is required to update memories."

        try:
            user_obj = await _resolve_user(__user__)
        except Exception as exc:
            return f"Unable to resolve user: {exc}"

        tag_list = _normalize_tags(tags)

        try:
            operation = MemoryOperation(
                operation="UPDATE",
                id=memory_value,
                content=normalized_content,
                tags=tag_list,
            )
        except ValidationError as exc:
            return f"Invalid memory parameters: {exc}"

        try:
            result_id = await _execute_memory_operation(
                self.valves, operation, user_obj, __request__
            )
        except Exception as exc:
            return f"Failed to update memory: {exc}"

        identifier = result_id or memory_value
        return f"Memory updated successfully (id: {identifier})."

    async def delete_memory(
        self,
        memory_id: str,
        __user__: Optional[dict] = None,
        __request__: Optional[Request] = None,
    ) -> str:
        """
        Remove a memory from the configured storage backend.

        Args:
            memory_id: Identifier returned by `search_memories` or `create_memory`.
            __user__: Injected OpenWebUI user context (must include an ``id``).
            __request__: FastAPI request object required for knowledge-base writes.

        Returns:
            Success confirmation or an error string.
        """
        memory_value = (memory_id or "").strip()
        if not memory_value:
            return "Memory ID is required."
        if __request__ is None and not self.valves.use_legacy_memory_system:
            return "Request context is required to delete memories."

        try:
            user_obj = await _resolve_user(__user__)
        except Exception as exc:
            return f"Unable to resolve user: {exc}"

        try:
            operation = MemoryOperation(
                operation="DELETE",
                id=memory_value,
                content="",
                tags=[],
            )
        except ValidationError as exc:
            return f"Invalid memory parameters: {exc}"

        try:
            await _execute_memory_operation(
                self.valves, operation, user_obj, __request__
            )
        except Exception as exc:
            return f"Failed to delete memory: {exc}"

        return f"Memory deleted successfully (id: {memory_value})."