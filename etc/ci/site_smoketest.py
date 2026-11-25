#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""
Real-World Website Smoke Test Harness.

Executes Servo on a curated list of representative websites, measuring:
- Load success / HTTP completion
- Script execution and console errors
- Renderer crashes, thread panics, and timeouts
- Failure root-cause categorization (DNS/TLS, DOM/JS, CSS/Layout, WebRender)
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from typing import List, Optional, TypedDict


class TargetSite(TypedDict):
    name: str
    url: str
    category: str
    expected_features: list[str]


DEFAULT_TARGET_SITES: list[TargetSite] = [
    {
        "name": "Example Domain",
        "url": "https://example.com",
        "category": "Basic HTML5 / Static Document",
        "expected_features": ["HTML parser", "basic CSS", "HTTP/2 or HTTP/1.1"],
    },
    {
        "name": "Hacker News",
        "url": "https://news.ycombinator.com",
        "category": "Table Layout / Lightweight Dynamic",
        "expected_features": ["Cookie storage", "Table formatting", "inline CSS"],
    },
    {
        "name": "Wikipedia Main Page",
        "url": "https://en.wikipedia.org/wiki/Main_Page",
        "category": "Complex Content / Media Queries / SVG",
        "expected_features": ["Responsive layouts", "Unicode fonts", "SVG rendering"],
    },
    {
        "name": "DuckDuckGo Lite",
        "url": "https://lite.duckduckgo.com",
        "category": "Forms / Query Parameters",
        "expected_features": ["Form submission", "HTTP GET/POST", "URL encoding"],
    },
    {
        "name": "Httpbin JSON",
        "url": "https://httpbin.org/get",
        "category": "Network / Headers / HTTPS",
        "expected_features": ["TLS handshake", "Accept headers", "JSON display"],
    },
]


@dataclass
class SiteTestResult:
    name: str
    url: str
    category: str
    exit_code: Optional[int]
    status: str  # PASS, TIMEOUT, CRASH, ERROR, NOT_RUN
    elapsed_seconds: float
    root_cause: Optional[str]
    error_summary: Optional[str]
    screenshot_path: Optional[str] = None


