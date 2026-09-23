#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern void *__real_dlsym(void *, const char *);
static unsigned bootstrap_allocations;
// This source is compiled without instrumentation: it deliberately runs while
// the runtime is resolving interceptors and has not published readiness.
void *__wrap_dlsym(void *handle, const char *name) {
  char *p = malloc(16);
  memset(p, 7, 16);
  if (!((uintptr_t)p >> 56)) ++bootstrap_allocations;
  void *result = __real_dlsym(handle, name);
  free(p);
  return result;
}
int main(void) { assert(bootstrap_allocations > 0); }
