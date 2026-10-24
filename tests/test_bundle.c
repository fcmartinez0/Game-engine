/* platform/bundle_path.h: telling "running from inside a Mac .app" from "run from a folder". */
#include "../platform/bundle_path.h"

#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void) {
    /* inside a bundle (SDL adds a trailing slash; the name may hold spaces and an apostrophe) */
    CHECK(ge_base_is_app_bundle("/Applications/Fran's demo.app/Contents/Resources/"));
    CHECK(ge_base_is_app_bundle("/Applications/Fran's demo.app/Contents/Resources"));
    CHECK(ge_base_is_app_bundle("/Users/me/Downloads/Fran's demo.app/Contents/Resources/"));
    CHECK(ge_base_is_app_bundle("/private/var/folders/xy/T/AppTranslocation/0A1B/d/Fran's demo.app/Contents/Resources/"));
    CHECK(ge_base_is_app_bundle("/a.app/Contents/Resources//"));
    CHECK(ge_base_is_app_bundle(".app/Contents/Resources/"));
    /* a plain program in a plain folder, including one that is *near* a bundle path */
    CHECK(!ge_base_is_app_bundle("/home/me/game/build/"));
    CHECK(!ge_base_is_app_bundle("/Applications/Fran's demo.app/Contents/MacOS/"));
    CHECK(!ge_base_is_app_bundle("/Applications/Fran's demo.app/Contents/"));
    CHECK(!ge_base_is_app_bundle("/Applications/Fran's demo.app/"));
    CHECK(!ge_base_is_app_bundle("/Applications/Fran's demo.app/Contents/Resources/sub/"));
    CHECK(!ge_base_is_app_bundle("/x/demo.app.bak/Contents/Resources/"));
    CHECK(!ge_base_is_app_bundle("/Contents/Resources/"));
    CHECK(!ge_base_is_app_bundle("/"));
    CHECK(!ge_base_is_app_bundle(""));
    CHECK(!ge_base_is_app_bundle(NULL));
    if (fails) { printf("%d checks failed\n", fails); return 1; }
    printf("bundle path: all checks passed\n");
    return 0;
}
