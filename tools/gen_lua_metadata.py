#!/usr/bin/env python3
"""Generate LuaLS metadata from C++ Lua binding code.

The script scans C++ source files for common Lua C API patterns like:
- lua_setglobal(L, "Name")
- lua_setfield(L, tableRef, "member")
- lua_newtable(L); int tableRef = lua_gettop(L);

It produces a Lua meta file used by Lua Language Server so scene scripts can use
engine globals without repetitive manual imports/diagnostic suppressions.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass, field
from pathlib import Path


RE_NEW_TABLE = re.compile(r"\blua_newtable\s*\(\s*L_?\s*\)\s*;")
RE_GETTOP_ASSIGN = re.compile(r"\b(?:const\s+)?int\s+([A-Za-z_]\w*)\s*=\s*lua_gettop\s*\(\s*L_?\s*\)\s*;")
RE_SETFIELD = re.compile(r"\blua_setfield\s*\(\s*L_?\s*,\s*([\-A-Za-z_]\w*|\-2)\s*,\s*\"([^\"]+)\"\s*\)\s*;")
RE_SETGLOBAL = re.compile(r"\blua_setglobal\s*\(\s*L_?\s*,\s*\"([^\"]+)\"\s*\)\s*;")
RE_LAMBDA_START = re.compile(r"\[[^\]]*\]\s*\([^)]*\)\s*(?:mutable\s*)?(?:->\s*[^\{]+)?\{")
RE_REGISTER_CALL = re.compile(r"\b(register[A-Za-z0-9_]+)\s*\(\s*\"([^\"]+)\"")

REGISTER_FN_GLOBAL_MAP = {
    "registerPlayAudioFn": "Gameplay",
    "registerPreloadAudioFn": "Gameplay",
    "registerAudioLoadFn": "Audio",
    "registerAudioPlayFn": "Audio",
}


@dataclass
class TableContext:
    created_line: int
    fields: set[str] = field(default_factory=set)
    var_name: str | None = None
    last_use_line: int = -1
    consumed: bool = False


def source_files(source_root: Path) -> list[Path]:
    files = [p for p in source_root.rglob("*") if p.suffix in {".cpp", ".hpp"}]
    files.sort(key=lambda p: p.as_posix())
    return files


def choose_context_for_global(contexts: list[TableContext], last_global_line: int, line_idx: int) -> TableContext | None:
    candidates = [
        ctx
        for ctx in contexts
        if not ctx.consumed
        and ctx.fields
        and ctx.last_use_line >= last_global_line
        and ctx.last_use_line <= line_idx
        and (line_idx - ctx.last_use_line) <= 40
    ]
    if not candidates:
        return None
    candidates.sort(key=lambda c: (c.last_use_line, c.created_line), reverse=True)
    return candidates[0]


def parse_bindings(files: list[Path]) -> tuple[set[str], dict[str, set[str]]]:
    globals_found: set[str] = set()
    table_fields: dict[str, set[str]] = {}

    contexts: list[TableContext] = []
    var_to_context: dict[str, TableContext] = {}
    last_global_line = -1
    line_counter = 0
    lambda_brace_depth = 0

    for file_path in files:
        contexts = []
        var_to_context = {}
        last_global_line = -1
        brace_depth = 0

        content = file_path.read_text(encoding="utf-8", errors="ignore")
        for raw_line in content.splitlines():
            line_counter += 1
            line = raw_line.strip()

            if lambda_brace_depth == 0 and RE_LAMBDA_START.search(line):
                lambda_brace_depth += line.count("{") - line.count("}")
                if lambda_brace_depth < 0:
                    lambda_brace_depth = 0
                continue

            if lambda_brace_depth > 0:
                lambda_brace_depth += line.count("{") - line.count("}")
                if lambda_brace_depth < 0:
                    lambda_brace_depth = 0
                continue

            # Reset local table-tracking contexts at likely function starts so
            # tables built in one function do not leak into another.
            if (
                brace_depth <= 1
                and "{" in line
                and "(" in line
                and ")" in line
                and not line.endswith(";")
            ):
                contexts = []
                var_to_context = {}
                last_global_line = -1

            if RE_NEW_TABLE.search(line):
                contexts.append(TableContext(created_line=line_counter, last_use_line=line_counter))

            m_gettop = RE_GETTOP_ASSIGN.search(line)
            if m_gettop:
                var_name = m_gettop.group(1)
                for ctx in reversed(contexts):
                    if ctx.var_name is None and not ctx.consumed:
                        ctx.var_name = var_name
                        var_to_context[var_name] = ctx
                        break

            m_setfield = RE_SETFIELD.search(line)
            if m_setfield:
                target = m_setfield.group(1)
                field_name = m_setfield.group(2)
                target_ctx: TableContext | None = None

                if target == "-2":
                    for ctx in reversed(contexts):
                        if not ctx.consumed:
                            target_ctx = ctx
                            break
                else:
                    target_ctx = var_to_context.get(target)

                if target_ctx is not None:
                    target_ctx.fields.add(field_name)
                    target_ctx.last_use_line = line_counter

            m_register_call = RE_REGISTER_CALL.search(line)
            if m_register_call:
                register_fn = m_register_call.group(1)
                field_name = m_register_call.group(2)
                target_global = REGISTER_FN_GLOBAL_MAP.get(register_fn)
                if target_global:
                    table_fields.setdefault(target_global, set()).add(field_name)
                    globals_found.add(target_global)

            m_setglobal = RE_SETGLOBAL.search(line)
            if m_setglobal:
                global_name = m_setglobal.group(1)
                globals_found.add(global_name)

                ctx = choose_context_for_global(contexts, last_global_line, line_counter)
                if ctx is not None:
                    table_fields.setdefault(global_name, set()).update(ctx.fields)
                    ctx.consumed = True

                last_global_line = line_counter

            brace_depth += line.count("{") - line.count("}")
            if brace_depth < 0:
                brace_depth = 0

    return globals_found, table_fields


def render_metadata(globals_found: set[str], table_fields: dict[str, set[str]]) -> str:
    ordered_globals = sorted(globals_found)

    lines: list[str] = [
        "---@meta",
        "",
        "-- AUTO-GENERATED FILE. DO NOT EDIT.",
        "-- Generated by tools/gen_lua_metadata.py from C++ Lua bindings.",
        "",
    ]

    for global_name in ordered_globals:
        fields = sorted(table_fields.get(global_name, set()))
        if fields:
            lines.append(f"---@class NS.{global_name}")
            lines.append(f"{global_name} = {{}}")
            for member in fields:
                lines.append(f"---@type any")
                lines.append(f"{global_name}.{member} = nil")
            lines.append("")
        else:
            lines.append("---@type any")
            lines.append(f"{global_name} = nil")
            lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate LuaLS metadata from C++ Lua bindings.")
    parser.add_argument("--source-root", required=True, help="Path to source tree to scan (e.g. src).")
    parser.add_argument("--output", required=True, help="Output Lua metadata path.")
    args = parser.parse_args()

    source_root = Path(args.source_root).resolve()
    output_path = Path(args.output).resolve()

    files = source_files(source_root)
    globals_found, table_fields = parse_bindings(files)
    metadata = render_metadata(globals_found, table_fields)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(metadata, encoding="utf-8")

    print(f"Generated {output_path} from {len(files)} source files.")
    print(f"Discovered {len(globals_found)} globals.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
