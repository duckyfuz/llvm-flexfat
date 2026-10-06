//===- FlexFatSanitizer.cpp - FlexFat Pointer Bounds Checking
//---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the FlexFat Sanitizer instrumentation pass.
//
// FlexFat pointers encode allocation bounds information directly in the pointer
// value through careful memory layout. This pass instruments memory accesses
// to call runtime functions that verify bounds using this encoded information.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/FlexFatSanitizer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

#include <algorithm>
#include <optional>

// When the build generates a custom size-class config, pull in the tables so
// the pass can emit the right IR (AND vs. 128-bit magic multiply).
#ifdef FLEXFAT_CUSTOM_CONFIG
#include "flexfat_config_generated.h"
#endif

using namespace llvm;

#define DEBUG_TYPE "flexfat"

static cl::opt<bool> ClShareContainedGeometry(
    "flexfat-share-contained-geometry", cl::Hidden, cl::init(true),
    cl::desc("Share temporal geometry for accesses proven within an allocation"));
static cl::opt<bool> ClVersionTBILoops(
    "flexfat-version-tbi-loops", cl::Hidden, cl::init(false),
    cl::desc("Extend TBI loop versioning with grouped access ranges"));

STATISTIC(NumInstrumentedLoads, "Number of loads instrumented");
STATISTIC(NumInstrumentedStores, "Number of stores instrumented");
STATISTIC(NumInstrumentedAtomics, "Number of atomic operations instrumented");
STATISTIC(NumInstrumentedMemIntrinsics,
          "Number of mem intrinsics instrumented");
STATISTIC(NumInstrumentedEscapes, "Number of pointer escapes instrumented");
STATISTIC(NumTBILoopCandidates, "Number of TBI loop candidates");
STATISTIC(NumTBILoopEligibleAccesses, "TBI affine accesses collected");
STATISTIC(NumTBILoopRejectedSize, "TBI loops rejected by size limits");
STATISTIC(NumTBILoopRejectedFew, "TBI loops with too few affine accesses");
STATISTIC(NumTBILoopRejectedTrip, "TBI loops without expandable trip counts");
STATISTIC(NumTBILoopRejectedControl, "TBI loops with unsupported control flow");
STATISTIC(NumTBILoopHoistedGeometry, "TBI root geometry records hoisted");
STATISTIC(NumTBILoopRemovedSpatial, "TBI loop spatial checks removed");

namespace {

static bool shouldInstrumentFunction(const Function &F) {
  return !F.isDeclaration() && !F.empty() &&
         !F.getName().starts_with("__flexfat_") &&
         !F.hasFnAttribute(Attribute::DisableSanitizerInstrumentation) &&
         !F.hasFnAttribute("no-sanitize-flexfat");
}

template <typename Callable>
static bool invalidateTemporalAttributes(Callable &C) {
  AttributeList Before = C.getAttributes();
  C.setMemoryEffects(MemoryEffects::unknown());
  C.removeFnAttr(Attribute::Speculatable);
  C.removeFnAttr(Attribute::NoSync);
  C.removeFnAttr(Attribute::NoFree);
  return Before != C.getAttributes();
}

enum class CheckKind {
  Read,
  Write,
  MemIntrinsicRead,
  MemIntrinsicWrite,
  CallEscape,
  Deallocation,
  ReturnEscape,
  StoreEscape,
  IntegerEscape,
  AggregateEscape,
};

enum class BaseKind {
  /// The companion base is an allocation result propagated through SSA.
  StaticKnown,
  /// The companion base must be recovered from the input pointer at run time.
  Dynamic,
  /// Stack, global, null, and otherwise statically non-FlexFat pointers.
  NonFlexFat,
};

struct BoundsRecord {
  Value *CheckedPointer = nullptr;
  Value *CompanionBase = nullptr;
  BaseKind Kind = BaseKind::Dynamic;
  /// LowFat's approximate inclusive upper bound from CheckedPointer.  A
  /// missing value is Bounds::UNKNOWN_BOUND in the LLVM 4 implementation.
  std::optional<uint64_t> StaticUpperBound;
};

struct AccessRecord {
  Instruction *Inst;
  Value *Pointer;
  Value *Length;
  bool Write;
  uint64_t SpatialWidth;
};

// Immutable address geometry only. A generation observation must never be
// stored here: it belongs to the individual covered memory access.
struct GeometryRecord {
  Value *Tagged = nullptr;
  Value *Raw = nullptr;
  Value *Index = nullptr;
  Value *Size = nullptr;
  Value *Slot = nullptr;
  Value *Base = nullptr;
  Value *Managed = nullptr;
  Value *Entry = nullptr;
  Instruction *PrepareBefore = nullptr;
  // Custom-mode loop guards prove that affine accesses retain a nonzero
  // root tag. This does not cache the observed generation.
  bool NonzeroTagProven = false;
};

struct SelectProvenance {
  Value *Root = nullptr;
  BaseKind Kind = BaseKind::Dynamic;
  std::optional<uint64_t> StaticUpperBound;
};

/// Helper class to instrument a module with FlexFat bounds checks.
class FlexFatSanitizer {
public:
  FlexFatSanitizer(Module &M, const FlexFatSanitizerOptions &Options)
      : M(M), Options(Options), DL(M.getDataLayout()),
        TLII(M.getTargetTriple()), TLI(TLII),
        IntptrTy(DL.getIntPtrType(M.getContext())), TablesBase(kTablesBase) {
    if (Options.TemporalTBI &&
        (M.getTargetTriple().getArch() != Triple::aarch64 ||
         !M.getTargetTriple().isOSLinux() ||
         M.getTargetTriple().getEnvironment() == Triple::GNUILP32 ||
         !DL.isLittleEndian() ||
         DL.getPointerSizeInBits() != 64))
      report_fatal_error("FlexFat TBI requires little-endian Linux AArch64 with 64-bit pointers");
  }

  bool run();
  bool runFunction(Function &F);

private:
  Module &M;
  const FlexFatSanitizerOptions &Options;
  const DataLayout &DL;
  TargetLibraryInfoImpl TLII;
  TargetLibraryInfo TLI;
  Type *IntptrTy;
  const uint64_t TablesBase;
  bool usesInSlotTags() const {
    return Options.TemporalTBI &&
           Options.Storage != FlexFatSanitizerOptions::TBIStorage::Shadow;
  }
  DenseMap<Value *, BoundsRecord> Bounds;
  DenseMap<Value *, Value *> RecoveredBases;
  DenseMap<Value *, Value *> RecoveredBaseOrigins;
  DenseMap<Value *, Value *> SafeTableIndices;
  DenseMap<Value *, GeometryRecord> Geometries;
  DenseMap<Value *, Value *> BaseSizes;
  DenseMap<Instruction *, Value *> AccessLengths;
  DenseMap<std::pair<Instruction *, Value *>, GeometryRecord> AccessGeometry;
  DenseMap<Instruction *, GeometryRecord> FastGeometry;
  SmallPtrSet<Instruction *, 16> RangeProven;
  DenseMap<std::pair<Instruction *, Value *>, GeometryRecord> SpatialGeometry;
  SmallPtrSet<BasicBlock *, 16> CountedLoopHeaders;
  bool versionLoop(Function &F, DominatorTree &DT, LoopInfo &LI,
                   ScalarEvolution &SE);
  void prepareLoopGeometry(ArrayRef<AccessRecord> Accesses, DominatorTree &DT,
                           LoopInfo &LI);
  void prepareContainedGeometry(ArrayRef<AccessRecord> Accesses,
                                DominatorTree &DT);
  Value *getContainedAllocationRoot(const AccessRecord &Access);
  GeometryRecord &getGeometry(IRBuilder<> &B, Value *Ptr);
  GeometryRecord emitGeometry(IRBuilder<> &B, Value *Ptr, bool Temporal = true);
  void materializeTemporalGeometry(IRBuilder<> &B, GeometryRecord &G);
  GeometryRecord &getTemporalGeometry(Value *Ptr);
  unsigned BoundsIRGeneration = 0;

  FunctionCallee ReportOobFn = nullptr;
  FunctionCallee WarnOobFn = nullptr;

  FunctionCallee getReportOobFn();
  FunctionCallee getWarnOobFn();

  bool instrumentFunction(Function &F);
  void discoverAccesses(Instruction *I,
                        SmallVectorImpl<AccessRecord> &Accesses);
  bool instrumentTemporal(const AccessRecord &Access);
  Value *rawPointer(IRBuilder<> &IRB, Value *Ptr, const Twine &Name = "") {
    if (auto It = Geometries.find(Ptr);
        It != Geometries.end() && It->second.Raw)
      return It->second.Raw;
    Value *Int = IRB.CreatePtrToInt(Ptr, IntptrTy, Name);
    return Options.TemporalTBI
        ? IRB.CreateAnd(Int, ConstantInt::get(IntptrTy, 0x00ffffffffffffffULL),
                        "flexfat.raw") : Int;
  }
  bool instrumentMemoryAccess(Instruction *I, Value *Ptr, Type *AccessTy);
  bool instrumentMemoryRange(Instruction *I, Value *Ptr, Value *Size,
                             CheckKind Kind);
  bool instrumentPointerEscape(Instruction *I, Value *Ptr, CheckKind Kind);
  bool instrumentPointerCheck(Instruction *I, Value *Ptr,
                              uint64_t FixedAccessSize, Value *DynAccessSize,
                              CheckKind Kind);
  void prepareBounds(Instruction *I);
  BoundsRecord getBounds(Value *Ptr);
  SelectProvenance analyzeSelectOperand(Value *Ptr);
  std::optional<uint64_t> getAllocationUpperBound(Value *Ptr);
  Value *getRecoveredBase(Value *CompanionBase);
  Value *getSafeTableIndex(IRBuilder<> &IRB, Value *PtrInt);
  Value *getMemoizedTableIndex(Value *CompanionBase) const;
  bool isAllocationResult(Value *Ptr) const;
  bool isDirectAllocationBase(Value *Ptr) const;
  bool isDefinitelyNonFlexFat(Value *Ptr, SmallPtrSetImpl<Value *> &Seen) const;
  bool doesIntEscape(Value *V, SmallPtrSetImpl<Value *> &Seen) const;
  std::optional<unsigned> getConsumedOperandIndex(const CallBase &CB) const;
  static bool isEscapeCheck(CheckKind Kind);
  static bool isWriteCheck(CheckKind Kind);
  bool shouldSkipInstruction(const Instruction *I) const;
  void markInstrumented(Instruction *I);
  void markNoSanitize(Instruction *I);
  MDNode *getInstrumentedMetadata();
  MDNode *getNoSanitizeMetadata();

  // Emit the OOB-check block given a pre-computed (Base, AllocSize, PtrInt).
  void emitOobCheck(IRBuilder<> &IRB, Value *PtrInt, Value *Base,
                    Value *AllocSize, uint64_t FixedAccessSize,
                    Value *DynAccessSize, Instruction *InsertBefore,
                    CheckKind Kind);

#ifdef FLEXFAT_CUSTOM_CONFIG
  // Build the IR to compute (AllocSize, Base) using runtime table lookups when
  // the region index is only known at runtime.
  std::pair<Value *, Value *>
  emitDynamicSlotMagic(IRBuilder<> &IRB, Value *PtrInt, Value *RegionIndex);

#endif

  // Helper: GEP + load from a fixed absolute base at runtime index.
  Value *loadFromFixedTable(IRBuilder<> &IRB, uint64_t TableBase, Type *ElemTy,
                            Value *Idx) {
    LLVMContext &Ctx = M.getContext();
    Type *I64Ty = Type::getInt64Ty(Ctx);
    Value *BasePtr = IRB.CreateIntToPtr(ConstantInt::get(I64Ty, TableBase),
                                        PointerType::getUnqual(Ctx));
    Value *Idx64 = IRB.CreateZExtOrTrunc(Idx, I64Ty);
    Value *GEP = IRB.CreateInBoundsGEP(ElemTy, BasePtr, {Idx64});
    auto *Load = IRB.CreateLoad(ElemTy, GEP);
    if (Options.TemporalTBI)
      Load->setMetadata(LLVMContext::MD_invariant_load, MDNode::get(Ctx, {}));
    return Load;
  }

  // Constants (kept in sync with flexfat_config.h / flexfat_config_generated.h)
#ifdef FLEXFAT_CUSTOM_CONFIG
  static constexpr uint64_t RegionBase = FLEXFAT_REGION_BASE;
  static constexpr uint64_t RegionSizeLog = FLEXFAT_REGION_SIZE_LOG;
  static constexpr uint64_t NumSizeClasses = FLEXFAT_NUM_SIZE_CLASSES;
  static constexpr uint64_t kTablesBase = FLEXFAT_TABLES_BASE;
#else
  static constexpr uint64_t RegionBase = 0x100000000000ULL;
  static constexpr uint64_t RegionSizeLog = 32;
  static constexpr uint64_t NumSizeClasses =
      27; // kMaxSizeLog(30) - kMinSizeLog(4) + 1
  static constexpr uint64_t kTablesBase = 0x118000000000ULL;
#endif

  static constexpr uint64_t kTablesOffset = 0x1000000ULL;
#ifndef FLEXFAT_CUSTOM_CONFIG
  static constexpr uint64_t kTemporalShadowOffset = 0x200000000000ULL;
#endif
  static constexpr uint64_t UserAddressLimit = 1ULL << 48;
  static constexpr uint64_t NumTableEntries = UserAddressLimit >> RegionSizeLog;
  static constexpr uint64_t ManagedTableBegin = RegionBase >> RegionSizeLog;

  static_assert(NumTableEntries * sizeof(uint64_t) <= kTablesOffset,
                "FlexFat metadata table exceeds its fixed mapping");
  static_assert(ManagedTableBegin + NumSizeClasses <= NumTableEntries,
                "FlexFat managed regions exceed the 48-bit metadata table");
  static_assert(RegionBase + (NumSizeClasses << RegionSizeLog) <= kTablesBase,
                "FlexFat managed regions overlap fixed metadata");
  static_assert(kTablesBase + 4 * kTablesOffset <= UserAddressLimit,
                "FlexFat fixed metadata exceeds the 48-bit address space");
#ifndef FLEXFAT_CUSTOM_CONFIG
  static_assert(kTemporalShadowOffset + (1ULL << 44) <= UserAddressLimit,
                "FlexFat direct shadow exceeds the user address range");
#endif

