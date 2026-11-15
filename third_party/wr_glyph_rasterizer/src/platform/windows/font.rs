/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{FontInstanceFlags, FontKey, FontRenderMode, FontVariation};
use api::{ColorU, FontTemplate, GlyphDimensions, NativeFontHandle};
use crate::gamma_lut::{ColorLut, GammaLut};
use crate::rasterizer::{FontInstance, FontTransform, GlyphKey};
use crate::rasterizer::{GlyphFormat, GlyphRasterError, GlyphRasterResult, RasterizedGlyph};
use crate::rasterizer::apply_multistrike_bold;
use crate::types::{FastHashMap, FastHashSet};
use std::borrow::Borrow;
use std::collections::hash_map::Entry;
use std::hash::{Hash, Hasher};
use std::path::Path;
use std::sync::{Arc, Mutex};
use api::FontInstancePlatformOptions;
use std::mem;
use std::os::raw::c_void;
use std::ptr;
use std::slice;

use winapi::shared::winerror::S_OK;
use winapi::um::dwrite::{DWriteCreateFactory, DWRITE_FACTORY_TYPE_SHARED, IDWriteFactory};
use winapi::um::dwrite_2::{DWRITE_COLOR_GLYPH_RUN, IDWriteColorGlyphRunEnumerator, IDWriteFactory2};
use winapi::um::unknwnbase::IUnknown;
use winapi::Interface;

lazy_static! {
    static ref DEFAULT_FONT_DESCRIPTOR: dwrote::FontDescriptor = dwrote::FontDescriptor {
        family_name: "Arial".to_owned(),
        weight: dwrote::FontWeight::Regular,
        stretch: dwrote::FontStretch::Normal,
        style: dwrote::FontStyle::Normal,
    };
}

type CachedFontKey = Arc<Path>;

// A cached dwrote font file that is shared among all faces.
// Each face holds a CachedFontKey to keep track of how many users of the font there are.
struct CachedFont {
    key: CachedFontKey,
    file: dwrote::FontFile,
}

// FontFile contains a ComPtr<IDWriteFontFile>, but DWrite font files are threadsafe.
unsafe impl Send for CachedFont {}

impl PartialEq for CachedFont {
    fn eq(&self, other: &CachedFont) -> bool {
        self.key == other.key
    }
}
impl Eq for CachedFont {}

impl Hash for CachedFont {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.key.hash(state);
    }
}

impl Borrow<Path> for CachedFont {
    fn borrow(&self) -> &Path {
        &*self.key
    }
}

lazy_static! {
    // This is effectively a weak map of dwrote FontFiles. CachedFonts are entered into the
    // cache when there are any FontFaces using them. CachedFonts are removed from the cache
    // when there are no more FontFaces using them at all.
    static ref FONT_CACHE: Mutex<FastHashSet<CachedFont>> = Mutex::new(FastHashSet::default());
}

struct FontFace {
    cached: Option<CachedFontKey>,
    file: dwrote::FontFile,
    index: u32,
    face: dwrote::FontFace,
}

pub struct FontContext {
    fonts: FastHashMap<FontKey, FontFace>,
    variations: FastHashMap<(FontKey, dwrote::DWRITE_FONT_SIMULATIONS, Vec<FontVariation>), dwrote::FontFace>,
    gamma_luts: FastHashMap<(u16, u8), GammaLut>,
}

// DirectWrite is safe to use on multiple threads and non-shareable resources are
// all hidden inside their font context.
unsafe impl Send for FontContext {}

fn dwrite_texture_type(render_mode: FontRenderMode) -> dwrote::DWRITE_TEXTURE_TYPE {
    match render_mode {
        FontRenderMode::Mono => dwrote::DWRITE_TEXTURE_ALIASED_1x1,
        FontRenderMode::Alpha |
        FontRenderMode::Subpixel => dwrote::DWRITE_TEXTURE_CLEARTYPE_3x1,
    }
}

fn dwrite_measure_mode(
    font: &FontInstance,
    bitmaps: bool,
) -> dwrote::DWRITE_MEASURING_MODE {
    if bitmaps || font.flags.contains(FontInstanceFlags::FORCE_GDI) {
        dwrote::DWRITE_MEASURING_MODE_GDI_CLASSIC
    } else {
      match font.render_mode {
          FontRenderMode::Mono => dwrote::DWRITE_MEASURING_MODE_GDI_CLASSIC,
          FontRenderMode::Alpha | FontRenderMode::Subpixel => dwrote::DWRITE_MEASURING_MODE_NATURAL,
      }
    }
}

fn dwrite_render_mode(
    font_face: &dwrote::FontFace,
    font: &FontInstance,
    em_size: f32,
    measure_mode: dwrote::DWRITE_MEASURING_MODE,
    bitmaps: bool,
) -> dwrote::DWRITE_RENDERING_MODE {
    let dwrite_render_mode = match font.render_mode {
        FontRenderMode::Mono => dwrote::DWRITE_RENDERING_MODE_ALIASED,
        FontRenderMode::Alpha | FontRenderMode::Subpixel => {
            if bitmaps || font.flags.contains(FontInstanceFlags::FORCE_GDI) {
                dwrote::DWRITE_RENDERING_MODE_GDI_CLASSIC
            } else if font.flags.contains(FontInstanceFlags::FORCE_SYMMETRIC) {
                dwrote::DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC
            } else if font.flags.contains(FontInstanceFlags::NO_SYMMETRIC) {
                dwrote::DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL
            } else {
                font_face.get_recommended_rendering_mode_default_params(em_size, 1.0, measure_mode)
            }
        }
    };

    if dwrite_render_mode == dwrote::DWRITE_RENDERING_MODE_OUTLINE {
        // Outline mode is not supported
        return dwrote::DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC;
    }

    dwrite_render_mode
}

