# Compatibility probe report (`compat-v1`)

> Binary: `C:\Program Files\Servo\Servo Tech Demo\servoshell.exe` · version: `Version: Servo 0.7.0-fd4655eb3` · sha256: `E0E28202B914ECDC0EC1E352D69FDEE76AE25A307C59B2551316A512415D7314`
> Source CI run: `45428660357` · artifact: `14943064230`
> Extra servoshell arguments: `['--prefs-file=docs/embedding/compat_triage_animations.json']`
> A PASS means the page loaded, the expected DOM state was present, animation callbacks ran, and a post-load screenshot was captured. It is not full conformance.

| Site | Status | Title | RAF ticks | Screenshot | Details |
|---|---|---|---:|---|---|
| [GitHub Servo repository](https://github.com/servo/servo) | **PASS** | `GitHub - servo/servo: Servo aims to empower developers with a lightweight, high-performance alternative for embedding web technologies in applications. · GitHub` | 13 | `C:\Users\LOQ\AppData\Local\Temp\opencode\compat_triage_anim_shots\01-probe-github-servo-repository.png` | `{"detail": {"bodyTextLength": 8184, "codeRows": 0, "title": "GitHub - servo/servo: Servo aims to empower developers with a lightweight, high-performance alternative for embedding web technologies in applications. \u00b7 GitHub", "regions": 10}, "ok": true}` |
| [YouTube watch page](https://www.youtube.com/watch?v=dQw4w9WgXcQ) | **FAIL** | `Rick Astley - Never Gonna Give You Up (Official Video) (4K Remaster) - YouTube` | 3 | `C:\Users\LOQ\AppData\Local\Temp\opencode\compat_triage_anim_shots\02-probe-youtube-watch-page.png` | `{"ok": true, "detail": {"readyState": 4, "videoCount": 1, "playerNodes": 44, "duration": 213.043046875}}` |
| [three.js skinning demo](https://threejs.org/examples/webgl_animation_skinning_blending.html) | **FAIL** | `three.js webgl - animation - skinning` | 24 | `C:\Users\LOQ\AppData\Local\Temp\opencode\compat_triage_anim_shots\03-probe-three.js-skinning-demo.png` | `{"ok": false, "detail": {"canvasCount": 0, "sizes": [], "rafCount": 25}}` |
| [Anime.js showcase](https://animejs.com/) | **FAIL** | `Anime.js | JavaScript Animation Engine` | 39 | `C:\Users\LOQ\AppData\Local\Temp\opencode\compat_triage_anim_shots\04-probe-anime.js-showcase.png` | `{"ok": false, "detail": {"rafCount": 40, "bodyTextLength": 3660, "animatedElements": 0}}` |
| [Google Maps](https://www.google.com/maps/@37.7749,-122.4194,12z) | **FAIL** | `None` | 0 | `None` | `{}` |

## Triage

### YouTube watch page (FAIL)
- URL: `https://www.youtube.com/watch?v=dQw4w9WgXcQ`
- Reason: `document.readyState never reached complete`
- General facts: `{"hasWebGL": true, "title": "Rick Astley - Never Gonna Give You Up (Official Video) (4K Remaster) - YouTube", "canvasCount": 0, "scriptCount": 59, "bodyTextLength": 1333, "canvasSizes": [], "elementCount": 4167, "rafCount": 3, "readyState": "interactive"}`

### three.js skinning demo (FAIL)
- URL: `https://threejs.org/examples/webgl_animation_skinning_blending.html`
- Reason: `Site-specific DOM/animation check failed`
- General facts: `{"canvasSizes": [], "rafCount": 24, "title": "three.js webgl - animation - skinning", "hasWebGL": true, "elementCount": 121, "readyState": "complete", "bodyTextLength": 540, "scriptCount": 2, "canvasCount": 0}`

### Anime.js showcase (FAIL)
- URL: `https://animejs.com/`
- Reason: `Site-specific DOM/animation check failed`
- General facts: `{"scriptCount": 5, "hasWebGL": true, "bodyTextLength": 3660, "readyState": "complete", "title": "Anime.js | JavaScript Animation Engine", "elementCount": 726, "canvasSizes": [], "canvasCount": 0, "rafCount": 39}`

### Google Maps (FAIL)
- URL: `https://www.google.com/maps/@37.7749,-122.4194,12z`
- Reason: `URLError: <urlopen error [WinError 10061] No connection could be made because the target machine actively refused it>`
- General facts: `null`
