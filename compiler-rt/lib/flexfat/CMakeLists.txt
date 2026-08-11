include_directories(..)

set(FLEXFAT_COMMON_DEFINITIONS)

#===------------------------------------------------------------------------===
# Custom size-class configuration (non-POW2 support)
#
# Set -DFLEXFAT_SIZES_CFG=/path/to/sizes.cfg at cmake time to enable.
# This builds the flexfat_config_gen host tool, runs it to produce
# flexfat_config_generated.h, and compiles the runtime with FLEXFAT_CUSTOM_CONFIG.
#===------------------------------------------------------------------------===
set(FLEXFAT_SIZES_CFG "" CACHE FILEPATH
  "Path to sizes.cfg for non-POW2 FlexFat size class generation. Empty = POW2-only mode.")

if(FLEXFAT_SIZES_CFG)
  # ---- Build the generator tool (host compiler, no LLVM deps) ----
  # Use add_custom_target / add_custom_command rather than add_executable so
  # this host tool is never cross-compiled.
  set(FLEXFAT_CONFIG_GEN_SRC
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/flexfat_config_gen.c)
  set(FLEXFAT_CONFIG_GEN_BIN
      ${CMAKE_CURRENT_BINARY_DIR}/flexfat_config_gen${CMAKE_EXECUTABLE_SUFFIX})
  set(FLEXFAT_GENERATED_HEADER
      ${CMAKE_CURRENT_BINARY_DIR}/flexfat_config_generated.h)

  add_custom_command(
    OUTPUT  ${FLEXFAT_CONFIG_GEN_BIN}
    COMMAND ${CMAKE_C_COMPILER} -std=c11 -O2
            ${FLEXFAT_CONFIG_GEN_SRC}
            -o ${FLEXFAT_CONFIG_GEN_BIN}
    DEPENDS ${FLEXFAT_CONFIG_GEN_SRC}
    COMMENT "Compiling flexfat_config_gen host tool"
    VERBATIM
  )

  add_custom_command(
    OUTPUT  ${FLEXFAT_GENERATED_HEADER}
    COMMAND ${FLEXFAT_CONFIG_GEN_BIN} ${FLEXFAT_SIZES_CFG} ${FLEXFAT_GENERATED_HEADER}
    DEPENDS ${FLEXFAT_CONFIG_GEN_BIN} ${FLEXFAT_SIZES_CFG}
    COMMENT "Generating FlexFat size tables from ${FLEXFAT_SIZES_CFG}"
    VERBATIM
  )

  add_custom_target(flexfat_config_generated DEPENDS ${FLEXFAT_GENERATED_HEADER})
  set(FLEXFAT_DEPS flexfat_config_generated)

  # Make the generated header visible to all sources in this directory.
  include_directories(${CMAKE_CURRENT_BINARY_DIR})

  # Activate the custom-config code paths in the runtime AND the LLVM pass.
  list(APPEND FLEXFAT_COMMON_DEFINITIONS FLEXFAT_CUSTOM_CONFIG=1)

  message(STATUS "FlexFat: custom config enabled: ${FLEXFAT_SIZES_CFG}")
  message(STATUS "FlexFat: generated header:      ${FLEXFAT_GENERATED_HEADER}")
else()
  message(STATUS "FlexFat: using default POW2-only mode (no FLEXFAT_SIZES_CFG set)")
  set(FLEXFAT_DEPS)
endif()
set(FLEXFAT_SOURCES
  flexfat_rtl.cpp
  flexfat_interceptors.cpp
  flexfat_stack.cpp
)

set(FLEXFAT_HEADERS
  flexfat_allocator.h
  flexfat_config.h
  flexfat_interface.h
  flexfat_stack.h
)

set(FLEXFAT_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF FLEXFAT_CFLAGS)

# Static runtime library.
add_compiler_rt_component(flexfat)

if(COMPILER_RT_HAS_FLEXFAT)
  if(APPLE)
    add_weak_symbols("sanitizer_common" WEAK_SYMBOL_LINK_FLAGS)

    add_compiler_rt_runtime(clang_rt.flexfat
      SHARED
      OS ${SANITIZER_COMMON_SUPPORTED_OS}
      ARCHS ${FLEXFAT_SUPPORTED_ARCH}
      SOURCES ${FLEXFAT_SOURCES}
      ADDITIONAL_HEADERS ${FLEXFAT_HEADERS}
      OBJECT_LIBS RTInterception
                  RTSanitizerCommon
                  RTSanitizerCommonLibc
                  RTSanitizerCommonSymbolizer
      CFLAGS ${FLEXFAT_CFLAGS}
      LINK_FLAGS ${WEAK_SYMBOL_LINK_FLAGS}
      DEFS ${FLEXFAT_COMMON_DEFINITIONS}
      DEPS ${FLEXFAT_DEPS}
      PARENT_TARGET flexfat)
  else()
    add_compiler_rt_runtime(clang_rt.flexfat
      STATIC
      ARCHS ${FLEXFAT_SUPPORTED_ARCH}
      SOURCES ${FLEXFAT_SOURCES}
      ADDITIONAL_HEADERS ${FLEXFAT_HEADERS}
      OBJECT_LIBS RTInterception
                  RTSanitizerCommon
                  RTSanitizerCommonLibc
                  RTSanitizerCommonSymbolizer
                  RTSanitizerCommonSymbolizerInternal
      CFLAGS ${FLEXFAT_CFLAGS}
      DEFS ${FLEXFAT_COMMON_DEFINITIONS}
      DEPS ${FLEXFAT_DEPS}
      PARENT_TARGET flexfat)
  endif()
endif()
