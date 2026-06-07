/*
 * heap_churn.c — malloc/free hot path.
 *
 * Exercises the Unit-4 allocator and runtime classifier. With FlexFat ON, every
 * malloc goes through `lowfat_malloc` (per-class bump+freelist), every free
 * through `lowfat_free` (classifier + LIFO push). With FlexFat OFF, this is
 * straight glibc malloc.
 *
 * No OOB; should run cleanly under all configs. Result-printing prevents the
 * optimizer from dead-stripping the workload.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define TRIALS 6000
#define BATCH  10000

int main(void) {
    uintptr_t accum = 0;
    void **p = malloc(BATCH * sizeof(void *));
    if (!p) return 1;
    for (int t = 0; t < TRIALS; t++) {
        for (int i = 0; i < BATCH; i++) {
            size_t sz = 16 + ((i * 13 + t) & 511);
            p[i] = malloc(sz);
            ((char *)p[i])[0] = (char)i;
            accum += (uintptr_t)p[i];
        }
        for (int i = 0; i < BATCH; i++) free(p[i]);
    }
    free(p);
    printf("%llx\n", (unsigned long long)accum);
    return 0;
}
