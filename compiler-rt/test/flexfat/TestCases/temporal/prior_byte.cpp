// A slot's generation lives at base - 1, in the reserved final byte of the
// preceding slot. The first aligned slot of each region is never allocated.
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "flexfat/flexfat_config.h"

extern "C" uintptr_t __flexfat_get_base(uintptr_t);
extern "C" uintptr_t __flexfat_get_size(uintptr_t);
extern "C" uintptr_t __flexfat_get_usable_size(uintptr_t);
extern "C" void *__interceptor_memset(void *, int, size_t);

__attribute__((noinline, disable_sanitizer_instrumentation))
static unsigned raw_byte(uintptr_t address) {
  return *(volatile unsigned char *)address;
}

__attribute__((noinline)) static char *opaque(char *p) {
  asm volatile("" : "+r"(p) : : "memory");
  return p;
}

int main(int argc, char **argv) {
  char *p = opaque((char *)malloc(15));
  char *q = opaque((char *)malloc(15));
  assert(p && q);
  uintptr_t raw_p = (uintptr_t)p & 0x00ffffffffffffffULL;
  uintptr_t raw_q = (uintptr_t)q & 0x00ffffffffffffffULL;
  uintptr_t base_p = __flexfat_get_base((uintptr_t)p) & 0x00ffffffffffffffULL;
  uintptr_t base_q = __flexfat_get_base((uintptr_t)q) & 0x00ffffffffffffffULL;
  uintptr_t size = __flexfat_get_size((uintptr_t)p);
  assert(size == 16 && raw_p == base_p && raw_q == base_q);
  assert(__flexfat_get_usable_size((uintptr_t)p) == size - 1);
  uintptr_t region = __flexfat::GetRegionStart(__flexfat::GetRegionIndex(raw_p));
  assert(base_p >= region + size && base_p - 1 >= region);
  assert(base_q == base_p + size);
  // The first region uses a guard page; later regions use the previous
  // region's reserved final byte. All are safe zero-tag locations.
  for (uintptr_t i = 0; i < __flexfat::kNumSizeClasses; ++i)
    assert(raw_byte(__flexfat::GetRegionStart(i) - 1) == 0);
  assert(raw_byte(base_p - 1) == ((uintptr_t)p >> 56));
  assert(raw_byte(base_q - 1) == ((uintptr_t)q >> 56));

  if (argc > 1 && !strcmp(argv[1], "tag-byte"))
    return *(volatile char *)opaque(p + size - 1);
  if (argc > 1 && !strcmp(argv[1], "adjacent-tag"))
    return *(volatile char *)opaque((char *)((uintptr_t)q - 1));
  if (argc > 1 && !strcmp(argv[1], "memset")) {
    __interceptor_memset(p, 0, size);
    return 0;
  }

  char *one_past = opaque(p + 15);
  assert(((uintptr_t)one_past & 0x00ffffffffffffffULL) == base_p + 15);
  unsigned old_tag = (uintptr_t)p >> 56;
  free(p);
  assert(raw_byte(base_p - 1) != old_tag);
  assert(raw_byte(base_q - 1) == ((uintptr_t)q >> 56));
  *q = 7;
  assert(*q == 7);
  char *again = opaque((char *)malloc(15));
  assert(((__flexfat_get_base((uintptr_t)again)) & 0x00ffffffffffffffULL) ==
         base_p);
  assert(raw_byte(base_p - 1) == ((uintptr_t)again >> 56));
  free(q);
  free(again);
}
