#include "game.h"
#include <stdlib.h>

/* ---- Helpers ------------------------------------------------------------*/
static int potion_index(int def) {
    int idx = 0;
    for (int i = 0; i < ITEMS_N; ++i) {
        if (ITEMS[i].kind == IT_POTION) {
            if (i == def) return idx;
            idx++;
        }
    }
    return -1;
}
static int scroll_index(int def) {
    int idx = 0;
    for (int i = 0; i < ITEMS_N; ++i) {
        if (ITEMS[i].kind == IT_SCROLL) {
            if (i == def) return idx;
            idx++;
        }
    }
    return -1;
}

const char *item_display(const Game *g, int def, char *buf, int buf_n) {
    const ItemDef *d = &ITEMS[def];
    if (g->know.identified[def]) {
        snprintf(buf, buf_n, "%s", d->true_name);
        return buf;
    }
    if (d->kind == IT_POTION) {
        int i = potion_index(def);
        if (i >= 0) {
            snprintf(buf, buf_n, "%s", POTION_ALIASES[g->know.potion_alias[i]]);
            return buf;
        }
    } else if (d->kind == IT_SCROLL) {
        int i = scroll_index(def);
        if (i >= 0) {
            snprintf(buf, buf_n, "%s", SCROLL_ALIASES[g->know.scroll_alias[i]]);
            return buf;
        }
    }
    snprintf(buf, buf_n, "%s", d->true_name);
    return buf;
}

/* ---- Per-run alias shuffle ----------------------------------------------*/
void item_init_knowledge(Game *g) {
    memset(&g->know, 0, sizeof g->know);
    for (int i = 0; i < MAX_POTIONS; ++i) g->know.potion_alias[i] = i;
    for (int i = 0; i < MAX_SCROLLS; ++i) g->know.scroll_alias[i] = i;
    /* Fisher-Yates shuffle. */
    for (int i = MAX_POTIONS - 1; i > 0; --i) {
        int j = rng_range(&g->rng, 0, i);
        int t = g->know.potion_alias[i];
        g->know.potion_alias[i] = g->know.potion_alias[j];
        g->know.potion_alias[j] = t;
    }
    for (int i = MAX_SCROLLS - 1; i > 0; --i) {
        int j = rng_range(&g->rng, 0, i);
        int t = g->know.scroll_alias[i];
        g->know.scroll_alias[i] = g->know.scroll_alias[j];
        g->know.scroll_alias[j] = t;
    }
    /* Always-identified categories. */
    for (int i = 0; i < ITEMS_N; ++i) {
        int k = ITEMS[i].kind;
        if (k == IT_WEAPON || k == IT_ARMOR || k == IT_FOOD) {
            g->know.identified[i] = 1;
        }
    }
}

/* ---- Floor item helpers -------------------------------------------------*/
FloorItem *item_at(Game *g, int x, int y) {
    for (int i = 0; i < g->n_floor; ++i) {
        FloorItem *f = &g->floor[i];
        if (f->active && f->pos.x == x && f->pos.y == y) return f;
    }
    return NULL;
}

static int spawn_item(Game *g, int def, const V2 *floors, int n_floor) {
    for (int tries = 0; tries < 32; ++tries) {
        int fi = rng_range(&g->rng, 0, n_floor - 1);
        int fx = floors[fi].x, fy = floors[fi].y;
        if (fx == g->player.pos.x && fy == g->player.pos.y) continue;
        uint8_t tt = g->map.tile[fy][fx];
        if (tt == T_STAIRS_UP || tt == T_STAIRS_DOWN) continue;
        if (item_at(g, fx, fy)) continue;
        if (g->n_floor >= MAX_FLOOR_ITEMS) return 0;
        FloorItem *fit = &g->floor[g->n_floor++];
        fit->pos.x = fx; fit->pos.y = fy;
        fit->def = def;
        fit->active = 1;
        return 1;
    }
    return 0;
}

