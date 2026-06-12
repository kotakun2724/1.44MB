#include "game.h"

/* ---- Palette (limited, CGA-ish) -----------------------------------------*/
static const TPixel C_BG       = {  8,  8, 16, 255 };
static const TPixel C_WALL     = { 90, 80, 60, 255 };
static const TPixel C_WALL_DIM = { 40, 36, 28, 255 };
static const TPixel C_FLOOR    = { 60, 60, 70, 255 };
static const TPixel C_FLOOR_DIM= { 24, 24, 32, 255 };
static const TPixel C_PLAYER   = {255, 255, 80, 255 };
static const TPixel C_STAIRS   = { 80,255, 80, 255 };
static const TPixel C_DOOR     = {200,140, 60, 255 };
static const TPixel C_TEXT     = {220,220,220, 255 };
static const TPixel C_DIM      = {120,120,120, 255 };
static const TPixel C_BORDER   = { 60, 60, 90, 255 };
static const TPixel C_ACCENT   = {120,200,255, 255 };

/* Draw a single ASCII char at cell (cx, cy). */
static void putc_at(Tigr *s, int cx, int cy, TPixel col, char ch) {
    char buf[2] = { ch, 0 };
    tigrPrint(s, tfont, cx * CELL_W, cy * CELL_H, col, buf);
}

static void puts_at(Tigr *s, int cx, int cy, TPixel col, const char *str) {
    tigrPrint(s, tfont, cx * CELL_W, cy * CELL_H, col, "%s", str);
}

/* Print at absolute pixel y (looser line spacing than the cell grid). */
static void puts_px(Tigr *s, int cx, int py, TPixel col, const char *str) {
    tigrPrint(s, tfont, cx * CELL_W, py, col, "%s", str);
}

static void hline(Tigr *s, int cy, TPixel col) {
    for (int x = 0; x < COLS; ++x) putc_at(s, x, cy, col, '-');
}

/* ---- Tile rendering ------------------------------------------------------*/
static void draw_tile(Tigr *s, int cx, int cy, int sx, int sy, const Map *m) {
    int vis = m->visible[sy][sx];
    int kno = m->known[sy][sx];
    if (!vis && !kno) return;
    uint8_t t = m->tile[sy][sx];

    char glyph = ' ';
    TPixel col = C_FLOOR;
    switch (t) {
        case T_WALL:        glyph = '#'; col = vis ? C_WALL : C_WALL_DIM; break;
        case T_FLOOR:       glyph = '.'; col = vis ? C_FLOOR: C_FLOOR_DIM; break;
        case T_DOOR:        glyph = '+'; col = vis ? C_DOOR : C_WALL_DIM; break;
        case T_STAIRS_DOWN: glyph = '>'; col = vis ? C_STAIRS: C_FLOOR_DIM; break;
        case T_STAIRS_UP:   glyph = '<'; col = vis ? C_STAIRS: C_FLOOR_DIM; break;
        default: return;
    }
    putc_at(s, cx, cy, col, glyph);
}

/* ---- Title screen -------------------------------------------------------*/
static void center(Tigr *s, int row, TPixel col, const char *str) {
    int len = (int)strlen(str);
    int x = (COLS - len) / 2;
    if (x < 0) x = 0;
    puts_at(s, x, row, col, str);
}

/* Draw text centered horizontally in pixels, using proportional width. */
static void center_px(Tigr *s, int row, TPixel col, const char *str) {
    int w = tigrTextWidth(tfont, str);
    int x = (SCREEN_W - w) / 2;
    if (x < 0) x = 0;
    tigrPrint(s, tfont, x, row * CELL_H, col, "%s", str);
}

/* Draw text inflated by repeated drawing for a faux-bold effect. */
static void big_center(Tigr *s, int row, TPixel col, const char *str) {
    int w = tigrTextWidth(tfont, str);
    int x = (SCREEN_W - w) / 2;
    if (x < 0) x = 0;
    int y = row * CELL_H;
    /* Bold by 1-pixel offset overdraw. */
    tigrPrint(s, tfont, x + 1, y,     col, "%s", str);
    tigrPrint(s, tfont, x,     y + 1, col, "%s", str);
    tigrPrint(s, tfont, x + 1, y + 1, col, "%s", str);
    tigrPrint(s, tfont, x,     y,     col, "%s", str);
}

