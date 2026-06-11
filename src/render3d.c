/*
 *  Column-based raycaster renderer (Wolfenstein-style 3D over the existing
 *  grid map). Walls, floor and ceiling are textured with embedded Dungeon
 *  Crawl Stone Soup tiles (CC0); mobs and items are drawn as depth-buffered,
 *  alpha-tested billboard sprites from the same tileset.
 *
 *  Drawn directly into the TIGR backbuffer via bmp->pix for speed.
 */
#include "game.h"
#include "assets.h"
#include <math.h>

#define TILE          32               /* DCSS tiles are 32x32 */
#define TILE_MASK      (TILE - 1)
#define FOV_PLANE     0.66f            /* tan(33 deg) -> ~66 deg HFOV */
#define MAX_LINE_MUL  8                /* clamp line_h to vh * MAX_LINE_MUL */
#define MAX_VIEW_W    640
#define MAX_WALL_TEX  16
#define CEIL_DIM      160              /* extra Q8 darken for the ceiling */

/* Decoded tiles (lazily built once on the first frame). */
static Tigr *g_wall[MAX_WALL_TEX];
static int   g_wall_n;
static Tigr *g_floor[4];
static int   g_floor_n;
static Tigr *g_ceil;
static Tigr *g_door;
static Tigr *g_mob[32];
static Tigr *g_item[64];
static int   g_init = 0;

static uint32_t hash_cell(int x, int y) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
    h ^= h >> 16; h *= 0x7feb352du;
    h ^= h >> 15; h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

void render3d_init(void) {
    if (g_init) return;
    g_init = 1;

    g_wall_n = ASSET_WALL_N < MAX_WALL_TEX ? ASSET_WALL_N : MAX_WALL_TEX;
    for (int i = 0; i < g_wall_n; ++i)
        g_wall[i] = tigrLoadImageMem(ASSET_WALL[i].png, ASSET_WALL[i].len);

    g_floor_n = ASSET_FLOOR_N < 4 ? ASSET_FLOOR_N : 4;
    for (int i = 0; i < g_floor_n; ++i)
        g_floor[i] = tigrLoadImageMem(ASSET_FLOOR[i].png, ASSET_FLOOR[i].len);

    g_ceil = tigrLoadImageMem(ASSET_CEIL.png, ASSET_CEIL.len);
    g_door = tigrLoadImageMem(ASSET_DOOR.png, ASSET_DOOR.len);

    for (int i = 0; i < MOB_TYPES_N && i < ASSET_MOB_N && i < 32; ++i)
        g_mob[i] = tigrLoadImageMem(ASSET_MOB[i].png, ASSET_MOB[i].len);
    for (int i = 0; i < ITEMS_N && i < ASSET_ITEM_N && i < 64; ++i)
        g_item[i] = tigrLoadImageMem(ASSET_ITEM[i].png, ASSET_ITEM[i].len);
}

Tigr *render3d_mob_sprite(int kind) {
    render3d_init();
    if (kind < 0 || kind >= 32) return NULL;
    return g_mob[kind];
}

/* Bounds-checked single-pixel write. */
static inline void put_px(Tigr *s, int x, int y, TPixel c) {
    if ((unsigned)x < (unsigned)s->w && (unsigned)y < (unsigned)s->h)
        s->pix[y * s->w + x] = c;
}

/* Diminished-lighting factor as Q8 fixed-point (0..256). */
static inline int light_mul(float d) {
    float f = 1.0f - d * 0.06f;
    if (f < 0.12f) f = 0.12f;
    return (int)(f * 256.0f);
}

/* Sample a 32x32 tile (wrapping) without lighting. */
static inline TPixel tile_at(Tigr *t, int tx, int ty) {
    return t->pix[(ty & TILE_MASK) * t->w + (tx & TILE_MASK)];
}

