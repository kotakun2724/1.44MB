#include "game.h"
#include <time.h>

/* Returns 1 if a play action was generated; 2 to quit.
 *
 * 3D first-person input scheme (grid-locked, turn-based):
 *   W / S       step forward / backward (relative to facing)
 *   A / D       strafe left / right
 *   Q / E       turn 45 deg left / right (does NOT consume a turn)
 *   . space     wait one turn
 *   >           descend stairs
 * Diagonal / vim / arrow keys are intentionally removed - they don't map
 * cleanly onto a 4-direction first-person view.
 */
static int play_input(Tigr *screen, int c, int *dx, int *dy, int *action,
                      const Player *p) {
    (void)screen;
    *dx = *dy = 0; *action = ACT_NONE;
    int fx = p->facing.x, fy = p->facing.y;
    if (fx == 0 && fy == 0) { fy = -1; }
    /* Right-hand strafe vector = facing rotated 90 deg CW = (-fy, fx). */
    int rx = -fy, ry = fx;
    if (c > 0) {
        switch (c) {
            case 'w': case 'W': *dx =  fx; *dy =  fy; return 1;
            case 's': case 'S': *dx = -fx; *dy = -fy; return 1;
            case 'a': case 'A': *dx = -rx; *dy = -ry; return 1;
            case 'd': case 'D': *dx =  rx; *dy =  ry; return 1;
            case 'q': case 'Q': *action = ACT_TURN_L; return 1;
            case 'e': case 'E': *action = ACT_TURN_R; return 1;
            case '.': case ' ': *action = ACT_WAIT;    return 1;
            case '>':           *action = ACT_DESCEND; return 1;
        }
    }
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

    /* Gameplay shot: pose the player one tile away from a mob, facing it,
     * so the 3D view actually contains an enemy sprite. */
    game_new(&g, 42);
    {
        static const int dxs[4] = {  0,  0, -1,  1 };
        static const int dys[4] = {  1, -1,  0,  0 };
        static const int fxs[4] = {  0,  0,  1, -1 };
        static const int fys[4] = { -1,  1,  0,  0 };
        for (int i = 0; i < g.n_mobs; ++i) {
            if (!g.mobs[i].alive) continue;
            int mx = g.mobs[i].pos.x, my = g.mobs[i].pos.y;
            int placed = 0;
            for (int d = 0; d < 4 && !placed; ++d) {
                int px = mx + dxs[d], py = my + dys[d];
                if (map_walkable(&g.map, px, py)) {
                    g.player.pos    = (V2){ px, py };
                    g.player.facing = (V2){ fxs[d], fys[d] };
                    placed = 1;
                }
            }
            if (placed) break;
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
        } else if (game.state == GS_COMBAT) {
            combat_input(&game, c);
            if (game.state == GS_DEAD) record_run(&game, 1);
            else if (game.state == GS_WIN) record_run(&game, 2);
        } else if (game.state == GS_COMBAT_ITEM) {
            if (c == '3' || esc_pressed) {
                game.state = GS_COMBAT;
            } else if (c >= 'a' && c < 'a' + INV_SIZE) {
                inv_use_in_combat(&game, c - 'a');
                if (game.state == GS_DEAD) record_run(&game, 1);
                else if (game.state == GS_WIN) record_run(&game, 2);
            }
        } else if (game.state == GS_COMBAT_ATTACK) {
            if (c == '1' || esc_pressed) {
                game.state = GS_COMBAT;
            } else if (c >= 'a' && c <= 'e') {
                int slot = c - 'a';
                if (slot < combat_skill_count(&game)) {
                    int skill = combat_skill_at(&game, slot);
                    if (skill >= 0) combat_attack_turn(&game, skill);
                    if (game.state == GS_DEAD) record_run(&game, 1);
                    else if (game.state == GS_WIN) record_run(&game, 2);
                }
            }
        } else if (game.state == GS_LEVEL_UP) {
            levelup_input(&game, c);
            if (game.state == GS_DEAD) record_run(&game, 1);
            else if (game.state == GS_WIN) record_run(&game, 2);
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
                int r = play_input(screen, c, &dx, &dy, &action, &game.player);
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
