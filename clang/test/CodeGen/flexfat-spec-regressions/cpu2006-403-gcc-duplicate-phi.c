// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 -mllvm -flexfat-mode=fast -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-mode=fast -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O3 -mllvm -flexfat-mode=fast -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 -mllvm -flexfat-mode=safe -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-mode=safe -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O3 -mllvm -flexfat-mode=safe -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 -mllvm -flexfat-mode=optimized -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-mode=optimized -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O3 -mllvm -flexfat-mode=optimized -emit-llvm -o /dev/null %s
// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-tbi=true -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-tbi=true -mllvm -flexfat-tbi-storage=prior-byte -emit-llvm -o /dev/null %s
// RUN: %if !flexfat-custom-config %{ %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-tbi=true -mllvm -flexfat-tbi-storage=shadow -emit-llvm -o /dev/null %s %}

// SPEC CPU2006 403.gcc, explow.c: two switch cases exit a loop through the
// same cleanup block. FlexFat must give duplicate predecessor edges compatible
// companion PHI values so that the following SimplifyCFG pass can merge them.

typedef struct node {
  int code;
  struct node *next;
  void *pattern;
} node;

extern int mentions(node *, void *);
extern int sets(node *, node *);
extern void unexpected(void);

node *find_next_ref(node *reg, node *insn) {
  node *next;
  for (insn = insn->next; insn; insn = next) {
    next = insn->next;
    switch (insn->code) {
    case 35:
    case 36:
      return 0;
    case 32:
    case 33:
    case 34:
      if (sets(reg, insn))
        return 0;
      if (mentions(reg, insn->pattern))
        return insn;
      break;
    case 37:
      break;
    default:
      unexpected();
    }
  }
  return 0;
}
