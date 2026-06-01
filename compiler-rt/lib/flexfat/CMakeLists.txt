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
        -DLOWFAT_NO_REPLACE_STD_FREE)

foreach(arch ${FLEXFAT_SUPPORTED_ARCH})
  add_compiler_rt_runtime(clang_rt.flexfat
    STATIC
    ARCHS ${arch}
    OBJECT_LIBS RTFlexfat
    CFLAGS ${FLEXFAT_CFLAGS}
    PARENT_TARGET flexfat)
endforeach()

if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()
