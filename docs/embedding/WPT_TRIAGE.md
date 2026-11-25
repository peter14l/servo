# WPT High-Impact Triage & Remediation Tickets

Status: **Active Triage** ┬╖ Milestone: `M1 / M4`
Companion docs: [`FULL_BROWSER_ROADMAP.md`](FULL_BROWSER_ROADMAP.md), [`PARITY_MATRIX.md`](PARITY_MATRIX.md), [`WPT_CONFORMANCE_BASELINE.md`](WPT_CONFORMANCE_BASELINE.md).

---

## 1. Overview & Strategy

Analysis of the 154,583 non-pass metadata entries across `tests/wpt/meta` reveals that failures are concentrated heavily in a small set of foundational suites. To reach modern web compatibility and support real-world consumer sites, remediation is prioritized by high-leverage user impact rather than sheer volume.

```
+-----------------------------------------------------------------------+
| Priority 1: Web Fundamentals (Blockers for top 100 consumer websites) |
|   - Fetch & Streams (Streaming body, chunked transfer, redirect)      |
|   - HTML Forms, Canvas & Media (Interactive controls, 2D contexts)   |
|   - CSS Flexbox & Grid Layout (Subgrid, intrinsic sizing, alignment)  |
|   - DOM & Event Timing (Microtask ordering, event dispatch)          |
+-----------------------------------------------------------------------+
| Priority 2: Modern Web Storage & Offline (PWA / Application sites)   |
|   - IndexedDB & Cache API (Local database transactions)              |
|   - Service Workers (Lifecycle, fetch routing)                        |
+-----------------------------------------------------------------------+
| Priority 3: Rich Graphics & Audio                                     |
|   - WebGL 2 & WebGPU conformance                                     |
|   - WebAudio & Media Source Extensions (MSE)                          |
+-----------------------------------------------------------------------+
| Defer / Non-blocking (Low consumer leverage)                          |
|   - Editing / execCommand legacy surface (23k failures)               |
|   - WebNN (spec in high flux)                                         |
+-----------------------------------------------------------------------+
```

---

## 2. High-Impact Remediation Tickets

### Ticket WPT-01: WHATWG Fetch & Streams Conformance
- **Target Suite:** `tests/wpt/meta/fetch/`, `tests/wpt/meta/streams/`
- **Current Failures:** 1,415 (`fetch`) + partial streams
- **Real-World Impact:** Modern sites (YouTube, GitHub, Twitter/X) rely on `fetch` response streaming, CORS redirect handling, and `ReadableStream` upload/download bodies.
- **Key Focus Areas:**
  1. Stream pipe-to / tee operations in `components/script/dom/readablestream.rs`.
  2. CORS header parsing edge cases in `components/net/cors_cache.rs` and header list null check in `components/shared/net/fetch/headers.rs` (Landed).
  3. Chunked transfer decoding and keep-alive handling in `components/net/http_loader.rs`.
- **Milestone:** M1

---

### Ticket WPT-02: CSS Grid & Flexbox Edge Cases
- **Target Suite:** `tests/wpt/meta/css/css-grid/`, `tests/wpt/meta/css/css-flexbox/`
- **Current Failures:** ~8,500 subset in `css`
- **Real-World Impact:** Broken page layout, overlapping text, and misaligned navigation bars across e-commerce and media sites.
- **Key Focus Areas:**
  1. Intrinsic sizing (`min-content`, `max-content`, `fit-content`) calculations in `components/layout/`.
  2. Grid track auto-placement and subgrid alignment.
  3. Flexbox baseline alignment with nested items.
- **Milestone:** M1 / M4

---

### Ticket WPT-03: HTML5 Form Validation & Interactive Controls
- **Target Suite:** `tests/wpt/meta/html/semantics/forms/`, `tests/wpt/meta/html/semantics/interactive-elements/the-dialog-element/`
- **Current Failures:** ~4,200 in `html`
- **Real-World Impact:** Broken login forms, date pickers, client-side validation errors preventing form submission.
- **Key Focus Areas:**
  1. Constraint validation API (`checkValidity()`, `reportValidity()`, `:invalid` pseudo-class, and `stepMismatch` precision calculations - Landed in `components/script/dom/html/form_controls/htmlinputelement.rs`).
  2. Date/Time input `valueAsNumber` conversions: Euclidean division for negative/positive months and modulo 24h millisecond time conversions (Landed in `month_input_type.rs` and `time_input_type.rs`).
  3. Embedder control event forwarding (`components/servo/lib.rs:EmbedderControl`).
  4. `<dialog>` element modal request close, `cancel` event firing, and button `command="request-close"` (Landed in `components/script/dom/html/interactive/htmldialogelement.rs`).
- **Milestone:** M1

---