fn is_bitmap_font(font: &FontInstance) -> bool {
    // If bitmaps are requested, then treat as a bitmap font to disable transforms.
    // If mono AA is requested, let that take priority over using bitmaps.
    font.render_mode != FontRenderMode::Mono &&
        font.flags.contains(FontInstanceFlags::EMBEDDED_BITMAPS)
}

/// One rasterized layer of a color glyph. DirectWrite exposes `COLR`/`CPAL`
/// fonts as a list of monochrome glyph runs, one per colored layer, that have
/// to be composited in order to get the final image.
struct ColorLayer {
    analysis: dwrote::GlyphRunAnalysis,
    texture_type: dwrote::DWRITE_TEXTURE_TYPE,
    bounds: dwrote::RECT,
    color: [f32; 4],
}

fn color_layers_bounds(layers: &[ColorLayer]) -> dwrote::RECT {
    let mut bounds = dwrote::RECT {
        left: i32::MAX,
        top: i32::MAX,
        right: i32::MIN,
        bottom: i32::MIN,
    };
    for layer in layers {
        bounds.left = bounds.left.min(layer.bounds.left);
        bounds.top = bounds.top.min(layer.bounds.top);
        bounds.right = bounds.right.max(layer.bounds.right);
        bounds.bottom = bounds.bottom.max(layer.bounds.bottom);
    }
    bounds
}

/// Creates the glyph run analysis for a single color layer. Color layers are
/// rendered with grayscale antialiasing when the font instance asks for it,
/// since ClearType fringes would be lost when the layer is tinted with a single
/// color.
fn create_color_layer_analysis(
    glyph_run: &dwrote::DWRITE_GLYPH_RUN,
    font: &FontInstance,
    baseline_x: f32,
    baseline_y: f32,
) -> Option<(dwrote::GlyphRunAnalysis, dwrote::DWRITE_TEXTURE_TYPE, dwrote::RECT)> {
    let (render_mode, texture_type, measuring_mode) = if font.render_mode == FontRenderMode::Mono {
        (
            dwrote::DWRITE_RENDERING_MODE_ALIASED,
            dwrote::DWRITE_TEXTURE_ALIASED_1x1,
            dwrote::DWRITE_MEASURING_MODE_GDI_CLASSIC,
        )
    } else {
        (
            dwrote::DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,
            dwrote::DWRITE_TEXTURE_CLEARTYPE_3x1,
            dwrote::DWRITE_MEASURING_MODE_NATURAL,
        )
    };
    let analysis = dwrote::GlyphRunAnalysis::create(
        glyph_run,
        1.0,
        None,
        render_mode,
        measuring_mode,
        baseline_x,
        baseline_y,
    )
    .ok()?;
    let bounds = analysis.get_alpha_texture_bounds(texture_type).ok()?;
    if bounds.right > bounds.left && bounds.bottom > bounds.top {
        return Some((analysis, texture_type, bounds));
    }

    // Empty color layers are legal (a layer can be fully transparent), but if
    // even the aliased coverage is empty there is nothing to draw for it.
    if texture_type == dwrote::DWRITE_TEXTURE_ALIASED_1x1 {
        return None;
    }
    let analysis = dwrote::GlyphRunAnalysis::create(
        glyph_run,
        1.0,
        None,
        dwrote::DWRITE_RENDERING_MODE_ALIASED,
        dwrote::DWRITE_MEASURING_MODE_GDI_CLASSIC,
        baseline_x,
        baseline_y,
    )
    .ok()?;
    let bounds = analysis.get_alpha_texture_bounds(dwrote::DWRITE_TEXTURE_ALIASED_1x1).ok()?;
    if bounds.right > bounds.left && bounds.bottom > bounds.top {
        Some((analysis, dwrote::DWRITE_TEXTURE_ALIASED_1x1, bounds))
    } else {
        None
    }
}

struct ColorGlyphFactory(*mut IDWriteFactory2);

// `IDWriteFactory2` is a thread-safe, reference counted COM object.
unsafe impl Send for ColorGlyphFactory {}
unsafe impl Sync for ColorGlyphFactory {}

fn create_color_glyph_factory() -> Option<ColorGlyphFactory> {
    unsafe {
        let mut factory: *mut IDWriteFactory = ptr::null_mut();
        let hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            &IDWriteFactory::uuidof(),
            &mut factory as *mut _ as *mut *mut IUnknown,
        );
        if hr != S_OK || factory.is_null() {
            return None;
        }

        let mut factory2: *mut IDWriteFactory2 = ptr::null_mut();
        let hr = (*factory).QueryInterface(
            &IDWriteFactory2::uuidof(),
            &mut factory2 as *mut _ as *mut *mut c_void,
        );
        (*factory).Release();
        if hr != S_OK || factory2.is_null() {
            None
        } else {
            Some(ColorGlyphFactory(factory2))
        }
    }
}

lazy_static! {
    static ref COLOR_GLYPH_FACTORY: Option<ColorGlyphFactory> = create_color_glyph_factory();
}

impl FontContext {
    pub fn distribute_across_threads() -> bool {
        true
    }

