# Product

## Register

brand

## Users

Fellow Pebble enthusiasts. People who chose Pebble because they wanted something other than a mainstream smartwatch and who care more about craft than feature count. They expect more than glanceability; they're happy to spend a moment with a watch to read it. Many also wear or collect mechanical watches and would put a Pebble next to them in a rotation.

## Product Purpose

Apex Shadow is a Pebble watchface that makes time feel physical and theatrical instead of displayed. The current hour is told by where a wedge of shadow points on the dial. The minutes are not shown on the surface; they sit on a lower layer and are revealed only inside the shadow's cutaway, peeking through from underneath. The experience is closer to watching a stage trick than reading a clock. Success is when wearers describe glancing at the watch as a small daily moment rather than a function call.

## Brand Personality

Theatrical, cinematic, hidden. The face has stagecraft: a directional light, a dramatic cutaway, a reveal. Voice in any accompanying copy (app store listing, README, settings labels if they ever exist) is sparse, declarative, slightly noir. Closer to a film title card than to product marketing.

## Anti-references

- **Generic digital LCD.** Casio and G-Shock 7-segment readouts, plain info-dense rows of data. The watch must never resemble a calculator-style display sitting flat on the screen.
- **Skeuomorphic analog.** Photorealistic Rolex or Omega imitations with brushed metal, dial textures, and bezels. No imitation jewelry. No drawn-on rivets.
- **Smartwatch complications.** Apple Watch and Wear OS dashboards loaded with weather, steps, calendar, and battery. No dashboarding. Every element on the face exists to tell time or to stage the telling.

## Design Principles

1. **Time is staged, not displayed.** The wearer reads time through a performance (shadow direction, then the reveal of digits underneath). The face should feel like a small mechanism doing something, not a panel showing a number.
2. **Reveal beats glance.** Legibility-at-a-glance is explicitly deprioritized. A two-second read is acceptable if it earns the wearer a moment of pleasure. Approachable, but not instant.
3. **Light is the instrument.** All hierarchy comes from where light falls and where shadow cuts. Direction, contrast, and falloff are the design language. Not strokes, labels, or chrome.
4. **One element, two layers, no chrome.** There is the top face and the under-face, separated by a cut. Nothing else. No ticks for ticks' sake, no logos, no decorative borders. Every added element must justify itself against this rule.
5. **Embrace 1-bit honestly.** Black, white, and ordered dither are the medium, not a limitation to hide. Stipple textures, hard edges, and Bayer halftones are part of the language. Not antialiased pretenders to grayscale.

## Accessibility & Inclusion

The watchface is rendered as 1-bit black-and-white (or 1-bit-on-color via dither) on every Pebble platform, so color-blindness is not a concern. Contrast is intentionally extreme: pure black, pure white, ordered dither. Legibility-at-a-glance is deliberately deprioritized in favor of theatrical reveal; wearers who need a constantly-readable digital face are not the audience and we don't try to serve both.

Critical OS-level information (low battery, notifications, alarms) is handled by Pebble's own system chrome, not by the face. Animation is limited to per-minute tick redraws: no flashing, no strobing, nothing that could trigger photosensitive responses.
