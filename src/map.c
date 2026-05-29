#include "game.h"
#include <stdlib.h>

/* ============================================================================
 *  Dungeon generation - recursive BSP with rooms and L-corridors.
 * ==========================================================================*/

typedef struct { int x, y, w, h; } Rect;

#define MIN_ROOM    4
#define MAX_ROOM_W  14
#define MAX_ROOM_H   8
#define MIN_LEAF    8
#define MAX_LEAVES  32

static Rect g_rooms[MAX_LEAVES];
static int  g_nrooms;

static void carve_h(Map *m, int x0, int x1, int y) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; ++x) {
        if (m->tile[y][x] == T_WALL || m->tile[y][x] == T_VOID)
            m->tile[y][x] = T_FLOOR;
    }
}
static void carve_v(Map *m, int y0, int y1, int x) {
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; ++y) {
        if (m->tile[y][x] == T_WALL || m->tile[y][x] == T_VOID)
            m->tile[y][x] = T_FLOOR;
    }
}

static void carve_room(Map *m, Rect r) {
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x)
            m->tile[y][x] = T_FLOOR;
}

static void connect_centers(Map *m, RNG *rng, Rect a, Rect b) {
    int ax = a.x + a.w / 2, ay = a.y + a.h / 2;
    int bx = b.x + b.w / 2, by = b.y + b.h / 2;
    if (rng_chance(rng, 50)) {
        carve_h(m, ax, bx, ay);
        carve_v(m, ay, by, bx);
    } else {
        carve_v(m, ay, by, ax);
        carve_h(m, ax, bx, by);
    }
}

/* BSP: split bounds into two halves until small enough, then place a room. */
static void bsp_split(Map *m, RNG *rng, Rect b, int depth) {
    if (g_nrooms >= MAX_LEAVES) return;
    int can_h = b.h > MIN_LEAF * 2;
    int can_w = b.w > MIN_LEAF * 2;
    int split_h;

    if (can_h && can_w) split_h = rng_chance(rng, 50);
    else if (can_h)     split_h = 1;
    else if (can_w)     split_h = 0;
    else                split_h = -1;

    if (split_h < 0 || depth >= 5) {
        /* Leaf -> place a room within bounds. */
        int w = rng_range(rng, MIN_ROOM, b.w - 2 < MAX_ROOM_W ? b.w - 2 : MAX_ROOM_W);
        int h = rng_range(rng, MIN_ROOM, b.h - 2 < MAX_ROOM_H ? b.h - 2 : MAX_ROOM_H);
        if (w < MIN_ROOM) w = MIN_ROOM;
        if (h < MIN_ROOM) h = MIN_ROOM;
        if (w > b.w - 2) w = b.w - 2;
        if (h > b.h - 2) h = b.h - 2;
        int rx = b.x + 1 + rng_range(rng, 0, b.w - 2 - w);
        int ry = b.y + 1 + rng_range(rng, 0, b.h - 2 - h);
        Rect r = { rx, ry, w, h };
        carve_room(m, r);
        g_rooms[g_nrooms++] = r;
        return;
    }

    if (split_h) {
        int sy = b.y + rng_range(rng, MIN_LEAF, b.h - MIN_LEAF);
        Rect top    = { b.x, b.y, b.w, sy - b.y };
        Rect bottom = { b.x, sy, b.w, b.h - (sy - b.y) };
        bsp_split(m, rng, top, depth + 1);
        bsp_split(m, rng, bottom, depth + 1);
    } else {
        int sx = b.x + rng_range(rng, MIN_LEAF, b.w - MIN_LEAF);
        Rect left  = { b.x, b.y, sx - b.x, b.h };
        Rect right = { sx, b.y, b.w - (sx - b.x), b.h };
        bsp_split(m, rng, left, depth + 1);
        bsp_split(m, rng, right, depth + 1);
    }
}

void map_generate(Map *m, RNG *rng, int depth) {
    (void)depth;
    memset(m, 0, sizeof(*m));
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            m->tile[y][x] = T_WALL;

    g_nrooms = 0;
    Rect bounds = { 1, 1, MAP_W - 2, MAP_H - 2 };
    bsp_split(m, rng, bounds, 0);

    /* Connect rooms in the order they were generated (cheap MST-ish). */
    for (int i = 1; i < g_nrooms; ++i) {
        connect_centers(m, rng, g_rooms[i - 1], g_rooms[i]);
    }

    /* Place up-stair in first room, down-stair in last room. */
    if (g_nrooms > 0) {
        Rect a = g_rooms[0];
        Rect b = g_rooms[g_nrooms - 1];
        m->up_stair   = (V2){ a.x + a.w / 2, a.y + a.h / 2 };
        m->down_stair = (V2){ b.x + b.w / 2, b.y + b.h / 2 };
        m->tile[m->up_stair.y][m->up_stair.x]     = T_STAIRS_UP;
        m->tile[m->down_stair.y][m->down_stair.x] = T_STAIRS_DOWN;
    }
}

/* ============================================================================
 *  Field of view - symmetric raycasting.
 *  For every cell in a circular range from the origin, draw a Bresenham
 *  line and mark cells along it visible until a wall is hit.
 * ==========================================================================*/
static void cast_ray(Map *m, int x0, int y0, int x1, int y1) {
    int dx = x1 - x0, dy = y1 - y0;
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;
    int err = (ax > ay ? ax : -ay) / 2;
    int x = x0, y = y0;
    for (;;) {
        if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return;
        m->visible[y][x] = 1;
        m->known[y][x]   = 1;
        if (x == x1 && y == y1) return;
        if (!(x == x0 && y == y0) && map_blocks_sight(m, x, y)) return;
        int e2 = err;
        if (e2 > -ax) { err -= ay; x += sx; }
        if (e2 <  ay) { err += ax; y += sy; }
    }
}

/* Directional FOV: wider in the facing direction, narrower behind.
 *  - Forward half-plane (dot > 0):  full radius
 *  - Perpendicular     (dot == 0):  full radius
 *  - Behind            (dot <  0):  half radius
 * If facing is (0,0) (player has not moved yet) we fall back to a full circle.
 */
void map_compute_fov(Map *m, int cx, int cy, int radius, V2 facing) {
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            m->visible[y][x] = 0;

    int r_fwd  = radius;
    int r_back = radius / 2;
    if (r_back < 3) r_back = 3;
    int r2_fwd  = r_fwd  * r_fwd;
    int r2_back = r_back * r_back;
    int has_facing = (facing.x | facing.y) != 0;

    int x0 = cx - radius, x1 = cx + radius;
    int y0 = cy - radius, y1 = cy + radius;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= MAP_W) x1 = MAP_W - 1;
    if (y1 >= MAP_H) y1 = MAP_H - 1;

    /* Brute force: cast a ray to every candidate cell in the bounding box,
     * filtered by the directional ellipse. ~360 rays at radius 9. */
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            int dx = x - cx, dy = y - cy;
            int dist2 = dx * dx + dy * dy;
            int max_r2 = r2_fwd;
            if (has_facing) {
                int dot = dx * facing.x + dy * facing.y;
                if (dot < 0) max_r2 = r2_back;
            }
            if (dist2 > max_r2) continue;
            cast_ray(m, cx, cy, x, y);
        }
    }

    /* Always see the cell we stand on. */
    m->visible[cy][cx] = 1;
    m->known[cy][cx]   = 1;
}