  MDNode *InstrumentedMD = nullptr;
  MDNode *NoSanitizeMD = nullptr;
};

FunctionCallee FlexFatSanitizer::getReportOobFn() {
  if (!ReportOobFn) {
    // void __flexfat_report_oob(uptr ptr, uptr base, uptr size, i8 is_write)
    Type *VoidTy = Type::getVoidTy(M.getContext());
    Type *I8Ty = Type::getInt8Ty(M.getContext());
    ReportOobFn = M.getOrInsertFunction(
        "__flexfat_report_oob",
        FunctionType::get(VoidTy, {IntptrTy, IntptrTy, IntptrTy, I8Ty}, false));
    if (auto *F = dyn_cast<Function>(ReportOobFn.getCallee())) {
      F->addFnAttr(Attribute::NoReturn);
      // inaccessibleMemOnly: prevents the branch from being eliminated
      // as "dead" if the optimizer can't prove OOB is impossible.
      F->setMemoryEffects(MemoryEffects::inaccessibleMemOnly());
    }
  }
  return ReportOobFn;
}

FunctionCallee FlexFatSanitizer::getWarnOobFn() {
  if (!WarnOobFn) {
    // void __flexfat_warn_oob(uptr ptr, uptr base, uptr size, i8 is_write)
    Type *VoidTy = Type::getVoidTy(M.getContext());
    Type *I8Ty = Type::getInt8Ty(M.getContext());
    WarnOobFn = M.getOrInsertFunction(
        "__flexfat_warn_oob",
        FunctionType::get(VoidTy, {IntptrTy, IntptrTy, IntptrTy, I8Ty}, false));
    if (auto *F = dyn_cast<Function>(WarnOobFn.getCallee())) {
      F->addFnAttr(Attribute::NoUnwind);
      F->setMemoryEffects(MemoryEffects::inaccessibleMemOnly());
    }
  }
  return WarnOobFn;
}

#ifdef FLEXFAT_CUSTOM_CONFIG
// ---------------------------------------------------------------------------
// emitDynamicSlotMagic
//
// Given a runtime RegionIndex, emit IR that loads the per-class size and
// magic from the embedded tables and returns (AllocSize, Base) as IntptrTy.
//
// Generated IR (conceptually):
//
//   %alloc_size = load i64, ptr getelementptr(__flexfat_gen_sizes, 0,
//   %region_idx) %magic      = load i64, ptr
//   getelementptr(__flexfat_gen_magics, 0, %region_idx)
//
//   ; Reciprocal fixed-point base recovery for every class
//   %ptr128     = zext i64 %ptr to i128
//   %magic128   = zext i64 %magic to i128
//   %mul128     = mul i128 %ptr128, %magic128
//   %idx128     = lshr i128 %mul128, 64
//   %idx        = trunc i128 %idx128 to i64
//   %candidate  = mul i64 %idx, %alloc_size
//   %too_high   = icmp ugt i64 %candidate, %ptr
//   %slot       = sub i64 %idx, (zext i1 %too_high to i64)
//   %base       = mul i64 %slot, %alloc_size
// ---------------------------------------------------------------------------
std::pair<Value *, Value *>
FlexFatSanitizer::emitDynamicSlotMagic(IRBuilder<> &IRB, Value *PtrInt,
                                     Value *RegionIndex) {
  LLVMContext &Ctx = M.getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  Type *I128Ty = Type::getInt128Ty(Ctx);

  Value *AllocSize64 = loadFromFixedTable(IRB, TablesBase + 0 * kTablesOffset,
                                          I64Ty, RegionIndex);

  // Narrow to IntptrTy (which is i64 on 64-bit targets)
  Value *AllocSize = IRB.CreateZExtOrTrunc(AllocSize64, IntptrTy);

  // In custom-config mode we deliberately use the reciprocal-multiply path
  // for every class, including power-of-two sizes, so runtime and
  // instrumentation recover bases the same way.
  Value *Magic64 = loadFromFixedTable(IRB, TablesBase + 1 * kTablesOffset,
                                      I64Ty, RegionIndex);

  Value *Ptr128 = IRB.CreateZExt(PtrInt, I128Ty);
  Value *Magic128 =
      IRB.CreateZExt(IRB.CreateZExtOrTrunc(Magic64, IntptrTy), I128Ty);
  Value *Mul128 = IRB.CreateMul(Ptr128, Magic128);
  Value *Idx128 = IRB.CreateLShr(Mul128, ConstantInt::get(I128Ty, 64));
  Value *Idx = IRB.CreateTrunc(Idx128, IntptrTy);
  // Supported addresses are below 2^48 and sizes are at most region_size / 4.
  // The at-most-one-high quotient therefore has a non-overflowing product.
  Value *Candidate = IRB.CreateMul(Idx, AllocSize, "flexfat.base.candidate");
  Value *QuotientTooHigh =
      IRB.CreateICmpUGT(Candidate, PtrInt, "flexfat.quotient.high");
  Value *Slot = IRB.CreateSub(Idx, IRB.CreateZExt(QuotientTooHigh, IntptrTy),
                              "flexfat.slot");
  return {AllocSize, Slot};
}

#endif // FLEXFAT_CUSTOM_CONFIG

// Emit the OOB-check block given a pre-computed (Base, AllocSize, PtrInt).
void FlexFatSanitizer::emitOobCheck(IRBuilder<> &IRB, Value *PtrInt,
                                    Value *Base, Value *AllocSize,
                                    uint64_t FixedAccessSize,
                                    Value *DynAccessSize,
                                    Instruction *InsertBefore, CheckKind Kind) {
  Value *AccessBound = usesInSlotTags() && !isEscapeCheck(Kind)
                           ? IRB.CreateSub(AllocSize, IRB.getInt64(1))
                           : AllocSize;
  Value *IsOOB = nullptr;
  if (!FixedAccessSize && !DynAccessSize) {
    // The allocator reserves a trailing byte, so legal requested-object
    // one-past pointers remain strictly inside the slot. Reject slot-boundary
    // escapes before storing/reloading them can recover a neighbouring base.
    Value *Diff = IRB.CreateSub(PtrInt, Base);
    IsOOB = IRB.CreateICmpUGE(Diff, AccessBound);
  } else {
    Value *AccessSize = DynAccessSize;
    if (!AccessSize)
      AccessSize = ConstantInt::get(IntptrTy, FixedAccessSize);
    Value *Diff = IRB.CreateSub(PtrInt, Base);
    Value *TooWide = IRB.CreateICmpUGT(AccessSize, AccessBound);
    Value *Limit = IRB.CreateSub(AccessBound, AccessSize);
    Value *PastEnd = IRB.CreateICmpUGT(Diff, Limit);
    IsOOB = IRB.CreateOr(TooWide, PastEnd);
  }

  Instruction *OobTerm =
      SplitBlockAndInsertIfThen(IsOOB, InsertBefore, /*Unreachable=*/false);
  markNoSanitize(OobTerm);
  IRBuilder<> OobIRB(OobTerm);
  OobIRB.SetNoSanitizeMetadata();
  FunctionCallee OobFn = Options.Recover ? getWarnOobFn() : getReportOobFn();
  Type *I8Ty = Type::getInt8Ty(M.getContext());
  Value *IsWriteVal = ConstantInt::get(I8Ty, isWriteCheck(Kind) ? 1 : 0);
  OobIRB.CreateCall(OobFn, {PtrInt, Base, AccessBound, IsWriteVal});
}

bool FlexFatSanitizer::shouldSkipInstruction(const Instruction *I) const {
  return I->hasMetadata(LLVMContext::MD_nosanitize) ||
         I->getMetadata("flexfat.instrumented");
}

void FlexFatSanitizer::markInstrumented(Instruction *I) {
  I->setMetadata("flexfat.instrumented", getInstrumentedMetadata());
}

void FlexFatSanitizer::markNoSanitize(Instruction *I) {
  I->setMetadata(LLVMContext::MD_nosanitize, getNoSanitizeMetadata());
}

MDNode *FlexFatSanitizer::getInstrumentedMetadata() {
  if (!InstrumentedMD)
    InstrumentedMD = MDNode::get(M.getContext(), {});
  return InstrumentedMD;
}

MDNode *FlexFatSanitizer::getNoSanitizeMetadata() {
  if (!NoSanitizeMD)
    NoSanitizeMD = MDNode::get(M.getContext(), {});
  return NoSanitizeMD;
}

bool FlexFatSanitizer::isAllocationResult(Value *Ptr) const {
  auto *CB = dyn_cast<CallBase>(Ptr);
  if (!CB)
    return false;
  const Value *Callee = CB->getCalledOperand()->stripPointerCasts();
  const auto *F = dyn_cast<Function>(Callee);
  if (!F)
    return false;
  StringRef Name = F->getName();
  return Name == "malloc" || Name == "calloc" || Name == "realloc" ||
         Name == "aligned_alloc" || Name == "valloc" || Name == "memalign" ||
         Name == "pvalloc" || Name == "strdup" || Name == "strndup" ||
         Name == "_Znwm" || Name == "_Znam" || Name == "_ZnwmRKSt9nothrow_t" ||
         Name == "_ZnamRKSt9nothrow_t" || Name == "_Znwj" || Name == "_Znaj" ||
         Name == "_ZnwjRKSt9nothrow_t" || Name == "_ZnajRKSt9nothrow_t" ||
         Name == "_ZnwmSt11align_val_t" || Name == "_ZnamSt11align_val_t" ||
         Name == "_ZnwmSt11align_val_tRKSt9nothrow_t" ||
         Name == "_ZnamSt11align_val_tRKSt9nothrow_t" ||
         Name == "_ZnwjSt11align_val_t" || Name == "_ZnajSt11align_val_t" ||
         Name == "_ZnwjSt11align_val_tRKSt9nothrow_t" ||
         Name == "_ZnajSt11align_val_tRKSt9nothrow_t";
}

bool FlexFatSanitizer::isDirectAllocationBase(Value *Ptr) const {
  if (Options.AllocationAlignment == FlexFatSanitizerOptions::Alignment::Right)
    return false;

  auto *CB = dyn_cast<CallBase>(Ptr);
  if (!CB)
    return false;
  const auto *F =
      dyn_cast<Function>(CB->getCalledOperand()->stripPointerCasts());
  if (!F)
    return false;
  StringRef Name = F->getName();
  return Name == "malloc" || Name == "calloc" || Name == "realloc" ||
         Name == "strdup" || Name == "strndup" || Name == "_Znwm" ||
         Name == "_Znam" || Name == "_ZnwmRKSt9nothrow_t" ||
         Name == "_ZnamRKSt9nothrow_t" || Name == "_Znwj" || Name == "_Znaj" ||
         Name == "_ZnwjRKSt9nothrow_t" || Name == "_ZnajRKSt9nothrow_t";
}

bool FlexFatSanitizer::isDefinitelyNonFlexFat(
    Value *Ptr, SmallPtrSetImpl<Value *> &Seen) const {
  if (!Seen.insert(Ptr).second)
    return true;
  if (isa<AllocaInst>(Ptr) || isa<GlobalValue>(Ptr) ||
      isa<ConstantPointerNull>(Ptr) || isa<UndefValue>(Ptr) ||
      isa<PoisonValue>(Ptr))
    return true;
  if (auto *GEP = dyn_cast<GEPOperator>(Ptr))
    return isDefinitelyNonFlexFat(GEP->getPointerOperand(), Seen);
  if (auto *Cast = dyn_cast<BitCastOperator>(Ptr))
    return isDefinitelyNonFlexFat(Cast->getOperand(0), Seen);
  if (auto *Cast = dyn_cast<AddrSpaceCastOperator>(Ptr))
    return isDefinitelyNonFlexFat(Cast->getOperand(0), Seen);
  if (auto *Freeze = dyn_cast<FreezeInst>(Ptr))
    return isDefinitelyNonFlexFat(Freeze->getOperand(0), Seen);
  if (auto *Select = dyn_cast<SelectInst>(Ptr))
    return isDefinitelyNonFlexFat(Select->getTrueValue(), Seen) &&
           isDefinitelyNonFlexFat(Select->getFalseValue(), Seen);
  if (auto *Phi = dyn_cast<PHINode>(Ptr)) {
    for (Value *Incoming : Phi->incoming_values())
      if (!isDefinitelyNonFlexFat(Incoming, Seen))
        return false;
    return true;
  }
  return false;
}

Value *FlexFatSanitizer::getRecoveredBase(Value *CompanionBase) {
  if (auto It = RecoveredBases.find(CompanionBase); It != RecoveredBases.end())
    return It->second;

  // A musttail result may only be followed by its return.  Recovery would be
  // dead for that zero-width return escape, and inserting it after the call
  // would invalidate the required musttail/ret pair.
  if (auto *Call = dyn_cast<CallInst>(CompanionBase);
      Call && Call->isMustTailCall()) {
    RecoveredBases[CompanionBase] = CompanionBase;
    return CompanionBase;
  }

  Function *F = nullptr;
  if (auto *I = dyn_cast<Instruction>(CompanionBase))
    F = I->getFunction();
  else if (auto *Arg = dyn_cast<Argument>(CompanionBase))
    F = Arg->getParent();
  assert(F && "dynamic FlexFat roots must belong to a function");

  Instruction *InsertBefore = nullptr;
  if (auto *Invoke = dyn_cast<InvokeInst>(CompanionBase)) {
    BasicBlock *NormalEdge =
        SplitEdge(Invoke->getParent(), Invoke->getNormalDest());
    assert(NormalEdge && "failed to split an invoke normal edge");
    InsertBefore = NormalEdge->getTerminator();
  } else if (isa<Argument>(CompanionBase)) {
    InsertBefore = &*F->getEntryBlock().getFirstInsertionPt();
  } else if (auto *Phi = dyn_cast<PHINode>(CompanionBase)) {
    InsertBefore = &*Phi->getParent()->getFirstNonPHIIt();
  } else if (auto *I = dyn_cast<Instruction>(CompanionBase)) {
    InsertBefore = I->getNextNode();
    assert(InsertBefore &&
           "pointer root must have a following insertion point");
  }

  IRBuilder<> IRB(InsertBefore);
  IRB.SetNoSanitizeMetadata();
  GeometryRecord &G = getGeometry(IRB, CompanionBase);
  Value *Base =
      IRB.CreateIntToPtr(G.Base, CompanionBase->getType(), "flexfat.base");
  RecoveredBases[CompanionBase] = Base;
  RecoveredBaseOrigins[Base] = CompanionBase;
  SafeTableIndices[Base] = G.Index;
  BaseSizes[Base] = G.Size;
  ++BoundsIRGeneration;
  return Base;
}

GeometryRecord &FlexFatSanitizer::getGeometry(IRBuilder<> &B, Value *Ptr) {
  auto &G = Geometries[Ptr];
  if (G.Raw)
    return G;
  G = emitGeometry(B, Ptr, false);
  G.PrepareBefore = &*B.GetInsertPoint();
  return G;
}

GeometryRecord &FlexFatSanitizer::getTemporalGeometry(Value *Ptr) {
  (void)getRecoveredBase(Ptr);
  auto &G = Geometries.find(Ptr)->second;
  if (!G.Entry) {
    IRBuilder<> B(G.PrepareBefore);
    B.SetNoSanitizeMetadata();
    materializeTemporalGeometry(B, G);
  }
  return G;
}

GeometryRecord FlexFatSanitizer::emitGeometry(IRBuilder<> &B, Value *Ptr,
                                              bool Temporal) {
  GeometryRecord G;
  G.Tagged = B.CreatePtrToInt(Ptr, IntptrTy, "flexfat.root.int");
  G.Raw = Options.TemporalTBI
              ? B.CreateAnd(G.Tagged, B.getInt64(0x00ffffffffffffffULL),
                            "flexfat.raw")
              : G.Tagged;
#ifdef FLEXFAT_CUSTOM_CONFIG
  G.Index = getSafeTableIndex(B, G.Raw);
  auto [Size, Slot] = emitDynamicSlotMagic(B, G.Raw, G.Index);
  G.Size = Size;
  G.Slot = Slot;
  G.Base = B.CreateMul(Slot, Size, "flexfat.base.int");
#else
  if (Options.TemporalTBI) {
    // Pow2 classes have size 2^(class+4). Derive immutable geometry from the
    // region instead of reading the size and mask tables on each root. All
    // foreign regions use the zero-index bias sentinel, including high ones.
    Value *Region = B.CreateLShr(G.Raw, B.getInt64(RegionSizeLog),
                                 "flexfat.region.raw");
    Value *Class = B.CreateSub(Region, B.getInt64(ManagedTableBegin),
                               "flexfat.class");
    G.Managed = B.CreateICmpULT(Class, B.getInt64(NumSizeClasses),
                                "flexfat.managed");
    G.Index = B.CreateSelect(G.Managed, Region, B.getInt64(0),
                              "flexfat.region");
    Value *Shift = B.CreateSelect(
        G.Managed, B.CreateAdd(Class, B.getInt64(4)), B.getInt64(0),
        "flexfat.size.shift");
    Value *ManagedSize = B.CreateShl(B.getInt64(1), Shift,
                                    "flexfat.managed.size");
    G.Size = B.CreateSelect(G.Managed, ManagedSize, B.getInt64(-1),
                            "flexfat.size");
    Value *Mask = B.CreateSelect(G.Managed, B.CreateNeg(ManagedSize),
                                 B.getInt64(0), "flexfat.mask");
    G.Base = B.CreateAnd(G.Raw, Mask, "flexfat.base.int");
  } else {
    G.Index = getSafeTableIndex(B, G.Raw);
    G.Size = B.CreateZExtOrTrunc(
        loadFromFixedTable(B, TablesBase, B.getInt64Ty(), G.Index), IntptrTy);
    Value *Mask = B.CreateZExtOrTrunc(
        loadFromFixedTable(B, TablesBase + 3 * kTablesOffset, B.getInt64Ty(),
                           G.Index),
        IntptrTy);
    Mask->setName("flexfat.mask");
    G.Base = B.CreateAnd(G.Raw, Mask, "flexfat.base.int");
  }
#endif
  if (Options.TemporalTBI && Temporal)
    materializeTemporalGeometry(B, G);
  return G;
}

// Temporal-only fields are requested lazily. Escape-only spatial preparation
// does not introduce a bias-table lookup, classification, or slot shift.
void FlexFatSanitizer::materializeTemporalGeometry(IRBuilder<> &B,
                                                   GeometryRecord &G) {
  if (G.Entry)
    return;
  if (!G.Managed)
    G.Managed = B.CreateICmpNE(G.Size, ConstantInt::getSigned(IntptrTy, -1),
                               "flexfat.managed");
  if (usesInSlotTags()) {
    Value *Valid = G.Managed;
#ifdef FLEXFAT_CUSTOM_CONFIG
    Value *Start = B.CreateShl(G.Index, B.getInt64(RegionSizeLog));
    Value *End = B.CreateAdd(Start, B.getInt64(1ULL << RegionSizeLog));
    Valid = B.CreateAnd(Valid, B.CreateICmpUGE(G.Base, Start));
    Valid = B.CreateAnd(Valid,
                        B.CreateICmpULE(G.Base, B.CreateSub(End, G.Size)));
#endif
    Value *Address = Options.Storage == FlexFatSanitizerOptions::TBIStorage::PriorByte
                         ? B.CreateSub(G.Base, B.getInt64(1),
                                       "flexfat.metadata.address")
                         : B.CreateAdd(G.Base,
                                       B.CreateSub(G.Size, B.getInt64(1)),
                                       "flexfat.metadata.address");
    Value *Sentinel = B.CreatePtrToInt(
        M.getOrInsertGlobal("__flexfat_tbi_zero_sentinel", B.getInt8Ty()),
        IntptrTy);
    G.Entry = B.CreateIntToPtr(B.CreateSelect(Valid, Address, Sentinel),
                               B.getPtrTy(), "flexfat.metadata");
    return;
  }
#ifndef FLEXFAT_CUSTOM_CONFIG
  Value *Address = B.CreateAdd(B.CreateLShr(G.Base, B.getInt64(4)),
                               B.getInt64(kTemporalShadowOffset),
                               "flexfat.metadata.address");
  G.Entry = B.CreateIntToPtr(Address, B.getPtrTy(), "flexfat.metadata");
#else
  Value *Slot =
      B.CreateSelect(G.Managed, G.Slot, B.getInt64(0), "flexfat.metadata.slot");
  Value *Bias = loadFromFixedTable(B, TablesBase + 2 * kTablesOffset,
                                   B.getInt64Ty(), G.Index);
  // Biases intentionally wrap. Do not use overflow flags or pointer GEPs.
  G.Entry =
      B.CreateIntToPtr(B.CreateAdd(Bias, Slot, "flexfat.metadata.address"),
                       B.getPtrTy(), "flexfat.metadata");
#endif
}

Value *FlexFatSanitizer::getMemoizedTableIndex(Value *CompanionBase) const {
  if (auto It = SafeTableIndices.find(CompanionBase);
      It != SafeTableIndices.end())
    return It->second;
  if (isa<ConstantPointerNull>(CompanionBase))
    return ConstantInt::get(IntptrTy, 0);
  return nullptr;
}

Value *FlexFatSanitizer::getSafeTableIndex(IRBuilder<> &IRB, Value *PtrInt) {
  Value *RawIndex = IRB.CreateLShr(
      PtrInt, ConstantInt::get(IntptrTy, RegionSizeLog), "flexfat.region.raw");
  Value *InUserAddressSpace =
      IRB.CreateICmpULT(PtrInt, ConstantInt::get(IntptrTy, UserAddressLimit),
                        "flexfat.address.valid");
  return IRB.CreateSelect(InUserAddressSpace, RawIndex,
                          ConstantInt::get(IntptrTy, 0), "flexfat.region");
}

std::optional<uint64_t> FlexFatSanitizer::getAllocationUpperBound(Value *Ptr) {
  uint64_t ObjectSize = 0;
  std::optional<uint64_t> StaticUpperBound = 0;
  if (getObjectSize(Ptr, ObjectSize, DL, &TLI))
    return ObjectSize;

  auto *CB = dyn_cast<CallBase>(Ptr);
  if (!CB)
    return StaticUpperBound;
  const auto *F =
      dyn_cast<Function>(CB->getCalledOperand()->stripPointerCasts());
  StringRef Name = F ? F->getName() : StringRef();
  unsigned SizeArg = 0;
  if (Name == "realloc" || Name == "aligned_alloc" || Name == "memalign")
    SizeArg = 1;
  if (Name == "calloc" && CB->arg_size() >= 2) {
    auto *Count = dyn_cast<ConstantInt>(CB->getArgOperand(0));
    auto *Size = dyn_cast<ConstantInt>(CB->getArgOperand(1));
    bool Overflow = false;
    if (Count && Size) {
      APInt Product = Count->getValue().umul_ov(Size->getValue(), Overflow);
      if (!Overflow && Product.getActiveBits() <= 64)
        StaticUpperBound = Product.getZExtValue();
    }
  } else if (SizeArg < CB->arg_size()) {
    if (auto *Size = dyn_cast<ConstantInt>(CB->getArgOperand(SizeArg));
        Size && Size->getValue().getActiveBits() <= 64)
      StaticUpperBound = Size->getZExtValue();
  }
  return StaticUpperBound;
}

SelectProvenance FlexFatSanitizer::analyzeSelectOperand(Value *Ptr) {
  SmallPtrSet<Value *, 16> NonFlexFatSeen;
  if (isDefinitelyNonFlexFat(Ptr, NonFlexFatSeen))
    return {nullptr, BaseKind::NonFlexFat, UINT64_MAX};

  if (auto *GEP = dyn_cast<GEPOperator>(Ptr)) {
    SelectProvenance Result = analyzeSelectOperand(GEP->getPointerOperand());
    if (Result.StaticUpperBound) {
      APInt Offset(DL.getIndexTypeSizeInBits(GEP->getType()), 0);
      if (GEP->accumulateConstantOffset(DL, Offset) && Offset.isNonNegative() &&
          Offset.getActiveBits() <= 64 &&
          Offset.getZExtValue() <= *Result.StaticUpperBound)
        *Result.StaticUpperBound -= Offset.getZExtValue();
      else
        Result.StaticUpperBound.reset();
    }
    return Result;
  }
  if (auto *Cast = dyn_cast<BitCastOperator>(Ptr))
    return analyzeSelectOperand(Cast->getOperand(0));
  if (auto *Cast = dyn_cast<AddrSpaceCastOperator>(Ptr))
    return analyzeSelectOperand(Cast->getOperand(0));
  if (auto *Freeze = dyn_cast<FreezeInst>(Ptr))
    return analyzeSelectOperand(Freeze->getOperand(0));
  if (isAllocationResult(Ptr))
    return {Ptr, BaseKind::StaticKnown, getAllocationUpperBound(Ptr)};
  return {Ptr, BaseKind::Dynamic, 0};
}

BoundsRecord FlexFatSanitizer::getBounds(Value *Ptr) {
  if (auto It = Bounds.find(Ptr); It != Bounds.end())
    return It->second;

  auto makeRecord = [Ptr](Value *Base, BaseKind Kind,
                          std::optional<uint64_t> StaticUpperBound = 0) {
    return BoundsRecord{Ptr, Base, Kind, StaticUpperBound};
  };
  auto nonFlexFat = [&]() {
    return makeRecord(
        ConstantPointerNull::get(cast<PointerType>(Ptr->getType())),
        BaseKind::NonFlexFat, UINT64_MAX);
  };

  SmallPtrSet<Value *, 16> NonFlexFatSeen;
  if (isDefinitelyNonFlexFat(Ptr, NonFlexFatSeen)) {
    BoundsRecord Result = nonFlexFat();
    Bounds[Ptr] = Result;
    return Result;
  }

  BoundsRecord Result;
  if (isAllocationResult(Ptr)) {
    Value *Base = isDirectAllocationBase(Ptr) ? Ptr : getRecoveredBase(Ptr);
    Result =
        makeRecord(Base, BaseKind::StaticKnown, getAllocationUpperBound(Ptr));
  } else if (auto *GEP = dyn_cast<GEPOperator>(Ptr)) {
    Result = getBounds(GEP->getPointerOperand());
    Result.CheckedPointer = Ptr;
    if (Result.StaticUpperBound) {
      APInt Offset(DL.getIndexTypeSizeInBits(GEP->getType()), 0);
      if (GEP->accumulateConstantOffset(DL, Offset) && Offset.isNonNegative() &&
          Offset.getActiveBits() <= 64 &&
          Offset.getZExtValue() <= *Result.StaticUpperBound)
        *Result.StaticUpperBound -= Offset.getZExtValue();
      else
        Result.StaticUpperBound.reset();
    }
  } else if (auto *Cast = dyn_cast<BitCastOperator>(Ptr)) {
    Result = getBounds(Cast->getOperand(0));
    Result.CheckedPointer = Ptr;
  } else if (auto *Cast = dyn_cast<AddrSpaceCastInst>(Ptr)) {
    Result = getBounds(Cast->getOperand(0));
    Result.CheckedPointer = Ptr;
    if (Result.Kind != BaseKind::NonFlexFat) {
      Value *OriginalBase = Result.CompanionBase;
      IRBuilder<> IRB(Cast->getNextNode());
      IRB.SetNoSanitizeMetadata();
      Result.CompanionBase = IRB.CreateAddrSpaceCast(
          Result.CompanionBase, Cast->getType(), "flexfat.base.cast");
      if (Value *Index = getMemoizedTableIndex(OriginalBase))
        SafeTableIndices[Result.CompanionBase] = Index;
      if (Value *Size = BaseSizes.lookup(OriginalBase))
        BaseSizes[Result.CompanionBase] = Size;
      BoundsIRGeneration += isa<Instruction>(Result.CompanionBase);
    } else {
      Result.CompanionBase =
          ConstantPointerNull::get(cast<PointerType>(Cast->getType()));
    }
  } else if (auto *Freeze = dyn_cast<FreezeInst>(Ptr)) {
    Result = getBounds(Freeze->getOperand(0));
    Result.CheckedPointer = Ptr;
  } else if (auto *Select = dyn_cast<SelectInst>(Ptr)) {
    SelectProvenance TrueInfo = analyzeSelectOperand(Select->getTrueValue());
    SelectProvenance FalseInfo = analyzeSelectOperand(Select->getFalseValue());
    std::optional<uint64_t> StaticUpperBound =
        TrueInfo.StaticUpperBound && FalseInfo.StaticUpperBound
            ? std::optional<uint64_t>(std::min(*TrueInfo.StaticUpperBound,
                                               *FalseInfo.StaticUpperBound))
            : std::nullopt;
    if (TrueInfo.Kind == BaseKind::NonFlexFat &&
        FalseInfo.Kind == BaseKind::NonFlexFat) {
      Result = nonFlexFat();
    } else if (TrueInfo.Root && TrueInfo.Root == FalseInfo.Root) {
      BoundsRecord RootBounds = getBounds(TrueInfo.Root);
      Value *Base = RootBounds.CompanionBase;
      if (Base->getType() != Select->getType()) {
        IRBuilder<> IRB(Select->getNextNode());
        IRB.SetNoSanitizeMetadata();
        Base = IRB.CreateAddrSpaceCast(Base, Select->getType(),
                                       "flexfat.base.cast");
        if (Value *Index = getMemoizedTableIndex(RootBounds.CompanionBase))
          SafeTableIndices[Base] = Index;
        if (Value *Size = BaseSizes.lookup(RootBounds.CompanionBase))
          BaseSizes[Base] = Size;
        BoundsIRGeneration += isa<Instruction>(Base);
      }
      Result = makeRecord(Base,
                          TrueInfo.Kind == FalseInfo.Kind ? TrueInfo.Kind
                                                          : BaseKind::Dynamic,
                          StaticUpperBound);
    } else {
      Result = makeRecord(getRecoveredBase(Select), BaseKind::Dynamic,
                          StaticUpperBound);
    }
  } else if (auto *Phi = dyn_cast<PHINode>(Ptr)) {
    BasicBlock::iterator InsertPt = Phi->getParent()->getFirstNonPHIIt();
    unsigned NumIncoming = Phi->getNumIncomingValues();
    auto HasDirectAllocation = [&](Value *V, auto &&Recurse,
                                   SmallPtrSetImpl<Value *> &Seen) -> bool {
      if (!Seen.insert(V).second)
        return false;
      if (isDirectAllocationBase(V))
        return true;
      if (auto *GEP = dyn_cast<GEPOperator>(V))
        return Recurse(GEP->getPointerOperand(), Recurse, Seen);
      if (auto *Cast = dyn_cast<BitCastOperator>(V))
        return Recurse(Cast->getOperand(0), Recurse, Seen);
      if (auto *Cast = dyn_cast<AddrSpaceCastOperator>(V))
        return Recurse(Cast->getOperand(0), Recurse, Seen);
      if (auto *Freeze = dyn_cast<FreezeInst>(V))
        return Recurse(Freeze->getOperand(0), Recurse, Seen);
      if (auto *Select = dyn_cast<SelectInst>(V))
        return Recurse(Select->getTrueValue(), Recurse, Seen) ||
               Recurse(Select->getFalseValue(), Recurse, Seen);
      if (auto *IncomingPhi = dyn_cast<PHINode>(V))
        for (Value *Incoming : IncomingPhi->incoming_values())
          if (Recurse(Incoming, Recurse, Seen))
            return true;
      return false;
    };
    bool UseIndexPhi = true;
    SmallPtrSet<Value *, 16> DirectSeen;
    for (Value *Incoming : Phi->incoming_values())
      if (HasDirectAllocation(Incoming, HasDirectAllocation, DirectSeen)) {
        UseIndexPhi = false;
        break;
      }

    auto *BasePhi =
        PHINode::Create(Phi->getType(), NumIncoming, "flexfat.base", InsertPt);
    PHINode *IndexPhi =
        UseIndexPhi
            ? PHINode::Create(IntptrTy, NumIncoming, "flexfat.region", InsertPt)
            : nullptr;
    PHINode *SizePhi = UseIndexPhi ? PHINode::Create(IntptrTy, NumIncoming,
                                                     "flexfat.size", InsertPt)
                                   : nullptr;
    markNoSanitize(BasePhi);
    if (IndexPhi) {
      markNoSanitize(IndexPhi);
      markNoSanitize(SizePhi);
      BaseSizes[BasePhi] = SizePhi;
    }
    BoundsIRGeneration += IndexPhi ? 3 : 1;
    Result = makeRecord(BasePhi, BaseKind::Dynamic, std::nullopt);
    Bounds[Ptr] = Result;
    if (IndexPhi)
      SafeTableIndices[BasePhi] = IndexPhi;

    // Complete the companion PHI's predecessor list before recursively
    // resolving any incoming value.  Recovering an invoke result splits its
    // normal edge, and SplitEdge updates every PHI in the destination block.
    // A partially constructed BasePhi would not yet have the edge that LLVM
    // is trying to rewrite.
    for (unsigned I = 0; I != NumIncoming; ++I)
      BasePhi->addIncoming(PoisonValue::get(Phi->getType()),
                           Phi->getIncomingBlock(I));
    if (IndexPhi)
      for (unsigned I = 0; I != NumIncoming; ++I)
        IndexPhi->addIncoming(PoisonValue::get(IntptrTy),
                              Phi->getIncomingBlock(I));
    if (SizePhi)
      for (unsigned I = 0; I != NumIncoming; ++I)
        SizePhi->addIncoming(PoisonValue::get(IntptrTy),
                             Phi->getIncomingBlock(I));

    bool AllNonFlexFat = true;
    std::optional<uint64_t> StaticUpperBound = UINT64_MAX;
    DenseMap<BasicBlock *, unsigned> IncomingEdges;
    for (unsigned I = 0; I != NumIncoming; ++I) {
      BoundsRecord Incoming = getBounds(Phi->getIncomingValue(I));
      // Duplicate switch edges from one predecessor must carry identical PHI
      // values. In particular, do not emit a separate size load for each edge.
      auto [Edge, IsNew] =
          IncomingEdges.try_emplace(Phi->getIncomingBlock(I), I);
      if (!IsNew) {
        BasePhi->setIncomingValue(I, BasePhi->getIncomingValue(Edge->second));
        if (IndexPhi) {
          IndexPhi->setIncomingValue(I,
                                     IndexPhi->getIncomingValue(Edge->second));
          SizePhi->setIncomingValue(I, SizePhi->getIncomingValue(Edge->second));
        }
      } else {
        BasePhi->setIncomingValue(I, Incoming.CompanionBase);
        if (IndexPhi) {
          Value *IncomingIndex = getMemoizedTableIndex(Incoming.CompanionBase);
          assert(IncomingIndex &&
                 "companion base is missing its safe table index");
          IndexPhi->setIncomingValue(I, IncomingIndex);
          Value *Size = BaseSizes.lookup(Incoming.CompanionBase);
          if (!Size) {
            IRBuilder<> B(Phi->getIncomingBlock(I)->getTerminator());
            B.SetNoSanitizeMetadata();
            Size = B.CreateZExtOrTrunc(loadFromFixedTable(B, TablesBase,
                                                          B.getInt64Ty(),
                                                          IncomingIndex),
                                       IntptrTy);
          }
          SizePhi->setIncomingValue(I, Size);
        }
      }
      AllNonFlexFat &= Incoming.Kind == BaseKind::NonFlexFat;
      if (StaticUpperBound && Incoming.StaticUpperBound)
        *StaticUpperBound =
            std::min(*StaticUpperBound, *Incoming.StaticUpperBound);
      else
        StaticUpperBound.reset();
    }
    if (AllNonFlexFat)
      Result.Kind = BaseKind::NonFlexFat;
    Result.StaticUpperBound = StaticUpperBound;
  } else if (auto *CE = dyn_cast<ConstantExpr>(Ptr)) {
    if (CE->isCast() && CE->getOperand(0)->getType()->isPointerTy()) {
      Result = getBounds(CE->getOperand(0));
      Result.CheckedPointer = Ptr;
    } else if (CE->getOpcode() == Instruction::IntToPtr) {
      auto *AddressConstant = dyn_cast<ConstantInt>(CE->getOperand(0));
      if (!AddressConstant) {
        Result = makeRecord(
            ConstantPointerNull::get(cast<PointerType>(Ptr->getType())),
            BaseKind::Dynamic, std::nullopt);
      } else {
        uint64_t Address = AddressConstant->getValue()
                               .zextOrTrunc(IntptrTy->getIntegerBitWidth())
                               .getZExtValue();
        uint64_t RegionIndex =
            Address < UserAddressLimit ? Address >> RegionSizeLog : 0;
        bool IsManaged = RegionIndex >= ManagedTableBegin &&
                         RegionIndex < ManagedTableBegin + NumSizeClasses;
        uint64_t BaseAddress = 0;
        if (IsManaged) {
          uint64_t ClassIndex = RegionIndex - ManagedTableBegin;
#ifdef FLEXFAT_CUSTOM_CONFIG
          uint64_t AllocSize = kFlexFatGenSizes[ClassIndex];
#else
          uint64_t AllocSize = 1ULL << (ClassIndex + 4);
#endif
          BaseAddress = Address - Address % AllocSize;
        } else {
          RegionIndex = 0;
        }

        Constant *Base = ConstantExpr::getIntToPtr(
            ConstantInt::get(IntptrTy, BaseAddress),
            cast<PointerType>(Ptr->getType()));
        SafeTableIndices[Base] = ConstantInt::get(IntptrTy, RegionIndex);
        Result = makeRecord(Base, BaseKind::Dynamic, std::nullopt);
      }
    } else {
      // Unknown constant expressions use the foreign-pointer sentinel.
      Result = makeRecord(
          ConstantPointerNull::get(cast<PointerType>(Ptr->getType())),
          BaseKind::Dynamic, std::nullopt);
    }
  } else {
    // Arguments, loads, inttoptr, aggregate extraction, and unknown call
    // results follow LowFat's input-pointer rule: recover the base from the
    // pointer itself rather than guessing its stored or calling provenance.
    Result = makeRecord(getRecoveredBase(Ptr), BaseKind::Dynamic, 0);
  }

  Bounds[Ptr] = Result;
  return Result;
}

bool FlexFatSanitizer::doesIntEscape(Value *V,
                                     SmallPtrSetImpl<Value *> &Seen) const {
  if (!Seen.insert(V).second)
    return false;
  for (User *U : V->users()) {
    if (isa<ReturnInst>(U) || isa<CallBase>(U) || isa<StoreInst>(U) ||
        isa<IntToPtrInst>(U))
      return true;
    if (isa<CmpInst>(U) || isa<BranchInst>(U) || isa<SwitchInst>(U))
      continue;
    if (doesIntEscape(U, Seen))
      return true;
  }
  return false;
}

std::optional<unsigned>
FlexFatSanitizer::getConsumedOperandIndex(const CallBase &CB) const {
  auto findOperand = [&](Value *Consumed) -> std::optional<unsigned> {
    if (!Consumed)
      return std::nullopt;

    // Allocation attributes precisely identify the consumed parameter, even
    // when the same SSA value is also passed in another argument position.
    for (unsigned I = 0; I != CB.arg_size(); ++I)
      if (CB.paramHasAttr(I, Attribute::AllocatedPointer) &&
          CB.getArgOperand(I) == Consumed)
        return I;
    for (unsigned I = 0; I != CB.arg_size(); ++I)
      if (CB.getArgOperand(I) == Consumed)
        return I;
    return std::nullopt;
  };

  if (std::optional<unsigned> Freed = findOperand(getFreedOperand(&CB, &TLI)))
    return Freed;
  if (std::optional<unsigned> Reallocated =
          findOperand(getReallocatedOperand(&CB)))
    return Reallocated;

  // Ordinary free/realloc-family declarations may be described by TLI rather
  // than LLVM allocation attributes.  All supported library variants consume
  // their first operand.
  const Function *Callee = CB.getCalledFunction();
  LibFunc TLIFn;
  if (!CB.isNoBuiltin() && Callee && TLI.getLibFunc(*Callee, TLIFn) &&
      TLI.has(TLIFn)) {
    switch (TLIFn) {
    case LibFunc_free:
    case LibFunc_vec_free:
    case LibFunc_realloc:
    case LibFunc_reallocf:
    case LibFunc_reallocarray:
      if (CB.arg_size() && CB.getArgOperand(0)->getType()->isPointerTy())
        return 0;
      break;
    default:
      break;
    }
  }
  return std::nullopt;
}

bool FlexFatSanitizer::isWriteCheck(CheckKind Kind) {
  return Kind == CheckKind::Write || Kind == CheckKind::MemIntrinsicWrite;
}

bool FlexFatSanitizer::isEscapeCheck(CheckKind Kind) {
  return Kind == CheckKind::CallEscape || Kind == CheckKind::ReturnEscape ||
         Kind == CheckKind::StoreEscape || Kind == CheckKind::IntegerEscape ||
         Kind == CheckKind::AggregateEscape;
}

bool FlexFatSanitizer::instrumentPointerCheck(Instruction *I, Value *Ptr,
                                              uint64_t FixedAccessSize,
                                              Value *DynAccessSize,
                                              CheckKind Kind) {
  BoundsRecord PtrBounds = getBounds(Ptr);
  if (PtrBounds.Kind == BaseKind::NonFlexFat)
    return false;
  markInstrumented(I);

  if (!DynAccessSize && PtrBounds.StaticUpperBound) {
    // StaticUpperBound describes the requested object, not its allocation
    // slot. The reserved trailing byte makes its exact one-past escape valid.
    // Keep scalar point accesses at that offset dynamically checked and
    // preserve width-aware access elision.
    bool IsStaticallyValid =
        isEscapeCheck(Kind) ? FixedAccessSize <= *PtrBounds.StaticUpperBound
                            : FixedAccessSize < *PtrBounds.StaticUpperBound;
    if (!usesInSlotTags() && FixedAccessSize &&
        FixedAccessSize == *PtrBounds.StaticUpperBound)
      IsStaticallyValid = true;
    if (IsStaticallyValid)
      return false;
  }

  IRBuilder<> IRB(I);
  IRB.SetNoSanitizeMetadata();
  Value *PtrInt = rawPointer(IRB, Ptr);
  Value *BasePtr = PtrBounds.CompanionBase;
  Value *BasePtrInt = rawPointer(IRB, BasePtr);
  Value *SizeInt = DynAccessSize ? AccessLengths.lookup(I) : nullptr;
  if (DynAccessSize && !SizeInt)
    SizeInt = IRB.CreateZExtOrTrunc(DynAccessSize, IntptrTy);

  Instruction *CheckInsertBefore = I;
  std::optional<IRBuilder<>> GuardedIRB;
  IRBuilder<> *CheckIRB = &IRB;
  if (SizeInt) {
    Value *NonZero = IRB.CreateICmpNE(SizeInt, ConstantInt::get(IntptrTy, 0));
    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(NonZero, I, /*Unreachable=*/false);
    markNoSanitize(ThenTerm);
    GuardedIRB.emplace(ThenTerm);
    GuardedIRB->SetNoSanitizeMetadata();
    CheckIRB = &*GuardedIRB;
    CheckInsertBefore = ThenTerm;
  }

  Value *RegionIndex = getMemoizedTableIndex(BasePtr);
  if (!RegionIndex)
    RegionIndex = getSafeTableIndex(*CheckIRB, BasePtrInt);

  Type *I64Ty = Type::getInt64Ty(M.getContext());
  Value *AllocSize64 = BaseSizes.lookup(BasePtr);
  if (!AllocSize64)
    if (auto G = Geometries.find(BasePtr); G != Geometries.end())
      AllocSize64 = G->second.Size;
  if (!AllocSize64)
    AllocSize64 = loadFromFixedTable(*CheckIRB, TablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
  Value *AllocSize = CheckIRB->CreateZExtOrTrunc(AllocSize64, IntptrTy);

  emitOobCheck(*CheckIRB, PtrInt, BasePtrInt, AllocSize, FixedAccessSize,
               SizeInt, CheckInsertBefore, Kind);
  return true;
}

bool FlexFatSanitizer::instrumentMemoryAccess(Instruction *I, Value *Ptr,
                                              Type *AccessTy) {
  if (RangeProven.contains(I)) {
    markInstrumented(I);
    return false;
  }
  TypeSize AccessSize = DL.getTypeStoreSize(AccessTy);
  bool IsWrite =
      isa<StoreInst>(I) || isa<AtomicRMWInst>(I) || isa<AtomicCmpXchgInst>(I);
  if (AccessSize.isScalable()) {
    if (!usesInSlotTags() || getBounds(Ptr).Kind == BaseKind::NonFlexFat)
      return false;
    IRBuilder<> B(I);
    B.SetNoSanitizeMetadata();
    return instrumentPointerCheck(I, Ptr, 0,
                                  B.CreateTypeSize(IntptrTy, AccessSize),
                                  IsWrite ? CheckKind::Write : CheckKind::Read);
  }
  uint64_t CheckedSize =
      Options.CheckWholeAccess ? AccessSize.getFixedValue() : 0;
  CheckKind Kind = IsWrite ? CheckKind::Write : CheckKind::Read;
  if (!instrumentPointerCheck(I, Ptr, CheckedSize, nullptr, Kind))
    return false;
  if (isa<LoadInst>(I))
    NumInstrumentedLoads++;
  else if (isa<StoreInst>(I))
    NumInstrumentedStores++;
  else
    NumInstrumentedAtomics++;
  return true;
}

bool FlexFatSanitizer::instrumentMemoryRange(Instruction *I, Value *Ptr,
                                             Value *Size, CheckKind Kind) {
  if (auto *ConstantSize = dyn_cast<ConstantInt>(Size);
      ConstantSize && ConstantSize->isZero())
    return false;
  if (!instrumentPointerCheck(I, Ptr, 0, Size, Kind))
    return false;
  NumInstrumentedMemIntrinsics++;
  return true;
}

bool FlexFatSanitizer::instrumentPointerEscape(Instruction *I, Value *Ptr,
                                               CheckKind Kind) {
  if (auto *VT = dyn_cast<VectorType>(Ptr->getType())) {
    // Late instrumentation can see vectorized pointer-to-integer escapes.
    // Geometry and tags belong to individual pointers, never the vector.
    markInstrumented(I);
    IRBuilder<> B(I);
    B.SetNoSanitizeMetadata();
    DenseMap<std::pair<PHINode *, unsigned>, PHINode *> LanePhis;
    auto ScalarLane = [&](auto &&Self, Value *V, Value *Lane,
                          IRBuilder<> &Builder) -> Value * {
      if (!V->getType()->isVectorTy())
        return V;
      auto *Index = dyn_cast<ConstantInt>(Lane);
      if (Index)
        if (Value *Scalar = findScalarElement(V, Index->getZExtValue()))
          return Scalar;
      if (auto *GEP = dyn_cast<GEPOperator>(V)) {
        SmallVector<Value *, 4> Indices;
        for (Value *Offset : GEP->indices())
          Indices.push_back(Offset->getType()->isVectorTy()
                                ? Builder.CreateExtractElement(Offset, Lane)
                                : Offset);
        return Builder.CreateGEP(GEP->getSourceElementType(),
                                 Self(Self, GEP->getPointerOperand(), Lane,
                                      Builder),
                                 Indices, "flexfat.escape.pointer",
                                 GEP->getNoWrapFlags());
      }
      if (auto *Select = dyn_cast<SelectInst>(V)) {
        Value *Condition = Select->getCondition();
        if (Condition->getType()->isVectorTy())
          Condition = Builder.CreateExtractElement(Condition, Lane);
        return Builder.CreateSelect(
            Condition, Self(Self, Select->getTrueValue(), Lane, Builder),
            Self(Self, Select->getFalseValue(), Lane, Builder));
      }
      if (auto *Phi = dyn_cast<PHINode>(V)) {
        if (!Index)
          report_fatal_error("FlexFat cannot recover scalable pointer-PHI "
                             "escape provenance");
        auto Key = std::make_pair(Phi, unsigned(Index->getZExtValue()));
        if (auto It = LanePhis.find(Key); It != LanePhis.end())
          return It->second;
        auto *Scalar = PHINode::Create(
            VT->getElementType(), Phi->getNumIncomingValues(),
            "flexfat.escape.phi", Phi->getParent()->getFirstNonPHIIt());
        LanePhis[Key] = Scalar;
        markNoSanitize(Scalar);
        // Edge splitting must see complete predecessor lists, including cycles.
        for (unsigned N = 0; N < Phi->getNumIncomingValues(); ++N)
          Scalar->addIncoming(PoisonValue::get(Scalar->getType()),
                               Phi->getIncomingBlock(N));
        DenseMap<BasicBlock *, unsigned> IncomingEdges;
        for (unsigned N = 0; N < Phi->getNumIncomingValues(); ++N) {
          Value *Incoming = Phi->getIncomingValue(N);
          BasicBlock *Pred = Phi->getIncomingBlock(N);
          auto [Edge, IsNew] = IncomingEdges.try_emplace(Pred, N);
          if (!IsNew) {
            Scalar->setIncomingValue(N, Scalar->getIncomingValue(Edge->second));
            continue;
          }
          if (auto *Invoke = dyn_cast<InvokeInst>(Incoming))
            Pred = SplitEdge(Invoke->getParent(), Invoke->getNormalDest());
          IRBuilder<> IncomingBuilder(Pred->getTerminator());
          IncomingBuilder.SetNoSanitizeMetadata();
          Scalar->setIncomingValue(
              N, Self(Self, Incoming, Lane, IncomingBuilder));
        }
        return Scalar;
      }
      return Builder.CreateExtractElement(V, Lane, "flexfat.escape.lane");
    };
    if (auto *Fixed = dyn_cast<FixedVectorType>(VT)) {
      for (unsigned Lane = 0; Lane < Fixed->getNumElements(); ++Lane) {
        B.SetInsertPoint(I);
        Value *Element = ScalarLane(ScalarLane, Ptr, B.getInt64(Lane), B);
        instrumentPointerEscape(I, Element, Kind);
      }
      return true;
    }

    // Scalable vectors require one check for every runtime lane.
    Value *Count = B.CreateElementCount(IntptrTy, VT->getElementCount());
    BasicBlock *Entry = I->getParent();
    BasicBlock *Continue = Entry->splitBasicBlock(I->getIterator(),
                                                 "flexfat.escape.continue");
    BasicBlock *Loop = BasicBlock::Create(I->getContext(), "flexfat.escape.loop",
                                          I->getFunction(), Continue);
    Entry->getTerminator()->setSuccessor(0, Loop);
    markNoSanitize(Entry->getTerminator());
    B.SetInsertPoint(Loop);
    PHINode *Lane = B.CreatePHI(IntptrTy, 2, "flexfat.escape.index");
    Lane->addIncoming(ConstantInt::get(IntptrTy, 0), Entry);
    Value *Element = ScalarLane(ScalarLane, Ptr, Lane, B);
    Value *Next = B.CreateAdd(Lane, ConstantInt::get(IntptrTy, 1));
    BranchInst *Latch = B.CreateCondBr(B.CreateICmpULT(Next, Count), Loop,
                                      Continue);
    instrumentPointerEscape(Latch, Element, Kind);
    Lane->addIncoming(Next, Latch->getParent());
    return true;
  }
  if (!instrumentPointerCheck(I, Ptr, 0, nullptr, Kind))
    return false;
  NumInstrumentedEscapes++;
  return true;
}

void FlexFatSanitizer::prepareBounds(Instruction *I) {
  auto Prepare = [this](Value *Ptr) {
    // Vector escape operands are extracted and checked lane by lane later.
    if (Ptr->getType()->isPointerTy())
      (void)getBounds(Ptr);
  };

  if (auto *LI = dyn_cast<LoadInst>(I))
    Prepare(LI->getPointerOperand());
  else if (auto *SI = dyn_cast<StoreInst>(I)) {
    Prepare(SI->getPointerOperand());
    if (SI->getValueOperand()->getType()->isPointerTy())
      Prepare(SI->getValueOperand());
  } else if (auto *RMW = dyn_cast<AtomicRMWInst>(I))
    Prepare(RMW->getPointerOperand());
  else if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(I))
    Prepare(CmpXchg->getPointerOperand());
  else if (auto *MS = dyn_cast<MemSetInst>(I)) {
    if (auto *Size = dyn_cast<ConstantInt>(MS->getLength());
        !Size || !Size->isZero())
      Prepare(MS->getDest());
  } else if (auto *MT = dyn_cast<MemTransferInst>(I)) {
    if (auto *Size = dyn_cast<ConstantInt>(MT->getLength());
        !Size || !Size->isZero()) {
      Prepare(MT->getDest());
      Prepare(MT->getSource());
    }
  } else if (auto *RI = dyn_cast<ReturnInst>(I)) {
    Value *V = RI->getReturnValue();
    if (V && V->getType()->isPointerTy())
      Prepare(V);
  } else if (auto *IV = dyn_cast<InsertValueInst>(I)) {
    Value *V = IV->getInsertedValueOperand();
    if (V->getType()->isPointerTy())
      Prepare(V);
  } else if (auto *IE = dyn_cast<InsertElementInst>(I)) {
    Value *V = IE->getOperand(1);
    if (V->getType()->isPointerTy())
      Prepare(V);
  } else if (auto *P2I = dyn_cast<PtrToIntInst>(I)) {
    SmallPtrSet<Value *, 16> Seen;
    if (doesIntEscape(P2I, Seen))
      Prepare(P2I->getPointerOperand());
  } else if (auto *CB = dyn_cast<CallBase>(I)) {
    std::optional<unsigned> Consumed = getConsumedOperandIndex(*CB);
    SmallPtrSet<Value *, 4> CheckedArgs;
    if (Consumed) {
      CheckedArgs.insert(CB->getArgOperand(*Consumed));
      Prepare(CB->getArgOperand(*Consumed));
    }
    for (unsigned ArgNo = 0; ArgNo != CB->arg_size(); ++ArgNo) {
      Value *Arg = CB->getArgOperand(ArgNo);
      if ((!Consumed || ArgNo != *Consumed) && Arg->getType()->isPointerTy() &&
          CheckedArgs.insert(Arg).second)
        Prepare(Arg);
    }
  }
}

// Emit a generation observation for each covered access. Shadow observations
// are ordinary loads, like HWASan's, and may be reused by later optimization.
// In-slot observations retain atomic ordering.
bool FlexFatSanitizer::instrumentTemporal(const AccessRecord &Access) {
  Instruction *I = Access.Inst;
  IRBuilder<> B(I);
  B.SetNoSanitizeMetadata();
  Instruction *CheckBefore = I;
  if (!isa<ConstantInt>(Access.Length)) {
    CheckBefore = SplitBlockAndInsertIfThen(
        B.CreateICmpNE(Access.Length, ConstantInt::get(IntptrTy, 0)), I, false);
    markNoSanitize(CheckBefore);
    B.SetInsertPoint(CheckBefore);
  }
  auto Existing = Geometries.find(Access.Pointer);
  auto Shared = AccessGeometry.find({I, Access.Pointer});
  auto Fast = FastGeometry.find(I);
  auto Spatial = SpatialGeometry.find({I, Access.Pointer});
  GeometryRecord G = Fast != FastGeometry.end()       ? Fast->second
                     : Shared != AccessGeometry.end() ? Shared->second
                     : Spatial != SpatialGeometry.end() ? Spatial->second
                     : Existing != Geometries.end()
                         ? Existing->second
                         : emitGeometry(B, Access.Pointer, true);
  // A proven same-slot root shares geometry, never the actual pointer tag.
  if (Fast != FastGeometry.end() || Shared != AccessGeometry.end() ||
      Spatial != SpatialGeometry.end()) {
    auto *Tagged = dyn_cast<PtrToIntInst>(G.Tagged);
    if (!Tagged || Tagged->getPointerOperand() != Access.Pointer)
      G.Tagged = B.CreatePtrToInt(Access.Pointer, IntptrTy);
  }
  auto *Generation = B.CreateLoad(B.getInt8Ty(), G.Entry, "flexfat.generation");
#ifdef FLEXFAT_CUSTOM_CONFIG
  Generation->setAtomic(AtomicOrdering::Acquire);
#else
  if (usesInSlotTags())
    Generation->setAtomic(AtomicOrdering::Monotonic);
#endif
  Generation->setAlignment(Align(1));
  Value *Tag = B.CreateTrunc(B.CreateLShr(G.Tagged, 56), B.getInt8Ty());
  Value *Matches = B.CreateICmpEQ(Tag, Generation);
#ifdef FLEXFAT_CUSTOM_CONFIG
  if (!G.NonzeroTagProven)
    Matches = B.CreateAnd(B.CreateICmpNE(Tag, B.getInt8(0)), Matches);
  auto *Managed = dyn_cast<ConstantInt>(G.Managed);
  Value *Valid = Managed && Managed->isOne()
                     ? Matches
                     : B.CreateOr(B.CreateNot(G.Managed), Matches);
#else
  Value *Valid = Matches;
#endif
  auto *Failure = SplitBlockAndInsertIfThen(
      B.CreateNot(Valid), CheckBefore, true,
      MDBuilder(M.getContext()).createBranchWeights(1, 1048575));
  markNoSanitize(Failure);
  // Keep the temporal predicate as one condition. In custom mode this hint
  // prevents SelectionDAG from turning its managed/tag OR into another branch.
  Failure->getParent()->getSinglePredecessor()->getTerminator()->setMetadata(
      LLVMContext::MD_unpredictable, getInstrumentedMetadata());
  B.SetInsertPoint(Failure);
  auto Reporter = M.getOrInsertFunction(
      "__flexfat_report_temporal_v3",
      FunctionType::get(B.getVoidTy(),
                        {IntptrTy, IntptrTy, B.getInt32Ty(), B.getInt32Ty()},
                        false));
  if (auto *RF = dyn_cast<Function>(Reporter.getCallee())) {
    invalidateTemporalAttributes(*RF);
    RF->removeFnAttr(Attribute::WillReturn);
    RF->addFnAttr(Attribute::Cold);
    RF->addFnAttr(Attribute::NoReturn);
  }
  B.CreateCall(Reporter, {G.Tagged, Access.Length, B.getInt32(Access.Write),
                          B.CreateZExt(Generation, B.getInt32Ty())});
  I->setMetadata("flexfat.temporal", getInstrumentedMetadata());
  return true;
}

void FlexFatSanitizer::discoverAccesses(
    Instruction *I, SmallVectorImpl<AccessRecord> &Accesses) {
  if (!Options.TemporalTBI || I->getMetadata("flexfat.temporal"))
    return;
  auto Add = [&](Value *Ptr, Value *Length, bool Write, uint64_t Width) {
    SmallPtrSet<Value *, 16> Seen;
    if (isDefinitelyNonFlexFat(Ptr, Seen))
      return;
    if (auto *C = dyn_cast<ConstantInt>(Length); C && C->isZero())
      return;
    IRBuilder<> B(I);
    B.SetNoSanitizeMetadata();
    Value *Converted = AccessLengths.lookup(I);
    if (!Converted)
      AccessLengths[I] = Converted = B.CreateZExtOrTrunc(Length, IntptrTy);
    Accesses.push_back(
        {I, Ptr, Converted, Write,
         Options.CheckWholeAccess ? Width : 0});
  };
  auto Scalar = [&](Value *Ptr, Type *Ty, bool Write) {
    SmallPtrSet<Value *, 16> Seen;
    if (isDefinitelyNonFlexFat(Ptr, Seen))
      return;
    IRBuilder<> B(I);
    B.SetNoSanitizeMetadata();
    TypeSize Size = DL.getTypeStoreSize(Ty);
    Add(Ptr, B.CreateTypeSize(IntptrTy, Size), Write,
        Size.isScalable() ? 0 : Size.getFixedValue());
  };
  if (auto *L = dyn_cast<LoadInst>(I))
    Scalar(L->getPointerOperand(), L->getType(), false);
  else if (auto *S = dyn_cast<StoreInst>(I))
    Scalar(S->getPointerOperand(), S->getValueOperand()->getType(), true);
  else if (auto *A = dyn_cast<AtomicRMWInst>(I))
    Scalar(A->getPointerOperand(), A->getValOperand()->getType(), true);
  else if (auto *A = dyn_cast<AtomicCmpXchgInst>(I))
    Scalar(A->getPointerOperand(), A->getNewValOperand()->getType(), true);
  else if (auto *T = dyn_cast<MemTransferInst>(I)) {
    Add(T->getDest(), T->getLength(), true, 0);
    Add(T->getSource(), T->getLength(), false, 0);
  } else if (auto *S = dyn_cast<MemSetInst>(I))
    Add(S->getDest(), S->getLength(), true, 0);
}

// Version original scalar loops before provenance or check insertion mutates
// their CFG. Recompute candidate facts after each clone before selecting the
// next original loop. Generation observations are never moved.
bool FlexFatSanitizer::versionLoop(Function &F, DominatorTree &DT, LoopInfo &LI,
                                   ScalarEvolution &SE) {
  if (!Options.TemporalTBI)
    return false;
  const bool Extended = ClVersionTBILoops;
  struct RangeAccess {
    Instruction *Inst;
    Value *Root;
    int64_t Offset;
    int64_t Stride;
    uint64_t Width;
  };
  struct RangeGroup {
    Value *Root;
    int64_t Stride;
    int64_t LowOffset;
    int64_t HighEnd;
  };
  auto Loops = LI.getLoopsInPreorder();
  for (Loop *L : Loops) {
    if (!L->isInnermost() || !L->isLoopSimplifyForm() || !L->getLoopLatch() ||
        !L->getExitingBlock() || !L->getUniqueExitBlock() ||
        L->getLoopLatch()->getTerminator()->getMetadata(
            "flexfat.loop.versioned"))
      continue;
    bool CountStats = CountedLoopHeaders.insert(L->getHeader()).second;
    if (CountStats)
      ++NumTBILoopCandidates;
    SmallVector<RangeAccess, 128> Accesses;
    SmallVector<RangeGroup, 16> Groups;
    SmallPtrSet<Value *, 8> Roots;
    bool Eligible = true;
    // Keep the default small-loop path when the extended policy is enabled.
    // Its one-access threshold is profitable for loops such as
    // quantum_sigma_x, provided all of its stricter legality rules hold.
    bool DefaultEligible = true;
    unsigned Instructions = 0;
    for (BasicBlock *BB : L->blocks()) {
      for (Instruction &I : *BB) {
        if (isa<DbgInfoIntrinsic>(I))
          continue;
        if (++Instructions > (Extended ? 1024U : 128U)) {
          if (CountStats)
            ++NumTBILoopRejectedSize;
          Eligible = false;
          break;
        }
        if (Instructions > 128)
          DefaultEligible = false;
        if (I.getMetadata("flexfat.instrumented") ||
            I.getMetadata("flexfat.temporal") || I.isEHPad() ||
            isa<ResumeInst, CatchReturnInst, CleanupReturnInst,
                IndirectBrInst>(I)) {
          Eligible = false;
          break;
        }
        if (auto *CB = dyn_cast<CallBase>(&I)) {
          auto *II = dyn_cast<IntrinsicInst>(CB);
          if (!II || (II->getIntrinsicID() != Intrinsic::lifetime_start &&
                      II->getIntrinsicID() != Intrinsic::lifetime_end))
            DefaultEligible = false;
          if ((!Extended &&
               (!II || (II->getIntrinsicID() != Intrinsic::lifetime_start &&
                        II->getIntrinsicID() != Intrinsic::lifetime_end))) ||
              (Extended && (!isa<CallInst>(CB) || CB->cannotDuplicate() ||
                            CB->isConvergent() ||
                            CB->hasFnAttr(Attribute::ReturnsTwice) ||
                            cast<CallInst>(CB)->isMustTailCall())))
            Eligible = false;
          continue;
        }
        Value *Ptr = nullptr;
        Type *Ty = nullptr;
        if (auto *Load = dyn_cast<LoadInst>(&I)) {
          if (Load->isAtomic()) {
            DefaultEligible = false;
            if (!Extended)
              Eligible = false;
            continue;
          }
          Ptr = Load->getPointerOperand();
          Ty = Load->getType();
        } else if (auto *Store = dyn_cast<StoreInst>(&I)) {
          if (Store->isAtomic()) {
            DefaultEligible = false;
            if (!Extended)
              Eligible = false;
            continue;
          }
          Ptr = Store->getPointerOperand();
          Ty = Store->getValueOperand()->getType();
        } else if (I.mayReadOrWriteMemory()) {
          DefaultEligible = false;
          if (!Extended)
            Eligible = false;
          continue;
        } else {
          continue;
        }
        if ((!Ty->isIntegerTy() && !Ty->isFloatingPointTy()) ||
            DL.getTypeStoreSize(Ty).isScalable() ||
            (!Extended &&
             (!DT.dominates(BB, L->getLoopLatch()) ||
              !DT.dominates(BB, L->getExitingBlock())))) {
          DefaultEligible = false;
          if (!Extended)
            Eligible = false;
          continue;
        }
        if (!DT.dominates(BB, L->getLoopLatch()) ||
            !DT.dominates(BB, L->getExitingBlock()))
          DefaultEligible = false;
        SmallPtrSet<Value *, 16> NonFlexFatSeen;
        if (isDefinitelyNonFlexFat(Ptr, NonFlexFatSeen))
          continue;
        const SCEV *Start = SE.getSCEV(Ptr);
        int64_t Stride = 0;
        if (auto *AR = dyn_cast<SCEVAddRecExpr>(Start)) {
          auto *Step = dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE));
          if (AR->getLoop() != L || !AR->isAffine() || !Step ||
              Step->getAPInt().getSignificantBits() > 64) {
            DefaultEligible = false;
            if (!Extended)
              Eligible = false;
            continue;
          }
          Stride = Step->getAPInt().getSExtValue();
          Start = AR->getStart();
        } else if (!SE.isLoopInvariant(Start, L)) {
          DefaultEligible = false;
          if (!Extended)
            Eligible = false;
          continue;
        }
        auto *RootSCEV = dyn_cast<SCEVUnknown>(SE.getPointerBase(Start));
        if (!RootSCEV || !RootSCEV->getType()->isPointerTy() ||
            !L->isLoopInvariant(RootSCEV->getValue())) {
          DefaultEligible = false;
          if (!Extended)
            Eligible = false;
          continue;
        }
        auto *Offset = dyn_cast<SCEVConstant>(SE.getMinusSCEV(Start, RootSCEV));
        if (!Offset || Offset->getAPInt().getSignificantBits() > 64) {
          DefaultEligible = false;
          if (!Extended)
            Eligible = false;
          continue;
        }
        Value *Root = RootSCEV->getValue();
        if (auto *Def = dyn_cast<Instruction>(Root);
            Def && !DT.dominates(Def, L->getLoopPreheader()->getTerminator())) {
          DefaultEligible = false;
          if (!Extended)
            Eligible = false;
          continue;
        }
        int64_t OffsetValue = Offset->getAPInt().getSExtValue();
        uint64_t Width = DL.getTypeStoreSize(Ty).getFixedValue();
        if (Width > INT64_MAX || OffsetValue > INT64_MAX - int64_t(Width)) {
          DefaultEligible = false;
          if (!Extended)
            Eligible = false;
          continue;
        }
        Roots.insert(Root);
        Accesses.push_back({&I, Root, OffsetValue, Stride, Width});
        if (CountStats)
          ++NumTBILoopEligibleAccesses;
        int64_t End = OffsetValue + int64_t(Width);
        auto Group = llvm::find_if(Groups, [&](const RangeGroup &G) {
          return G.Root == Root && G.Stride == Stride;
        });
        if (!Extended || Group == Groups.end())
          Groups.push_back({Root, Stride, OffsetValue, End});
        else {
          Group->LowOffset = std::min(Group->LowOffset, OffsetValue);
          Group->HighEnd = std::max(Group->HighEnd, End);
        }
        if (Accesses.size() > (Extended ? 128U : 8U) ||
            Roots.size() > (Extended ? 8U : 4U) ||
            Groups.size() > (Extended ? 16U : 8U)) {
          if (CountStats)
            ++NumTBILoopRejectedSize;
          Eligible = false;
          break;
        }
      }
      if (!Eligible)
        break;
    }
    if (!Eligible) {
      if (CountStats && Instructions <= (Extended ? 1024U : 128U) &&
          Accesses.size() <= (Extended ? 128U : 8U) &&
          Roots.size() <= (Extended ? 8U : 4U) &&
          Groups.size() <= (Extended ? 16U : 8U))
        ++NumTBILoopRejectedControl;
      continue;
    }
    if (Instructions > (Extended ? 1024U : 128U) ||
        Accesses.size() > (Extended ? 128U : 8U) ||
        Roots.size() > (Extended ? 8U : 4U) ||
        Groups.size() > (Extended ? 16U : 8U)) {
      if (CountStats)
        ++NumTBILoopRejectedSize;
      continue;
    }
    if (Accesses.empty() ||
        (Extended && Accesses.size() < 4 && !DefaultEligible)) {
      if (CountStats)
        ++NumTBILoopRejectedFew;
      continue;
    }
    SCEVExpander Exp(SE, "flexfat.trip");
    const SCEV *Backedges = SE.getBackedgeTakenCount(L);
    if (isa<SCEVCouldNotCompute>(Backedges) ||
        !SE.isLoopInvariant(Backedges, L) ||
        SE.getTypeSizeInBits(Backedges->getType()) > 64 ||
        !Exp.isSafeToExpandAt(Backedges,
                              L->getLoopPreheader()->getTerminator())) {
      if (CountStats)
        ++NumTBILoopRejectedTrip;
      continue;
    }

