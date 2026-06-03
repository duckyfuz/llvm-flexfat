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

// Forward-declared: stack/global lowfatification is Part II; these flags will
// gate it then. Inert today (no alloca/global replacement is emitted).
static cl::opt<bool> ClNoReplaceAlloca(
    "flexfat-no-replace-alloca", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: do not lowfatify stack allocations (Part II; inert)"));
static cl::opt<bool> ClNoReplaceGlobals(
    "flexfat-no-replace-globals", cl::Hidden, cl::init(false),
    cl::desc("FlexFat: do not lowfatify globals (Part II; inert)"));

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
    B = getPtrBounds(GEP->getPointerOperand());
    // -flexfat-no-check-fields: trust an input-pointer base (empty bounds) up to
    // the indexed object -- the opaque-pointer analog of [0, sizeof(*ptr)].
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
    Base = calcBasePtr(GEP->getPointerOperand());
  } else if (auto *BC = dyn_cast<BitCastInst>(Ptr)) {
    Base = calcBasePtr(BC->getOperand(0));
  } else if (auto *ASC = dyn_cast<AddrSpaceCastInst>(Ptr)) {
    Base = calcBasePtr(ASC->getOperand(0));
  } else if (isa<AllocaInst>(Ptr)) {
    // Stack lowfatification is a later unit; treat allocas as non-fat for now.
    Base = NonFat;
  } else if (isa<Constant>(Ptr)) {
    // Globals/constants are not lowfatified yet; non-fat.
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

bool FlexFat::run() {
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

  bool Changed = false;
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
