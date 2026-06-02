#!/usr/bin/env python3
"""Generate embedded tile assets for the 3D renderer.

Selects a small set of Dungeon Crawl Stone Soup (CC0) 32x32 tiles, normalizes
each to non-interlaced 8-bit RGBA PNG (the only PNG variant TIGR's decoder
handles reliably), and emits src/assets_gen.c + src/assets.h.

The full tileset is NOT committed; only the generated C is. Run this from the
repo root after dropping the "Dungeon Crawl Stone Soup Full" folder in place:

    python3 tools/gen_assets.py
"""
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TILESET = os.path.join(REPO, "Dungeon Crawl Stone Soup Full")
OUT_C = os.path.join(REPO, "src", "assets_gen.c")
OUT_H = os.path.join(REPO, "src", "assets.h")

# --- Asset manifest -------------------------------------------------------
# Paths are relative to the tileset root. Order of MOBS/ITEMS must match the
# order of MOB_TYPES[] / ITEMS[] in src/data.c.
WALLS = [
    "dungeon/wall/crystal_wall_0.png",
    "dungeon/wall/brick_gray_0.png",
    "dungeon/wall/brick_dark_0.png",
    "dungeon/wall/catacombs_0.png",
    "dungeon/wall/crystal_wall_darkgray.png",
    "dungeon/wall/crystal_wall_blue.png",
]
FLOORS = [
    "dungeon/floor/crystal_floor_0.png",
    "dungeon/floor/crystal_floor_2.png",
]
CEIL = "dungeon/wall/brick_dark_0.png"
DOOR = "dungeon/doors/closed_door.png"

MOBS = [
    "monster/eyes/giant_eyeball.png",       # bit
    "monster/eyes/eye_of_draining.png",     # byte
    "monster/animals/brain_worm_new.png",   # worm
    "monster/amorphous/ooze_new.png",       # glitch
    "monster/amorphous/jelly.png",          # virus
    "monster/undead/ghost_new.png",         # shade
    "monster/demons/abomination_small.png", # daemon
    "monster/demons/balrug_new.png",        # archon
    "monster/undead/freezing_wraith.png",   # phage
    "monster/dragons/hydra_3_new.png",      # kraken
    "monster/eyes/great_orb_of_eyes.png",   # null pointer
    "monster/demons/abomination_large.png", # Defrag Daemon (boss)
    "monster/dragons/dragon.png",           # Kernel Panic (boss)
    "monster/undead/ancient_lich_new.png",  # Master Boot Record (boss)
]
ITEMS = [
    "item/weapon/dagger_new.png",              # rusty dagger
    "item/weapon/short_sword_1_new.png",       # short sword
    "item/weapon/elven_broadsword.png",        # broadsword
    "item/weapon/hammer_1_new.png",            # warhammer
    "item/weapon/demon_blade.png",             # runeblade
    "item/armor/torso/animal_skin_1_new.png",  # patched rags
    "item/armor/torso/leather_armor_1.png",    # leather jacket
    "item/armor/torso/chain_mail_1.png",       # chainmail
    "item/armor/torso/plate_mail_1.png",       # platemail
    "item/potion/black_new.png",               # patch of healing
    "item/potion/brilliant_blue_new.png",      # patch of greater heal
    "item/potion/brown_new.png",               # patch of strength
    "item/potion/cyan_new.png",                # patch of guard
    "item/potion/emerald.png",                 # patch of vigor
    "item/potion/magenta_new.png",             # patch of corruption
    "item/scroll/scroll-red.png",              # scroll of identify
    "item/scroll/scroll-blue.png",             # scroll of mapping
    "item/scroll/scroll-green.png",            # scroll of teleport
    "item/scroll/scroll-yellow.png",           # scroll of lightning
    "item/food/bread_ration_new.png",          # data cache
]


