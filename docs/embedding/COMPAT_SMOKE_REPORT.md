# Real-World Website Conformance Smoke Report

> **Executed smoke run.** Site set: `compat-v1`. Binary: `C:\Program Files\Servo\Servo Tech Demo\servoshell.exe` (`Servo 0.7.0-fd4655eb3`, SHA-256 `E0E28202B914ECDC0EC1E352D69FDEE76AE25A307C59B2551316A512415D7314`), installed from successful Main run `45428660357` / artifact `14943064230`.
>
> These rows prove load + clean exit + screenshot capture only. The deeper DOM/animation
> probe found real failures; see [`COMPAT_PROBE_REPORT.md`](COMPAT_PROBE_REPORT.md) and
> [`COMPAT_TRIAGE_REPORT.md`](COMPAT_TRIAGE_REPORT.md). They are not conformance passes.

| Site | Category | Status | Time | Screenshot | Root Cause |
|---|---|---|---:|---|---|
| [GitHub Servo repository](https://github.com/servo/servo) | Large dynamic application / SVG icons / code view | **PASS** | 6.0s | `docs/embedding/compat_smoke_shots\01-github-servo-repository.png` | Clean exit |
| [YouTube watch page](https://www.youtube.com/watch?v=dQw4w9WgXcQ) | Heavy media / canvas / continuous animation | **PASS** | 3.61s | `docs/embedding/compat_smoke_shots\02-youtube-watch-page.png` | Clean exit |
| [three.js skinning demo](https://threejs.org/examples/webgl_animation_skinning_blending.html) | WebGL animation | **PASS** | 2.06s | `docs/embedding/compat_smoke_shots\03-three-js-skinning-demo.png` | Clean exit |
| [Anime.js showcase](https://animejs.com/) | JS/CSS/SVG animation showcase | **PASS** | 2.83s | `docs/embedding/compat_smoke_shots\04-anime-js-showcase.png` | Clean exit |
| [Google Maps](https://www.google.com/maps/@37.7749,-122.4194,12z) | WebGL tiles / continuous pan and zoom | **PASS** | 6.75s | `docs/embedding/compat_smoke_shots\05-google-maps.png` | Clean exit |

## Root Cause Triage & Findings

All target smoke sites completed without crashing or timing out.