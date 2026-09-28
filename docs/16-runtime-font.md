# 16 — The overlay font, read from the game at runtime (2026-09-28)

Until now the mod embedded the game's ASCII font — the whole sheet as raw
RGBA in `mod/sprite_font_ascii.h`, extracted from `th06IN.dat` (overlay/51).
That put the game's artwork in the source and in every built DLL, which
can't go into a public repository. Now the text renderer reads it from the
player's own installed game.

- **The game's file reader.** `FUN_14003a0c0(path, fromDisk, uint32
  *outSize)` — the call the anm loader (`FUN_140002440`) makes — strips the
  directory, looks the file name up in the open `.dat` archives (up to 16,
  at `0xC6DBA0`; a language-prefixed name first, then the plain one), and
  returns a malloc'd, decrypted, decompressed copy, or 0. The mod calls it
  for `data/ascii/ascii.dds` and `data/ascii/ascii.anm` the first time the
  overlay draws text (retrying for a few seconds if the archives aren't
  open yet). The buffers are kept (read once per run).
- **The texture** is 512×512 **BC7** behind a DX10 DDS header. Direct3D 11
  samples BC7 natively, so the payload is uploaded as is
  (`OverlayRenderer::CreateTextureFromData`, row pitch 16 bytes per 4×4
  block); no decoder needed. DXT1/DXT5 and 32-bit RGBA are handled too.
- **The glyphs.** The current font is not the old 256×256 grid of 16-pixel
  cells: it's a packed atlas of 43×43 glyphs at arbitrary positions. The
  `.anm`'s sprite table (count at `+0`, offsets from `+0x40`, each sprite
  `{u32 id; float x, y, w, h}`) gives each rect; sprite id = 11 +
  (character − 0x20) still holds.

If loading fails, the log says `TextRenderer: ...` and the overlay draws its
boxes without text; nothing else is affected.

History: the embedded header was removed from every commit before the
repository was published (the pre-rewrite history is kept only as a local
backup bundle).
