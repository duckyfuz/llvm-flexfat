//===-- lowfat.c - FlexFat runtime: encoding, init, allocator, reporter ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// FlexFat runtime core — a focused port of the reference LowFat runtime.
//   Unit 3: pointer-encoding accessors (lowfat.h), the SIZES/MAGICS tables,
//           region reservation, constructor/preinit.
//   Unit 4: the per-size-class bump+freelist allocator (lowfat_malloc.c).
//   Unit 5: pointer classification, the memops (lowfat_memops.c), and the OOB
//           reporter. The reporter formatting (banner, "LOWFAT ERROR:" text,
//           the field layout, ANSI coloring) is a VERBATIM port — its uncolored
//           output is byte-identical to the reference, which the e2e CHECKs and
//           the MSET differential depend on.
//
// Exit convention: lowfat_oob_error -> lowfat_error -> abort() (SIGABRT / 6),
// matching the reference and the MSET lowfat_original/lowfat configs. See
// docs/STATUS.md for the exit-code decision.
//
//===----------------------------------------------------------------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

#define LOWFAT_PAGE_SIZE   4096
#define LOWFAT_MAX_ADDRESS 0x1000000000000ull // 2^48
#define LOWFAT_CONSTRUCTOR __attribute__((__constructor__(10102)))
#define LOWFAT_NOINLINE    __attribute__((__noinline__))
#define LOWFAT_NORETURN    __attribute__((__noreturn__))
#define LOWFAT_CONST       __attribute__((__const__))
#define LOWFAT_DATA        /* EMPTY */
#define LOWFAT_ALIAS(name) __attribute__((__alias__(name)))
#define LOWFAT_CPUID(a, c, ax, bx, cx, dx)                                     \
  __asm__ __volatile__("cpuid"                                                 \
                       : "=a"(ax), "=b"(bx), "=c"(cx), "=d"(dx)                \
                       : "a"(a), "c"(c))

#define LOWFAT_SIZES  _LOWFAT_SIZES
#define LOWFAT_MAGICS _LOWFAT_MAGICS

// Unit 17: angle-bracket include so the build picks the variant-selected copy
// from the build dir (configure_file'd from flexfat/config/golden/{pow2,
// nonpow2}/lowfat_config.c by compiler-rt/lib/flexfat/CMakeLists.txt). The
// quote-include "lowfat_config.c" form would unconditionally resolve to the
// committed nonpow2 source-dir copy and defeat LLVM_FLEXFAT_POW2.
#include <lowfat_config.c>
#include "lowfat.h"

static bool lowfat_malloc_inited = false;

//===----------------------------------------------------------------------===//
// Low-level helpers.
//===----------------------------------------------------------------------===//

// Per-region lock (the default reference build uses a pthread mutex).
typedef pthread_mutex_t lowfat_mutex_t;
static inline bool lowfat_mutex_init(lowfat_mutex_t *m) {
  return (pthread_mutex_init(m, NULL) == 0);
}
static inline void lowfat_mutex_lock(lowfat_mutex_t *m) { pthread_mutex_lock(m); }
static inline void lowfat_mutex_unlock(lowfat_mutex_t *m) {
  pthread_mutex_unlock(m);
}

static void lowfat_rand(void *buf, size_t len) {
  uint8_t *p = (uint8_t *)buf;
  while (len > 0) {
    ssize_t n = getrandom(p, len, 0);
    if (n <= 0) {
      memset(p, 0, len);
      return;
    }
    p += n;
    len -= (size_t)n;
  }
}

static void lowfat_dont_need(void *ptr, size_t size) {
  madvise(ptr, size, MADV_DONTNEED);
}

// `fd >= 0` swaps MAP_PRIVATE|MAP_ANONYMOUS for MAP_SHARED — the SHM-aliasing
// trick the stack regions depend on (same physical bytes at every size-class
// region's stack sub-range). MAP_FIXED_NOREPLACE keeps the "detect a stray
// mapping" behavior at our fixed addresses.
static void *lowfat_map(void *addr, size_t len, bool read, bool write, int fd) {
  int prot = (read ? PROT_READ : 0) | (write ? PROT_WRITE : 0);
  int flags = MAP_NORESERVE;
  if (fd >= 0)
    flags |= MAP_SHARED;
  else
    flags |= MAP_PRIVATE | MAP_ANONYMOUS;
  if (addr != NULL)
    flags |= MAP_FIXED_NOREPLACE;
  return mmap(addr, len, prot, flags, fd, 0);
}

