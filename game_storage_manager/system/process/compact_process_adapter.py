from __future__ import annotations

import subprocess
import threading
from dataclasses import dataclass, field
from typing import Callable

from game_storage_manager.core.rules_engine.recommendation_engine import CompressionAlgorithm, algorithm_to_string
from game_storage_manager.system.filesystem.filesystem import normalize_path


@dataclass
class CompactCommand:
    executable: str = "compact.exe"
    arguments: list[str] = field(default_factory=list)


@dataclass
class ProcessResult:
    exit_code: int = -1
    output: str = ""


@dataclass
class CompactOutputMetrics:
    parsed: bool = False
    bytes_before_from_output: int = 0
    bytes_after_from_output: int = 0
    files_processed: int = 0
    files_already_compressed: int = 0
    files_compressed: int = 0


def _quote_for_display(value: str) -> str:
    if not any(c in value for c in (" ", "\t", '"')):
        return value
    escaped = value.replace('"', '\\"')
    return f'"{escaped}"'


def to_display_string(command: CompactCommand) -> str:
    parts = [_quote_for_display(command.executable)]
    for arg in command.arguments:
        parts.append(_quote_for_display(arg))
    return " ".join(parts)


class CompactProcessAdapter:
    def build_compress_command(self, target_path: str, algorithm: CompressionAlgorithm) -> CompactCommand:
        return CompactCommand(
            arguments=[
                "/c",
                "/s",
                "/a",
                "/i",
                f"/exe:{algorithm_to_string(algorithm)}",
                normalize_path(target_path) + "\\*",
            ]
        )

    def build_restore_command(self, target_path: str) -> CompactCommand:
        return CompactCommand(
            arguments=[
                "/u",
                "/s",
                normalize_path(target_path) + "\\*",
            ]
        )

    def run(
        self,
        command: CompactCommand,
        on_output: Callable[[str], None] | None = None,
        cancel_flag: threading.Event | None = None,
    ) -> ProcessResult:
        result = ProcessResult()

        cmd_args = [command.executable] + command.arguments
        full_cmd = 'cmd.exe /c "' + " ".join(f'"{a}"' for a in cmd_args) + ' 2>&1"'

        try:
            process = subprocess.Popen(
                full_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
        except OSError as e:
            result.output = f"Failed to start process: {e}"
            return result

        output_lines: list[str] = []

        try:
            for line in process.stdout:
                if cancel_flag and cancel_flag.is_set():
                    process.terminate()
                    output_lines.append("\n[Operation cancelled by user]\n")
                    result.exit_code = 1
                    break
                output_lines.append(line)
                if on_output:
                    on_output(line)
        except Exception:
            pass

        if result.exit_code == -1:
            process.wait()
            result.exit_code = process.returncode or 0

        result.output = "".join(output_lines)
        return result

    @staticmethod
    def parse_compress_output(output: str) -> CompactOutputMetrics:
        return CompactOutputMetrics(parsed=False)