def site_slug(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def exit_code_for(results: List[SiteTestResult]) -> int:
    """Return 0 only when every site passed. A clean harness exit is not a site pass."""
    return 0 if results and all(r.status == "PASS" for r in results) else 1


def run_site_test(
    servo_binary: str,
    site: TargetSite,
    timeout_seconds: int = 15,
    extra_args: Optional[List[str]] = None,
) -> SiteTestResult:
    # -f = hard-fail on thread failure; -x = exit after load; --headless avoids a window.
    # Without -x the browser stays open and every site looks like a TIMEOUT.
    args = [servo_binary, "-f", "-x", "--headless"]
    if extra_args:
        args.extend(extra_args)
    args.append(site["url"])

    start_time = time.monotonic()
    status = "UNKNOWN"
    root_cause = None
    error_summary = None
    exit_code = None

    try:
        proc = subprocess.run(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout_seconds,
        )
        elapsed = time.monotonic() - start_time
        exit_code = proc.returncode

        combined_output = f"{proc.stdout}\n{proc.stderr}"

        if exit_code == 0:
            status = "PASS"
        else:
            status = "ERROR"
            # Categorize failure root cause
            if "panicked at" in combined_output:
                status = "CRASH"
                root_cause = "Rust thread panic"
                for line in combined_output.splitlines():
                    if "panicked at" in line:
                        error_summary = line.strip()
                        break
            elif "certificate" in combined_output.lower() or "tls" in combined_output.lower():
                root_cause = "TLS / Certificate negotiation error"
                error_summary = "TLS failure encountered"
            elif "dns" in combined_output.lower() or "resolve" in combined_output.lower():
                root_cause = "DNS resolution failure"
                error_summary = "Host lookup failed"
            else:
                root_cause = f"Non-zero exit code ({exit_code})"
                error_summary = proc.stderr.splitlines()[-1] if proc.stderr.splitlines() else "Unknown error"

    except subprocess.TimeoutExpired:
        elapsed = time.monotonic() - start_time
        status = "TIMEOUT"
        root_cause = f"Timed out after {timeout_seconds}s (potential hang or slow resource)"
        error_summary = "Execution timed out"
    except Exception as e:
        elapsed = time.monotonic() - start_time
        status = "CRASH"
        root_cause = f"Harness execution error: {type(e).__name__}"
        error_summary = str(e)

    return SiteTestResult(
        name=site["name"],
        url=site["url"],
        category=site["category"],
        exit_code=exit_code,
        status=status,
        elapsed_seconds=round(elapsed, 2),
        root_cause=root_cause,
        error_summary=error_summary,
        screenshot_path=None,
    )


def generate_site_report(results: List[SiteTestResult], header: str = "") -> str:
    lines = [
        "# Real-World Website Conformance Smoke Report",
        "",
    ]
    if header:
        lines.extend([header, ""])
    lines.extend(
        [
            "| Site | Category | Status | Time | Screenshot | Root Cause |",
            "|---|---|---|---:|---|---|",
        ]
    )
    for r in results:
        status_badge = f"**{r.status}**" if r.status == "PASS" else f"`{r.status}`"
        cause = r.root_cause or "Clean exit"
        shot = f"`{r.screenshot_path}`" if r.screenshot_path else "—"
        lines.append(
            f"| [{r.name}]({r.url}) | {r.category} | {status_badge} | {r.elapsed_seconds}s | {shot} | {cause} |"
        )

    lines.append("")
    lines.append("## Root Cause Triage & Findings")
    lines.append("")
    failures = [r for r in results if r.status != "PASS"]
    if not failures:
        lines.append("All target smoke sites completed without crashing or timing out.")
    else:
        for f in failures:
            lines.append(f"### {f.name} ({f.status})")
            lines.append(f"- **URL:** `{f.url}`")
            lines.append(f"- **Category:** {f.category}")
            lines.append(f"- **Root Cause:** {f.root_cause}")
            if f.screenshot_path:
                lines.append(f"- **Screenshot:** `{f.screenshot_path}`")
            if f.error_summary:
                lines.append(f"- **Details:** `{f.error_summary}`")
            lines.append("")

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Servo Real-World Website Smoke Test")
    parser.add_argument("--servo-binary", help="Path to servoshell binary", default=None)
    parser.add_argument("--timeout", type=int, default=15, help="Timeout per site in seconds")
    parser.add_argument(
        "--screenshot-dir",
        help="Directory for per-site screenshots; visual targets (#2-#5) are NOT_CONFIRMED without one",
    )
    parser.add_argument("--output-json", help="Path to write JSON results")
    parser.add_argument("--output-markdown", help="Path to write Markdown summary")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Emit an explicit NOT_RUN plan; never treat this as test evidence",
    )
    args = parser.parse_args()

    # If no binary is passed, search common target paths
    binary = args.servo_binary
    if not binary:
        candidates = [
            os.path.join("target", "release", "servoshell"),
            os.path.join("target", "release", "servoshell.exe"),
            os.path.join("target", "debug", "servoshell"),
            os.path.join("target", "debug", "servoshell.exe"),
            "C:\\servo_target\\release\\servoshell.exe",
            "C:\\servo_target\\debug\\servoshell.exe",
        ]
        for c in candidates:
            if os.path.exists(c):
                binary = c
                break

    if args.dry_run:
        # A plan is not a result. It is only written when explicitly requested,
        # and every row is labelled NOT_RUN so it cannot be mistaken for evidence.
        planned = [
            SiteTestResult(
                name=s["name"],
                url=s["url"],
                category=s["category"],
                exit_code=None,
                status="NOT_RUN",
                elapsed_seconds=0.0,
                root_cause="Dry-run plan only; no binary was executed",
                error_summary=None,
            )
            for s in DEFAULT_TARGET_SITES
        ]
        report = generate_site_report(
            planned,
            "> **NOT_RUN plan.** No servoshell binary was executed. This is not test evidence. "
            "See [SITE_SMOKE_RUNBOOK.md](SITE_SMOKE_RUNBOOK.md) for the execution and triage procedure.",
        )
        if args.output_markdown:
            with open(args.output_markdown, "w", encoding="utf-8") as fh:
                fh.write(report)
            print(f"Wrote NOT_RUN plan to {args.output_markdown}")
        else:
            print(report)
        if args.output_json:
            with open(args.output_json, "w", encoding="utf-8") as fh:
                json.dump([asdict(r) for r in planned], fh, indent=2)
        return 0

    if not binary or not os.path.exists(binary):
        # Never silently generate a report that could be read as real results.
        print(
            "ERROR: no servoshell binary found. Pass --servo-binary with a binary "
            "extracted from a successful CI artifact, or use --dry-run to write an "
            "explicit NOT_RUN plan.",
            file=sys.stderr,
        )
        return 2

    print(f"Running site smoke tests using binary: {binary}")
    run_header = (
        f"> **Executed smoke run.** Binary: `{binary}`. Record the source CI run id "
        "and artifact hash next to this report before treating it as evidence."
    )
    screenshot_dir = args.screenshot_dir
    if screenshot_dir:
        os.makedirs(screenshot_dir, exist_ok=True)

    results = []
    for index, site in enumerate(DEFAULT_TARGET_SITES):
        print(f"Testing {site['name']} ({site['url']})...")
        extra_args: List[str] = []
        screenshot_path = None
        if screenshot_dir:
            screenshot_path = os.path.join(screenshot_dir, f"{index + 1:02d}-{site_slug(site['name'])}.png")
            extra_args.extend(["-o", screenshot_path])

        res = run_site_test(binary, site, timeout_seconds=args.timeout, extra_args=extra_args)
        res.screenshot_path = screenshot_path

        # Runbook: visual targets #2-#5 need screenshot evidence, not just a clean exit.
        if index >= 1 and (not screenshot_path or not os.path.isfile(screenshot_path)):
            if res.status == "PASS":
                res.status = "NOT_CONFIRMED"
                res.root_cause = "No screenshot evidence for visual target"
                res.error_summary = f"Expected screenshot at {screenshot_path}"

        results.append(res)
        print(f"  -> {res.status} in {res.elapsed_seconds}s")

    report = generate_site_report(results, run_header)
    if args.output_markdown:
        with open(args.output_markdown, "w", encoding="utf-8") as fh:
            fh.write(report)
        print(f"Wrote smoke report to {args.output_markdown}")
    else:
        print(report)

    if args.output_json:
        with open(args.output_json, "w", encoding="utf-8") as fh:
            json.dump([asdict(r) for r in results], fh, indent=2)
        print(f"Wrote JSON report to {args.output_json}")

    # A non-PASS row must fail the harness. Exit 0 is necessary but not
    # sufficient for REL-3 (visual targets also require screenshots).
    return exit_code_for(results)


if __name__ == "__main__":
    sys.exit(main())
