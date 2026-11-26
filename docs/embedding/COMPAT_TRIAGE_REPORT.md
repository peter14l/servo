# `compat-v1` deeper probe triage report

Status: **findings only** · REL-3 remains `IN_PROGRESS` · 2026-11-26

## Scope and provenance

This report covers the bounded compatibility sprint for GitHub, YouTube, the three.js
skinning demo, Anime.js, and Google Maps. It is separate from the fixed five-site smoke
set and is **not** a conformance pass or a release gate by itself.

All browser runs used the installed CI-derived binary:

| Item | Value |
|---|---|
| Binary | `C:\Program Files\Servo\Servo Tech Demo\servoshell.exe` |
| Version | `Servo 0.7.0-fd4655eb3` |
| SHA-256 | `E0E28202B914ECDC0EC1E352D69FDEE76AE25A307C59B2551316A512415D7314` |
| Source CI run | `45428660357` (successful Main run) |
| Artifact | `14943064230` (`checked-release-binary-windows`) |

The baseline deeper-probe output is in [`COMPAT_PROBE_REPORT.md`](COMPAT_PROBE_REPORT.md)
and [`compat_probe.json`](compat_probe.json). Diagnostic re-runs used
`--extra-servo-arg=--prefs-file=…` only to isolate causes; those prefs are **not** site
exceptions, are not persisted as shipped defaults, and no check was weakened. Raw
diagnostic reports are `compat_triage_webgl2_run.json`/`.md` and
`compat_triage_animations_run.json`/`.md`; screenshots are in `compat_triage_shots/`.

## Result summary

| Site | Load smoke | Deeper probe | Root cause / status |
|---|---|---|---|
| GitHub Servo repository | `PASS` | `FAIL` (screenshot unavailable) | Intermittent WebRender/screenshot evidence failure on the old artifact. Both diagnostic re-runs captured the screenshot and passed the DOM check. No site-specific change made. |
| YouTube watch page | `PASS` | `PASS` | Video metadata and player DOM check passed. Diagnostic re-runs sometimes reached only `readyState=interactive` while the video check still passed; no change made. |
| three.js skinning demo | `PASS` | `FAIL` (`canvasCount: 0`) | `dom_webgl2_enabled` is `false` by default, so the WebGL2 canvas was not created. With the diagnostic WebGL2 pref, `canvasCount: 3` and the site check passed, but screenshot capture hit the renderer panic below. |
| Anime.js showcase | `PASS` | `FAIL` (`.page opacity: 0`, `animatedElements: 0`) | The site's module script aborts while creating a WebGL renderer: `THREE.WebGLRenderer: Error creating WebGL context.` Without the class added by that script the page stays hidden. With WebGL2 enabled, the same renderer panic below crashed the browser. |
| Google Maps | `PASS` | `FAIL` (browser crash) | Repeated `SurfaceImportFailed(BadAttribute)` panic at `components/shared/paint/rendering_context.rs:260`. With WebGL2 enabled the crash stopped but the map DOM was still empty (`canvasCount: 0`, `mapNodes: 0`, `bodyTextLength: 1`). |

A load-only `PASS` is not visual correctness. The smoke screenshots proved clean process
exit and screenshot capture, not that the expected DOM/animation state existed.

## Reproducible engine defect fixed in source

Renderer crash (`SurfaceImportFailed(BadAttribute)`):

- Evidence: `servo_crash.log` and `docs/embedding/compat_triage_logs/anime_webgl2_stderr.log`
  show `called Result::unwrap() on an Err value: (SurfaceImportFailed(BadAttribute), …)`
  at `components/shared/paint/rendering_context.rs:260`; this occurred on Google Maps and
  on Anime.js with WebGL2 enabled.
- `create_texture` already returns `Option`, and its caller
  (`components/paint/webrender_external_images.rs`) treats `None` as “no external image
  available”. Unwrapping the surfman result was therefore an unnecessary panic boundary
  violation, not a required failure.
- Fix committed at `044f25042c5`: log the surfman error with `warn!` and return `None`
  instead of unwinding through the renderer.
- Verification status: local checks passed and CI was dispatched at the fixed commit;
  the fix is **not** claimed verified against the old installed artifact. A fresh
  successful CI artifact is required before changing any site result to `PASS`.

## Feature-gate findings (not flipped)

`dom_webgl2_enabled` is intentionally `false` in `components/config/prefs.rs`. The
three.js and Anime.js diagnostic runs show that this pref is the immediate cause of the
missing canvas / hidden page, but flipping a feature gate globally is a product decision
with broader conformance and stability consequences. It was not changed in this sprint.

A controlled local probe served a page containing both a classic script and
`<script type="module">`. Result: `classicRan: true`, `moduleRan: true`,
`moduleValue: 42`. Module-script execution is therefore not the root cause of the
Anime.js failure; WebGL context creation is.

## Lint repair found during CI verification

CI run `45786718192` failed with `clippy::collapsible-if` at
`components/layout/display_list/background.rs:193`. Commit `c94b8bcb080` collapsed the
natural-ratio and default-SVG-size guards into a `let` chain without changing behavior.
This is recorded for traceability; it is not a compatibility result.

## Honest conclusion

REL-3 remains `IN_PROGRESS`:

1. ✅ WPT baseline published with its 88.37% assertion-pass and 55.04% harness-`OK`
   caveats.
2. ✅ Fixed five-site smoke and `compat-v1` load smoke executed with screenshots.
3. ⏳ Deeper probe found four failures; one reproducible renderer panic is fixed in
   source but awaits CI/new-artifact verification, and the WebGL2 feature-gate gap remains.
4. ⏳ Strict WPT delta on the release candidate is not computed until a green Main run

No target was made to pass by a site-specific hack, a lowered timeout, a suppressed
check, or an expectation change. This branch remains developer preview / dogfood only;
`PUBLIC_RELEASE_READINESS = NOT_CONFIRMED`.
