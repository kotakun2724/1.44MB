/*
 *  Column-based raycaster renderer (Wolfenstein-style 3D over the existing
 *  grid map). Walls are textured with hand-generated 64x64 paletted tiles,
 *  floor and ceiling use perspective-correct affine mapping, mobs and items
 *  appear as depth-buffered scaled silhouettes with their ASCII glyph on top.
 *
 *  Drawn directly into the TIGR backbuffer via bmp->pix for speed.
 */
#include "game.h"
#include <math.h>

#define TEX_SIZE      64
#define TEX_MASK      (TEX_SIZE - 1)
#define NUM_WALL_TEX  6                /* 0..4 generic, 5 = door */
#define FOV_PLANE     0.66f            /* tan(33 deg) -> ~66 deg HFOV */
#define MAX_LINE_MUL  8                /* clamp line_h to vh * MAX_LINE_MUL */
#define MAX_VIEW_W    640

static uint8_t g_wall_tex[NUM_WALL_TEX][TEX_SIZE * TEX_SIZE];
static TPixel  g_wall_pal[NUM_WALL_TEX][16];
static uint8_t g_floor_tex[TEX_SIZE * TEX_SIZE];
static TPixel  g_floor_pal[16];
static uint8_t g_ceil_tex [TEX_SIZE * TEX_SIZE];
static TPixel  g_ceil_pal [16];
static int     g_init = 0;

/* Deterministic noise stream for texture generation. */
static uint32_t tex_rand(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}

static uint32_t hash_cell(int x, int y) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
    h ^= h >> 16; h *= 0x7feb352du;
    h ^= h >> 15; h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

static TPixel rgb_(int r, int g, int b) {
    TPixel p = { (uint8_t)r, (uint8_t)g, (uint8_t)b, 255 };
    return p;
}

/* Linear 16-entry ramp from `a` to `b`. */
static void ramp(TPixel *pal, TPixel a, TPixel b) {
    for (int i = 0; i < 16; ++i) {
        pal[i].r = (uint8_t)(a.r + (b.r - a.r) * i / 15);
        pal[i].g = (uint8_t)(a.g + (b.g - a.g) * i / 15);
        pal[i].b = (uint8_t)(a.b + (b.b - a.b) * i / 15);
        pal[i].a = 255;
    }
}

/* ---- Procedural texture generators -------------------------------------- */
static void gen_metal_panel(uint8_t *tex, TPixel *pal) {
    ramp(pal, rgb_(20, 22, 28), rgb_(180, 180, 200));
    uint32_t s = 0xc0ffeeu;
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int v = 7 + (int)(tex_rand(&s) & 3);
            if ((y % 16) == 0 || (y % 16) == 15) v = 3;
            if ((y % 16) == 8 && (x % 16) == 8) v = 13;
            if ((y % 16) == 8 && ((x + 15) % 16) == 8) v = 11;
            if (x == 0 || x == 32) v = 2;
            tex[y * TEX_SIZE + x] = (uint8_t)v;
        }
    }
}

static void gen_concrete(uint8_t *tex, TPixel *pal) {
    ramp(pal, rgb_(30, 30, 36), rgb_(170, 170, 180));
    uint32_t s = 0xdeadbeefu;
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int v = 6 + (int)(tex_rand(&s) % 7);
            if ((tex_rand(&s) & 0x1f) == 0) v = 2;
            tex[y * TEX_SIZE + x] = (uint8_t)v;
        }
    }
}

