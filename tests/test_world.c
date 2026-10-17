/* The world as data: the built-in world survives a save and a load, bad files say where they are wrong,
 * and the editing helpers keep rooms consistent. */

#include "../demo/demo.h"

#include <stdio.h>
#include <string.h>

#define GLOAMWOOD_TILEMAP_HASH 0xc53e6169u   /* the old level.c's, cell for cell */
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char *tmp_path(const char *name) {
    static char p[256];
    snprintf(p, sizeof p, "world_test_%s.txt", name);
    return p;
}

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static void test_round_trip(void) {
    puts("built-in world: save, load, same world");
    static World a, b;
    world_builtin(&a);
    CHECK(a.nrooms >= 1 && !strcmp(a.rooms[0].name, "gloamwood") && a.rooms[0].w == 214 && a.rooms[0].h == 15);
    CHECK(world_save(&a, tmp_path("rt"), false) == 0);
    char err[128] = "";
    int rc = world_load(&b, tmp_path("rt"), err, sizeof err);
    if (rc) printf("  load failed at line %d: %s\n", rc, err);
    CHECK(rc == 0);
    CHECK(world_equal(&a, &b));
    /* and again: what it saves is what it read */
    CHECK(world_save(&b, tmp_path("rt2"), false) == 0);
    FILE *f1 = fopen(tmp_path("rt"), "r"), *f2 = fopen(tmp_path("rt2"), "r");
    bool same = f1 && f2;
    for (int c1 = 0, c2 = 0; same && c1 != EOF; ) { c1 = fgetc(f1); c2 = fgetc(f2); same = c1 == c2; }
    if (f1) fclose(f1);
    if (f2) fclose(f2);
    CHECK(same);
    /* a thing with every kind of setting */
    Room *r = &b.rooms[0];
    Thing s = thing_default(TH_SIGN, 3, 10);
    snprintf(s.text, sizeof s.text, "A SIGN, WITH SPACES = AND EQUALS");
    room_add_thing(r, s);
    Thing gt = thing_default(TH_GATE, 9, 8);
    gt.w = 2; gt.h = 4; gt.ch = 7; gt.need = 0; gt.invert = true; gt.plank = true;
    room_add_thing(r, gt);
    Thing lv = thing_default(TH_LEVER, 11, 10);
    lv.ch = 7; lv.time = 300; lv.mode = LEVER_TIMED;
    room_add_thing(r, lv);
    Thing rl = thing_default(TH_RELIC, 12, 10);
    rl.gives = GIVE_NEEDLE; snprintf(rl.name, sizeof rl.name, "SHARP THING");
    room_add_thing(r, rl);
    Thing mk = thing_default(TH_MARK, 13, 10);
    snprintf(mk.name, sizeof mk.name, "A PLACE");
    room_add_thing(r, mk);
    Thing d = thing_default(TH_DOOR, 1, 10);
    snprintf(d.name, sizeof d.name, "west"); snprintf(d.target, sizeof d.target, "hollow:entry");
    room_add_thing(r, d);
    room_set_tile(r, 2, 2, 'C');
    room_set_tile(r, 3, 2, 'H');
    CHECK(world_save(&b, tmp_path("rt3"), false) == 0);
    static World c;
    CHECK(world_load(&c, tmp_path("rt3"), err, sizeof err) == 0);
    CHECK(world_equal(&b, &c));
    CHECK(room_tile(&c.rooms[0], 2, 2) == 'C' && room_tile(&c.rooms[0], 3, 2) == 'H');
    int li = room_find(&c.rooms[0], TH_LEVER, 0), ri = room_find(&c.rooms[0], TH_RELIC, 0);
    CHECK(li >= 0 && c.rooms[0].things[li].time == 300 && c.rooms[0].things[li].mode == LEVER_TIMED);   /* 5.0 s */
    CHECK(ri >= 0 && c.rooms[0].things[ri].gives == GIVE_NEEDLE && !strcmp(c.rooms[0].things[ri].name, "SHARP THING"));
    /* only the rooms marked custom are written when asked */
    static World d1, d2;
    world_builtin(&d1);
    Room *extra = world_add_room(&d1, "extra", 30, 16, BD_CAVE);
    CHECK(extra != NULL);
    extra->custom = true;
    CHECK(world_save(&d1, tmp_path("part"), true) == 0);
    CHECK(world_load(&d2, tmp_path("part"), err, sizeof err) == 0 && d2.nrooms == 1 && !strcmp(d2.rooms[0].name, "extra"));
    world_free(&d1); world_free(&d2);
    world_free(&a); world_free(&b); world_free(&c);
}

