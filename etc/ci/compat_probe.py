#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Deeper in-page compatibility probe for the bounded `compat-v1` site set.

The site smoke harness only proves that a page can be loaded and screenshotted.
This probe drives Servo through WebDriver, waits for `readyState=complete`,
samples `requestAnimationFrame` activity, runs a site-specific DOM check, and
captures a post-load screenshot. A clean load without the expected DOM state is
a FAIL, not a silent pass.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import time
import urllib.request
from typing import Any, Dict, List, Optional

# Load the curated site list from the smoke harness without adding `etc/ci` to
# the repository-wide import path.
_spec = importlib.util.spec_from_file_location(
    "site_smoketest", os.path.join(os.path.dirname(__file__), "site_smoketest.py")
)
assert _spec and _spec.loader
_site_smoketest = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _site_smoketest
_spec.loader.exec_module(_site_smoketest)
SITE_SETS = _site_smoketest.SITE_SETS

GENERAL_SCRIPT = r"""
return {
  title: document.title || '',
  readyState: document.readyState,
  bodyTextLength: document.body && document.body.innerText ? document.body.innerText.length : 0,
  elementCount: document.querySelectorAll('*').length,
  scriptCount: document.scripts.length,
  canvasCount: document.querySelectorAll('canvas').length,
  canvasSizes: Array.from(document.querySelectorAll('canvas')).slice(0, 8).map(function(c) {
    return [c.width, c.height];
  }),
  hasWebGL: typeof WebGLRenderingContext !== 'undefined' || typeof WebGL2RenderingContext !== 'undefined',
  rafCount: window.__compatRafCount || 0
};
"""

RAF_START_SCRIPT = r"""
window.__compatRafCount = 0;
(function tick() {
  window.__compatRafCount++;
  window.requestAnimationFrame(tick);
})();
return true;
"""

SITE_CHECKS = {
    "GitHub Servo repository": r"""
var title = document.title || '';
var codeRows = document.querySelectorAll('.blob-code, .react-code-line, [data-testid="blob-view"]').length;
var regions = document.querySelectorAll('header, nav, main, article').length;
var text = document.body && document.body.innerText ? document.body.innerText.length : 0;
return {ok: /servo/i.test(title) && text > 500 && regions > 0,
        detail: {title: title, codeRows: codeRows, regions: regions, bodyTextLength: text}};
""",
    "YouTube watch page": r"""
var videos = Array.from(document.querySelectorAll('video'));
var best = null;
for (var i = 0; i < videos.length; i++) {
  if (!best || videos[i].readyState > best.readyState) best = videos[i];
}
var controls = document.querySelectorAll('ytd-player, .ytp-chrome-controls, [class*="player"]').length;
return {ok: videos.length > 0 && best !== null && best.readyState >= 1,
        detail: {videoCount: videos.length, readyState: best ? best.readyState : null,
                 duration: best ? best.duration : null, playerNodes: controls}};
""",
    "three.js skinning demo": r"""
var canvases = Array.from(document.querySelectorAll('canvas'));
var sizes = canvases.map(function(c) { return [c.width, c.height]; });
var sized = sizes.some(function(s) { return s[0] > 0 && s[1] > 0; });
var raf = window.__compatRafCount || 0;
return {ok: canvases.length > 0 && sized && raf > 0,
        detail: {canvasCount: canvases.length, sizes: sizes, rafCount: raf}};
""",
    "Anime.js showcase": r"""
var animated = document.querySelectorAll('[style*="transform"], [style*="translate"], [class*="anime"]').length;
var raf = window.__compatRafCount || 0;
var text = document.body && document.body.innerText ? document.body.innerText.length : 0;
return {ok: animated > 0 && raf > 0,
        detail: {animatedElements: animated, rafCount: raf, bodyTextLength: text}};
""",
    "Google Maps": r"""
var canvases = document.querySelectorAll('canvas').length;
var mapNodes = document.querySelectorAll('[class*="gm-style"], [id*="map"]').length;
var text = document.body && document.body.innerText ? document.body.innerText.length : 0;
return {ok: canvases > 0 && mapNodes > 0 && text > 0,
        detail: {canvasCount: canvases, mapNodes: mapNodes, bodyTextLength: text}};
""",
}


