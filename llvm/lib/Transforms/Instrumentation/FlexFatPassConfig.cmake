if(FLEXFAT_SIZES_CFG)
  # LLVM pass needs the generated header too. Since runtimes build AFTER LLVM,
  # we must generate a local copy of the header for the pass to use.
  #
  # We use CMAKE_C_COMPILER (not add_executable) so that the generator is
  # always built for the host machine. add_executable would produce a
  # target-architecture binary on cross-compile setups (e.g. building for
  # AArch64 on an x86 host) which cannot be executed during the build.
  set(FLEXFAT_CONFIG_GEN_SRC
      ${LLVM_MAIN_SRC_DIR}/../compiler-rt/lib/flexfat/tools/flexfat_config_gen.c)
  set(FLEXFAT_CONFIG_GEN_BIN
      ${CMAKE_CURRENT_BINARY_DIR}/flexfat_config_gen_llvm${CMAKE_EXECUTABLE_SUFFIX})
  set(FLEXFAT_GENERATED_HEADER ${CMAKE_CURRENT_BINARY_DIR}/flexfat_config_generated.h)

  add_custom_command(
    OUTPUT  ${FLEXFAT_CONFIG_GEN_BIN}
    COMMAND ${CMAKE_C_COMPILER} -std=c11 -O2
            ${FLEXFAT_CONFIG_GEN_SRC}
            -o ${FLEXFAT_CONFIG_GEN_BIN}
    DEPENDS ${FLEXFAT_CONFIG_GEN_SRC}
    COMMENT "Compiling flexfat_config_gen host tool (for LLVM pass)"
    VERBATIM
  )

  add_custom_command(
    OUTPUT  ${FLEXFAT_GENERATED_HEADER}
    COMMAND ${FLEXFAT_CONFIG_GEN_BIN} ${FLEXFAT_SIZES_CFG} ${FLEXFAT_GENERATED_HEADER}
    DEPENDS ${FLEXFAT_CONFIG_GEN_BIN} ${FLEXFAT_SIZES_CFG}
    COMMENT "Generating FlexFat size config for LLVM pass"
    VERBATIM
  )

  add_custom_target(flexfat_pass_config DEPENDS ${FLEXFAT_GENERATED_HEADER})
  add_dependencies(LLVMInstrumentation flexfat_pass_config)

  set_property(SOURCE FlexFatSanitizer.cpp APPEND PROPERTY COMPILE_DEFINITIONS "FLEXFAT_CUSTOM_CONFIG=1")
  set_property(SOURCE FlexFatSanitizer.cpp APPEND PROPERTY OBJECT_DEPENDS ${FLEXFAT_GENERATED_HEADER})

  # Tell the compiler where to find the generated header.
  target_include_directories(LLVMInstrumentation PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif()