    // A constant trip count within a known allocation needs neither a range
    // guard nor a clone. Use wide signed arithmetic for the static proof; even
    // a 64-bit count times a signed 64-bit stride fits this calculation.
    if (auto *Count = dyn_cast<SCEVConstant>(Backedges)) {
      bool Contained = true;
      for (const RangeAccess &A : Accesses) {
        auto Bound = getAllocationUpperBound(A.Root);
        APInt First(128, A.Offset, true);
        APInt Span =
            Count->getAPInt().zextOrTrunc(128) * APInt(128, A.Stride, true);
        APInt Low = A.Stride < 0 ? First + Span : First;
        APInt High = A.Stride < 0 ? First : First + Span;
        APInt End = High + APInt(128, A.Width);
        if (!isAllocationResult(A.Root) || !Bound || Low.isNegative() ||
            End.ugt(APInt(128, *Bound))) {
          Contained = false;
          break;
        }
      }
      if (Contained) {
        IRBuilder<> B(L->getLoopPreheader()->getTerminator());
        B.SetNoSanitizeMetadata();
        DenseMap<Value *, GeometryRecord> RootGeometry;
        for (Value *Root : Roots)
          RootGeometry[Root] = emitGeometry(B, Root);
        NumTBILoopHoistedGeometry += Roots.size();
        for (const RangeAccess &A : Accesses) {
          FastGeometry[A.Inst] = RootGeometry.find(A.Root)->second;
          RangeProven.insert(A.Inst);
        }
        NumTBILoopRemovedSpatial += Accesses.size();
        L->getLoopLatch()->getTerminator()->setMetadata(
            "flexfat.loop.versioned", getInstrumentedMetadata());
        return true;
      }
    }