    pub fn new() -> FontContext {
        FontContext {
            fonts: FastHashMap::default(),
            variations: FastHashMap::default(),
            gamma_luts: FastHashMap::default(),
        }
    }

    fn add_font_descriptor(&mut self, font_key: &FontKey, desc: &dwrote::FontDescriptor) {
        let system_fc = dwrote::FontCollection::get_system(false);
        if let Ok(font) = system_fc.font_from_descriptor(desc) {
            if let Some(font) = font {
                let face = font.create_font_face();
                if let Ok(mut files) = face.files() {
                    if let Some(file) = files.pop() {
                        let index = face.get_index();
                        self.fonts.insert(*font_key, FontFace { cached: None, file, index, face });
                    }
                }
            }
        }
    }

    pub fn add_raw_font(&mut self, font_key: &FontKey, data: Arc<Vec<u8>>, index: u32) {
        if self.fonts.contains_key(font_key) {
            return;
        }

        if let Some(file) = dwrote::FontFile::new_from_buffer(data) {
            if let Ok(face) = file.create_face(index, dwrote::DWRITE_FONT_SIMULATIONS_NONE) {
                self.fonts.insert(*font_key, FontFace { cached: None, file, index, face });
                return;
            }
        }
        // XXX add_raw_font needs to have a way to return an error
        debug!("DWrite WR failed to load font from data, using Arial instead");
        self.add_font_descriptor(font_key, &DEFAULT_FONT_DESCRIPTOR);
    }

    /// Whether the font described by `template` contains embedded bitmap
    /// strikes. On Windows the `EMBEDDED_BITMAPS` instance flag is set by Gecko
    /// only for fonts that may have color glyphs (see `is_bitmap_font`), so the
    /// per-instance flag is authoritative and this can defer to it without
    /// loading the face.
    pub fn has_bitmap_strikes(_template: &FontTemplate) -> bool {
        true
    }

    pub fn add_native_font(&mut self, font_key: &FontKey, font_handle: NativeFontHandle) {
        if self.fonts.contains_key(font_key) {
            return;
        }

        let index = font_handle.index;
        let mut cache = FONT_CACHE.lock().unwrap();
        // Check to see if the font is already in the cache. If so, reuse it.
        if let Some(font) = cache.get(font_handle.path.as_path()) {
            if let Ok(face) = font.file.create_face(index, dwrote::DWRITE_FONT_SIMULATIONS_NONE) {
                self.fonts.insert(
                    *font_key,
                    FontFace { cached: Some(font.key.clone()), file: font.file.clone(), index, face },
                );
                return;
            }
        }
        if let Some(file) = dwrote::FontFile::new_from_path(&font_handle.path) {
            // The font is not in the cache yet, so try to create the font and insert it in the cache.
            if let Ok(face) = file.create_face(index, dwrote::DWRITE_FONT_SIMULATIONS_NONE) {
                let key: CachedFontKey = font_handle.path.into();
                self.fonts.insert(
                    *font_key,
                    FontFace { cached: Some(key.clone()), file: file.clone(), index, face },
                );
                cache.insert(CachedFont { key, file });
                return;
            }
        }

        // XXX add_native_font needs to have a way to return an error
        debug!("DWrite WR failed to load font from path, using Arial instead");
        self.add_font_descriptor(font_key, &DEFAULT_FONT_DESCRIPTOR);
    }

    pub fn delete_font(&mut self, font_key: &FontKey) {
        if let Some(face) = self.fonts.remove(font_key) {
            self.variations.retain(|k, _| k.0 != *font_key);
            // Check if this was a cached font.
            if let Some(key) = face.cached {
                let mut cache = FONT_CACHE.lock().unwrap();
                // If there are only two references left, that means only this face and
                // the cache are using the font. So remove it from the cache.
                if Arc::strong_count(&key) == 2 {
                    cache.remove(&*key);
                }
            }
        }
    }

    pub fn delete_font_instance(&mut self, instance: &FontInstance) {
        // Ensure we don't keep around excessive amounts of stale variations.
        if !instance.variations.is_empty() {
            let sims = if instance.flags.contains(FontInstanceFlags::SYNTHETIC_BOLD) {
                dwrote::DWRITE_FONT_SIMULATIONS_BOLD
            } else {
                dwrote::DWRITE_FONT_SIMULATIONS_NONE
            };
            self.variations.remove(&(instance.font_key, sims, instance.variations.clone()));
        }
    }

    // Assumes RGB format from dwrite, which is 3 bytes per pixel as dwrite
    // doesn't output an alpha value via GlyphRunAnalysis::CreateAlphaTexture
    #[allow(dead_code)]
    fn print_glyph_data(&self, data: &[u8], width: usize, height: usize) {
        // Rust doesn't have step_by support on stable :(
        for i in 0 .. height {
            let current_height = i * width * 3;

            for pixel in data[current_height .. current_height + (width * 3)].chunks(3) {
                let r = pixel[0];
                let g = pixel[1];
                let b = pixel[2];
                debug!("({}, {}, {}) ", r, g, b,);
            }
        }
    }