static void render_title(Tigr *screen) {
    tigrClear(screen, C_BG);

    /* Decorative frame using box-drawing chars (ASCII). */
    TPixel frame = tigrRGB(60, 60, 90);
    for (int x = 2; x < COLS - 2; ++x) {
        putc_at(screen, x, 2,         frame, '=');
        putc_at(screen, x, ROWS - 3,  frame, '=');
    }

    /* Big title - rendered as bold proportional text, centered in pixels. */
    big_center(screen, 7,  tigrRGB(120, 200, 255), "1.44 MB");
    big_center(screen, 10, tigrRGB(200, 220, 255), "Sectors of the Lost Disk");

    center_px(screen, 14, tigrRGB(140, 140, 160),
              "a roguelike that fits on a 3.5\" floppy");

    /* Menu. */
    big_center(screen, 21, tigrRGB(255, 255, 120), "[ N ]  New Game");
    center_px (screen, 24, tigrRGB(200, 200, 200), "[ S ]  High Scores");
    center_px (screen, 26, tigrRGB(200, 200, 200), "[ Q ]  Quit");

    center_px(screen, 33, tigrRGB(140, 140, 140),
              "Recover the lost sectors before the disk dies.");
    center_px(screen, 35, tigrRGB(100, 100, 100),
              "1,474,560 bytes - not a byte more.");
    center_px(screen, ROWS - 5, tigrRGB(80, 80, 110),
              "v1.0  -  C99 + TIGR  -  built " __DATE__);
}

/* ---- Help screen --------------------------------------------------------*/
static void render_help(Tigr *screen) {
    tigrClear(screen, C_BG);
    center(screen, 2, C_ACCENT, "==== HELP ====");

    static const char *lines[] = {
        "MOVEMENT (first-person, grid + turn-based)",
        "  W              step forward",
        "  S              step backward",
        "  A              strafe left",
        "  D              strafe right",
        "  Q              turn 45 deg left   (no turn consumed)",
        "  E              turn 45 deg right  (no turn consumed)",
        "  .  space       wait one turn",
        "",
        "ACTIONS",
        "  >              descend stairs",
        "  i              open inventory",
        "  ?              this help screen",
        "  ESC            abandon run / close menu",
        "",
        "INVENTORY",
        "  a-p            use or equip the selected slot",
        "  D <letter>     drop the selected slot (capital D)",
        "  i / ESC        close inventory",
        "",
        "ITEMS",
        "  )  weapon  ]  armor  !  patch (potion)",
        "  ?  scroll  %  food   >  stairs down",
        "",
        "ENEMIES (low -> high)",
        "  b bit   B byte   w worm   g glitch   v virus",
        "  s shade   d daemon   a archon   p phage",
        "  k kraken   N null pointer",
        "  D / K / M  bosses on floors 5 / 10 / 15",
        "",
        "COMBAT (auto-starts when adjacent to a foe)",
        "  1  Attack   2  Defend   3  Item   4  Flee",
        "  T            retarget (multiple foes)",
        "  1-9          pick target when ambushed",
        "  a-e          pick attack skill (if unlocked)",
        "  a-p          use consumable (in item menu)",
        "",
        "LEVEL UP: pick 1 of 3 upgrades when XP threshold is met.",
        "Crits, misses, and stuns can turn the fight.",
        "The minimap (bottom right) shows explored layout."
    };
    int n = (int)(sizeof(lines) / sizeof(lines[0]));
    int line_h = 11;
    int y = 4 * CELL_H;
    for (int i = 0; i < n; ++i) {
        const char *s = lines[i];
        TPixel col = (s[0] == ' ' || s[0] == 0) ? C_TEXT : C_ACCENT;
        puts_px(screen, 4, y, col, s);
        y += line_h;
        if (y >= (ROWS - 2) * CELL_H) break;
    }
    center(screen, ROWS - 2, C_DIM, "[ press any key to return ]");
}

