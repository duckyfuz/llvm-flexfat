/*
 * linked_list.c — heap node alloc + pointer-chasing traversal.
 *
 * Phase 1 grows a 1M-node singly-linked list (malloc per node, every node
 * lowfatified). Phase 2 traverses it repeatedly. Hits both the allocator
 * hot path AND the load-side bounds check on every `->next` (Unit 7 + Unit 8
 * lattice: arg `head` has input bounds [0,0], so the load runs the runtime
 * check). The traversal is the dominant cost — a good test of the load-check
 * fast path's quality.
 */
#include <stdio.h>
#include <stdlib.h>

struct node {
    struct node *next;
    long v;
};

#define COUNT  1000000
#define TRIALS 250

int main(void) {
    struct node *head = NULL;
    for (long i = 0; i < COUNT; i++) {
        struct node *n = malloc(sizeof(*n));
        if (!n) return 1;
        n->next = head;
        n->v = i;
        head = n;
    }
    long sum = 0;
    for (int t = 0; t < TRIALS; t++) {
        long s = 0;
        for (struct node *p = head; p; p = p->next) s += p->v;
        sum ^= s;
    }
    printf("%ld\n", sum);
    return 0;
}