void item_spawn_floor(Game *g) {
    g->n_floor = 0;
    static V2 floors[MAP_W * MAP_H];
    int n_floor = map_collect_floor(&g->map, floors, MAP_W * MAP_H);
    if (n_floor <= 0) return;

    int eligible[40], n_elig = 0;
    int food_def = -1;
    for (int i = 0; i < ITEMS_N && n_elig < 40; ++i) {
        if (g->player.depth < ITEMS[i].min_depth) continue;
        if (ITEMS[i].kind == IT_FOOD) food_def = i;
        eligible[n_elig++] = i;
    }
    if (n_elig == 0) return;

    /* Guaranteed food per floor so hunger is fair. */
    if (food_def >= 0) {
        spawn_item(g, food_def, floors, n_floor);
        if (rng_chance(&g->rng, 35))
            spawn_item(g, food_def, floors, n_floor);
    }

    int target = 4 + g->player.depth / 3;
    if (target > MAX_FLOOR_ITEMS - 2) target = MAX_FLOOR_ITEMS - 2;
    for (int t = 0; t < target; ++t) {
        int def = eligible[rng_range(&g->rng, 0, n_elig - 1)];
        spawn_item(g, def, floors, n_floor);
    }
}

/* ---- Inventory ----------------------------------------------------------*/
int inv_first_empty(const Game *g) {
    for (int i = 0; i < INV_SIZE; ++i) if (g->inv[i].def < 0) return i;
    return -1;
}

void item_pickup_under(Game *g) {
    FloorItem *f = item_at(g, g->player.pos.x, g->player.pos.y);
    if (!f) return;
    int slot = inv_first_empty(g);
    if (slot < 0) {
        msg_push(&g->log, "Your inventory is full.");
        return;
    }
    g->inv[slot].def = f->def;
    f->active = 0;
    char buf[64];
    msg_push(&g->log, "Picked up: %c) %s",
             'a' + slot,
             item_display(g, f->def, buf, sizeof buf));
}

/* ---- Effects ------------------------------------------------------------*/
static void apply_effect(Game *g, int def) {
    const ItemDef *d = &ITEMS[def];
    char buf[64];
    item_display(g, def, buf, sizeof buf);

    switch (d->effect) {
        case EFF_HEAL_S: {
            int amt = 8;
            g->player.hp += amt;
            if (g->player.hp > g->player.hp_max) g->player.hp = g->player.hp_max;
            msg_push(&g->log, "You recover %d HP.", amt);
            break;
        }
        case EFF_HEAL_L: {
            int amt = 20;
            g->player.hp += amt;
            if (g->player.hp > g->player.hp_max) g->player.hp = g->player.hp_max;
            msg_push(&g->log, "You feel fully restored! (+%d HP)", amt);
            break;
        }
        case EFF_STR:
            g->player.atk += 1;
            msg_push(&g->log, "Your routines compile cleaner. ATK +1.");
            break;
        case EFF_GUARD:
            g->player.def += 1;
            msg_push(&g->log, "Your shielding hardens. DEF +1.");
            break;
        case EFF_VIGOR:
            g->player.hp_max += 5;
            g->player.hp += 5;
            msg_push(&g->log, "Max HP +5.");
            break;
        case EFF_CORRUPT: {
            int amt = 4;
            g->player.hp -= amt;
            if (g->player.hp <= 0) {
                g->player.hp = 0;
                g->state = GS_DEAD;
                msg_push(&g->log, "The patch corrupts you fatally!");
            } else {
                msg_push(&g->log, "Corruption! You lose %d HP.", amt);
            }
            break;
        }
        case EFF_IDENTIFY: {
            /* Identify a random unidentified item in inventory. */
            int cand[INV_SIZE], n = 0;
            for (int i = 0; i < INV_SIZE; ++i) {
                if (g->inv[i].def >= 0 && !g->know.identified[g->inv[i].def])
                    cand[n++] = i;
            }
            if (n == 0) {
                msg_push(&g->log, "Nothing in your inventory needs identifying.");
            } else {
                int slot = cand[rng_range(&g->rng, 0, n - 1)];
                int target = g->inv[slot].def;
                g->know.identified[target] = 1;
                char nb[64];
                item_display(g, target, nb, sizeof nb);
                msg_push(&g->log, "Identified: %c) %s", 'a' + slot, nb);
            }
            break;
        }
        case EFF_MAPPING:
            for (int y = 0; y < MAP_H; ++y)
                for (int x = 0; x < MAP_W; ++x)
                    if (g->map.tile[y][x] != T_VOID) g->map.known[y][x] = 1;
            msg_push(&g->log, "The sector layout fills your buffers.");
            break;
        case EFF_TELEPORT: {
            static V2 floors[MAP_W * MAP_H];
            int n = map_collect_floor(&g->map, floors, MAP_W * MAP_H);
            for (int t = 0; t < 64 && n > 0; ++t) {
                V2 p = floors[rng_range(&g->rng, 0, n - 1)];
                if (!mob_at(g, p.x, p.y)) {
                    g->player.pos = p;
                    msg_push(&g->log, "You blink across the disk!");
                    break;
                }
            }
            map_compute_fov(&g->map, g->player.pos.x, g->player.pos.y, FOV_RADIUS,
                            g->player.facing);
            break;
        }
        case EFF_LIGHTNING: {
            int hits = 0;
            for (int i = 0; i < g->n_mobs; ++i) {
                Mob *m = &g->mobs[i];
                if (!m->alive) continue;
                if (!g->map.visible[m->pos.y][m->pos.x]) continue;
                int dmg = 8 + rng_range(&g->rng, 0, 4);
                m->hp -= dmg;
                hits++;
                if (m->hp <= 0) {
                    m->alive = 0;
                    g->player.xp += MOB_TYPES[m->kind].xp;
                }
            }
            msg_push(&g->log, "A bolt strikes %d enem%s.", hits, hits == 1 ? "y" : "ies");
            break;
        }
        default: break;
    }
    g->know.identified[def] = 1;
}

