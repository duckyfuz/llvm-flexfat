//===- LowFatSanitizer.cpp - LowFat Pointer Bounds Checking ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LowFat Sanitizer instrumentation pass.
//
// LowFat pointers encode allocation bounds information directly in the pointer
// value through careful memory layout. This pass instruments memory accesses
// to call runtime functions that verify bounds using this encoded information.
//
// For every load, store, atomic, or memory intrinsic on a pointer that could
// be heap-derived, the pass:
//   1. Computes the region index: (ptr - RegionBase) >> RegionSizeLog
//   2. Guards on a valid region (alloc_size != 0)
//   3. Recovers base via AND: base = ptr & mask
//   4. Checks: (ptr - base) >= alloc_size  =>  OOB
//
// GEP instructions are also instrumented to catch pointer arithmetic that
// escapes a slot boundary before any load/store is reached.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/LowFatSanitizer.h"
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

using namespace llvm;

#define DEBUG_TYPE "lowfat"

STATISTIC(NumInstrumentedLoads, "Number of loads instrumented");
STATISTIC(NumInstrumentedStores, "Number of stores instrumented");
STATISTIC(NumInstrumentedAtomics, "Number of atomic operations instrumented");
STATISTIC(NumInstrumentedMemIntrinsics,
          "Number of mem intrinsics instrumented");
STATISTIC(NumInstrumentedGEPs,
          "Number of GEP pointer-arithmetic operations instrumented");

namespace {

class LowFatSanitizer {
public:
  LowFatSanitizer(Module &M, const LowFatSanitizerOptions &Options)
      : M(M), Options(Options), DL(M.getDataLayout()),
        IntptrTy(DL.getIntPtrType(M.getContext())),
        UseDarwinMetadataGuard(Triple(M.getTargetTriple()).isOSDarwin()) {}

  bool run();

private:
  Module &M;
  const LowFatSanitizerOptions &Options;
  const DataLayout &DL;
  Type *IntptrTy;
  const bool UseDarwinMetadataGuard;

  FunctionCallee ReportOobFn = nullptr;
  FunctionCallee WarnOobFn = nullptr;

  FunctionCallee getReportOobFn();
  FunctionCallee getWarnOobFn();

  bool instrumentFunction(Function &F);
  bool instrumentMemoryAccess(Instruction *I, Value *Ptr, Type *AccessTy);
  bool instrumentMemoryRange(Instruction *I, Value *Ptr, Value *Size,
                             bool IsWrite);
  bool instrumentGEP(GetElementPtrInst *GEP);

  void emitOobCheck(IRBuilder<> &IRB, Value *PtrInt, Value *Base,
                    Value *AllocSize, uint64_t FixedAccessSize,
                    Value *DynAccessSize, Instruction *InsertBefore,
                    bool IsWrite);

  // Load from a fixed absolute-addressed metadata table at runtime index.
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

  // Constants — must match lf_config.h / lf_rtl.cpp.
  static constexpr uint64_t RegionBase     = 0x100000000000ULL;
  static constexpr uint64_t RegionSizeLog  = 32;
  static constexpr uint64_t NumSizeClasses = 27;