/* ---- High scores --------------------------------------------------------*/
static void render_hiscore(Tigr *screen, const ScoreList *list) {
    tigrClear(screen, C_BG);
    center(screen, 2, C_ACCENT, "==== HIGH SCORES ====");

    if (list->count == 0) {
        center(screen, 12, C_DIM, "(no runs recorded yet)");
    } else {
        puts_at(screen, 8, 5, C_TEXT,
                "rank  score   depth  level  turns   result");
        for (uint32_t i = 0; i < list->count && i < MAX_SCORES; ++i) {
            const ScoreEntry *e = &list->entries[i];
            const char *status =
                e->status == 0 ? "quit" :
                e->status == 1 ? "died" :
                e->status == 2 ? "won " : "?";
            char buf[80];
            snprintf(buf, sizeof buf,
                     "%2u.  %6u   %4d   %4d   %5u   %s",
                     i + 1, e->score, e->depth, e->level, e->turns, status);
            puts_at(screen, 8, 7 + i, C_TEXT, buf);
        }
    }
    center(screen, ROWS - 2, C_DIM, "[ press any key to return ]");
}

/* ---- Inventory screen ---------------------------------------------------*/
static void render_inventory(Game *g, Tigr *screen, int drop_mode) {
    tigrClear(screen, C_BG);
    puts_at(screen, 2, 1, C_ACCENT, "==== INVENTORY ====");
    if (drop_mode) {
        puts_at(screen, 2, 3, tigrRGB(255, 160, 80),
                "DROP MODE: press a-p to drop a slot   ESC to cancel");
    } else {
        puts_at(screen, 2, 3, C_DIM,
                "a-p: use/equip   D: drop mode   i/ESC: close");
    }

    char buf[120];
    int line_h = 11;
    int top_y  = 5 * CELL_H;
    int y = top_y;
    int any = 0;
    for (int i = 0; i < INV_SIZE; ++i) {
        int def = g->inv[i].def;
        if (def < 0) continue;
        any = 1;

        char nm[64];
        item_display(g, def, nm, sizeof nm);
        const ItemDef *d = &ITEMS[def];
        const char *tag = "";
        if (i == g->wielded) tag = " (wielded)";
        else if (i == g->worn) tag = " (worn)";

        /* Description: hidden for unidentified potions/scrolls. */
        const char *desc = d->desc ? d->desc : "";
        int show_desc = 1;
        if ((d->kind == IT_POTION || d->kind == IT_SCROLL)
            && !g->know.identified[def]) show_desc = 0;

        if (show_desc) {
            snprintf(buf, sizeof buf, "%c) %c %-22s%-12s   %s",
                     'a' + i, d->glyph, nm, tag, desc);
        } else {
            snprintf(buf, sizeof buf, "%c) %c %-22s%-12s   (unidentified)",
                     'a' + i, d->glyph, nm, tag);
        }
        puts_px(screen, 2, y, d->color, buf);
        y += line_h;
    }
    if (!any) puts_px(screen, 2, y, C_DIM, "(empty)");

    snprintf(buf, sizeof buf,
             "Sector %02d   HP %d/%d   ATK %d   DEF %d   Lv %d   T %d",
             g->player.depth,
             g->player.hp, g->player.hp_max,
             player_total_atk(g), player_total_def(g),
             g->player.level, g->player.turn);
    puts_at(screen, 1, ROWS - 2, C_TEXT, buf);
}

