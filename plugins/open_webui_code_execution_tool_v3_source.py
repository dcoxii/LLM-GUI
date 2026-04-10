# openwebui_tools_v3.py
# --------------------------------------------------------------
# Refined Open WebUI code‑execution tool (performance‑focused)
# --------------------------------------------------------------
#   • Session‑aware sandbox (temporary directory per chat)
#   • File write / read / list helpers
#   • Python & shell execution (async subprocess, timeout)
#   • pip‑install, lint, dependency‑check utilities
#   • Environment‑info & session‑info helpers
#   • Structured markdown output (truncated for UI safety)
# --------------------------------------------------------------
#   Safety‑related checks (blocked imports / shell patterns) are
#   optional and disabled by default (see `Valves`).
#   Made by Cole
# --------------------------------------------------------------

import asyncio
import os
import re
import json
import time
import hashlib
import shutil
import tempfile
import ast
from pathlib import Path
from dataclasses import dataclass, field
from typing import Any, List, Dict, Tuple, Optional, Union
from datetime import datetime

# ----------------------------------------------------------------------
# 1️⃣  Configuration (exposed in the Open WebUI Settings drawer)
# ----------------------------------------------------------------------
@dataclass
class Valves:
    """
    Simple configuration container – Open WebUI UI can edit any field that
    is a plain str / int / bool.  All limits are expressed as literals
    for maximum clarity in the UI.
    """

    # ── Execution limits ────────────────────────────────────────
    python_cmd: str = "python3"
    max_execution_time: int = 30                # seconds
    max_output_lines: int = 150
    max_output_chars: int = 50_000
    max_file_size_bytes: int = 10_485_760        # 10 MiB

    # ── Feature toggles ────────────────────────────────────────
    allow_shell: bool = True
    allow_pip_install: bool = True
    allow_file_persistence: bool = True

    # ── Session handling ───────────────────────────────────────
    session_timeout_minutes: int = 30
    max_sessions: int = 10

    # ── (Optional) Safety toggles – keep disabled for speed ─────
    enable_import_blocking: bool = False
    blocked_imports: str = "subprocess,multiprocessing,ctypes,_thread"
    enable_shell_blocking: bool = False
    blocked_shell_patterns: str = (
        r"rm\s+-rf\s+/,mkfs\.,dd\s+if=,:\(\)\{,>\s*/dev/sd,chmod\s+-R\s+777\s+/"
    )

# ----------------------------------------------------------------------
# 2️⃣  Utility helpers
# ----------------------------------------------------------------------
def _truncate_output(text: str, max_lines: int, max_chars: int) -> Tuple[str, bool]:
    """Trim a string to the configured line / character limits."""
    if not text:
        return "", False

    truncated = False
    result = text

    if len(result) > max_chars:
        result = result[:max_chars]
        truncated = True

    lines = result.splitlines()
    if len(lines) > max_lines:
        result = "\n".join(lines[:max_lines])
        truncated = True

    if truncated:
        result += f"\n\n... [OUTPUT TRUNCATED – {len(text)} chars, {len(text.splitlines())} lines total]"

    return result, truncated


def _coerce_to_string(value: Any) -> str:
    """Turn anything the LLM might send into a plain string."""
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        for key in ("code", "command", "text", "input", "script", "content", "source"):
            if key in value and isinstance(value[key], str):
                return value[key]
        return json.dumps(value)
    if isinstance(value, (list, tuple)):
        return "\n".join(str(v) for v in value)
    return str(value) if value is not None else ""


def _extract_code_block(text: str, lang: str = "python") -> str:
    """Return the raw code from a Markdown‑fenced block (or the raw text)."""
    if not text:
        return ""

    text = text.strip()
    if "```" not in text:
        return text

    # Language‑specific block first
    pattern = rf"```{lang}\s*\n(.*?)```"
    m = re.search(pattern, text, re.DOTALL | re.IGNORECASE)
    if m:
        return m.group(1).strip()

    # Generic fenced block
    m = re.search(r"```\s*\n(.*?)```", text, re.DOTALL)
    if m:
        return m.group(1).strip()

    # Inline back‑ticks
    m = re.search(r"`([^`]+)`", text)
    if m and "\n" not in m.group(1):
        return m.group(1).strip()

    return text


