#include "text_renderer.h"
#include "sprite_font_ascii.h"
#include "mod_log.h"

#include <cstring>

// Glyph layout (docs/51): confirmed by rendering the full extracted sheet
// and visually reading it as standard printable ASCII, then verified
// programmatically against the parsed `.anm` sprite table -- id 11's rect
// is (0,32,16,16), id 27's is (0,48,16,16) landing exactly on '0' (the
// digit row), id 65's on 'V', etc. All 95 glyphs (0x20-0x7E) sit on a
// plain contiguous 16x16-per-cell grid, 16 columns wide, starting at pixel
// (0,32) in the 256x256 sheet -- no lookup table needed, just arithmetic:
//   localId = 11 + (asciiCode - 0x20)
//   col = (localId - 11) % 16, row = (localId - 11) / 16
//   pixelRect = (col*16, 32 + row*16, 16, 16)
namespace {
const int kGlyphFirstCode = 0x20;
const int kGlyphLastCode = 0x7E;
const int kGlyphCols = 16;
const int kGlyphCellPx = 16;
const int kGlyphGridOriginYPx = 32;
const unsigned int kSheetSizePx = 256; // g_spriteFontAscii_width/height, both square

SpriteTexture g_fontTex;
bool g_loadAttempted = false;
}

void TextRenderer_EnsureLoaded(OverlayRenderer& overlay) {
    if (g_loadAttempted) return;
    g_loadAttempted = true;
    g_fontTex = overlay.CreateSpriteTexture(g_spriteFontAscii_width, g_spriteFontAscii_height, g_spriteFontAscii_rgba);
    ModLog("TextRenderer: font texture %s", g_fontTex.srv ? "OK" : "FAILED");
}

void DrawText(OverlayRenderer& overlay, const char* text, float xFrac, float yFrac,
              float charWidthFrac, float charHeightFrac, float alpha, float r, float g, float b) {
    if (!g_fontTex.srv || !text) return;

    float cursorX = xFrac;
    for (const char* p = text; *p; p++) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c >= kGlyphFirstCode && c <= kGlyphLastCode) {
            int localId = 11 + (c - kGlyphFirstCode);
            int col = (localId - 11) % kGlyphCols;
            int row = (localId - 11) / kGlyphCols;
            float pxX = static_cast<float>(col * kGlyphCellPx);
            float pxY = static_cast<float>(kGlyphGridOriginYPx + row * kGlyphCellPx);
            float uMin = pxX / kSheetSizePx;
            float vMin = pxY / kSheetSizePx;
            float uMax = (pxX + kGlyphCellPx) / kSheetSizePx;
            float vMax = (pxY + kGlyphCellPx) / kSheetSizePx;

            // OverlaySprite's center/half-extent convention (not top-left),
            // same as every other sprite draw in this project.
            OverlaySprite glyph = {
                g_fontTex,
                cursorX + charWidthFrac * 0.5f,
                yFrac + charHeightFrac * 0.5f,
                charWidthFrac * 0.5f,
                charHeightFrac * 0.5f,
                alpha,
                uMin, vMin, uMax, vMax,
                r, g, b
            };
            overlay.DrawSprite(glyph);
        }
        // Unsupported characters (outside 0x20-0x7E) still advance the
        // cursor -- a blank space, not a dropped character, so column
        // alignment of anything after it isn't thrown off.
        cursorX += charWidthFrac;
    }
}