/* The built-in Gloamwood's tilemap, pinned: FNV-1a over every cell's type, then every cell's picture. It must
 * only change on purpose (and then the benchmark scenes' frames change too). */
static void test_gloamwood_pinned(void) {
    puts("the built-in Gloamwood is pinned");
    static World w;
    world_builtin(&w);
    GeTilemap m;
    memset(&m, 0, sizeof m);
    room_build_tilemap(&w.rooms[0], &m);
    uint32_t h = 2166136261u;
    for (int i = 0; i < m.w * m.h; i++) { h ^= m.type[i]; h *= 16777619u; }
    for (int i = 0; i < m.w * m.h; i++) { h ^= m.type[i] ? m.vis[i] : 0; h *= 16777619u; }
    printf("  tilemap hash %08x\n", h);
    CHECK(h == GLOAMWOOD_TILEMAP_HASH);
    ge_tilemap_free(&m);
    world_free(&w);
}

static void test_friendly_values(void) {
    puts("values written the way a person would");
    static World w;
    char err[128];
    write_file(tmp_path("friendly"), "\xEF\xBB\xBFgloamwood-world 1\nroom r 25 15 cave\ntiles\n"
                                     ".........................  \n.........................\t\n.........................\n.........................\n"
                                     ".........................\n.........................\n.........................\n.........................\n"
                                     ".........................\n.........................\n.........................\n.........................\n"
                                     ".........................\n.........................\n#########################\nend\n"
                                     "thing lever 3 13 mode=timed time=2.5\nthing gate 6 13 need=all\nthing gate 8 13 need=any\n"
                                     "thing lock 10 13\nendroom\n");
    int rc = world_load(&w, tmp_path("friendly"), err, sizeof err);
    if (rc) printf("  %d: %s\n", rc, err);
    CHECK(rc == 0 && w.nrooms == 1 && w.rooms[0].nthings == 4);         /* a BOM and trailing spaces are fine */
    if (rc == 0) {
        CHECK(w.rooms[0].things[0].time == 150 && w.rooms[0].things[0].mode == LEVER_TIMED);
        CHECK(w.rooms[0].things[1].need == 0 && w.rooms[0].things[2].need == 1);
        CHECK(w.rooms[0].things[3].kind == TH_GATE && w.rooms[0].things[3].lock && w.rooms[0].things[3].ch == 0);
    }
    write_file(tmp_path("badname"), "gloamwood-world 1\nroom Big_Room 25 15 cave\n");
    CHECK(world_load(&w, tmp_path("badname"), err, sizeof err) == 2 && strstr(err, "a-z") != NULL);
    world_free(&w);
}

