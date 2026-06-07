# FlexFat variant selector (Unit 17). LowFat has two encoding variants:
# non-POW2 (default, sizes.cfg, 61 regions, reciprocal-magic) and POW2
# (sizes2.cfg, 30 regions, bitmask-magic). The pass folds heap_select() at
# compile time from a per-variant size table and emits an inline lowfat_base
# whose codegen is variant-specific (POW2 = single `and`; non-POW2 = 128-bit
# mulq). The pass and the runtime MUST build for the same variant -- a
# mismatch sends constant mallocs to invalid regions (Unit 17 root-cause).
# Both variants' size tables are committed alongside FlexFat.cpp; the
# active one is picked by FLEXFAT_IS_POW2 at compile time.
set(LLVM_FLEXFAT_POW2 OFF CACHE BOOL
    "Build FlexFat for the POW2 (sizes2.cfg) encoding variant.")
if(LLVM_FLEXFAT_POW2)
  set_source_files_properties(FlexFat.cpp
      PROPERTIES COMPILE_DEFINITIONS "FLEXFAT_IS_POW2=1")
  message(STATUS "FlexFat: variant = POW2")
else()
  message(STATUS "FlexFat: variant = non-POW2 (default)")
endif()

add_llvm_component_library(LLVMInstrumentation
  AddressSanitizer.cpp
  AllocToken.cpp
  BoundsChecking.cpp
  CGProfile.cpp
  ControlHeightReduction.cpp
  DataFlowSanitizer.cpp
  FlexFat.cpp
  GCOVProfiling.cpp
  BlockCoverageInference.cpp
  MemProfInstrumentation.cpp
  MemProfUse.cpp
  MemorySanitizer.cpp
  NumericalStabilitySanitizer.cpp
  IndirectCallPromotion.cpp
  InstrProfiling.cpp
  KCFI.cpp
  LowerAllowCheckPass.cpp
  PGOCtxProfFlattening.cpp
  PGOCtxProfLowering.cpp
  PGOForceFunctionAttrs.cpp
  PGOInstrumentation.cpp
  PGOMemOPSizeOpt.cpp
  SanitizerCoverage.cpp
  SanitizerBinaryMetadata.cpp
  ValueProfileCollector.cpp
  ThreadSanitizer.cpp
  TypeSanitizer.cpp
  HWAddressSanitizer.cpp
  RealtimeSanitizer.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis
  Core
  Demangle
  MC
  Support
  TargetParser
  TransformUtils
  ProfileData
  )
