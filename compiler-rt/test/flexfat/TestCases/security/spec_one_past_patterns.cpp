// RUN: %clangxx_flexfat -O3 %s -o %t && %run %t callee
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t subtraction && %run %t arena && %run %t deque && %run %t loop && %run %t callee

#include <cstddef>
#include <cstdlib>
#include <cstring>

static char *volatile ArenaCursor;

__attribute__((noinline)) static std::ptrdiff_t subtractFrom(char *end,
                                                             char *begin) {
  asm volatile("" : "+r"(end) : : "memory");
  return end - begin;
}

__attribute__((noinline)) static void consume(char *p) {
  asm volatile("" : : "r"(p) : "memory");
}

__attribute__((noinline)) static void consumeEnd(char *base) {
  consume(base + 16);
}

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  size_t size = 16;
  char *base = static_cast<char *>(std::malloc(size));
  if (!base)
    return 1;
  char *end = base + size;
  if (std::strcmp(argv[1], "callee") == 0) {
    consumeEnd(base);
    return 0;
  }
  if (std::strcmp(argv[1], "subtraction") == 0)
    return subtractFrom(end, base) != static_cast<std::ptrdiff_t>(size);
  if (std::strcmp(argv[1], "arena") == 0) {
    ArenaCursor = end;
    return 0;
  }
  if (std::strcmp(argv[1], "deque") == 0) {
    consume(end);
    return 0;
  }
  if (std::strcmp(argv[1], "loop") == 0) {
    for (char *cursor = base; cursor != end; ++cursor)
      asm volatile("" : : "r"(cursor));
    consume(end);
    return 0;
  }
  return 3;
}
