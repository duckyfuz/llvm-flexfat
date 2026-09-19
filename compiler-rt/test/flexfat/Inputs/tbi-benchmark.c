#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <sys/resource.h>
static volatile unsigned long sink;
__attribute__((noinline)) static void consume(void *p) { asm volatile("" : : "r"(p) : "memory"); }
int main(int argc, char **argv) {
  struct timespec start, end; clock_gettime(CLOCK_MONOTONIC, &start);
  if (argc > 1) {
    volatile unsigned long *p = malloc(4096);
    *p = 1;
    for (unsigned i = 0; i < 10000000; ++i) sink += *p;
    free((void *)p);
  } else {
    // Keep enough slots live to expose resident metadata cost.
    void **p = malloc(200000 * sizeof(void *));
    for (unsigned k = 0; k < 10; ++k) {
      for (unsigned i = 0; i < 200000; ++i) { p[i] = malloc(32); consume(p[i]); }
      for (unsigned i = 0; i < 200000; ++i) free(p[i]);
    }
    free(p);
  }
  clock_gettime(CLOCK_MONOTONIC, &end);
  struct rusage usage; getrusage(RUSAGE_SELF, &usage);
  printf("%.6f %ld\n", end.tv_sec-start.tv_sec+(end.tv_nsec-start.tv_nsec)*1e-9, usage.ru_maxrss);
}