    // Check the entire affine range before entering either loop version.
    // Conditional accesses are covered even on iterations that skip them.
    BasicBlock *Guard = L->getLoopPreheader();
    Value *Count = Exp.expandCodeFor(Backedges, Backedges->getType(),
                                     Guard->getTerminator());
    for (Instruction *Inserted : Exp.getAllInsertedInstructions())
      markNoSanitize(Inserted);
    IRBuilder<> B(Guard->getTerminator());
    B.SetNoSanitizeMetadata();
    Count = B.CreateZExtOrTrunc(Count, IntptrTy);
    Value *Valid = B.CreateICmpULE(Count, B.getInt64(INT64_MAX));
    auto Checked = [&](Intrinsic::ID ID, Value *A, Value *C) {
      auto *Fn = Intrinsic::getOrInsertDeclaration(&M, ID, {IntptrTy});
      Value *Pair = B.CreateCall(Fn, {A, C});
      Valid = B.CreateAnd(Valid, B.CreateNot(B.CreateExtractValue(Pair, 1)));
      return B.CreateExtractValue(Pair, 0);
    };
    DenseMap<Value *, GeometryRecord> RootGeometry;
    for (Value *Root : Roots) {
      GeometryRecord G = emitGeometry(B, Root);
      RootGeometry[Root] = G;
      // The range guard keeps every affine access within this root's slot,
      // so address arithmetic cannot carry into or borrow from the tag byte.
#ifdef FLEXFAT_CUSTOM_CONFIG
      Value *RootTag = B.CreateTrunc(B.CreateLShr(G.Tagged, 56), B.getInt8Ty());
      Valid = B.CreateAnd(Valid, B.CreateICmpNE(RootTag, B.getInt8(0)));
#endif
      Value *Start = B.CreateShl(G.Index, RegionSizeLog);
      Value *End = B.CreateAdd(Start, B.getInt64(1ULL << RegionSizeLog));
      Valid = B.CreateAnd(Valid, G.Managed);
      Valid = B.CreateAnd(Valid, B.CreateICmpUGE(G.Base, Start));
      if (Options.Storage == FlexFatSanitizerOptions::TBIStorage::PriorByte)
        Valid = B.CreateAnd(
            Valid, B.CreateICmpUGE(G.Base, B.CreateAdd(Start, G.Size)));
      Valid =
          B.CreateAnd(Valid, B.CreateICmpULE(G.Base, B.CreateSub(End, G.Size)));
    }
    NumTBILoopHoistedGeometry += Roots.size();
    for (const RangeGroup &Group : Groups) {
      const GeometryRecord &G = RootGeometry.find(Group.Root)->second;
      Value *Span = Checked(Intrinsic::smul_with_overflow, Count,
                            B.getInt64(Group.Stride));
      Value *First = Checked(Intrinsic::sadd_with_overflow, G.Raw,
                             B.getInt64(Group.LowOffset));
      Value *Low = Group.Stride < 0
                       ? Checked(Intrinsic::sadd_with_overflow, First, Span)
                       : First;
      Value *High = Checked(Intrinsic::sadd_with_overflow, G.Raw,
                            B.getInt64(Group.HighEnd));
      Value *Limit = Group.Stride < 0
                         ? High
                         : Checked(Intrinsic::sadd_with_overflow, High, Span);
      Valid = B.CreateAnd(Valid, B.CreateICmpUGE(Low, G.Base));
      Valid = B.CreateAnd(Valid,
                          B.CreateICmpULE(
                              Limit,
                              B.CreateAdd(G.Base,
                                          usesInSlotTags()
                                              ? B.CreateSub(G.Size, B.getInt64(1))
                                              : G.Size)));
    }
    formLCSSARecursively(*L, DT, &LI, &SE);
    BasicBlock *PH = SplitBlock(Guard, Guard->getTerminator(), &DT, &LI,
                                nullptr, "flexfat.fallback.ph");
    ValueToValueMapTy VMap;
    SmallVector<BasicBlock *, 8> Blocks;
    Loop *Fast = cloneLoopWithPreheader(PH, Guard, L, VMap, ".flexfat.fast",
                                        &LI, &DT, Blocks);
    remapInstructionsInBlocks(Blocks, VMap);
    Instruction *OldBranch = Guard->getTerminator();
    B.SetInsertPoint(OldBranch);
    B.CreateCondBr(Valid, Fast->getLoopPreheader(), PH);
    OldBranch->eraseFromParent();
    BasicBlock *Exit = L->getUniqueExitBlock();
    for (PHINode &Phi : Exit->phis()) {
      int Index = Phi.getBasicBlockIndex(L->getExitingBlock());
      assert(Index >= 0 && "LCSSA exit missing original edge");
      Value *Incoming = Phi.getIncomingValue(Index);
      if (Value *Mapped = VMap.lookup(Incoming))
        Incoming = Mapped;
      Phi.addIncoming(Incoming, Fast->getExitingBlock());
    }
    for (const RangeAccess &A : Accesses) {
      auto *Clone = cast<Instruction>(VMap.lookup(A.Inst));
      FastGeometry[Clone] = RootGeometry.find(A.Root)->second;
      // Only the runtime guard proves managed allocation. Static containment
      // can also describe an allocation that fell back to libc.
      FastGeometry[Clone].Managed = B.getTrue();
#ifdef FLEXFAT_CUSTOM_CONFIG
      FastGeometry[Clone].NonzeroTagProven = true;
#endif
      RangeProven.insert(Clone);
    }
    NumTBILoopRemovedSpatial += Accesses.size();
    for (Loop *Version : {L, Fast}) {
      Version->getLoopLatch()->getTerminator()->setMetadata(
          "flexfat.loop.versioned", getInstrumentedMetadata());
      if (MDNode *ID = Version->getLoopID()) {
        SmallVector<Metadata *, 8> Ops;
        Ops.push_back(nullptr);
        for (unsigned I = 1; I < ID->getNumOperands(); ++I)
          Ops.push_back(ID->getOperand(I));
        MDNode *NewID = MDNode::getDistinct(M.getContext(), Ops);
        NewID->replaceOperandWith(0, NewID);
        Version->setLoopID(NewID);
      }
    }
    SE.forgetAllLoops();
    DT.recalculate(F);
    return true;
  }
  return false;
}

