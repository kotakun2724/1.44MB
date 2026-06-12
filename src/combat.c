#include "game.h"
#include "assets.h"

/* HUD icon indices (match tools/gen_assets.py HUD_ICONS order). */
enum {
    HUD_ATTACK = 0,
    HUD_DEFEND,
    HUD_ITEM,
    HUD_FLEE,
    HUD_N
};

static Tigr *g_hud[HUD_N];
static int   g_hud_init = 0;

static void hud_init(void) {
    if (g_hud_init) return;
    g_hud_init = 1;
    for (int i = 0; i < HUD_N && i < ASSET_HUD_N; ++i)
        g_hud[i] = tigrLoadImageMem(ASSET_HUD[i].png, ASSET_HUD[i].len);
}

Mob *combat_target(Game *g) {
    int idx = g->combat.target_idx;
    if (idx < 0 || idx >= g->n_mobs) return NULL;
    Mob *m = &g->mobs[idx];
    return m->alive ? m : NULL;
}

static int cheb_dist(int x0, int y0, int x1, int y1) {
    int dx = x0 - x1; if (dx < 0) dx = -dx;
    int dy = y0 - y1; if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

static int mob_is_adjacent(const Game *g, const Mob *m) {
    if (!m->alive) return 0;
    if (!g->map.visible[m->pos.y][m->pos.x]) return 0;
    return cheb_dist(g->player.pos.x, g->player.pos.y, m->pos.x, m->pos.y) <= 1;
}

static void combat_refresh_adjacent(Game *g) {
    CombatState *c = &g->combat;
    c->n_adjacent = 0;
    for (int i = 0; i < g->n_mobs && c->n_adjacent < 8; ++i) {
        if (mob_is_adjacent(g, &g->mobs[i]))
            c->adjacent[c->n_adjacent++] = i;
    }
}

static void combat_face_target(Game *g, const Mob *m) {
    int dx = m->pos.x - g->player.pos.x;
    int dy = m->pos.y - g->player.pos.y;
    if (dx > 1) dx = 1; else if (dx < -1) dx = -1;
    if (dy > 1) dy = 1; else if (dy < -1) dy = -1;
    if (dx == 0 && dy == 0) dy = -1;
    g->player.facing = (V2){ dx, dy };
    map_compute_fov(&g->map, g->player.pos.x, g->player.pos.y, FOV_RADIUS,
                    g->player.facing);
}

static void combat_begin(Game *g) {
    combat_refresh_adjacent(g);
    if (g->combat.n_adjacent == 0) return;

    g->state = GS_COMBAT;
    g->combat.hud_sel = 0;

    if (g->combat.n_adjacent == 1) {
        g->combat.target_idx = g->combat.adjacent[0];
        g->combat.phase = CP_PLAYER;
        combat_face_target(g, &g->mobs[g->combat.target_idx]);
        msg_push(&g->log, "** Combat! **");
    } else {
        g->combat.target_idx = -1;
        g->combat.phase = CP_TARGET;
        msg_push(&g->log, "** Ambush! Pick a target (1-%d). **",
                 g->combat.n_adjacent);
    }
}

void combat_check_adjacent(Game *g) {
    if (g->state != GS_PLAYING) return;
    combat_refresh_adjacent(g);
    if (g->combat.n_adjacent > 0)
        combat_begin(g);
}

static int combat_select_by_slot(Game *g, int slot) {
    if (slot < 0 || slot >= g->combat.n_adjacent) return 0;
    g->combat.target_idx = g->combat.adjacent[slot];
    g->combat.phase = CP_PLAYER;
    combat_face_target(g, &g->mobs[g->combat.target_idx]);
    const MobType *t = &MOB_TYPES[g->mobs[g->combat.target_idx].kind];
    msg_push(&g->log, "You engage the %s.", t->name);
    return 1;
}

static const char *SKILL_NAMES[SK_MAX] = {
    "Strike", "Overclock", "Quick Ping", "Buffer Overflow", "Defrag Sweep"
};

const char *combat_skill_name(int skill_id) {
    if (skill_id < 0 || skill_id >= SK_MAX) return "?";
    return SKILL_NAMES[skill_id];
}

int combat_skill_count(const Game *g) {
    int n = 0;
    for (int i = 0; i < SK_MAX; ++i)
        if (g->player.skills.unlocked & (1 << i)) n++;
    return n;
}

int combat_skill_at(const Game *g, int slot) {
    int n = 0;
    for (int i = 0; i < SK_MAX; ++i) {
        if (g->player.skills.unlocked & (1 << i)) {
            if (n == slot) return i;
            n++;
        }
    }
    return -1;
}

static int combat_weapon_bonus(const Game *g) {
    if (g->wielded >= 0 && g->inv[g->wielded].def >= 0)
        return ITEMS[g->inv[g->wielded].def].power;
    return 0;
}

static int combat_crit_chance(const Game *g, int skill_id, int bonus) {
    int weapon_bonus = combat_weapon_bonus(g);
    int crit = 10 + weapon_bonus * 2 + g->player.skills.crit_bonus + bonus;
    if (crit > 50) crit = 50;
    return crit;
}

static void combat_apply_kill_heal(Game *g) {
    int heal = g->player.skills.kill_heal;
    if (heal <= 0) return;
    g->player.hp += heal;
    if (g->player.hp > g->player.hp_max) g->player.hp = g->player.hp_max;
    msg_push(&g->log, "Data drain restores %d HP.", heal);
}

void combat_mob_defeat(Game *g, Mob *m, int crit_msg) {
    const MobType *t = &MOB_TYPES[m->kind];
    m->alive = 0;
    g->player.xp += t->xp;
    combat_apply_kill_heal(g);
    if (t->boss) {
        msg_push(&g->log, "** %s collapses into clean bytes! **", t->name);
    } else {
        msg_push(&g->log, crit_msg ? "CRIT! You shatter the %s (+%d xp)."
                                   : "You defeat the %s (+%d xp).",
                 t->name, t->xp);
    }
    int need = 10 * g->player.level;
    while (g->player.xp >= need) {
        g->player.xp -= need;
        g->player.level++;
        g->player.hp_max += 2;
        g->player.hp = g->player.hp_max;
        g->levelup.pending++;
        need = 10 * g->player.level;
    }
    if (t->boss && g->player.depth >= FINAL_DEPTH)
        g->levelup.boss_win = 1;
    levelup_queue(g);
}

static void combat_hit_mob(Game *g, Mob *m, int dmg, int crit, const char *verb) {
    const MobType *t = &MOB_TYPES[m->kind];
    if (dmg < 1) dmg = 1;
    m->hp -= dmg;
    if (m->hp <= 0) {
        combat_mob_defeat(g, m, crit);
    } else {
        msg_push(&g->log, crit ? "CRIT! %s the %s for %d."
                                : "%s the %s for %d.",
                 verb, t->name, dmg);
        if (crit) {
            int stun_chance = (t->ai & AI_TOUGH) ? 15 : 40;
            if (rng_chance(&g->rng, stun_chance)) {
                m->stun_turns = 1;
                msg_push(&g->log, "The %s is stunned!", t->name);
            }
        }
    }
}

static void combat_use_skill(Game *g, Mob *m, int skill_id) {
    const MobType *t = &MOB_TYPES[m->kind];
    int miss_chance = 5;
    if (t->ai & AI_ERRATIC) miss_chance += 5;

    switch (skill_id) {
        case SK_POWER: miss_chance += 10; break;
        default: break;
    }

    if (rng_chance(&g->rng, miss_chance)) {
        msg_push(&g->log, "You miss the %s!", t->name);
        return;
    }

    int atk = player_total_atk(g);
    int def = t->def;
    int crit_bonus = 0;
    int dmg;
    const char *verb = "You hit";

    switch (skill_id) {
        case SK_STRIKE:
            dmg = atk - def + rng_range(&g->rng, -1, 1);
            break;
        case SK_POWER:
            dmg = (atk - def + rng_range(&g->rng, -1, 1)) * 3 / 2;
            verb = "You overclock-strike";
            break;
        case SK_PING:
            dmg = (atk - def + rng_range(&g->rng, -1, 1)) * 4 / 5;
            crit_bonus = 25;
            verb = "You ping";
            break;
        case SK_OVERFLOW:
            def = def / 2;
            dmg = atk - def + 2 + rng_range(&g->rng, -1, 1);
            verb = "You overflow";
            break;
        default:
            dmg = atk - def + rng_range(&g->rng, -1, 1);
            break;
    }

    int crit = rng_chance(&g->rng, combat_crit_chance(g, skill_id, crit_bonus));
    if (crit) dmg = dmg * 2 + 1;
    combat_hit_mob(g, m, dmg, crit, verb);
}

static void combat_after_player_action(Game *g);

static void combat_use_sweep(Game *g) {
    combat_refresh_adjacent(g);
    if (g->combat.n_adjacent == 0) {
        msg_push(&g->log, "No adjacent targets to sweep.");
        return;
    }
    msg_push(&g->log, "You defrag-sweep adjacent foes!");
    int atk = player_total_atk(g);
    for (int i = 0; i < g->combat.n_adjacent; ++i) {
        Mob *m = &g->mobs[g->combat.adjacent[i]];
        if (!m->alive) continue;
        const MobType *t = &MOB_TYPES[m->kind];
        int dmg = (atk - t->def + rng_range(&g->rng, -1, 1)) * 3 / 5;
        if (dmg < 1) dmg = 1;
        combat_hit_mob(g, m, dmg, 0, "Sweep hits");
        if (g->state == GS_LEVEL_UP || g->state == GS_DEAD || g->state == GS_WIN)
            return;
    }
}

void combat_attack_turn(Game *g, int skill_id) {
    g->state = GS_COMBAT;
    if (skill_id == SK_SWEEP) {
        combat_use_sweep(g);
    } else {
        Mob *m = combat_target(g);
        if (m && mob_is_adjacent(g, m))
            combat_use_skill(g, m, skill_id);
    }
    if (g->state == GS_LEVEL_UP) return;
    combat_after_player_action(g);
}

static void combat_mob_attack_player(Game *g, Mob *m) {
    const MobType *t = &MOB_TYPES[m->kind];

    if (m->stun_turns > 0) {
        m->stun_turns--;
        msg_push(&g->log, "The %s is stunned.", t->name);
        return;
    }

    /* Teleport out of melee during combat. */
    if ((t->ai & AI_TELEPORT) && rng_chance(&g->rng, 10)) {
        for (int tries = 0; tries < 16; ++tries) {
            int tx = g->player.pos.x + rng_range(&g->rng, -4, 4);
            int ty = g->player.pos.y + rng_range(&g->rng, -4, 4);
            if (!map_walkable(&g->map, tx, ty)) continue;
            if (mob_at(g, tx, ty)) continue;
            if (cheb_dist(tx, ty, g->player.pos.x, g->player.pos.y) <= 1) continue;
            m->pos.x = tx; m->pos.y = ty;
            msg_push(&g->log, "The %s blinks away!", t->name);
            return;
        }
    }

    int def = player_total_def(g);
    if (g->player.defending) def += 2;
    int dmg = t->atk - def + rng_range(&g->rng, -1, 1);
    if (g->player.defending) {
        dmg = dmg / 2;
        g->player.defending = 0;
    }
    int crit = rng_chance(&g->rng, 5);
    if (crit) dmg = dmg * 2;
    if (dmg < 1) dmg = 1;

    g->player.hp -= dmg;
    msg_push(&g->log, crit ? "The %s crit-strikes you for %d!"
                            : "The %s hits you for %d.",
             t->name, dmg);

    if (rng_chance(&g->rng, 8)) {
        g->player.stun_turns = 1;
        msg_push(&g->log, "You are stunned!");
    }

    if (g->player.hp <= 0) {
        g->player.hp = 0;
        g->state = GS_DEAD;
        msg_push(&g->log, "Segmentation fault. You are deallocated.");
    }
}

static void combat_end_turn(Game *g) {
    g->player.turn++;
    g->player.defending = 0;

    int prev_hunger = g->player.hunger;
    g->player.hunger++;
    if (prev_hunger < HUNGER_HUNGRY && g->player.hunger >= HUNGER_HUNGRY)
        msg_push(&g->log, "Your buffers feel low. (Hungry)");
    if (prev_hunger < HUNGER_STARVING && g->player.hunger >= HUNGER_STARVING)
        msg_push(&g->log, "Memory pressure rising! (Starving)");

    if (g->player.hunger < HUNGER_HUNGRY &&
        (g->player.turn & 0x0F) == 0 &&
        g->player.hp < g->player.hp_max)
        g->player.hp++;

    if (g->player.hunger >= HUNGER_STARVING &&
        (g->player.turn & 0x07) == 0) {
        g->player.hp--;
        if (g->player.hp <= 0) {
            g->player.hp = 0;
            g->state = GS_DEAD;
            msg_push(&g->log, "Out of memory. You crash.");
        }
    }
}

static void combat_leave(Game *g) {
    g->state = GS_PLAYING;
    g->combat.target_idx = -1;
    g->combat.phase = CP_PLAYER;
    g->combat.n_adjacent = 0;
}

static int combat_try_flee(Game *g, Mob *m) {
    const MobType *t = &MOB_TYPES[m->kind];
    int chance = 50 + g->player.level - t->min_depth + g->player.skills.flee_bonus;
    if (chance < 20) chance = 20;
    if (chance > 90) chance = 90;

    if (!rng_chance(&g->rng, chance)) {
        msg_push(&g->log, "You fail to retreat!");
        return 0;
    }

    int dx = g->player.pos.x - m->pos.x;
    int dy = g->player.pos.y - m->pos.y;
    if (dx > 1) dx = 1; else if (dx < -1) dx = -1;
    if (dy > 1) dy = 1; else if (dy < -1) dy = -1;
    if (dx == 0 && dy == 0) dy = 1;

    int nx = g->player.pos.x + dx;
    int ny = g->player.pos.y + dy;
    if (!map_walkable(&g->map, nx, ny) || mob_at(g, nx, ny)) {
        msg_push(&g->log, "No escape route!");
        return 0;
    }

    g->player.pos.x = nx;
    g->player.pos.y = ny;
    g->player.facing = (V2){ dx, dy };
    map_compute_fov(&g->map, g->player.pos.x, g->player.pos.y, FOV_RADIUS,
                    g->player.facing);
    msg_push(&g->log, "You break away from combat!");
    combat_leave(g);
    return 1;
}

void combat_item_turn(Game *g) {
    g->state = GS_COMBAT;
    combat_after_player_action(g);
}

void combat_resume_after_action(Game *g) {
    combat_after_player_action(g);
}

static void combat_after_player_action(Game *g) {
    if (g->state == GS_DEAD || g->state == GS_WIN || g->state == GS_LEVEL_UP) return;

    Mob *m = combat_target(g);
    if (m && mob_is_adjacent(g, m) && g->state == GS_COMBAT) {
        combat_mob_attack_player(g, m);
    }

    combat_end_turn(g);

    if (g->state == GS_DEAD || g->state == GS_WIN) return;

    combat_refresh_adjacent(g);
    if (g->combat.n_adjacent == 0) {
        combat_leave(g);
        return;
    }

    if (g->combat.target_idx < 0 || !g->mobs[g->combat.target_idx].alive ||
        !mob_is_adjacent(g, &g->mobs[g->combat.target_idx])) {
        if (g->combat.n_adjacent == 1) {
            g->combat.target_idx = g->combat.adjacent[0];
            g->combat.phase = CP_PLAYER;
            combat_face_target(g, &g->mobs[g->combat.target_idx]);
        } else {
            g->combat.target_idx = -1;
            g->combat.phase = CP_TARGET;
            msg_push(&g->log, "Pick a target (1-%d).", g->combat.n_adjacent);
        }
    } else {
        g->combat.phase = CP_PLAYER;
    }
}

int combat_input(Game *g, int c) {
    if (g->state != GS_COMBAT) return 0;

    CombatState *cmb = &g->combat;

    if (cmb->phase == CP_TARGET) {
        if (c >= '1' && c <= '9') {
            if (combat_select_by_slot(g, c - '1'))
                return 1;
        }
        if (c == 't' || c == 'T') return 1;
        return 0;
    }

    if (cmb->phase != CP_PLAYER) return 0;

    /* Player stunned: skip action, enemy still acts. */
    if (g->player.stun_turns > 0) {
        g->player.stun_turns--;
        msg_push(&g->log, "You are stunned and lose your turn!");
        combat_after_player_action(g);
        return 1;
    }

    int action = CA_NONE;
    if (c == '1') action = CA_ATTACK;
    else if (c == '2') action = CA_DEFEND;
    else if (c == '3') { g->state = GS_COMBAT_ITEM; return 1; }
    else if (c == '4') action = CA_FLEE;
    else if (c == 't' || c == 'T') {
        combat_refresh_adjacent(g);
        if (g->combat.n_adjacent > 1) {
            g->combat.target_idx = -1;
            g->combat.phase = CP_TARGET;
            msg_push(&g->log, "Retarget (1-%d).", g->combat.n_adjacent);
        }
        return 1;
    } else return 0;

    cmb->hud_sel = action - 1;
    Mob *m = combat_target(g);
    if (!m || !mob_is_adjacent(g, m)) {
        combat_refresh_adjacent(g);
        if (g->combat.n_adjacent == 0) { combat_leave(g); return 1; }
        g->combat.phase = CP_TARGET;
        return 1;
    }

    switch (action) {
        case CA_ATTACK:
            if (combat_skill_count(g) > 1) {
                g->state = GS_COMBAT_ATTACK;
                return 1;
            }
            combat_attack_turn(g, SK_STRIKE);
            return 1;
        case CA_DEFEND:
            g->player.defending = 1;
            msg_push(&g->log, "You brace for impact.");
            break;
        case CA_FLEE:
            if (combat_try_flee(g, m)) return 1;
            break;
        default: break;
    }

    combat_after_player_action(g);
    return 1;
}

/* ---- Combat HUD ---------------------------------------------------------*/
static void blit_icon(Tigr *dst, Tigr *src, int dx, int dy) {
    if (!src) return;
    for (int y = 0; y < src->h; ++y) {
        int sy = dy + y;
        if ((unsigned)sy >= (unsigned)dst->h) continue;
        for (int x = 0; x < src->w; ++x) {
            int sx = dx + x;
            if ((unsigned)sx >= (unsigned)dst->w) continue;
            TPixel p = src->pix[y * src->w + x];
            if (p.a < 128) continue;
            dst->pix[sy * dst->w + sx] = p;
        }
    }
}

static void draw_hp_bar(Tigr *s, int x, int y, int w, int h,
                        int hp, int hp_max, TPixel fill) {
    TPixel bg = { 30, 30, 40, 255 };
    tigrFillRect(s, x, y, w, h, bg);
    if (hp_max < 1) hp_max = 1;
    int fw = (hp * w) / hp_max;
    if (fw < 0) fw = 0;
    if (fw > w) fw = w;
    if (fw > 0) tigrFillRect(s, x, y, fw, h, fill);
    tigrRect(s, x, y, w, h, tigrRGB(80, 80, 100));
}

void combat_render_hud(Game *g, Tigr *screen, int x0, int y0, int w, int h) {
    hud_init();
    TPixel panel = { 12, 12, 22, 255 };
    tigrFillRect(screen, x0, y0, w, h, panel);

    Mob *m = combat_target(g);
    const MobType *mt = m ? &MOB_TYPES[m->kind] : NULL;
    int mob_max = mt ? mt->hp : 1;

    /* Enemy row */
    int row1 = y0 + 4;
    if (m && m->kind < ASSET_MOB_N)
        blit_icon(screen, render3d_mob_sprite(m->kind), x0 + 6, row1);

    char buf[96];
    if (mt) {
        snprintf(buf, sizeof buf, "%s", mt->name);
        if (m->stun_turns > 0) strncat(buf, " [STUNNED]", sizeof buf - strlen(buf) - 1);
    } else {
        snprintf(buf, sizeof buf, "(no target)");
    }
    tigrPrint(screen, tfont, x0 + 44, row1 + 2, tigrRGB(220, 180, 180), "%s", buf);
    draw_hp_bar(screen, x0 + 44, row1 + 14, 180, 8,
                m ? m->hp : 0, mob_max, tigrRGB(200, 60, 60));

    /* Player row */
    int row2 = y0 + 28;
    snprintf(buf, sizeof buf, "YOU  ATK %d  DEF %d  Lv%d",
             player_total_atk(g), player_total_def(g), g->player.level);
    if (g->player.defending) strncat(buf, " [DEFENDING]", sizeof buf - strlen(buf) - 1);
    if (g->player.stun_turns > 0) strncat(buf, " [STUNNED]", sizeof buf - strlen(buf) - 1);
    tigrPrint(screen, tfont, x0 + 44, row2, tigrRGB(200, 220, 255), "%s", buf);
    {
        char sk[64];
        sk[0] = '\0';
        int n = combat_skill_count(g);
        for (int i = 0; i < n; ++i) {
            int sid = combat_skill_at(g, i);
            if (sid < 0) continue;
            if (sk[0]) strncat(sk, ", ", sizeof sk - strlen(sk) - 1);
            strncat(sk, combat_skill_name(sid), sizeof sk - strlen(sk) - 1);
        }
        if (sk[0])
            tigrPrint(screen, tfont, x0 + 280, row2, tigrRGB(140, 180, 220),
                      "Skills: %s", sk);
    }
    draw_hp_bar(screen, x0 + 44, row2 + 12, 180, 8,
                g->player.hp, g->player.hp_max, tigrRGB(60, 200, 80));

    /* Combat log (2 lines) */
    int log_y = y0 + 52;
    for (int i = 0; i < 2; ++i) {
        TPixel col = (i == 0) ? tigrRGB(220, 220, 220) : tigrRGB(140, 140, 140);
        tigrPrint(screen, tfont, x0 + 6, log_y + i * 11, col, "%s", msg_at(&g->log, i));
    }

    /* Action buttons */
    static const char *labels[4] = { "1 Attack", "2 Defend", "3 Item", "4 Flee" };
    int btn_y = y0 + 78;
    for (int i = 0; i < 4; ++i) {
        int bx = x0 + 6 + i * 156;
        TPixel hi = tigrRGB(120, 200, 255);
        TPixel dim = tigrRGB(60, 60, 80);
        if (g->combat.hud_sel == i)
            tigrRect(screen, bx - 2, btn_y - 2, 36, 36, hi);
        else
            tigrRect(screen, bx - 2, btn_y - 2, 36, 36, dim);
        blit_icon(screen, g_hud[i], bx, btn_y);
        tigrPrint(screen, tfont, bx + 36, btn_y + 10, tigrRGB(200, 200, 200),
                  "%s", labels[i]);
    }

    if (g->combat.phase == CP_TARGET) {
        snprintf(buf, sizeof buf, "TARGET: press 1-%d", g->combat.n_adjacent);
        tigrPrint(screen, tfont, x0 + 6, y0 + h - 12, tigrRGB(255, 200, 100),
                  "%s", buf);
        for (int i = 0; i < g->combat.n_adjacent; ++i) {
            Mob *am = &g->mobs[g->combat.adjacent[i]];
            snprintf(buf, sizeof buf, "%d:%s", i + 1, MOB_TYPES[am->kind].name);
            tigrPrint(screen, tfont, x0 + 120 + i * 100, y0 + h - 12,
                      tigrRGB(255, 220, 160), "%s", buf);
        }
    } else {
        tigrPrint(screen, tfont, x0 + 400, btn_y + 10, tigrRGB(120, 120, 140),
                  "T=retarget");
    }
}