// Anonymous, unlinked /dev/shm object — the fd MAP_SHARED-aliases identical
// physical bytes at every VA it is mmap'd to. Port of the reference's
// lowfat_create_shm (lowfat_linux.c:80-107): O_EXCL temp, unlink, F_SETLEASE
// to fail loud if the path is somehow shared, ftruncate to the requested size.
// Path bytes come from lowfat_rand for collision avoidance.
int lowfat_create_shm(size_t size) {
  char path[] = "/dev/shm/flexfat.XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX.tmp";
  for (size_t i = 0; i < sizeof(path) - 2; i++) {
    if (path[i] != 'X' || path[i + 1] != 'X')
      continue;
    const char *xdigs = "0123456789ABCDEF";
    uint8_t rbyte;
    lowfat_rand(&rbyte, sizeof(rbyte));
    path[i++] = xdigs[rbyte & 0x0F];
    path[i] = xdigs[(rbyte >> 4) & 0x0F];
  }
  int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0);
  if (fd < 0)
    return -1;
  if (unlink(path) < 0) {
    close(fd);
    return -1;
  }
  if (fcntl(fd, F_SETLEASE, F_WRLCK) < 0) {
    close(fd);
    return -1;
  }
  if (ftruncate(fd, (off_t)size) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static bool lowfat_protect(void *addr, size_t len, bool read, bool write) {
  int prot = (read ? PROT_READ : 0) | (write ? PROT_WRITE : 0);
  return (mprotect(addr, len, prot) == 0);
}

static LOWFAT_CONST void *lowfat_region(size_t idx) {
  return (void *)(idx * LOWFAT_REGION_SIZE);
}

// ANSI color only on a TTY (verbatim; keeps non-TTY output byte-identical).
static LOWFAT_NOINLINE const char *lowfat_color_escape_code(FILE *stream,
                                                            bool red) {
  int err = errno;
  int r = isatty(fileno(stream));
  errno = err;
  if (!r)
    return "";
  else
    return (red ? "\33[31m" : "\33[0m");
}

static LOWFAT_NOINLINE void lowfat_backtrace(void) {
  size_t MAX_TRACE = 256;
  void *trace[MAX_TRACE];
  int len = backtrace(trace, sizeof(trace) / sizeof(void *));
  char **trace_strs = backtrace_symbols(trace, len);
  for (int i = 0; i < len; i++)
    fprintf(stderr, "%d: %s\n", i, trace_strs[i]);
  if (len == 0 || len == (int)(sizeof(trace) / sizeof(void *)))
    fprintf(stderr, "...\n");
}

//===----------------------------------------------------------------------===//
// Pointer classification (verbatim; SPEC §5.4).
//===----------------------------------------------------------------------===//

LOWFAT_CONST bool lowfat_is_ptr(const void *ptr) {
  size_t idx = lowfat_index(ptr);
  return (idx - 1) <= LOWFAT_NUM_REGIONS;
}

LOWFAT_CONST bool lowfat_is_stack_ptr(const void *ptr) {
  size_t idx = lowfat_index(ptr);
  uintptr_t stack_end = (uintptr_t)lowfat_region(idx) +
                        LOWFAT_STACK_MEMORY_OFFSET + LOWFAT_STACK_MEMORY_SIZE;
  return lowfat_is_ptr(ptr) &&
         ((stack_end - (uintptr_t)ptr) <= LOWFAT_STACK_MEMORY_SIZE);
}

LOWFAT_CONST bool lowfat_is_global_ptr(const void *ptr) {
  size_t idx = lowfat_index(ptr);
  uintptr_t global_end = (uintptr_t)lowfat_region(idx) +
                         LOWFAT_GLOBAL_MEMORY_OFFSET + LOWFAT_GLOBAL_MEMORY_SIZE;
  return lowfat_is_ptr(ptr) &&
         ((global_end - (uintptr_t)ptr) <= LOWFAT_GLOBAL_MEMORY_SIZE);
}

LOWFAT_CONST bool lowfat_is_heap_ptr(const void *ptr) {
  size_t idx = lowfat_index(ptr);
  uintptr_t heap_end = (uintptr_t)lowfat_region(idx) + LOWFAT_HEAP_MEMORY_OFFSET +
                       LOWFAT_HEAP_MEMORY_SIZE;
  return lowfat_is_ptr(ptr) &&
         ((heap_end - (uintptr_t)ptr) <= LOWFAT_HEAP_MEMORY_SIZE);
}

//===----------------------------------------------------------------------===//
// Error / warning reporting (VERBATIM port — output is byte-identical).
//===----------------------------------------------------------------------===//

static size_t lowfat_num_messages = 0;
static pthread_mutex_t lowfat_print_mutex = PTHREAD_MUTEX_INITIALIZER;

static LOWFAT_NOINLINE void lowfat_print_banner(void) {
  fprintf(stderr,
          "%s"
          "_|                                      _|_|_|_|            _|\n"
          "_|          _|_|    _|      _|      _|  _|        _|_|_|  _|_|_|_|\n"
          "_|        _|    _|  _|      _|      _|  _|_|_|  _|    _|    _|\n"
          "_|        _|    _|    _|  _|  _|  _|    _|      _|    _|    _|\n"
          "_|_|_|_|    _|_|        _|      _|      _|        _|_|_|      _|_|%s\n"
          "\n",
          lowfat_color_escape_code(stderr, true),
          lowfat_color_escape_code(stderr, false));
}

static LOWFAT_NOINLINE void lowfat_message(const char *format, bool err,
                                           va_list ap) {
  lowfat_mutex_lock(&lowfat_print_mutex);

  // (1) Print the error:
  lowfat_print_banner();
  fprintf(stderr, "%sLOWFAT %s%s: ", lowfat_color_escape_code(stderr, true),
          (err ? "ERROR" : "WARNING"), lowfat_color_escape_code(stderr, false));
  vfprintf(stderr, format, ap);
  fputc('\n', stderr);

  // (2) Dump the stack:
  if (lowfat_malloc_inited)
    lowfat_backtrace();

  lowfat_num_messages++;
  lowfat_mutex_unlock(&lowfat_print_mutex);
}

LOWFAT_NOINLINE LOWFAT_NORETURN void lowfat_error(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  lowfat_message(format, /*err=*/true, ap);
  va_end(ap);
  abort();
}

LOWFAT_NOINLINE void lowfat_warning(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  lowfat_message(format, /*err=*/false, ap);
  va_end(ap);
}

size_t lowfat_get_num_errors(void) { return lowfat_num_messages; }

static LOWFAT_NOINLINE const char *lowfat_kind(const void *ptr) {
  if (!lowfat_is_ptr(ptr))
    return "nonfat";
  if (lowfat_is_heap_ptr(ptr))
    return "heap";
  if (lowfat_is_stack_ptr(ptr))
    return "stack";
  if (lowfat_is_global_ptr(ptr))
    return "global";
  return "unused";
}

static LOWFAT_NOINLINE const char *lowfat_error_kind(unsigned info) {
  switch (info) {
  case LOWFAT_OOB_ERROR_READ:
    return "read";
  case LOWFAT_OOB_ERROR_WRITE:
    return "write";
  case LOWFAT_OOB_ERROR_MEMCPY:
    return "memcpy";
  case LOWFAT_OOB_ERROR_MEMSET:
    return "memset";
  case LOWFAT_OOB_ERROR_ESCAPE_CALL:
    return "escape (call)";
  case LOWFAT_OOB_ERROR_ESCAPE_RETURN:
    return "escape (return)";
  case LOWFAT_OOB_ERROR_ESCAPE_STORE:
    return "escape (store)";
  case LOWFAT_OOB_ERROR_ESCAPE_PTR2INT:
    return "escape (ptr2int)";
  case LOWFAT_OOB_ERROR_ESCAPE_INSERT:
    return "escape (insert)";
  default:
    return "unknown";
  }
}

LOWFAT_NORETURN void lowfat_oob_error(unsigned info, const void *ptr,
                                      const void *baseptr) {
  const char *kind = lowfat_error_kind(info);
  ssize_t overflow = (ssize_t)ptr - (ssize_t)baseptr;
  if (overflow > 0)
    overflow -= lowfat_size(baseptr);
  lowfat_error("out-of-bounds error detected!\n"
               "\toperation = %s\n"
               "\tpointer   = %p (%s)\n"
               "\tbase      = %p\n"
               "\tsize      = %zu\n"
               "\t%s = %+zd\n",
               kind, ptr, lowfat_kind(ptr), baseptr, lowfat_size(baseptr),
               (overflow < 0 ? "underflow" : "overflow "), overflow);
}

void lowfat_oob_warning(unsigned info, const void *ptr, const void *baseptr) {
  const char *kind = lowfat_error_kind(info);
  ssize_t overflow = (ssize_t)ptr - (ssize_t)baseptr;
  if (overflow > 0)
    overflow -= lowfat_size(baseptr);
  lowfat_warning("out-of-bounds error detected!\n"
                 "\toperation = %s\n"
                 "\tpointer   = %p (%s)\n"
                 "\tbase      = %p\n"
                 "\tsize      = %zu\n"
                 "\t%s = %+zd\n",
                 kind, ptr, lowfat_kind(ptr), baseptr, lowfat_size(baseptr),
                 (overflow < 0 ? "underflow" : "overflow "), overflow);
}

void lowfat_oob_check(unsigned info, const void *ptr, size_t size0,
                      const void *baseptr) {
  size_t size = lowfat_size(baseptr);
  size_t diff = (size_t)((const uint8_t *)ptr - (const uint8_t *)baseptr);
  size -= size0;
  if (diff >= size)
    lowfat_oob_error(info, ptr, baseptr);
}

//===----------------------------------------------------------------------===//
// The allocator and bounds-checked memops (#included as part of this TU).
//===----------------------------------------------------------------------===//

#include "lowfat_malloc.c"
#include "lowfat_memops.c"

//===----------------------------------------------------------------------===//
// Stack region machinery (Unit 12a): SHM-aliased stack regions, bump
// allocator for thread stacks, and the pivot trampoline + helper.
//===----------------------------------------------------------------------===//

#define LOWFAT_STACK_GUARD        (32 * LOWFAT_PAGE_SIZE)
#define LOWFAT_NUM_THREAD_STACKS  (LOWFAT_STACK_MEMORY_SIZE / LOWFAT_STACK_SIZE)
#define LOWFAT_STACKS_START                                                    \
  ((void *)((LOWFAT_STACK_REGION * LOWFAT_REGION_SIZE) +                       \
            LOWFAT_STACK_MEMORY_OFFSET))
#define LOWFAT_STACK_BASE(ptr)                                                 \
  ((void *)((const uint8_t *)(ptr) -                                           \
            ((uintptr_t)(ptr) % LOWFAT_STACK_SIZE)))

// Saved by lowfat_preinit; lowfat_stack_pivot_2 walks back from this to find
// the high end of the initial native stack (envp lives at the base).
static LOWFAT_DATA char **lowfat_envp = NULL;

static LOWFAT_DATA size_t lowfat_stack_freeidx = 0;
static LOWFAT_DATA lowfat_mutex_t lowfat_stack_mutex;

// Unit 14a: Fisher-Yates shuffle of slot indices. Initialized in
// lowfat_init from lowfat_rand so the order in which pthread_create
// consumes the 128 slots is unpredictable to an attacker who knows the
// region scheme. Non-static so the gtest can verify the permutation
// property (every value in [0, 128) appears exactly once).
uint16_t lowfat_stack_perm[LOWFAT_NUM_THREAD_STACKS] = {0};

// Unit 14a: freelist of slots whose previous owner thread has died (its
// pthread_t descriptor is still readable because the descriptor lives on
// the lowfat stack the thread used). lowfat_stack_alloc walks this list
// first; lowfat_is_thread_dead reads the TID/JOINID offsets to decide
// reclaimability. The build gate from this same unit pins those offsets
// against the host glibc.
struct lowfat_stack_freelist_s {
  pthread_t thread;
  struct lowfat_stack_freelist_s *next;
};
static LOWFAT_DATA struct lowfat_stack_freelist_s *lowfat_stack_freelist = NULL;

// LowFat.cpp:1138-1151 — read the kernel-managed TID and the glibc-managed
// joinid at the offsets the build gate validated. Two final states are
// "definitely dead": tid == -1 (the join() final-clear sentinel) or
// tid == 0 && joinid == thread (detached thread's self-marker). Anything
// else is alive-or-zombie; the caller skips this freelist entry.
static bool lowfat_is_thread_dead(pthread_t thread) {
  pid_t *tid_ptr = (pid_t *)((uint8_t *)thread + LOWFAT_TID_OFFSET);
  pthread_t *joinid_ptr =
      (pthread_t *)((uint8_t *)thread + LOWFAT_JOINID_OFFSET);
  if (*tid_ptr > 0)
    return false;        // still active
  else if (*tid_ptr != 0)
    return true;         // dead + joined (tid = -1)
  else if (*joinid_ptr == thread)
    return true;         // dead + detached (joinid = thread)
  else
    return false;        // zombie waiting to be joined
}

// LowFat.cpp:1152-1156 — used by lowfat_force_stack_free to synthesize a
// definitely-dead pthread_t for the failure-recovery freelist push.
static void lowfat_force_thread_dead(pthread_t thread) {
  pid_t *tid_ptr = (pid_t *)((uint8_t *)thread + LOWFAT_TID_OFFSET);
  *tid_ptr = -1;
}

// Allocate a master-stack slot, mprotect read+write across every mirror.
// Walks the freelist first (reclaiming any dead-thread slot) before
// bump-allocating via the Fisher-Yates permutation. The pivot's first
// call returns the first permuted slot.
void *lowfat_stack_alloc(void) {
  lowfat_mutex_lock(&lowfat_stack_mutex);

  // STEP (1): freelist walk — first dead-thread entry wins.
  struct lowfat_stack_freelist_s *prev = NULL;
  struct lowfat_stack_freelist_s *curr = lowfat_stack_freelist;
  while (curr != NULL) {
    if (lowfat_is_thread_dead(curr->thread)) {
      if (prev != NULL)
        prev->next = curr->next;
      else
        lowfat_stack_freelist = curr->next;
      uint8_t *stack = (uint8_t *)LOWFAT_STACK_BASE(curr);
      lowfat_mutex_unlock(&lowfat_stack_mutex);
      return stack;
    }
    prev = curr;
    curr = curr->next;
  }

  // STEP (2): bump-allocate via the Fisher-Yates permutation.
  if (lowfat_stack_freeidx >= LOWFAT_NUM_THREAD_STACKS) {
    lowfat_mutex_unlock(&lowfat_stack_mutex);
    errno = ENOMEM;
    return NULL;
  }
  size_t stack_idx = lowfat_stack_perm[lowfat_stack_freeidx++];
  lowfat_mutex_unlock(&lowfat_stack_mutex);

  uint8_t *stack = (uint8_t *)LOWFAT_STACKS_START + stack_idx * LOWFAT_STACK_SIZE;
  uint8_t *stack_lo = stack + LOWFAT_STACK_GUARD;
  uint8_t *stack_hi = stack + LOWFAT_STACK_SIZE;
  size_t idx;
  for (size_t i = 0; (idx = lowfat_stacks[i]) != 0; i++) {
    ptrdiff_t diff = (uint8_t *)lowfat_region(LOWFAT_STACK_REGION) -
                     (uint8_t *)lowfat_region(idx);
    if (mprotect(stack_lo - diff, stack_hi - stack_lo,
                 PROT_READ | PROT_WRITE) != 0)
      return NULL;
  }
  return stack;
}

// Add `thread`'s stack to the reclamation freelist. The node lives in the
// last sizeof(node) bytes of the slot itself (no separate allocation).
// Idempotent: the slot stays "in use" until lowfat_stack_alloc's freelist
// walk sees lowfat_is_thread_dead(thread) == true.
static void lowfat_stack_free(pthread_t thread) {
  uint8_t *nptr = (uint8_t *)LOWFAT_STACK_BASE(thread);
  nptr += LOWFAT_STACK_SIZE - sizeof(struct lowfat_stack_freelist_s);
  struct lowfat_stack_freelist_s *node =
      (struct lowfat_stack_freelist_s *)nptr;
  node->thread = thread;
  lowfat_mutex_lock(&lowfat_stack_mutex);
  node->next = lowfat_stack_freelist;
  lowfat_stack_freelist = node;
  lowfat_mutex_unlock(&lowfat_stack_mutex);
}

// LowFat.cpp:1235-1245 — recovery path when the real pthread_create
// failed AFTER we allocated a stack: synthesize a fake "already-dead"
// pthread_t at the top of the slot, force its tid to -1, and push it to
// the freelist so the slot is immediately reclaimable. Non-static so the
// gtest can simulate dead-thread reclamation without spawning a real
// pthread (the test that would have caught a wrong TID_OFFSET as silent
// corruption, now doubly defended by the build gate).
void lowfat_force_stack_free(void *stack) {
  uint8_t *ptr = (uint8_t *)LOWFAT_STACK_BASE(stack);
  ptr += LOWFAT_STACK_SIZE - LOWFAT_PAGE_SIZE;
  pthread_t fake_thread = (pthread_t)ptr;
  lowfat_force_thread_dead(fake_thread);
  lowfat_stack_free(fake_thread);
}

// The pivot's payload (port of lowfat.c:524-575): walk envp to find the high
// end of the initial native stack, allocate a low-fat stack via
// lowfat_stack_alloc, memcpy the live range onto it, then rewrite any
// self-referential pointers that point back into the old range. Returns the
// new top-of-stack (low address) to the asm trampoline below, which switches
// %rsp to it.
extern LOWFAT_NOINLINE void *lowfat_stack_pivot_2(void *stack_top) {
  if (lowfat_envp == NULL) {
    fprintf(stderr, "FlexFat: pivot called without envp\n");
    abort();
  }
  char **envp = lowfat_envp;
  lowfat_envp = NULL;
  uint8_t *stack_bottom = (void *)envp;
  while (*envp != NULL) {
    char *var = *envp;
    uint8_t *end = (uint8_t *)(var + strlen(var) + 1);
    stack_bottom = (stack_bottom < end ? end : stack_bottom);
    envp++;
  }
  stack_bottom = (stack_bottom < (uint8_t *)envp ? (uint8_t *)envp : stack_bottom);
  if (((uintptr_t)stack_bottom % LOWFAT_PAGE_SIZE) != 0)
    stack_bottom = stack_bottom +
                   (LOWFAT_PAGE_SIZE - (uintptr_t)stack_bottom % LOWFAT_PAGE_SIZE);

  size_t size = stack_bottom - (uint8_t *)stack_top;
  uint8_t *stack_base = (uint8_t *)lowfat_stack_alloc();
  if (stack_base == NULL)
    lowfat_error("failed to allocate stack: %s", strerror(errno));
  stack_base += LOWFAT_STACK_SIZE;
  memcpy(stack_base - size, stack_top, size);

  // Patch any words on the new stack that look like pointers into the OLD
  // stack range (saved %rbp, captured &local in temporaries, etc.) so they
  // refer to the new range instead.
  void *old_stack_lo = stack_top, *old_stack_hi = stack_bottom;
  void **new_stack_lo = (void **)(stack_base - size),
       **new_stack_hi = (void **)stack_base;
  for (void **pptr = new_stack_lo; pptr < new_stack_hi; pptr++) {
    void *ptr = *pptr;
    if (ptr >= old_stack_lo && ptr <= old_stack_hi) {
      ssize_t diff = ((uint8_t *)ptr - (uint8_t *)old_stack_lo);
      void *new_ptr = (uint8_t *)new_stack_lo + diff;
      *pptr = new_ptr;
    }
  }

  return stack_base - size;
}

// Tiny asm trampoline (verbatim port of lowfat.c:577-586): stash %rsp into
// %rdi (lowfat_stack_pivot_2's first arg), call the helper through %rax to
// stay legal under -mcmodel=large, move the returned new-stack-top into %rsp,
// then `ret` jumps to the copied-over return address now sitting on the new
// stack — first instruction after the pivot call executes on the low-fat stack.
extern LOWFAT_NOINLINE void lowfat_stack_pivot(void);
__asm__(
    "\t.align 16, 0x90\n"
    "\t.type lowfat_stack_pivot,@function\n"
    "lowfat_stack_pivot:\n"
    "\tmovq %rsp, %rdi\n"
    "\tmovabsq $lowfat_stack_pivot_2, %rax\n"
    "\tcallq *%rax\n"
    "\tmovq %rax, %rsp\n"
    "\tretq\n");

//===----------------------------------------------------------------------===//
// Unit 14a: pthread_create interposer.
//
// Modern glibc (post-2.34) folded libpthread into libc.so.6 — the symbol
// is still pthread_create with the same signature, and dlsym(RTLD_NEXT,
// "pthread_create") finds it. The reference's LowFat targeted glibc 2.27
// where libpthread was still separate; the only material difference for
// us is that the dlsym lookup walks the libc image instead of a separate
// libpthread image, which is opaque to user code (sname/signature
// unchanged) and works identically. Flagged here so future-us doesn't
// chase a non-issue.
//===----------------------------------------------------------------------===//

#ifndef LOWFAT_NO_REPLACE_PTHREAD_CREATE

typedef int (*pthread_create_t)(pthread_t *, const pthread_attr_t *,
                                void *(*)(void *), void *);

extern int lowfat_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                 void *(*start_routine)(void *), void *arg)
    LOWFAT_ALIAS("pthread_create");

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
  static pthread_create_t real_pthread_create = NULL;
  if (real_pthread_create == NULL) {
    real_pthread_create =
        (pthread_create_t)dlsym(RTLD_NEXT, "pthread_create");
    if (real_pthread_create == NULL || real_pthread_create == pthread_create)
      lowfat_error("failed to find real pthread_create");
  }

  // Honor the caller's attributes EXCEPT the stack — we replace it with a
  // lowfat slot. If the user supplied a custom stack, warn (their pointer
  // would not be a lowfat address) but proceed with ours.
  pthread_attr_t newattr;
  int err;
  if (attr != NULL) {
    void *user_stack = NULL;
    size_t user_size = 0;
    err = pthread_attr_getstack(attr, &user_stack, &user_size);
    if (err == 0 && (user_stack != NULL || user_size != 0))
      lowfat_warning(
          "custom pthread stack will be replaced with a lowfat stack");
    memcpy(&newattr, attr, sizeof(newattr));
  } else {
    err = pthread_attr_init(&newattr);
    if (err != 0)
      lowfat_error("pthread_attr_init failed: %s", strerror(err));
  }

  void *stack = lowfat_stack_alloc();
  if (stack == NULL)
    lowfat_error("failed to allocate stack for new thread");
  size_t stack_size =
      LOWFAT_STACK_SIZE - sizeof(struct lowfat_stack_freelist_s);

  err = pthread_attr_setstack(&newattr, stack, stack_size);
  if (err != 0)
    lowfat_error("pthread_attr_setstack failed: %s", strerror(err));

  err = real_pthread_create(thread, &newattr, start_routine, arg);
  if (err != 0) {
    // pthread_create failed AFTER we allocated the slot — recover the
    // slot via lowfat_force_stack_free (synthesizes a dead pthread_t at
    // the top of the slot so the next lowfat_stack_alloc walk reclaims).
    lowfat_force_stack_free(stack);
    return err;
  }
  // Push the slot to the freelist immediately. It is NOT really free
  // until the thread terminates AND lowfat_is_thread_dead returns true.
  lowfat_stack_free(*thread);
  return 0;
}

