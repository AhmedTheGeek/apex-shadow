---
name: Apex Shadow
description: A Pebble watchface where the shadow sweeps an engraved sundial dial and a small minute mark rides the cut.
colors:
  curtain-black: "#000000"
  stage-light: "#ffffff"
typography:
  hour-numeral:
    fontFamily: "Apex Display, a custom 5x9 cinematic bitmap face (NOT seven-segment)"
    scale: 2
    pixelSize: "10x18 per digit"
  minute-marker:
    fontFamily: "Apex Display, 5x9 cinematic bitmap face"
    scale: 1
    pixelSize: "5x9 per digit"
spacing:
  hair: "1px"
  lip: "1px"
  wall: "2px"
  gnomon-length: "12px"
  gnomon-base-half: "4px"
  shadow-core: "4px"
  shadow-reach: "10px"
  hour-ring-margin: "10px (rect) / 16px (round)"
  minute-marker-radius: "32px"
  wedge-half-angle: "~27° (full ~54°)"
components:
  light-face:
    backgroundColor: "{colors.stage-light}"
    textColor: "{colors.curtain-black}"
  gnomon:
    backgroundColor: "{colors.curtain-black}"
  shadow-wedge:
    backgroundColor: "{colors.curtain-black}"
  cut-lip:
    backgroundColor: "{colors.stage-light}"
  cut-wall:
    backgroundColor: "{colors.curtain-black}"
  hour-numeral:
    backgroundColor: "{colors.curtain-black}"
    textColor: "{colors.stage-light}"
    typography: "{typography.hour-numeral}"
  minute-marker:
    backgroundColor: "{colors.curtain-black}"
    textColor: "{colors.stage-light}"
    typography: "{typography.minute-marker}"
  drop-shadow:
    backgroundColor: "{colors.curtain-black}"
---

# Design System: Apex Shadow

## 1. Overview

**Creative North Star: "The Stagelight Reveal"**

The face is a stage. Light enters from off-screen, the gnomon is the actor, the shadow is the curtain. Beneath the top face an engraved sundial dial sits in fixed positions — twelve hour numerals around the rim — and the cut the shadow makes through the top face is what exposes them. The reader is the audience: they watch a small mechanism do something, then they read what it has uncovered.

Density is near zero on purpose. No chrome, no ticks, no labels, no dashboards. The wearer reads time through staging, not through data. A two-second read is acceptable when it earns a moment of pleasure.

This system rejects three families by name: generic digital LCDs (Casio/G-Shock 7-segment readouts sitting flat on the screen), skeuomorphic analog dials (brushed-metal Rolex imitations with drawn-on bezels), and smartwatch complications dashboards (Apple Watch/Wear OS faces stuffed with weather, steps, calendar, battery). None of those families stage time. They display it.

**Key Characteristics:**
- 1-bit honestly. Only black, only white, only 4x4 Bayer halftone in between.
- Theatrical directional lighting from an off-screen source.
- Two physical layers, separated by exactly one cut.
- The under-face is a stationary engraved dial. The wedge moves; the dial does not.
- A small axis-aligned minute mark rides the cut for precision. Orientation never rotates with the shadow.
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

**Display Font:** Apex Display, a custom 5x9 cinematic glyph set drawn directly into the Pebble framebuffer as a pixel mask. **Not** a seven-segment readout — that's PRODUCT.md's first anti-reference.

**Character:** Hand-drawn at native resolution. Hard edges. No subpixel rendering, no antialiasing. Geometric, slightly condensed, with intentional character in every glyph:

- **0** — open ring; the body is one continuous loop, not a "lit segments minus the middle bar."
- **1** — slim single column with a small flag at the top. Asymmetric, not a tall rectangle.
- **2** — top arc, then a real diagonal cutting down to a full base bar.
- **3** — three rightward hooks on a vertical spine. Distinctive silhouette; reads as 3 at a glance.
- **4** — two full vertical stems crossed by a horizontal bar; the left stem stops at the bar, the right continues down.
- **5** — square top, open middle, full bottom bowl.
- **6** — a top hook curving into a full closed bottom loop.
- **7** — a single sharp diagonal slash from the top-right corner to the bottom-left.
- **8** — a **true double loop** with two complete enclosed rings. This is the "tell" that proves the type is not segment-built: a 7-segment 8 has all seven segments lit; this 8 has two real holes.
- **9** — mirror of 6: full closed top loop, hooked tail descending.
- **:** — two stacked dots, one cell square each.

The font extrudes by integer scale, so the only ways to grow a glyph are to double, triple, or quadruple every pixel. At runtime, an auto-scale solver tries 4x first and falls back to 3x or 2x whenever the wedge geometry would clip the larger size at the current hour.

### Hierarchy
- **Hour numeral** (scale 2x, 10x18 px per digit): twelve numerals stamped at the clock-face perimeter, axis-aligned upright. The primary read.
- **Minute marker** (scale 1x, 5x9 px per digit): a small 2-digit annotation sitting along the wedge axis just past the gnomon, axis-aligned upright. Secondary read.

### Named Rules
**The Native-Pixel Rule.** Type is drawn directly to the framebuffer as integer-scaled bitmaps. No Pebble system fonts (`FONT_KEY_BITHAM_42_BOLD`, `FONT_KEY_LECO_42_NUMBERS`, none of them). No TTF resources. No antialiasing. System fonts were tried in earlier iterations and rejected because they don't stamp pixel-cleanly into a rotating drop-shadow tracer and their antialiased grays betray the 1-bit medium.

