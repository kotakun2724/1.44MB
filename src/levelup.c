#include "game.h"

typedef struct {
    const char *name;
    const char *desc;
    int category; /* 0=stat, 1=skill */
} UpgradeDef;

static const UpgradeDef UPGRADES[UP_N] = {
    [UP_HP]             = { "Reinforce RAM",    "+8 HP max, heal 8",           0 },
    [UP_ATK]            = { "Overclock CPU",    "+2 ATK",                      0 },
    [UP_DEF]            = { "Harden Sectors",   "+1 DEF",                      0 },
    [UP_CRIT]           = { "Precision Patch",  "+8% crit chance",             0 },
    [UP_DRAIN]          = { "Data Drain",       "Heal 3 HP on kill",           0 },
    [UP_ESCAPE]         = { "Fast Boot",        "+10% flee chance",            0 },
    [UP_SKILL_POWER]    = { "Overclock",        "Unlock power strike skill",   1 },
    [UP_SKILL_PING]     = { "Quick Ping",       "Unlock fast crit skill",      1 },
    [UP_SKILL_OVERFLOW] = { "Buffer Overflow",  "Unlock armor-pierce skill",   1 },
    [UP_SKILL_SWEEP]    = { "Defrag Sweep",     "Unlock area attack skill",    1 },
};

const char *levelup_choice_name(int up_id) {
    if (up_id < 0 || up_id >= UP_N) return "?";
    return UPGRADES[up_id].name;
}

const char *levelup_choice_desc(int up_id) {
    if (up_id < 0 || up_id >= UP_N) return "";
    return UPGRADES[up_id].desc;
}

static int upgrade_available(const Game *g, int up_id) {
    if (up_id < 0 || up_id >= UP_N) return 0;
    switch (up_id) {
        case UP_SKILL_POWER:    return !(g->player.skills.unlocked & (1 << SK_POWER));
        case UP_SKILL_PING:     return !(g->player.skills.unlocked & (1 << SK_PING));
        case UP_SKILL_OVERFLOW: return !(g->player.skills.unlocked & (1 << SK_OVERFLOW));
        case UP_SKILL_SWEEP:    return !(g->player.skills.unlocked & (1 << SK_SWEEP));
        default: return 1;
    }
}

static void levelup_apply(Game *g, int up_id) {
    switch (up_id) {
        case UP_HP:
            g->player.hp_max += 8;
            g->player.hp += 8;
            if (g->player.hp > g->player.hp_max) g->player.hp = g->player.hp_max;
            break;
        case UP_ATK:
            g->player.atk += 2;
            break;
        case UP_DEF:
            g->player.def += 1;
            break;
        case UP_CRIT:
            g->player.skills.crit_bonus += 8;
            if (g->player.skills.crit_bonus > 50) g->player.skills.crit_bonus = 50;
            break;
        case UP_DRAIN:
            g->player.skills.kill_heal += 3;
            break;
        case UP_ESCAPE:
            g->player.skills.flee_bonus += 10;
            break;
        case UP_SKILL_POWER:
            g->player.skills.unlocked |= (1 << SK_POWER);
            break;
        case UP_SKILL_PING:
            g->player.skills.unlocked |= (1 << SK_PING);
            break;
        case UP_SKILL_OVERFLOW:
            g->player.skills.unlocked |= (1 << SK_OVERFLOW);
            break;
        case UP_SKILL_SWEEP:
            g->player.skills.unlocked |= (1 << SK_SWEEP);
            break;
        default: break;
    }
    msg_push(&g->log, ">> %s: %s", UPGRADES[up_id].name, UPGRADES[up_id].desc);
}