#endif // LOWFAT_NO_REPLACE_PTHREAD_CREATE

//===----------------------------------------------------------------------===//
// Unit 14b: fork() interposer — closes the 12a MAP_SHARED stack alias.
//
// Bare fork() inherits all parent mappings; the lowfat stack regions are
// MAP_SHARED to one shm fd, so parent and child end up reading/writing
// the SAME physical stack bytes. The reference fixes this with a clone()-
// based fork: child runs on a temp stack, mmap()s the parent's stack
// region addresses MAP_SHARED|MAP_FIXED to a FRESH shm fd (which the
// parent does NOT see, because the child has its own VM), memcpy()s the
// parent's live stack content into the new shm, signals the parent, then
// longjmp()s back into the parent's `lowfat_fork()` setjmp frame on the
// child's now-private master stack.
//
// Modern glibc deviations (post-2.34 unified libc):
//   * SKIPPED: the reference's step (0) — reset lowfat_seed_pos. Our
//     lowfat_rand uses getrandom(2) directly (Unit 12a port decision);
//     there is no application-level seed pool to reset.
//   * clone() is still in <sched.h> with the same signature; no change.
//   * pthread_cond_t / pthread_mutex_t with PROCESS_SHARED still work
//     via futex syscalls. The cond var lives in the temp stack (which
//     is MAP_SHARED|MAP_ANONYMOUS), so parent and child see the same
//     memory.
//   * fork() in modern glibc takes internal locks (malloc arena, atfork);
//     by interposing at the fork symbol with a clone()-based body we
//     bypass those entirely. Atfork handlers DO NOT run — matches the
//     reference's deliberate choice. Documented in STATUS as a known
//     deviation from POSIX fork() semantics.
//   * Direct clone() callers stay UNSUPPORTED (matches the reference).
//===----------------------------------------------------------------------===//

