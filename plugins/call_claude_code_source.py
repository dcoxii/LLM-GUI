"""
title: Claude Code CLI
author: cole
description: >
  Lets any Open WebUI model invoke Claude Code CLI (claude-opus-4-6 / claude-sonnet-4-6)
  non-interactively. Models can delegate agentic coding tasks, file operations, shell
  commands, or codebase analysis to Claude Code and receive the result inline.
  Runs in print mode — no persistent session.
version: 1.0.0
licence: MIT
"""

import os
import subprocess
from pydantic import BaseModel, Field


class Tools:
    class Valves(BaseModel):
        claude_path: str = Field(
            default="/opt/homebrew/bin/claude",
            description="Absolute path to the claude binary.",
        )
        model: str = Field(
            default="",
            description=(
                "Override the Claude model. Examples: sonnet, opus, haiku, "
                "claude-sonnet-4-6, claude-opus-4-6. "
                "Leave blank to use the default."
            ),
        )
        permission_mode: str = Field(
            default="bypassPermissions",
            description=(
                "Permission mode for Claude Code. "
                "Options: bypassPermissions | acceptEdits | default | plan"
            ),
        )
        timeout_seconds: int = Field(
            default=600,
            description="Max seconds to wait for Claude to respond before timing out.",
        )
        max_turns: int = Field(
            default=0,
            description=(
                "Max agentic turns (tool-use round-trips). "
                "0 = unlimited (Claude decides when to stop)."
            ),
        )
        allowed_tools: str = Field(
            default="",
            description=(
                "Restrict which tools Claude can use, space-separated. "
                'E.g. "Bash Edit Read Glob Grep". '
                "Leave blank for all tools."
            ),
        )
        default_workdir: str = Field(
            default="",
            description=(
                "Default working directory for Claude Code. "
                "Leave blank to use the Open WebUI server's cwd."
            ),
        )

    def __init__(self):
        self.valves = self.Valves()

    def run_claude_task(self, prompt: str, working_directory: str = "") -> str:
        """
        Send a task or coding instruction to Claude Code CLI and return its response.
        Use this when you need to: write or modify code, run shell commands, analyse
        a local codebase, create files, build and publish Open WebUI artifacts,
        or perform any agentic coding task.

        :param prompt: The instruction or task for Claude Code to carry out.
        :param working_directory: Optional absolute path for Claude Code to treat as
                                  its workspace root. Leave empty to use the server default.
        :return: Claude Code's response text, or an error message.
        """
        workdir = working_directory.strip() or self.valves.default_workdir.strip()

        cmd = [
            self.valves.claude_path,
            "-p",
            "--output-format", "text",
        ]

        perm = (self.valves.permission_mode or "bypassPermissions").strip()
        if perm == "bypassPermissions":
            cmd.append("--dangerously-skip-permissions")
        else:
            cmd += ["--permission-mode", perm]

        if self.valves.model:
            cmd += ["--model", self.valves.model]

        if self.valves.max_turns > 0:
            cmd += ["--max-turns", str(self.valves.max_turns)]

        if self.valves.allowed_tools.strip():
            cmd += ["--allowedTools"] + self.valves.allowed_tools.strip().split()

        cmd.append(prompt)

        env = os.environ.copy()
        path_prefix = "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin"
        existing_path = env.get("PATH", "")
        env["PATH"] = f"{path_prefix}:{existing_path}" if existing_path else path_prefix

        run_kwargs = dict(
            capture_output=True,
            text=True,
            timeout=self.valves.timeout_seconds,
            env=env,
        )
        if workdir:
            run_kwargs["cwd"] = workdir

        try:
            result = subprocess.run(cmd, **run_kwargs)
        except subprocess.TimeoutExpired:
            return (
                f"[claude_tool] Timed out after {self.valves.timeout_seconds}s. "
                "Try a simpler task or increase the timeout valve."
            )
        except FileNotFoundError:
            return (
                f"[claude_tool] Claude binary not found at '{self.valves.claude_path}'. "
                "Check the Valves setting."
            )
        except Exception as exc:
            return f"[claude_tool] Unexpected error: {exc}"

        output = (result.stdout or "").strip()
        if result.returncode != 0:
            stderr_text = (result.stderr or "").strip()
            if stderr_text:
                output = (output + "\n" + stderr_text).strip() if output else stderr_text

        if not output:
            return f"(Claude Code returned no output. Exit code: {result.returncode})"

        return output

    def claude_version(self) -> str:
        """
        Return the installed Claude Code CLI version string. Useful for confirming
        the tool is wired up correctly before using it for real tasks.

        :return: Version string or error message.
        """
        try:
            result = subprocess.run(
                [self.valves.claude_path, "--version"],
                capture_output=True,
                text=True,
                timeout=10,
            )
            return result.stdout.strip() or result.stderr.strip()
        except Exception as exc:
            return f"[claude_tool] Error: {exc}"
