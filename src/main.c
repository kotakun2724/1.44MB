#include "game.h"
#include <time.h>

/* Returns 1 if a play action was generated; 2 to quit. */
static int play_input(Tigr *screen, int c, int *dx, int *dy, int *action) {
    *dx = *dy = 0; *action = ACT_NONE;
    if (c > 0) {
        switch (c) {
            /* WASD primary cardinals. */
            case 'a': case 'A': *dx = -1; return 1;
            case 'd': case 'D': *dx =  1; return 1;
            case 'w': case 'W': *dy = -1; return 1;
            case 's': case 'S': *dy =  1; return 1;
            /* QEZC diagonals. */
            case 'q': case 'Q': *dx = -1; *dy = -1; return 1;
            case 'e': case 'E': *dx =  1; *dy = -1; return 1;
            case 'z': case 'Z': *dx = -1; *dy =  1; return 1;
            case 'c': case 'C': *dx =  1; *dy =  1; return 1;
            /* hjkl / yubn kept as a silent alternative for vim folks. */
            case 'h': case 'H': *dx = -1; return 1;
            case 'l': case 'L': *dx =  1; return 1;
            case 'k': case 'K': *dy = -1; return 1;
            case 'j': case 'J': *dy =  1; return 1;
            case 'y': case 'Y': *dx = -1; *dy = -1; return 1;
            case 'u': case 'U': *dx =  1; *dy = -1; return 1;
            case 'b': case 'B': *dx = -1; *dy =  1; return 1;
            case 'n': case 'N': *dx =  1; *dy =  1; return 1;
            case '.': case ' ': *action = ACT_WAIT;    return 1;
            case '>':           *action = ACT_DESCEND; return 1;
            /* No quit-by-letter during play; use ESC. */
        }
    }
    if (tigrKeyDown(screen, TK_LEFT))  { *dx = -1; return 1; }
    if (tigrKeyDown(screen, TK_RIGHT)) { *dx =  1; return 1; }
    if (tigrKeyDown(screen, TK_UP))    { *dy = -1; return 1; }
    if (tigrKeyDown(screen, TK_DOWN))  { *dy =  1; return 1; }
    return 0;
}

/* Record the current run's outcome into the hiscore table. status:
 * 0=quit, 1=died, 2=won. */
static void record_run(Game *g, int status) {
    if (g->run_recorded) return;
    ScoreEntry e = {0};
    e.score  = score_compute(&g->player);
    e.depth  = (uint16_t)g->player.depth;
    e.level  = (uint16_t)g->player.level;
    e.turns  = (uint32_t)g->player.turn;
    e.status = (uint8_t)status;
    score_insert(&g->hiscores, &e);
    score_save(&g->hiscores);
    g->run_recorded = 1;
}

#ifdef DISK_SCREENSHOT
/* Reveal entire map and recompute FOV for nicer screenshots. */
static void shot_reveal_floor(Game *g) {
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            if (g->map.tile[y][x] != T_VOID) g->map.known[y][x] = 1;
    map_compute_fov(&g->map, g->player.pos.x, g->player.pos.y, FOV_RADIUS,
                    g->player.facing);
}

/* Headless screenshot tool, only compiled with -DDISK_SCREENSHOT. */
static int run_screenshots(void) {
    Tigr *bmp = tigrBitmap(SCREEN_W, SCREEN_H);
    Game g;  memset(&g, 0, sizeof g);

    g.state = GS_TITLE;
    game_render(&g, bmp);
    tigrSaveImage("screenshots/title.png", bmp);

    /* Gameplay shot: descend two floors, reveal map, simulate a fight. */
    game_new(&g, 42);
    game_step(&g, 0, 0, ACT_DESCEND);
    /* Force the player into combat range of a mob if possible. */
    for (int i = 0; i < g.n_mobs; ++i) {
        if (!g.mobs[i].alive) continue;
        if (map_walkable(&g.map, g.mobs[i].pos.x + 1, g.mobs[i].pos.y)) {
            g.player.pos = (V2){ g.mobs[i].pos.x + 1, g.mobs[i].pos.y };
            break;
        }
    }
    msg_push(&g.log, "You hit the bit for 5.");
    msg_push(&g.log, "The byte hits you for 2.");
    shot_reveal_floor(&g);
    game_render(&g, bmp);
    tigrSaveImage("screenshots/gameplay.png", bmp);

    g.state = GS_HELP;
    game_render(&g, bmp);
    tigrSaveImage("screenshots/help.png", bmp);

    game_new(&g, 7);
    g.inv[2].def = 9;  g.inv[3].def = 14; g.inv[4].def = 15;
    g.inv[5].def = 16; g.inv[6].def = 19; g.inv[7].def = 2;
    g.state = GS_INVENTORY;
    game_render(&g, bmp);
    tigrSaveImage("screenshots/inventory.png", bmp);

    tigrFree(bmp);
    return 0;
}
#endif