#ifndef LOWFAT_NO_REPLACE_FORK

struct lowfat_fork_info {
  pthread_mutex_t mutex;
  pthread_cond_t condvar;
  bool done;
  void *stack;     // parent's __builtin_frame_address(0) at fork time
  jmp_buf env;
};

// Child runs here on the temp stack — see comments above for the
// step-by-step. Errors signal `done = false` then abort.
static int lowfat_fork_child_wrapper(void *arg) {
  struct lowfat_fork_info *info = (struct lowfat_fork_info *)arg;

  // STEP (1): fresh shm + remap size-class 1's stack range to it.
  int fd = lowfat_create_shm(LOWFAT_STACK_MEMORY_SIZE);
  if (fd < 0)
    goto fail_before_signal;
  size_t idx = lowfat_stacks[0];
  uint8_t *stack_lo =
      (uint8_t *)lowfat_region(idx) + LOWFAT_STACK_MEMORY_OFFSET;
  void *ptr = mmap(stack_lo, LOWFAT_STACK_MEMORY_SIZE, PROT_NONE,
                   MAP_SHARED | MAP_FIXED | MAP_NORESERVE, fd, 0);
  if (ptr != stack_lo)
    goto fail_before_signal;

  // STEP (2): mprotect+memcpy parent's live stack pages onto the fresh
  // shm via size-class 1's mirror. Because all size-class mirrors will
  // alias to the SAME shm by the end of step (3), this one copy
  // populates them all.
  uint8_t *copy_lo = (uint8_t *)LOWFAT_PAGES_BASE(info->stack);
  uint8_t *copy_hi =
      (uint8_t *)LOWFAT_STACK_BASE(info->stack) + LOWFAT_STACK_SIZE;
  ptrdiff_t offset = copy_lo - (uint8_t *)LOWFAT_STACKS_START;
  stack_lo = stack_lo + offset;
  uint8_t *stack_hi =
      (uint8_t *)LOWFAT_STACK_BASE(stack_lo) + LOWFAT_STACK_SIZE;
  uint8_t *prot_lo = stack_hi - LOWFAT_STACK_SIZE + LOWFAT_STACK_GUARD;
  if (mprotect(prot_lo, stack_hi - prot_lo, PROT_READ | PROT_WRITE) != 0)
    goto fail_before_signal;
  memcpy(stack_lo, copy_lo, copy_hi - copy_lo);

  // STEP (2a): copy is complete; wake parent.
  pthread_mutex_lock(&info->mutex);
  info->done = true;
  pthread_cond_signal(&info->condvar);
  pthread_mutex_unlock(&info->mutex);

  // STEP (3): remap every other stack region (incl. master = 62) onto
  // the same fresh shm fd. No memcpy needed — the shm content is shared.
  for (size_t i = 1; (idx = lowfat_stacks[i]) != 0; i++) {
    uint8_t *sl = (uint8_t *)lowfat_region(idx) + LOWFAT_STACK_MEMORY_OFFSET;
    void *p = mmap(sl, LOWFAT_STACK_MEMORY_SIZE, PROT_NONE,
                   MAP_SHARED | MAP_FIXED | MAP_NORESERVE, fd, 0);
    if ((uint8_t *)p != sl)
      lowfat_error("fork: failed to mmap region %zu mirror: %s",
                   idx, strerror(errno));
    sl = sl + offset;
    uint8_t *sh = (uint8_t *)LOWFAT_STACK_BASE(sl) + LOWFAT_STACK_SIZE;
    uint8_t *pl = sh - LOWFAT_STACK_SIZE + LOWFAT_STACK_GUARD;
    if (mprotect(pl, sh - pl, PROT_READ | PROT_WRITE) != 0)
      lowfat_error("fork: failed to mprotect region %zu mirror: %s",
                   idx, strerror(errno));
  }
  if (close(fd) != 0)
    lowfat_error("fork: failed to close shm fd: %s", strerror(errno));

  // STEP (4): jump back into lowfat_fork()'s setjmp frame on the now-
  // private master stack. Execution resumes at the `if (setjmp(...))`
  // branch as the child.
  longjmp(info->env, 1);
  return 0; // unreachable

fail_before_signal:
  pthread_mutex_lock(&info->mutex);
  info->done = false;
  pthread_cond_signal(&info->condvar);
  pthread_mutex_unlock(&info->mutex);
  lowfat_error("fork: child setup failed: %s", strerror(errno));
  return 0; // unreachable
}

