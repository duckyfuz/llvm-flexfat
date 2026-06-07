//===- FlexFat.cpp - FlexFat bounds-checking instrumentation --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// FlexFat: an in-tree reimplementation of the LowFat spatial-memory-safety
// bounds checker, modeled structurally on AddressSanitizer.
//
// Unit 7: load/store bounds-check instrumentation (the LOAD/STORE path). For
// each interesting memory access we (1) compute the object base via calcBasePtr
// (recursing through GEP/bitcast; an allocation is its own base; a NULL base
// means non-fat -> no check), and (2) emit an inlined bounds check before the
// access. Faithful port of LowFat.cpp:718-808 (calcBasePtr), :892-1021
// (getInterestingInsts), :1026-1066 (insertBoundsCheck), :1128-1243 (the
// inlined lowfat_base / lowfat_oob_check bodies).
//
// DIVERGENCE from the reference structure (see docs/STATUS.md): the reference
// emits `call lowfat_base` / `call lowfat_oob_check` and materializes them as
// alwaysinline helpers (addLowFatFuncs), relying on a bundled post-pass inliner
// to inline them. FlexFat is a New-PM *function* pass at ScalarOptimizerLate
// with no inliner after it (the Unit 6 decision), and a function pass cannot
// safely add module-level functions. So we emit the *post-inline* IR directly:
// the lowfat_base computation and the oob_check body are emitted inline at the
// access site. The only out-of-line callee is lowfat_oob_error, which lives in
// the cold error block (off the fast path) exactly as in the reference.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/FlexFat.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <string>

using namespace llvm;

#define DEBUG_TYPE "flexfat"

STATISTIC(NumChecks, "Number of FlexFat bounds checks inserted");
STATISTIC(NumElided, "Number of FlexFat bounds checks elided (proven in-bounds)");
// Bumped whenever getPtrBounds hits a pointer producer it does not recognize and
// falls back to NONFAT (elide). On LLVM 23 / opaque pointers, IR forms have
// shifted vs the 4.0 reference, so this firing means the recognition list is
// incomplete relative to the IR we actually see -- a missing case to add, not a
// silent unsound elision. check-flexfat asserts this stays 0 over the corpus.
STATISTIC(NumUnknownProducers,
          "Number of unrecognized pointer producers (bounds analysis fell back "
          "to NONFAT / no check)");

// A real, FileCheck-able warning (port of the reference's LowFatWarning) so the
// unrecognized-producer fallback is a visible signal, not a silent elision.
class FlexFatDiag : public DiagnosticInfo {
  const Twine &Msg;

public:
  explicit FlexFatDiag(const Twine &M LLVM_LIFETIME_BOUND)
      : DiagnosticInfo(getKind(), DS_Warning), Msg(M) {}
  void print(DiagnosticPrinter &DP) const override { DP << Msg; }
  static int getKind() {
    static int K = getNextAvailablePluginDiagnosticKind();
    return K;
  }
};

// -flexfat-no-check-fields: trust input pointers up to the indexed object type,
// eliding checks on in-bounds field accesses (LowFat -lowfat-no-check-fields).
static cl::opt<bool> ClNoCheckFields(
    "flexfat-no-check-fields", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: trust input pointers up to the indexed object's size, "
             "skipping bounds checks on provably in-bounds field accesses"));

// -flexfat-no-elide: disable the static bounds analysis (check everything). For
// A/B measurement of the elimination's effect; not a LowFat option.
static cl::opt<bool> ClNoElide(
    "flexfat-no-elide", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: disable static elision of provably-safe bounds checks"));

// -flexfat-no-replace-malloc (LowFat -lowfat-no-replace-malloc): leave the
// allocator family (malloc/free/.../new/delete) calls untouched. mem-intrinsics
// are replaced regardless.
static cl::opt<bool> ClNoReplaceMalloc(
    "flexfat-no-replace-malloc", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: do not replace malloc()/free()/... with lowfat_* "
             "(disables heap protection of pass-compiled allocations)"));