def _sanitize_filename(name: str) -> str:
    """Strip dangerous characters & limit length – never escape the session dir."""
    name = os.path.basename(name)                 # drop any path component
    name = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", name)
    return name[:255] or "unnamed"


def _hash_code(code: str) -> str:
    return hashlib.md5(code.encode()).hexdigest()[:8]


# ----------------------------------------------------------------------
# 3️⃣  Session management (isolated temp dirs, file catalog, history)
# ----------------------------------------------------------------------
class ExecutionSession:
    """One isolated workspace for a chat (or any caller)."""

    def __init__(self, session_id: str, config: Valves):
        self.session_id = session_id
        self.cfg = config
        self.created_at = time.time()
        self.last_accessed = self.created_at
        self.execution_count = 0
        self.temp_dir = tempfile.mkdtemp(prefix=f"owui_{session_id}_")
        self.files: Dict[str, str] = {}   # filename → absolute path
        self.history: List[Dict] = []     # small execution log

    # --------------------------------------------------------------
    def touch(self) -> None:
        self.last_accessed = time.time()

    def is_expired(self) -> bool:
        age_min = (time.time() - self.last_accessed) / 60
        return age_min > self.cfg.session_timeout_minutes

    # --------------------------------------------------------------
    def add_file(self, filename: str, content: str) -> str:
        safe = _sanitize_filename(filename)
        if len(self.files) >= self.cfg.max_sessions:
            raise ValueError("Maximum file count reached for this session")
        if len(content.encode()) > self.cfg.max_file_size_bytes:
            raise ValueError("File exceeds the per‑file size limit")

        path = Path(self.temp_dir) / safe
        path.write_text(content, encoding="utf-8")
        path.chmod(0o600)               # owner‑only – still cheap
        self.files[safe] = str(path)
        return safe

    def get_file(self, filename: str) -> Optional[str]:
        safe = _sanitize_filename(filename)
        path = self.files.get(safe)
        if path and Path(path).exists():
            return Path(path).read_text(encoding="utf-8")
        return None

    def list_files(self) -> List[str]:
        return list(self.files.keys())

    # --------------------------------------------------------------
    def add_history(self, tool: str, input_summary: str,
                    success: bool, duration: float) -> None:
        self.history.append({
            "tool": tool,
            "input": input_summary[:200],
            "success": success,
            "duration": round(duration, 2),
            "timestamp": datetime.now().isoformat(),
        })
        # keep only the most recent 50 entries
        if len(self.history) > 50:
            self.history = self.history[-50:]

    # --------------------------------------------------------------
    def cleanup(self) -> None:
        shutil.rmtree(self.temp_dir, ignore_errors=True)


class SessionManager:
    """Keeps a bounded set of sessions and periodically cleans up expired ones."""

    def __init__(self, cfg: Valves):
        self.cfg = cfg
        self.sessions: Dict[str, ExecutionSession] = {}
        self._cleanup_counter = 0

    def get_or_create(self, session_id: Optional[str] = None) -> ExecutionSession:
        """Return an existing session, or make a fresh one (evicting old ones if needed)."""
        # occasional purge of stale sessions
        self._maybe_cleanup()

        if session_id and session_id in self.sessions:
            sess = self.sessions[session_id]
            if not sess.is_expired():
                sess.touch()
                return sess
            # expired – delete it
            sess.cleanup()
            del self.sessions[session_id]

        # need a brand‑new session
        new_id = session_id or hashlib.sha256(str(time.time()).encode()).hexdigest()[:12]

        # enforce global session limit
        if len(self.sessions) >= self.cfg.max_sessions:
            # drop the least‑recently‑used session
            oldest = min(self.sessions, key=lambda k: self.sessions[k].last_accessed)
            self.sessions[oldest].cleanup()
            del self.sessions[oldest]

        sess = ExecutionSession(new_id, self.cfg)
        self.sessions[new_id] = sess
        return sess

    # --------------------------------------------------------------
    def _maybe_cleanup(self) -> None:
        """Run a cheap cleanup every N calls (N≈10)."""
        self._cleanup_counter += 1
        if self._cleanup_counter % 10 != 0:
            return
        for sid in list(self.sessions):
            if self.sessions[sid].is_expired():
                self.sessions[sid].cleanup()
                del self.sessions[sid]


