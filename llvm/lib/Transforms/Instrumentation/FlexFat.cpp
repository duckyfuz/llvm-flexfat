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
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "flexfat"

namespace {

// ABI constants -- must stay byte-identical to the runtime (lowfat_config.h /
// lowfat.h). _LOWFAT_REGION_SIZE = 2^35, so the region index is ptr >> 35.
constexpr uint64_t kRegionSizeShift = 35;
constexpr uint64_t kSizesAddr = 0x200000;  // _LOWFAT_SIZES  (size_t[])
constexpr uint64_t kMagicsAddr = 0x300000; // _LOWFAT_MAGICS (uint64_t[])

// OOB info codes (lowfat.h: LOWFAT_OOB_ERROR_{READ,WRITE}).
constexpr unsigned kInfoRead = 0;
constexpr unsigned kInfoWrite = 1;

// Fast-path branch weights (2000000000:1 in favour of the fast path), so the
// cold error block is placed out of line. NOTE: the reference weights the OOB
// (error) edge 2000000000 and relies on LLVM-4.0's noreturn-cold heuristic to
// override that for block placement. LLVM 23's MachineBlockPlacement honours the
// explicit weight over that heuristic, so weighting the error edge hot would put
// it on the fall-through (a fast-path regression). We weight the error edge cold
// instead -- same intent (2e9:1), same fast-path asm as the reference.
constexpr uint32_t kErrorWeight = 1;
constexpr uint32_t kFastWeight = 2000000000;

class FlexFat {
public:
  FlexFat(Function &F, const TargetLibraryInfo &TLI)
      : F(F), M(*F.getParent()), Ctx(F.getContext()),
        I64Ty(Type::getInt64Ty(Ctx)), I128Ty(Type::getInt128Ty(Ctx)),
        PtrTy(PointerType::getUnqual(Ctx)), TLI(TLI) {}

  bool run();

private:
  Value *calcBasePtr(Value *Ptr);
  Value *emitInlineBase(Value *Ptr);
  void insertBoundsCheck(Instruction *I, Value *Ptr, unsigned Info,
                         Value *Base);
  std::pair<BasicBlock *, BasicBlock::iterator> nextInsertPoint(Value *Ptr);

  // Inline a GEP into a fixed runtime table (_LOWFAT_SIZES / _LOWFAT_MAGICS).
  Value *tableSlot(IRBuilder<> &B, uint64_t TableAddr, Value *Idx) {
    Value *Table = B.CreateIntToPtr(B.getInt64(TableAddr), PtrTy);
    return B.CreateGEP(I64Ty, Table, Idx);
  }

  Function &F;
  Module &M;
  LLVMContext &Ctx;
  IntegerType *I64Ty;
  IntegerType *I128Ty;
  PointerType *PtrTy;
  const TargetLibraryInfo &TLI;
  DenseMap<Value *, Value *> baseInfo;
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
                                Value *Base) {
  IRBuilder<> B(I);
  Value *IBase = B.CreatePtrToInt(Base, I64Ty);
  Value *Idx = B.CreateLShr(IBase, B.getInt64(kRegionSizeShift));
  Value *Size = B.CreateAlignedLoad(I64Ty, tableSlot(B, kSizesAddr, Idx),
                                    Align(sizeof(uint64_t)));
  Value *IPtr = B.CreatePtrToInt(Ptr, I64Ty);
  Value *Diff = B.CreateSub(IPtr, IBase);
  // The check is `diff >=u size - access_size`. access_size defaults to 0 (check
  // the byte at ptr); -lowfat-check-whole-access (a later option) would subtract
  // sizeof(access)-1 here. Nothing to subtract for 0.
  Value *Cmp = B.CreateICmpUGE(Diff, Size);

  MDNode *Weights =
      MDBuilder(Ctx).createBranchWeights(kErrorWeight, kFastWeight);
  Instruction *ErrTerm =
      SplitBlockAndInsertIfThen(Cmp, I, /*Unreachable=*/true, Weights);

  IRBuilder<> EB(ErrTerm);
  FunctionCallee OobError = M.getOrInsertFunction(
      "lowfat_oob_error",
      FunctionType::get(EB.getVoidTy(),
                        {EB.getInt32Ty(), PtrTy, PtrTy}, false));
  if (auto *Fn = dyn_cast<Function>(OobError.getCallee()))
    Fn->setDoesNotReturn();
  CallInst *Call = EB.CreateCall(OobError, {EB.getInt32(Info), Ptr, Base});
  Call->setDoesNotReturn();
}

bool FlexFat::run() {
  // Plan a check per interesting memory op first (getInterestingInsts,
  // LowFat.cpp:892-1021, LOAD/STORE subset). Skip nosanitize-tagged accesses.
  SmallVector<std::tuple<Instruction *, Value *, unsigned>, 16> Plan;
  for (Instruction &I : instructions(F)) {
    if (I.getMetadata(LLVMContext::MD_nosanitize))
      continue;
    if (auto *LD = dyn_cast<LoadInst>(&I))
      Plan.emplace_back(&I, LD->getPointerOperand(), kInfoRead);
    else if (auto *ST = dyn_cast<StoreInst>(&I))
      Plan.emplace_back(&I, ST->getPointerOperand(), kInfoWrite);
  }

  bool Changed = false;
  for (auto &[I, Ptr, Info] : Plan) {
    Value *Base = calcBasePtr(Ptr);
    if (!Base || isa<ConstantPointerNull>(Base))
      continue; // non-fat pointer: no check
    insertBoundsCheck(I, Ptr, Info, Base);
    Changed = true;
  }
  return Changed;
}

PreservedAnalyses FlexFatPass::run(Function &F, FunctionAnalysisManager &AM) {
  if (F.isDeclaration())
    return PreservedAnalyses::all();
  const TargetLibraryInfo &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  bool Changed = FlexFat(F, TLI).run();
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