int main(int argc, char *argv[]) {
#ifdef DISK_SCREENSHOT
    if (argc > 1 && argv[1][0] == 's') return run_screenshots();
#endif
    (void)argc; (void)argv;

    Tigr *screen = tigrWindow(SCREEN_W, SCREEN_H,
                              "1.44MB :: Sectors of the Lost Disk",
                              TIGR_FIXED | TIGR_2X);

    Game game;
    memset(&game, 0, sizeof game);
    game.state = GS_TITLE;
    score_load(&game.hiscores);

    int esc_held = 0;

    while (!tigrClosed(screen) && game.state != GS_QUIT) {
        int c = tigrReadChar(screen);
        int esc_now = tigrKeyDown(screen, TK_ESCAPE);
        int esc_pressed = esc_now && !esc_held;
        esc_held = esc_now;

        if (game.state == GS_TITLE) {
            if (c == 'n' || c == 'N' || c == '\r' || c == '\n') {
                game_new(&game, (uint32_t)time(NULL));
            } else if (c == 's' || c == 'S') {
                game.state = GS_HISCORE;
            } else if (c == 'q' || c == 'Q' || esc_pressed) {
                game.state = GS_QUIT;
            }
        } else if (game.state == GS_PLAYING) {
            if (c == 'i' || c == 'I') {
                game.state = GS_INVENTORY;
                game.inv_drop_mode = 0;
            } else if (c == '?') {
                game.state = GS_HELP;
            } else if (esc_pressed) {
                record_run(&game, 0);
                game.state = GS_TITLE;
            } else {
                int dx, dy, action;
                int r = play_input(screen, c, &dx, &dy, &action);
                if (r == 2) {
                    record_run(&game, 0);
                    game.state = GS_TITLE;
                } else if (r == 1) {
                    game_step(&game, dx, dy, action);
                    if (game.state == GS_DEAD) record_run(&game, 1);
                    else if (game.state == GS_WIN) record_run(&game, 2);
                }
            }
        } else if (game.state == GS_INVENTORY) {
            if (c == 'i' || c == 'I' || esc_pressed) {
                if (game.inv_drop_mode) {
                    /* ESC inside drop-mode just cancels drop-mode. */
                    game.inv_drop_mode = 0;
                } else {
                    game.state = GS_PLAYING;
                }
            } else if (c == 'D') {
                /* Capital D enters drop-mode; next a-p selects slot to drop. */
                game.inv_drop_mode = 1;
            } else if (c >= 'a' && c < 'a' + INV_SIZE) {
                int slot = c - 'a';
                if (game.inv_drop_mode) inv_drop(&game, slot);
                else                    inv_use(&game, slot);
                game.inv_drop_mode = 0;
                game.state = GS_PLAYING;
            }
        } else if (game.state == GS_HELP) {
            if (c != 0 || esc_pressed) game.state = GS_PLAYING;
        } else if (game.state == GS_DEAD || game.state == GS_WIN) {
            if (c != 0 || esc_pressed) game.state = GS_HISCORE;
        } else if (game.state == GS_HISCORE) {
            if (c != 0 || esc_pressed) game.state = GS_TITLE;
        }

        game_render(&game, screen);
        tigrUpdate(screen);
    }

    tigrFree(screen);
    return 0;
}