// Per-kind check suppression (LowFat.cpp:161-184, :237-262, via filterKind).
static cl::opt<bool> ClNoCheckReads(
    "flexfat-no-check-reads", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: do not OOB-check reads"));
static cl::opt<bool> ClNoCheckWrites(
    "flexfat-no-check-writes", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: do not OOB-check writes"));
static cl::opt<bool> ClNoCheckMemset(
    "flexfat-no-check-memset", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: do not OOB-check memset"));
static cl::opt<bool> ClNoCheckMemcpy(
    "flexfat-no-check-memcpy", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: do not OOB-check memcpy or memmove"));
// Forward-declared: pointer-escape checks land in Part III; this flag is wired
// into filterKind for the escape info codes (5-9) but no escape checks are
// emitted yet, so it is currently inert.
static cl::opt<bool> ClNoCheckEscapes(
    "flexfat-no-check-escapes", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: do not OOB-check pointer escapes (Part III; inert)"));

static cl::opt<bool> ClCheckWholeAccess(
    "flexfat-check-whole-access", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: OOB-check the whole access [ptr, ptr+sizeof(*ptr)) "
             "rather than just the byte at ptr"));

// Unit 12b: suppress alloca lowfatification (the escape-gated mirror
// transform). Default = checks on (escaping allocas get lowfatified).
static cl::opt<bool> ClNoReplaceAlloca(
    "flexfat-no-replace-alloca", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: do not lowfatify stack allocations (escaping allocas "
             "stay native; no mirror inserted, no stack OOB checks)"));
// Forward-declared: globals lowfatification is Unit 13; flag is inert.
static cl::opt<bool> ClNoReplaceGlobals(
    "flexfat-no-replace-globals", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: do not lowfatify globals (Unit 13; inert)"));

static cl::opt<std::string> ClBlacklist(
    "flexfat-no-check-blacklist", cl::Hidden, cl::init("-"),
    cl::desc("FlexFat: do not OOB-check the functions/files in this "
             "SpecialCaseList blacklist ([flexfat] section, fun:/src: globs)"));

// Error-block modes (LowFat.cpp:1202-1235, §4.4).
static cl::opt<bool> ClNoAbort(
    "flexfat-no-abort", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: warn and continue (lowfat_oob_warning) instead of "
             "aborting on an OOB error"));
static cl::opt<bool> ClSignal(
    "flexfat-signal", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: raise SIGILL (inline ud2, no runtime call) on an OOB "
             "error"));

// filterKind (LowFat.cpp:237-262): is this access kind's check suppressed?
static bool filterKind(unsigned Info) {
  switch (Info) {
  case 0: // READ
    return ClNoCheckReads;
  case 1: // WRITE
    return ClNoCheckWrites;
  case 2: // MEMCPY / MEMMOVE
    return ClNoCheckMemcpy;
  case 3: // MEMSET
    return ClNoCheckMemset;
  default: // 5-9: escape kinds (Part III)
    return ClNoCheckEscapes;
  }
}

// SpecialCaseList blacklist (LowFat.cpp:1684-1695). Cached per path so a large
// module parses the file once. The path is constant within a compile, so the
// first (thread-safe) static init is the only mutation.
static SpecialCaseList *getBlacklist() {
  static std::unique_ptr<SpecialCaseList> Cached;
  static std::string CachedPath;
  static bool Inited = false;
  if (!Inited || CachedPath != ClBlacklist) {
    Inited = true;
    CachedPath = ClBlacklist;
    Cached.reset();
    if (!ClBlacklist.empty() && ClBlacklist != "-") {
      std::string Err;
      Cached = SpecialCaseList::create({std::string(ClBlacklist)},
                                       *vfs::getRealFileSystem(), Err);
    }
  }
  return Cached.get();
}

static bool isBlacklisted(Function &F) {
  SpecialCaseList *SCL = getBlacklist();
  if (!SCL)
    return false;
  return SCL->inSection("flexfat", "src",
                        F.getParent()->getModuleIdentifier()) ||
         SCL->inSection("flexfat", "fun", F.getName());
}

// Mirror metadata kind: tagged onto the mirror gep produced by
// makeAllocaLowFatPtr so calcBasePtr / getPtrBounds recognise it as a fat
// (lowfat) stack pointer instead of walking back through to the (non-fat)
// alloca. See Unit 12b doc in STATUS.md.
static constexpr const char kStackMirrorMD[] = "flexfat.stack.mirror";

// LowFat.cpp:810-846. Does the given pointer-derived integer "escape"? Used
// by doesAllocaEscape's PtrToInt branch to decide whether the address of a
// local has truly escaped (stored into memory, passed to a call, etc.) or
// only used in a comparison/branch (which doesn't expose the address).
static bool doesIntEscape(llvm::Value *Val, llvm::SmallPtrSetImpl<llvm::Value *> &Seen) {
  if (!Seen.insert(Val).second)
    return false;
  if (Val->getType()->isVoidTy())
    return true;  // unrecognized — conservatively escape
  for (User *U : Val->users()) {
    if (isa<ReturnInst>(U) || isa<CallInst>(U) || isa<InvokeInst>(U) ||
        isa<StoreInst>(U) || isa<IntToPtrInst>(U))
      return true;
    if (isa<CmpInst>(U) || isa<BranchInst>(U) || isa<SwitchInst>(U))
      continue;
    if (doesIntEscape(U, Seen))
      return true;
  }
  return false;
}

// LowFat.cpp:1343-1414. Returns true iff the alloca's address is observable
// outside direct-use channels (the predicate for "needs lowfatification").
// Direct uses that do NOT count as escapes: load through the pointer, cmp
// (address comparison), self-store of a value through the address,
// return-of-local (UB but doesn't escape), lifetime intrinsics, and calls to
// doesNotAccessMemory functions. RAUW-relevant: this walks recursively
// through gep/bitcast/select/phi.
static bool doesAllocaEscape(llvm::Value *Val,
                             llvm::SmallPtrSetImpl<llvm::Value *> &Seen) {
  if (!Seen.insert(Val).second)
    return false;
  if (Val->getType()->isVoidTy())
    return true;
  for (User *U : Val->users()) {
    if (isa<ReturnInst>(U))
      continue; // returning a local is UB but address doesn't escape
    if (isa<LoadInst>(U) || isa<CmpInst>(U))
      continue;
    if (auto *S = dyn_cast<StoreInst>(U)) {
      // Self-store (storing some value TO the address) is fine; storing the
      // address itself somewhere is an escape.
      if (S->getPointerOperand() == Val)
        continue;
      return true;
    }
    if (isa<PtrToIntInst>(U)) {
      llvm::SmallPtrSet<llvm::Value *, 8> IntSeen;
      if (doesIntEscape(U, IntSeen))
        return true;
      continue;
    }
    if (auto *Intr = dyn_cast<IntrinsicInst>(U)) {
      // Lifetime markers reference the alloca's address but never observe
      // it outside the optimizer's bookkeeping, so they must NOT count as
      // escapes. The reference (LowFat.cpp:1343-1414, clang/LLVM 4.0) got
      // away without this case because at -O its CallInst clause caught
      // them under `doesNotAccessMemory()` — back then lifetime intrinsics
      // had no argmem effects. Modern LLVM marks them `memory(argmem:
      // readwrite)`, so without this carve-out every clang-4+ emitted
      // alloca that survives mem2reg (e.g. anything `volatile`) gets
      // spuriously lowfatified — the byte-array swap produces a mirror
      // gep with a ~2 TB negative offset off the alloca that downstream
      // SROA can't reconcile, folding the function to `ret … poison`.
      // Pinned by volatile_alloca_escape_bug.ll.
      Intrinsic::ID ID = Intr->getIntrinsicID();
      if (ID == Intrinsic::lifetime_start || ID == Intrinsic::lifetime_end)
        continue;
    }
    if (auto *Call = dyn_cast<CallInst>(U)) {
      Function *F = Call->getCalledFunction();
      if (F && F->doesNotAccessMemory())
        continue;
      return true;
    }
    if (auto *Inv = dyn_cast<InvokeInst>(U)) {
      Function *F = Inv->getCalledFunction();
      if (F && F->doesNotAccessMemory())
        continue;
      return true;
    }
    if (isa<GetElementPtrInst>(U) || isa<BitCastInst>(U) ||
        isa<SelectInst>(U) || isa<PHINode>(U)) {
      if (doesAllocaEscape(U, Seen))
        return true;
      continue;
    }
    // Unknown user — conservatively treat as escape (matches the reference's
    // "(BUG) unknown alloca user" branch).
    return true;
  }
  return false;
}

// LowFat.cpp:1419-1430. Wrapper: gated by -flexfat-no-replace-alloca.
static bool isInterestingAlloca(llvm::Instruction *I) {
  if (ClNoReplaceAlloca)
    return false;
  auto *A = dyn_cast<AllocaInst>(I);
  if (!A)
    return false;
  // Idempotence guard: skip allocas that are already lowfatified — their
  // user list contains a !flexfat.stack.mirror gep. This fires when the
  // pass runs on a function into which a (previously processed) callee was
  // inlined, so the callee's lowfat alloca is now visible to us. Without
  // this, the inlined alloca would be re-class-sized one tier up and
  // double-mirrored, with the inner mirror landing in an unmapped region.
  for (User *U : A->users())
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U))
      if (GEP->hasMetadata(kStackMirrorMD))
        return false;
  llvm::SmallPtrSet<llvm::Value *, 8> Seen;
  return doesAllocaEscape(A, Seen);
}

// Heap size classes (non-POW2 default). SINGLE-SOURCED with the runtime: the
// Unit 2 generator (flexfat/config/lowfat-config.c) emits this `.inc` and the
// runtime's lowfat_sizes[] from the same sizes.cfg run, so the pass cannot
// hand-drift from the runtime. A byte-for-byte drift guard
// (flexfat/config/test/sizes-sync.test) and a behavioral e2e
// (compiler-rt/test/flexfat/TestCases/malloc_class.c) fail the build if they
// ever diverge. Region index `i` (1-based) has class size kLowFatSizes[i-1].
static constexpr uint64_t kLowFatSizes[] = {
#include "FlexFatSizes.inc"
};
static constexpr unsigned kNumRegions =
    sizeof(kLowFatSizes) / sizeof(kLowFatSizes[0]);

// Host-side lowfat_heap_select: the region index for an allocation of `Size`
// (smallest class with `Size <= class - 1`, i.e. class >= Size+1), or 0 (too
// large -> runtime libc fallback). Equivalent to the generated runtime switch.
static unsigned flexfatHeapSelect(uint64_t Size) {
  for (unsigned J = 0; J < kNumRegions; ++J)
    if (kLowFatSizes[J] >= Size + 1)
      return J + 1;
  return 0;
}

namespace {

// ABI constants -- must stay byte-identical to the runtime (lowfat_config.h /
// lowfat.h). _LOWFAT_REGION_SIZE = 2^35, so the region index is ptr >> 35.
constexpr uint64_t kRegionSizeShift = 35;
constexpr uint64_t kSizesAddr = 0x200000;  // _LOWFAT_SIZES  (size_t[])
constexpr uint64_t kMagicsAddr = 0x300000; // _LOWFAT_MAGICS (uint64_t[])

// Stack class limit from the runtime: any alloca > LOWFAT_MAX_STACK_ALLOC_SIZE
// (32 MiB) cannot be mirrored (idx falls below clzll(MAX) = 38).
constexpr uint64_t kMaxStackAllocSize = 33554432;

// Unit 12b fixed-alloca fast path: the reference looks up
// `lowfat_stack_{sizes,masks,offsets}[clzll(size)]` at compile time when
// `size` is a constant, folding the result into immediates so the emitted IR
// is a single sized alloca + a single constant-offset gep (no runtime table
// load). These match lowfat_config.c byte-for-byte (idx 0..64). 0-entries
// flag classes that have no stack support (size > LOWFAT_MAX_STACK_ALLOC_SIZE).
constexpr uint64_t kStackSizes[65] = {
    0,        0,        0,        0,        0,        0,        0,
    0,        0,        0,        0,        0,        0,        0,
    0,        0,        0,        0,        0,        0,        0,
    0,        0,        0,        0,        0,        0,        0,
    0,        0,        0,        0,        0,        0,        0,
    0,        0,        0,        0,
    33554432, 16777216, 8388608,  4194304,  2097152,  1048576,  524288,
    262144,   131072,   65536,    32768,    16384,    8192,     4096,
    2048,     1024,     512,      256,      128,      64,       32,
    16,       16,       16,       16,       16};

constexpr uint64_t kStackMasks[65] = {
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0xFFFFFFFFFE000000ull,
    0xFFFFFFFFFF000000ull,
    0xFFFFFFFFFF800000ull,
    0xFFFFFFFFFFC00000ull,
    0xFFFFFFFFFFE00000ull,
    0xFFFFFFFFFFF00000ull,
    0xFFFFFFFFFFF80000ull,
    0xFFFFFFFFFFFC0000ull,
    0xFFFFFFFFFFFE0000ull,
    0xFFFFFFFFFFFF0000ull,
    0xFFFFFFFFFFFF8000ull,
    0xFFFFFFFFFFFFC000ull,
    0xFFFFFFFFFFFFE000ull,
    0xFFFFFFFFFFFFF000ull,
    0xFFFFFFFFFFFFF800ull,
    0xFFFFFFFFFFFFFC00ull,
    0xFFFFFFFFFFFFFE00ull,
    0xFFFFFFFFFFFFFF00ull,
    0xFFFFFFFFFFFFFF80ull,
    0xFFFFFFFFFFFFFFC0ull,
    0xFFFFFFFFFFFFFFE0ull,
    0xFFFFFFFFFFFFFFF0ull,
    0xFFFFFFFFFFFFFFF0ull,
    0xFFFFFFFFFFFFFFF0ull,
    0xFFFFFFFFFFFFFFF0ull,
    0xFFFFFFFFFFFFFFF0ull,
};

constexpr int64_t kStackOffsets[65] = {
    0,             0,             0,             0,             0,
    0,             0,             0,             0,             0,
    0,             0,             0,             0,             0,
    0,             0,             0,             0,             0,
    0,             0,             0,             0,             0,
    0,             0,             0,             0,             0,
    0,             0,             0,             0,             0,
    0,             0,             0,             0,
    -309237645312, -343597383680, -377957122048, -412316860416, -446676598784,
    -481036337152, -515396075520, -549755813888, -584115552256, -618475290624,
    -652835028992, -687194767360, -824633720832, -996432412672, -1168231104512,
    -1340029796352,-1511828488192,-1683627180032,-1855425871872,-1992864825344,
    -2061584302080,-2095944040448,-2095944040448,-2095944040448,-2095944040448,
    -2095944040448};

// OOB info codes (lowfat.h: LOWFAT_OOB_ERROR_{READ,WRITE}).
constexpr unsigned kInfoRead = 0;
constexpr unsigned kInfoWrite = 1;
constexpr unsigned kInfoMemcpy = 2;
constexpr unsigned kInfoMemset = 3;

// Fast-path branch weights (2000000000:1 in favour of the fast path), so the
// cold error block is placed out of line. NOTE: the reference weights the OOB
// (error) edge 2000000000 and relies on LLVM-4.0's noreturn-cold heuristic to
// override that for block placement. LLVM 23's MachineBlockPlacement honours the
// explicit weight over that heuristic, so weighting the error edge hot would put
// it on the fall-through (a fast-path regression). We weight the error edge cold
// instead -- same intent (2e9:1), same fast-path asm as the reference.
constexpr uint32_t kErrorWeight = 1;
constexpr uint32_t kFastWeight = 2000000000;

// Static bounds lattice (LowFat.cpp:61-137). lb is always 0; `ub` is the
// greatest byte offset from the pointer that is known in-bounds. Two sentinels:
// NONFAT (the pointer is non-fat -> no check) and UNKNOWN (can't prove -> must
// check). An access at offset k is provably safe iff 0 <= k <= ub.
struct Bounds {
  static constexpr int64_t NONFAT = INT64_MAX;
  static constexpr int64_t UNKNOWN = INT64_MIN;

  int64_t ub = 0;
  Bounds() = default;
  explicit Bounds(int64_t Ub) : ub(Ub) {}

  static Bounds empty() { return Bounds(0); }
  static Bounds nonFat() { return Bounds(NONFAT); }
  static Bounds unknown() { return Bounds(UNKNOWN); }

  bool isUnknown() const { return ub == UNKNOWN; }
  bool isNonFat() const { return ub == NONFAT; }
  bool isEmpty() const { return ub == 0; }
  bool isInBounds(int64_t K) const { return K >= 0 && K <= ub; }

  // Walk a GEP that adds `K` bytes: the remaining in-bounds size shrinks.
  void sub(uint64_t K) {
    if (K == 0 || isUnknown() || isNonFat())
      return;
    ub = ((int64_t)K > ub) ? UNKNOWN : ub - (int64_t)K;
  }
  static Bounds min(Bounds A, Bounds B) {
    return Bounds(std::min(A.ub, B.ub));
  }
};

class FlexFat {
public:
  FlexFat(Function &F, const TargetLibraryInfo &TLI)
      : F(F), M(*F.getParent()), Ctx(F.getContext()), DL(M.getDataLayout()),
        I8Ty(Type::getInt8Ty(Ctx)), I64Ty(Type::getInt64Ty(Ctx)),
        I128Ty(Type::getInt128Ty(Ctx)), PtrTy(PointerType::getUnqual(Ctx)),
        TLI(TLI) {}

  bool run();

private:
  Value *calcBasePtr(Value *Ptr);
  Value *emitInlineBase(Value *Ptr);
  void insertBoundsCheck(Instruction *I, Value *Ptr, unsigned Info, Value *Base,
                         uint64_t AccessSize);
  std::pair<BasicBlock *, BasicBlock::iterator> nextInsertPoint(Value *Ptr);

  // Static bounds analysis (LowFat.cpp:426-621): prove an access in-bounds.
  Bounds getPtrBounds(Value *Ptr);
  Bounds getConstantPtrBounds(Constant *C);
  Bounds getInputPtrBounds(Value *Ptr);

  // Unit 9.
  bool checkAccess(Instruction *I, Value *Ptr, unsigned Info,
                   uint64_t AccessSize = 0); // bounds elide+check
  bool instrumentMemIntrinsic(MemIntrinsic *MI);              // end-pointer checks
  bool replaceLibFunc(CallBase *CB);                          // replaceUnsafeLibFuncs
  bool optimizeMalloc(CallBase *CB);                          // const heap_select fold

  // Unit 12b: escape-gated alloca lowfatification (port of
  // LowFat.cpp:1512-1677). Replaces an escaping alloca with a sized byte-
  // array alloca (if the class size differs) at the class boundary, then
  // emits a constant-offset mirror gep tagged !flexfat.stack.mirror so
  // calcBasePtr / getPtrBounds recognise the result as a fat (stack)
  // pointer. Lifetime intrinsics on the alloca are deleted.
  bool makeAllocaLowFatPtr(AllocaInst *Alloca);
  // Helpers to declare extern globals exporting the stack-class tables
  // (consumed only by the VLA path).
  Constant *getStackTable(StringRef Name);

  // Inline a GEP into a fixed runtime table (_LOWFAT_SIZES / _LOWFAT_MAGICS).
  Value *tableSlot(IRBuilder<> &B, uint64_t TableAddr, Value *Idx) {
    Value *Table = B.CreateIntToPtr(B.getInt64(TableAddr), PtrTy);
    return B.CreateGEP(I64Ty, Table, Idx);
  }

  Function &F;
  Module &M;
  LLVMContext &Ctx;
  const DataLayout &DL;
  IntegerType *I8Ty;
  IntegerType *I64Ty;
  IntegerType *I128Ty;
  PointerType *PtrTy;
  const TargetLibraryInfo &TLI;
  DenseMap<Value *, Value *> baseInfo;
  DenseMap<const Value *, int64_t> boundsInfo; // memoized Bounds::ub
};

} // namespace

// Where to place the base computation for `Ptr` (LowFat.cpp:286-320).
std::pair<BasicBlock *, BasicBlock::iterator>
FlexFat::nextInsertPoint(Value *Ptr) {
  if (isa<Argument>(Ptr) || isa<GlobalValue>(Ptr)) {
    BasicBlock &Entry = F.getEntryBlock();
    return {&Entry, Entry.begin()};
  }
  if (auto *I = dyn_cast<Instruction>(Ptr)) {
    if (!I->isTerminator()) {
      BasicBlock::iterator It(I);
      ++It;
      return {I->getParent(), It};
    }
  }
  BasicBlock &Entry = F.getEntryBlock();
  return {&Entry, Entry.begin()};
}

// The inlined non-POW2 lowfat_base (LowFat.cpp:1128-1160): reconstruct the
// object base from the pointer using the reciprocal-multiply magic. Emitted
// right after the pointer's definition; no integer division.
Value *FlexFat::emitInlineBase(Value *Ptr) {
  auto IP = nextInsertPoint(Ptr);
  IRBuilder<> B(&*IP.first, IP.second);

  Value *IPtr = B.CreatePtrToInt(Ptr, I64Ty);
  Value *Idx = B.CreateLShr(IPtr, B.getInt64(kRegionSizeShift));
  Value *Magic = B.CreateAlignedLoad(I64Ty, tableSlot(B, kMagicsAddr, Idx),
                                     Align(sizeof(uint64_t)));
  // objidx = ((u128)iptr * (u128)magic) >> 64
  Value *IPtr128 = B.CreateZExt(IPtr, I128Ty);
  Value *Magic128 = B.CreateZExt(Magic, I128Ty);
  Value *Prod = B.CreateMul(IPtr128, Magic128);
  Prod = B.CreateLShr(Prod, ConstantInt::get(I128Ty, 64));
  Value *ObjIdx = B.CreateTrunc(Prod, I64Ty);
  Value *Size = B.CreateAlignedLoad(I64Ty, tableSlot(B, kSizesAddr, Idx),
                                    Align(sizeof(uint64_t)));
  Value *IBase = B.CreateMul(ObjIdx, Size);
  return B.CreateIntToPtr(IBase, PtrTy);
}

// Bounds for an "input" pointer of unknown provenance (LowFat.cpp:426-439).
// Default: empty [0,0] -- the byte at the pointer is trusted, any positive
// offset is checked. With opaque pointers there is no pointee type to size, so
// -flexfat-no-check-fields is applied at the GEP level (getPtrBounds) using the
// GEP's source element type, not here.
Bounds FlexFat::getInputPtrBounds(Value *Ptr) { return Bounds::empty(); }

// Bounds of a constant pointer (LowFat.cpp:441-531).
Bounds FlexFat::getConstantPtrBounds(Constant *C) {
  if (isa<ConstantPointerNull>(C) || isa<UndefValue>(C))
    return Bounds::nonFat();
  auto It = boundsInfo.find(C);
  if (It != boundsInfo.end())
    return Bounds(It->second);

  Bounds B = Bounds::nonFat();
  if (auto *GV = dyn_cast<GlobalVariable>(C)) {
    // Unit 13: a lowfatified global is fat — its base is recoverable via the
    // standard magic-multiply, and the runtime bounds it to the class size
    // (which differs from the source size). Static analysis can still elide
    // in-bounds accesses through the SOURCE size — accessing beyond [0,size)
    // would be UB even if it falls within the rounded-up class — so keep the
    // ub at the source TypeAllocSize, but mark the lowfat tier so the dynamic
    // check still fires for everything we can't prove safe.
    Type *Ty = GV->getValueType();
    if (Ty->isSized()) {
      uint64_t Size = DL.getTypeAllocSize(Ty);
      if (Size != 0) // size==0 implies unspecified size, e.g. int x[];
        B = Bounds((int64_t)Size);
    }
  } else if (auto *CE = dyn_cast<ConstantExpr>(C)) {
    switch (CE->getOpcode()) {
    case Instruction::GetElementPtr: {
      auto *GEP = cast<GEPOperator>(CE);
      B = getPtrBounds(GEP->getPointerOperand());
      if (!B.isUnknown() && !B.isNonFat()) {
        APInt Off(64, 0);
        if (GEP->accumulateConstantOffset(DL, Off) && !Off.isNegative())
          B.sub(Off.getZExtValue());
        else
          B = Bounds::unknown();
      }
      break;
    }
    case Instruction::BitCast:
    case Instruction::AddrSpaceCast:
      B = getConstantPtrBounds(CE->getOperand(0));
      break;
    default:
      B = Bounds::nonFat(); // inttoptr / extract / ... -> assumed non-fat
      break;
    }
  } else if (!isa<GlobalValue>(C)) {
    B = Bounds::nonFat();
  }
  boundsInfo[C] = B.ub;
  return B;
}

// Statically (approx.) bound the object pointed to by `Ptr` (LowFat.cpp:537-621).
Bounds FlexFat::getPtrBounds(Value *Ptr) {
  auto It = boundsInfo.find(Ptr);
  if (It != boundsInfo.end())
    return Bounds(It->second);

  Bounds B = Bounds::nonFat();
  if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
    // A mirror gep is a fat (stack) input pointer in its own right — treat it
    // like an argument: empty bounds, so direct deref is elided but any
    // positive offset is checked. Don't walk back to the alloca.
    if (GEP->hasMetadata(kStackMirrorMD)) {
      B = Bounds::empty();
    } else {
      B = getPtrBounds(GEP->getPointerOperand());
      // -flexfat-no-check-fields: trust an input-pointer base (empty bounds) up
      // to the indexed object -- the opaque-pointer analog of [0, sizeof(*ptr)].
      if (ClNoCheckFields && B.isEmpty() && GEP->getSourceElementType()->isSized())
        B = Bounds((int64_t)DL.getTypeAllocSize(GEP->getSourceElementType()));
      if (!B.isUnknown() && !B.isNonFat()) {
        APInt Off(64, 0);
        if (cast<GEPOperator>(GEP)->accumulateConstantOffset(DL, Off) &&
            !Off.isNegative())
          B.sub(Off.getZExtValue());
        else
          B = Bounds::unknown();
      }
    }
  } else if (auto *AI = dyn_cast<AllocaInst>(Ptr)) {
    auto *CI = dyn_cast<ConstantInt>(AI->getArraySize());
    if (CI && AI->getAllocatedType()->isSized())
      B = Bounds((int64_t)(CI->getZExtValue() *
                           DL.getTypeAllocSize(AI->getAllocatedType())));
    else
      B = getInputPtrBounds(Ptr);
  } else if (auto *BC = dyn_cast<BitCastInst>(Ptr)) {
    B = getPtrBounds(BC->getOperand(0));
  } else if (auto *ASC = dyn_cast<AddrSpaceCastInst>(Ptr)) {
    B = getPtrBounds(ASC->getOperand(0));
  } else if (auto *Sel = dyn_cast<SelectInst>(Ptr)) {
    B = Bounds::min(getPtrBounds(Sel->getTrueValue()),
                    getPtrBounds(Sel->getFalseValue()));
  } else if (auto *C = dyn_cast<Constant>(Ptr)) {
    B = getConstantPtrBounds(C);
  } else if (isa<Argument>(Ptr) || isa<LoadInst>(Ptr) ||
             isa<IntToPtrInst>(Ptr) || isa<ExtractValueInst>(Ptr) ||
             isa<ExtractElementInst>(Ptr)) {
    B = getInputPtrBounds(Ptr);
  } else if (auto *CB = dyn_cast<CallBase>(Ptr)) {
    uint64_t Size;
    if (isAllocationFn(CB, &TLI) && getObjectSize(CB, Size, DL, &TLI))
      B = Bounds((int64_t)Size);
    else
      B = getInputPtrBounds(Ptr);
  } else if (auto *PHI = dyn_cast<PHINode>(Ptr)) {
    B = Bounds::nonFat();
    boundsInfo[Ptr] = Bounds::UNKNOWN; // break cycles while recursing
    for (unsigned i = 0, n = PHI->getNumIncomingValues(); i < n; i++) {
      B = Bounds::min(B, getPtrBounds(PHI->getIncomingValue(i)));
      if (B.isUnknown())
        break;
    }
    boundsInfo.erase(Ptr);
  } else {
    // Unrecognized producer. Match the reference's default (NONFAT -> elide,
    // B is already nonFat), but make it a visible signal: this means our
    // recognition list is incomplete for the IR we are seeing, not that the
    // pointer is provably safe. See the NumUnknownProducers note above.
    ++NumUnknownProducers;
    StringRef OpName = "value";
    if (auto *Inst = dyn_cast<Instruction>(Ptr))
      OpName = Inst->getOpcodeName();
    Ctx.diagnose(FlexFatDiag(
        "FlexFat: (BUG) unknown pointer type in static bounds analysis ('" +
        OpName + "'); bounds-check elided -- add this producer to getPtrBounds"));
  }

  boundsInfo[Ptr] = B.ub;
  return B;
}