/* ---- Main draw ---------------------------------------------------------- */
void render3d_draw(Game *g, Tigr *s, int x0, int y0, int vw, int vh) {
    render3d_init();
    if (vw > MAX_VIEW_W) vw = MAX_VIEW_W;

    float px = g->player.pos.x + 0.5f;
    float py = g->player.pos.y + 0.5f;
    int fx = g->player.facing.x, fy = g->player.facing.y;
    if (fx == 0 && fy == 0) { fy = -1; }
    float dirx   = (float)fx;
    float diry   = (float)fy;
    float planex = -diry * FOV_PLANE;
    float planey =  dirx * FOV_PLANE;

    static float zbuf[MAX_VIEW_W];

    int half = vh / 2;
    float inv_vw = 1.0f / (float)vw;

    /* --- Floor + ceiling: perspective-correct affine row scan. --- */
    for (int y = 1; y < vh; ++y) {
        int p = y - half;
        if (p == 0) continue;
        int is_floor = (p > 0);
        int ap = p < 0 ? -p : p;
        float row_dist = 0.5f * (float)vh / (float)ap;
        if (row_dist > 64.0f) continue;

        float floor_x = px + row_dist * (dirx - planex);
        float floor_y = py + row_dist * (diry - planey);
        float step_x  = row_dist * 2.0f * planex * inv_vw;
        float step_y  = row_dist * 2.0f * planey * inv_vw;

        int lm = light_mul(row_dist);
        int ceil_lm = (lm * CEIL_DIM) >> 8;
        int use_lm = is_floor ? lm : ceil_lm;

        int sy = y0 + y;
        if ((unsigned)sy >= (unsigned)s->h) continue;
        TPixel *row = &s->pix[sy * s->w + x0];

        for (int x = 0; x < vw; ++x) {
            int cellx = (int)floorf(floor_x);
            int celly = (int)floorf(floor_y);
            int tx = (int)(TILE * (floor_x - cellx)) & TILE_MASK;
            int ty = (int)(TILE * (floor_y - celly)) & TILE_MASK;

            Tigr *t;
            if (is_floor) {
                int idx = g_floor_n > 1 ? ((cellx + celly) & 1) : 0;
                t = g_floor[idx];
            } else {
                t = g_ceil;
            }
            TPixel c = t->pix[ty * t->w + tx];
            row[x].r = (uint8_t)((c.r * use_lm) >> 8);
            row[x].g = (uint8_t)((c.g * use_lm) >> 8);
            row[x].b = (uint8_t)((c.b * use_lm) >> 8);
            row[x].a = 255;
            floor_x += step_x;
            floor_y += step_y;
        }
    }

    /* --- Walls: DDA grid traversal, per column. --- */
    for (int x = 0; x < vw; ++x) {
        float camera_x = 2.0f * (float)x * inv_vw - 1.0f;
        float rdx = dirx + planex * camera_x;
        float rdy = diry + planey * camera_x;

        int mapX = g->player.pos.x;
        int mapY = g->player.pos.y;

        float dxd = (rdx == 0.0f) ? 1e30f : fabsf(1.0f / rdx);
        float dyd = (rdy == 0.0f) ? 1e30f : fabsf(1.0f / rdy);

        int stepX, stepY;
        float sideX, sideY;
        if (rdx < 0) { stepX = -1; sideX = (px - mapX) * dxd; }
        else         { stepX =  1; sideX = (mapX + 1.0f - px) * dxd; }
        if (rdy < 0) { stepY = -1; sideY = (py - mapY) * dyd; }
        else         { stepY =  1; sideY = (mapY + 1.0f - py) * dyd; }

        int hit = 0, side = 0;
        int tile_kind = T_WALL;
        for (int i = 0; i < 96 && !hit; ++i) {
            if (sideX < sideY) { sideX += dxd; mapX += stepX; side = 0; }
            else               { sideY += dyd; mapY += stepY; side = 1; }
            if (mapX < 0 || mapY < 0 || mapX >= MAP_W || mapY >= MAP_H) {
                hit = 1; tile_kind = T_VOID; break;
            }
            if (map_blocks_sight(&g->map, mapX, mapY)) {
                hit = 1;
                tile_kind = g->map.tile[mapY][mapX];
            }
        }

        float perp = (side == 0) ? (sideX - dxd) : (sideY - dyd);
        if (perp < 0.05f) perp = 0.05f;
        zbuf[x] = perp;

        int line_h = (int)((float)vh / perp);
        int line_max = vh * MAX_LINE_MUL;
        if (line_h > line_max) line_h = line_max;
        int draw_start = (vh - line_h) / 2;
        int draw_end   = draw_start + line_h;
        int top = draw_start < 0 ? 0 : draw_start;
        int bot = draw_end   > vh ? vh : draw_end;

        Tigr *tex;
        if (tile_kind == T_DOOR) tex = g_door;
        else tex = g_wall[g_wall_n ? (hash_cell(mapX, mapY) % (uint32_t)g_wall_n) : 0];

        float wall_hit = (side == 0) ? (py + perp * rdy)
                                      : (px + perp * rdx);
        wall_hit -= floorf(wall_hit);
        int tx = (int)(wall_hit * (float)TILE);
        if (side == 0 && rdx > 0) tx = TILE - tx - 1;
        if (side == 1 && rdy < 0) tx = TILE - tx - 1;
        tx &= TILE_MASK;

        int lm = light_mul(perp);
        if (side == 1) lm = (lm * 180) >> 8;   /* Y-side darker (Wolf3D style) */

        int sx = x0 + x;
        if ((unsigned)sx >= (unsigned)s->w) continue;

        for (int yy = top; yy < bot; ++yy) {
            int d  = yy * 256 - vh * 128 + line_h * 128;
            int ty = ((d * TILE) / line_h) >> 8;
            ty &= TILE_MASK;
            TPixel c = tex->pix[ty * tex->w + tx];
            int sy = y0 + yy;
            if ((unsigned)sy >= (unsigned)s->h) continue;
            TPixel *out = &s->pix[sy * s->w + sx];
            out->r = (uint8_t)((c.r * lm) >> 8);
            out->g = (uint8_t)((c.g * lm) >> 8);
            out->b = (uint8_t)((c.b * lm) >> 8);
            out->a = 255;
        }
    }

    /* --- Sprites: mobs + items, depth-sorted, z-buffered billboards. --- */
    typedef struct { int idx, is_mob; float dist; } SR;
    static SR list[MAX_MOBS + MAX_FLOOR_ITEMS];
    int n = 0;
    for (int i = 0; i < g->n_mobs; ++i) {
        Mob *m = &g->mobs[i];
        if (!m->alive) continue;
        if (!g->map.visible[m->pos.y][m->pos.x]) continue;
        float dx = (m->pos.x + 0.5f) - px;
        float dy = (m->pos.y + 0.5f) - py;
        list[n].idx = i; list[n].is_mob = 1; list[n].dist = dx*dx + dy*dy;
        ++n;
    }
    for (int i = 0; i < g->n_floor; ++i) {
        FloorItem *f = &g->floor[i];
        if (!f->active) continue;
        if (!g->map.visible[f->pos.y][f->pos.x]) continue;
        float dx = (f->pos.x + 0.5f) - px;
        float dy = (f->pos.y + 0.5f) - py;
        list[n].idx = i; list[n].is_mob = 0; list[n].dist = dx*dx + dy*dy;
        ++n;
    }
    /* Insertion sort: back-to-front (largest dist first). */
    for (int i = 1; i < n; ++i) {
        SR k = list[i]; int j = i - 1;
        while (j >= 0 && list[j].dist < k.dist) { list[j+1] = list[j]; --j; }
        list[j+1] = k;
    }

    float det = planex * diry - dirx * planey;
    if (det == 0.0f) det = 1e-6f;
    float inv_det = 1.0f / det;

    for (int si = 0; si < n; ++si) {
        SR sr = list[si];
        float spr_x, spr_y;
        Tigr *spr;
        char glyph;
        TPixel base;
        if (sr.is_mob) {
            Mob *m = &g->mobs[sr.idx];
            spr_x = m->pos.x + 0.5f; spr_y = m->pos.y + 0.5f;
            spr   = (m->kind < MOB_TYPES_N) ? g_mob[m->kind] : NULL;
            glyph = MOB_TYPES[m->kind].glyph;
            base  = MOB_TYPES[m->kind].color;
        } else {
            FloorItem *f = &g->floor[sr.idx];
            spr_x = f->pos.x + 0.5f; spr_y = f->pos.y + 0.5f;
            spr   = (f->def < 64) ? g_item[f->def] : NULL;
            glyph = ITEMS[f->def].glyph;
            base  = ITEMS[f->def].color;
        }
        float dx = spr_x - px, dy = spr_y - py;
        float tx_c = inv_det * (diry * dx - dirx * dy);
        float ty_c = inv_det * (-planey * dx + planex * dy);
        if (ty_c <= 0.2f) continue;

        int screen_x = (int)((vw / 2) * (1.0f + tx_c / ty_c));
        int spr_dim = (int)((float)vh / ty_c);   /* square tile -> w == h */
        if (spr_dim < 6) spr_dim = 6;
        if (spr_dim > vh * MAX_LINE_MUL) spr_dim = vh * MAX_LINE_MUL;

        int lm = light_mul(ty_c);

        /* Vertical span: centered on the horizon like the walls. */
        int top = (vh - spr_dim) / 2;
        int left = screen_x - spr_dim / 2;
        int top_c = top < 0 ? 0 : top;
        int bot_c = (top + spr_dim) > vh ? vh : (top + spr_dim);

        if (spr) {
            int tw = spr->w, th = spr->h;
            for (int cx = left; cx < left + spr_dim; ++cx) {
                if (cx < 0 || cx >= vw) continue;
                if (ty_c >= zbuf[cx]) continue;
                int u = (cx - left) * tw / spr_dim;
                if (u < 0) u = 0; else if (u >= tw) u = tw - 1;
                for (int cy = top_c; cy < bot_c; ++cy) {
                    int v = (cy - top) * th / spr_dim;
                    if (v < 0) v = 0; else if (v >= th) v = th - 1;
                    TPixel c = spr->pix[v * spr->w + u];
                    if (c.a < 128) continue;
                    TPixel pc = {
                        (uint8_t)((c.r * lm) >> 8),
                        (uint8_t)((c.g * lm) >> 8),
                        (uint8_t)((c.b * lm) >> 8), 255
                    };
                    put_px(s, x0 + cx, y0 + cy, pc);
                }
            }
        } else {
            /* Fallback: tinted rectangle + glyph (sprite failed to load). */
            TPixel tinted = {
                (uint8_t)((base.r * lm) >> 8),
                (uint8_t)((base.g * lm) >> 8),
                (uint8_t)((base.b * lm) >> 8), 255
            };
            for (int cx = left; cx < left + spr_dim; ++cx) {
                if (cx < 0 || cx >= vw) continue;
                if (ty_c >= zbuf[cx]) continue;
                for (int cy = top_c; cy < bot_c; ++cy)
                    put_px(s, x0 + cx, y0 + cy, tinted);
            }
            if (spr_dim >= 14 && screen_x >= 0 && screen_x < vw) {
                char buf[2] = { glyph, 0 };
                TPixel gc = { 240, 240, 240, 255 };
                tigrPrint(s, tfont, x0 + screen_x - 4, y0 + vh / 2 - 4,
                          gc, "%s", buf);
            }
        }
    }
}