void FlexFatSanitizer::prepareLoopGeometry(ArrayRef<AccessRecord> Accesses,
                                           DominatorTree &DT, LoopInfo &LI) {
  DenseMap<std::pair<BasicBlock *, Value *>, GeometryRecord> Shared;
  for (const AccessRecord &A : Accesses) {
    Loop *L = LI.getLoopFor(A.Inst->getParent());
    if (!L || !L->getLoopPreheader())
      continue;
    Value *Root = A.Pointer;
    if (!L->isLoopInvariant(Root)) {
      Root = getContainedAllocationRoot(A);
      if (!Root || !L->isLoopInvariant(Root))
        continue;
    }
    BasicBlock *PH = L->getLoopPreheader();
    if (auto *Def = dyn_cast<Instruction>(Root);
        Def && !DT.dominates(Def, PH->getTerminator()))
      continue;
    auto Key = std::make_pair(PH, Root);
    auto It = Shared.find(Key);
    if (It == Shared.end()) {
      IRBuilder<> B(PH->getTerminator());
      B.SetNoSanitizeMetadata();
      auto Existing = Geometries.find(Root);
      GeometryRecord G = Existing != Geometries.end()
                             ? getTemporalGeometry(Root)
                             : emitGeometry(B, Root);
      It = Shared.insert({Key, G}).first;
    }
    AccessGeometry[{A.Inst, A.Pointer}] = It->second;
  }
}