// Reduce `Ptr` to its object base (LowFat.cpp:718-808). NULL => non-fat.
Value *FlexFat::calcBasePtr(Value *Ptr) {
  auto It = baseInfo.find(Ptr);
  if (It != baseInfo.end())
    return It->second;

  Value *NonFat = ConstantPointerNull::get(PtrTy);
  Value *Base = NonFat;

  if (auto *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
    // A mirror gep (Unit 12b) is a fat (stack) pointer in its own right —
    // don't walk back through it to the non-fat alloca underneath.
    if (GEP->hasMetadata(kStackMirrorMD))
      Base = emitInlineBase(GEP);
    else
      Base = calcBasePtr(GEP->getPointerOperand());
  } else if (auto *BC = dyn_cast<BitCastInst>(Ptr)) {
    Base = calcBasePtr(BC->getOperand(0));
  } else if (auto *ASC = dyn_cast<AddrSpaceCastInst>(Ptr)) {
    Base = calcBasePtr(ASC->getOperand(0));
  } else if (isa<AllocaInst>(Ptr)) {
    // Non-escaping allocas: not lowfatified, so non-fat. Escaping allocas
    // never appear directly under a checked access — every use has been
    // RAUW'd to a !flexfat.stack.mirror gep (handled above).
    Base = NonFat;
  } else if (auto *GV = dyn_cast<GlobalVariable>(Ptr)) {
    // Unit 13: a global with a `lowfat_section_*` section is in a lowfat
    // region, so its base is recoverable via the same magic-multiply as any
    // other fat pointer. Globals without that section (uninstrumented or
    // excluded) stay non-fat.
    if (GV->hasSection() && GV->getSection().starts_with("lowfat_section_"))
      Base = emitInlineBase(GV);
    else
      Base = NonFat;
  } else if (isa<Constant>(Ptr)) {
    // Non-global constants (e.g. inttoptr immediates) — non-fat.
    Base = NonFat;
  } else if (auto *Sel = dyn_cast<SelectInst>(Ptr)) {
    Value *A = calcBasePtr(Sel->getTrueValue());
    Value *C = calcBasePtr(Sel->getFalseValue());
    IRBuilder<> B(Sel);
    Base = B.CreateSelect(Sel->getCondition(), A, C);
  } else if (auto *PHI = dyn_cast<PHINode>(Ptr)) {
    unsigned N = PHI->getNumIncomingValues();
    IRBuilder<> B(PHI);
    PHINode *BasePHI = B.CreatePHI(PtrTy, N);
    baseInfo[Ptr] = BasePHI; // memoize before recursing (break cycles)
    for (unsigned i = 0; i < N; i++)
      BasePHI->addIncoming(UndefValue::get(PtrTy), PHI->getIncomingBlock(i));
    bool AllNonFat = true;
    for (unsigned i = 0; i < N; i++) {
      Value *IB = calcBasePtr(PHI->getIncomingValue(i));
      if (!isa<ConstantPointerNull>(IB))
        AllNonFat = false;
      BasePHI->setIncomingValue(i, IB);
    }
    if (AllNonFat) {
      baseInfo[Ptr] = NonFat; // leave the (dead) PHI; the optimizer drops it
      return NonFat;
    }
    return BasePHI;
  } else if (auto *CB = dyn_cast<CallBase>(Ptr)) {
    // An allocation is its own base; anything else needs a runtime base.
    Base = isAllocationFn(CB, &TLI) ? Ptr : emitInlineBase(Ptr);
  } else if (isa<Argument>(Ptr) || isa<LoadInst>(Ptr) || isa<IntToPtrInst>(Ptr) ||
             isa<ExtractValueInst>(Ptr) || isa<ExtractElementInst>(Ptr)) {
    Base = emitInlineBase(Ptr);
  }
  // else: unknown pointer producer -> non-fat (drop the check).

  baseInfo[Ptr] = Base;
  return Base;
}

