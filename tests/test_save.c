/* demo/save.c: the saved game survives a round trip, a cut-off write, a hand-edited file and rubbish. */
#include "../demo/save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const char *P = "save_test.txt";

static void write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static SaveGame a, b;   /* (big: not on the stack) */

static void fill(SaveGame *s) {
    memset(s, 0, sizeof *s);
    snprintf(s->bench_room, sizeof s->bench_room, "hollow");
    s->bench_col = 46; s->bench_row = 11;
    s->bonus_masks = 1; s->bonus_ammo = 2; s->keys = 1;
    s->kills = 12; s->deaths = 3; s->duels_won = 2; s->duels_lost = 1; s->heals = 4; s->play_ticks = 123456;
    s->ending_seen = true;
    s->nrooms = 2;
    SaveRoom *r = &s->rooms[0];
    snprintf(r->name, sizeof r->name, "gloamwood");
    r->nthings = 60;
    for (int i = 0; i < r->nthings; i++) r->kinds[i] = (uint8_t)(i % TH_COUNT);
    for (int i = 0; i < r->nthings; i += 3) r->beaten[i] = true;   /* 20 of them: more than one line's worth is not needed here */
    r->progress.done[5] = r->progress.done[59] = true;
    r = &s->rooms[1];
    snprintf(r->name, sizeof r->name, "deep");
    r->nthings = 20;
    for (int i = 0; i < r->nthings; i++) r->kinds[i] = (uint8_t)(TH_LEVER + i % 3);
    for (int i = 0; i < 20; i++) r->beaten[i] = i % 2;            /* 10, then a list longer than 32 below */
    for (int i = 0; i < 40; i++) { r->progress.broken[i][0] = (int16_t)(100 + i); r->progress.broken[i][1] = (int16_t)(i % 24); }
    r->progress.nbroken = 40;
    r->progress.crate_homed[3] = true; r->progress.crate_home[3][0] = 10; r->progress.crate_home[3][1] = 11;
}

static bool same(const SaveGame *x, const SaveGame *y) { return memcmp(x, y, sizeof *x) == 0; }

