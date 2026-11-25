#!/usr/bin/env python3
"""Fail if an exported ``extern "C"`` function in ``ffi/capi`` has no panic boundary.

Every ``#[no_mangle]`` ``extern "C"`` function is an FFI entry point: a Rust
panic that unwinds through it is undefined behaviour. Each one must route its
body through one of the ``ServoError::catch_ffi*`` helpers, which catch the
panic and translate it into a C error code, ``NULL``, ``0`` or ``false``.

The check is intentionally syntactic: it locates the function body and looks
for a ``catch_ffi`` call. It does not try to prove that the call dominates
every panicking operation.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CAPI = ROOT / "ffi" / "capi"

# ``#[unsafe(no_mangle)]`` (Rust 2024) or ``#[no_mangle]`` (older editions).
ATTR = r"#\[(?:unsafe\s*\(\s*)?no_mangle\s*\)?\]"
FN = re.compile(ATTR + r'[^;{]*?extern\s+"C"\s+fn\s+([A-Za-z0-9_]+)')


def body_span(src: str, start: int) -> tuple[int, int] | None:
    """Return the ``{...}`` span of the first block after *start*."""
    open_i = src.find("{", start)
    if open_i == -1:
        return None
    depth = 0
    for i in range(open_i, len(src)):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return open_i, i
    return None


def main() -> int:
    missing: list[tuple[str, str, str]] = []
    count = 0
    for path in sorted(CAPI.glob("*.rs")):
        src = path.read_text(encoding="utf-8", errors="replace")
        for match in FN.finditer(src):
            count += 1
            span = body_span(src, match.end())
            if span is None:
                missing.append((path.name, match.group(1), "no body found"))
                continue
            open_i, close_i = span
            if "catch_ffi" not in src[open_i:close_i]:
                line = src[: match.start()].count("\n") + 1
                missing.append((path.name, match.group(1), f"line {line}"))

    if missing:
        print('::error::Unwrapped extern "C" functions (a panic can cross the FFI boundary):')
        for file_name, fn_name, where in missing:
            print(f"  {file_name}: {fn_name} ({where})")
        print(f'\n{len(missing)} of {count} exported extern "C" functions are not wrapped.')
        return 1

    print(f'All {count} exported extern "C" functions are wrapped in catch_ffi*.')
    return 0


if __name__ == "__main__":
    sys.exit(main())