// The inlined lowfat_oob_check (LowFat.cpp:1026-1066 + :1168-1243), emitted
// before the access. access_size defaults to 0 (check the byte at ptr).
void FlexFat::insertBoundsCheck(Instruction *I, Value *Ptr, unsigned Info,
                                Value *Base, uint64_t AccessSize) {
  IRBuilder<> B(I);
  Value *IBase = B.CreatePtrToInt(Base, I64Ty);
  Value *Idx = B.CreateLShr(IBase, B.getInt64(kRegionSizeShift));
  Value *Size = B.CreateAlignedLoad(I64Ty, tableSlot(B, kSizesAddr, Idx),
                                    Align(sizeof(uint64_t)));
  Value *IPtr = B.CreatePtrToInt(Ptr, I64Ty);
  Value *Diff = B.CreateSub(IPtr, IBase);
  // The check is `diff >=u size - access_size`. access_size defaults to 0 (just
  // the byte at ptr); -flexfat-check-whole-access sets it to sizeof(*ptr)-1 so
  // the whole [ptr, ptr+sizeof) span is validated.
  if (AccessSize != 0)
    Size = B.CreateSub(Size, B.getInt64(AccessSize));
  Value *Cmp = B.CreateICmpUGE(Diff, Size);

  MDNode *Weights =
      MDBuilder(Ctx).createBranchWeights(kErrorWeight, kFastWeight);
  // Error-block mode (LowFat.cpp:1202-1235): -flexfat-no-abort warns and
  // continues (branch back to the access); otherwise the block is unreachable
  // (-flexfat-signal traps with ud2/SIGILL, default calls lowfat_oob_error).
  Instruction *ErrTerm = SplitBlockAndInsertIfThen(
      Cmp, I, /*Unreachable=*/!ClNoAbort, Weights);
  IRBuilder<> EB(ErrTerm);

  if (ClNoAbort) {
    FunctionCallee Warn = M.getOrInsertFunction(
        "lowfat_oob_warning",
        FunctionType::get(EB.getVoidTy(), {EB.getInt32Ty(), PtrTy, PtrTy},
                          false));
    EB.CreateCall(Warn, {EB.getInt32(Info), Ptr, Base});
  } else if (ClSignal) {
    InlineAsm *Ud2 = InlineAsm::get(
        FunctionType::get(EB.getVoidTy(), {}, false), "ud2",
        "~{dirflag},~{fpsr},~{flags}", /*hasSideEffects=*/true,
        /*isAlignStack=*/false, InlineAsm::AD_Intel);
    CallInst *Call = EB.CreateCall(Ud2, {});
    Call->setDoesNotReturn();
  } else {
    FunctionCallee OobError = M.getOrInsertFunction(
        "lowfat_oob_error",
        FunctionType::get(EB.getVoidTy(), {EB.getInt32Ty(), PtrTy, PtrTy},
                          false));
    if (auto *Fn = dyn_cast<Function>(OobError.getCallee()))
      Fn->setDoesNotReturn();
    CallInst *Call = EB.CreateCall(OobError, {EB.getInt32(Info), Ptr, Base});
    Call->setDoesNotReturn();
  }
}

