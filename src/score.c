#include "game.h"
#include <stdio.h>

#define SCORE_FILE "score.dat"

uint32_t score_compute(const Player *p) {
    return (uint32_t)(p->depth * 100 + p->xp + p->gold + p->level * 50);
}

void score_load(ScoreList *list) {
    memset(list, 0, sizeof *list);
    list->magic = SCORE_MAGIC;
    FILE *f = fopen(SCORE_FILE, "rb");
    if (!f) return;
    ScoreList tmp;
    if (fread(&tmp, sizeof tmp, 1, f) == 1 && tmp.magic == SCORE_MAGIC) {
        if (tmp.count > MAX_SCORES) tmp.count = MAX_SCORES;
        *list = tmp;
    }
    fclose(f);
}

void score_save(const ScoreList *list) {
    FILE *f = fopen(SCORE_FILE, "wb");
    if (!f) return;
    fwrite(list, sizeof *list, 1, f);
    fclose(f);
}

int score_insert(ScoreList *list, const ScoreEntry *e) {
    /* Insert sorted by descending score; keep at most MAX_SCORES. */
    uint32_t n = list->count;
    if (n > MAX_SCORES) n = MAX_SCORES;
    int pos = (int)n;
    for (int i = 0; i < (int)n; ++i) {
        if (e->score > list->entries[i].score) { pos = i; break; }
    }
    if (pos >= MAX_SCORES) return -1;
    /* Shift down. */
    for (int i = MAX_SCORES - 1; i > pos; --i) {
        list->entries[i] = list->entries[i - 1];
    }
    list->entries[pos] = *e;
    if (list->count < MAX_SCORES) list->count++;
    return pos;
}