    fn get_font_face(
        &mut self,
        font: &FontInstance,
    ) -> &dwrote::FontFace {
        if !font.flags.contains(FontInstanceFlags::SYNTHETIC_BOLD) &&
           font.variations.is_empty() {
            return &self.fonts.get(&font.font_key).unwrap().face;
        }
        let sims = if font.flags.contains(FontInstanceFlags::SYNTHETIC_BOLD) {
            dwrote::DWRITE_FONT_SIMULATIONS_BOLD
        } else {
            dwrote::DWRITE_FONT_SIMULATIONS_NONE
        };
        match self.variations.entry((font.font_key, sims, font.variations.clone())) {
            Entry::Occupied(entry) => entry.into_mut(),
            Entry::Vacant(entry) => {
                let normal_face = self.fonts.get(&font.font_key).unwrap();
                if !font.variations.is_empty() {
                    if let Some(var_face) = normal_face.face.create_font_face_with_variations(
                        sims,
                        &font.variations.iter().map(|var| {
                            dwrote::DWRITE_FONT_AXIS_VALUE {
                                // OpenType tags are big-endian, but DWrite wants little-endian.
                                axisTag: var.tag.swap_bytes(),
                                value: var.value,
                            }
                        }).collect::<Vec<_>>(),
                    ) {
                        return entry.insert(var_face);
                    }
                }
                let var_face = normal_face.file
                    .create_face(normal_face.index, sims)
                    .unwrap_or_else(|_| normal_face.face.clone());
                entry.insert(var_face)
            }
        }
    }

    fn create_glyph_analysis(
        &mut self,
        font: &FontInstance,
        key: &GlyphKey,
        size: f32,
        transform: Option<dwrote::DWRITE_MATRIX>,
        bitmaps: bool,
    ) -> Result<(dwrote::GlyphRunAnalysis, dwrote::DWRITE_TEXTURE_TYPE, dwrote::RECT), dwrote::HRESULT> {
        let face = self.get_font_face(font);
        let glyph = key.index() as u16;
        let advance = 0.0f32;
        let offset = dwrote::GlyphOffset {
            advanceOffset: 0.0,
            ascenderOffset: 0.0,
        };

        let glyph_run = dwrote::DWRITE_GLYPH_RUN {
            fontFace: unsafe { face.as_ptr() },
            fontEmSize: size, // size in DIPs (1/96", same as CSS pixels)
            glyphCount: 1,
            glyphIndices: &glyph,
            glyphAdvances: &advance,
            glyphOffsets: &offset,
            isSideways: 0,
            bidiLevel: 0,
        };

        let dwrite_measure_mode = dwrite_measure_mode(font, bitmaps);
        let dwrite_render_mode = dwrite_render_mode(
            face,
            font,
            size,
            dwrite_measure_mode,
            bitmaps,
        );

        let analysis = dwrote::GlyphRunAnalysis::create(
            &glyph_run,
            1.0,
            transform,
            dwrite_render_mode,
            dwrite_measure_mode,
            0.0,
            0.0,
        )?;
        let texture_type = dwrite_texture_type(font.render_mode);
        let bounds = analysis.get_alpha_texture_bounds(texture_type)?;
        // If the bounds are empty, then we might not be able to render the glyph with cleartype.
        // Try again with aliased rendering to check if that works instead.
        if font.render_mode != FontRenderMode::Mono &&
           (bounds.left == bounds.right || bounds.top == bounds.bottom) {
            let analysis2 = dwrote::GlyphRunAnalysis::create(
                &glyph_run,
                1.0,
                transform,
                dwrote::DWRITE_RENDERING_MODE_ALIASED,
                dwrite_measure_mode,
                0.0,
                0.0,
            )?;
            let bounds2 = analysis2.get_alpha_texture_bounds(dwrote::DWRITE_TEXTURE_ALIASED_1x1)?;
            if bounds2.left != bounds2.right && bounds2.top != bounds2.bottom {
                return Ok((analysis2, dwrote::DWRITE_TEXTURE_ALIASED_1x1, bounds2));
            }
        }
        Ok((analysis, texture_type, bounds))
    }

