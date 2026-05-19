# Apex Shadow Design Notes

## Concept

Apex Shadow is a Pebble watchface built around a sundial metaphor. The face uses an off-screen light source, a central gnomon, and a shadow wedge to make the current hour feel physical rather than simply displayed.

## Original Version

The first version showed the current minutes as large engraved digits in the center of the dial. A small sun sat on the rim opposite the current hour, casting a dithered light gradient across the face. The center gnomon created a narrow black wedge pointing toward the hour.

Rendering was done per pixel using the Pebble framebuffer:

- draw the minute digits as a temporary mask,
- compute light intensity from the sun position,
- darken pixels inside the gnomon shadow,
- boost digit pixels,
- convert intensity to black/white using a 4x4 Bayer dither matrix.

## Raised Digit Experiment

We changed the minute digits from engraved outlines into solid raised objects. The digits were treated as silhouettes with mass:

- a bright rim on the side facing the light,
- a darker body,
- a projected drop shadow away from the light.

The light source was moved off-screen so the visible sun dot was removed.

## Moving Minute And Date Marker

Next, the minute digits were moved out of the center and made to follow the hour shadow. We added the current date and grouped it with the minutes. Several layouts were tested:

- minute/date marker near the tip of the shadow,
- marker offset beside the shadow,
- marker on the illuminated side,
- minute and date aligned along the upper and lower edges of the shadow wedge.

To support rotated labels, we introduced custom mask-drawn seven-segment glyphs instead of Pebble system fonts. This allowed the text to rotate with the shadow direction.

## Hour Marker Experiment

We also tested hour markers around the dial. The first version used simple radial ticks. Later, those were replaced with small numeric hour glyphs rendered through the same object-lighting model.

The intent was that the current-hour marker would be hidden by the gnomon shadow. This worked conceptually, but the result became visually noisy, especially once the hour numbers also cast shadows. The hour markers were removed for now.

## Current Direction

The current approach changes the shadow from a flat black wedge into a reveal/cutaway. The shadow now acts like it is exposing a lower layer underneath the watchface.

Inside that revealed underlayer, the current digital time is drawn as a rotated `HH:MM` mask. The time appears only inside the widened shadow wedge, as if it is peeking through from beneath the dial.

The current rendering model includes:

- a wider gnomon wedge,
- point-distance lighting from an off-screen light source,
- a zoomed, textured darker underlayer inside the wedge,
- brighter cut-edge highlights along the wedge edges,
- a masked digital time readout visible only within the reveal,
- subtle lighting/depth on the time glyphs instead of solid flat colors,
- independent bottom-layer lighting, so top-layer shadow/drop-shadow rules do not affect the revealed lower face.
- a dynamic virtual camera, so the screen behaves like a cropped/zoomed view focused on the reveal instead of keeping the gnomon fixed in the exact center.
- camera pull, lower-layer zoom, time placement, and time scale are recalculated from the current shadow direction so the hidden digital time remains visible at different times of day.
- the reveal wedge background is a near-black sparse stipple (intensity ~16/255), one Bayer band darker than the top-face dim floor so the cutaway always reads as a deeper plane.
- the top-face gradient is floored at intensity 56/255. Even at the far rim the lit surface keeps a visible dither density, so the wedge below cannot be mistaken for more of the same gradient.
- a single bright pixel-line just inside the wedge boundary (intensity 210/255) acts as a cut-edge lip catching the light. This is the element that converts "darker wedge" into "removed slice."
- inside the wedge, raised glyphs cast drop shadows back toward the gnomon (the under-face's light source). The shadow has a four-pixel pure-black core followed by a six-pixel linear fade to the under-face base, so the void reads sharp at the glyph heel and feathers as it leaves.
- the four tonal bands (top-face lit, top-face floor, cut-edge lip, under-face base, drop-shadow void) each land in a distinct Bayer dither density. If any two collapsed into the same density the layer metaphor would dissolve.
- lower-layer time scale now also checks the actual wedge width at the chosen reveal point, so the time shrinks when it would otherwise be sliced near the shadow tip.
- the original raised gnomon rectangle was removed because the virtual-camera reveal made it read as a stray black line at the wedge origin.

## Current Files

- `src/c/main.c` contains the full watchface implementation.
- `package.json` defines the Pebble SDK metadata and target platforms.
- `build/apex-shadow.pbw` is regenerated by `pebble build`.
- `screenshot.png` contains the latest basalt emulator screenshot.

## Verification So Far

The watchface has been repeatedly built and run with Pebble tools:

```sh
pebble build
pebble install --emulator basalt build/apex-shadow.pbw
pebble screenshot --emulator basalt screenshot.png --no-open
```

The latest build succeeds for the configured Pebble platforms: `aplite`, `basalt`, `chalk`, and `diorite`.
