#include "text_renderer.h"
#include "game.h"
#include "mod_log.h"

#include <cstring>

// The game's own UI font, read out of the game's own archives at runtime
// (docs/16): `ascii.dds` (a 512x512 BC7 atlas) and `ascii.anm` (its sprite
// table) from th06IN.dat, through the game's file loader -- so neither this
// repository nor the built DLL carries the game's artwork.
//
// Glyphs: sprite id = 11 + (character - 0x20), for 0x20 ' ' through 0x7E
// '~'; each sprite's rect (x, y, w, h, pixels in the atlas) comes from the
// .anm's sprite table.
namespace {

const int kGlyphFirstCode = 0x20;
const int kGlyphLastCode = 0x7E;
const int kGlyphCount = kGlyphLastCode - kGlyphFirstCode + 1;
const int kGlyphFirstSpriteId = 11;

struct GlyphRect {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    bool valid = false;
};

SpriteTexture g_fontTex;
GlyphRect g_glyphs[kGlyphCount];
int g_attempts = 0;
int g_retryTimer = 0;

// FUN_14003a0c0(path, fromDisk, outSize): finds `path`'s file name in the
// open .dat archives and returns a malloc'd, decrypted, decompressed copy
// (the anm loader's own file read). The buffer is the game CRT's; it is
// kept, not freed (read once per run).
const uint8_t* ReadGameFile(const char* path, uint32_t* size) {
    using ReadFileFn = const uint8_t* (*)(const char* path, char fromDisk, uint32_t* outSize);
    *size = 0;
    return Game::Fn<ReadFileFn>(Game::kFnReadArchiveFile)(path, 0, size);
}

uint32_t U32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

float F32(const uint8_t* p) {
    float v;
    memcpy(&v, p, 4);
    return v;
}

// A DDS file -> D3D11 texture. Handles the DX10 header (the game's: BC7)
// and the legacy DXT1/DXT5 and 32-bit RGBA layouts.
bool CreateFromDds(OverlayRenderer& overlay, const uint8_t* dds, uint32_t size, uint32_t* width, uint32_t* height) {
    if (size < 128 || memcmp(dds, "DDS ", 4) != 0) return false;
    *height = U32(dds + 12);
    *width = U32(dds + 16);
    uint32_t fourCC = U32(dds + 84);
    size_t offset = 128;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    if (fourCC == 0x30315844) { // "DX10"
        if (size < 148) return false;
        format = static_cast<DXGI_FORMAT>(U32(dds + 128));
        offset = 148;
    } else if (fourCC == 0x31545844) { // "DXT1"
        format = DXGI_FORMAT_BC1_UNORM;
    } else if (fourCC == 0x35545844) { // "DXT5"
        format = DXGI_FORMAT_BC3_UNORM;
    } else if (U32(dds + 88) == 32) {
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    uint32_t pitch = 0;
    bool blocks = true;
    switch (format) {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC4_UNORM:
            pitch = ((*width + 3) / 4) * 8;
            break;
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            pitch = ((*width + 3) / 4) * 16;
            break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            pitch = *width * 4;
            blocks = false;
            break;
        default:
            ModLog("TextRenderer: font texture has an unsupported format (%u)", static_cast<unsigned>(format));
            return false;
    }
    size_t rows = blocks ? (*height + 3) / 4 : *height;
    if (offset + static_cast<size_t>(pitch) * rows > size) return false;
    g_fontTex = overlay.CreateTextureFromData(*width, *height, format, dds + offset, pitch);
    return g_fontTex.srv != nullptr;
}

// The .anm's sprite table: sprite count at +0, offsets to each sprite from
// +0x40; a sprite is { u32 id; float x, y, w, h; }.
int ReadGlyphRects(const uint8_t* anm, uint32_t size, uint32_t width, uint32_t height) {
    if (size < 0x40 || width == 0 || height == 0) return 0;
    uint32_t count = U32(anm);
    if (count > 4096 || 0x40 + count * 4 > size) return 0;
    int found = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t at = U32(anm + 0x40 + i * 4);
        if (at > size || size - at < 20) continue;
        int glyph = static_cast<int>(U32(anm + at)) - kGlyphFirstSpriteId;
        if (glyph < 0 || glyph >= kGlyphCount) continue;
        float x = F32(anm + at + 4), y = F32(anm + at + 8), w = F32(anm + at + 12), h = F32(anm + at + 16);
        GlyphRect& r = g_glyphs[glyph];
        r.u0 = x / static_cast<float>(width);
        r.v0 = y / static_cast<float>(height);
        r.u1 = (x + w) / static_cast<float>(width);
        r.v1 = (y + h) / static_cast<float>(height);
        r.valid = true;
        found++;
    }
    return found;
}

} // namespace

void TextRenderer_EnsureLoaded(OverlayRenderer& overlay) {
    if (g_fontTex.srv || g_attempts >= 10) return;
    // The archives open while the game starts; try again a little later if
    // they aren't yet.
    if (g_retryTimer > 0) {
        g_retryTimer--;
        return;
    }
    g_attempts++;
    g_retryTimer = 60;
    uint32_t ddsSize = 0, anmSize = 0;
    const uint8_t* dds = ReadGameFile("data/ascii/ascii.dds", &ddsSize);
    const uint8_t* anm = ReadGameFile("data/ascii/ascii.anm", &anmSize);
    if (!dds || !anm) {
        ModLog("TextRenderer: the game's font isn't readable yet (attempt %d)", g_attempts);
        return;
    }
    uint32_t width = 0, height = 0;
    if (!CreateFromDds(overlay, dds, ddsSize, &width, &height)) {
        ModLog("TextRenderer: couldn't create the font texture from ascii.dds (%u bytes)", ddsSize);
        g_attempts = 10;
        return;
    }
    int glyphs = ReadGlyphRects(anm, anmSize, width, height);
    ModLog("TextRenderer: game font loaded (%ux%u, %d of %d glyphs)", width, height, glyphs, kGlyphCount);
}

void DrawText(OverlayRenderer& overlay, const char* text, float xFrac, float yFrac,
              float charWidthFrac, float charHeightFrac, float alpha, float r, float g, float b) {
    if (!g_fontTex.srv || !text) return;

    float cursorX = xFrac;
    for (const char* p = text; *p; p++) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c > kGlyphFirstCode && c <= kGlyphLastCode && g_glyphs[c - kGlyphFirstCode].valid) {
            const GlyphRect& rect = g_glyphs[c - kGlyphFirstCode];
            // OverlaySprite's center/half-extent convention (not top-left),
            // same as every other sprite draw in this project.
            OverlaySprite glyph = {
                g_fontTex,
                cursorX + charWidthFrac * 0.5f,
                yFrac + charHeightFrac * 0.5f,
                charWidthFrac * 0.5f,
                charHeightFrac * 0.5f,
                alpha,
                rect.u0, rect.v0, rect.u1, rect.v1,
                r, g, b
            };
            overlay.DrawSprite(glyph);
        }
        // Spaces and unsupported characters still advance the cursor.
        cursorX += charWidthFrac;
    }
}
