/*
 *  1.44MB: Sectors of the Lost Disk
 *  ---------------------------------
 *  Shared game state, types, and constants.
 */
#ifndef GAME_H
#define GAME_H

#include "tigr.h"
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* ---- Screen layout (8x8 cells in a 640x400 window) -----------------------*/
#define CELL_W      8
#define CELL_H      8
#define SCREEN_W    640
#define SCREEN_H    400
#define COLS        (SCREEN_W / CELL_W)   /* 80 */
#define ROWS        (SCREEN_H / CELL_H)   /* 50 */

#define LOG_TOP     0
#define LOG_ROWS    3
#define MAP_TOP     (LOG_TOP + LOG_ROWS + 1)   /* 4 */
#define MAP_W       80
#define MAP_H       40
#define MAP_BOTTOM  (MAP_TOP + MAP_H)          /* 44 */
#define STATUS_TOP  (MAP_BOTTOM + 1)           /* 45 */
#define STATUS_ROWS (ROWS - STATUS_TOP)        /* 5 */

/* ---- Tile kinds ----------------------------------------------------------*/
typedef enum {
    T_VOID = 0,
    T_WALL,
    T_FLOOR,
    T_DOOR,
    T_STAIRS_DOWN,
    T_STAIRS_UP
} TileKind;

/* ---- Vector / position --------------------------------------------------*/
typedef struct { int x, y; } V2;

/* ---- Random number generator --------------------------------------------*/
typedef struct { uint32_t s; } RNG;

void  rng_seed(RNG *r, uint32_t seed);
uint32_t rng_next(RNG *r);
int   rng_range(RNG *r, int lo, int hi);            /* [lo, hi] inclusive */
int   rng_chance(RNG *r, int percent);              /* 1 in 100 percent  */

/* ---- Map ----------------------------------------------------------------*/
typedef struct {
    uint8_t tile[MAP_H][MAP_W];
    uint8_t visible[MAP_H][MAP_W];
    uint8_t known[MAP_H][MAP_W];
    V2 up_stair;
    V2 down_stair;
} Map;

/* ---- Player -------------------------------------------------------------*/
typedef struct {
    V2 pos;
    V2 facing;      /* {-1..1, -1..1}, last move direction; {0,-1} initially */
    int hp, hp_max;
    int atk, def;
    int xp, level;
    int gold;
    int depth;
    int turn;
    int hunger;     /* 0=full; ticks up over time */
} Player;

#define HUNGER_HUNGRY    500
#define HUNGER_STARVING  750
#define HUNGER_FATAL    1000
#define FINAL_DEPTH      15

/* ---- Mobs --------------------------------------------------------------*/
#define AI_CHASE     0x01
#define AI_TELEPORT  0x02
#define AI_ERRATIC   0x04
#define AI_TOUGH     0x08

typedef struct {
    char        glyph;
    TPixel      color;
    const char *name;
    int hp;
    int atk;
    int def;
    int xp;
    int ai;
    int min_depth;
    int max_depth;   /* 0 = unlimited */
    int boss;        /* 1 = only spawns on boss floors */
} MobType;

extern const MobType MOB_TYPES[];
extern const int     MOB_TYPES_N;

typedef struct {
    V2  pos;
    int kind;            /* index into MOB_TYPES */
    int hp;
    int last_seen_turn;  /* 0 = never seen player */
    V2  last_seen_pos;
    int alive;
} Mob;

#define MAX_MOBS 24

/* ---- Items --------------------------------------------------------------*/
typedef enum {
    IT_NONE = 0,
    IT_WEAPON,
    IT_ARMOR,
    IT_POTION,
    IT_SCROLL,
    IT_FOOD
} ItemKind;

typedef enum {
    EFF_NONE = 0,
    /* potions */
    EFF_HEAL_S, EFF_HEAL_L, EFF_STR, EFF_GUARD, EFF_VIGOR, EFF_CORRUPT,
    /* scrolls */
    EFF_IDENTIFY, EFF_MAPPING, EFF_TELEPORT, EFF_LIGHTNING
} ItemEffect;

typedef struct {
    const char *true_name;
    const char *desc;       /* short effect description, e.g. "+8 HP" */
    char        glyph;
    TPixel      color;
    int         kind;       /* ItemKind */
    int         power;      /* atk/def bonus or effect potency */
    int         effect;     /* ItemEffect */
    int         min_depth;
} ItemDef;

extern const ItemDef ITEMS[];
extern const int     ITEMS_N;

/* Floor item placed during dungeon generation. */
typedef struct {
    V2  pos;
    int def;        /* index into ITEMS */
    int active;     /* 1 = on floor, 0 = picked up */
} FloorItem;

#define MAX_FLOOR_ITEMS 32
#define INV_SIZE        16

typedef struct {
    int def;        /* -1 = empty */
} InvSlot;

