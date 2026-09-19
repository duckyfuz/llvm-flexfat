// Run an executable under a filter denying tagged-address prctl activation.
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
int main(int argc, char **argv) {
  assert(argc == 2);
  struct sock_filter code[] = {
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_prctl, 0, 3),
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, PR_SET_TAGGED_ADDR_CTRL, 0, 1),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };
  struct sock_fprog prog = { sizeof(code) / sizeof(code[0]), code };
  assert(!prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
  assert(!prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog));
  execl(argv[1], argv[1], (char *)0); return 1;
}
