/*
 * array_sum.c — stack-array tight loop.
 *
 * Exercises the bounds-check hot path: every load/store from a stack alloca's
 * lowfat mirror runs `shr/table-load/cmpq/jae`. Hardened mode (no-check-reads)
 * removes ~half of these. POW2 variant uses bitmask `and` for base; non-POW2
 * uses the 128-bit reciprocal multiply.
 *
 * No OOB. Volatile stop the optimizer from collapsing the workload.
 */
#include <stdio.h>

#define N      4096
#define ITERS  3000000

int main(void) {
    int a[N];
    volatile int salt = 0xC0FFEE;
    long long sum = 0;
    for (int t = 0; t < ITERS; t++) {
        int s = salt + t;
        for (int i = 0; i < N; i++) a[i] = (i ^ s) + (i & 7);
        for (int i = 0; i < N; i++) sum += a[i];
    }
    printf("%lld\n", sum);
    return 0;
}
