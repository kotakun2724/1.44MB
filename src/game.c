#include "game.h"

/* ---- Message log --------------------------------------------------------*/
void msg_init(MsgLog *log) {
    log->head = 0;
    log->count = 0;
    for (int i = 0; i < MSG_BUF; ++i) log->text[i][0] = '\0';
}

void msg_push(MsgLog *log, const char *fmt, ...) {
    log->head = (log->head + 1) % MSG_BUF;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(log->text[log->head], MSG_LEN, fmt, ap);
    va_end(ap);
    if (log->count < MSG_BUF) log->count++;
}

const char *msg_at(const MsgLog *log, int n) {
    if (n < 0 || n >= log->count) return "";
    int idx = log->head - n;
    while (idx < 0) idx += MSG_BUF;
    return log->text[idx];
}

int map_walkable(const Map *m, int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 0;
    uint8_t t = m->tile[y][x];
    return t == T_FLOOR || t == T_DOOR || t == T_STAIRS_DOWN || t == T_STAIRS_UP;
}

int map_blocks_sight(const Map *m, int x, int y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 1;
    return m->tile[y][x] == T_WALL || m->tile[y][x] == T_VOID;
}

static const V2 FACING8[8] = {
    {0, -1}, {1, -1}, {1, 0}, {1, 1},
    {0,  1}, {-1, 1}, {-1, 0}, {-1, -1}
};

static int facing8_index(int fx, int fy) {
    for (int i = 0; i < 8; ++i)
        if (FACING8[i].x == fx && FACING8[i].y == fy) return i;
    return 0;
}

static V2 facing8_rotate(V2 f, int ccw) {
    int idx = facing8_index(f.x, f.y);
    /* FACING8 is clockwise; CCW (turn left) steps backward. */
    idx = ccw ? (idx + 7) % 8 : (idx + 1) % 8;
    return FACING8[idx];
}

static int can_step_to(const Game *g, int nx, int ny, int dx, int dy) {
    if (!map_walkable(&g->map, nx, ny)) return 0;
    if (dx && dy) {
        if (!map_walkable(&g->map, g->player.pos.x + dx, g->player.pos.y) ||
            !map_walkable(&g->map, g->player.pos.x, g->player.pos.y + dy))
            return 0;
    }
    return 1;
}

/* ---- Game flow ----------------------------------------------------------*/
void game_new(Game *g, uint32_t seed) {
    ScoreList saved = g->hiscores;
    memset(g, 0, sizeof(*g));
    g->hiscores = saved;
    g->run_recorded = 0;
    rng_seed(&g->rng, seed);
    msg_init(&g->log);

    g->player.hp      = g->player.hp_max = 30;
    g->player.atk     = 4;
    g->player.def     = 1;
    g->player.level   = 1;
    g->player.depth   = 1;
    g->player.turn    = 0;
    g->player.facing  = (V2){ 0, -1 };  /* face north on entry */
    g->player.defending = 0;
    g->player.stun_turns = 0;
    g->player.skills.unlocked = (1 << SK_STRIKE);
    g->player.skills.crit_bonus = 0;
    g->player.skills.kill_heal = 0;
    g->player.skills.flee_bonus = 0;
    g->levelup.pending = 0;
    g->levelup.boss_win = 0;
    g->levelup.n_choices = 0;
    g->combat.target_idx = -1;
    g->combat.phase = CP_PLAYER;
    g->combat.n_adjacent = 0;
    g->state          = GS_PLAYING;
    g->wielded        = -1;
    g->worn           = -1;
    g->inv_select     = 0;
    for (int i = 0; i < INV_SIZE; ++i) g->inv[i].def = -1;

    item_init_knowledge(g);
    map_generate(&g->map, &g->rng, 1);
    g->player.pos = g->map.up_stair;
    mob_spawn_floor(g);
    item_spawn_floor(g);

    /* Starting kit: rusty dagger + leather jacket. */
    g->inv[0].def = 0;  /* rusty dagger */
    g->inv[1].def = 6;  /* leather jacket */
    g->wielded = 0;
    g->worn    = 1;

    map_compute_fov(&g->map, g->player.pos.x, g->player.pos.y, FOV_RADIUS,
                    g->player.facing);

    msg_push(&g->log, "You jack into the corrupted floppy.");
    msg_push(&g->log, "WASD move  QE turn  > descend  i inv  ? help");
    g->state = GS_PLAYING;
}

