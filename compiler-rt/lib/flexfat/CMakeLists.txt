# Build for the FlexFat runtime support library (LowFat reimplementation).
#
# Unit 1: a no-op stub static runtime (clang_rt.flexfat). Modeled structurally
# on compiler-rt/lib/asan/CMakeLists.txt, kept minimal like lib/safestack.

add_compiler_rt_component(flexfat)

set(FLEXFAT_SOURCES
  lowfat.c
  )

set(FLEXFAT_HEADERS
  lowfat.h
  )

include_directories(..)

# FlexFat/LowFat encodes object base+size into the pointer and relocates its
# globals and metadata tables into reserved high regions, using BMI/BMI2/LZCNT.
# Build the runtime with the large code model and those ISA extensions, matching
# the reference build (build.sh: -mcmodel=large -mbmi -mbmi2 -mlzcnt).
set(FLEXFAT_CFLAGS
  -mcmodel=large
  -mbmi
  -mbmi2
  -mlzcnt)

foreach(arch ${FLEXFAT_SUPPORTED_ARCH})
  add_compiler_rt_runtime(clang_rt.flexfat
    STATIC
    ARCHS ${arch}
    SOURCES ${FLEXFAT_SOURCES}
    ADDITIONAL_HEADERS ${FLEXFAT_HEADERS}
    CFLAGS ${FLEXFAT_CFLAGS}
    PARENT_TARGET flexfat)
endforeach()

if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()