/* ---- Minimap ------------------------------------------------------------ */
/* Draws the known map (visible cells brighter) at `cell` pixels per tile. */
void render3d_minimap(Game *g, Tigr *s, int x0, int y0, int cell) {
    TPixel bg     = {  8,  8, 16, 255 };
    TPixel wall_v = { 90, 80, 60, 255 };
    TPixel wall_k = { 40, 36, 28, 255 };
    TPixel floor_v= { 60, 60, 70, 255 };
    TPixel floor_k= { 20, 20, 26, 255 };
    TPixel stair  = { 80,255, 80, 255 };
    TPixel door   = {200,140, 60, 255 };
    TPixel mobc   = {220, 80, 80, 255 };
    TPixel itemc  = {120,200,255, 255 };
    TPixel pcol   = {255,255,100, 255 };

    int mw = MAP_W * cell;
    int mh = MAP_H * cell;

    for (int y = 0; y < mh; ++y) {
        int sy = y0 + y;
        if ((unsigned)sy >= (unsigned)s->h) continue;
        TPixel *row = &s->pix[sy * s->w + x0];
        for (int x = 0; x < mw; ++x) row[x] = bg;
    }

    for (int my = 0; my < MAP_H; ++my) {
        for (int mx = 0; mx < MAP_W; ++mx) {
            int vis = g->map.visible[my][mx];
            int kno = g->map.known[my][mx];
            if (!vis && !kno) continue;
            uint8_t t = g->map.tile[my][mx];
            TPixel c;
            switch (t) {
                case T_WALL:        c = vis ? wall_v  : wall_k;  break;
                case T_FLOOR:       c = vis ? floor_v : floor_k; break;
                case T_DOOR:        c = vis ? door    : wall_k;  break;
                case T_STAIRS_DOWN:
                case T_STAIRS_UP:   c = vis ? stair   : floor_k; break;
                default: continue;
            }
            for (int yy = 0; yy < cell; ++yy)
                for (int xx = 0; xx < cell; ++xx)
                    put_px(s, x0 + mx * cell + xx, y0 + my * cell + yy, c);
        }
    }
    for (int i = 0; i < g->n_floor; ++i) {
        FloorItem *f = &g->floor[i];
        if (!f->active) continue;
        if (!g->map.visible[f->pos.y][f->pos.x]) continue;
        for (int yy = 0; yy < cell; ++yy)
            for (int xx = 0; xx < cell; ++xx)
                put_px(s, x0 + f->pos.x * cell + xx,
                          y0 + f->pos.y * cell + yy, itemc);
    }
    for (int i = 0; i < g->n_mobs; ++i) {
        Mob *m = &g->mobs[i];
        if (!m->alive) continue;
        if (!g->map.visible[m->pos.y][m->pos.x]) continue;
        for (int yy = 0; yy < cell; ++yy)
            for (int xx = 0; xx < cell; ++xx)
                put_px(s, x0 + m->pos.x * cell + xx,
                          y0 + m->pos.y * cell + yy, mobc);
    }
    /* Player block + a single pixel "facing" pip one cell ahead. */
    for (int yy = 0; yy < cell; ++yy)
        for (int xx = 0; xx < cell; ++xx)
            put_px(s, x0 + g->player.pos.x * cell + xx,
                      y0 + g->player.pos.y * cell + yy, pcol);
    int fx2 = g->player.facing.x, fy2 = g->player.facing.y;
    if (fx2 == 0 && fy2 == 0) fy2 = -1;
    put_px(s, x0 + g->player.pos.x * cell + cell / 2 + fx2 * cell,
              y0 + g->player.pos.y * cell + cell / 2 + fy2 * cell, pcol);
}