/* ---- Combat item picker (consumables only) ------------------------------*/
static void render_combat_items(Game *g, Tigr *screen) {
    tigrFillRect(screen, 0, 200, SCREEN_W, 80, tigrRGBA(8, 8, 16, 220));
    puts_at(screen, 2, 26, C_ACCENT, "==== COMBAT ITEMS (a-p) ====");
    puts_at(screen, 2, 28, C_DIM, "patches / scrolls / food only   3 or ESC back");
    char buf[120];
    int y = 30 * CELL_H;
    int any = 0;
    for (int i = 0; i < INV_SIZE; ++i) {
        int def = g->inv[i].def;
        if (def < 0) continue;
        const ItemDef *d = &ITEMS[def];
        if (d->kind != IT_POTION && d->kind != IT_SCROLL && d->kind != IT_FOOD)
            continue;
        any = 1;
        char nm[64];
        item_display(g, def, nm, sizeof nm);
        snprintf(buf, sizeof buf, "%c) %c %-24s  %s",
                 'a' + i, d->glyph, nm, d->desc ? d->desc : "");
        puts_px(screen, 2, y, d->color, buf);
        y += 11;
    }
    if (!any) puts_px(screen, 2, y, C_DIM, "(no usable items)");
}

/* ---- Combat attack skill picker -----------------------------------------*/
static void render_combat_attacks(Game *g, Tigr *screen) {
    tigrFillRect(screen, 0, 200, SCREEN_W, 80, tigrRGBA(8, 8, 16, 220));
    puts_at(screen, 2, 26, C_ACCENT, "==== ATTACK SKILLS (a-e) ====");
    puts_at(screen, 2, 28, C_DIM, "select a skill   1 or ESC back");
    char buf[120];
    int y = 30 * CELL_H;
    int n = combat_skill_count(g);
    for (int i = 0; i < n; ++i) {
        int sid = combat_skill_at(g, i);
        if (sid < 0) continue;
        const char *desc = "";
        switch (sid) {
            case SK_STRIKE:   desc = "standard hit"; break;
            case SK_POWER:    desc = "1.5x dmg, +10% miss"; break;
            case SK_PING:     desc = "fast hit, +25% crit"; break;
            case SK_OVERFLOW: desc = "ignore half DEF, +2"; break;
            case SK_SWEEP:    desc = "60% dmg to all adjacent"; break;
            default: break;
        }
        snprintf(buf, sizeof buf, "%c) %-18s  %s",
                 'a' + i, combat_skill_name(sid), desc);
        puts_px(screen, 2, y, C_TEXT, buf);
        y += 11;
    }
}