    /// Collects the rasterized `COLR` layers of a color glyph. Returns `None`
    /// for ordinary glyphs (DirectWrite reports `DWRITE_E_NOCOLOR`), in which
    /// case the regular outline rasterization path is used.
    fn color_glyph_layers(
        &mut self,
        font: &FontInstance,
        key: &GlyphKey,
        size: f32,
    ) -> Option<Vec<ColorLayer>> {
        let factory = COLOR_GLYPH_FACTORY.as_ref()?;
        let font_face = self.get_font_face(font);
        let face = unsafe { font_face.as_ptr() };
        let glyph = key.index() as u16;
        let advance = 0.0f32;
        let offset = dwrote::GlyphOffset {
            advanceOffset: 0.0,
            ascenderOffset: 0.0,
        };
        let glyph_run = dwrote::DWRITE_GLYPH_RUN {
            fontFace: face,
            fontEmSize: size,
            glyphCount: 1,
            glyphIndices: &glyph,
            glyphAdvances: &advance,
            glyphOffsets: &offset,
            isSideways: 0,
            bidiLevel: 0,
        };

        let mut enumerator: *mut IDWriteColorGlyphRunEnumerator = ptr::null_mut();
        let hr = unsafe {
            (*factory.0).TranslateColorGlyphRun(
                0.0,
                0.0,
                &glyph_run,
                ptr::null(),
                dwrote::DWRITE_MEASURING_MODE_NATURAL,
                ptr::null(),
                0,
                &mut enumerator,
            )
        };
        if hr != S_OK || enumerator.is_null() {
            return None;
        }

        let mut layers = Vec::new();
        loop {
            let mut has_run = 0;
            let hr = unsafe { (*enumerator).MoveNext(&mut has_run) };
            if hr != S_OK || has_run == 0 {
                break;
            }

            let mut color_run: *const DWRITE_COLOR_GLYPH_RUN = ptr::null();
            let hr = unsafe { (*enumerator).GetCurrentRun(&mut color_run) };
            if hr != S_OK || color_run.is_null() {
                break;
            }

            let color_run = unsafe { &*color_run };
            let run = &color_run.glyphRun;
            if run.glyphCount == 0 ||
                run.glyphIndices.is_null() ||
                run.glyphAdvances.is_null()
            {
                continue;
            }

            // The enumerator only guarantees the glyph arrays until the next
            // call to `MoveNext`, so the layer analysis has to be created from
            // owned copies.
            let count = run.glyphCount as usize;
            let glyph_indices = unsafe { slice::from_raw_parts(run.glyphIndices, count) }.to_vec();
            let glyph_advances = unsafe { slice::from_raw_parts(run.glyphAdvances, count) }.to_vec();
            let glyph_offsets = if run.glyphOffsets.is_null() {
                vec![
                    dwrote::GlyphOffset {
                        advanceOffset: 0.0,
                        ascenderOffset: 0.0,
                    };
                    count
                ]
            } else {
                unsafe { slice::from_raw_parts(run.glyphOffsets, count) }.to_vec()
            };
            let layer_run = dwrote::DWRITE_GLYPH_RUN {
                fontFace: face,
                fontEmSize: run.fontEmSize,
                glyphCount: run.glyphCount,
                glyphIndices: glyph_indices.as_ptr(),
                glyphAdvances: glyph_advances.as_ptr(),
                glyphOffsets: glyph_offsets.as_ptr(),
                isSideways: run.isSideways,
                bidiLevel: run.bidiLevel,
            };
            let (analysis, texture_type, bounds) = match create_color_layer_analysis(
                &layer_run,
                font,
                color_run.baselineOriginX,
                color_run.baselineOriginY,
            ) {
                Some(layer) => layer,
                None => continue,
            };

            let color = color_run.runColor;
            layers.push(ColorLayer {
                analysis,
                texture_type,
                bounds,
                color: [color.r, color.g, color.b, color.a],
            });
        }
        unsafe { (*enumerator).Release() };

        if layers.is_empty() { None } else { Some(layers) }
    }

    fn color_glyph_bounds(
        &mut self,
        font: &FontInstance,
        key: &GlyphKey,
        size: f32,
    ) -> Option<dwrote::RECT> {
        let layers = self.color_glyph_layers(font, key, size)?;
        Some(color_layers_bounds(&layers))
    }

    /// Composites the layers of a color glyph into a premultiplied BGRA bitmap,
    /// which WebRender samples as `GlyphFormat::ColorBitmap`.
    fn rasterize_color_glyph(
        &mut self,
        font: &FontInstance,
        key: &GlyphKey,
        size: f32,
        y_scale: f64,
    ) -> Option<RasterizedGlyph> {
        let layers = self.color_glyph_layers(font, key, size)?;
        let bounds = color_layers_bounds(&layers);
        let width = bounds.right - bounds.left;
        let height = bounds.bottom - bounds.top;
        if width <= 0 || height <= 0 {
            return None;
        }

        let padding = if font.use_texture_padding() { 1 } else { 0 };
        let buffer_width = width as usize + padding * 2;
        let buffer_height = height as usize + padding * 2;
        let mut pixels = vec![0u8; buffer_width * buffer_height * 4];
        let text_alpha = font.color.a as f32 / 255.0;

        for layer in &layers {
            let rect = layer.bounds;
            let layer_width = (rect.right - rect.left) as usize;
            let layer_height = (rect.bottom - rect.top) as usize;
            let coverage = layer.analysis.create_alpha_texture(layer.texture_type, rect).ok()?;
            let channels = if layer.texture_type == dwrote::DWRITE_TEXTURE_ALIASED_1x1 {
                1
            } else {
                3
            };
            if coverage.len() < layer_width * layer_height * channels {
                continue;
            }

            let dest_x = (rect.left - bounds.left) as usize + padding;
            let dest_y = (rect.top - bounds.top) as usize + padding;
            let [red, green, blue, alpha] = layer.color;
            let layer_alpha = alpha * text_alpha;
            if layer_alpha <= 0.0 {
                continue;
            }

            for row in 0..layer_height {
                for col in 0..layer_width {
                    let index = (row * layer_width + col) * channels;
                    let coverage_norm = if channels == 1 {
                        coverage[index] as f32 / 255.0
                    } else {
                        (coverage[index] as f32 +
                            coverage[index + 1] as f32 +
                            coverage[index + 2] as f32) /
                            (3.0 * 255.0)
                    };
                    // Straight alpha: source color is scaled into 0..255 so that
                    // source-over compositing works on premultiplied bytes.
                    let source_alpha_norm = coverage_norm * layer_alpha;
                    if source_alpha_norm <= 0.0 {
                        continue;
                    }

                    let dest = ((dest_y + row) * buffer_width + dest_x + col) * 4;
                    let inverse = 1.0 - source_alpha_norm;
                    let blend = |source: f32, dest: u8| -> u8 {
                        ((source * source_alpha_norm) + dest as f32 * inverse)
                            .round()
                            .clamp(0.0, 255.0) as u8
                    };
                    pixels[dest] = blend(blue * 255.0, pixels[dest]);
                    pixels[dest + 1] = blend(green * 255.0, pixels[dest + 1]);
                    pixels[dest + 2] = blend(red * 255.0, pixels[dest + 2]);
                    pixels[dest + 3] = blend(255.0, pixels[dest + 3]);
                }
            }
        }

        Some(RasterizedGlyph {
            left: (bounds.left - padding as i32) as f32,
            top: (-bounds.top + padding as i32) as f32,
            width: width + padding as i32 * 2,
            height: height + padding as i32 * 2,
            scale: y_scale.recip() as f32,
            format: GlyphFormat::ColorBitmap,
            bytes: pixels,
            is_packed_glyph: false,
        })
    }

