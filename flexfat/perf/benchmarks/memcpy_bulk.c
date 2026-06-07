/*
 * memcpy_bulk.c — bulk memcpy hot path.
 *
 * With FlexFat ON, every memcpy is replaced by `lowfat_memcpy` (Unit-5
 * memops): bounds-check src+n and dst+n against their respective lowfat
 * objects, then call libc memcpy on the inner range. The check is once per
 * memcpy, not per byte, so overhead is dominated by the underlying copy.
 * This tests whether the wrapping itself is cheap (one bounds check per
 * large copy should be lost in the noise).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFSZ  (64 * 1024)
#define ITERS  1000000

int main(void) {
    char *src = malloc(BUFSZ);
    char *dst = malloc(BUFSZ);
    if (!src || !dst) return 1;
    for (int i = 0; i < BUFSZ; i++) src[i] = (char)(i & 0xff);
    int parity = 0;
    for (int t = 0; t < ITERS; t++) {
        memcpy(dst, src, BUFSZ);
        parity ^= dst[t & (BUFSZ - 1)];
    }
    printf("%d\n", parity);
    free(src); free(dst);
    return 0;
}
