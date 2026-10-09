// RUN: %clangxx_flexfat -O0 -S -emit-llvm %S/Inputs/store-undef.ll -o %t.instrumented.ll
// RUN: %python %S/Inputs/materialize-store-undef.py %t.instrumented.ll %t.chosen.ll
// RUN: %clang -c %t.chosen.ll -o %t.checked.o
// RUN: %clang -c %s -o %t.driver.o
// RUN: %clangxx_flexfat %t.driver.o %t.checked.o -o %t
// RUN: %run %t undef
// RUN: %run %t valid
// RUN: %run %t derived-undef
// RUN: %run %t derived-valid
// RUN: not %run %t derived-oob 2>&1 | FileCheck %s --check-prefix=VALUE
// RUN: not %run %t oob 2>&1 | FileCheck %s --check-prefix=VALUE
// RUN: not %run %t destination 2>&1 | FileCheck %s --check-prefix=DEST

// VALUE: FLEXFAT ERROR: out-of-bounds
// VALUE: operation = read
// DEST: FLEXFAT ERROR: out-of-bounds
// DEST: operation = write

// The driver is uninstrumented so the tested checks are in the IR fixture.
#include <string.h>
extern "C" void loop_spill(void **slot, unsigned long offset, bool update);
extern "C" void bad_destination();
extern "C" void derived_spill(void **slot, unsigned long offset, bool take);

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  void *slot = nullptr;
  if (!strcmp(argv[1], "destination"))
    bad_destination();
  else if (!strncmp(argv[1], "derived-", 8))
    derived_spill(&slot, !strcmp(argv[1], "derived-oob") ? 256 : 1,
                  strcmp(argv[1], "derived-undef") != 0);
  else
    loop_spill(&slot, !strcmp(argv[1], "oob") ? 256 : 1,
               strcmp(argv[1], "undef") != 0);
  return 0;
}