    pub fn get_glyph_index(&mut self, font_key: FontKey, ch: char) -> Option<u32> {
        let face = &self.fonts.get(&font_key).unwrap().face;
        if let Ok(indices) = face.glyph_indices(&[ch as u32]) {
            return indices.first().map(|idx| *idx as u32);
        }
        None
    }

    pub fn get_glyph_dimensions(
        &mut self,
        font: &FontInstance,
        key: &GlyphKey,
    ) -> Option<GlyphDimensions> {
        let (size, x_scale, y_scale, bitmaps, transform) = Self::get_glyph_parameters(font, key);
        // Color glyphs (COLR/CPAL) must be measured from the union of their
        // layers; the base glyph outline only describes the monochrome
        // fallback and is not what gets drawn.
        let bounds = if bitmaps {
            match self.color_glyph_bounds(font, key, size) {
                Some(bounds) => bounds,
                None => self.create_glyph_analysis(font, key, size, transform, bitmaps).ok()?.2,
            }
        } else {
            self.create_glyph_analysis(font, key, size, transform, bitmaps).ok()?.2
        };

        let width = (bounds.right - bounds.left) as i32;
        let height = (bounds.bottom - bounds.top) as i32;

        // Alpha texture bounds can sometimes return an empty rect
        // Such as for spaces
        if width == 0 || height == 0 {
            return None;
        }

        let (strike_scale, pixel_step) = if bitmaps {
            (y_scale, 1.0)
        } else {
            (x_scale, y_scale / x_scale)
        };
        let extra_strikes = font.get_extra_strikes(FontInstanceFlags::MULTISTRIKE_BOLD, strike_scale);
        let extra_width = extra_strikes as f64 * pixel_step;

        let face = self.get_font_face(font);
        if let Ok(metrics) = face.design_glyph_metrics(&[key.index() as u16], false) {
            return metrics
                .first()
                .map(|metrics| {
                    let em_size = size / 16.;
                    let design_units_per_pixel = face.metrics().metrics0().designUnitsPerEm as f32 / 16. as f32;
                    let scaled_design_units_to_pixels = em_size / design_units_per_pixel;
                    let advance = metrics.advanceWidth as f32 * scaled_design_units_to_pixels;

                    GlyphDimensions {
                        left: bounds.left,
                        top: -bounds.top,
                        width: width + extra_width.ceil() as i32,
                        height,
                        advance: advance + extra_width as f32,
                    }
                });
        }

        None
    }

    // DWrite ClearType gives us values in RGB, but WR expects BGRA.
    fn convert_to_bgra(
        &self,
        pixels: &[u8],
        width: usize,
        height: usize,
        texture_type: dwrote::DWRITE_TEXTURE_TYPE,
        render_mode: FontRenderMode,
        bitmaps: bool,
        subpixel_bgr: bool,
        padding: usize,
    ) -> (Vec<u8>, bool) {
        let (buffer_width, buffer_height) = (width + padding * 2, height + padding * 2);
        let buffer_length = buffer_width * buffer_height * 4;
        let mut bgra_pixels: Vec<u8> = vec![0; buffer_length];

        match (texture_type, render_mode, bitmaps) {
            (dwrote::DWRITE_TEXTURE_ALIASED_1x1, _, _) => {
                assert!(width * height == pixels.len());
                let mut i = 0;
                for row in padding .. height + padding {
                    let row_offset = row * buffer_width;
                    for col in padding .. width + padding {
                        let offset = (row_offset + col) * 4;
                        let alpha = pixels[i];
                        i += 1;
                        bgra_pixels[offset + 0] = alpha;
                        bgra_pixels[offset + 1] = alpha;
                        bgra_pixels[offset + 2] = alpha;
                        bgra_pixels[offset + 3] = alpha;
                    }
                }
                (bgra_pixels, false)
            }
            (_, FontRenderMode::Subpixel, false) => {
                assert!(width * height * 3 == pixels.len());
                let mut i = 0;
                for row in padding .. height + padding {
                    let row_offset = row * buffer_width;
                    for col in padding .. width + padding {
                        let offset = (row_offset + col) * 4;
                        let (mut r, g, mut b) = (pixels[i + 0], pixels[i + 1], pixels[i + 2]);
                        if subpixel_bgr {
                            mem::swap(&mut r, &mut b);
                        }
                        i += 3;
                        bgra_pixels[offset + 0] = b;
                        bgra_pixels[offset + 1] = g;
                        bgra_pixels[offset + 2] = r;
                        bgra_pixels[offset + 3] = 0xff;
                    }
                }
                (bgra_pixels, true)
            }
            _ => {
                assert!(width * height * 3 == pixels.len());
                let mut i = 0;
                for row in padding .. height + padding {
                    let row_offset = row * buffer_width;
                    for col in padding .. width + padding {
                        let offset = (row_offset + col) * 4;
                        // Only take the G channel, as its closest to D2D
                        let alpha = pixels[i + 1] as u8;
                        i += 3;
                        bgra_pixels[offset + 0] = alpha;
                        bgra_pixels[offset + 1] = alpha;
                        bgra_pixels[offset + 2] = alpha;
                        bgra_pixels[offset + 3] = alpha;
                    }
                }
                (bgra_pixels, false)
            }
        }
    }

