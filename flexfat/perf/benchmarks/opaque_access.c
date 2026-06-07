/*
 * opaque_access.c — runtime-opaque pointer accesses to defeat Unit-8 static
 * bounds elision.
 *
 * Each access goes through an opaque function (`load` / `store` declared
 * external, defined here noinline) so calcBasePtr cannot trace from the
 * access back to the alloca/malloc. Unit-8's lattice gives the arg input
 * bounds [0,0]; the load+store both fall through to a runtime check. This
 * is the closest thing in this corpus to the "tight loop over an opaque
 * pointer" pattern SPEC2006 has lots of, and the most direct test of the
 * inlined fast-path codegen (shr/table-load/cmpq/jae).
 */
#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) static void store_at(int *p, int idx, int v) {
    p[idx] = v;
}
__attribute__((noinline)) static int load_at(const int *p, int idx) {
    return p[idx];
}

#define N      4096
#define ITERS  300000

int main(void) {
    int *a = malloc(N * sizeof(*a));
    if (!a) return 1;
    long long sum = 0;
    for (int t = 0; t < ITERS; t++) {
        for (int i = 0; i < N; i++) store_at(a, i, (i ^ t) + (i & 7));
        for (int i = 0; i < N; i++) sum += load_at(a, i);
    }
    printf("%lld\n", sum);
    free(a);
    return 0;
}
