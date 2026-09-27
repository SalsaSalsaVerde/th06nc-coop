#pragma once

#include "overlay_renderer.h"

// Draws ASCII text using the game's own bitmap font (docs/51), extracted
// from `data/ascii/ascii.dds` (th06IN.dat) the same way every other sprite
// in this project is extracted (docs/22's pkgl_extract.py) -- not a custom
// font, the exact glyphs the game itself uses for its own UI text.
//
// Monospaced (the source sheet is a plain 16x16-per-cell grid, one cell per
// printable ASCII code 0x20 ' ' through 0x7E '~"), left-to-right, no
// wrapping/kerning. Unsupported characters (outside 0x20-0x7E) are drawn as
// a blank advance (skipped, cursor still moves) rather than a fallback
// glyph -- fine for this mod's own short status/debug strings, which are
// plain ASCII by construction.
//
// Call once after OverlayRenderer::EnsureInitialized has run (needs a live
// device) to create the font texture; safe to call every frame; only
// creates the texture once.
void TextRenderer_EnsureLoaded(OverlayRenderer& overlay);

// Draws `text` starting with its top-left corner at (xFrac, yFrac) (screen-
// fraction coordinates, same convention as OverlaySprite/OverlayQuad).
// `charWidthFrac`/`charHeightFrac` size each glyph cell; consecutive
// characters advance by `charWidthFrac` with no extra spacing. No color
// tint (see cpp comment) -- always drawn in the font sheet's own native
// color, alpha-multiplied by `alpha`.
void DrawText(OverlayRenderer& overlay, const char* text, float xFrac, float yFrac,
              float charWidthFrac, float charHeightFrac, float alpha);
