# Build for the FlexFat runtime support library (LowFat reimplementation).
#
# Unit 3: the pointer-encoding core + init. Built as an object library so the
# unit tests can link the runtime objects. Modeled on lib/asan and lib/rtsan.

add_compiler_rt_component(flexfat)

set(FLEXFAT_SOURCES
  lowfat.c
  )

set(FLEXFAT_HEADERS
  lowfat.h
  lowfat_config.h
  )

include_directories(..)

# FlexFat/LowFat encodes base+size into the pointer and relocates its globals and
# metadata tables into reserved high regions, using BMI/BMI2/LZCNT. Build with
# the large code model and those ISA extensions, matching the reference build
# (build.sh: -mcmodel=large -mbmi -mbmi2 -mlzcnt). -I for <lowfat_config.h>.
# The generated lowfat_config.c carries stack/heap-select tables not consumed
# until later units; silence their unused diagnostics.
set(FLEXFAT_CFLAGS
  -mcmodel=large
  -mbmi
  -mbmi2
  -mlzcnt
  -I${CMAKE_CURRENT_SOURCE_DIR}
  -Wno-unused-function
  -Wno-unused-const-variable)

add_compiler_rt_object_libraries(RTFlexfat
  ARCHS ${FLEXFAT_SUPPORTED_ARCH}
  SOURCES ${FLEXFAT_SOURCES}
  ADDITIONAL_HEADERS ${FLEXFAT_HEADERS}
  CFLAGS ${FLEXFAT_CFLAGS})

# A test-only variant WITHOUT the libc malloc/free interposition aliases, so the
# unit-test process keeps using libc and calls lowfat_malloc/lowfat_free directly.
add_compiler_rt_object_libraries(RTFlexfat_noreplace
  ARCHS ${FLEXFAT_SUPPORTED_ARCH}
  SOURCES ${FLEXFAT_SOURCES}
  ADDITIONAL_HEADERS ${FLEXFAT_HEADERS}
  CFLAGS ${FLEXFAT_CFLAGS}
        -DLOWFAT_NO_REPLACE_STD_MALLOC
        -DLOWFAT_NO_REPLACE_STD_FREE
        # Unit 14a: keep gtest's own pthread_create on real glibc so its
        # workers run on stock kernel stacks (gtest death-test infra and
        # the test harness assume that). The Unit-4 per-region malloc
        # mutexes are still exercised concurrently via lowfat_malloc /
        # lowfat_free called from those stock-stack worker threads — that
        # is the test we actually need.
        -DLOWFAT_NO_REPLACE_PTHREAD_CREATE)

foreach(arch ${FLEXFAT_SUPPORTED_ARCH})
  add_compiler_rt_runtime(clang_rt.flexfat
    STATIC
    ARCHS ${arch}
    OBJECT_LIBS RTFlexfat
    CFLAGS ${FLEXFAT_CFLAGS}
    PARENT_TARGET flexfat)
endforeach()

# Unit 13: ship the generated linker script (lowfat.ld, committed under
# flexfat/config/golden/nonpow2/) into the compiler-rt resource directory
# alongside libclang_rt.flexfat.a. The clang driver picks it up from
# TC.getCompilerRTPath() and passes it as `-T <resource_dir>/lowfat.ld`
# on every -fsanitize=flexfat link, pinning each lowfat_section_<size>
# to its region's [16 GiB, 24 GiB) global sub-range.
set(FLEXFAT_GOLDEN_LD
  ${CMAKE_SOURCE_DIR}/../flexfat/config/golden/nonpow2/lowfat.ld)
# Match the per-target-runtime-dir layout that add_compiler_rt_runtime uses
# for libclang_rt.flexfat.a — see compiler-rt's get_compiler_rt_output_dir
# (COMPILER_RT_OUTPUT_LIBRARY_DIR/${triple}). The driver derives lowfat.ld's
# path from the runtime archive's location (clang/Driver/CommonArgs.cpp), so
# they must live in the same directory.
set(FLEXFAT_LD_DST
  ${COMPILER_RT_OUTPUT_LIBRARY_DIR}/${COMPILER_RT_DEFAULT_TARGET_TRIPLE}/lowfat.ld)