void inv_use(Game *g, int slot) {
    if (slot < 0 || slot >= INV_SIZE) return;
    int def = g->inv[slot].def;
    if (def < 0) return;
    const ItemDef *d = &ITEMS[def];

    switch (d->kind) {
        case IT_WEAPON:
            if (g->wielded == slot) {
                g->wielded = -1;
                msg_push(&g->log, "You stow your %s.", d->true_name);
            } else {
                g->wielded = slot;
                msg_push(&g->log, "You wield the %s (+%d ATK).", d->true_name, d->power);
            }
            break;
        case IT_ARMOR:
            if (g->worn == slot) {
                g->worn = -1;
                msg_push(&g->log, "You remove your %s.", d->true_name);
            } else {
                g->worn = slot;
                msg_push(&g->log, "You don the %s (+%d DEF).", d->true_name, d->power);
            }
            break;
        case IT_POTION:
        case IT_SCROLL:
            apply_effect(g, def);
            g->inv[slot].def = -1;        /* consumed */
            if (g->wielded == slot) g->wielded = -1;
            if (g->worn == slot) g->worn = -1;
            break;
        case IT_FOOD: {
            int reduce = d->power > 0 ? d->power * 15 : 300;
            g->player.hunger -= reduce;
            if (g->player.hunger < 0) g->player.hunger = 0;
            msg_push(&g->log, "You munch the cache. Buffer freed.");
            g->inv[slot].def = -1;
            break;
        }
        default: break;
    }
}

void inv_drop(Game *g, int slot) {
    if (slot < 0 || slot >= INV_SIZE) return;
    int def = g->inv[slot].def;
    if (def < 0) return;
    if (item_at(g, g->player.pos.x, g->player.pos.y)) {
        msg_push(&g->log, "Something is already here.");
        return;
    }
    if (g->n_floor >= MAX_FLOOR_ITEMS) {
        msg_push(&g->log, "The floor cannot hold any more items.");
        return;
    }
    FloorItem *f = &g->floor[g->n_floor++];
    f->pos = g->player.pos;
    f->def = def;
    f->active = 1;
    g->inv[slot].def = -1;
    if (g->wielded == slot) g->wielded = -1;
    if (g->worn == slot) g->worn = -1;
    char buf[64];
    msg_push(&g->log, "Dropped: %s", item_display(g, def, buf, sizeof buf));
}

int player_total_atk(const Game *g) {
    int base = g->player.atk;
    if (g->wielded >= 0 && g->inv[g->wielded].def >= 0)
        base += ITEMS[g->inv[g->wielded].def].power;
    return base;
}

int player_total_def(const Game *g) {
    int base = g->player.def;
    if (g->worn >= 0 && g->inv[g->worn].def >= 0)
        base += ITEMS[g->inv[g->worn].def].power;
    return base;
}