class ServoDriver:
    def __init__(self, binary: str, port: int, extra_args: Optional[List[str]] = None) -> None:
        self.binary = binary
        self.port = port
        self.extra_args = list(extra_args or [])
        self.base = f"http://127.0.0.1:{port}"
        self.process: Optional[subprocess.Popen[bytes]] = None
        self.session_id: Optional[str] = None

    def request(
        self,
        method: str,
        path: str,
        data: Optional[Dict[str, Any]] = None,
        timeout: int = 30,
    ) -> Dict[str, Any]:
        body = json.dumps(data).encode("utf-8") if data is not None else None
        request = urllib.request.Request(
            self.base + path,
            data=body,
            method=method,
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read().decode("utf-8")
            return json.loads(raw) if raw else {}

    def start(self) -> None:
        self.stop()
        self.process = subprocess.Popen(
            [
                self.binary,
                f"--webdriver={self.port}",
                "--headless",
                "--incognito",
                "--window-size=1280x740",
                *self.extra_args,
                "about:blank",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                status = self.request("GET", "/status", timeout=2)
                if status.get("value", {}).get("ready"):
                    break
            except Exception:
                time.sleep(0.2)
        else:
            raise RuntimeError("WebDriver server did not become ready")
        response = self.request(
            "POST",
            "/session",
            {"capabilities": {"alwaysMatch": {"browserName": "servo"}}},
        )
        self.session_id = response["value"]["sessionId"]

    def stop(self) -> None:
        if self.session_id and self.process and self.process.poll() is None:
            try:
                self.request("DELETE", f"/session/{self.session_id}", timeout=5)
            except Exception:
                pass
        self.session_id = None
        if self.process:
            if self.process.poll() is None:
                self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
        self.process = None

    def execute(self, script: str) -> Any:
        assert self.session_id
        response = self.request(
            "POST",
            f"/session/{self.session_id}/execute/sync",
            {"script": script, "args": []},
        )
        return response.get("value")

    def navigate(self, url: str, timeout: int) -> None:
        assert self.session_id
        self.request(
            "POST",
            f"/session/{self.session_id}/url",
            {"url": url},
            timeout=timeout,
        )

    def screenshot(self, path: str) -> bool:
        assert self.session_id
        try:
            response = self.request("GET", f"/session/{self.session_id}/screenshot", timeout=20)
            encoded = response.get("value")
            if not isinstance(encoded, str):
                return False
            with open(path, "wb") as fh:
                fh.write(base64.b64decode(encoded))
            return True
        except Exception:
            return False


def sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def binary_version(path: str) -> str:
    try:
        result = subprocess.run([path, "--version"], capture_output=True, text=True, timeout=20)
        return (result.stdout + result.stderr).strip() or "not reported"
    except Exception as exc:
        return f"unavailable: {exc}"


def markdown_report(results: List[Dict[str, Any]], provenance: Dict[str, str]) -> str:
    lines = [
        "# Compatibility probe report (`compat-v1`)",
        "",
        f"> Binary: `{provenance['binary']}` · version: `{provenance['version']}` · sha256: `{provenance['sha256']}`",
        f"> Source CI run: `{provenance['source_run_id']}` · artifact: `{provenance['artifact_id']}`",
        f"> Extra servoshell arguments: `{provenance['extra_args']}`",
        "> A PASS means the page loaded, the expected DOM state was present, animation "
        "callbacks ran, and a post-load screenshot was captured. It is not full conformance.",
        "",
        "| Site | Status | Title | RAF ticks | Screenshot | Details |",
        "|---|---|---|---:|---|---|",
    ]
    for row in results:
        detail = row.get("site_check") or {}
        lines.append(
            f"| [{row['name']}]({row['url']}) | **{row['status']}** | `{row.get('title', '')}` | "
            f"{row.get('raf_count', 0)} | `{row.get('screenshot', '')}` | `{json.dumps(detail)}` |"
        )
    failures = [row for row in results if row["status"] != "PASS"]
    lines.extend(["", "## Triage", ""])
    if not failures:
        lines.append("No site failed the load + DOM + animation probe.")
    for row in failures:
        lines.extend(
            [
                f"### {row['name']} ({row['status']})",
                f"- URL: `{row['url']}`",
                f"- Reason: `{row.get('reason')}`",
                f"- General facts: `{json.dumps(row.get('general'))}`",
                "",
            ]
        )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Servo compatibility probe for compat-v1")
    parser.add_argument("--servo-binary", required=True)
    parser.add_argument(
        "--extra-servo-arg",
        action="append",
        default=[],
        help="Extra servoshell argument for triage, e.g. a --prefs-file=PATH",
    )
    parser.add_argument("--port", type=int, default=7011)
    parser.add_argument("--timeout", type=int, default=25, help="Navigation timeout per site")
    parser.add_argument("--screenshot-dir", required=True)
    parser.add_argument("--output-json", required=True)
    parser.add_argument("--output-markdown", required=True)
    parser.add_argument("--source-run-id", default="unknown")
    parser.add_argument("--artifact-id", default="unknown")
    args = parser.parse_args()

    if not os.path.isfile(args.servo_binary):
        print(f"ERROR: no servoshell binary at {args.servo_binary}", file=sys.stderr)
        return 2
    os.makedirs(args.screenshot_dir, exist_ok=True)

    provenance = {
        "binary": args.servo_binary,
        "version": binary_version(args.servo_binary),
        "sha256": sha256(args.servo_binary),
        "source_run_id": args.source_run_id,
        "artifact_id": args.artifact_id,
        "extra_args": args.extra_servo_arg,
    }

    driver = ServoDriver(args.servo_binary, args.port, args.extra_servo_arg)
    results: List[Dict[str, Any]] = []
    try:
        driver.start()
        for index, site in enumerate(SITE_SETS["compat-v1"]):
            row: Dict[str, Any] = {
                "name": site["name"],
                "url": site["url"],
                "category": site["category"],
                "status": "FAIL",
                "reason": None,
                "title": None,
                "general": None,
                "site_check": None,
                "raf_count": 0,
                "screenshot": None,
            }
            try:
                driver.navigate(site["url"], args.timeout)
                deadline = time.monotonic() + 10
                while True:
                    general = driver.execute(GENERAL_SCRIPT) or {}
                    if general.get("readyState") == "complete" or time.monotonic() >= deadline:
                        break
                    time.sleep(0.5)
                driver.execute(RAF_START_SCRIPT)
                time.sleep(0.75)
                general = driver.execute(GENERAL_SCRIPT) or {}
                check_script = SITE_CHECKS.get(site["name"])
                site_check = driver.execute(check_script) if check_script else {"ok": False, "detail": "no check"}
                screenshot_path = os.path.join(
                    args.screenshot_dir,
                    f"{index + 1:02d}-probe-{site['name'].lower().replace(' ', '-')}.png",
                )
                got_screenshot = driver.screenshot(screenshot_path)
                row.update(
                    {
                        "title": general.get("title"),
                        "general": general,
                        "site_check": site_check,
                        "raf_count": general.get("rafCount", 0),
                        "screenshot": screenshot_path if got_screenshot else None,
                    }
                )
                if not got_screenshot:
                    row["reason"] = "WebDriver screenshot was not available"
                elif not site_check or not site_check.get("ok"):
                    row["reason"] = "Site-specific DOM/animation check failed"
                elif general.get("readyState") != "complete":
                    row["reason"] = "document.readyState never reached complete"
                else:
                    row["status"] = "PASS"
            except Exception as exc:
                row["reason"] = f"{type(exc).__name__}: {exc}"
                try:
                    driver.stop()
                    driver.start()
                except Exception:
                    pass
            results.append(row)
            print(f"{row['status']}: {site['name']} ({row.get('reason') or 'ok'})")
    finally:
        driver.stop()

    with open(args.output_json, "w", encoding="utf-8") as fh:
        json.dump({"provenance": provenance, "results": results}, fh, indent=2)
    with open(args.output_markdown, "w", encoding="utf-8") as fh:
        fh.write(markdown_report(results, provenance))
    return 0 if results and all(row["status"] == "PASS" for row in results) else 1


if __name__ == "__main__":
    sys.exit(main())
