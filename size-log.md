# Size Log

Track binary size at each checkpoint. Budget: 1,474,560 bytes (1.44MB floppy).

| Stage | Mac (clang) | Windows (mingw) | Win + UPX | Notes |
|-------|------------:|----------------:|----------:|-------|
| 1. toolchain (Hello World) | 53,752 | 38,912 | 23,552 | 1.6% of 1.44MB. TIGR + CRT static.
| 2. core loop (move, log, status) | 53,880 | 39,936 | 25,088 | +128B Mac / +1.5KB Win. LTO pruning aggressive.
| 3. mapgen (BSP rooms + FOV + stairs) | 53,928 | - | - | +48B Mac. BSP, raycast FOV, multi-floor.
| 4. combat (5 mobs, AI, level up) | 53,928 | - | 28,160 | Mac stripped dead code; +3KB Win+UPX.
| 5. items (20 items, inv, ident) | 70,440 | - | 30,720 | +16KB Mac code; +2.5KB Win+UPX.
| 6. ui-meta (title/help/hiscore) | 70,664 | - | 32,256 | +1.5KB Win+UPX. Hiscore persisted to score.dat.
| 7. depth (hunger, 14 mobs, 3 bosses, win) | 70,680 | - | 32,768 | +512B Win. 11 reg mobs + 3 bosses + hunger/win.
| 8. size-opt (final flag audit) | 70,680 | 134,144 | 32,768 | Final flags: -Os -flto -ffunction-sections -fdata-sections --gc-sections --strip-all + UPX --best --lzma. 2.2% of 1.44MB.
| 9. polish (README + screenshots + UI spacing) | 70,632 | ~134,000 | 33,280 | +512B for proper inventory/help line spacing.
| 10. 3D renderer (raycaster + procedural textures) | 70,696 | 178,176 | 36,864 | +64B Mac / +44KB Win raw / **+3.5KB UPX**. Procedural 64x64 wall/floor/ceiling textures (no shipped texture bytes), DDA wall casting, perspective-correct floor mapping, depth-buffered sprites, minimap. Input updated for first-person grid+turn-based (W/S forward/back, A/D strafe, Q/E turn 90 deg). Existing game state, FOV, mob AI, items, hunger, bosses untouched.

## Budget summary

- Target floppy size: **1,474,560 bytes** (1.44 MB, 3.5" HD floppy)
- Final submission `.exe`: **36,864 bytes** (36 KB)
- Headroom: **1,437,696 bytes** unused (97.5 % of the disk)
- Compression: raw ~174 KB -> UPX 36 KB (~4.8x ratio, --best --lzma)

## 3D renderer cost breakdown (stage 10 - stage 9)

- Win raw delta:  +44 KB  (renderer code + libm `sinf`/`cosf`/`floorf`/`fabsf` pulled in)
- Win UPX delta:  **+3.5 KB** (LZMA squashes the new code very effectively)
- Mac delta:      +64 bytes (LLVM LTO + dead-strip is more aggressive than mingw)
- BSS / zero-init: ~32 KB at runtime for the 8 procedural textures (paletted 64x64 + 16-color palettes). Lives in `.bss`, not in the on-disk file.