// Bounds-check (or, via the static analysis, elide) one access at `Ptr`.
bool FlexFat::checkAccess(Instruction *I, Value *Ptr, unsigned Info,
                          uint64_t AccessSize) {
  if (filterKind(Info))
    return false; // this access kind's checks are suppressed
  if (!ClNoElide && getPtrBounds(Ptr).isInBounds((int64_t)AccessSize)) {
    ++NumElided;
    return false; // provably in-bounds
  }
  Value *Base = calcBasePtr(Ptr);
  if (!Base || isa<ConstantPointerNull>(Base))
    return false; // non-fat pointer
  insertBoundsCheck(I, Ptr, Info, Base, AccessSize);
  ++NumChecks;
  return true;
}

// memcpy/memset/memmove intrinsics: validate the end pointer(s) Dst+len (and
// Src+len) against their objects (LowFat.cpp:913-947). Info = MEMCPY / MEMSET.
bool FlexFat::instrumentMemIntrinsic(MemIntrinsic *MI) {
  IRBuilder<> B(MI);
  bool Changed = false;
  if (auto *MT = dyn_cast<MemTransferInst>(MI)) {
    Value *Len = B.CreateIntCast(MT->getLength(), I64Ty, /*isSigned=*/false);
    Value *SrcEnd = B.CreateGEP(I8Ty, MT->getRawSource(), Len);
    Value *DstEnd = B.CreateGEP(I8Ty, MT->getRawDest(), Len);
    Changed |= checkAccess(MI, SrcEnd, kInfoMemcpy);
    Changed |= checkAccess(MI, DstEnd, kInfoMemcpy);
  } else if (auto *MS = dyn_cast<MemSetInst>(MI)) {
    Value *Len = B.CreateIntCast(MS->getLength(), I64Ty, /*isSigned=*/false);
    Value *DstEnd = B.CreateGEP(I8Ty, MS->getRawDest(), Len);
    Changed |= checkAccess(MI, DstEnd, kInfoMemset);
  }
  return Changed;
}

