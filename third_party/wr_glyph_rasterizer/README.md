# Vendored `wr_glyph_rasterizer`

This is a copy of the `wr_glyph_rasterizer` crate (version 0.70.1) with a local
patch that adds DirectWrite color glyph support on Windows.

Upstream only rasterizes the *base* glyph outline with
`IDWriteGlyphRunAnalysis::CreateAlphaTexture`. For color fonts such as
`Segoe UI Emoji`, the base glyphs are the monochrome `COLR` fallback outlines,
which is why emoji rendered as white outlines with a valid but colorless shape.

The patch calls `IDWriteFactory2::TranslateColorGlyphRun` for glyphs that the
engine considers bitmap/color glyphs, rasterizes each `COLR` layer with
`IDWriteGlyphRunAnalysis`, and composites the layers into a premultiplied BGRA
bitmap reported as `GlyphFormat::ColorBitmap`.

Everything else is unmodified from the 0.70.1 release. The crate is wired up
through `[patch.crates-io]` in the repository root `Cargo.toml` and is excluded
from the Cargo workspace.
