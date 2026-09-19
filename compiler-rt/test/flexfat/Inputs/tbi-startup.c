#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static char *bootstrap;
static void early(void) {
  bootstrap = malloc(16);
  assert(bootstrap && !((uintptr_t)bootstrap >> 56));
  memset(bootstrap, 42, 16);
  void *p = 0; assert(!posix_memalign(&p, 64, 16)); free(p);
}
__attribute__((section(".preinit_array"), used)) static void (*pre)(void) = early;
int main(void) { assert(bootstrap[0] == 42); free(bootstrap); }
