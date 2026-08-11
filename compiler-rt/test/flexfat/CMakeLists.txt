set(FLEXFAT_LIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(FLEXFAT_TESTSUITES)
set(FLEXFAT_TEST_DEPS ${SANITIZER_COMMON_LIT_TEST_DEPS})
list(APPEND FLEXFAT_TEST_DEPS flexfat)

set(FLEXFAT_TEST_ARCH ${FLEXFAT_SUPPORTED_ARCH})
if(APPLE)
  darwin_filter_host_archs(FLEXFAT_SUPPORTED_ARCH FLEXFAT_TEST_ARCH)
endif()

foreach(arch ${FLEXFAT_TEST_ARCH})
  set(FLEXFAT_TEST_TARGET_ARCH ${arch})
  # Use COMPILER_RT_TEST_COMPILER directly — get_test_cc_for_arch only populates
  # the CC output when cross-compiling; on Darwin native builds it returns empty.
  set(FLEXFAT_TEST_TARGET_CC ${COMPILER_RT_TEST_COMPILER})
  get_test_cc_for_arch(${arch} _unused FLEXFAT_TEST_TARGET_CFLAGS)
  string(TOLOWER "-${arch}-${OS_NAME}" FLEXFAT_TEST_CONFIG_SUFFIX)
  string(TOUPPER ${arch} ARCH_UPPER_CASE)
  set(CONFIG_NAME ${ARCH_UPPER_CASE}${OS_NAME}Config)

  # Propagate custom-config flag to lit as a Python boolean literal.
  if(FLEXFAT_SIZES_CFG)
    set(FLEXFAT_CUSTOM_CONFIG_ENABLED "True")
  else()
    set(FLEXFAT_CUSTOM_CONFIG_ENABLED "False")
  endif()

  configure_lit_site_cfg(
    ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
    ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME}/lit.site.cfg.py
    MAIN_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py"
  )
  list(APPEND FLEXFAT_TESTSUITES ${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_NAME})
endforeach()

add_lit_testsuite(check-flexfat "Running FlexFat Sanitizer tests"
  ${FLEXFAT_TESTSUITES}
  DEPENDS ${FLEXFAT_TEST_DEPS}
)
set_target_properties(check-flexfat PROPERTIES FOLDER "Compiler-RT/Tests")