    pub fn prepare_font(font: &mut FontInstance) {
        match font.render_mode {
            FontRenderMode::Mono => {
                // In mono mode the color of the font is irrelevant.
                font.color = ColorU::new(255, 255, 255, 255);
                // Subpixel positioning is disabled in mono mode.
                font.disable_subpixel_position();
            }
            FontRenderMode::Alpha => {
                font.color = font.color.luminance_color().quantize();
            }
            FontRenderMode::Subpixel => {
                font.color = font.color.quantize();
            }
        }
    }

    fn get_glyph_parameters(font: &FontInstance, key: &GlyphKey)
                            -> (f32, f64, f64, bool, Option<dwrote::DWRITE_MATRIX>) {
        let (x_scale, y_scale) = font.transform.compute_scale().unwrap_or((1.0, 1.0));
        let scaled_size = font.size.to_f64_px() * y_scale;
        let bitmaps = is_bitmap_font(font);
        let (mut shape, (mut x_offset, mut y_offset)) = if bitmaps {
            (FontTransform::identity(), (0.0, 0.0))
        } else {
            (font.transform.invert_scale(y_scale, y_scale), font.get_subpx_offset(key))
        };
        if font.flags.contains(FontInstanceFlags::FLIP_X) {
            shape = shape.flip_x();
        }
        if font.flags.contains(FontInstanceFlags::FLIP_Y) {
            shape = shape.flip_y();
        }
        if font.flags.contains(FontInstanceFlags::TRANSPOSE) {
            shape = shape.swap_xy();
        }
        let (mut tx, mut ty) = (0.0, 0.0);
        if font.synthetic_italics.is_enabled() {
            let (shape_, (tx_, ty_)) = font.synthesize_italics(shape, scaled_size);
            shape = shape_;
            tx = tx_;
            ty = ty_;
        };
        x_offset += tx;
        y_offset += ty;
        let transform = if !shape.is_identity() || (x_offset, y_offset) != (0.0, 0.0) {
            Some(dwrote::DWRITE_MATRIX {
                m11: shape.scale_x,
                m12: shape.skew_y,
                m21: shape.skew_x,
                m22: shape.scale_y,
                dx: x_offset as f32,
                dy: y_offset as f32,
            })
        } else {
            None
        };
        (scaled_size as f32, x_scale, y_scale, bitmaps, transform)
    }

    pub fn begin_rasterize(_font: &FontInstance) {
    }

    pub fn end_rasterize(_font: &FontInstance) {
    }

    pub fn rasterize_glyph(&mut self, font: &FontInstance, key: &GlyphKey) -> GlyphRasterResult {
        let (size, x_scale, y_scale, bitmaps, transform) = Self::get_glyph_parameters(font, key);
        // Color fonts are composited from their COLR layers; rasterizing the
        // base glyph would only produce the colorless fallback outlines.
        if bitmaps {
            if let Some(glyph) = self.rasterize_color_glyph(font, key, size, y_scale) {
                return Ok(glyph);
            }
        }
        let (analysis, texture_type, bounds) = self.create_glyph_analysis(font, key, size, transform, bitmaps)
                                                   .or(Err(GlyphRasterError::LoadFailed))?;
        let mut width = (bounds.right - bounds.left) as i32;
        let height = (bounds.bottom - bounds.top) as i32;
        // Alpha texture bounds can sometimes return an empty rect
        // Such as for spaces
        if width == 0 || height == 0 {
            return Err(GlyphRasterError::LoadFailed);
        }

        let pixels = analysis.create_alpha_texture(texture_type, bounds).or(Err(GlyphRasterError::LoadFailed))?;
        let padding = if font.use_texture_padding() { 1 } else { 0 };
        let (mut bgra_pixels, is_subpixel) = self.convert_to_bgra(
            &pixels,
            width as usize,
            height as usize,
            texture_type,
            font.render_mode,
            bitmaps,
            font.flags.contains(FontInstanceFlags::SUBPIXEL_BGR),
            padding as usize,
        );

        // Apply multistrike bold, if necessary, and replace the current pixels with it.
        let (strike_scale, pixel_step) = if bitmaps {
            (y_scale, 1.0)
        } else {
            (x_scale, y_scale / x_scale)
        };
        let extra_strikes = font.get_extra_strikes(FontInstanceFlags::MULTISTRIKE_BOLD, strike_scale);
        if extra_strikes > 0 {
            let (bold_pixels, bold_width) = apply_multistrike_bold(
                &bgra_pixels,
                (width + padding * 2) as usize,
                (height + padding * 2) as usize,
                is_subpixel,
                extra_strikes,
                pixel_step,
            );
            width = bold_width as i32 - padding * 2;
            bgra_pixels = bold_pixels;
        }

        let FontInstancePlatformOptions { gamma, contrast, cleartype_level, .. } =
            font.platform_options.unwrap_or_default();
        let gamma_lut = self.gamma_luts
            .entry((gamma, contrast))
            .or_insert_with(||
                GammaLut::new(
                    contrast as f32 / 100.0,
                    gamma as f32 / 100.0,
                    gamma as f32 / 100.0,
                ));
        if is_subpixel {
            gamma_lut.preblend_scaled(&mut bgra_pixels, font.color, cleartype_level);
        } else {
            gamma_lut.preblend(&mut bgra_pixels, font.color);
        }

        let format = if bitmaps {
            GlyphFormat::Bitmap
        } else if texture_type == dwrote::DWRITE_TEXTURE_ALIASED_1x1 {
            font.get_alpha_glyph_format()
        } else {
            font.get_glyph_format()
        };

        Ok(RasterizedGlyph {
            left: (bounds.left - padding) as f32,
            top: (-bounds.top + padding) as f32,
            width: width + padding * 2,
            height: height + padding * 2,
            scale: (if bitmaps { y_scale.recip() } else { 1.0 }) as f32,
            format,
            bytes: bgra_pixels,
            is_packed_glyph: false,
        })
    }
}