static LOWFAT_NOINLINE pid_t lowfat_fork_wrapper(
    void *stack_tmp, size_t stack_tmp_size, struct lowfat_fork_info *info) {
  // Init the PROCESS_SHARED cond var + mutex INSIDE the parent (the temp
  // stack is MAP_SHARED|MAP_ANONYMOUS, so both processes see the same
  // physical bytes for `info`).
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&info->mutex, &mattr);
  pthread_condattr_t cattr;
  pthread_condattr_init(&cattr);
  pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
  pthread_cond_init(&info->condvar, &cattr);
  info->done = false;
  info->stack = __builtin_frame_address(0);

  // Child's %rsp at clone() — top of stack_tmp minus a little headroom
  // for the i128 / fork_info live at the high end.
  void *stack_tmp_ptr = (uint8_t *)stack_tmp + stack_tmp_size -
                        sizeof(__int128) - sizeof(struct lowfat_fork_info);

  pid_t pid =
      clone(lowfat_fork_child_wrapper, stack_tmp_ptr, SIGCHLD, info);
  pthread_mutex_lock(&info->mutex);
  while (!info->done) {
    // The reference does a single pthread_cond_wait. We loop so a
    // spurious wakeup (POSIX-permitted) doesn't drop us out early.
    if (pthread_cond_wait(&info->condvar, &info->mutex) != 0)
      break;
  }
  bool done = info->done;
  pthread_mutex_unlock(&info->mutex);

  pthread_mutex_destroy(&info->mutex);
  pthread_mutexattr_destroy(&mattr);
  pthread_cond_destroy(&info->condvar);
  pthread_condattr_destroy(&cattr);
  if (munmap(stack_tmp, stack_tmp_size) != 0)
    lowfat_error("fork: failed to munmap tmp stack: %s", strerror(errno));

  if (!done) {
    waitpid(pid, NULL, 0);
    errno = ECHILD;
    return -1;
  }
  return pid;
}

