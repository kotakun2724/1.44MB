#include "game.h"

/* xorshift32 - small, fast, deterministic. */
void rng_seed(RNG *r, uint32_t seed) {
    if (seed == 0) seed = 0xC0FFEEu;
    r->s = seed;
}

uint32_t rng_next(RNG *r) {
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

int rng_range(RNG *r, int lo, int hi) {
    if (hi <= lo) return lo;
    uint32_t span = (uint32_t)(hi - lo + 1);
    return lo + (int)(rng_next(r) % span);
}

int rng_chance(RNG *r, int percent) {
    return (int)(rng_next(r) % 100u) < percent;
}
