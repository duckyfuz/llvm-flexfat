//===- FlexFatSanitizer.cpp - FlexFat Pointer Bounds Checking ---------------===//
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
#include "llvm/ADT/Statistic.h"
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
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

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
STATISTIC(NumInstrumentedMemIntrinsics, "Number of mem intrinsics instrumented");
STATISTIC(NumInstrumentedGEPs, "Number of GEP pointer-arithmetic operations instrumented");

namespace {

/// Helper class to instrument a module with FlexFat bounds checks.
class FlexFatSanitizer {
public:
  FlexFatSanitizer(Module &M, const FlexFatSanitizerOptions &Options)
      : M(M), Options(Options), DL(M.getDataLayout()),
        IntptrTy(DL.getIntPtrType(M.getContext())),
        UseDarwinMetadataGuard(Triple(M.getTargetTriple()).isOSDarwin()),
        TablesBase(kTablesBase) {}

  bool run();
  bool runFunction(Function &F);

private:
  Module &M;
  const FlexFatSanitizerOptions &Options;
  const DataLayout &DL;
  Type *IntptrTy;
  const bool UseDarwinMetadataGuard;
  const uint64_t TablesBase;

  FunctionCallee ReportOobFn = nullptr;
  FunctionCallee WarnOobFn = nullptr;

  FunctionCallee getReportOobFn();
  FunctionCallee getWarnOobFn();

  bool instrumentFunction(Function &F);
  bool instrumentMemoryAccess(Instruction *I, Value *Ptr, Type *AccessTy);
  bool instrumentMemoryRange(Instruction *I, Value *Ptr, Value *Size,
                             bool IsWrite);
  bool instrumentGEP(GetElementPtrInst *GEP);
  bool shouldSkipInstruction(const Instruction *I) const;
  void markInstrumented(Instruction *I);
  void markNoSanitize(Instruction *I);
  MDNode *getInstrumentedMetadata();
  MDNode *getNoSanitizeMetadata();

  // Emit the OOB-check block given a pre-computed (Base, AllocSize, PtrInt).
  void emitOobCheck(IRBuilder<> &IRB, Value *PtrInt, Value *Base,
                    Value *AllocSize, uint64_t FixedAccessSize,
                    Value *DynAccessSize, Instruction *InsertBefore,
                    bool IsWrite);

#ifdef FLEXFAT_CUSTOM_CONFIG
  // Build the IR to compute (AllocSize, Base) using runtime table lookups when
  // the region index is only known at runtime.
  std::pair<Value *, Value *> emitDynamicBaseMagic(IRBuilder<> &IRB,
                                                    Value *PtrInt,
                                                    Value *RegionIndex);

  // Lazily get or create the global arrays that mirror the generated tables.
  GlobalVariable *getSizesTable();
  GlobalVariable *getMagicsTable();

  GlobalVariable *SizesTableGV  = nullptr;
  GlobalVariable *MagicsTableGV = nullptr;
#endif

  // Helper: GEP + load from a fixed absolute base at runtime index.
  Value *loadFromFixedTable(IRBuilder<> &IRB, uint64_t TableBase,
                            Type *ElemTy, Value *Idx) {
    LLVMContext &Ctx = M.getContext();
    Type *I64Ty = Type::getInt64Ty(Ctx);
    Value *BasePtr = IRB.CreateIntToPtr(ConstantInt::get(I64Ty, TableBase),
                                        PointerType::getUnqual(Ctx));
    Value *Idx64   = IRB.CreateZExtOrTrunc(Idx, I64Ty);
    Value *GEP     = IRB.CreateInBoundsGEP(ElemTy, BasePtr, {Idx64});
    return IRB.CreateLoad(ElemTy, GEP);
  }

  // Constants (kept in sync with flexfat_config.h / flexfat_config_generated.h)
#ifdef FLEXFAT_CUSTOM_CONFIG
  static constexpr uint64_t RegionBase     = 0x100000000000ULL;
  static constexpr uint64_t RegionSizeLog  = FLEXFAT_REGION_SIZE_LOG;   // 35
  static constexpr uint64_t NumSizeClasses = FLEXFAT_NUM_SIZE_CLASSES;
  static constexpr uint64_t MinSizeLog     = 4;  // unused in custom mode
#else
  static constexpr uint64_t RegionBase     = 0x100000000000ULL;
  static constexpr uint64_t RegionSizeLog  = 32;
  static constexpr uint64_t NumSizeClasses = 27; // kMaxSizeLog(30) - kMinSizeLog(4) + 1
  static constexpr uint64_t MinSizeLog     = 4;
#endif