extern pid_t fork(void) LOWFAT_ALIAS("lowfat_fork");
pid_t lowfat_fork(void) {
  // 4-page MAP_SHARED|MAP_ANONYMOUS temp stack — shared so the child
  // can write the cond-var state visible to the parent (process-private
  // anon mappings would diverge after clone).
  size_t stack_tmp_size = 4 * LOWFAT_PAGE_SIZE;
  void *stack_tmp = mmap(NULL, stack_tmp_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_NORESERVE | MAP_ANONYMOUS, -1, 0);
  if (stack_tmp == MAP_FAILED)
    lowfat_error("fork: failed to allocate temp stack: %s", strerror(errno));

  struct lowfat_fork_info *info =
      (struct lowfat_fork_info *)((uint8_t *)stack_tmp + stack_tmp_size -
                                  sizeof(struct lowfat_fork_info));
  if (setjmp(info->env)) {
    // CHILD: woken up by longjmp from lowfat_fork_child_wrapper.
    if (munmap(stack_tmp, stack_tmp_size) != 0)
      lowfat_error("fork(child): failed to munmap tmp stack: %s",
                   strerror(errno));
    return 0;
  }

  // PARENT path.
  return lowfat_fork_wrapper(stack_tmp, stack_tmp_size, info);
}

#endif // LOWFAT_NO_REPLACE_FORK

