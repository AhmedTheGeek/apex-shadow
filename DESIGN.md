---
name: Apex Shadow
description: A Pebble watchface where the shadow tells the hour and a hidden layer reveals the minutes.
colors:
  curtain-black: "#000000"
  stage-light: "#ffffff"
typography:
  display:
    fontFamily: "Apex Segment, a custom 5x9 pseudo-seven-segment bitmap"
    fontSize: "36px"
    fontWeight: 400
    lineHeight: 1
    letterSpacing: "4px"
spacing:
  hair: "1px"
  edge-lip: "1px"
  margin: "5px"
  wedge-near-gap: "9px"
  shadow-core: "4px"
  shadow-reach: "10px"
  camera-pull-near: "14px"
  camera-pull-far: "34px"
components:
  light-face:
    backgroundColor: "{colors.stage-light}"
    textColor: "{colors.curtain-black}"
  shadow-wedge:
    backgroundColor: "{colors.curtain-black}"
  cut-edge:
    backgroundColor: "{colors.stage-light}"
  digital-time:
    backgroundColor: "{colors.curtain-black}"
    textColor: "{colors.stage-light}"
    typography: "{typography.display}"
    padding: "{spacing.margin}"
  drop-shadow:
    backgroundColor: "{colors.curtain-black}"
---

# Design System: Apex Shadow

## 1. Overview

**Creative North Star: "The Stagelight Reveal"**

The face is a stage. Light enters from off-screen, the gnomon is the actor, the shadow is the curtain. Minutes hide on a lower stage and are revealed only inside the cut the shadow makes through the top face. The reader is the audience: they watch a small mechanism do something, then they read what it has uncovered.

Density is near zero on purpose. No chrome, no ticks, no labels, no dashboards. The wearer reads time through staging, not through data. A two-second read is acceptable when it earns a moment of pleasure.

This system rejects three families by name: generic digital LCDs (Casio/G-Shock 7-segment readouts sitting flat on the screen), skeuomorphic analog dials (brushed-metal Rolex imitations with drawn-on bezels), and smartwatch complications dashboards (Apple Watch/Wear OS faces stuffed with weather, steps, calendar, battery). None of those families stage time. They display it.

**Key Characteristics:**
- 1-bit honestly. Only black, only white, only 4x4 Bayer halftone in between.
- Theatrical directional lighting from an off-screen source.
- Two physical layers, separated by exactly one cut.
- No chrome, no labels, no decoration, no logos.
- Per-minute redraws only. No sub-minute motion.

## 2. Colors

The palette is binary. The medium is 1-bit e-paper (or 1-bit-on-color rendered identically across all four Pebble platforms), so subpixel hue is not available and not faked. Every perceived gray is dither.