// replaceUnsafeLibFuncs (LowFat.cpp:1071-1118): redirect a call to an unsafe
// libc function to its lowfat_* equivalent. mem-intrinsics (the *named* memcpy/
// memset/memmove functions) are always replaced; the allocator family is
// replaced unless -flexfat-no-replace-malloc. Done per call site (a function
// pass must not RAUW module-level declarations); rare non-call uses are left.
bool FlexFat::replaceLibFunc(CallBase *CB) {
  Function *Callee = CB->getCalledFunction();
  if (!Callee || !Callee->hasName())
    return false;
  StringRef Name = Callee->getName();
  static const char *const MemFns[] = {"memcpy", "memset", "memmove"};
  static const char *const AllocFns[] = {
      "malloc",  "free",    "calloc",  "realloc",
      "posix_memalign", "aligned_alloc", "valloc", "memalign", "pvalloc",
      "strdup",  "strndup", "_Znwm",   "_Znam",   "_ZdlPv",    "_ZdaPv",
      "_ZnwmRKSt9nothrow_t", "_ZnamRKSt9nothrow_t"};
  bool IsMem = false, IsAlloc = false;
  for (const char *N : MemFns)
    IsMem |= (Name == N);
  for (const char *N : AllocFns)
    IsAlloc |= (Name == N);
  if (!IsMem && !IsAlloc)
    return false;
  if (IsAlloc && ClNoReplaceMalloc)
    return false;
  FunctionCallee New =
      M.getOrInsertFunction(("lowfat_" + Name).str(), CB->getFunctionType());
  CB->setCalledFunction(New);
  return true;
}

// optimizeMalloc (LowFat.cpp:330-384): a constant lowfat_malloc(K) becomes
// lowfat_malloc_index(idx, K) with idx = heap_select(K) folded at compile time,
// eliding the runtime clzll/lzcnt dispatch.
bool FlexFat::optimizeMalloc(CallBase *CB) {
  Function *Callee = CB->getCalledFunction();
  if (!Callee || CB->arg_size() != 1 || isa<InvokeInst>(CB))
    return false;
  StringRef Name = Callee->getName();
  if (Name != "lowfat_malloc" && Name != "lowfat__Znwm" &&
      Name != "lowfat__Znam")
    return false;
  auto *Size = dyn_cast<ConstantInt>(CB->getArgOperand(0));
  if (!Size)
    return false;
  unsigned Idx = flexfatHeapSelect(Size->getZExtValue());
  IRBuilder<> B(CB);
  FunctionCallee MIdx =
      M.getOrInsertFunction("lowfat_malloc_index", PtrTy, I64Ty, I64Ty);
  CallInst *NewCall = B.CreateCall(MIdx, {B.getInt64(Idx), Size});
  NewCall->setDebugLoc(CB->getDebugLoc());
  CB->replaceAllUsesWith(NewCall);
  CB->eraseFromParent();
  return true;
}