**The Axis-Aligned-Always Rule.** All type on the face reads upright in screen space, never rotated to the wedge axis. An earlier draft rotated the `HH:MM` to follow the shadow; that defeated the stationary-dial read because the digits behaved like brush strokes the wedge dragged around. The dial is engraved; the minute marker is upright; nothing tilts.

**The Two-Type-Sizes Rule.** Exactly two sizes are in use: scale 2 for the perimeter hour numerals (the dial's identity), scale 1 for the minute marker (a quiet refinement). Don't add a third.

## 4. Elevation

This system has no box-shadows and no Material-style tonal surface layering. Depth is conveyed by a literal two-layer construction: the **top face** carries the lit dithered gradient, and a hidden **under-face** sits behind it. The shadow wedge is the cut through the top face that exposes the under-face. Inside that cut, the under-face has its own independent lighting, and raised digits there cast their own drop shadows on the under-face surface, traced pixel by pixel.

### Layer Vocabulary

A four-band tonal architecture. Each band lands in a different Bayer dither density so the eye reads each as a distinct depth.

- **Top Face — lit core**: dithered radial gradient lit from an off-screen point source opposite the hour. Bright nearest the sun, fading toward the far rim. The gradient's *amplitude* varies through the 24-hour cycle (see Diurnal Light below): full range at noon, compressed at midnight.
- **Top Face — dim floor**: the gradient never collapses to pure black. A floor at intensity 56/255 (~3–5 lit pixels per 4x4) keeps the surface visibly lit even at the far rim, so the wedge below cannot be mistaken for "more gradient."
- **Cut — lip**: a single-pixel band of high intensity (230/255, ~14–15 lit pixels per 4x4) just inside the wedge boundary. The cut's top corner catching light.
- **Cut — wall**: a 2-pixel band of low intensity (4/255, 0–1 lit pixels) just inside the lip. The inner wall of the cut, in shadow.
- **Cut (Shadow Wedge)**: a ~54° wedge (~27° half-angle) originating at the gnomon, pointing toward the hour. Inside the cut, the top face is absent; only the under-face dial shows.
- **Under-Face — engraved plate**: a sparse near-black stipple (intensity ~16–31/255, 1–2 lit pixels per 4x4 block) carrying the stamped hour numerals 1–12 at fixed clock positions. One Bayer band below the top-face floor.
- **Drop Shadow**: a per-pixel ray traced 10 pixels back toward the gnomon (the under-face's own light source). The first four steps are pure black (0/255, no lit pixels). The next six steps fade linearly back to the under-face base, so the shadow reads as a solid void at the numeral's heel feathering out as it leaves.

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

### The Shadow Wedge
- **Geometry:** triangle, 66° apex (33° half-angle), origin at the gnomon tip, length unbounded (clipped by the screen rect). Wedge apex and gnomon tip coincide at a single pixel.
- **Direction:** the hour angle, including the minute fraction (`(hour*60 + minute) / (12*60) × 360°`). Sweeps smoothly with the minute, not in discrete hourly jumps.
- **Camera pull:** the virtual dial center is shifted backward along the shadow axis by a locked 32px, so the wedge mouth opens further onto the screen. The effect is a cropped, zoomed view of a larger imagined dial. The pull does not vary with angle — locking it is what makes the digit's on-screen position consistent at every hour.
- **Fill:** none. The wedge area is what shows through to the under-face, framed by the lip and wall.

### The Digital Time
- **Type:** Apex Display at locked scale 3x rendering `HH:MM` (or `H:MM` per 12/24-hour preference).
- **Rotation:** baseline parallel to the wedge axis. If the wedge points to 4 o'clock, the digits read at 4-o'clock-tilt.
- **Locked scale:** scale 3x is the largest that fits inside the wedge at every hour, across every Pebble platform. The scale never varies with angle — the digits read at the same on-screen pixel size at 12:00, 3:00, 7:35, and every angle in between. Scale 4x cannot fit at cardinal angles on the 144×168 screens even at maximum camera pull, so it is never reached.
- **Locked position:** the digit center sits 22px from the screen center along the shadow axis at every hour. Combined with a fixed 44px camera pull (so reveal_radius = 66 from the dial center), this places the digit at the same on-screen offset regardless of where the wedge points, and keeps a ~27px gap between the gnomon tip and the digit's near edge so the gnomon, wedge apex, lip, and wall all read clearly before the digits begin.
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
- **Geometry:** a small directional triangle on the lit dial. Base 9px wide on the side facing the sun, narrowing to a 1px tip that touches the wedge apex. Length along the shadow axis: 12px.
- **Direction:** rotates with the hour. Always lies opposite the wedge, between the dial center and the off-screen sun.
- **Fill:** pure black (intensity 0). No internal shading; the silhouette against the lit top-face dither carries the read.
- **Role:** the visible actor on the stage. The wedge IS its shadow — gnomon tip and wedge apex meet at a single pixel, so the two read as one continuous shape: solid triangle joining its own cast shadow.
- **History:** an earlier draft used a fixed vertical bar at the dial center. That was wrong: it didn't rotate with the wedge, so at most hours it read as a stray line. The directional triangle replaces it. The cut metaphor demands a visible cause.

### The Cut Edge
- **Lip:** a 1-pixel band just inside the wedge boundary at intensity 230. Bright. Reads as light catching the top corner of the cut.
- **Wall:** a 2-pixel band just inside the lip at intensity 4. Darker than the under-face base. Reads as the inner wall of the cut, in shadow because it faces downward.
- **Read:** three pixel bands — bright lip, dark wall, then the under-face stipple — sell the cut as a carved slice rather than a painted-on dark region.

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