// A constant-offset access within the requested allocation remains in the
// root's slot. Share immutable metadata geometry, but continue to observe the
// generation at every access and use the actual pointer's tag for comparison.
Value *FlexFatSanitizer::getContainedAllocationRoot(
    const AccessRecord &Access) {
  Value *Root = Access.Pointer;
  APInt Offset(DL.getIndexTypeSizeInBits(Root->getType()), 0);
  Root = Root->stripAndAccumulateConstantOffsets(DL, Offset, true);
  if (!isAllocationResult(Root) || Offset.isNegative() ||
      Offset.getActiveBits() > 64)
    return nullptr;
  auto Bound = getAllocationUpperBound(Root);
  auto *Length = dyn_cast<ConstantInt>(Access.Length);
  if (!Bound || !Length || Length->getValue().getActiveBits() > 64 ||
      Offset.getZExtValue() > *Bound ||
      Length->getZExtValue() > *Bound - Offset.getZExtValue())
    return nullptr;
  return Root;
}

void FlexFatSanitizer::prepareContainedGeometry(
    ArrayRef<AccessRecord> Accesses, DominatorTree &DT) {
  if (!ClShareContainedGeometry)
    return;
  for (const AccessRecord &Access : Accesses) {
    if (FastGeometry.contains(Access.Inst) ||
        AccessGeometry.count({Access.Inst, Access.Pointer}))
      continue;
    Value *Root = getContainedAllocationRoot(Access);
    // Materializing geometry after an invoke can split its normal edge and
    // invalidate the dominator tree used for the remaining accesses.
    if (!Root || Root == Access.Pointer || isa<InvokeInst>(Root))
      continue;
    if (auto *Def = dyn_cast<Instruction>(Root);
        Def && !DT.dominates(Def, Access.Inst))
      continue;
    AccessGeometry[{Access.Inst, Access.Pointer}] = getTemporalGeometry(Root);
  }
}

