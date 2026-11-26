# Compatibility probe report (`compat-v1`)

> Binary: `C:\Program Files\Servo\Servo Tech Demo\servoshell.exe` · version: `Version: Servo 0.7.0-fd4655eb3` · sha256: `E0E28202B914ECDC0EC1E352D69FDEE76AE25A307C59B2551316A512415D7314`
> Source CI run: `45428660357` · artifact: `14943064230`
> A PASS means the page loaded, the expected DOM state was present, animation callbacks ran, and a post-load screenshot was captured. It is not full conformance.
> Root-cause analysis and diagnostic re-runs are in [`COMPAT_TRIAGE_REPORT.md`](COMPAT_TRIAGE_REPORT.md).

| Site | Status | Title | RAF ticks | Screenshot | Details |
|---|---|---|---:|---|---|
| [GitHub Servo repository](https://github.com/servo/servo) | **FAIL** | `GitHub - servo/servo: Servo aims to empower developers with a lightweight, high-performance alternative for embedding web technologies in applications. · GitHub` | 20 | `None` | `{"ok": true, "detail": {"title": "GitHub - servo/servo: Servo aims to empower developers with a lightweight, high-performance alternative for embedding web technologies in applications. \u00b7 GitHub", "codeRows": 0, "bodyTextLength": 8457, "regions": 10}}` |
| [YouTube watch page](https://www.youtube.com/watch?v=dQw4w9WgXcQ) | **PASS** | `Rick Astley - Never Gonna Give You Up (Official Video) (4K Remaster) - YouTube` | 24 | `docs/embedding/compat_probe_shots\02-probe-youtube-watch-page.png` | `{"ok": true, "detail": {"duration": 213.043046875, "playerNodes": 44, "videoCount": 1, "readyState": 4}}` |
| [three.js skinning demo](https://threejs.org/examples/webgl_animation_skinning_blending.html) | **FAIL** | `three.js webgl - animation - skinning` | 25 | `docs/embedding/compat_probe_shots\03-probe-three.js-skinning-demo.png` | `{"detail": {"sizes": [], "canvasCount": 0, "rafCount": 25}, "ok": false}` |
| [Anime.js showcase](https://animejs.com/) | **FAIL** | `Anime.js | JavaScript Animation Engine` | 26 | `docs/embedding/compat_probe_shots\04-probe-anime.js-showcase.png` | `{"ok": false, "detail": {"bodyTextLength": 3648, "rafCount": 26, "animatedElements": 0}}` |
| [Google Maps](https://www.google.com/maps/@37.7749,-122.4194,12z) | **FAIL** | `None` | 0 | `None` | `{}` |

## Triage

### GitHub Servo repository (FAIL)
- URL: `https://github.com/servo/servo`
- Reason: `WebDriver screenshot was not available`
- General facts: `{"title": "GitHub - servo/servo: Servo aims to empower developers with a lightweight, high-performance alternative for embedding web technologies in applications. \u00b7 GitHub", "rafCount": 20, "hasWebGL": true, "readyState": "complete", "scriptCount": 13, "canvasCount": 0, "bodyTextLength": 8457, "canvasSizes": [], "elementCount": 2784}`

### three.js skinning demo (FAIL)
- URL: `https://threejs.org/examples/webgl_animation_skinning_blending.html`
- Reason: `Site-specific DOM/animation check failed`
- General facts: `{"readyState": "complete", "bodyTextLength": 540, "hasWebGL": true, "elementCount": 121, "title": "three.js webgl - animation - skinning", "rafCount": 25, "scriptCount": 2, "canvasCount": 0, "canvasSizes": []}`

### Anime.js showcase (FAIL)
- URL: `https://animejs.com/`
- Reason: `Site-specific DOM/animation check failed`
- General facts: `{"scriptCount": 5, "rafCount": 26, "canvasSizes": [], "elementCount": 726, "readyState": "complete", "bodyTextLength": 3648, "canvasCount": 0, "hasWebGL": true, "title": "Anime.js | JavaScript Animation Engine"}`

### Google Maps (FAIL)
- URL: `https://www.google.com/maps/@37.7749,-122.4194,12z`
- Reason: `URLError: <urlopen error [WinError 10061] No connection could be made because the target machine actively refused it>`
- General facts: `null`
