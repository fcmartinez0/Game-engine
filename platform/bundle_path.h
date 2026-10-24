#ifndef GE_BUNDLE_PATH_H
#define GE_BUNDLE_PATH_H

#include <stdbool.h>
#include <string.h>

/* Is this what SDL_GetBasePath() returns inside a macOS app bundle? There it is the bundle's Resources folder,
 * "/Applications/Fran's demo.app/Contents/Resources/"; run bare from a terminal it is the folder the program is in.
 * (Kept apart from sdl2.c so the test-suite can check it on any machine.) */
static inline bool ge_base_is_app_bundle(const char *base) {
    static const char tail[] = ".app/Contents/Resources";
    if (!base) return false;
    size_t n = strlen(base);
    while (n > 0 && base[n - 1] == '/') n--;
    return n >= sizeof tail - 1 && memcmp(base + n - (sizeof tail - 1), tail, sizeof tail - 1) == 0;
}

#endif
