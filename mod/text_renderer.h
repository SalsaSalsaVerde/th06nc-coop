#pragma once

#include "overlay_renderer.h"

// Draws ASCII text in the game's own UI font, which it reads out of the
// game's archives at runtime (docs/16) -- not a copy shipped with the mod.
// Monospaced cells, left to right, no kerning; characters outside
// 0x20-0x7E advance as a blank.
//
// Call after OverlayRenderer::EnsureInitialized has run; safe every frame
// (loads once, retrying for a few seconds if the archives aren't open yet).
void TextRenderer_EnsureLoaded(OverlayRenderer& overlay);

// Draws `text` starting with its top-left corner at (xFrac, yFrac) (screen-
// fraction coordinates, same convention as OverlaySprite/OverlayQuad).
// `charWidthFrac`/`charHeightFrac` size each glyph cell; consecutive
// characters advance by `charWidthFrac` with no extra spacing. The glyphs
// are white in the sheet; r/g/b color them.
void DrawText(OverlayRenderer& overlay, const char* text, float xFrac, float yFrac,
              float charWidthFrac, float charHeightFrac, float alpha,
              float r = 1.0f, float g = 1.0f, float b = 1.0f);