  // Fixed absolute addresses for metadata tables.
  static constexpr uint64_t kTablesBase   = 0x118000000000ULL;
  static constexpr uint64_t kTablesOffset = 0x1000000ULL;  // 16 MB
};

//===----------------------------------------------------------------------===//
// Runtime function declarations
//===----------------------------------------------------------------------===//

FunctionCallee LowFatSanitizer::getReportOobFn() {
  if (!ReportOobFn) {
    Type *VoidTy = Type::getVoidTy(M.getContext());
    Type *I8Ty = Type::getInt8Ty(M.getContext());
    ReportOobFn = M.getOrInsertFunction(
        "__lf_report_oob",
        FunctionType::get(VoidTy, {IntptrTy, IntptrTy, IntptrTy, I8Ty},
                          false));
    if (auto *F = dyn_cast<Function>(ReportOobFn.getCallee())) {
      F->addFnAttr(Attribute::NoReturn);
      F->setMemoryEffects(MemoryEffects::inaccessibleMemOnly());
    }
  }
  return ReportOobFn;
}

FunctionCallee LowFatSanitizer::getWarnOobFn() {
  if (!WarnOobFn) {
    Type *VoidTy = Type::getVoidTy(M.getContext());
    Type *I8Ty = Type::getInt8Ty(M.getContext());
    WarnOobFn = M.getOrInsertFunction(
        "__lf_warn_oob",
        FunctionType::get(VoidTy, {IntptrTy, IntptrTy, IntptrTy, I8Ty},
                          false));
    if (auto *F = dyn_cast<Function>(WarnOobFn.getCallee())) {
      F->addFnAttr(Attribute::NoUnwind);
      F->setMemoryEffects(MemoryEffects::inaccessibleMemOnly());
    }
  }
  return WarnOobFn;
}

//===----------------------------------------------------------------------===//
// OOB check emission
//===----------------------------------------------------------------------===//

void LowFatSanitizer::emitOobCheck(IRBuilder<> &IRB, Value *PtrInt,
                                    Value *Base, Value *AllocSize,
                                    uint64_t FixedAccessSize,
                                    Value *DynAccessSize,
                                    Instruction *InsertBefore, bool IsWrite) {
  Value *IsOOB = nullptr;
  if (!FixedAccessSize && !DynAccessSize) {
    // Compact GEP check: OOB iff (ptr - base) >= alloc_size (unsigned).
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
  IRBuilder<> OobIRB(OobTerm);
  FunctionCallee OobFn = Options.Recover ? getWarnOobFn() : getReportOobFn();
  Type *I8Ty = Type::getInt8Ty(M.getContext());
  Value *IsWriteVal = ConstantInt::get(I8Ty, IsWrite ? 1 : 0);
  OobIRB.CreateCall(OobFn, {PtrInt, Base, AllocSize, IsWriteVal});
}

//===----------------------------------------------------------------------===//
// Instrumentation: loads, stores, atomics
//===----------------------------------------------------------------------===//

bool LowFatSanitizer::instrumentMemoryAccess(Instruction *I, Value *Ptr,
                                              Type *AccessTy) {
  TypeSize AccessSize = DL.getTypeStoreSize(AccessTy);
  if (AccessSize.isScalable())
    return false;
  uint64_t FixedAccessSize = AccessSize.getFixedValue();

  IRBuilder<> IRB(I);
  Value *PtrInt = IRB.CreatePtrToInt(Ptr, IntptrTy);

  // 1. Region index: (ptr - RegionBase) >> RegionSizeLog
  Value *RegionBaseVal = ConstantInt::get(IntptrTy, RegionBase);
  Value *RegionOffset = IRB.CreateSub(PtrInt, RegionBaseVal);
  Value *RegionIndex = IRB.CreateLShr(RegionOffset, RegionSizeLog);

  // 2. Guard: is this a valid LowFat pointer?
  LLVMContext &Ctx = M.getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  Value *AllocSize64 = nullptr;
  Value *IsLowFat = nullptr;

  if (UseDarwinMetadataGuard) {
    Value *MaxRegion = ConstantInt::get(IntptrTy, NumSizeClasses);
    IsLowFat = IRB.CreateICmpULT(RegionIndex, MaxRegion);
  } else {
    AllocSize64 = loadFromFixedTable(IRB, kTablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
    IsLowFat = IRB.CreateICmpNE(AllocSize64, ConstantInt::get(I64Ty, 0));
  }

  Instruction *ThenTerm = SplitBlockAndInsertIfThen(IsLowFat, I, false);
  IRBuilder<> ThenIRB(ThenTerm);

  bool IsWrite = isa<StoreInst>(I) || isa<AtomicRMWInst>(I) ||
                 isa<AtomicCmpXchgInst>(I);

  // 3. Load alloc_size and mask from tables.
  if (!AllocSize64)
    AllocSize64 = loadFromFixedTable(ThenIRB, kTablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
  Value *AllocSize = ThenIRB.CreateZExtOrTrunc(AllocSize64, IntptrTy);

  Value *Mask64 = loadFromFixedTable(ThenIRB, kTablesBase + 3 * kTablesOffset,
                                     I64Ty, RegionIndex);
  Value *Mask = ThenIRB.CreateZExtOrTrunc(Mask64, IntptrTy);

  // 4. Recover base: base = ptr & mask
  Value *Base = ThenIRB.CreateAnd(PtrInt, Mask);

  // 5. Emit the OOB check.
  emitOobCheck(ThenIRB, PtrInt, Base, AllocSize, FixedAccessSize, nullptr,
               ThenTerm, IsWrite);

  if (isa<LoadInst>(I))
    NumInstrumentedLoads++;
  else if (isa<StoreInst>(I))
    NumInstrumentedStores++;
  else
    NumInstrumentedAtomics++;

  return true;
}

//===----------------------------------------------------------------------===//
// Instrumentation: memory intrinsics (memset, memcpy, memmove)
//===----------------------------------------------------------------------===//

bool LowFatSanitizer::instrumentMemoryRange(Instruction *I, Value *Ptr,
                                             Value *Size, bool IsWrite) {
  IRBuilder<> IRB(I);
  Value *PtrInt = IRB.CreatePtrToInt(Ptr, IntptrTy);
  Value *SizeInt = IRB.CreateZExtOrTrunc(Size, IntptrTy);

  Value *RegionBaseVal = ConstantInt::get(IntptrTy, RegionBase);
  Value *RegionOffset = IRB.CreateSub(PtrInt, RegionBaseVal);
  Value *RegionIndex = IRB.CreateLShr(RegionOffset, RegionSizeLog);

  LLVMContext &Ctx = M.getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  Value *AllocSize64 = nullptr;
  Value *IsLowFat = nullptr;

  if (UseDarwinMetadataGuard) {
    Value *MaxRegion = ConstantInt::get(IntptrTy, NumSizeClasses);
    IsLowFat = IRB.CreateICmpULT(RegionIndex, MaxRegion);
  } else {
    AllocSize64 = loadFromFixedTable(IRB, kTablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
    IsLowFat = IRB.CreateICmpNE(AllocSize64, ConstantInt::get(I64Ty, 0));
  }

  Instruction *ThenTerm = SplitBlockAndInsertIfThen(IsLowFat, I, false);
  IRBuilder<> ThenIRB(ThenTerm);

  if (!AllocSize64)
    AllocSize64 = loadFromFixedTable(ThenIRB, kTablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
  Value *AllocSize = ThenIRB.CreateZExtOrTrunc(AllocSize64, IntptrTy);

  Value *Mask64 = loadFromFixedTable(ThenIRB, kTablesBase + 3 * kTablesOffset,
                                     I64Ty, RegionIndex);
  Value *Mask = ThenIRB.CreateZExtOrTrunc(Mask64, IntptrTy);
  Value *Base = ThenIRB.CreateAnd(PtrInt, Mask);

  emitOobCheck(ThenIRB, PtrInt, Base, AllocSize, 0, SizeInt, ThenTerm,
               IsWrite);

  NumInstrumentedMemIntrinsics++;
  return true;
}

//===----------------------------------------------------------------------===//
// Instrumentation: GEP (pointer arithmetic)
//===----------------------------------------------------------------------===//

bool LowFatSanitizer::instrumentGEP(GetElementPtrInst *GEP) {
  Instruction *InsertPt = GEP->getNextNode();
  if (!InsertPt)
    return false;

  IRBuilder<> IRB(InsertPt);

  // Use the SOURCE pointer's allocation bounds, not the result's.
  // This catches cross-slot escapes that a load/store check would miss.
  Value *SrcPtr = GEP->getPointerOperand();
  Value *SrcInt = IRB.CreatePtrToInt(SrcPtr, IntptrTy);
  Value *ResInt = IRB.CreatePtrToInt(GEP, IntptrTy);

  Value *RegionBaseVal = ConstantInt::get(IntptrTy, RegionBase);
  Value *RegionOffset = IRB.CreateSub(SrcInt, RegionBaseVal);
  Value *RegionIndex = IRB.CreateLShr(RegionOffset, RegionSizeLog);

  LLVMContext &Ctx = M.getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  Value *AllocSize64 = nullptr;
  Value *IsLowFat = nullptr;

  if (UseDarwinMetadataGuard) {
    Value *MaxRegion = ConstantInt::get(IntptrTy, NumSizeClasses);
    IsLowFat = IRB.CreateICmpULT(RegionIndex, MaxRegion);
  } else {
    AllocSize64 = loadFromFixedTable(IRB, kTablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
    IsLowFat = IRB.CreateICmpNE(AllocSize64, ConstantInt::get(I64Ty, 0));
  }

  Instruction *ThenTerm = SplitBlockAndInsertIfThen(IsLowFat, InsertPt, false);
  IRBuilder<> ThenIRB(ThenTerm);

  if (!AllocSize64)
    AllocSize64 = loadFromFixedTable(ThenIRB, kTablesBase + 0 * kTablesOffset,
                                     I64Ty, RegionIndex);
  Value *AllocSize = ThenIRB.CreateZExtOrTrunc(AllocSize64, IntptrTy);

  Value *Mask64 = loadFromFixedTable(ThenIRB, kTablesBase + 3 * kTablesOffset,
                                     I64Ty, RegionIndex);
  Value *Mask = ThenIRB.CreateZExtOrTrunc(Mask64, IntptrTy);
  Value *Base = ThenIRB.CreateAnd(SrcInt, Mask);

  // Check: result pointer must be within [Base, Base+AllocSize).
  emitOobCheck(ThenIRB, ResInt, Base, AllocSize, 0, nullptr, ThenTerm, false);

  NumInstrumentedGEPs++;
  return true;
}

//===----------------------------------------------------------------------===//
// Per-function instrumentation
//===----------------------------------------------------------------------===//

bool LowFatSanitizer::instrumentFunction(Function &F) {
  bool Modified = false;
  SmallVector<Instruction *, 16> ToInstrument;

  for (auto &BB : F) {
    for (auto &I : BB) {
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
    if (auto *LI = dyn_cast<LoadInst>(I))
      Modified |=
          instrumentMemoryAccess(I, LI->getPointerOperand(), LI->getType());
    else if (auto *SI = dyn_cast<StoreInst>(I))
      Modified |= instrumentMemoryAccess(I, SI->getPointerOperand(),
                                          SI->getValueOperand()->getType());
    else if (auto *RMW = dyn_cast<AtomicRMWInst>(I))
      Modified |= instrumentMemoryAccess(I, RMW->getPointerOperand(),
                                          RMW->getValOperand()->getType());
    else if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(I))
      Modified |= instrumentMemoryAccess(
          I, CmpXchg->getPointerOperand(),
          CmpXchg->getNewValOperand()->getType());
    else if (auto *MS = dyn_cast<MemSetInst>(I))
      Modified |=
          instrumentMemoryRange(I, MS->getDest(), MS->getLength(), true);
    else if (auto *MT = dyn_cast<MemTransferInst>(I)) {
      Modified |=
          instrumentMemoryRange(I, MT->getDest(), MT->getLength(), true);
      Modified |=
          instrumentMemoryRange(I, MT->getSource(), MT->getLength(), false);
    } else if (auto *GEP = dyn_cast<GetElementPtrInst>(I))
      Modified |= instrumentGEP(GEP);
  }
  return Modified;
}

//===----------------------------------------------------------------------===//
// Module-level pass entry point
//===----------------------------------------------------------------------===//

bool LowFatSanitizer::run() {
  LLVM_DEBUG(dbgs() << "[LowFat] run() Mode=" << (int)Options.Mode
                    << " BarrierOnly=" << Options.InternalBarrierOnly_ << "\n");

  // Safe mode first pass: insert barriers to prevent DCE/DAE.
  if (Options.InternalBarrierOnly_) {
    LLVM_DEBUG(dbgs() << "[LowFat] Inserting barriers (Safe mode)\n");
    bool Modified = false;
    Function *SideEffectFn =
        Intrinsic::getOrInsertDeclaration(&M, Intrinsic::sideeffect);
    Function *FakeUseFn =
        Intrinsic::getOrInsertDeclaration(&M, Intrinsic::fake_use);

    for (Function &F : M) {
      if (F.isDeclaration() || F.empty())
        continue;

      // Prevent FunctionAttrs from inferring memory(none).
      IRBuilder<> IRB(&*F.getEntryBlock().getFirstInsertionPt());
      IRB.CreateCall(SideEffectFn, {});

      // Prevent DAE from proving loads are dead.
      SmallVector<LoadInst *, 8> Loads;
      for (BasicBlock &BB : F)
        for (Instruction &I : BB)
          if (auto *LI = dyn_cast<LoadInst>(&I))
            Loads.push_back(LI);
      for (LoadInst *LI : Loads) {
        IRBuilder<> LIRB(LI->getNextNode());
        LIRB.CreateCall(FakeUseFn, {LI});
      }

      Modified = true;
    }
    return Modified;
  }

  // Main instrumentation pass.
  bool Modified = false;
  for (Function &F : M) {
    if (F.isDeclaration() || F.empty())
      continue;
    Modified |= instrumentFunction(F);
  }

  // Emit module constructor to set recover mode in the runtime.
  if (Options.Recover) {
    LLVMContext &Ctx = M.getContext();
    FunctionType *SetRecoverTy =
        FunctionType::get(Type::getVoidTy(Ctx), {Type::getInt32Ty(Ctx)}, false);
    FunctionCallee SetRecoverFn =
        M.getOrInsertFunction("__lf_set_recover", SetRecoverTy);
    Function *Ctor = Function::Create(
        FunctionType::get(Type::getVoidTy(Ctx), false),
        GlobalValue::InternalLinkage, "__lowfat_set_recover_ctor", &M);
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Ctor);
    IRBuilder<> CtorBuilder(BB);
    CtorBuilder.CreateCall(SetRecoverFn,
                           {ConstantInt::get(Type::getInt32Ty(Ctx), 1)});
    CtorBuilder.CreateRetVoid();
    appendToGlobalCtors(M, Ctor, /*Priority=*/0);
    Modified = true;
  }

  // Emit module constructor to enable right-align mode.
  if (Options.Mode == LowFatSanitizerOptions::LowFatMode::RightAlign) {
    LLVMContext &Ctx = M.getContext();
    FunctionType *SetRATy =
        FunctionType::get(Type::getVoidTy(Ctx), {Type::getInt32Ty(Ctx)}, false);
    FunctionCallee SetRAFn =
        M.getOrInsertFunction("__lf_set_right_align", SetRATy);
    Function *Ctor = Function::Create(
        FunctionType::get(Type::getVoidTy(Ctx), false),
        GlobalValue::InternalLinkage, "__lowfat_set_right_align_ctor", &M);
    BasicBlock *BB = BasicBlock::Create(Ctx, "entry", Ctor);
    IRBuilder<> CtorBuilder(BB);
    CtorBuilder.CreateCall(SetRAFn,
                           {ConstantInt::get(Type::getInt32Ty(Ctx), 1)});
    CtorBuilder.CreateRetVoid();
    appendToGlobalCtors(M, Ctor, /*Priority=*/0);
    Modified = true;
  }

  return Modified;
}

}  // anonymous namespace

//===----------------------------------------------------------------------===//
// Pass interface
//===----------------------------------------------------------------------===//

LowFatSanitizerPass::LowFatSanitizerPass(
    const LowFatSanitizerOptions &Options)
    : Options(Options) {}

PreservedAnalyses LowFatSanitizerPass::run(Module &M,
                                           ModuleAnalysisManager &AM) {
  LowFatSanitizer Sanitizer(M, Options);
  if (!Sanitizer.run())
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}
