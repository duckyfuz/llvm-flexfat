# LowFatPassConfig.cmake - Custom size-class configuration for LowFat pass
#
# Set -DLOWFAT_SIZES_CFG=/path/to/sizes.cfg at cmake time to enable non-POW2
# size classes. When not set, the pass uses POW2-only mode (default).
#
# This file is intentionally minimal for the initial POW2-only implementation.
# Non-POW2 support will add a host tool (lf_config_gen) that generates
# lf_config_generated.h with custom size/magic/mask tables.

set(LOWFAT_SIZES_CFG "" CACHE FILEPATH
  "Path to sizes.cfg for non-POW2 LowFat size class generation. Empty = POW2-only mode.")

if(LOWFAT_SIZES_CFG)
  message(STATUS "LowFat: custom config requested but not yet implemented in this build")
else()
  message(STATUS "LowFat: using default POW2-only mode")
endif()
