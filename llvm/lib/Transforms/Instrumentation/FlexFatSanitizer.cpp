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
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <algorithm>
#include <optional>

// When the build generates a custom size-class config, pull in the tables so
// the pass can emit the right IR (AND vs. 128-bit magic multiply).
#ifdef FLEXFAT_CUSTOM_CONFIG
#include "flexfat_config_generated.h"
#endif

using namespace llvm;

#define DEBUG_TYPE "flexfat"

STATISTIC(NumInstrumentedLoads, "Number of loads instrumented");
STATISTIC(NumInstrumentedStores, "Number of stores instrumented");
STATISTIC(NumInstrumentedAtomics, "Number of atomic operations instrumented");
STATISTIC(NumInstrumentedMemIntrinsics,
          "Number of mem intrinsics instrumented");
STATISTIC(NumInstrumentedEscapes, "Number of pointer escapes instrumented");

namespace {

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
        IntptrTy(DL.getIntPtrType(M.getContext())), TablesBase(kTablesBase) {}

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
  DenseMap<Value *, BoundsRecord> Bounds;
  DenseMap<Value *, Value *> RecoveredBases;
  DenseMap<Value *, Value *> SafeTableIndices;
  unsigned BoundsIRGeneration = 0;

  FunctionCallee ReportOobFn = nullptr;
  FunctionCallee WarnOobFn = nullptr;

  FunctionCallee getReportOobFn();
  FunctionCallee getWarnOobFn();