#[cfg(test)]
mod color_glyph_tests {
    use super::*;
    use crate::rasterizer::{BaseFontInstance, FontInstance, GlyphKey, SubpixelDirection};
    use api::units::DevicePoint;
    use api::{FontInstanceFlags, FontInstanceKey, FontKey, FontRenderMode, IdNamespace};
    use std::sync::Arc;

    fn write_preview(glyph: &RasterizedGlyph) -> std::path::PathBuf {
        let width = glyph.width as usize;
        let height = glyph.height as usize;
        let row_size = (width * 3 + 3) & !3;
        let pixel_data = row_size * height;
        let mut out = vec![0u8; 54 + pixel_data];
        out[0] = b'B';
        out[1] = b'M';
        out[2..6].copy_from_slice(&((54 + pixel_data) as u32).to_le_bytes());
        out[10..14].copy_from_slice(&54u32.to_le_bytes());
        out[14..18].copy_from_slice(&40u32.to_le_bytes());
        out[18..22].copy_from_slice(&(width as i32).to_le_bytes());
        out[22..26].copy_from_slice(&(height as i32).to_le_bytes());
        out[26..28].copy_from_slice(&1u16.to_le_bytes());
        out[28..30].copy_from_slice(&24u16.to_le_bytes());
        out[34..38].copy_from_slice(&(pixel_data as u32).to_le_bytes());
        out[38..42].copy_from_slice(&2835i32.to_le_bytes());
        out[42..46].copy_from_slice(&2835i32.to_le_bytes());

        for y in 0..height {
            let src_row = height - 1 - y;
            for x in 0..width {
                let pixel = &glyph.bytes[(src_row * width + x) * 4..][..4];
                let (b, g, r, a) = (
                    pixel[0] as u32,
                    pixel[1] as u32,
                    pixel[2] as u32,
                    pixel[3] as u32,
                );
                let background = if ((x / 4) + (y / 4)) % 2 == 0 { 235 } else { 205 };
                let inverse = 255 - a;
                let dst = 54 + y * row_size + x * 3;
                out[dst] = (b + background * inverse / 255) as u8;
                out[dst + 1] = (g + background * inverse / 255) as u8;
                out[dst + 2] = (r + background * inverse / 255) as u8;
            }
        }

        std::fs::create_dir_all("target").expect("create preview directory");
        let path = std::path::Path::new("target").join("emoji-preview.bmp");
        std::fs::write(&path, out).expect("write emoji preview");
        path
    }

    #[test]
    fn emoji_is_rasterized_as_a_color_bitmap() {
        let Some(data) = std::fs::read("C:\\Windows\\Fonts\\seguiemj.ttf")
            .ok()
            .map(Arc::new)
        else {
            return;
        };

        let namespace = IdNamespace(1);
        let font_key = FontKey::new(namespace, 1);
        let instance_key = FontInstanceKey::new(namespace, 1);

        let mut context = FontContext::new();
        context.add_raw_font(&font_key, data, 0);

        let base = Arc::new(BaseFontInstance::new(
            instance_key,
            font_key,
            48.0,
            None,
            None,
            Vec::new(),
        ));
        let font = FontInstance::new(
            base,
            ColorU::new(0, 0, 0, 255),
            FontRenderMode::Subpixel,
            FontInstanceFlags::EMBEDDED_BITMAPS,
        );

        let index = context
            .get_glyph_index(font_key, '\u{1F600}')
            .expect("Segoe UI Emoji should contain U+1F600");
        let key = GlyphKey::new(index, DevicePoint::new(0.0, 0.0), SubpixelDirection::None);
        let glyph = context
            .rasterize_glyph(&font, &key)
            .expect("emoji rasterization should succeed");

        assert!(
            matches!(glyph.format, GlyphFormat::ColorBitmap),
            "emoji must use the color bitmap path"
        );
        assert!(glyph.width > 0 && glyph.height > 0);
        assert_eq!(
            glyph.bytes.len(),
            (glyph.width * glyph.height * 4) as usize,
            "color glyphs are premultiplied BGRA"
        );

        let mut colors = FastHashSet::default();
        for pixel in glyph.bytes.chunks_exact(4) {
            if pixel[3] != 0 {
                colors.insert([pixel[2], pixel[1], pixel[0]]);
            }
        }
        assert!(
            colors.len() > 3,
            "expected multiple COLR layer colors, found {colors:?}"
        );

        let preview = write_preview(&glyph);
        eprintln!(
            "rasterized U+1F600 at {}x{} with {} distinct colors into {}",
            glyph.width,
            glyph.height,
            colors.len(),
            preview.display()
        );
    }
}