/* Per-run knowledge of pseudo-named items. */
#define MAX_POTIONS 6
#define MAX_SCROLLS 4
typedef struct {
    int potion_alias[MAX_POTIONS];     /* potion-index -> alias-index */
    int scroll_alias[MAX_SCROLLS];     /* scroll-index -> alias-index */
    uint8_t identified[40];            /* per-ITEMS index */
} ItemKnowledge;

extern const char *POTION_ALIASES[MAX_POTIONS];
extern const char *SCROLL_ALIASES[MAX_SCROLLS];

/* ---- Message log --------------------------------------------------------*/
#define MSG_BUF      8
#define MSG_LEN      96
typedef struct {
    char  text[MSG_BUF][MSG_LEN];
    int   head;        /* index of newest message slot */
    int   count;       /* how many slots are filled (up to MSG_BUF) */
} MsgLog;

void msg_init(MsgLog *log);
void msg_push(MsgLog *log, const char *fmt, ...);
const char *msg_at(const MsgLog *log, int n);       /* 0 = newest */

/* ---- Game state ---------------------------------------------------------*/
typedef enum {
    GS_TITLE,
    GS_PLAYING,
    GS_INVENTORY,
    GS_HELP,
    GS_DEAD,
    GS_WIN,
    GS_HISCORE,
    GS_QUIT
} GameState;

/* ---- High scores --------------------------------------------------------*/
#define MAX_SCORES 8
#define SCORE_MAGIC 0x14400DDEu

typedef struct {
    uint32_t score;
    uint16_t depth;
    uint16_t level;
    uint32_t turns;
    uint8_t  status;     /* 0=quit, 1=died, 2=won */
    uint8_t  pad[3];
} ScoreEntry;

typedef struct {
    uint32_t   magic;
    uint32_t   count;
    ScoreEntry entries[MAX_SCORES];
} ScoreList;

void score_load(ScoreList *list);
void score_save(const ScoreList *list);
int  score_insert(ScoreList *list, const ScoreEntry *e);  /* index or -1 */
uint32_t score_compute(const Player *p);

typedef struct {
    RNG       rng;
    Map       map;
    Player    player;
    Mob       mobs[MAX_MOBS];
    int       n_mobs;
    FloorItem floor[MAX_FLOOR_ITEMS];
    int       n_floor;
    InvSlot   inv[INV_SIZE];
    int       wielded;     /* inv slot, -1 = bare */
    int       worn;        /* inv slot, -1 = bare */
    ItemKnowledge know;
    MsgLog    log;
    GameState state;
    int       took_turn;   /* was player's action consuming a turn? */
    int       inv_select;  /* selected slot in inventory view */
    int       inv_drop_mode; /* 1 = next a-p will drop, not use */
    ScoreList hiscores;    /* loaded at app start */
    int       run_recorded;/* 1 once we've inserted the current run */
} Game;

/* ---- Game flow ----------------------------------------------------------*/
void game_new(Game *g, uint32_t seed);
void game_step(Game *g, int dx, int dy, int action);  /* one turn          */
void game_render(Game *g, Tigr *screen);

/* ---- 3D renderer (see src/render3d.c) ----------------------------------*/
void render3d_init(void);
void render3d_draw(Game *g, Tigr *s, int x0, int y0, int vw, int vh);
void render3d_minimap(Game *g, Tigr *s, int x0, int y0, int cell);

/* Actions (non-movement). Movement uses dx/dy != 0.
 * ACT_TURN_L / ACT_TURN_R rotate the player's facing 90 degrees without
 * consuming a turn (mobs do not act). */
enum {
    ACT_NONE = 0,
    ACT_WAIT,
    ACT_DESCEND,
    ACT_TURN_L,
    ACT_TURN_R
};

/* ---- Map helpers --------------------------------------------------------*/
void map_generate(Map *m, RNG *rng, int depth);
void map_compute_fov(Map *m, int cx, int cy, int radius, V2 facing);
int  map_walkable(const Map *m, int x, int y);
int  map_blocks_sight(const Map *m, int x, int y);

#define FOV_RADIUS 9

/* Returns floor positions for spawning. Writes up to max positions. */
int  map_collect_floor(const Map *m, V2 *out, int max);

/* ---- Mobs --------------------------------------------------------------*/
void mob_spawn_floor(Game *g);
void mob_take_turns(Game *g);
Mob *mob_at(Game *g, int x, int y);

/* ---- Combat ------------------------------------------------------------*/
void combat_attack_mob(Game *g, Mob *m);     /* player attacks mob       */
void combat_mob_attacks(Game *g, Mob *m);    /* mob attacks player       */

/* ---- Items / inventory --------------------------------------------------*/
void item_init_knowledge(Game *g);
void item_spawn_floor(Game *g);
void item_pickup_under(Game *g);              /* auto-pickup on walk-over */
FloorItem *item_at(Game *g, int x, int y);
int  inv_first_empty(const Game *g);
void inv_use(Game *g, int slot);
void inv_drop(Game *g, int slot);
const char *item_display(const Game *g, int def_index, char *buf, int buf_n);
int  player_total_atk(const Game *g);
int  player_total_def(const Game *g);

#endif /* GAME_H */