# ----------------------------------------------------------------------
# 4️⃣  Subprocess runner (async, timeout, optional shell)
# ----------------------------------------------------------------------
async def _run_subprocess(
    cmd: Union[List[str], str],
    cwd: str,
    timeout: int,
    *,
    is_shell: bool = False,
    env: Optional[Dict[str, str]] = None,
) -> Tuple[int, str, str]:
    """
    Execute a command with a hard timeout.
    Returns (returncode, stdout, stderr).  A returncode of -1 signals timeout.
    """
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)

    # Choose the right asyncio API based on `is_shell`
    try:
        if is_shell:
            proc = await asyncio.create_subprocess_shell(
                cmd,
                cwd=cwd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                env=merged_env,
            )
        else:
            if isinstance(cmd, str):
                cmd = cmd.split()
            proc = await asyncio.create_subprocess_exec(
                *cmd,
                cwd=cwd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
                env=merged_env,
            )

        try:
            stdout_b, stderr_b = await asyncio.wait_for(proc.communicate(), timeout=timeout)
            stdout = stdout_b.decode(errors="replace") if stdout_b else ""
            stderr = stderr_b.decode(errors="replace") if stderr_b else ""
            return proc.returncode or 0, stdout, stderr
        except asyncio.TimeoutError:
            proc.kill()
            await proc.communicate()   # swallow any remaining output
            return -1, "", f"⏱️ Execution timed out after {timeout}s"
    except FileNotFoundError as e:
        return -1, "", f"❌ Command not found: {e}"
    except Exception as e:
        return -1, "", f"❌ Execution error: {type(e).__name__}: {e}"


# ----------------------------------------------------------------------
# 5️⃣  Output formatting (markdown for the UI)
# ----------------------------------------------------------------------
class OutputFormatter:
    """Consistent markdown output for all tool methods."""

    def __init__(self, cfg: Valves):
        self.cfg = cfg

    # --------------------------------------------------------------
    def format_result(
        self,
        exit_code: int,
        stdout: str,
        stderr: str,
        duration: float,
        extra: Optional[Dict[str, Any]] = None,
    ) -> str:
        parts = []

        # status line
        if exit_code == 0:
            parts.append("✅ **Status:** Success")
        elif exit_code == -1:
            parts.append("⚠️ **Status:** Timeout / Error")
        else:
            parts.append(f"❌ **Status:** Failed (code {exit_code})")

        parts.append(f"⏱️ **Duration:** {duration:.2f}s")

        if extra:
            for k, v in extra.items():
                parts.append(f"📋 **{k}:** {v}")

        # stdout
        if stdout:
            out, _ = _truncate_output(stdout, self.cfg.max_output_lines, self.cfg.max_output_chars)
            parts.append(f"**stdout:**\n```\n{out}\n```")

        # stderr
        if stderr:
            err, _ = _truncate_output(
                stderr, self.cfg.max_output_lines // 2, self.cfg.max_output_chars // 2
            )
            parts.append(f"**stderr:**\n```\n{err}\n```")

        # synthesis hint (helps the LLM keep the conversation on track)
        parts.append("\n---\n**💡 SYNTHESIS:** Summarise the goal, key results, and next steps.")
        return "\n\n".join(parts)

    # --------------------------------------------------------------
    def format_error(self, title: str, message: str, suggestion: Optional[str] = None) -> str:
        parts = [f"❌ **{title}:** {message}"]
        if suggestion:
            parts.append(f"💡 **Suggestion:** {suggestion}")
        return "\n\n".join(parts)

    # --------------------------------------------------------------
    def format_info(self, title: str, items: Dict[str, Any]) -> str:
        lines = [f"📋 **{title}**"]
        for k, v in items.items():
            if isinstance(v, list):
                lines.append(f"- **{k}:**")
                lines.extend([f"  - {i}" for i in v])
            else:
                lines.append(f"- **{k}:** {v}")
        return "\n".join(lines)