static void levelup_roll_choices(Game *g) {
    LevelUpState *lu = &g->levelup;
    int pool[UP_N];
    int n_pool = 0;
    for (int i = 0; i < UP_N; ++i) {
        if (upgrade_available(g, i))
            pool[n_pool++] = i;
    }

    lu->n_choices = 0;
    if (n_pool == 0) return;

    /* Try to offer at least one skill if any remain. */
    int want_skill = 0;
    for (int i = 0; i < n_pool; ++i) {
        if (UPGRADES[pool[i]].category == 1) { want_skill = 1; break; }
    }

    int picked_skill = 0;
    int tries = 0;
    while (lu->n_choices < 3 && tries < 64) {
        tries++;
        int idx = rng_range(&g->rng, 0, n_pool - 1);
        int up = pool[idx];
        int dup = 0;
        for (int j = 0; j < lu->n_choices; ++j)
            if (lu->choices[j] == up) { dup = 1; break; }
        if (dup) continue;

        if (want_skill && !picked_skill && lu->n_choices == 2 &&
            UPGRADES[up].category != 1)
            continue;

        lu->choices[lu->n_choices++] = up;
        if (UPGRADES[up].category == 1) picked_skill = 1;
    }

    /* Fill remaining slots if pool is small. */
    for (int i = 0; lu->n_choices < 3 && i < n_pool; ++i) {
        int up = pool[i];
        int dup = 0;
        for (int j = 0; j < lu->n_choices; ++j)
            if (lu->choices[j] == up) { dup = 1; break; }
        if (!dup) lu->choices[lu->n_choices++] = up;
    }
}

void levelup_begin(Game *g) {
    levelup_roll_choices(g);
    g->state = GS_LEVEL_UP;
    msg_push(&g->log, "** Recompiled to Lv %d! Pick upgrade (1-3). **",
             g->player.level);
}

static void levelup_finish(Game *g) {
    LevelUpState *lu = &g->levelup;
    if (lu->pending > 0) {
        levelup_begin(g);
        return;
    }
    GameState resume = lu->return_state;
    g->state = resume;
    if (resume == GS_COMBAT || resume == GS_COMBAT_ATTACK)
        combat_resume_after_action(g);
    if (lu->boss_win) {
        lu->boss_win = 0;
        g->state = GS_WIN;
        msg_push(&g->log, "** The disk is whole. You win. **");
    }
}

int levelup_input(Game *g, int c) {
    if (g->state != GS_LEVEL_UP) return 0;
    if (c < '1' || c > '3') return 0;
    int idx = c - '1';
    if (idx >= g->levelup.n_choices) return 0;

    levelup_apply(g, g->levelup.choices[idx]);
    g->levelup.pending--;
    levelup_finish(g);
    return 1;
}

void levelup_render(Game *g, Tigr *screen) {
    tigrFillRect(screen, 80, 60, 480, 160, tigrRGBA(8, 8, 20, 230));
    tigrRect(screen, 80, 60, 480, 160, tigrRGB(100, 180, 255));

    char buf[96];
    snprintf(buf, sizeof buf, "== LEVEL UP: Lv %d ==", g->player.level);
    tigrPrint(screen, tfont, 100, 72, tigrRGB(120, 200, 255), "%s", buf);
    tigrPrint(screen, tfont, 100, 88, tigrRGB(160, 160, 180),
              "Choose one upgrade (press 1, 2, or 3):");

    for (int i = 0; i < g->levelup.n_choices; ++i) {
        int y = 108 + i * 28;
        int up = g->levelup.choices[i];
        snprintf(buf, sizeof buf, "%d) %s", i + 1, levelup_choice_name(up));
        tigrPrint(screen, tfont, 110, y, tigrRGB(220, 220, 240), "%s", buf);
        tigrPrint(screen, tfont, 150, y + 12, tigrRGB(140, 160, 180),
                  "%s", levelup_choice_desc(up));
    }
    if (g->levelup.pending > 1) {
        snprintf(buf, sizeof buf, "(%d more level-ups pending)", g->levelup.pending);
        tigrPrint(screen, tfont, 100, 200, tigrRGB(255, 200, 100), "%s", buf);
    }
}

void levelup_queue(Game *g) {
    if (g->levelup.pending <= 0) return;
    if (g->state == GS_LEVEL_UP) return;
    g->levelup.return_state = g->state;
    levelup_begin(g);
}