static void gen_bios_chip(uint8_t *tex, TPixel *pal) {
    /* Dark green PCB + IC body + leaded pins + yellow label. */
    pal[0]  = rgb_(  8, 12,  8);
    pal[1]  = rgb_( 12, 30, 16);
    pal[2]  = rgb_( 16, 50, 24);
    pal[3]  = rgb_( 20, 70, 30);
    pal[4]  = rgb_( 28,100, 40);
    pal[5]  = rgb_( 36,140, 50);
    pal[6]  = rgb_( 60,180, 80);
    pal[7]  = rgb_(120,220,140);
    pal[8]  = rgb_( 50, 50, 60);
    pal[9]  = rgb_(100,100,110);
    pal[10] = rgb_(170,170,180);
    pal[11] = rgb_(220,220,220);
    pal[12] = rgb_(220,200, 80);
    pal[13] = rgb_(240,230,130);
    pal[14] = rgb_(255,240,160);
    pal[15] = rgb_(255,255,200);

    uint32_t s = 0x12345678u;
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int v = 2 + (int)(tex_rand(&s) & 1);
            if ((x % 8) == 0) v = 4;
            int cx = x - 16, cy = y - 16;
            if (cx >= 0 && cx < 32 && cy >= 0 && cy < 32) {
                v = 8;
                if (cx == 0 || cx == 31 || cy == 0 || cy == 31) v = 9;
                if (cy >= 12 && cy <= 18 && cx >= 4 && cx <= 26)
                    if (((cx + cy) & 1) == 0) v = 12;
                if ((cy == 4 || cy == 27) && (cx % 4) == 0 && cx > 1 && cx < 31)
                    v = 10;
            }
            tex[y * TEX_SIZE + x] = (uint8_t)v;
        }
    }
}

static void gen_blood_metal(uint8_t *tex, TPixel *pal) {
    pal[0]  = rgb_( 10,  8,  8);
    pal[1]  = rgb_( 24, 18, 18);
    pal[2]  = rgb_( 40, 30, 30);
    pal[3]  = rgb_( 60, 45, 45);
    pal[4]  = rgb_( 80, 60, 60);
    pal[5]  = rgb_(100, 75, 75);
    pal[6]  = rgb_(140,100,100);
    pal[7]  = rgb_(180,130,130);
    pal[8]  = rgb_( 60, 10, 10);
    pal[9]  = rgb_(100, 14, 14);
    pal[10] = rgb_(140, 20, 20);
    pal[11] = rgb_(180, 30, 30);
    pal[12] = rgb_(220, 40, 40);
    pal[13] = rgb_(255, 60, 60);
    pal[14] = rgb_(255,100, 80);
    pal[15] = rgb_(255,160,120);

    uint32_t s = 0xfeedfaceu;
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int v = 3 + (int)(tex_rand(&s) & 3);
            if ((y % 16) == 0) v = 1;
            uint32_t r = tex_rand(&s);
            if ((r & 0x3f) == 0) v = 9 + (int)(r & 3);
            tex[y * TEX_SIZE + x] = (uint8_t)v;
        }
    }
    /* Vertical drip streaks. */
    s = 0xbada55u;
    for (int dr = 0; dr < 4; ++dr) {
        int dx = (int)(tex_rand(&s) & TEX_MASK);
        int len = 12 + (int)(tex_rand(&s) & 31);
        for (int y = 0; y < len && y < TEX_SIZE; ++y) {
            int v = 11 - y / 4;
            if (v < 8) v = 8;
            tex[y * TEX_SIZE + dx] = (uint8_t)v;
        }
    }
}

static void gen_floppy_label(uint8_t *tex, TPixel *pal) {
    pal[0]  = rgb_( 20, 12,  8);
    pal[1]  = rgb_( 40, 20, 10);
    pal[2]  = rgb_( 80, 40, 16);
    pal[3]  = rgb_(120, 60, 22);
    pal[4]  = rgb_(160, 90, 30);
    pal[5]  = rgb_(200,130, 50);
    pal[6]  = rgb_(220,170, 80);
    pal[7]  = rgb_(230,200,130);
    pal[8]  = rgb_(240,220,180);
    pal[9]  = rgb_(245,230,200);
    for (int i = 10; i < 16; ++i) pal[i] = rgb_(250, 240, 220);

    uint32_t s = 0xabcd1234u;
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int v = 7;
            if (y < 10) v = 4;
            if (y > 56) v = 3;
            if (y >= 18 && y <= 22 && x >= 8 && x <= 56) v = (x & 1) ? 2 : 8;
            if (y >= 28 && y <= 32 && x >= 8 && x <= 48) v = (x & 1) ? 2 : 8;
            if (y >= 38 && y <= 42 && x >= 8 && x <= 52) v = (x & 1) ? 2 : 8;
            if ((tex_rand(&s) & 0xf) == 0) {
                int n = (int)(tex_rand(&s) & 1);
                v = n ? v + 1 : v - 1;
                if (v < 0) v = 0;
                if (v > 9) v = 9;
            }
            tex[y * TEX_SIZE + x] = (uint8_t)v;
        }
    }
}