  // Fixed absolute addresses for metadata tables (must match flexfat_rtl.cpp)
  static constexpr uint64_t kTablesBase   = 0x118000000000ULL;
  static constexpr uint64_t kTablesOffset = 0x1000000ULL;

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
// Custom-config pass helpers: table-accessor lazy initializers
// ---------------------------------------------------------------------------
//
// We mirror the generated size and reciprocal tables from
// flexfat_config_generated.h as LLVM GlobalVariable constants embedded inside the
// module. This lets the optimiser see them as constant loads and fold them
// through inlining.
//
// Arrays are initialised once (lazy, per-module) with the same values that
// flexfat_config_gen baked into the header.

static GlobalVariable *makeConstantArray(Module &M, StringRef Name,
                                          ArrayRef<uint64_t> Data,
                                          Type *ElemTy) {
  SmallVector<Constant *, 64> Elems;
  for (uint64_t V : Data)
    Elems.push_back(ConstantInt::get(ElemTy, V));
  auto *ArrayTy = ArrayType::get(ElemTy, Elems.size());
  auto *Init    = ConstantArray::get(ArrayTy, Elems);
  auto *GV = new GlobalVariable(M, ArrayTy, /*isConstant=*/true,
                                GlobalValue::PrivateLinkage, Init, Name);
  GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  return GV;
}

GlobalVariable *FlexFatSanitizer::getSizesTable() {
  if (!SizesTableGV) {
    SizesTableGV = M.getGlobalVariable("__flexfat_gen_sizes", /*AllowLocal=*/true);
  }
  if (!SizesTableGV) {
    SmallVector<uint64_t, 64> D(kFlexFatGenSizes,
                                 kFlexFatGenSizes + FLEXFAT_NUM_SIZE_CLASSES);
    SizesTableGV = makeConstantArray(M, "__flexfat_gen_sizes", D,
                                     Type::getInt64Ty(M.getContext()));
  }
  return SizesTableGV;
}

GlobalVariable *FlexFatSanitizer::getMagicsTable() {
  if (!MagicsTableGV) {
    MagicsTableGV = M.getGlobalVariable("__flexfat_gen_magics", /*AllowLocal=*/true);
  }
  if (!MagicsTableGV) {
    SmallVector<uint64_t, 64> D(kFlexFatGenMagics,
                                 kFlexFatGenMagics + FLEXFAT_NUM_SIZE_CLASSES);
    MagicsTableGV = makeConstantArray(M, "__flexfat_gen_magics", D,
                                      Type::getInt64Ty(M.getContext()));
  }
  return MagicsTableGV;
}

// ---------------------------------------------------------------------------
// emitDynamicBaseMagic
//
// Given a runtime RegionIndex, emit IR that loads the per-class size and
// magic from the embedded tables and returns (AllocSize, Base) as IntptrTy.
//
// Generated IR (conceptually):
//
//   %alloc_size = load i64, ptr getelementptr(__flexfat_gen_sizes, 0, %region_idx)
//   %magic      = load i64, ptr getelementptr(__flexfat_gen_magics, 0, %region_idx)
//
//   ; Reciprocal fixed-point base recovery for every class
//   %ptr128     = zext i64 %ptr to i128
//   %magic128   = zext i64 %magic to i128
//   %mul128     = mul i128 %ptr128, %magic128
//   %idx128     = lshr i128 %mul128, 64
//   %idx        = trunc i128 %idx128 to i64
//   %base_mul   = mul i64 %idx, %alloc_size
// ---------------------------------------------------------------------------
std::pair<Value *, Value *>
FlexFatSanitizer::emitDynamicBaseMagic(IRBuilder<> &IRB, Value *PtrInt,
                                       Value *RegionIndex) {
  LLVMContext &Ctx = M.getContext();
  Type *I64Ty  = Type::getInt64Ty(Ctx);
  Type *I128Ty = Type::getInt128Ty(Ctx);

  Value *AllocSize64 = loadFromFixedTable(IRB, TablesBase + 0 * kTablesOffset,
                                          I64Ty, RegionIndex);

  // Narrow to IntptrTy (which is i64 on 64-bit targets)
  Value *AllocSize = IRB.CreateZExtOrTrunc(AllocSize64, IntptrTy);

  // In custom-config mode we deliberately use the reciprocal-multiply path
  // for every class, including power-of-two sizes, so runtime and
  // instrumentation recover bases the same way.
  Value *Magic64     = loadFromFixedTable(IRB, TablesBase + 1 * kTablesOffset,
                                          I64Ty, RegionIndex);

  Value *Ptr128   = IRB.CreateZExt(PtrInt, I128Ty);
  Value *Magic128 = IRB.CreateZExt(IRB.CreateZExtOrTrunc(Magic64, IntptrTy),
                                   I128Ty);
  Value *Mul128   = IRB.CreateMul(Ptr128, Magic128);
  Value *Idx128   = IRB.CreateLShr(Mul128, ConstantInt::get(I128Ty, 64));
  Value *Idx      = IRB.CreateTrunc(Idx128, IntptrTy);
  Value *BaseMul  = IRB.CreateMul(Idx, AllocSize);

  return {AllocSize, BaseMul};
}
#endif  // FLEXFAT_CUSTOM_CONFIG

// Emit the OOB-check block given a pre-computed (Base, AllocSize, PtrInt).
void FlexFatSanitizer::emitOobCheck(IRBuilder<> &IRB, Value *PtrInt, Value *Base,
                                  Value *AllocSize, uint64_t FixedAccessSize,
                                  Value *DynAccessSize,
                                  Instruction *InsertBefore, bool IsWrite) {
  Value *IsOOB = nullptr;
  if (!FixedAccessSize && !DynAccessSize) {
    // Compact GEP check: OOB iff (ptr - base) >= alloc_size (unsigned).
    // This catches both underflow and overflow without separate compares.
    Value *Diff = IRB.CreateSub(PtrInt, Base);
    IsOOB = IRB.CreateICmpUGE(Diff, AllocSize);
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
  Value *IsWriteVal = ConstantInt::get(I8Ty, IsWrite ? 1 : 0);
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

bool FlexFatSanitizer::instrumentMemoryAccess(Instruction *I, Value *Ptr,
                                              Type *AccessTy) {
  TypeSize AccessSize = DL.getTypeStoreSize(AccessTy);
  if (AccessSize.isScalable())
    return false;
  uint64_t FixedAccessSize = AccessSize.getFixedValue();
  markInstrumented(I);

  IRBuilder<> IRB(I);
  IRB.SetNoSanitizeMetadata();
  Value *PtrInt = IRB.CreatePtrToInt(Ptr, IntptrTy);

  // 1. Get region index: (Ptr - RegionBase) >> RegionSizeLog
  Value *RegionBaseVal = ConstantInt::get(IntptrTy, RegionBase);
  Value *RegionOffset  = IRB.CreateSub(PtrInt, RegionBaseVal);
  Value *RegionIndex   = IRB.CreateLShr(RegionOffset, RegionSizeLog);

  // 2. Darwin-first safety guard:
  // On Darwin, prove the pointer is in a valid FlexFat region before touching
  // the fixed metadata tables. Other targets keep the current table-driven
  // classification for now.
  LLVMContext &Ctx = M.getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  Value *AllocSize64 = nullptr;
  Value *IsFlexFat = nullptr;
  if (UseDarwinMetadataGuard) {
    Value *MaxRegion = ConstantInt::get(IntptrTy, NumSizeClasses);
    IsFlexFat = IRB.CreateICmpULT(RegionIndex, MaxRegion);
  } else {
    AllocSize64 = loadFromFixedTable(IRB, TablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
    IsFlexFat = IRB.CreateICmpNE(AllocSize64, ConstantInt::get(I64Ty, 0));
  }

  Instruction *ThenTerm = SplitBlockAndInsertIfThen(IsFlexFat, I, false);
  markNoSanitize(ThenTerm);
  IRBuilder<> ThenIRB(ThenTerm);
  ThenIRB.SetNoSanitizeMetadata();

  bool IsWrite = isa<StoreInst>(I) || isa<AtomicRMWInst>(I) ||
                 isa<AtomicCmpXchgInst>(I);

  if (!AllocSize64)
    AllocSize64 = loadFromFixedTable(ThenIRB, TablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
  Value *AllocSize = ThenIRB.CreateZExtOrTrunc(AllocSize64, IntptrTy);

#ifdef FLEXFAT_CUSTOM_CONFIG
  auto [_, Base] = emitDynamicBaseMagic(ThenIRB, PtrInt, RegionIndex);
#else
  Value *Mask64      = loadFromFixedTable(ThenIRB, TablesBase + 3 * kTablesOffset,
                                          I64Ty, RegionIndex);
  Value *Mask      = ThenIRB.CreateZExtOrTrunc(Mask64, IntptrTy);
  Value *Base      = ThenIRB.CreateAnd(PtrInt, Mask);
#endif

  emitOobCheck(ThenIRB, PtrInt, Base, AllocSize, FixedAccessSize, nullptr,
               ThenTerm, IsWrite);

  if (isa<LoadInst>(I))          NumInstrumentedLoads++;
  else if (isa<StoreInst>(I))    NumInstrumentedStores++;
  else                           NumInstrumentedAtomics++;

  return true;
}

bool FlexFatSanitizer::instrumentMemoryRange(Instruction *I, Value *Ptr,
                                             Value *Size, bool IsWrite) {
  markInstrumented(I);
  IRBuilder<> IRB(I);
  IRB.SetNoSanitizeMetadata();
  Value *PtrInt  = IRB.CreatePtrToInt(Ptr, IntptrTy);
  Value *SizeInt = IRB.CreateZExtOrTrunc(Size, IntptrTy);

  Value *RegionBaseVal = ConstantInt::get(IntptrTy, RegionBase);
  Value *RegionOffset  = IRB.CreateSub(PtrInt, RegionBaseVal);
  Value *RegionIndex   = IRB.CreateLShr(RegionOffset, RegionSizeLog);

  LLVMContext &Ctx = M.getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  Value *AllocSize64 = nullptr;
  Value *IsFlexFat = nullptr;
  if (UseDarwinMetadataGuard) {
    Value *MaxRegion = ConstantInt::get(IntptrTy, NumSizeClasses);
    IsFlexFat = IRB.CreateICmpULT(RegionIndex, MaxRegion);
  } else {
    AllocSize64 = loadFromFixedTable(IRB, TablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
    IsFlexFat = IRB.CreateICmpNE(AllocSize64, ConstantInt::get(I64Ty, 0));
  }

  Instruction *ThenTerm = SplitBlockAndInsertIfThen(IsFlexFat, I, false);
  markNoSanitize(ThenTerm);
  IRBuilder<> ThenIRB(ThenTerm);
  ThenIRB.SetNoSanitizeMetadata();

  if (!AllocSize64)
    AllocSize64 = loadFromFixedTable(ThenIRB, TablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
  Value *AllocSize = ThenIRB.CreateZExtOrTrunc(AllocSize64, IntptrTy);

#ifdef FLEXFAT_CUSTOM_CONFIG
  auto [_, Base] = emitDynamicBaseMagic(ThenIRB, PtrInt, RegionIndex);
#else
  Value *Mask64      = loadFromFixedTable(ThenIRB, TablesBase + 3 * kTablesOffset,
                                          I64Ty, RegionIndex);
  Value *Mask      = ThenIRB.CreateZExtOrTrunc(Mask64, IntptrTy);
  Value *Base      = ThenIRB.CreateAnd(PtrInt, Mask);
#endif

  emitOobCheck(ThenIRB, PtrInt, Base, AllocSize, 0, SizeInt, ThenTerm, IsWrite);

  NumInstrumentedMemIntrinsics++;
  return true;
}

// ---------------------------------------------------------------------------
// instrumentGEP
//
// Instruments a GetElementPtr instruction to catch OOB pointer arithmetic
// before the out-of-bounds pointer can escape to a neighbouring allocation
// slot (where a load/store check would misidentify it as valid).
//
// Key insight: use the SOURCE pointer's allocation bounds, not the result's.
//
//   ptr = p + 48  (src=p, result=p+48, alloc=48)
//
//   Load/store check on p+48:
//     GetBase(p+48) = p+48  ← attributed to next slot
//     End = p+96  →  p+49 ≤ p+96  →  NOT OOB (false negative)
//
//   GEP check using src=p:
//     GetBase(p) = p, End = p+48
//     result p+48 ≥ End  →  OOB (detected!)
// ---------------------------------------------------------------------------
bool FlexFatSanitizer::instrumentGEP(GetElementPtrInst *GEP) {
  // We need an insertion point after the GEP so we can use its result value.
  Instruction *InsertPt = GEP->getNextNode();
  if (!InsertPt)
    return false;
  markInstrumented(GEP);

  IRBuilder<> IRB(InsertPt);
  IRB.SetNoSanitizeMetadata();

  // SOURCE pointer — determines the allocation the GEP started from.
  Value *SrcPtr = GEP->getPointerOperand();
  Value *SrcInt = IRB.CreatePtrToInt(SrcPtr, IntptrTy);

  // RESULT pointer — what we're checking stays within [Base, Base+AllocSize).
  Value *ResInt = IRB.CreatePtrToInt(GEP, IntptrTy);

  // 1. Compute Base and AllocSize from the SOURCE pointer.
  Value *RegionBaseVal = ConstantInt::get(IntptrTy, RegionBase);
  Value *RegionOffset  = IRB.CreateSub(SrcInt, RegionBaseVal);
  Value *RegionIndex   = IRB.CreateLShr(RegionOffset, RegionSizeLog);

  LLVMContext &Ctx = M.getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  Value *AllocSize64 = nullptr;
  Value *IsFlexFat = nullptr;
  if (UseDarwinMetadataGuard) {
    Value *MaxRegion = ConstantInt::get(IntptrTy, NumSizeClasses);
    IsFlexFat = IRB.CreateICmpULT(RegionIndex, MaxRegion);
  } else {
    AllocSize64 = loadFromFixedTable(IRB, TablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
    IsFlexFat = IRB.CreateICmpNE(AllocSize64, ConstantInt::get(I64Ty, 0));
  }

  Instruction *ThenTerm = SplitBlockAndInsertIfThen(IsFlexFat, InsertPt, false);
  markNoSanitize(ThenTerm);
  IRBuilder<> ThenIRB(ThenTerm);
  ThenIRB.SetNoSanitizeMetadata();

  if (!AllocSize64)
    AllocSize64 = loadFromFixedTable(ThenIRB, TablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
  Value *AllocSize = ThenIRB.CreateZExtOrTrunc(AllocSize64, IntptrTy);

#ifdef FLEXFAT_CUSTOM_CONFIG
  auto [_, Base] = emitDynamicBaseMagic(ThenIRB, SrcInt, RegionIndex);
#else
  Value *Mask64      = loadFromFixedTable(ThenIRB, TablesBase + 3 * kTablesOffset,
                                          I64Ty, RegionIndex);
  Value *Mask      = ThenIRB.CreateZExtOrTrunc(Mask64, IntptrTy);
  Value *Base      = ThenIRB.CreateAnd(SrcInt, Mask);
#endif

  // 2. OOB if result underflows (< Base) or reaches/passes the end (>= Base+AllocSize).
  emitOobCheck(ThenIRB, ResInt, Base, AllocSize, 0, nullptr, ThenTerm, false);

  NumInstrumentedGEPs++;
  return true;
}

bool FlexFatSanitizer::instrumentFunction(Function &F) {
  if (F.getName().starts_with("__flexfat_"))
    return false;

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
      else if (isa<GetElementPtrInst>(&I))
        ToInstrument.push_back(&I);
    }
  }

  for (Instruction *I : ToInstrument) {
    if (I->hasMetadata(LLVMContext::MD_nosanitize))
      continue;
    if (auto *LI = dyn_cast<LoadInst>(I))
      Modified |= instrumentMemoryAccess(I, LI->getPointerOperand(), LI->getType());
    else if (auto *SI = dyn_cast<StoreInst>(I))
      Modified |= instrumentMemoryAccess(I, SI->getPointerOperand(), SI->getValueOperand()->getType());
    else if (auto *RMW = dyn_cast<AtomicRMWInst>(I))
      Modified |= instrumentMemoryAccess(I, RMW->getPointerOperand(), RMW->getValOperand()->getType());
    else if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(I))
      Modified |= instrumentMemoryAccess(I, CmpXchg->getPointerOperand(), CmpXchg->getNewValOperand()->getType());
    else if (auto *MS = dyn_cast<MemSetInst>(I))
      Modified |= instrumentMemoryRange(I, MS->getDest(), MS->getLength(), true);
    else if (auto *MT = dyn_cast<MemTransferInst>(I)) {
      Modified |= instrumentMemoryRange(I, MT->getDest(), MT->getLength(), true);
      Modified |= instrumentMemoryRange(I, MT->getSource(), MT->getLength(), false);
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(I))
      Modified |= instrumentGEP(GEP);
  }
  return Modified;
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

FlexFatSanitizerPass::FlexFatSanitizerPass(const FlexFatSanitizerOptions &Options)
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

PreservedAnalyses FlexFatSanitizerFunctionPass::run(
    Function &F, FunctionAnalysisManager &) {
  FlexFatSanitizer Sanitizer(*F.getParent(), Options);
  if (!Sanitizer.runFunction(F))
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}
