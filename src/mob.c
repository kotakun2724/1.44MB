#include "game.h"
#include <stdlib.h>

/* ---- Mob lookup ---------------------------------------------------------*/
Mob *mob_at(Game *g, int x, int y) {
    for (int i = 0; i < g->n_mobs; ++i) {
        Mob *m = &g->mobs[i];
        if (m->alive && m->pos.x == x && m->pos.y == y) return m;
    }
    return NULL;
}

/* ---- Spawning -----------------------------------------------------------*/
int map_collect_floor(const Map *m, V2 *out, int max) {
    int n = 0;
    for (int y = 1; y < MAP_H - 1; ++y) {
        for (int x = 1; x < MAP_W - 1; ++x) {
            if (m->tile[y][x] == T_FLOOR && n < max) {
                out[n].x = x; out[n].y = y; n++;
            }
        }
    }
    return n;
}

static int spawn_one(Game *g, int kind, const V2 *floors, int n_floor, int min_dist2) {
    for (int tries = 0; tries < 64; ++tries) {
        int fi = rng_range(&g->rng, 0, n_floor - 1);
        int fx = floors[fi].x, fy = floors[fi].y;
        if (fx == g->player.pos.x && fy == g->player.pos.y) continue;
        if (mob_at(g, fx, fy)) continue;
        uint8_t tt = g->map.tile[fy][fx];
        if (tt == T_STAIRS_UP || tt == T_STAIRS_DOWN) continue;
        int dx = fx - g->player.pos.x, dy = fy - g->player.pos.y;
        if (dx * dx + dy * dy < min_dist2) continue;

        if (g->n_mobs >= MAX_MOBS) return 0;
        Mob *m = &g->mobs[g->n_mobs++];
        m->kind = kind;
        m->pos.x = fx; m->pos.y = fy;
        m->hp = MOB_TYPES[kind].hp;
        m->alive = 1;
        m->last_seen_turn = 0;
        m->last_seen_pos = (V2){ -1, -1 };
        return 1;
    }
    return 0;
}

static int is_boss_floor(int depth) {
    return depth == 5 || depth == 10 || depth == FINAL_DEPTH;
}

void mob_spawn_floor(Game *g) {
    g->n_mobs = 0;

    int eligible[24], n_elig = 0;
    for (int i = 0; i < MOB_TYPES_N && n_elig < 24; ++i) {
        const MobType *t = &MOB_TYPES[i];
        if (t->boss) continue;
        if (g->player.depth < t->min_depth) continue;
        if (t->max_depth > 0 && g->player.depth > t->max_depth + 2) continue;
        eligible[n_elig++] = i;
    }

    static V2 floors[MAP_W * MAP_H];
    int n_floor = map_collect_floor(&g->map, floors, MAP_W * MAP_H);
    if (n_floor <= 0) return;

    /* Place a boss on checkpoint floors. */
    if (is_boss_floor(g->player.depth)) {
        for (int i = 0; i < MOB_TYPES_N; ++i) {
            if (MOB_TYPES[i].boss && MOB_TYPES[i].min_depth == g->player.depth) {
                spawn_one(g, i, floors, n_floor, 49);
                break;
            }
        }
    }

    if (n_elig > 0) {
        int target = 4 + g->player.depth / 2;
        if (is_boss_floor(g->player.depth)) target = target / 2;
        if (target > MAX_MOBS - 1) target = MAX_MOBS - 1;
        for (int t = 0; t < target; ++t) {
            int kind = eligible[rng_range(&g->rng, 0, n_elig - 1)];
            spawn_one(g, kind, floors, n_floor, 36);
        }
    }
}

/* ---- AI -----------------------------------------------------------------*/
static int sgn(int v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }
static int try_step(Game *g, Mob *m, int dx, int dy) {
    int nx = m->pos.x + dx, ny = m->pos.y + dy;
    if (!map_walkable(&g->map, nx, ny)) return 0;
    if (nx == g->player.pos.x && ny == g->player.pos.y)
        return 1;   /* blocked; combat starts via combat_check_adjacent */
    if (mob_at(g, nx, ny)) return 0;
    m->pos.x = nx; m->pos.y = ny;
    return 1;
}

static void ai_step(Game *g, Mob *m) {
    const MobType *t = &MOB_TYPES[m->kind];
    int sees = g->map.visible[m->pos.y][m->pos.x];
    if (sees) {
        m->last_seen_pos = g->player.pos;
        m->last_seen_turn = g->player.turn;
    }

    /* Erratic mobs sometimes move randomly even when alert. */
    if ((t->ai & AI_ERRATIC) && rng_chance(&g->rng, 50)) {
        int dx = rng_range(&g->rng, -1, 1);
        int dy = rng_range(&g->rng, -1, 1);
        try_step(g, m, dx, dy);
        return;
    }

    /* Teleport behavior: chance to blink within 4 tiles of the player. */
    if ((t->ai & AI_TELEPORT) && sees && rng_chance(&g->rng, 15)) {
        for (int tries = 0; tries < 16; ++tries) {
            int tx = g->player.pos.x + rng_range(&g->rng, -3, 3);
            int ty = g->player.pos.y + rng_range(&g->rng, -3, 3);
            if (map_walkable(&g->map, tx, ty) && !mob_at(g, tx, ty)
                && !(tx == g->player.pos.x && ty == g->player.pos.y)) {
                m->pos.x = tx; m->pos.y = ty;
                return;
            }
        }
    }

    if (t->ai & AI_CHASE) {
        if (m->last_seen_turn == 0) return;     /* haven't noticed yet */
        int dx = sgn(m->last_seen_pos.x - m->pos.x);
        int dy = sgn(m->last_seen_pos.y - m->pos.y);
        if (dx == 0 && dy == 0) {
            m->last_seen_turn = 0;
            return;
        }
        /* Try diagonal first, fall back to cardinal. */
        if (try_step(g, m, dx, dy)) return;
        if (dx && try_step(g, m, dx, 0)) return;
        if (dy && try_step(g, m, 0, dy)) return;
    }
}

void mob_take_turns(Game *g) {
    for (int i = 0; i < g->n_mobs; ++i) {
        Mob *m = &g->mobs[i];
        if (!m->alive) continue;
        ai_step(g, m);
        if (g->state == GS_DEAD) return;
    }
}