//===----------------------------------------------------------------------===//
// Init: build the tables, reserve the regions, initialise the allocator.
//===----------------------------------------------------------------------===//

static LOWFAT_NOINLINE LOWFAT_NORETURN void lowfat_init_error(const char *msg) {
  fprintf(stderr, "FlexFat runtime init error: %s: %s\n", msg, strerror(errno));
  abort();
}

static bool lowfat_inited = false;

void LOWFAT_CONSTRUCTOR lowfat_init(void) {
  if (lowfat_inited)
    return;
  lowfat_inited = true;

  // Sanity checks.
  if (sizeof(void *) != sizeof(uint64_t))
    lowfat_init_error("incompatible architecture (not x86-64)");
  if (sysconf(_SC_PAGESIZE) != LOWFAT_PAGE_SIZE)
    lowfat_init_error("incompatible system page size");
#if !defined(LOWFAT_LEGACY)
  {
    uint32_t eax, ebx, ecx, edx;
    LOWFAT_CPUID(7, 0, eax, ebx, ecx, edx);
    if (((ebx >> 3) & 1) == 0 || ((ebx >> 8) & 1) == 0)
      lowfat_init_error("incompatible architecture (no BMI/BMI2)");
  }
#endif

  // SIZES/MAGICS cover the full index range:
  //   total_pages = (2^48 / REGION_SIZE) / (PAGE / sizeof(size_t)).
  size_t total_pages = (LOWFAT_MAX_ADDRESS / LOWFAT_REGION_SIZE) /
                       (LOWFAT_PAGE_SIZE / sizeof(size_t));
  size_t len = total_pages * LOWFAT_PAGE_SIZE;
  size_t entries = len / sizeof(size_t);

  size_t *sizes =
      (size_t *)lowfat_map((void *)LOWFAT_SIZES, len, true, true, -1);
  if (sizes != (size_t *)LOWFAT_SIZES)
    lowfat_init_error("failed to mmap SIZES table");
  uint64_t *magics =
      (uint64_t *)lowfat_map((void *)LOWFAT_MAGICS, len, true, true, -1);
  if (magics != (uint64_t *)LOWFAT_MAGICS)
    lowfat_init_error("failed to mmap MAGICS table");

  for (size_t i = 0; i < entries; i++) {
    sizes[i] = SIZE_MAX;
    magics[i] = 0;
  }
  size_t sizes_len = sizeof(lowfat_sizes) / sizeof(lowfat_sizes[0]);
  for (size_t j = 0; j < sizes_len; j++) {
    sizes[1 + j] = lowfat_sizes[j];
    magics[1 + j] = lowfat_magics[j];
  }

  if (!lowfat_protect((void *)LOWFAT_SIZES, len, true, false) ||
      !lowfat_protect((void *)LOWFAT_MAGICS, len, true, false))
    lowfat_init_error("failed to write-protect tables");

  // Reserve each size-class region (PROT_NONE, MAP_NORESERVE).
  for (size_t i = 1; i <= LOWFAT_NUM_REGIONS; i++) {
    void *heap_start = (uint8_t *)lowfat_region(i) + LOWFAT_HEAP_MEMORY_OFFSET;
    void *ptr =
        lowfat_map(heap_start, LOWFAT_HEAP_MEMORY_SIZE, false, false, -1);
    if (ptr != heap_start)
      lowfat_init_error("failed to reserve region");
  }

  // Initialise the allocator (ASLR start + lazy-commit bookkeeping per region).
  if (!lowfat_malloc_init())
    lowfat_init_error("failed to initialise allocator");
  lowfat_malloc_inited = true;

  // Stack regions: every entry in lowfat_stacks[] (each size-class region that
  // owns a stack sub-range, plus LOWFAT_STACK_REGION = the master region) is
  // mapped MAP_SHARED to one shm fd, so the same physical bytes are visible
  // at every mirror — the foundation for lowfat_stack_mirror's constant add.
  if (!lowfat_mutex_init(&lowfat_stack_mutex))
    lowfat_init_error("failed to init stack mutex");

  // Unit 14a: Fisher-Yates shuffle of the slot-index permutation. Seeded
  // by lowfat_rand (already used for the shm path suffix). Done before
  // the pivot so the very first slot pick (for main's lowfat stack) is
  // already shuffled — the master thread's stack address is not
  // predictable just because it's the first one allocated.
  for (size_t i = 0; i < LOWFAT_NUM_THREAD_STACKS; i++)
    lowfat_stack_perm[i] = (uint16_t)i;
  for (size_t i = LOWFAT_NUM_THREAD_STACKS - 1; i > 0; i--) {
    uint16_t j;
    lowfat_rand(&j, sizeof(j));
    j = j % (uint16_t)(i + 1);
    uint16_t tmp = lowfat_stack_perm[i];
    lowfat_stack_perm[i] = lowfat_stack_perm[j];
    lowfat_stack_perm[j] = tmp;
  }

  {
    int fd = lowfat_create_shm(LOWFAT_STACK_MEMORY_SIZE);
    if (fd < 0)
      lowfat_init_error("failed to create stack shm");
    size_t idx;
    for (size_t i = 0; (idx = lowfat_stacks[i]) != 0; i++) {
      void *stack_start =
          (uint8_t *)lowfat_region(idx) + LOWFAT_STACK_MEMORY_OFFSET;
      void *ptr =
          lowfat_map(stack_start, LOWFAT_STACK_MEMORY_SIZE, false, false, fd);
      if (ptr != stack_start)
        lowfat_init_error("failed to map stack region");
    }
    if (close(fd) < 0)
      lowfat_init_error("failed to close stack shm fd");
  }

  // Pivot the live native stack onto a low-fat stack. After this returns we
  // are executing on the new stack — &local in main(...) classifies as stack,
  // not nonfat.
  lowfat_stack_pivot();
}

// Run before ordinary constructors (matches the reference .preinit_array entry).
static void lowfat_preinit(int argc, char **argv, char **envp) {
  (void)argc;
  (void)argv;
  lowfat_envp = envp;
  lowfat_init();
}
__attribute__((used, section(".preinit_array"))) static void (
    *lowfat_preinit_ptr)(int, char **, char **) = lowfat_preinit;