bool FlexFatSanitizer::instrumentFunction(Function &F) {
  if (!shouldInstrumentFunction(F))
    return false;

  Bounds.clear();
  RecoveredBases.clear();
  RecoveredBaseOrigins.clear();
  SafeTableIndices.clear();
  Geometries.clear();
  BaseSizes.clear();
  AccessLengths.clear();
  AccessGeometry.clear();
  FastGeometry.clear();
  RangeProven.clear();
  SpatialGeometry.clear();
  CountedLoopHeaders.clear();
  BoundsIRGeneration = 0;
  bool Modified = false;
  DominatorTree DT(F);
  LoopInfo LI(DT);
  AssumptionCache AC(F);
  ScalarEvolution SE(F, TLI, AC, DT, LI);
  while (versionLoop(F, DT, LI, SE))
    Modified = true;
  SmallVector<Instruction *, 16> ToInstrument;

  for (auto &BB : F) {
    for (auto &I : BB) {
      if (I.hasMetadata(LLVMContext::MD_nosanitize))
        continue;
      if (isa<LoadInst>(&I) || isa<StoreInst>(&I) || isa<AtomicRMWInst>(&I) ||
          isa<AtomicCmpXchgInst>(&I))
        ToInstrument.push_back(&I);
      else if (isa<MemIntrinsic>(&I))
        ToInstrument.push_back(&I);
      else if (isa<ReturnInst>(&I) || isa<InsertValueInst>(&I) ||
               isa<InsertElementInst>(&I) || isa<PtrToIntInst>(&I))
        ToInstrument.push_back(&I);
      else if (auto *CB = dyn_cast<CallBase>(&I);
               CB && !isa<IntrinsicInst>(&I) &&
               !(CB->getCalledFunction() &&
                 CB->getCalledFunction()->doesNotAccessMemory()))
        ToInstrument.push_back(&I);
    }
  }

  SmallVector<AccessRecord, 16> TemporalAccesses;
  for (Instruction *I : ToInstrument)
    discoverAccesses(I, TemporalAccesses);
  SmallVector<AccessRecord, 16> LoopAccesses;
  for (const AccessRecord &A : TemporalAccesses)
    if (LI.getLoopFor(A.Inst->getParent()))
      LoopAccesses.push_back(A);
  llvm::erase_if(ToInstrument, [this](Instruction *I) {
    return shouldSkipInstruction(I);
  });

  // LowFat-style phase 2: resolve every provenance root and memoize one
  // recovered allocation base before CFG-changing checks are inserted.
  for (Instruction *I : ToInstrument)
    if (!I->hasMetadata(LLVMContext::MD_nosanitize) && !RangeProven.contains(I))
      prepareBounds(I);

  // Invoke provenance can split normal edges. Rebuild CFG analyses and
  // discard SCEV results before revalidating loop geometry candidates.
  SE.forgetAllLoops();
  DT.recalculate(F);
  LI.releaseMemory();
  LI.analyze(DT);
  prepareLoopGeometry(LoopAccesses, DT, LI);
  prepareContainedGeometry(TemporalAccesses, DT);
  if (!Options.Recover)
    for (const AccessRecord &A : TemporalAccesses)
      if (!FastGeometry.contains(A.Inst) &&
          !AccessGeometry.count({A.Inst, A.Pointer}) &&
          !shouldSkipInstruction(A.Inst)) {
        BoundsRecord B = getBounds(A.Pointer);
        if (B.Kind != BaseKind::NonFlexFat) {
          Value *Root = RecoveredBaseOrigins.lookup(B.CompanionBase);
          Value *GeometryRoot = Root ? Root : B.CompanionBase;
          if (isa<Instruction>(GeometryRoot) || isa<Argument>(GeometryRoot))
            SpatialGeometry[{A.Inst, A.Pointer}] =
                getTemporalGeometry(GeometryRoot);
        }
      }
  // Without a retained spatial check or an independent same-slot proof,
  // derive temporal metadata from the actual access pointer.
  for (const AccessRecord &A : TemporalAccesses)
    if (!FastGeometry.contains(A.Inst) &&
        !AccessGeometry.count({A.Inst, A.Pointer}) &&
        !SpatialGeometry.count({A.Inst, A.Pointer}) &&
        (isa<Instruction>(A.Pointer) || isa<Argument>(A.Pointer)))
      (void)getTemporalGeometry(A.Pointer);
  // The final preparation step can also split an invoke's normal edge.
  SE.forgetAllLoops();
  DT.recalculate(F);
  LI.releaseMemory();
  LI.analyze(DT);

  // Temporal discovery is independent of spatial elision. Prepare all
  // provenance before inserting any check branches (including invoke splits).
  if (Options.Recover)
    for (const AccessRecord &Access : TemporalAccesses)
      Modified |= instrumentTemporal(Access);

  // LowFat-style phase 3: insert checks using the cached recovered bases.
  for (Instruction *I : ToInstrument) {
    if (I->hasMetadata(LLVMContext::MD_nosanitize))
      continue;
    if (auto *LI = dyn_cast<LoadInst>(I))
      Modified |=
          instrumentMemoryAccess(I, LI->getPointerOperand(), LI->getType());
    else if (auto *SI = dyn_cast<StoreInst>(I)) {
      Modified |= instrumentMemoryAccess(I, SI->getPointerOperand(),
                                         SI->getValueOperand()->getType());
      if (SI->getValueOperand()->getType()->isPointerTy())
        Modified |= instrumentPointerEscape(I, SI->getValueOperand(),
                                            CheckKind::StoreEscape);
    } else if (auto *RMW = dyn_cast<AtomicRMWInst>(I))
      Modified |= instrumentMemoryAccess(I, RMW->getPointerOperand(),
                                         RMW->getValOperand()->getType());
    else if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(I))
      Modified |=
          instrumentMemoryAccess(I, CmpXchg->getPointerOperand(),
                                 CmpXchg->getNewValOperand()->getType());
    else if (auto *MS = dyn_cast<MemSetInst>(I))
      Modified |= instrumentMemoryRange(I, MS->getDest(), MS->getLength(),
                                        CheckKind::MemIntrinsicWrite);
    else if (auto *MT = dyn_cast<MemTransferInst>(I)) {
      Modified |= instrumentMemoryRange(I, MT->getDest(), MT->getLength(),
                                        CheckKind::MemIntrinsicWrite);
      Modified |= instrumentMemoryRange(I, MT->getSource(), MT->getLength(),
                                        CheckKind::MemIntrinsicRead);
    } else if (auto *RI = dyn_cast<ReturnInst>(I)) {
      Value *V = RI->getReturnValue();
      if (V && V->getType()->isPointerTy())
        Modified |= instrumentPointerEscape(I, V, CheckKind::ReturnEscape);
    } else if (auto *IV = dyn_cast<InsertValueInst>(I)) {
      Value *V = IV->getInsertedValueOperand();
      if (V->getType()->isPointerTy())
        Modified |= instrumentPointerEscape(I, V, CheckKind::AggregateEscape);
    } else if (auto *IE = dyn_cast<InsertElementInst>(I)) {
      Value *V = IE->getOperand(1);
      if (V->getType()->isPointerTy())
        Modified |= instrumentPointerEscape(I, V, CheckKind::AggregateEscape);
    } else if (auto *P2I = dyn_cast<PtrToIntInst>(I)) {
      SmallPtrSet<Value *, 16> Seen;
      if (doesIntEscape(P2I, Seen))
        Modified |= instrumentPointerEscape(I, P2I->getPointerOperand(),
                                            CheckKind::IntegerEscape);
    } else if (auto *CB = dyn_cast<CallBase>(I)) {
      std::optional<unsigned> Consumed = getConsumedOperandIndex(*CB);
      SmallPtrSet<Value *, 4> CheckedArgs;
      if (Consumed) {
        CheckedArgs.insert(CB->getArgOperand(*Consumed));
        Modified |= instrumentPointerEscape(I, CB->getArgOperand(*Consumed),
                                            CheckKind::Deallocation);
      }
      for (unsigned ArgNo = 0; ArgNo != CB->arg_size(); ++ArgNo) {
        Value *Arg = CB->getArgOperand(ArgNo);
        if ((!Consumed || ArgNo != *Consumed) &&
            Arg->getType()->isPointerTy() && CheckedArgs.insert(Arg).second)
          Modified |= instrumentPointerEscape(I, Arg, CheckKind::CallEscape);
      }
    }
  }
  // A fatal spatial failure cannot continue into the temporal observation.
  // Its successful edge also proves that the companion base names this slot.
  if (!Options.Recover)
    for (const AccessRecord &Access : TemporalAccesses)
      Modified |= instrumentTemporal(Access);
  if (Modified && Options.TemporalTBI)
    invalidateTemporalAttributes(F);
  return Modified || BoundsIRGeneration != 0;
}