static void gen_door(uint8_t *tex, TPixel *pal) {
    ramp(pal, rgb_(12, 14, 20), rgb_(160, 175, 200));
    uint32_t s = 0xc0c0a55u;
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int v = 8 + (int)(tex_rand(&s) & 1);
            if (x < 2 || x > 61 || y < 2 || y > 61) v = 3;
            if ((x == 8 || x == 55) && y >= 8 && y <= 55) v = 5;
            if ((y == 8 || y == 55) && x >= 8 && x <= 55) v = 5;
            int hx = x - 48, hy = y - 30;
            if (hx >= 0 && hx < 6 && hy >= 0 && hy < 4) v = 13;
            tex[y * TEX_SIZE + x] = (uint8_t)v;
        }
    }
}

static void gen_floor_tex(uint8_t *tex, TPixel *pal) {
    ramp(pal, rgb_(20, 22, 30), rgb_(120, 130, 160));
    uint32_t s = 0xacaba55u;
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            int v = 4 + (int)(tex_rand(&s) & 3);
            if ((x % 8) == 4 && (y % 8) == 4) v = 11;
            if ((x % 16) == 0 || (y % 16) == 0) v = 2;
            tex[y * TEX_SIZE + x] = (uint8_t)v;
        }
    }
}

static void gen_ceil_tex(uint8_t *tex, TPixel *pal) {
    ramp(pal, rgb_(4, 6, 12), rgb_(180, 200, 255));
    uint32_t s = 0xfacefeedu;
    for (int y = 0; y < TEX_SIZE; ++y) {
        for (int x = 0; x < TEX_SIZE; ++x) {
            uint32_t r = tex_rand(&s);
            int v = 1 + (int)(r & 1);
            if ((r & 0xff) < 4) v = 6 + (int)(r & 7);
            tex[y * TEX_SIZE + x] = (uint8_t)v;
        }
    }
}

