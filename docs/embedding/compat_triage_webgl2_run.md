# Compatibility probe report (`compat-v1`)

> Binary: `C:\Program Files\Servo\Servo Tech Demo\servoshell.exe` · version: `Version: Servo 0.7.0-fd4655eb3` · sha256: `E0E28202B914ECDC0EC1E352D69FDEE76AE25A307C59B2551316A512415D7314`
> Source CI run: `45428660357` · artifact: `14943064230`
> Extra servoshell arguments: `['--prefs-file=docs/embedding/compat_triage_webgl2.json']`
> A PASS means the page loaded, the expected DOM state was present, animation callbacks ran, and a post-load screenshot was captured. It is not full conformance.

| Site | Status | Title | RAF ticks | Screenshot | Details |
|---|---|---|---:|---|---|
| [GitHub Servo repository](https://github.com/servo/servo) | **PASS** | `GitHub - servo/servo: Servo aims to empower developers with a lightweight, high-performance alternative for embedding web technologies in applications. · GitHub` | 12 | `C:\Users\LOQ\AppData\Local\Temp\opencode\compat_triage_webgl2_shots\01-probe-github-servo-repository.png` | `{"detail": {"codeRows": 0, "title": "GitHub - servo/servo: Servo aims to empower developers with a lightweight, high-performance alternative for embedding web technologies in applications. \u00b7 GitHub", "bodyTextLength": 8184, "regions": 10}, "ok": true}` |
| [YouTube watch page](https://www.youtube.com/watch?v=dQw4w9WgXcQ) | **FAIL** | `Rick Astley - Never Gonna Give You Up (Official Video) (4K Remaster) - YouTube` | 4 | `C:\Users\LOQ\AppData\Local\Temp\opencode\compat_triage_webgl2_shots\02-probe-youtube-watch-page.png` | `{"ok": true, "detail": {"videoCount": 1, "duration": 213.043046875, "readyState": 4, "playerNodes": 44}}` |
| [three.js skinning demo](https://threejs.org/examples/webgl_animation_skinning_blending.html) | **FAIL** | `three.js webgl - animation - skinning` | 20 | `None` | `{"ok": true, "detail": {"rafCount": 20, "sizes": [[1280, 740], [80, 48], [80, 48]], "canvasCount": 3}}` |
| [Anime.js showcase](https://animejs.com/) | **FAIL** | `None` | 0 | `None` | `{}` |
| [Google Maps](https://www.google.com/maps/@37.7749,-122.4194,12z) | **FAIL** | `Google Maps` | 24 | `C:\Users\LOQ\AppData\Local\Temp\opencode\compat_triage_webgl2_shots\05-probe-google-maps.png` | `{"ok": false, "detail": {"bodyTextLength": 1, "canvasCount": 0, "mapNodes": 0}}` |

## Triage

### YouTube watch page (FAIL)
- URL: `https://www.youtube.com/watch?v=dQw4w9WgXcQ`
- Reason: `document.readyState never reached complete`
- General facts: `{"elementCount": 4223, "hasWebGL": true, "rafCount": 4, "title": "Rick Astley - Never Gonna Give You Up (Official Video) (4K Remaster) - YouTube", "canvasCount": 0, "readyState": "interactive", "scriptCount": 59, "canvasSizes": [], "bodyTextLength": 1653}`

### three.js skinning demo (FAIL)
- URL: `https://threejs.org/examples/webgl_animation_skinning_blending.html`
- Reason: `WebDriver screenshot was not available`
- General facts: `{"hasWebGL": true, "elementCount": 125, "readyState": "complete", "canvasSizes": [[1280, 740], [80, 48], [80, 48]], "scriptCount": 2, "canvasCount": 3, "title": "three.js webgl - animation - skinning", "rafCount": 20, "bodyTextLength": 540}`

### Anime.js showcase (FAIL)
- URL: `https://animejs.com/`
- Reason: `URLError: <urlopen error [WinError 10061] No connection could be made because the target machine actively refused it>`
- General facts: `null`

### Google Maps (FAIL)
- URL: `https://www.google.com/maps/@37.7749,-122.4194,12z`
- Reason: `Site-specific DOM/animation check failed`
- General facts: `{"rafCount": 24, "title": "Google Maps", "elementCount": 149, "canvasSizes": [], "scriptCount": 12, "hasWebGL": true, "canvasCount": 0, "readyState": "complete", "bodyTextLength": 1}`