### Primary
- **Curtain Black** (#000000): Off-pixels. Fills the shadow wedge entirely, lays the drop-shadow trails, and dominates the unlit half of the top face. The negative space the whole composition is cut into.

### Neutral
- **Stage Light** (#ffffff): On-pixels. Appears in the lit half of the top face as scattered dither, as the bright rim on revealed under-face digits, and as the leading edge of each glyph.

### Named Rules
**The Two-Tone Rule.** There are exactly two ink values, ever. Mid-tones are an illusion produced by 4x4 Bayer dither at sixteen thresholds. Never reach for a third value, a `darkgray` fill, or an antialiased ramp; mix at the pixel level or do not mix.

**The Stipple-Is-Light Rule.** Luminance is encoded as the count of Stage Light pixels per 4x4 block. A darker surface has fewer lit pixels; a brighter surface has more. The dither pattern is the luminance instrument, not a texture. Texture happens in service of light, never the other way around.

**The No-Tinted-Neutrals Exception.** The shared design law of tinting neutrals toward a brand hue is suspended here. The medium does not permit subpixel hue. The neutrals are literal #000 and #fff because the physical pixels are on or off.

## 3. Typography

**Display Font:** Apex Segment, a custom 5x9 pseudo-seven-segment glyph set drawn directly into the Pebble framebuffer as a pixel mask.

**Character:** Hand-drawn at native resolution. Hard edges. No subpixel rendering, no antialiasing. The font extrudes by integer scale, so the only ways to grow a glyph are to double, triple, or quadruple every pixel. At runtime, an auto-scale solver tries 4x first and falls back to 3x or 2x whenever the wedge geometry would clip the larger size at the current hour.

### Hierarchy
- **Display** (400 weight, 36px / scale 4x, lineHeight 1, letterSpacing 4px): the rotated `HH:MM` revealed inside the wedge. The only type on the face.

### Named Rules
**The Native-Pixel Rule.** Type is drawn directly to the framebuffer as integer-scaled bitmaps. No Pebble system fonts (`FONT_KEY_BITHAM_42_BOLD`, `FONT_KEY_LECO_42_NUMBERS`, none of them). No TTF resources. No antialiasing. System fonts were tried in earlier iterations and rejected because they cannot rotate with the wedge and their antialiased grays betray the 1-bit medium.

**The Rotates-With-The-Stage Rule.** The display type rotates so its baseline is parallel to the shadow wedge axis. The type is never axis-aligned to the watch bezel. If the wedge points to 4 o'clock, the digits read at 4-o'clock-tilt.

## 4. Elevation

This system has no box-shadows and no Material-style tonal surface layering. Depth is conveyed by a literal two-layer construction: the **top face** carries the lit dithered gradient, and a hidden **under-face** sits behind it. The shadow wedge is the cut through the top face that exposes the under-face. Inside that cut, the under-face has its own independent lighting, and raised digits there cast their own drop shadows on the under-face surface, traced pixel by pixel.

### Layer Vocabulary

A four-band tonal architecture. Each band lands in a different Bayer dither density so the eye reads each as a distinct depth.

- **Top Face — lit core**: dithered radial gradient lit from an off-screen point source opposite the hour. Bright nearest the sun, fading toward the far rim. The gradient's *amplitude* varies through the 24-hour cycle (see Diurnal Light below): full range at noon, compressed at midnight.
- **Top Face — dim floor**: the gradient never collapses to pure black. A floor at intensity 56/255 (~3–5 lit pixels per 4x4) keeps the surface visibly lit even at the far rim, so the wedge below cannot be mistaken for "more gradient."
- **Cut Edge — lip**: a single-pixel band of high intensity (210/255, ~13 lit pixels per 4x4) just inside the wedge boundary. Reads as light catching the upper face's cut edge.
- **Cut (Shadow Wedge)**: a 66° wedge (~33° half-angle) originating at the gnomon, pointing toward the hour. Inside the cut, the top face is absent; only the under-face shows.
- **Under-Face**: a sparse near-black stipple (intensity ~16–31/255, 1–2 lit pixels per 4x4 block). One Bayer band below the top-face floor.
- **Drop Shadow**: a per-pixel ray traced 10 pixels back toward the gnomon (the under-face's own light source). The first four steps are pure black (0/255, no lit pixels). The next six steps fade linearly back to the under-face base, so the shadow reads as a solid void at the digit's heel feathering out as it leaves.

### Named Rules
**The One-Cut Rule.** There is exactly one cut. Do not add inset rims, double cuts, second wedges, faux drop shadows on the top face, or chrome around the wedge edge.

**The Independent-Lighting Rule.** The under-face's light is unrelated to the top-face sun. They are two separate physical scenes that share a screen. Top-face shadows fall away from the off-screen sun (toward the hour). Under-face shadows fall outward from the gnomon (also toward the hour, but for a different physical reason: the under-face is lit from the gnomon side, the wedge's origin).

**The No-Faux-Shadow-On-Top-Face Rule.** Drop shadows live only inside the cut. The top face has the directional sun gradient and the wedge — nothing else. Adding a soft shadow under the gnomon, around the wedge mouth, or behind a top-face element breaks the layer metaphor.

**The Diurnal Light Rule.** The off-screen sun's intensity sweeps through a 24-hour cycle on a cosine curve, peaking at noon and bottoming at midnight. Only the top-face gradient amplitude moves; the cut-edge lip, the under-face base, the drop-shadow void, and the digital readout stay invariant at every hour. The wearer's 3 am wrist glance is a different film from their 3 pm glance, and the time stays equally readable in both. The sweep is silent: it is not announced, it is not animated within the minute, and there is no UI affordance for noticing or disabling it. The wearer either finds it or doesn't.

## 5. Components

This is a watchface, not a UI. The "components" are visual primitives drawn directly to the framebuffer, not interactive widgets. There are no buttons, cards, inputs, chips, or navigation. Documenting those would be a fabrication.

### The Light Face
- **Surface:** dithered radial gradient on the top face, outside the wedge.
- **Light source:** point light located off-screen, at angle `hour_angle + 180°`, distance `half_diag + 38px` from the dial center.
- **Falloff:** linear from full Stage Light at the sun toward the far rim, floored at intensity 56/255 so the surface never collapses to pure black.
- **Render:** intensity per pixel → Bayer-thresholded → one bit per pixel in the framebuffer.

### The Cut Edge
- **Geometry:** a one-pixel band on the inside of the wedge boundary, where `max_perp − abs_perp ≤ 1`.
- **Intensity:** 210/255, dithered. Sits well above both the top-face floor and the under-face base, so the cut is unambiguously delineated from both sides.
- **Read:** light catching the upper face's cut edge. The element that sells the metaphor: this is not a darker patch, this is a removed slice.

### The Shadow Wedge
- **Geometry:** triangle, 66° apex (33° half-angle), origin at the gnomon, length unbounded (clipped by the screen rect).
- **Direction:** the hour angle, including the minute fraction (`(hour*60 + minute) / (12*60) × 360°`). Sweeps smoothly with the minute, not in discrete hourly jumps.
- **Camera pull:** the virtual gnomon position is shifted backward along the shadow axis by 14–34px (zoom-dependent), so the wedge mouth opens further onto the screen. The effect is a cropped, zoomed view of a larger imagined dial.
- **Fill:** none. The wedge area is what shows through to the under-face.

### The Digital Time
- **Type:** Apex Segment at scale 4x rendering `HH:MM` (or `H:MM` per 12/24-hour preference).
- **Rotation:** baseline parallel to the wedge axis.
- **Placement:** along the wedge axis at a `reveal_radius` chosen by the layout solver so the rotated glyph block fits fully inside the wedge interior at the current angle.
- **Auto-scale:** the solver tries scale 4x first; falls back to 3x or 2x if the glyph block would clip against the wedge edges at the current geometry.
- **Body:** mid-stipple, intensity 150 with stipple noise.
- **Rim:** intensity 255 on the edge facing the under-face light (toward the gnomon).
- **Trailing edge:** intensity ~78 with stipple noise.

### The Drop Shadow
- **Trace length:** 10 pixels.
- **Trace direction:** from each under-face non-glyph pixel back toward the gnomon, the under-face's own light source.
- **Hit logic:** if the trace crosses a glyph mask pixel within 10 steps, the source pixel sits in that glyph's shadow at distance `hit`.
- **Core:** hits 1–4 render as pure black (intensity 0). A solid void behind every glyph heel.
- **Fade:** hits 5–10 fade linearly from black back to the under-face base (`intensity = base × (hit−4) / 6`). The shadow returns gradually to the plane rather than ending sharply.

### The Gnomon
- Removed from the current build. A small vertical black bar at the dial center was the original anchor of the shadow; the virtual-camera reveal made it read as a stray vertical line at the wedge mouth. The wedge itself now carries the gnomon's role implicitly.

## 6. Do's and Don'ts

### Do:
- **Do** stage the time. Hour through shadow direction; minutes through a reveal under the cut. Two layers, one moment.
- **Do** treat 1-bit halftone as the medium. Bayer 4x4 at sixteen thresholds. Stipple is the only grayscale.
- **Do** rotate the display type to match the wedge axis.
- **Do** keep the under-face near-black with sparse stipple (intensity ~20/255). Drop shadows need a surface to bite into, but the surface must read as deep dark.
- **Do** drift the off-screen sun position with the hour. The top-face gradient must visibly change as time passes; that drift is half the watchface.
- **Do** modulate the top-face gradient amplitude across the day. Full range at noon, compressed at midnight. The mood shift is silent and earned; it should not be announced.
- **Do** redraw on the minute tick. The wedge sweeps; nothing else moves.
- **Do** verify each build by emulator at 12:00, 03:15, 07:35, 10:23, 13:12, 16:47, 21:50. These angles cover all four screen quadrants and both diagonals.

### Don't:
- **Don't** make this look like a generic digital LCD. No flat 7-segment readouts sitting axis-aligned in the middle of the screen. (PRODUCT.md anti-reference.)
- **Don't** make this look like a skeuomorphic analog dial. No brushed-metal textures, no drawn-on bezels, no fake jewels. (PRODUCT.md anti-reference.)
- **Don't** make this look like a smartwatch complications dashboard. No weather, no step count, no battery indicator on the face. (PRODUCT.md anti-reference.)
- **Don't** use antialiased grayscale. If you need a mid-tone, dither it with the 4x4 Bayer matrix.
- **Don't** add chrome around the wedge. No inner stroke, no outer rim, no double-edge highlight.
- **Don't** add a logo, brand mark, watermark, or signature anywhere on the face.
- **Don't** use Pebble system fonts. They cannot rotate, and their antialiasing creates grays that violate the Two-Tone Rule.
- **Don't** glance-optimize. The two-second read is a feature, not a defect.
- **Don't** animate above the per-minute tick. The reveal is structural; do not flash, pulse, or transition.
- **Don't** add a second cutaway, a second wedge, or any secondary reveal. One cut, ever.
- **Don't** tint the neutrals. The medium doesn't allow it. Pure #000 and pure #fff are the honest values here.
