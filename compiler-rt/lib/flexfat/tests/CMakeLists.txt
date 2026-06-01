# Unit tests for the FlexFat runtime (gtest). Modeled on lib/rtsan/tests.
#
# Unit 3 links the runtime objects (RTFlexfat) so the constructor initialises the
# SIZES/MAGICS tables before the encoding cases run.

include(CompilerRTCompile)

include_directories(..)

set(FLEXFAT_UNITTEST_CFLAGS
  ${COMPILER_RT_UNITTEST_CFLAGS}
  ${COMPILER_RT_GTEST_CFLAGS}
  ${SANITIZER_TEST_CXX_CFLAGS}
  -I${COMPILER_RT_SOURCE_DIR}/lib
  -I${COMPILER_RT_SOURCE_DIR}/lib/flexfat
  -O2)

set(FLEXFAT_UNITTEST_SOURCES
  flexfat_test.cpp
  flexfat_encoding_test.cpp
  flexfat_test_main.cpp)

add_custom_target(FlexFatUnitTests)
set_target_properties(FlexFatUnitTests PROPERTIES FOLDER "Compiler-RT Tests")

set(FLEXFAT_UNITTEST_LINK_FLAGS
  ${COMPILER_RT_UNITTEST_LINK_FLAGS}
  ${COMPILER_RT_UNWINDER_LINK_LIBS}
  ${SANITIZER_TEST_CXX_LIBRARIES})

append_list_if(COMPILER_RT_HAS_LIBM -lm FLEXFAT_UNITTEST_LINK_FLAGS)
append_list_if(COMPILER_RT_HAS_LIBDL -ldl FLEXFAT_UNITTEST_LINK_FLAGS)
append_list_if(COMPILER_RT_HAS_LIBPTHREAD -pthread FLEXFAT_UNITTEST_LINK_FLAGS)

foreach(arch ${FLEXFAT_SUPPORTED_ARCH})
  set(FlexFatTestObjects)
  # Static archive of the runtime objects for the test to link against.
  set(FLEXFAT_TEST_RUNTIME RTFlexFatTest.${arch})
  add_library(${FLEXFAT_TEST_RUNTIME} STATIC $<TARGET_OBJECTS:RTFlexfat.${arch}>)
  set_target_properties(${FLEXFAT_TEST_RUNTIME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    FOLDER "Compiler-RT Runtime tests")

  generate_compiler_rt_tests(FlexFatTestObjects
    FlexFatUnitTests "Flexfat-${arch}-Test" ${arch}
    SOURCES ${FLEXFAT_UNITTEST_SOURCES} ${COMPILER_RT_GTEST_SOURCE}
    RUNTIME ${FLEXFAT_TEST_RUNTIME}
    CFLAGS ${FLEXFAT_UNITTEST_CFLAGS}
    LINK_FLAGS ${FLEXFAT_UNITTEST_LINK_FLAGS})
  set_target_properties(FlexFatUnitTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()