void render3d_init(void) {
    if (g_init) return;
    g_init = 1;
    gen_metal_panel  (g_wall_tex[0], g_wall_pal[0]);
    gen_concrete     (g_wall_tex[1], g_wall_pal[1]);
    gen_bios_chip    (g_wall_tex[2], g_wall_pal[2]);
    gen_blood_metal  (g_wall_tex[3], g_wall_pal[3]);
    gen_floppy_label (g_wall_tex[4], g_wall_pal[4]);
    gen_door         (g_wall_tex[5], g_wall_pal[5]);
    gen_floor_tex    (g_floor_tex,   g_floor_pal);
    gen_ceil_tex     (g_ceil_tex,    g_ceil_pal);
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

        uint8_t *tex = is_floor ? g_floor_tex : g_ceil_tex;
        TPixel  *pal = is_floor ? g_floor_pal : g_ceil_pal;

        int lm = light_mul(row_dist);
        int sy = y0 + y;
        if ((unsigned)sy >= (unsigned)s->h) continue;
        TPixel *row = &s->pix[sy * s->w + x0];

        for (int x = 0; x < vw; ++x) {
            int tx = (int)(TEX_SIZE * (floor_x - floorf(floor_x))) & TEX_MASK;
            int ty = (int)(TEX_SIZE * (floor_y - floorf(floor_y))) & TEX_MASK;
            TPixel c = pal[tex[ty * TEX_SIZE + tx] & 15];
            row[x].r = (uint8_t)((c.r * lm) >> 8);
            row[x].g = (uint8_t)((c.g * lm) >> 8);
            row[x].b = (uint8_t)((c.b * lm) >> 8);
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

        int tex_idx;
        if (tile_kind == T_DOOR) tex_idx = 5;
        else tex_idx = (int)(hash_cell(mapX, mapY) % 5u);
        uint8_t *tex = g_wall_tex[tex_idx];
        TPixel  *pal = g_wall_pal[tex_idx];

        float wall_hit = (side == 0) ? (py + perp * rdy)
                                      : (px + perp * rdx);
        wall_hit -= floorf(wall_hit);
        int tx = (int)(wall_hit * (float)TEX_SIZE);
        if (side == 0 && rdx > 0) tx = TEX_SIZE - tx - 1;
        if (side == 1 && rdy < 0) tx = TEX_SIZE - tx - 1;
        tx &= TEX_MASK;

        int lm = light_mul(perp);
        if (side == 1) lm = (lm * 180) >> 8;   /* Y-side darker (Wolf3D style) */

        int sx = x0 + x;
        if ((unsigned)sx >= (unsigned)s->w) continue;

        for (int yy = top; yy < bot; ++yy) {
            int d  = yy * 256 - vh * 128 + line_h * 128;
            int ty = ((d * TEX_SIZE) / line_h) >> 8;
            ty &= TEX_MASK;
            TPixel c = pal[tex[ty * TEX_SIZE + tx] & 15];
            int sy = y0 + yy;
            if ((unsigned)sy >= (unsigned)s->h) continue;
            TPixel *out = &s->pix[sy * s->w + sx];
            out->r = (uint8_t)((c.r * lm) >> 8);
            out->g = (uint8_t)((c.g * lm) >> 8);
            out->b = (uint8_t)((c.b * lm) >> 8);
            out->a = 255;
        }
    }

    /* --- Sprites: mobs + items, depth-sorted, z-buffered. --- */
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
        char glyph;
        TPixel base;
        if (sr.is_mob) {
            Mob *m = &g->mobs[sr.idx];
            spr_x = m->pos.x + 0.5f; spr_y = m->pos.y + 0.5f;
            glyph = MOB_TYPES[m->kind].glyph;
            base  = MOB_TYPES[m->kind].color;
        } else {
            FloorItem *f = &g->floor[sr.idx];
            spr_x = f->pos.x + 0.5f; spr_y = f->pos.y + 0.5f;
            glyph = ITEMS[f->def].glyph;
            base  = ITEMS[f->def].color;
        }
        float dx = spr_x - px, dy = spr_y - py;
        float tx_c = inv_det * (diry * dx - dirx * dy);
        float ty_c = inv_det * (-planey * dx + planex * dy);
        if (ty_c <= 0.2f) continue;

        int screen_x = (int)((vw / 2) * (1.0f + tx_c / ty_c));
        int spr_h = (int)((float)vh / ty_c);
        if (spr_h < 6) spr_h = 6;
        if (spr_h > vh * MAX_LINE_MUL) spr_h = vh * MAX_LINE_MUL;

        int lm = light_mul(ty_c);
        TPixel tinted = {
            (uint8_t)((base.r * lm) >> 8),
            (uint8_t)((base.g * lm) >> 8),
            (uint8_t)((base.b * lm) >> 8), 255
        };
        TPixel outline = {
            (uint8_t)(tinted.r / 3),
            (uint8_t)(tinted.g / 3),
            (uint8_t)(tinted.b / 3), 255
        };

        int top, bot, w;
        if (sr.is_mob) {
            int h = (spr_h * 3) / 4;          /* slightly shorter than wall */
            int floor_y2 = (vh + spr_h) / 2;
            bot = floor_y2;
            top = bot - h;
            w   = h / 2;
        } else {
            int item_h = spr_h / 4;
            if (item_h < 3) item_h = 3;
            bot = (vh + spr_h) / 2;
            top = bot - item_h;
            w   = item_h * 2;
        }
        int left  = screen_x - w / 2;
        int right = screen_x + w / 2;
        int top_c = top < 0 ? 0 : top;
        int bot_c = bot > vh ? vh : bot;

        for (int cx = left; cx < right; ++cx) {
            if (cx < 0 || cx >= vw) continue;
            if (ty_c >= zbuf[cx]) continue;
            int is_edge_x = (cx == left || cx == right - 1);
            for (int cy = top_c; cy < bot_c; ++cy) {
                int is_edge_y = (cy == top_c || cy == bot_c - 1);
                TPixel pc = (is_edge_x || is_edge_y) ? outline : tinted;
                put_px(s, x0 + cx, y0 + cy, pc);
            }
        }

        /* Stamp the glyph at sprite center if visible + large enough. */
        if (spr_h >= 14 && screen_x >= 0 && screen_x < vw
            && ty_c < zbuf[screen_x]) {
            char buf[2] = { glyph, 0 };
            int gx = x0 + screen_x - 4;
            int gy = y0 + (top + bot) / 2 - 4;
            TPixel gc = { 240, 240, 240, 255 };
            tigrPrint(s, tfont, gx, gy, gc, "%s", buf);
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