static void test_bad_files(void) {
    puts("bad world files say what and where");
    static World w;
    char err[128];
    write_file(tmp_path("bad1"), "something else\n");
    CHECK(world_load(&w, tmp_path("bad1"), err, sizeof err) == 1 && w.nrooms == 0);
    write_file(tmp_path("bad2"), "gloamwood-world 1\nroom r 25 15 forest\ntiles\n"
                                 ".........................\n.........................\n.........................\n.........................\n"
                                 ".........................\n.........................\n.........................\n.........................\n"
                                 ".........................\n.........................\n.........................\n.........................\n"
                                 ".........................\n.........................\n#########################\nend\n"
                                 "thing lever 3 13 ch=2\nthing wobbler 4 13\nendroom\n");
    int rc = world_load(&w, tmp_path("bad2"), err, sizeof err);
    CHECK(rc == 21 && strstr(err, "wobbler") != NULL);                 /* the unknown kind, on its line */
    write_file(tmp_path("bad3"), "gloamwood-world 1\nroom r 25 15 forest\ntiles\n.........................\n....x....................\n");
    rc = world_load(&w, tmp_path("bad3"), err, sizeof err);
    CHECK(rc == 5 && strstr(err, "'x'") != NULL);                      /* a stray character in the tiles */
    write_file(tmp_path("bad4"), "gloamwood-world 1\nroom r 25 15 forest\ntiles\n....\n");
    rc = world_load(&w, tmp_path("bad4"), err, sizeof err);
    CHECK(rc == 4 && strstr(err, "width") != NULL);                    /* a short row */
    write_file(tmp_path("bad5"), "gloamwood-world 1\nroom r 25 15 forest\nthing bench 40 3\n");
    rc = world_load(&w, tmp_path("bad5"), err, sizeof err);
    CHECK(rc == 3 && strstr(err, "outside") != NULL);                  /* a thing out of the room */
    /* settings it doesn't know are ignored (a newer file still loads) */
    write_file(tmp_path("ok"), "gloamwood-world 1\n# a note\nroom r 25 15 cave\ntiles\n"
                               ".........................\n.........................\n.........................\n.........................\n"
                               ".........................\n.........................\n.........................\n.........................\n"
                               ".........................\n.........................\n.........................\n.........................\n"
                               ".........................\n.........................\n#########################\nend\n"
                               "thing lever 3 13 ch=2 sparkle=yes\nendroom\n");
    rc = world_load(&w, tmp_path("ok"), err, sizeof err);
    CHECK(rc == 0 && w.nrooms == 1 && w.rooms[0].backdrop == BD_CAVE && w.rooms[0].nthings == 1 && w.rooms[0].things[0].ch == 2);
    world_free(&w);
}

static void test_editing_helpers(void) {
    puts("room editing helpers");
    static World w;
    memset(&w, 0, sizeof w);
    Room *r = world_add_room(&w, "test", 30, 16, BD_CAVE);
    CHECK(r && r->w == 30 && r->h == 16 && room_tile(r, 5, 15) == '#' && room_tile(r, 5, 14) == '.');
    CHECK(room_tile(r, -1, 0) == '.' && room_tile(r, 30, 0) == '.');   /* outside reads as empty */
    room_set_tile(r, 4, 4, 'Z');                                       /* not a tile: ignored */
    CHECK(room_tile(r, 4, 4) == '.');
    room_add_thing(r, thing_default(TH_CRATE, 28, 14));
    room_add_thing(r, thing_default(TH_BENCH, 3, 14));
    room_resize(r, 26, 20);                                            /* narrower: the crate falls outside */
    CHECK(r->w == 26 && r->h == 20 && r->nthings == 1 && r->things[0].kind == TH_BENCH);
    CHECK(room_tile(r, 5, 15) == '#' && room_tile(r, 5, 19) == '#' && room_tile(r, 5, 18) == '.');
    room_resize(r, 3, 3);                                              /* never smaller than a screen */
    CHECK(r->w == ROOM_MIN_W && r->h == ROOM_MIN_H);
    GeTilemap m;
    memset(&m, 0, sizeof m);
    room_set_tile(r, 1, 1, 'C'); room_set_tile(r, 2, 1, '='); room_set_tile(r, 3, 1, '^'); room_set_tile(r, 4, 1, 'S');
    room_build_tilemap(r, &m);
    CHECK(m.type[1 * m.w + 1] == GE_TILE_SOLID && m.vis[1 * m.w + 1] == TV_CRACK);
    CHECK(m.type[1 * m.w + 2] == GE_TILE_ONEWAY && m.type[1 * m.w + 3] == GE_TILE_SPIKE);
    CHECK(m.vis[1 * m.w + 4] == TV_STONE_TOP);                         /* stone open above shows its top edge */
    ge_tilemap_free(&m);
    CHECK(world_find_room(&w, "test") == 0 && world_find_room(&w, "nope") == -1);
    CHECK(thing_kind_from_name("gate") == TH_GATE && thing_kind_from_name("??") == -1 && !strcmp(thing_kind_name(TH_RELIC), "relic"));
    CHECK(thing_kind_from_name("lock") == TH_GATE);                    /* the old name for a locked gate */
    world_free(&w);
}

int main(void) {
    test_round_trip();
    test_gloamwood_pinned();
    test_friendly_values();
    test_bad_files();
    test_editing_helpers();
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