  bool instrumentFunction(Function &F);
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
  emitDynamicBaseMagic(IRBuilder<> &IRB, Value *PtrInt, Value *RegionIndex);

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
    return IRB.CreateLoad(ElemTy, GEP);
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
// emitDynamicBaseMagic
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
//   %corrected  = sub i64 %candidate, %alloc_size
//   %base       = select i1 %too_high, i64 %corrected, i64 %candidate
// ---------------------------------------------------------------------------
std::pair<Value *, Value *>
FlexFatSanitizer::emitDynamicBaseMagic(IRBuilder<> &IRB, Value *PtrInt,
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
  Value *Candidate = IRB.CreateMul(Idx, AllocSize, "flexfat.base.candidate");
  Value *QuotientTooHigh =
      IRB.CreateICmpUGT(Candidate, PtrInt, "flexfat.quotient.high");
  Value *CorrectedCandidate =
      IRB.CreateSub(Candidate, AllocSize, "flexfat.base.corrected");
  Value *Base = IRB.CreateSelect(QuotientTooHigh, CorrectedCandidate,
                                 Candidate, "flexfat.base.int");

  return {AllocSize, Base};
}
#endif // FLEXFAT_CUSTOM_CONFIG

// Emit the OOB-check block given a pre-computed (Base, AllocSize, PtrInt).
void FlexFatSanitizer::emitOobCheck(IRBuilder<> &IRB, Value *PtrInt,
                                    Value *Base, Value *AllocSize,
                                    uint64_t FixedAccessSize,
                                    Value *DynAccessSize,
                                    Instruction *InsertBefore, CheckKind Kind) {
  Value *IsOOB = nullptr;
  if (!FixedAccessSize && !DynAccessSize) {
    // C and C++ permit an exact one-past pointer value to escape, although
    // dereferencing it remains invalid; offsets beyond one-past still report.
    // This deliberately differs from released LowFat's >= escape predicate.
    Value *Diff = IRB.CreateSub(PtrInt, Base);
    IsOOB = isEscapeCheck(Kind) ? IRB.CreateICmpUGT(Diff, AllocSize)
                                : IRB.CreateICmpUGE(Diff, AllocSize);
  } else {
    Value *AccessSize = DynAccessSize;
    if (!AccessSize)
      AccessSize = ConstantInt::get(IntptrTy, FixedAccessSize);
    Value *Diff = IRB.CreateSub(PtrInt, Base);
    Value *TooWide = IRB.CreateICmpUGT(AccessSize, AllocSize);
    Value *Limit = IRB.CreateSub(AllocSize, AccessSize);
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
  OobIRB.CreateCall(OobFn, {PtrInt, Base, AllocSize, IsWriteVal});
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
  if (Options.Mode == FlexFatSanitizerOptions::FlexFatMode::RightAlign)
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
  Value *PtrInt =
      IRB.CreatePtrToInt(CompanionBase, IntptrTy, "flexfat.root.int");
  Value *RegionIndex = getSafeTableIndex(IRB, PtrInt);

#ifdef FLEXFAT_CUSTOM_CONFIG
  auto [_, BaseInt] = emitDynamicBaseMagic(IRB, PtrInt, RegionIndex);
#else
  Type *I64Ty = Type::getInt64Ty(M.getContext());
  Value *Mask64 = loadFromFixedTable(IRB, TablesBase + 3 * kTablesOffset, I64Ty,
                                     RegionIndex);
  if (auto *MaskLoad = dyn_cast<Instruction>(Mask64))
    MaskLoad->setName("flexfat.mask");
  Value *Mask = IRB.CreateZExtOrTrunc(Mask64, IntptrTy);
  Value *BaseInt = IRB.CreateAnd(PtrInt, Mask, "flexfat.base.int");
#endif

  Value *Base =
      IRB.CreateIntToPtr(BaseInt, CompanionBase->getType(), "flexfat.base");
  RecoveredBases[CompanionBase] = Base;
  SafeTableIndices[Base] = RegionIndex;
  ++BoundsIRGeneration;
  return Base;
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
    markNoSanitize(BasePhi);
    if (IndexPhi)
      markNoSanitize(IndexPhi);
    BoundsIRGeneration += IndexPhi ? 2 : 1;
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

    bool AllNonFlexFat = true;
    std::optional<uint64_t> StaticUpperBound = UINT64_MAX;
    for (unsigned I = 0; I != NumIncoming; ++I) {
      BoundsRecord Incoming = getBounds(Phi->getIncomingValue(I));
      BasePhi->setIncomingValue(I, Incoming.CompanionBase);
      if (IndexPhi) {
        Value *IncomingIndex = getMemoizedTableIndex(Incoming.CompanionBase);
        assert(IncomingIndex &&
               "companion base is missing its safe table index");
        IndexPhi->setIncomingValue(I, IncomingIndex);
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
    // C and C++ allow an exact one-past value to escape, but never to be
    // dereferenced; beyond-one-past values retain their checks.  This static
    // rule intentionally diverges from released LowFat by keeping exact
    // one-past scalar point checks while preserving width-aware elision.
    bool IsStaticallyValid =
        isEscapeCheck(Kind) ? FixedAccessSize <= *PtrBounds.StaticUpperBound
                            : FixedAccessSize < *PtrBounds.StaticUpperBound;
    if (FixedAccessSize && FixedAccessSize == *PtrBounds.StaticUpperBound)
      IsStaticallyValid = true;
    if (IsStaticallyValid)
      return false;
  }

  IRBuilder<> IRB(I);
  IRB.SetNoSanitizeMetadata();
  Value *PtrInt = IRB.CreatePtrToInt(Ptr, IntptrTy);
  Value *BasePtr = PtrBounds.CompanionBase;
  Value *BasePtrInt = IRB.CreatePtrToInt(BasePtr, IntptrTy);
  Value *SizeInt =
      DynAccessSize ? IRB.CreateZExtOrTrunc(DynAccessSize, IntptrTy) : nullptr;

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
  Value *AllocSize64 = loadFromFixedTable(
      *CheckIRB, TablesBase + 0 * kTablesOffset, I64Ty, RegionIndex);
  Value *AllocSize = CheckIRB->CreateZExtOrTrunc(AllocSize64, IntptrTy);

  emitOobCheck(*CheckIRB, PtrInt, BasePtrInt, AllocSize, FixedAccessSize,
               SizeInt, CheckInsertBefore, Kind);
  return true;
}

bool FlexFatSanitizer::instrumentMemoryAccess(Instruction *I, Value *Ptr,
                                              Type *AccessTy) {
  TypeSize AccessSize = DL.getTypeStoreSize(AccessTy);
  if (AccessSize.isScalable())
    return false;
  bool IsWrite =
      isa<StoreInst>(I) || isa<AtomicRMWInst>(I) || isa<AtomicCmpXchgInst>(I);
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
  if (!instrumentPointerCheck(I, Ptr, 0, nullptr, Kind))
    return false;
  NumInstrumentedEscapes++;
  return true;
}

void FlexFatSanitizer::prepareBounds(Instruction *I) {
  auto Prepare = [this](Value *Ptr) { (void)getBounds(Ptr); };

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

bool FlexFatSanitizer::instrumentFunction(Function &F) {
  if (F.getName().starts_with("__flexfat_"))
    return false;

  Bounds.clear();
  RecoveredBases.clear();
  SafeTableIndices.clear();
  BoundsIRGeneration = 0;
  bool Modified = false;
  SmallVector<Instruction *, 16> ToInstrument;

  for (auto &BB : F) {
    for (auto &I : BB) {
      if (shouldSkipInstruction(&I))
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

  // LowFat-style phase 2: resolve every provenance root and memoize one
  // recovered allocation base before CFG-changing checks are inserted.
  for (Instruction *I : ToInstrument)
    if (!I->hasMetadata(LLVMContext::MD_nosanitize))
      prepareBounds(I);

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
  if (!Options.InternalModuleSetupOnly_)
    for (Function &F : M)
      Modified |= runFunction(F);

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
  if (Options.Mode == FlexFatSanitizerOptions::FlexFatMode::RightAlign &&
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
  FlexFatSanitizer Sanitizer(*F.getParent(), Options);
  if (!Sanitizer.runFunction(F))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}