bool FlexFatSanitizer::runFunction(Function &F) {
  if (F.isDeclaration() || F.empty())
    return false;
  return instrumentFunction(F);
}

bool FlexFatSanitizer::run() {
  LLVM_DEBUG(dbgs() << "[FlexFat] run() Mode=" << (int)Options.Mode << "\n");
  LLVM_DEBUG(dbgs() << "[FlexFat] Running on module: " << M.getName() << "\n");

  bool Modified = false;
  SmallPtrSet<Function *, 16> TemporalFunctions;
  for (Function &F : M) {
    if (Options.InternalModuleSetupOnly_) {
      // Scalar-late instrumentation runs as a function pass. Invalidate its
      // prospective callees here, while it is safe to modify other functions.
      if (Options.TemporalTBI && shouldInstrumentFunction(F)) {
        TemporalFunctions.insert(&F);
        Modified |= invalidateTemporalAttributes(F);
      }
    } else if (runFunction(F)) {
      Modified = true;
      if (Options.TemporalTBI)
        TemporalFunctions.insert(&F);
    }
  }
  // Call-site attributes are independent of the callee's attributes. Do this
  // after visiting all functions so caller/callee ordering does not matter.
  if (!TemporalFunctions.empty())
    for (Function &F : M)
      for (BasicBlock &BB : F)
        for (Instruction &I : BB)
          if (auto *CB = dyn_cast<CallBase>(&I))
            if (auto *Callee = dyn_cast<Function>(
                    CB->getCalledOperand()->stripPointerCastsAndAliases());
                TemporalFunctions.contains(Callee))
              Modified |= invalidateTemporalAttributes(*CB);

  // In-slot layouts have distinct link ABIs from shadow and each other.
  using TBIStorage = FlexFatSanitizerOptions::TBIStorage;
  const auto Storage = Options.Storage;
#ifdef FLEXFAT_CUSTOM_CONFIG
  const char *CtorName = Storage == TBIStorage::PriorByte
                             ? "__flexfat_tbi_ctor_prior_byte_custom_v2"
                         : Storage == TBIStorage::LastByte
                             ? "__flexfat_tbi_ctor_last_byte_custom_v1"
                             : "__flexfat_tbi_ctor_v3";
  const char *ABIName = Storage == TBIStorage::PriorByte
                            ? "__flexfat_tbi_abi_prior_byte_custom_v2"
                        : Storage == TBIStorage::LastByte
                            ? "__flexfat_tbi_abi_last_byte_custom_v1"
                            : "__flexfat_tbi_abi_v3";
#else
  const char *CtorName =
      Storage == TBIStorage::PriorByte ? "__flexfat_tbi_ctor_prior_byte_pow2_v3"
      : Storage == TBIStorage::LastByte ? "__flexfat_tbi_ctor_last_byte_pow2_v2"
                                        : "__flexfat_tbi_ctor_v7";
  const char *ABIName =
      Storage == TBIStorage::PriorByte  ? "__flexfat_tbi_abi_prior_byte_pow2_v3"
      : Storage == TBIStorage::LastByte ? "__flexfat_tbi_abi_last_byte_pow2_v2"
                                        : "__flexfat_tbi_abi_v7";
#endif
  if (Options.TemporalTBI && !M.getFunction(CtorName)) {
    auto &Ctx = M.getContext();
    auto *Ty = FunctionType::get(Type::getVoidTy(Ctx), false);
    auto ABI = M.getOrInsertFunction(ABIName, Ty);
    auto *Ctor =
        Function::Create(Ty, GlobalValue::InternalLinkage, CtorName, &M);
    IRBuilder<> B(BasicBlock::Create(Ctx, "entry", Ctor));
    B.SetNoSanitizeMetadata();
    B.CreateCall(ABI);
    B.CreateRetVoid();
    appendToGlobalCtors(M, Ctor, 0);
    appendToUsed(M, {Ctor});
    Modified = true;
  }

  // Emit a module constructor that calls __flexfat_set_recover(Recover) so the
  // runtime interceptors (memset/memcpy/memmove) know whether to warn or abort.
  // This runs before main() via .init_array / __mod_init_func.
  if (Options.Recover && !M.getFunction("__flexfat_set_recover_ctor")) {
    LLVMContext &Ctx = M.getContext();
    FunctionType *SetRecoverTy =
        FunctionType::get(Type::getVoidTy(Ctx), {Type::getInt32Ty(Ctx)}, false);
    FunctionCallee SetRecoverFn =
        M.getOrInsertFunction("__flexfat_set_recover", SetRecoverTy);
    Function *Ctor = Function::Create(
        FunctionType::get(Type::getVoidTy(Ctx), false),
        GlobalValue::InternalLinkage, "__flexfat_set_recover_ctor", &M);
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Ctor);
    IRBuilder<> CtorBuilder(BB);
    CtorBuilder.CreateCall(SetRecoverFn,
                           {ConstantInt::get(Type::getInt32Ty(Ctx), 1)});
    CtorBuilder.CreateRetVoid();
    appendToGlobalCtors(M, Ctor, /*Priority=*/0);
    Modified = true;
  }

  // Emit a module constructor that calls __flexfat_set_right_align(1) so the
  // runtime allocator right-aligns objects within their size-class slot.
  // Right-aligning places the object's right edge at the slot boundary,
  // making off-by-one overflows detectable at the cost of a left-side
  // blind spot of (class_size - requested_size) bytes.
  if (Options.AllocationAlignment == FlexFatSanitizerOptions::Alignment::Right &&
      !M.getFunction("__flexfat_set_right_align_ctor")) {
    LLVMContext &Ctx = M.getContext();
    FunctionType *SetRightAlignTy =
        FunctionType::get(Type::getVoidTy(Ctx), {Type::getInt32Ty(Ctx)}, false);
    FunctionCallee SetRightAlignFn =
        M.getOrInsertFunction("__flexfat_set_right_align", SetRightAlignTy);
    Function *Ctor = Function::Create(
        FunctionType::get(Type::getVoidTy(Ctx), false),
        GlobalValue::InternalLinkage, "__flexfat_set_right_align_ctor", &M);
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Ctor);
    IRBuilder<> CtorBuilder(BB);
    CtorBuilder.CreateCall(SetRightAlignFn,
                           {ConstantInt::get(Type::getInt32Ty(Ctx), 1)});
    CtorBuilder.CreateRetVoid();
    appendToGlobalCtors(M, Ctor, /*Priority=*/0);
    Modified = true;
  }

  return Modified;
}

} // anonymous namespace

FlexFatSanitizerPass::FlexFatSanitizerPass(
    const FlexFatSanitizerOptions &Options)
    : Options(Options) {}

PreservedAnalyses FlexFatSanitizerPass::run(Module &M,
                                            ModuleAnalysisManager &AM) {
#ifdef FLEXFAT_CUSTOM_CONFIG
  if (Options.TemporalTBI &&
      Options.Storage == FlexFatSanitizerOptions::TBIStorage::Shadow) {
    M.getContext().emitError(
        "FlexFat TBI shadow storage requires a POW2 build");
    return PreservedAnalyses::all();
  }
#endif
  FlexFatSanitizer Sanitizer(M, Options);
  if (!Sanitizer.run())
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

FlexFatSanitizerFunctionPass::FlexFatSanitizerFunctionPass(
    const FlexFatSanitizerOptions &Options)
    : Options(Options) {}

PreservedAnalyses FlexFatSanitizerFunctionPass::run(Function &F,
                                                    FunctionAnalysisManager &) {
#ifdef FLEXFAT_CUSTOM_CONFIG
  if (Options.TemporalTBI &&
      Options.Storage == FlexFatSanitizerOptions::TBIStorage::Shadow) {
    F.getContext().emitError(
        "FlexFat TBI shadow storage requires a POW2 build");
    return PreservedAnalyses::all();
  }
#endif
  FlexFatSanitizer Sanitizer(*F.getParent(), Options);
  if (!Sanitizer.runFunction(F))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}