Constant *FlexFat::getStackTable(StringRef Name) {
  // Externally declared in compiler-rt/lib/flexfat/lowfat_config.c as
  // `size_t lowfat_stack_*[64+1]`. We declare it as `[0 x i64]` so the
  // type pin still mirrors the linker symbol; the GEP source element type
  // makes the array semantics explicit under opaque pointers.
  ArrayType *TableTy = ArrayType::get(I64Ty, 0);
  Constant *G = M.getOrInsertGlobal(Name, TableTy);
  if (auto *GV = dyn_cast<GlobalVariable>(G))
    GV->setConstant(true);
  return G;
}

// Port of LowFat.cpp:1512-1677: replace an escaping alloca with a sized
// byte-array alloca at the size-class boundary and a constant-offset mirror
// gep tagged !flexfat.stack.mirror. Per the Unit-7 architecture decision,
// the addLowFatFuncs path (helper-call IR + bundled inliner) is replaced
// with inline post-inline IR — runtime table loads only on the VLA path.
bool FlexFat::makeAllocaLowFatPtr(AllocaInst *Alloca) {
  Value *ArraySize = Alloca->getArraySize();
  Type *Ty = Alloca->getAllocatedType();
  ConstantInt *ISize = dyn_cast<ConstantInt>(ArraySize);
  auto IP = nextInsertPoint(Alloca);
  IRBuilder<> B(IP.first, IP.second);

  Value *Offset = nullptr;
  Value *AllocedPtr = nullptr;
  Value *NoReplace1 = nullptr;
  bool delAlloca = false;

  if (ISize) {
    // FIXED-SIZE PATH (the common case). Every lookup folds to a compile-
    // time immediate: idx, newSize, mask, offset all decided here, no
    // runtime table load.
    uint64_t TyAllocSize = DL.getTypeAllocSize(Ty).getFixedValue();
    uint64_t size = TyAllocSize * ISize->getZExtValue();
    if (size == 0)
      return false; // degenerate; leave native
    uint64_t idx = (uint64_t)__builtin_clzll(size);
    // clzll(LOWFAT_MAX_STACK_ALLOC_SIZE) = 38 ⇒ idx<=38 means too big.
    if (idx <= (uint64_t)__builtin_clzll(kMaxStackAllocSize))
      return false;
    uint64_t newSize = kStackSizes[idx];
    uint64_t mask = kStackMasks[idx];
    int64_t off = kStackOffsets[idx];
    uint64_t alignBytes = (uint64_t)(~mask) + 1;

    if (Align(alignBytes) > Alloca->getAlign())
      Alloca->setAlignment(Align(alignBytes));

    if (newSize != size) {
      // Replace with byte-array alloca of newSize at the class boundary.
      AllocaInst *NewAlloca = B.CreateAlloca(I8Ty, B.getInt64(newSize));
      NewAlloca->setAlignment(Alloca->getAlign());
      AllocedPtr = NewAlloca;
      delAlloca = true;
    } else {
      // Original alloca is already class-sized; keep it (with the new align).
      AllocedPtr = Alloca;
    }
    Offset = B.getInt64(off);
    NoReplace1 = AllocedPtr;
  } else {
    // VLA PATH: idx and tables go through inline IR (no helper call).
    delAlloca = true;
    uint64_t TyAllocSize = DL.getTypeAllocSize(Ty).getFixedValue();
    Value *Size =
        B.CreateMul(B.getInt64(TyAllocSize), ArraySize);
    // idx = ctlz.i64(size, /*is_zero_poison=*/true)
    Function *Ctlz =
        Intrinsic::getOrInsertDeclaration(&M, Intrinsic::ctlz, {I64Ty});
    CallInst *IdxC = B.CreateCall(Ctlz, {Size, B.getInt1(true)});
    IdxC->setTailCall(true);
    Value *Idx = IdxC;

    // offset = lowfat_stack_offsets[idx]
    Constant *Offs = getStackTable("lowfat_stack_offsets");
    ArrayType *TableTy = ArrayType::get(I64Ty, 0);
    Value *OffSlot = B.CreateGEP(TableTy, Offs, {B.getInt64(0), Idx});
    Offset = B.CreateAlignedLoad(I64Ty, OffSlot, Align(8));

    // newSize = lowfat_stack_sizes[idx]
    Constant *Sizes = getStackTable("lowfat_stack_sizes");
    Value *SzSlot = B.CreateGEP(TableTy, Sizes, {B.getInt64(0), Idx});
    Value *NewSz = B.CreateAlignedLoad(I64Ty, SzSlot, Align(8));

    // Replacement byte-array alloca, then align via the masks table.
    AllocaInst *NewAlloca = B.CreateAlloca(I8Ty, NewSz);
    Value *SP = NewAlloca;
    Constant *Masks = getStackTable("lowfat_stack_masks");
    Value *MaskSlot = B.CreateGEP(TableTy, Masks, {B.getInt64(0), Idx});
    Value *Mask = B.CreateAlignedLoad(I64Ty, MaskSlot, Align(8));
    Value *SPInt = B.CreatePtrToInt(SP, I64Ty);
    Value *Aligned = B.CreateAnd(SPInt, Mask);
    SP = B.CreateIntToPtr(Aligned, PtrTy);
    // stackrestore(SP) — discard the unaligned head so the function's stack
    // ends at the now-aligned address.
    Function *Restore =
        Intrinsic::getOrInsertDeclaration(&M, Intrinsic::stackrestore, {PtrTy});
    CallInst *RestoreC = B.CreateCall(Restore, {SP});
    RestoreC->setTailCall(true);

    AllocedPtr = SP;
    NoReplace1 = SP;
  }

  // The mirror: a constant-offset gep on the aligned alloca pointer. Tag it
  // with !flexfat.stack.mirror so the bounds-check path treats the result as
  // a fat (stack) pointer (calcBasePtr emits inlined lowfat_base on it).
  Value *MirroredPtr = B.CreateGEP(I8Ty, AllocedPtr, Offset);
  if (auto *MGEP = dyn_cast<GetElementPtrInst>(MirroredPtr))
    MGEP->setMetadata(kStackMirrorMD, MDNode::get(Ctx, {}));
  Value *NoReplace2 = MirroredPtr;

  // RAUW: every USER of the original alloca that isn't one of the values we
  // used in the construction (NoReplace1/NoReplace2) is rewritten to use the
  // mirror. Lifetime intrinsics get DELETED — the size on the marker no
  // longer matches the (possibly grown) allocation, and the reference's
  // bookkeeping is too painful to keep in sync.
  SmallVector<User *, 8> Replace, Lifetimes;
  for (User *U : Alloca->users()) {
    if (U == NoReplace1 || U == NoReplace2)
      continue;
    if (auto *Intr = dyn_cast<IntrinsicInst>(U)) {
      Intrinsic::ID ID = Intr->getIntrinsicID();
      if (ID == Intrinsic::lifetime_start || ID == Intrinsic::lifetime_end) {
        Lifetimes.push_back(U);
        continue;
      }
    }
    Replace.push_back(U);
  }
  for (User *U : Replace)
    U->replaceUsesOfWith(Alloca, MirroredPtr);
  for (User *U : Lifetimes)
    if (auto *L = dyn_cast<Instruction>(U))
      L->eraseFromParent();
  if (delAlloca)
    Alloca->eraseFromParent();
  return true;
}