def normalize(rel):
    """Return non-interlaced 8-bit RGBA PNG bytes for a tileset-relative path."""
    src = os.path.join(TILESET, rel)
    if not os.path.isfile(src):
        sys.exit("ERROR: asset not found: %s" % src)
    try:
        out = subprocess.run(
            ["magick", src, "-strip", "PNG32:-"],
            check=True, capture_output=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit("ERROR: magick failed on %s: %s" % (rel, e))
    if not out.startswith(b"\x89PNG"):
        sys.exit("ERROR: magick produced non-PNG for %s" % rel)
    return out


def emit_array(fc, name, data):
    fc.write("static const unsigned char %s[] = {\n" % name)
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        fc.write("    " + ",".join("%d" % b for b in chunk) + ",\n")
    fc.write("};\n")


def main():
    if not os.path.isdir(TILESET):
        sys.exit("ERROR: tileset folder missing: %s" % TILESET)

    blobs = []  # (cname, bytes)

    def add(prefix, rel):
        cname = "a_%s_%d" % (prefix, sum(1 for c, _ in blobs if c.startswith("a_" + prefix)))
        data = normalize(rel)
        blobs.append((cname, data))
        return cname

    wall_names = [add("wall", p) for p in WALLS]
    floor_names = [add("floor", p) for p in FLOORS]
    ceil_name = add("ceil", CEIL)
    door_name = add("door", DOOR)
    mob_names = [add("mob", p) for p in MOBS]
    item_names = [add("item", p) for p in ITEMS]

    total = sum(len(d) for _, d in blobs)

    with open(OUT_H, "w") as fh:
        fh.write(
            "/* AUTO-GENERATED by tools/gen_assets.py - DO NOT EDIT.\n"
            " * Embedded Dungeon Crawl Stone Soup tiles (CC0). */\n"
            "#ifndef ASSETS_H\n#define ASSETS_H\n\n"
            "typedef struct { const unsigned char *png; int len; } EmbeddedPng;\n\n"
            "extern const EmbeddedPng ASSET_WALL[];\n"
            "extern const int        ASSET_WALL_N;\n"
            "extern const EmbeddedPng ASSET_FLOOR[];\n"
            "extern const int        ASSET_FLOOR_N;\n"
            "extern const EmbeddedPng ASSET_CEIL;\n"
            "extern const EmbeddedPng ASSET_DOOR;\n"
            "extern const EmbeddedPng ASSET_MOB[];\n"
            "extern const int        ASSET_MOB_N;\n"
            "extern const EmbeddedPng ASSET_ITEM[];\n"
            "extern const int        ASSET_ITEM_N;\n\n"
            "#endif /* ASSETS_H */\n")

    with open(OUT_C, "w") as fc:
        fc.write(
            "/* AUTO-GENERATED by tools/gen_assets.py - DO NOT EDIT.\n"
            " * Embedded Dungeon Crawl Stone Soup tiles (CC0, normalized to\n"
            " * non-interlaced 8-bit RGBA PNG). Total payload: %d bytes. */\n"
            "#include \"assets.h\"\n\n" % total)

        for cname, data in blobs:
            emit_array(fc, cname, data)
        fc.write("\n")

        def table(arr_name, names):
            fc.write("const EmbeddedPng %s[] = {\n" % arr_name)
            for n in names:
                fc.write("    { %s, (int)sizeof %s },\n" % (n, n))
            fc.write("};\n")

        table("ASSET_WALL", wall_names)
        fc.write("const int ASSET_WALL_N = %d;\n" % len(wall_names))
        table("ASSET_FLOOR", floor_names)
        fc.write("const int ASSET_FLOOR_N = %d;\n" % len(floor_names))
        fc.write("const EmbeddedPng ASSET_CEIL = { %s, (int)sizeof %s };\n"
                 % (ceil_name, ceil_name))
        fc.write("const EmbeddedPng ASSET_DOOR = { %s, (int)sizeof %s };\n"
                 % (door_name, door_name))
        table("ASSET_MOB", mob_names)
        fc.write("const int ASSET_MOB_N = %d;\n" % len(mob_names))
        table("ASSET_ITEM", item_names)
        fc.write("const int ASSET_ITEM_N = %d;\n" % len(item_names))

    print("wrote %s (%d blobs, %d bytes of PNG data)"
          % (os.path.relpath(OUT_C, REPO), len(blobs), total))
    print("wrote %s" % os.path.relpath(OUT_H, REPO))


if __name__ == "__main__":
    main()
