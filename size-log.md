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

## Budget summary

- Target floppy size: **1,474,560 bytes** (1.44 MB, 3.5" HD floppy)
- Final submission `.exe`: **33,280 bytes** (33 KB)
- Headroom: **1,441,280 bytes** unused (97.7 % of the disk)
- Compression: raw ~134 KB -> UPX 33 KB (~4x ratio, --best --lzma)