### Ticket WPT-04: IndexedDB Transaction & Key-Path Conformance
- **Target Suite:** `tests/wpt/meta/IndexedDB/`
- **Current Failures:** 1,270 failures, 187 errors
- **Real-World Impact:** Web apps failing to load offline data or corrupting local stores (e.g. Google Docs, Slack web).
- **Key Focus Areas:**
  1. IndexedDB transaction lifecycle and auto-commit semantics in `components/storage/`.
  2. Keypath extraction for compound keys and binary arrays.
  3. Error code propagation matching W3C IndexedDB 3.0.
- **Milestone:** M4

---

### Ticket WPT-05: DOM Microtask & MutationObserver Ordering
- **Target Suite:** `tests/wpt/meta/dom/`
- **Current Failures:** 1,239 failures, 27 errors
- **Real-World Impact:** Race conditions in single-page apps (React, Vue, Svelte) causing blank screens or stale state.
- **Key Focus Areas:**
  1. Microtask checkpoint execution order in `components/script/dom/eventtarget.rs`.
  2. MutationObserver delivery timing relative to custom element reactions.
  3. Event bubbling and capture across shadow boundaries.
- **Milestone:** M1


---

## Real-site smoke triage (REL-3 step 2)

This section tracks non-`PASS` smoke results and harness defects. A row is only `FIXED`
when the correction is in the committed harness or engine, not when an expectation was
weakened. Site `PASS` rows in [`SITE_SMOKETEST_REPORT.md`](SITE_SMOKETEST_REPORT.md) do
not create tickets.

| ID | Symptom | Timestamp (UTC) | Root cause category | Status | Fix / evidence |
|---|---|---|---|---|---|
| `SMOKE-001` | Five `TIMEOUT` rows when the harness was pointed at the downloaded Windows artifact; `ServoShell.exe --version` hung and showed `WixBurnMessageWindow`. | `2026-11-24 18:25:19Z` | Harness / provenance | `FIXED` | The artifact `checked-release-binary-windows` contains a **WiX Burn installer**, not a browser. The installer was completed, and the installed `servoshell.exe` self-reports `Servo 0.7.0-fd4655eb3`, matching green run `45428660357`. The invalid rows were discarded — not counted as site failures or passes. See `SITE_SMOKETEST_REPORT.md` §Provenance. |
| `SMOKE-002` | Even with the real browser, a single-site run wrote a screenshot but never exited until killed. | `2026-11-24 18:37:39Z` | Harness | `FIXED` | `servoshell` needs `-x` / `--exit` to terminate after load. Added `-x` to the harness invocation. Verified: single-site `-f -x --headless … -o … https://example.com` exited `0` and wrote a 112,689-byte screenshot. No timeout was increased. |
| `SMOKE-003` | Harness returned exit code `0` even when all five sites were `TIMEOUT`. | observed `2026-11-24 18:25:19Z` | Harness | `FIXED` | Added `exit_code_for()`: exit `0` only when every result is `PASS`; otherwise `1`. Re-evaluating the discarded report through the fixed function yields `expected_exit=1`, so a CI consumer can no longer read mass timeouts as green. |
| `SMOKE-004` | A clean process exit was labelled `PASS` without any screenshot for visual targets. | pre-fix harness | Harness / evidence | `FIXED` | Added `--screenshot-dir`; visual targets #2–#5 are downgraded to `NOT_CONFIRMED` without a screenshot file. The executed run carries five 1280×740 PNGs referenced from `SITE_SMOKETEST_REPORT.md`. |

### Discarded invalid run (not a result)

| Site | URL | Reported status | Why it was discarded |
|---|---|---|---|
| Example Domain | `https://example.com` | `TIMEOUT` (30.06 s) | Harness launched the WiX installer, not the browser (`SMOKE-001`). |
| Hacker News | `https://news.ycombinator.com` | `TIMEOUT` (30.03 s) | Same cause. |
| Wikipedia Main Page | `https://en.wikipedia.org/wiki/Main_Page` | `TIMEOUT` (30.03 s) | Same cause. |
| DuckDuckGo Lite | `https://lite.duckduckgo.com` | `TIMEOUT` (30.03 s) | Same cause. |
| Httpbin JSON | `https://httpbin.org/get` | `TIMEOUT` (30.05 s) | Same cause. |

**Blocker note:** no engine cause could be isolated from that run because the executable
under test was the installer. The finding is therefore classified as harness/provenance,
not `DOM/JS`, `CSS/layout`, `WebRender`, `DNS/TLS`, or an engine timeout. It must not be
used as evidence for or against real-world compatibility.

### Open engine work

- **WPT delta (REL-3 step 3):** `WPT_STRICT_BASELINE.md` records 88.37% assertion passes
  and 55.04% harness `OK`, with 109 harness-`OK` files containing failing assertions.
  No engine fix or strict WPT re-run has been performed in this session, so no delta is
  claimed. Fix engine behaviour first, then re-run strict WPT.
- **No site-specific hacks:** none were added. All four fixes above are harness/provenance
  corrections that make failures visible rather than hiding them.
