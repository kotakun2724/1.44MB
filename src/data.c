#include "game.h"

/* ---- Monster archetypes -------------------------------------------------
 * 5 enemies for the combat milestone. Expanded in the depth milestone.
 *  glyph  color (r,g,b)             name      hp atk def xp ai-flags     mind
 * ------------------------------------------------------------------------*/
const MobType MOB_TYPES[] = {
    /* glyph col(r,g,b,a)            name           hp atk def  xp  ai-flags                    mind maxd boss */
    { 'b', {180,  90,  90, 255}, "bit",            3,  2,  0,  1, AI_ERRATIC,                   1,  4, 0 },
    { 'B', {200, 160,  80, 255}, "byte",           6,  3,  1,  3, AI_CHASE,                     1,  5, 0 },
    { 'w', {180, 220, 100, 255}, "worm",           9,  3,  2,  4, AI_CHASE,                     2,  6, 0 },
    { 'g', {180, 200,  80, 255}, "glitch",         8,  4,  1,  5, AI_CHASE | AI_TELEPORT,       3,  7, 0 },
    { 'v', {200, 100, 200, 255}, "virus",         11,  5,  2,  8, AI_CHASE,                     4,  9, 0 },
    { 's', {200, 200, 255, 255}, "shade",          9,  6,  0,  9, AI_CHASE | AI_TELEPORT,       5, 10, 0 },
    { 'd', {120, 200, 220, 255}, "daemon",        16,  7,  3, 12, AI_CHASE | AI_TOUGH,          6, 11, 0 },
    { 'a', {200, 140, 220, 255}, "archon",        13,  8,  2, 14, AI_CHASE,                     7, 12, 0 },
    { 'p', {220,  90,  90, 255}, "phage",         14,  9,  2, 16, AI_CHASE,                     8, 13, 0 },
    { 'k', {120,  60, 200, 255}, "kraken",        22, 10,  4, 20, AI_CHASE | AI_TOUGH,         10, 14, 0 },
    { 'N', {220, 220, 100, 255}, "null pointer",  18, 11,  3, 22, AI_CHASE | AI_TELEPORT,      11, 14, 0 },

    /* Bosses - one per checkpoint floor. */
    { 'D', {255, 100, 100, 255}, "Defrag Daemon", 40, 11,  5, 60, AI_CHASE | AI_TOUGH,          5,  5, 1 },
    { 'K', {255, 200,  80, 255}, "Kernel Panic",  75, 15,  7,120, AI_CHASE | AI_TOUGH,         10, 10, 1 },
    { 'M', {255, 100, 255, 255}, "Master Boot Record",
                                                 130, 19, 10,300, AI_CHASE | AI_TOUGH,         15, 15, 1 },
};
const int MOB_TYPES_N = (int)(sizeof(MOB_TYPES) / sizeof(MOB_TYPES[0]));

/* ---- Items --------------------------------------------------------------
 *  20 items: 5 weapons, 4 armor, 6 patches (potions), 4 scrolls, 1 food.
 *  Patches and scrolls have ItemEffect; weapons/armor use power as bonus.
 *  glyph color (r,g,b,a)             true_name           kind      pw eff           mind
 * ------------------------------------------------------------------------*/
const ItemDef ITEMS[] = {
    /* Weapons -- glyph ')' */
    { "rusty dagger",          "+1 ATK when wielded",   ')', {180,180,180,255}, IT_WEAPON, 1, EFF_NONE,     1 },
    { "short sword",           "+2 ATK when wielded",   ')', {200,200,220,255}, IT_WEAPON, 2, EFF_NONE,     2 },
    { "broadsword",            "+3 ATK when wielded",   ')', {220,220,255,255}, IT_WEAPON, 3, EFF_NONE,     4 },
    { "warhammer",             "+4 ATK when wielded",   ')', {200,160, 80,255}, IT_WEAPON, 4, EFF_NONE,     6 },
    { "runeblade",             "+6 ATK when wielded",   ')', {120,200,255,255}, IT_WEAPON, 6, EFF_NONE,     9 },

    /* Armor -- glyph ']' */
    { "patched rags",          "+0 DEF when worn",      ']', {160,140,100,255}, IT_ARMOR,  0, EFF_NONE,     1 },
    { "leather jacket",        "+1 DEF when worn",      ']', {180,120, 80,255}, IT_ARMOR,  1, EFF_NONE,     1 },
    { "chainmail",             "+2 DEF when worn",      ']', {180,180,200,255}, IT_ARMOR,  2, EFF_NONE,     3 },
    { "platemail",             "+3 DEF when worn",      ']', {220,220,255,255}, IT_ARMOR,  3, EFF_NONE,     7 },

    /* Potions (patches) -- glyph '!' */
    { "patch of healing",      "restores +8 HP",        '!', {180, 80, 80,255}, IT_POTION, 0, EFF_HEAL_S,   1 },
    { "patch of greater heal", "restores +20 HP",       '!', {255,120,120,255}, IT_POTION, 0, EFF_HEAL_L,   4 },
    { "patch of strength",     "+1 ATK permanent",      '!', {220,180, 80,255}, IT_POTION, 0, EFF_STR,      2 },
    { "patch of guard",        "+1 DEF permanent",      '!', { 80,180,220,255}, IT_POTION, 0, EFF_GUARD,    2 },
    { "patch of vigor",        "+5 max HP",             '!', {180,220, 80,255}, IT_POTION, 0, EFF_VIGOR,    3 },
    { "patch of corruption",   "loses 4 HP - cursed",   '!', {180, 80,180,255}, IT_POTION, 0, EFF_CORRUPT,  1 },

    /* Scrolls -- glyph '?' */
    { "scroll of identify",    "identifies one item",   '?', {220,220,180,255}, IT_SCROLL, 0, EFF_IDENTIFY, 1 },
    { "scroll of mapping",     "reveals the sector",    '?', {180,220,220,255}, IT_SCROLL, 0, EFF_MAPPING,  2 },
    { "scroll of teleport",    "teleports you",         '?', {220,180,220,255}, IT_SCROLL, 0, EFF_TELEPORT, 3 },
    { "scroll of lightning",   "zaps all visible foes", '?', {255,255,120,255}, IT_SCROLL, 0, EFF_LIGHTNING,5 },

    /* Food -- glyph '%' */
    { "data cache",            "frees ~300 memory",     '%', {200,200,120,255}, IT_FOOD,  20, EFF_NONE,     1 }
};
const int ITEMS_N = (int)(sizeof(ITEMS) / sizeof(ITEMS[0]));

/* Pseudo-name pools for unidentified items. */
const char *POTION_ALIASES[MAX_POTIONS] = {
    "red patch", "blue patch", "green patch",
    "amber patch", "violet patch", "white patch"
};
const char *SCROLL_ALIASES[MAX_SCROLLS] = {
    "ALPHA.exe", "BETA.exe", "GAMMA.exe", "DELTA.exe"
};
