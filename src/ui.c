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
        "MOVEMENT",
        "  W A S D        north / west / south / east",
        "  Q E Z C        diagonals (NW / NE / SW / SE)",
        "  arrows         cardinal directions",
        "  H J K L        vim-style aliases",
        "  Y U B N        vim-style diagonals",
        "  .  space       wait a turn",
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
        "Survive deep enough and recover the lost data."
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

/* ---- Main render --------------------------------------------------------*/
void game_render(Game *g, Tigr *screen) {
    switch (g->state) {
        case GS_TITLE:     render_title(screen);            return;
        case GS_HELP:      render_help(screen);             return;
        case GS_HISCORE:   render_hiscore(screen, &g->hiscores); return;
        case GS_INVENTORY: render_inventory(g, screen, g->inv_drop_mode); return;
        default: break;
    }

    tigrClear(screen, C_BG);

    /* ---- Top: message log (newest at top) ---- */
    for (int i = 0; i < LOG_ROWS; ++i) {
        TPixel c = (i == 0) ? C_TEXT : C_DIM;
        puts_at(screen, 1, LOG_TOP + i, c, msg_at(&g->log, i));
    }
    hline(screen, LOG_TOP + LOG_ROWS, C_BORDER);

    /* ---- Middle: map ---- */
    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            draw_tile(screen, x, MAP_TOP + y, x, y, &g->map);
        }
    }

    /* Floor items (visible only). */
    for (int i = 0; i < g->n_floor; ++i) {
        FloorItem *f = &g->floor[i];
        if (!f->active) continue;
        if (!g->map.visible[f->pos.y][f->pos.x]) continue;
        const ItemDef *d = &ITEMS[f->def];
        putc_at(screen, f->pos.x, MAP_TOP + f->pos.y, d->color, d->glyph);
    }

    /* Mobs (only when in current FOV). */
    for (int i = 0; i < g->n_mobs; ++i) {
        Mob *m = &g->mobs[i];
        if (!m->alive) continue;
        if (!g->map.visible[m->pos.y][m->pos.x]) continue;
        const MobType *t = &MOB_TYPES[m->kind];
        putc_at(screen, m->pos.x, MAP_TOP + m->pos.y, t->color, t->glyph);
    }

    /* Player. */
    putc_at(screen, g->player.pos.x, MAP_TOP + g->player.pos.y, C_PLAYER, '@');

    /* ---- Bottom: status bar ---- */
    hline(screen, MAP_BOTTOM, C_BORDER);
    char buf[128];
    int need = 10 * g->player.level;
    const char *hunger_tag =
        g->player.hunger >= HUNGER_STARVING ? " STARVING" :
        g->player.hunger >= HUNGER_HUNGRY   ? " Hungry"   : "";
    int is_boss = (g->player.depth == 5 || g->player.depth == 10 || g->player.depth == FINAL_DEPTH);
    snprintf(buf, sizeof buf,
             "Sector %02d%s  HP %d/%d  ATK %d  DEF %d  Lv %d  XP %d/%d  T %d%s",
             g->player.depth, is_boss ? "!" : " ",
             g->player.hp, g->player.hp_max,
             player_total_atk(g), player_total_def(g),
             g->player.level,
             g->player.xp, need,
             g->player.turn,
             hunger_tag);
    TPixel hp_col = (g->player.hp * 3 < g->player.hp_max) ? tigrRGB(255,100,100) : C_TEXT;
    puts_at(screen, 1, STATUS_TOP, hp_col, buf);

    if (g->state == GS_DEAD) {
        puts_at(screen, 1, STATUS_TOP + 1, tigrRGB(255, 80, 80),
                "*** SEGMENTATION FAULT - press any key ***");
    } else if (g->state == GS_WIN) {
        puts_at(screen, 1, STATUS_TOP + 1, tigrRGB(120, 255, 120),
                "*** DATA RECOVERED - press any key ***");
    } else if (is_boss) {
        puts_at(screen, 1, STATUS_TOP + 1, tigrRGB(255, 180, 80),
                "!! BOSS SECTOR - a corruption awaits !!");
    } else {
        puts_at(screen, 1, STATUS_TOP + 1, C_ACCENT,
                "1.44MB :: Sectors of the Lost Disk");
    }
    puts_at(screen, 1, STATUS_TOP + 2, C_DIM,
            "WASD move  QEZC diag  . wait  > descend  i inv  ? help  ESC quit");
}
