/* Counts guest instructions in a qemu-arm trace, for tools/perf/armcount.sh.
 *
 * Input (stdin): the output of `qemu-arm -d nochain,exec,in_asm`. Each translated block is listed once
 * ("IN:", its address, its bytes) and every execution of a block is a "Trace" line naming its address and
 * function. Instructions are counted between entering START (the game's draw function) and entering STOP
 * (what the engine calls after it), for the START entries FIRST+1 .. FIRST+N, so the result is the draw cost
 * of N frames and nothing else.
 *   qcount <start_pc hex> <stop_pc hex> <first> <n>      PER=1 in the environment: also each frame's total */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HN (1 << 20)   /* block start address -> instructions in the block */
static uint32_t hk[HN];
static int hv[HN];
static char hu[HN];
static int *slot(uint32_t pc) {
    uint32_t h = (pc * 2654435761u) >> 12 & (HN - 1);
    while (hu[h] && hk[h] != pc) h = (h + 1) & (HN - 1);
    if (!hu[h]) { hu[h] = 1; hk[h] = pc; hv[h] = 0; }
    return &hv[h];
}

#define SN 8192        /* per-function totals */
static char sname[SN][64];
static double scount[SN];
static int sn;
static double *sym(const char *s) {
    for (int i = 0; i < sn; i++) if (!strcmp(sname[i], s)) return &scount[i];
    if (sn >= SN) return &scount[0];
    snprintf(sname[sn], sizeof sname[sn], "%s", s);
    scount[sn] = 0;
    return &scount[sn++];
}

static int hexv(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: qcount start_pc stop_pc first n\n"); return 2; }
    uint32_t start = (uint32_t)strtoul(argv[1], 0, 16), stop = (uint32_t)strtoul(argv[2], 0, 16);
    int first = atoi(argv[3]), n = atoi(argv[4]);
    bool per = getenv("PER") != NULL;
    static char line[1 << 16];
    uint32_t cur_pc = 0;
    int cur_n = 0, in_block = 0, entries = 0, counting = 0;
    double total = 0, frame_start = 0;
    while (fgets(line, sizeof line, stdin)) {
        if (!strncmp(line, "IN:", 3)) { in_block = 1; cur_pc = 0; cur_n = 0; continue; }
        if (in_block) {
            if (line[0] == '0' && line[1] == 'x' && !cur_pc) { cur_pc = (uint32_t)strtoul(line + 2, 0, 16); continue; }
            if (!strncmp(line, "OBJD-", 5)) {               /* the block's bytes: A = ARM (4 bytes each), T = Thumb (2 or 4) */
                bool thumb = line[5] == 'T';
                const char *h = line + 8;
                int len = 0;
                while (h[len] && h[len] != '\n') len++;
                if (!thumb) cur_n += len / 8;
                else
                    for (int i = 0; i + 4 <= len; ) {
                        int top = (hexv(h[i + 2]) * 16 + hexv(h[i + 3])) >> 3;   /* high byte of the first halfword */
                        i += (top == 0x1d || top == 0x1e || top == 0x1f) ? 8 : 4;
                        cur_n++;
                    }
                continue;
            }
            if (line[0] != '\n' && strncmp(line, "---", 3) && strncmp(line, "Trace", 5)) continue;
            if (cur_pc) *slot(cur_pc) = cur_n;
            in_block = 0;
            if (line[0] != 'T') continue;
        }
        if (strncmp(line, "Trace", 5)) continue;
        char *b = strchr(line, '['), *p = b ? strchr(b, '/') : NULL;
        if (!p) continue;
        uint32_t pc = (uint32_t)strtoull(p + 1, 0, 16);
        if (pc == start || pc == stop) {
            if (counting && per) printf("FRAME %d %.0f\n", entries, total - frame_start);
            frame_start = total;
            counting = 0;
            if (pc == start) { entries++; counting = entries > first && entries <= first + n; }
        }
        if (counting) {
            int c = *slot(pc);
            total += c;
            char *nm = strchr(b, ']');
            nm = nm ? nm + 2 : (char *)"?";
            char *e = nm;
            while (*e && *e != '\n') e++;
            *e = 0;
            *sym(*nm ? nm : "?") += c;
        }
    }
    printf("TOTAL %.0f instructions per frame (%d frames counted)\n", n > 0 ? total / n : 0.0, n);
    for (int i = 0; i < sn; i++)            /* largest first */
        for (int j = i + 1; j < sn; j++)
            if (scount[j] > scount[i]) {
                double t = scount[i]; scount[i] = scount[j]; scount[j] = t;
                char tmp[64]; memcpy(tmp, sname[i], 64); memcpy(sname[i], sname[j], 64); memcpy(sname[j], tmp, 64);
            }
    for (int i = 0; i < sn && i < 25; i++) printf("%10.0f  %s\n", scount[i] / n, sname[i]);
    return 0;
}