add_custom_command(
  OUTPUT ${FLEXFAT_LD_DST}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${FLEXFAT_GOLDEN_LD} ${FLEXFAT_LD_DST}
  DEPENDS ${FLEXFAT_GOLDEN_LD}
  COMMENT "Installing FlexFat lowfat.ld to ${FLEXFAT_LD_DST}")
add_custom_target(flexfat_lowfat_ld ALL DEPENDS ${FLEXFAT_LD_DST})
add_dependencies(flexfat flexfat_lowfat_ld)
install(FILES ${FLEXFAT_GOLDEN_LD}
        DESTINATION
          ${COMPILER_RT_INSTALL_LIBRARY_DIR}/${COMPILER_RT_DEFAULT_TARGET_TRIPLE}
        COMPONENT flexfat)

# Unit 14a: build-time TID/JOINID offset gate.
#
# glibc declares `struct pthread` PRIVATE in descr.h and changes the layout
# across versions (we've already observed JOINID at both 0x620 and 0x628).
# Dead-thread reclamation reads those offsets directly. If they're wrong,
# the runtime silently corrupts stack-slot ownership; this gate makes that
# a loud build failure instead.
#
# Build the validator with the host compiler and `lowfat_config.c` on the
# include path so it sees the configured LOWFAT_{TID,JOINID}_OFFSET. Run it
# at build time; success writes a stamp file that libclang_rt.flexfat
# depends on. Failure prints expected-vs-found offsets and stops the build.
set(FLEXFAT_CHECK_CONFIG_SRC
  ${CMAKE_SOURCE_DIR}/../flexfat/config/lowfat-check-config.c)
set(FLEXFAT_CHECK_CONFIG_INC
  ${CMAKE_SOURCE_DIR}/../flexfat/config/golden/nonpow2/lowfat_config.c)
set(FLEXFAT_CHECK_CONFIG_BIN
  ${CMAKE_CURRENT_BINARY_DIR}/lowfat-check-config-validator)
set(FLEXFAT_CHECK_CONFIG_STAMP
  ${CMAKE_CURRENT_BINARY_DIR}/lowfat-check-config.stamp)
add_custom_command(
  OUTPUT ${FLEXFAT_CHECK_CONFIG_BIN}
  COMMAND ${CMAKE_C_COMPILER} -O2 -Wall
          -I${CMAKE_SOURCE_DIR}/../flexfat/config/golden/nonpow2
          -Wno-unused-function -Wno-unused-variable
          -o ${FLEXFAT_CHECK_CONFIG_BIN}
          ${FLEXFAT_CHECK_CONFIG_SRC}
          -lpthread
  # DEPENDS on both the validator and the lowfat_config.c whose offsets it
  # consumes (#included from the validator source) — without the latter, a
  # config edit doesn't trigger a re-build of the validator and the gate
  # silently keeps the prior offsets baked in.
  DEPENDS ${FLEXFAT_CHECK_CONFIG_SRC} ${FLEXFAT_CHECK_CONFIG_INC}
  COMMENT "Building FlexFat TID/JOINID offset validator")
add_custom_command(
  OUTPUT ${FLEXFAT_CHECK_CONFIG_STAMP}
  COMMAND ${FLEXFAT_CHECK_CONFIG_BIN}
  COMMAND ${CMAKE_COMMAND} -E touch ${FLEXFAT_CHECK_CONFIG_STAMP}
  DEPENDS ${FLEXFAT_CHECK_CONFIG_BIN}
  COMMENT "Validating FlexFat TID/JOINID offsets against host glibc")
add_custom_target(flexfat_check_config ALL
  DEPENDS ${FLEXFAT_CHECK_CONFIG_STAMP})
add_dependencies(flexfat flexfat_check_config)

if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()