# ----------------------------------------------------------------------
# 6️⃣  The public Tools class (what Open WebUI actually calls)
# ----------------------------------------------------------------------
class Tools:
    """
    Feature‑rich (but performance‑oriented) execution toolbox for Open WebUI.
    All safety‑related checks are optional and disabled by default, so the
    overhead is virtually zero.  If you later decide you want them back,
    just flip the corresponding flags in `Valves`.
    """

    def __init__(self):
        # UI will expose these fields automatically
        self.valves = Valves()
        self.session_mgr = SessionManager(self.valves)
        self.formatter = OutputFormatter(self.valves)
        self._run_counter = 0      # used for auto‑naming temporary scripts

    # ------------------------------------------------------------------
    # Helper: pull the list of blocked imports / shell patterns (if enabled)
    # ------------------------------------------------------------------
    def _blocked_imports(self) -> List[str]:
        return [i.strip() for i in self.valves.blocked_imports.split(",")] if self.valves.enable_import_blocking else []

    def _blocked_shell_patterns(self) -> List[re.Pattern]:
        if not self.valves.enable_shell_blocking:
            return []
        return [re.compile(p, re.IGNORECASE) for p in self.valves.blocked_shell_patterns.split(",") if p]

    # ------------------------------------------------------------------
    # Helper: turn whatever the LLM sent into clean code
    # ------------------------------------------------------------------
    def _extract_code(self, code: str = "", text: str = "", **kwargs) -> Optional[str]:
        raw = _coerce_to_string(code) or _coerce_to_string(text) or _coerce_to_string(kwargs)
        if not raw.strip():
            return None
        return _extract_code_block(raw)

    # ------------------------------------------------------------------
    # 1️⃣  Python execution (stateful session optional)
    # ------------------------------------------------------------------
    async def exec_python(
        self,
        code: str = "",
        text: str = "",
        session_id: str = "",
        save_as: str = "",
        **kwargs,
    ) -> str:
        start = time.time()
        py_code = self._extract_code(code, text, **kwargs)

        if not py_code:
            return self.formatter.format_error(
                "Input Error",
                "No Python code detected – provide a `code=` parameter or a markdown block",
            )

        # *** OPTIONAL import blocking ***
        if self.valves.enable_import_blocking:
            blocked = [imp for imp in self._blocked_imports() if re.search(rf"\b{imp}\b", py_code)]
            if blocked:
                return self.formatter.format_error(
                    "Security",
                    f"Blocked imports found: {', '.join(blocked)}",
                    "Remove those imports or use exec_shell for subprocess work",
                )

        # get or create a session (for file persistence)
        session = self.session_mgr.get_or_create(session_id or None)
        session.execution_count += 1
        self._run_counter += 1

        # write the script to the session directory (so the user can later read it)
        script_name = save_as or f"script_{self._run_counter}_{_hash_code(py_code)}.py"
        script_path = Path(session.temp_dir) / _sanitize_filename(script_name)
        script_path.write_text(py_code, encoding="utf-8")
        script_path.chmod(0o600)

        if save_as:
            session.files[_sanitize_filename(save_as)] = str(script_path)

        # run it
        exit_code, stdout, stderr = await _run_subprocess(
            [self.valves.python_cmd, str(script_path)],
            cwd=session.temp_dir,
            timeout=self.valves.max_execution_time,
        )
        duration = time.time() - start
        success = exit_code == 0

        session.add_history("exec_python", py_code[:100], success, duration)

        extra = {"Session": session.session_id}
        if save_as:
            extra["Saved as"] = save_as

        return self.formatter.format_result(exit_code, stdout, stderr, duration, extra)

    # ------------------------------------------------------------------
    # 2️⃣  Shell execution
    # ------------------------------------------------------------------
    async def exec_shell(
        self,
        command: str = "",
        text: str = "",
        session_id: str = "",
        **kwargs,
    ) -> str:
        start = time.time()

        if not self.valves.allow_shell:
            return self.formatter.format_error(
                "Disabled",
                "Shell execution has been turned off in the configuration",
            )

        cmd = _coerce_to_string(command) or _coerce_to_string(text)
        if not cmd.strip():
            return self.formatter.format_error("Input Error", "No command supplied")

        # *** OPTIONAL shell‑pattern blocking ***
        if self.valves.enable_shell_blocking:
            for pat in self._blocked_shell_patterns():
                if pat.search(cmd):
                    return self.formatter.format_error(
                        "Security",
                        f"Command matches a blocked pattern: {pat.pattern}",
                        "Rewrite the command without the dangerous pattern",
                    )

        session = self.session_mgr.get_or_create(session_id or None)

        exit_code, stdout, stderr = await _run_subprocess(
            cmd,
            cwd=session.temp_dir,
            timeout=self.valves.max_execution_time,
            is_shell=True,
        )
        duration = time.time() - start
        session.add_history("exec_shell", cmd[:100], exit_code == 0, duration)

        extra = {"Session": session.session_id, "CWD": session.temp_dir}
        return self.formatter.format_result(exit_code, stdout, stderr, duration, extra)

    # ------------------------------------------------------------------
    # 3️⃣  Lint / syntax checking (uses py_compile & optional ruff)
    # ------------------------------------------------------------------
    async def exec_lint(self, code: str = "", text: str = "", **kwargs) -> str:
        start = time.time()
        py_code = self._extract_code(code, text, **kwargs)

        if not py_code:
            return self.formatter.format_error("Input Error", "No code to lint")

        temp_dir = tempfile.mkdtemp(prefix="lint_")
        try:
            src_path = Path(temp_dir) / "tmp.py"
            src_path.write_text(py_code, encoding="utf-8")

            # Syntax check via py_compile
            exit_code, _, stderr = await _run_subprocess(
                [self.valves.python_cmd, "-m", "py_compile", str(src_path)],
                cwd=temp_dir,
                timeout=8,
            )

            msgs = []
            if exit_code == 0:
                msgs.append("✅ **Syntax:** OK")
            else:
                msgs.append(f"❌ **Syntax error:**\n```\n{stderr}\n```")

            # Optional ruff style check (if installed)
            rc, _, _ = await _run_subprocess(
                "command -v ruff", cwd=temp_dir, timeout=3, is_shell=True
            )
            if rc == 0:
                rc, out, err = await _run_subprocess(
                    ["ruff", "check", "--output-format=text", str(src_path)],
                    cwd=temp_dir,
                    timeout=12,
                )
                if rc == 0:
                    msgs.append("✅ **Ruff:** No issues")
                else:
                    truncated, _ = _truncate_output(out or err, 30, 2000)
                    msgs.append(f"⚠️ **Ruff issues:**\n```\n{truncated}\n```")
            else:
                msgs.append("ℹ️ **Ruff:** Not installed – style check skipped")

            duration = time.time() - start
            msgs.append(f"\n⏱️ **Duration:** {duration:.2f}s")
            msgs.append("\n---\n**💡 SYNTHESIS:** Review the diagnostics and apply fixes.")
            return "\n\n".join(msgs)
        finally:
            shutil.rmtree(temp_dir, ignore_errors=True)

    # ------------------------------------------------------------------
    # 4️⃣  Dependency checker (imports → availability)
    # ------------------------------------------------------------------
    async def exec_check_deps(self, code: str = "", text: str = "", **kwargs) -> str:
        start = time.time()
        py_code = self._extract_code(code, text, **kwargs)

        if not py_code:
            return self.formatter.format_error("Input Error", "No code supplied")

        # Parse imports with AST
        imports: set[str] = set()
        try:
            tree = ast.parse(py_code)
            for node in ast.walk(tree):
                if isinstance(node, ast.Import):
                    for alias in node.names:
                        imports.add(alias.name.split(".")[0])
                elif isinstance(node, ast.ImportFrom) and node.module:
                    imports.add(node.module.split(".")[0])
        except SyntaxError as e:
            return self.formatter.format_error(
                "Syntax Error", f"Cannot parse imports: {e}", "Run exec_lint first"
            )

        if not imports:
            return "ℹ️ No `import` statements found in the supplied code."

        lines = ["📦 **Dependency check**"]
        available, missing = [], []

        for mod in sorted(imports):
            check_script = f"""
import sys, importlib
try:
    m = importlib.import_module("{mod}")
    v = getattr(m, "__version__", getattr(m, "VERSION", "unknown"))
    print("OK|%s" % v)
except Exception as e:
    print("FAIL|%s" % e)
"""
            rc, out, _ = await _run_subprocess(
                [self.valves.python_cmd, "-c", check_script], cwd=".", timeout=5
            )
            out = out.strip()
            if out.startswith("OK|"):
                version = out.split("|", 1)[1]
                available.append(f"✅ `{mod}` (v{version})")
            else:
                err = out.split("|", 1)[1] if "|" in out else "not found"
                missing.append(f"❌ `{mod}` – {err}")

        if available:
            lines.append("**Available:**")
            lines.extend(available)

        if missing:
            lines.append("\n**Missing:**")
            lines.extend(missing)
            # hint for pip install
            pkgs = " ".join([m.split("`")[1] for m in missing])
            lines.append(f"\n💡 Install missing: `pip install {pkgs}`")

        duration = time.time() - start
        lines.append(f"\n⏱️ **Checked {len(imports)} packages in {duration:.2f}s")
        lines.append("\n---\n**💡 SYNTHESIS:** List what can be installed.")
        return "\n".join(lines)

    # ------------------------------------------------------------------
    # 5️⃣  pip‑install helper (still optional)
    # ------------------------------------------------------------------
    async def exec_pip_install(self, packages: str = "", text: str = "", **kwargs) -> str:
        if not self.valves.allow_pip_install:
            return self.formatter.format_error(
                "Disabled", "Package installation is turned off in config"
            )

        raw = _coerce_to_string(packages) or _coerce_to_string(text)
        if not raw.strip():
            return self.formatter.format_error("Input Error", "No packages provided")

        # split on commas or whitespace
        pkg_list = [p.strip() for p in re.split(r"[,\s]+", raw) if p.strip()]
        # very light validation – just characters typical for a PyPI name
        bad = [p for p in pkg_list if not re.fullmatch(r"[a-zA-Z0-9_\-\.]+", p)]
        if bad:
            return self.formatter.format_error(
                "Invalid package names", f"Bad tokens: {', '.join(bad)}"
            )

        start = time.time()
        cmd = [
            self.valves.python_cmd,
            "-m",
            "pip",
            "install",
            "--user",
            "--quiet",
        ] + pkg_list

        rc, out, err = await _run_subprocess(
            cmd, cwd=".", timeout=min(60, self.valves.max_execution_time * 2)
        )
        duration = time.time() - start

        if rc == 0:
            return self.formatter.format_result(
                rc,
                f"✅ Successfully installed: {', '.join(pkg_list)}",
                err,
                duration,
                {"Packages": len(pkg_list)},
            )
        else:
            return self.formatter.format_result(rc, out, err, duration, None)

    # ------------------------------------------------------------------
    # 6️⃣  File helpers (write / read / list)
    # ------------------------------------------------------------------
    async def exec_write_file(
        self,
        filename: str = "",
        content: str = "",
        text: str = "",
        session_id: str = "",
        **kwargs,
    ) -> str:
        if not self.valves.allow_file_persistence:
            return self.formatter.format_error(
                "Disabled", "File persistence is turned off in configuration"
            )

        name = _coerce_to_string(filename) or _coerce_to_string(kwargs.get("name", ""))
        data = _coerce_to_string(content) or _coerce_to_string(text)

        if not name:
            return self.formatter.format_error("Input Error", "No filename supplied")
        if not data:
            return self.formatter.format_error("Input Error", "No file content supplied")

        session = self.session_mgr.get_or_create(session_id or None)

        try:
            saved = session.add_file(name, data)
            info = {
                "Filename": saved,
                "Size": f"{len(data)} chars / {len(data.encode())} bytes",
                "Session": session.session_id,
                "Files in session": len(session.files),
            }
            return self.formatter.format_info("File written", info)
        except ValueError as e:
            return self.formatter.format_error("File error", str(e))

    async def exec_read_file(
        self,
        filename: str = "",
        text: str = "",
        session_id: str = "",
        **kwargs,
    ) -> str:
        name = _coerce_to_string(filename) or _coerce_to_string(text)
        if not name:
            return self.formatter.format_error("Input Error", "No filename supplied")

        session = self.session_mgr.get_or_create(session_id or None)
        payload = session.get_file(name)

        if payload is None:
            avail = session.list_files()
            return self.formatter.format_error(
                "File not found",
                f"`{name}` does not exist in session `{session.session_id}`",
                f"Available files: {', '.join(avail) or 'none'}",
            )

        out, truncated = _truncate_output(
            payload, self.valves.max_output_lines, self.valves.max_output_chars
        )
        result = [f"📄 **File:** {name}\n```\n{out}\n```"]
        if truncated:
            result.append("⚠️ Output truncated")
        return "\n".join(result)

    async def exec_list_files(self, session_id: str = "", **kwargs) -> str:
        session = self.session_mgr.get_or_create(session_id or None)
        files = session.list_files()
        if not files:
            return f"ℹ️ No files in session `{session.session_id}`"

        file_info = []
        for f in sorted(files):
            path = session.files[f]
            size = os.path.getsize(path) if os.path.exists(path) else 0
            file_info.append(f"- `{f}` ({size} bytes)")

        info = {
            "Session": session.session_id,
            "Files": file_info,
            "Total": len(files),
            "Working directory": session.temp_dir,
        }
        return self.formatter.format_info("Session files", info)

    # ------------------------------------------------------------------
    # 7️⃣  Session and environment introspection
    # ------------------------------------------------------------------
    async def exec_session_info(self, session_id: str = "", **kwargs) -> str:
        session = self.session_mgr.get_or_create(session_id or None)
        age = (time.time() - session.created_at) / 60
        idle = (time.time() - session.last_accessed) / 60

        info = {
            "Session ID": session.session_id,
            "Created": f"{age:.1f} min ago",
            "Last active": f"{idle:.1f} min ago",
            "Executions": session.execution_count,
            "Files stored": len(session.files),
            "Working directory": session.temp_dir,
        }

        if session.history:
            recent = session.history[-5:]
            info["Recent history"] = [
                f"{h['tool']}: {'✅' if h['success'] else '❌'} ({h['duration']} s)"
                for h in recent
            ]

        return self.formatter.format_info("Session info", info)

    async def exec_env_info(self, **kwargs) -> str:
        # Basic python version
        rc, out, _ = await _run_subprocess(
            [self.valves.python_cmd, "--version"], cwd=".", timeout=5
        )
        python_ver = out.strip() if rc == 0 else "unknown"

        # pip version
        rc, out, _ = await _run_subprocess(
            [self.valves.python_cmd, "-m", "pip", "--version"], cwd=".", timeout=5
        )
        pip_ver = out.split()[1] if rc == 0 else "unknown"

        # Detect common helper tools (ruff, black, git, node)
        tool_names = ["ruff", "black", "git", "node", "npm"]
        available = []
        for t in tool_names:
            rc, _, _ = await _run_subprocess(f"command -v {t}", cwd=".", timeout=3, is_shell=True)
            if rc == 0:
                available.append(t)

        info = {
            "Python": python_ver,
            "Pip": pip_ver,
            "Available tools": available or ["none detected"],
            "Config": {
                "Max exec time": f"{self.valves.max_execution_time}s",
                "Shell enabled": self.valves.allow_shell,
                "Pip install enabled": self.valves.allow_pip_install,
                "File persistence": self.valves.allow_file_persistence,
            },
            "Active sessions": len(self.session_mgr.sessions),
        }

        return self.formatter.format_info("Environment info", info)

# ----------------------------------------------------------------------
# End of module – drop this file into your Open WebUI plugins directory
# ----------------------------------------------------------------------
