set(FLEXFAT_LIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(FLEXFAT_TESTSUITES)

set(FLEXFAT_TEST_DEPS ${SANITIZER_COMMON_LIT_TEST_DEPS})
list(APPEND FLEXFAT_TEST_DEPS flexfat)

# check-flexfat is a single aggregate covering all four FlexFat test surfaces,
# so it also needs the LLVM IR pass-test tools (opt/FileCheck) and the in-tree
# clang for the end-to-end TestCases.
if(NOT COMPILER_RT_STANDALONE_BUILD)
  list(APPEND FLEXFAT_TEST_DEPS clang opt FileCheck count not)
endif()

set(FLEXFAT_TEST_ARCH ${FLEXFAT_SUPPORTED_ARCH})

# Surface 3: end-to-end TestCases (one per supported arch config).
foreach(arch ${FLEXFAT_TEST_ARCH})
  set(FLEXFAT_TEST_TARGET_ARCH ${arch})
  string(TOLOWER "-${arch}-${OS_NAME}" FLEXFAT_TEST_CONFIG_SUFFIX)
  get_test_cc_for_arch(${arch} FLEXFAT_TEST_TARGET_CC FLEXFAT_TEST_TARGET_CFLAGS)
  string(TOUPPER ${arch} ARCH_UPPER_CASE)
  set(CONFIG_NAME ${ARCH_UPPER_CASE}${OS_NAME}Config)
  configure_lit_site_cfg(
    ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
    ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/lit.site.cfg.py)
  list(APPEND FLEXFAT_TESTSUITES ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME})
endforeach()

# Surface 2: runtime unit tests (gtest).
configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/Unit/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/Unit/lit.site.cfg.py)
list(APPEND FLEXFAT_TEST_DEPS FlexFatUnitTests)
list(APPEND FLEXFAT_TESTSUITES ${CMAKE_CURRENT_BINARY_DIR}/Unit)

# Surfaces 1 and 4 live in other trees; fold them into the single check-flexfat.
#   surface 1: LLVM IR pass tests -> llvm/test/Instrumentation/FlexFat
#   surface 4: config golden-diff -> flexfat/config/test (self-contained lit cfg)
set(FLEXFAT_EXTRA_TESTSUITES)
if(LLVM_MAIN_SRC_DIR AND EXISTS ${LLVM_MAIN_SRC_DIR}/test/Instrumentation/FlexFat)
  list(APPEND FLEXFAT_EXTRA_TESTSUITES
    ${LLVM_MAIN_SRC_DIR}/test/Instrumentation/FlexFat)
endif()
get_filename_component(FLEXFAT_TOP_DIR "${COMPILER_RT_SOURCE_DIR}/.." ABSOLUTE)
if(EXISTS ${FLEXFAT_TOP_DIR}/flexfat/config/test)
  list(APPEND FLEXFAT_EXTRA_TESTSUITES ${FLEXFAT_TOP_DIR}/flexfat/config/test)
endif()

add_lit_testsuite(check-flexfat "Running the FlexFat tests"
  ${FLEXFAT_TESTSUITES}
  ${FLEXFAT_EXTRA_TESTSUITES}
  DEPENDS ${FLEXFAT_TEST_DEPS})
set_target_properties(check-flexfat PROPERTIES FOLDER "Compiler-RT Misc")
