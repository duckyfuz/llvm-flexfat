// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O3 -emit-llvm -o /dev/null %s
// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -fsanitize-flexfat-tbi=last-byte -emit-llvm -o /dev/null %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-tbi=prior-byte -emit-llvm -o /dev/null %s
// RUN: %if !flexfat-custom-config %{ %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-tbi=shadow -emit-llvm -o /dev/null %s %}

// SPEC 403.gcc: duplicate switch edges need compatible companion PHIs for SimplifyCFG.
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