void game_step(Game *g, int dx, int dy, int action) {
    if (g->state != GS_PLAYING) return;
    g->took_turn = 0;

    /* Turn-in-place: rotate facing 45 degrees, do NOT consume a turn. */
    if (action == ACT_TURN_L || action == ACT_TURN_R) {
        V2 f = g->player.facing;
        if (f.x == 0 && f.y == 0) f = (V2){ 0, -1 };
        g->player.facing = facing8_rotate(f, action == ACT_TURN_L);
        map_compute_fov(&g->map, g->player.pos.x, g->player.pos.y, FOV_RADIUS,
                        g->player.facing);
        return;
    }

    if (dx || dy) {
        /* In 3D mode, dx/dy is the world-space step computed from facing
         * (forward / back / strafe). Facing itself is NOT changed by W/S/A/D
         * - use Q/E to turn. */
        int nx = g->player.pos.x + dx;
        int ny = g->player.pos.y + dy;
        Mob *target = mob_at(g, nx, ny);
        if (target) {
            msg_push(&g->log, "The %s blocks your path.",
                     MOB_TYPES[target->kind].name);
        } else if (can_step_to(g, nx, ny, dx, dy)) {
            g->player.pos.x = nx;
            g->player.pos.y = ny;
            item_pickup_under(g);
            g->took_turn = 1;
        } else {
            msg_push(&g->log, "You bump into a bad sector.");
        }
    } else if (action == ACT_WAIT) {
        g->took_turn = 1;
    } else if (action == ACT_DESCEND) {
        if (g->map.tile[g->player.pos.y][g->player.pos.x] == T_STAIRS_DOWN) {
            g->player.depth++;
            msg_push(&g->log, "You descend to sector %d.", g->player.depth);
            map_generate(&g->map, &g->rng, g->player.depth);
            g->player.pos = g->map.up_stair;
            g->player.facing = (V2){ 0, -1 };
            mob_spawn_floor(g);
            item_spawn_floor(g);
            g->took_turn = 1;
        } else {
            msg_push(&g->log, "No stairs down here.");
        }
    }

    if (g->took_turn) {
        g->player.turn++;
        map_compute_fov(&g->map, g->player.pos.x, g->player.pos.y, FOV_RADIUS,
                        g->player.facing);
        mob_take_turns(g);
        if (g->state == GS_PLAYING) {
            int prev_hunger = g->player.hunger;
            g->player.hunger++;
            if (prev_hunger < HUNGER_HUNGRY && g->player.hunger >= HUNGER_HUNGRY)
                msg_push(&g->log, "Your buffers feel low. (Hungry)");
            if (prev_hunger < HUNGER_STARVING && g->player.hunger >= HUNGER_STARVING)
                msg_push(&g->log, "Memory pressure rising! (Starving)");

            /* Slow HP regen, gated by hunger. */
            if (g->player.hunger < HUNGER_HUNGRY &&
                (g->player.turn & 0x0F) == 0 &&
                g->player.hp < g->player.hp_max)
                g->player.hp++;

            /* Starvation damage. */
            if (g->player.hunger >= HUNGER_STARVING &&
                (g->player.turn & 0x07) == 0) {
                g->player.hp--;
                if (g->player.hp <= 0) {
                    g->player.hp = 0;
                    g->state = GS_DEAD;
                    msg_push(&g->log, "Out of memory. You crash.");
                }
            }
            if (g->state == GS_PLAYING)
                combat_check_adjacent(g);
        }
    }
}
