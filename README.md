# 1.44 MB :: Sectors of the Lost Disk

A **first-person 3D dungeon-crawl roguelike** that fits on a single 3.5-inch
HD floppy disk. Doom-style textured walls, grid-locked movement,
turn-based combat -- in the spirit of *Wizardry*, *Eye of the Beholder*,
and *Legend of Grimrock*. The whole game (executable, font, textures,
data, everything) is a **72,192-byte standalone Windows executable** --
about 4.9 % of the 1,474,560-byte floppy budget.

Built for the [1.44MB GAME_DEV CONTEST](https://2pgarcade.com/contest-144mb.html).

![title](screenshots/title.png)

## Story

The floppy disk is dying. Its sectors are corrupting one by one, and the
**Master Boot Record** has been overwritten by something that should not be.
You are the last data-recovery routine in memory. Jack in, descend through
15 corrupted sectors, and restore the boot record before the disk fails.

## Gameplay

A turn-based dungeon crawler with a real-time-feel **column-based raycaster**
in the tradition of *Wolfenstein 3D* and *Doom*, played in the tradition of
*Wizardry* and *Rogue*:

- 15 procedurally generated floors built with recursive BSP partitioning
- **Textured 3D first-person view** rendered with DDA wall casting,
  perspective-correct floor / ceiling mapping, distance fade,
  and depth-buffered, alpha-tested billboard sprites
- 44 embedded **Dungeon Crawl Stone Soup** tiles (CC0): 6 walls, 2 floors,
  ceiling, door, plus a dedicated sprite for each of the 14 monsters and
  20 items -- decoded once at startup from compact PNGs in the binary
- Symmetric raycast field-of-view with tile memory; the **minimap** in
  the bottom right shows everything you've explored
- 11 regular enemies + 3 unique bosses on the checkpoint floors (5, 10, 15)
- 20-item table: weapons, armor, patches (potions), scrolls, food
- Unidentified patches and scrolls get random pseudo-names (`red patch`,
  `ALPHA.exe`) -- the mapping is reshuffled every run
- Bump-to-attack combat with critical strikes and a level-up curve
- Hunger system thematically branded as **memory pressure**: ticks up every
  turn, stops regen at *Hungry*, drains HP at *Starving*
- Permadeath, high-score table persisted in a tiny `score.dat` (~232 bytes)

![gameplay](screenshots/gameplay.png)

### Controls

Movement is **grid-locked** and **turn-based**: one keypress = one tile.

```
W               step forward (1 tile, facing direction)
S               step backward (1 tile)
A               strafe left
D               strafe right
Q               turn 90 deg left   (does NOT consume a turn)
E               turn 90 deg right  (does NOT consume a turn)
.   space       wait one turn
>               descend stairs
i               open inventory
?               in-game help
ESC             abandon run (returns to title)
```

Walking into an enemy attacks it (bump-to-attack). Facing is restricted to
the four cardinal directions; `Q` and `E` rotate the camera 90 degrees in
place and do not cost a turn, so you can survey the room freely.

In inventory: `a-p` use/equip an item, `D` (capital) then `<letter>` drop, `i`/`ESC` close.

![inventory](screenshots/inventory.png)
![help](screenshots/help.png)

## Build

The game targets Windows (mingw-w64 cross-compile from macOS / Linux) but is
fully cross-platform. The Mac binary is used for development.

```bash
# Toolchain - one-time setup on macOS:
brew install mingw-w64 upx

# Development build (native, macOS or Linux):
make mac          # produces build/disk
make run

# Submission build (Windows .exe, UPX-compressed):
make submit       # produces build/disk.exe
                  # fails the build if it ever exceeds 1,474,560 bytes

# Regenerate embedded tiles (dev only; needs python3 + ImageMagick):
make assets       # rewrites src/assets_gen.c from the DCSS tileset folder
```

There are no runtime dependencies. Just copy `build/disk.exe` to any
Windows machine and double-click. The embedded tiles live in the committed
`src/assets_gen.c`, so a normal build needs neither Python nor the tileset.

## Size budget

The hard rule of the contest is **1,474,560 bytes** -- the physical capacity
of a 1.44 MB 3.5-inch HD floppy. The actual numbers from this build:

| Artifact                              | Size      | % of 1.44 MB |
|---------------------------------------|----------:|-------------:|
| Mac build (`clang -Os -flto`)         |   117 KB |          8 % |
| Win build (`mingw -Os -flto -s`)      |   204 KB |         14 % |
| Win build + `upx --best --lzma`       |  **70 KB** |    **4.9 %** |

The 44 Dungeon Crawl Stone Soup tiles add only ~39 KB of (already
DEFLATE-compressed) PNG data, embedded directly in the binary and decoded
once at startup -- no external files ship with the game. We still leave
**1,402,368 bytes** unused on the floppy.

The full per-stage size log is in [`size-log.md`](size-log.md).

## Architecture

Pure C99, statically linked, **no runtime dependencies**:

- [src/main.c](src/main.c) -- entry point, input dispatch, state machine
- [src/game.c](src/game.c) -- game loop, message log, turn order, regen, hunger
- [src/map.c](src/map.c) -- BSP dungeon generation, raycast FOV
- [src/mob.c](src/mob.c) -- spawning, AI (chase / erratic / teleport), combat
- [src/item.c](src/item.c) -- inventory, equipment, identification, effects
- [src/score.c](src/score.c) -- binary high-score persistence (`score.dat`)
- [src/ui.c](src/ui.c) -- screens (title, help, hiscore, inventory) +
  3D viewport / minimap / status panel composition
- [src/render3d.c](src/render3d.c) -- Wolfenstein-style raycaster: DCSS-tiled
  wall / floor / ceiling sampling, DDA wall casting, perspective-correct
  floor mapping, distance fade, depth-buffered sprite blits, minimap
- [src/rng.c](src/rng.c) -- 32-bit xorshift PRNG
- [src/data.c](src/data.c) -- static tables: monsters, items, pseudo-names
- [src/assets_gen.c](src/assets_gen.c) -- auto-generated embedded tile PNGs
  (built by [tools/gen_assets.py](tools/gen_assets.py); run `make assets`)
- [src/game.h](src/game.h) -- all shared types and prototypes
- [third_party/tigr.{h,c}](third_party/) -- TIGR ("TIny GRaphics") by erkkah,
  public-domain single-file graphics + input library

The entire game state lives in one `Game` struct (~12 KB) including the map
grid, mobs, floor items, inventory, knowledge of pseudo-names, message log,
and high-score table.

## Theme

> Bit by bit, the disk is rotting. Every step you take spins the platter
> one more revolution. Every patch you apply, every scroll you execute,
> every monster you defeat reclaims a few more bytes of pristine data.
> Recover the boot record. Don't let the disk die.

## Credits

- Game design + code: this entry
- [TIGR](https://github.com/erkkah/tigr) graphics library by erkkah,
  released into the public domain
- Tiles from [Dungeon Crawl Stone Soup](https://opengameart.org/content/dungeon-crawl-32x32-tiles)
  (rltiles and the DCSS team), released under
  [CC0](http://creativecommons.org/publicdomain/zero/1.0/). CC0 requires no
  attribution, but credit is given gladly. The full tileset is not
  redistributed here; only the handful of tiles used are embedded via
  [tools/gen_assets.py](tools/gen_assets.py).

## License

Original game code: MIT-0 / public domain. Embedded tiles: CC0 (see above).