/* ---- Main render --------------------------------------------------------*/
void game_render(Game *g, Tigr *screen) {
    switch (g->state) {
        case GS_TITLE:     render_title(screen);            return;
        case GS_HELP:      render_help(screen);             return;
        case GS_HISCORE:   render_hiscore(screen, &g->hiscores); return;
        case GS_INVENTORY: render_inventory(g, screen, g->inv_drop_mode); return;
        default: break;
    }

    int in_combat = (g->state == GS_COMBAT || g->state == GS_COMBAT_ITEM
                     || g->state == GS_COMBAT_ATTACK);
    int in_levelup = (g->state == GS_LEVEL_UP);

    tigrClear(screen, C_BG);

    /* Layout (640x400):
     *   0   .. 279   3D viewport  (full width, 280 high)
     *   280          1px border
     *   281 .. 399   bottom panel: log (left), status (left), minimap (right)
     */
    const int V3D_X = 0,   V3D_Y = 0,   V3D_W = SCREEN_W, V3D_H = 280;
    const int MINI_CELL = 2;                              /* px per tile */
    const int MINI_W = MAP_W * MINI_CELL;                 /* 160 */
    const int MINI_H = MAP_H * MINI_CELL;                 /* 80  */
    const int MINI_X = SCREEN_W - MINI_W - 2;             /* 478 */
    const int MINI_Y = V3D_H + 10;                        /* 290 */
    const int PANEL_TOP = V3D_H + 1;                      /* 281 */
    const int LEFT_PAD = 8;
    const int LOG_PX_Y = PANEL_TOP + 1;                   /* 282 */
    const int LOG_LINE_H = 10;
    const int STAT_PX_Y = LOG_PX_Y + LOG_ROWS * LOG_LINE_H + 2;  /* 314 */

    /* 3D viewport. */
    render3d_draw(g, screen, V3D_X, V3D_Y, V3D_W, V3D_H);

    /* Border between 3D view and bottom panel. */
    for (int x = 0; x < SCREEN_W; ++x) tigrPlot(screen, x, V3D_H, C_BORDER);

    if (in_combat || in_levelup) {
        combat_render_hud(g, screen, 0, PANEL_TOP, SCREEN_W, SCREEN_H - PANEL_TOP);
        if (g->state == GS_COMBAT_ITEM)
            render_combat_items(g, screen);
        if (g->state == GS_COMBAT_ATTACK)
            render_combat_attacks(g, screen);
        if (g->state == GS_LEVEL_UP)
            levelup_render(g, screen);
    } else {
        /* Message log (newest at top). */
        for (int i = 0; i < LOG_ROWS; ++i) {
            TPixel c = (i == 0) ? C_TEXT : C_DIM;
            puts_px(screen, LEFT_PAD / CELL_W, LOG_PX_Y + i * LOG_LINE_H, c,
                    msg_at(&g->log, i));
        }

        /* Status line + tagline. */
        char buf[128];
        int need = 10 * g->player.level;
        const char *hunger_tag =
            g->player.hunger >= HUNGER_STARVING ? " STARVING" :
            g->player.hunger >= HUNGER_HUNGRY   ? " Hungry"   : "";
        int is_boss = (g->player.depth == 5 || g->player.depth == 10
                       || g->player.depth == FINAL_DEPTH);
        snprintf(buf, sizeof buf,
                 "Sec %02d%s HP %d/%d ATK %d DEF %d Lv %d XP %d/%d T %d%s",
                 g->player.depth, is_boss ? "!" : " ",
                 g->player.hp, g->player.hp_max,
                 player_total_atk(g), player_total_def(g),
                 g->player.level,
                 g->player.xp, need,
                 g->player.turn,
                 hunger_tag);
        TPixel hp_col = (g->player.hp * 3 < g->player.hp_max)
            ? tigrRGB(255, 100, 100) : C_TEXT;
        puts_px(screen, LEFT_PAD / CELL_W, STAT_PX_Y, hp_col, buf);

        if (g->state == GS_DEAD) {
            puts_px(screen, LEFT_PAD / CELL_W, STAT_PX_Y + LOG_LINE_H,
                    tigrRGB(255, 80, 80),
                    "*** SEGMENTATION FAULT - press any key ***");
        } else if (g->state == GS_WIN) {
            puts_px(screen, LEFT_PAD / CELL_W, STAT_PX_Y + LOG_LINE_H,
                    tigrRGB(120, 255, 120),
                    "*** DATA RECOVERED - press any key ***");
        } else if (is_boss) {
            puts_px(screen, LEFT_PAD / CELL_W, STAT_PX_Y + LOG_LINE_H,
                    tigrRGB(255, 180, 80),
                    "!! BOSS SECTOR - a corruption awaits !!");
        } else {
            puts_px(screen, LEFT_PAD / CELL_W, STAT_PX_Y + LOG_LINE_H, C_DIM,
                    "WASD move  QE turn  . wait  > descend  i inv  ? help");
        }
    }

    /* Minimap (top-right of bottom panel). */
    /* 1px frame around it. */
    for (int x = -1; x <= MINI_W; ++x) {
        tigrPlot(screen, MINI_X + x, MINI_Y - 1,       C_BORDER);
        tigrPlot(screen, MINI_X + x, MINI_Y + MINI_H,  C_BORDER);
    }
    for (int y = -1; y <= MINI_H; ++y) {
        tigrPlot(screen, MINI_X - 1,        MINI_Y + y, C_BORDER);
        tigrPlot(screen, MINI_X + MINI_W,   MINI_Y + y, C_BORDER);
    }
    render3d_minimap(g, screen, MINI_X, MINI_Y, MINI_CELL);
}