bool FlexFat::run() {
  bool Changed = false;
  // Phase 0 (Unit 12b): lowfatify every alloca whose address escapes. Done
  // BEFORE the load/store sweep so the bounds-check phase sees the mirror
  // pointer (a real fat pointer) for any access through the alloca.
  if (!ClNoReplaceAlloca) {
    SmallVector<AllocaInst *, 8> Allocas;
    for (Instruction &I : instructions(F))
      if (isInterestingAlloca(&I))
        Allocas.push_back(cast<AllocaInst>(&I));
    for (AllocaInst *A : Allocas)
      Changed |= makeAllocaLowFatPtr(A);
  }

  // Phase 1: collect interesting instructions without mutating IR (the
  // getInterestingInsts sweep). Loads/stores and the mem-intrinsics get bounds
  // checks; named libc calls get replaced.
  // (Instruction, pointer, info, access-size). access_size is sizeof(*ptr)-1
  // under -flexfat-check-whole-access, else 0.
  SmallVector<std::tuple<Instruction *, Value *, unsigned, uint64_t>, 16>
      LoadStores;
  SmallVector<MemIntrinsic *, 8> MemIntrs;
  SmallVector<CallBase *, 8> LibCalls;
  auto AccessSizeOf = [&](Type *Ty) -> uint64_t {
    if (!ClCheckWholeAccess || !Ty->isSized())
      return 0;
    uint64_t Sz = DL.getTypeAllocSize(Ty).getFixedValue();
    return Sz ? Sz - 1 : 0;
  };
  for (Instruction &I : instructions(F)) {
    if (I.getMetadata(LLVMContext::MD_nosanitize))
      continue;
    if (auto *LD = dyn_cast<LoadInst>(&I))
      LoadStores.emplace_back(&I, LD->getPointerOperand(), kInfoRead,
                              AccessSizeOf(LD->getType()));
    else if (auto *ST = dyn_cast<StoreInst>(&I))
      LoadStores.emplace_back(&I, ST->getPointerOperand(), kInfoWrite,
                              AccessSizeOf(ST->getValueOperand()->getType()));
    else if (auto *MI = dyn_cast<MemIntrinsic>(&I))
      MemIntrs.push_back(MI);
    else if (auto *CB = dyn_cast<CallBase>(&I))
      LibCalls.push_back(CB); // filtered in replaceLibFunc
  }

  // Phase 2: load/store bounds checks.
  for (auto &[I, Ptr, Info, AccessSize] : LoadStores)
    Changed |= checkAccess(I, Ptr, Info, AccessSize);
  // Phase 3: mem-intrinsic end-pointer checks.
  for (MemIntrinsic *MI : MemIntrs)
    Changed |= instrumentMemIntrinsic(MI);
  // Phase 4 + 5: replaceUnsafeLibFuncs, then optimizeMalloc on the result.
  for (CallBase *CB : LibCalls)
    if (replaceLibFunc(CB)) {
      Changed = true;
      optimizeMalloc(CB); // CB may be erased
    }
  return Changed;
}

PreservedAnalyses FlexFatPass::run(Function &F, FunctionAnalysisManager &AM) {
  if (F.isDeclaration())
    return PreservedAnalyses::all();
  if (isBlacklisted(F))
    return PreservedAnalyses::all(); // -flexfat-no-check-blacklist
  const TargetLibraryInfo &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  bool Changed = FlexFat(F, TLI).run();
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

//===----------------------------------------------------------------------===//
// Unit 13: global-variable lowfatification (module pass).
//===----------------------------------------------------------------------===//

// Largest global the lowfat region scheme can hold per region (per
// lowfat_config.h: LOWFAT_MAX_GLOBAL_ALLOC_SIZE = 64 MiB). The reference's
// check is `clzll(size) <= clzll(MAX) = 37` ⇒ too big.
constexpr uint64_t kMaxGlobalAllocSize = 67108864; // 64 MiB

// LowFat.cpp:1435-1458 — port verbatim. The reference uses getAlignment()
// (legacy MaybeAlign-as-uint); under modern LLVM that's getAlign().value().
// All exclusions checked here; size-cap + Common-promotion live in the
// transform itself per the reference.
static bool isInterestingGlobal(GlobalVariable *GV) {
  if (ClNoReplaceGlobals)
    return false;
  if (GV->hasSection())                 // user-declared section
    return false;
  if (GV->getAlign().valueOrOne().value() > 16) // user-declared alignment > 16
    return false;
  if (GV->isThreadLocal())              // TLS not supported
    return false;
  switch (GV->getLinkage()) {
  case GlobalValue::ExternalLinkage:
  case GlobalValue::InternalLinkage:
  case GlobalValue::PrivateLinkage:
  case GlobalValue::WeakAnyLinkage:
  case GlobalValue::WeakODRLinkage:
  case GlobalValue::CommonLinkage:
    break;
  default:
    return false;                       // no "fancy" linkage
  }
  return true;
}

// LowFat.cpp:1467-1505 — port verbatim. Skips declarations; warns and skips
// oversized globals; promotes Common to WeakAny (linker would otherwise drop
// the section attribute on Common symbols); sets alignment to the class
// boundary and writes the lowfat section name.
static bool makeGlobalVariableLowFatPtr(Module &M, GlobalVariable *GV) {
  if (GV->isDeclaration())
    return false;
  if (!isInterestingGlobal(GV))
    return false;

  // Common linkage ⇒ linker ignores `section` attr and places in BSS. Promote
  // to WeakAny so the section sticks. (May break legacy code that depends on
  // common-symbol merge semantics; same behavior as the reference.)
  if (GV->hasCommonLinkage())
    GV->setLinkage(GlobalValue::WeakAnyLinkage);

  const DataLayout &DL = M.getDataLayout();
  Type *Ty = GV->getValueType();
  uint64_t size = DL.getTypeAllocSize(Ty).getFixedValue();
  if (size == 0)
    return false;
  uint64_t idx = (uint64_t)__builtin_clzll(size);
  if (idx <= (uint64_t)__builtin_clzll(kMaxGlobalAllocSize)) {
    M.getContext().diagnose(FlexFatDiag(
        "FlexFat: global '" + GV->getName() +
        "' cannot be made low-fat (size > LOWFAT_MAX_GLOBAL_ALLOC_SIZE)"));
    return false;
  }

  uint64_t newSize = kStackSizes[idx];
  uint64_t mask = kStackMasks[idx];
  uint64_t alignBytes = (uint64_t)(~mask) + 1;
  if (Align(alignBytes) > GV->getAlign().valueOrOne())
    GV->setAlignment(Align(alignBytes));

  std::string section("lowfat_section_");
  if (GV->isConstant())
    section += "const_";
  section += std::to_string(newSize);
  GV->setSection(section);
  return true;
}

PreservedAnalyses FlexFatGlobalsPass::run(Module &M, ModuleAnalysisManager &) {
  if (ClNoReplaceGlobals)
    return PreservedAnalyses::all();
  bool Changed = false;
  // Snapshot the global list first — makeGlobalVariableLowFatPtr can mutate
  // linkage, which on some LLVM revisions perturbs iteration of M.globals().
  SmallVector<GlobalVariable *, 16> Worklist;
  for (GlobalVariable &GV : M.globals())
    Worklist.push_back(&GV);
  for (GlobalVariable *GV : Worklist)
    Changed |= makeGlobalVariableLowFatPtr(M, GV);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