int main(void) {
    char err[96];
    save_remove(P);

    /* nothing there */
    CHECK(save_read(&b, P, err, sizeof err) == -1);
    CHECK(!save_exists(P));

    /* a round trip keeps everything */
    fill(&a);
    CHECK(save_write(&a, P) == 0);
    CHECK(save_exists(P));
    CHECK(save_read(&b, P, err, sizeof err) == 0);
    CHECK(same(&a, &b));
    CHECK(save_find_room(&b, "deep") != NULL && save_find_room(&b, "deep")->progress.nbroken == 40);
    CHECK(save_find_room(&b, "nowhere") == NULL);

    /* long lists: every piece of a full room beaten, done and homed */
    SaveRoom *big = &a.rooms[0];
    big->nthings = ROOM_MAX_THINGS;
    for (int i = 0; i < ROOM_MAX_THINGS; i++) { big->kinds[i] = (uint8_t)(TH_COUNT - 1); big->beaten[i] = big->progress.done[i] = true; }
    for (int i = 0; i < ROOM_MAX_BROKEN; i++) { big->progress.broken[i][0] = (int16_t)(ROOM_MAX_W - 1); big->progress.broken[i][1] = (int16_t)(ROOM_MAX_H - 1); }
    big->progress.nbroken = ROOM_MAX_BROKEN;
    CHECK(save_write(&a, P) == 0);
    CHECK(save_read(&b, P, err, sizeof err) == 0);
    CHECK(same(&a, &b));
    fill(&a);

    /* the previous save is kept as .bak, and is what is read if the save is lost */
    CHECK(save_write(&a, P) == 0);
    a.kills = 99;
    CHECK(save_write(&a, P) == 0);
    remove(P);
    CHECK(save_read(&b, P, err, sizeof err) == 0 && b.kills == 12);   /* the one before */

    /* a write cut off before its swap: the whole .tmp is the newest save */
    save_remove(P);
    a.kills = 7;
    CHECK(save_write(&a, P) == 0);
    rename(P, "save_test.txt.tmp");
    CHECK(save_read(&b, P, err, sizeof err) == 0 && b.kills == 7);

    /* a write cut off half way is not a save: the file ends without "end" */
    save_remove(P);
    write_text(P, "fransdemo-save 1\nbench gloamwood 4 11\nroom gloamwood 2 0,1\nbeaten 1\n");
    CHECK(save_read(&b, P, err, sizeof err) > 0);
    CHECK(strstr(err, "cut off") != NULL || strstr(err, "inside room") != NULL);
    CHECK(!save_exists(P));
    /* ...unless the one before it is whole */
    fill(&a);
    CHECK(save_write(&a, P) == 0);                     /* (this moves the broken one to .bak) */
    write_text(P, "fransdemo-save 1\nbench gloamwood 4 11\n");
    rename("save_test.txt.bak", "save_test.txt.keep");
    CHECK(save_read(&b, P, err, sizeof err) > 0);      /* the broken one, and no whole one beside it */
    rename("save_test.txt.keep", "save_test.txt.bak");
    CHECK(save_read(&b, P, err, sizeof err) > 0);      /* .bak is the broken one moved aside earlier */

    /* by hand: notes, Windows line ends, unknown lines from a later version, extra spaces */
    save_remove(P);
    write_text(P, "fransdemo-save 1\r\n# my notes\r\nbench   deep 4 20\r\nstats 1 2 3 4 5 600\r\nwings 3\r\n"
                  "room deep 3 4, 5, 6\r\nbeaten 0 2\r\ndone 1\r\nendroom\r\nend\r\n");
    CHECK(save_read(&b, P, err, sizeof err) == 0);
    CHECK(!strcmp(b.bench_room, "deep") && b.bench_col == 4 && b.bench_row == 20);
    CHECK(b.kills == 1 && b.heals == 5 && b.play_ticks == 600);
    CHECK(b.nrooms == 1 && b.rooms[0].nthings == 3 && b.rooms[0].kinds[2] == 6);
    CHECK(b.rooms[0].beaten[0] && !b.rooms[0].beaten[1] && b.rooms[0].beaten[2] && b.rooms[0].progress.done[1]);

    /* rubbish is refused with the line that is wrong, and leaves nothing half-read */
    static const struct { const char *text; int line; const char *says; } bad[] = {
        { "", 1, "empty" },
        { "gloamwood-world 1\n", 1, "not a save" },
        { "fransdemo-save 2\nend\n", 1, "newer" },
        { "fransdemo-save 1\nbench Gloam Wood 1 1\nend\n", 2, "bench" },
        { "fransdemo-save 1\nbench gloamwood -1 1\nend\n", 2, "bench" },
        { "fransdemo-save 1\nrelics 1\nend\n", 2, "relics" },
        { "fransdemo-save 1\nstats 1 2 3 4 5 x\nend\n", 2, "stats" },
        { "fransdemo-save 1\nroom deep 2 1\nendroom\nend\n", 2, "kinds" },
        { "fransdemo-save 1\nroom deep 1 1,2\nendroom\nend\n", 2, "more kinds" },
        { "fransdemo-save 1\nroom deep 1 99\nendroom\nend\n", 2, "kinds" },
        { "fransdemo-save 1\nroom deep 97\nendroom\nend\n", 2, "room" },
        { "fransdemo-save 1\nbeaten 1\nend\n", 2, "outside" },
        { "fransdemo-save 1\nroom deep 2 1,1\nbeaten 2\nendroom\nend\n", 3, "not one of" },
        { "fransdemo-save 1\nroom deep 2 1,1\nbroken 3\nendroom\nend\n", 3, "column:row" },
        { "fransdemo-save 1\nroom deep 2 1,1\nbroken 3:x\nendroom\nend\n", 3, "column:row" },
        { "fransdemo-save 1\nroom deep 2 1,1\ncrate 5 1:1\nendroom\nend\n", 3, "crate" },
        { "fransdemo-save 1\nroom deep 0\nendroom\nroom deep 0\nendroom\nend\n", 4, "twice" },
        { "fransdemo-save 1\nroom deep 0\nroom hollow 0\nend\n", 3, "inside" },
        { "fransdemo-save 1\nroom deep 0\nend\n", 3, "inside room" },
        { "fransdemo-save 1\nend\nbench gloamwood 1 1\n", 3, "after the end" },
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        save_remove(P);
        write_text(P, bad[i].text);
        int rc = save_read(&b, P, err, sizeof err);
        bool ok = rc == bad[i].line && strstr(err, bad[i].says) != NULL;
        if (!ok) printf("  bad file %u: got line %d \"%s\", wanted line %d \"%s\"\n", i, rc, err, bad[i].line, bad[i].says);
        CHECK(ok);
        static const SaveGame zero;
        CHECK(same(&b, &zero));
    }

    /* NEW GAME: everything goes */
    fill(&a);
    CHECK(save_write(&a, P) == 0 && save_write(&a, P) == 0);
    write_text("save_test.txt.tmp", "fransdemo-save 1\n");
    save_remove(P);
    FILE *f;
    CHECK((f = fopen(P, "r")) == NULL);
    CHECK((f = fopen("save_test.txt.bak", "r")) == NULL);
    CHECK((f = fopen("save_test.txt.tmp", "r")) == NULL);
    CHECK(!save_exists(P));

    /* a folder that isn't there: the write fails cleanly */
    CHECK(save_write(&a, "no_such_folder/save.txt") < 0);

    if (fails) { printf("%d of %d checks failed\n", fails, checks); return 1; }
    printf("save: all %d checks passed\n", checks);
    return 0;
}
