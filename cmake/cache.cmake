# ============================================================================= XSigma Build Speed
# Optimization Configuration Module
# =============================================================================
# Enables configurable compiler caching (ccache, sccache, buildcache) and faster linkers for
# improved build performance. Supports GCC, Clang, and MSVC on Linux, macOS, and Windows.
#
# NOTE: This module applies faster linker configuration ONLY to the xsigmabuild interface target,
# ensuring that third-party dependencies are not affected by linker choices.
# =============================================================================

# Include guard to prevent multiple inclusions
include_guard(GLOBAL)

# Distributed compilation with Icecream
option(XSIGMA_ENABLE_ICECC "Use Icecream distributed compilation" OFF)
mark_as_advanced(XSIGMA_ENABLE_ICECC)

if(XSIGMA_ENABLE_ICECC)
  find_program(ICECC_EXECUTABLE icecc)
  if(ICECC_EXECUTABLE)
    set(CMAKE_C_COMPILER_LAUNCHER ${ICECC_EXECUTABLE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${ICECC_EXECUTABLE})
    message(STATUS "Using Icecream: ${ICECC_EXECUTABLE}")
  endif()
endif()

# Build Speed Optimization Flag Controls whether caching and faster linker optimizations are
# enabled. When enabled, uses the selected cache type and selects faster linkers when available.
option(XSIGMA_ENABLE_CACHE "Enable compiler caching and faster linker for faster builds" ON)
mark_as_advanced(XSIGMA_ENABLE_CACHE)

# Cache Type Configuration Selects which compiler cache to use: none, ccache, sccache, or buildcache
set(XSIGMA_CACHE_TYPE "none"
    CACHE STRING "Compiler cache type to use. Options: none, ccache, sccache, buildcache"
)
set_property(CACHE XSIGMA_CACHE_TYPE PROPERTY STRINGS none ccache sccache buildcache)
mark_as_advanced(XSIGMA_CACHE_TYPE)

# Validate cache type
if(NOT XSIGMA_CACHE_TYPE MATCHES "^(none|ccache|sccache|buildcache)$")
  message(
    FATAL_ERROR
      "Invalid XSIGMA_CACHE_TYPE: ${XSIGMA_CACHE_TYPE}. Valid options are: none, ccache, sccache, buildcache"
  )
endif()

if(NOT XSIGMA_ENABLE_CACHE)
  message(WARNING "Build speed cache optimization configuration complete")
  return()
endif()

message(STATUS "Configuring build speed optimizations with cache type: ${XSIGMA_CACHE_TYPE}")

# Note: When LTO is enabled, faster linkers (especially gold) may run out of memory during the
# linking phase. In such cases, it's better to use the default linker. This is a known limitation of
# LTO with certain linkers.

# ============================================================================ Compiler Cache
# Configuration
# ============================================================================
#
# NOTE: Compiler caches are configured globally as compiler launchers because they need to intercept
# all compilation commands, including those for third-party dependencies. This is safe because
# caches only cache compilation results and don't affect the actual compilation flags or behavior.
set(XSIGMA_CACHE_PROGRAM "none")
if(XSIGMA_CACHE_TYPE STREQUAL "ccache")
  find_program(CCACHE_PROGRAM ccache)
  set(XSIGMA_CACHE_PROGRAM ${CCACHE_PROGRAM})
  if(CCACHE_PROGRAM)
    message(STATUS "Found ccache: ${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C compiler launcher")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "CXX compiler launcher")
    if(XSIGMA_ENABLE_CUDA)
      set(CMAKE_CUDA_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "CUDA compiler launcher")
    endif()
    message(STATUS "ccache enabled for C/C++ compilation")
  else()
    message(WARNING "ccache not found - compiler caching disabled")
  endif()

elseif(XSIGMA_CACHE_TYPE STREQUAL "sccache")
  find_program(SCCACHE_PROGRAM sccache)
  set(XSIGMA_CACHE_PROGRAM ${SCCACHE_PROGRAM})
  if(SCCACHE_PROGRAM)
    message(STATUS "Found sccache: ${SCCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}" CACHE STRING "C compiler launcher")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}" CACHE STRING "CXX compiler launcher")
    if(XSIGMA_ENABLE_CUDA)
      set(CMAKE_CUDA_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}" CACHE STRING "CUDA compiler launcher")
    endif()
    message(STATUS "sccache enabled for C/C++ compilation")
  else()
    message(WARNING "sccache not found - compiler caching disabled")
  endif()

elseif(XSIGMA_CACHE_TYPE STREQUAL "buildcache")
  find_program(BUILDCACHE_PROGRAM buildcache)
  set(XSIGMA_CACHE_PROGRAM ${BUILDCACHE_PROGRAM})
  if(BUILDCACHE_PROGRAM)
    message(STATUS "Found buildcache: ${BUILDCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${BUILDCACHE_PROGRAM}" CACHE STRING "C compiler launcher")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${BUILDCACHE_PROGRAM}" CACHE STRING "CXX compiler launcher")
    if(XSIGMA_ENABLE_CUDA)
      set(CMAKE_CUDA_COMPILER_LAUNCHER "${BUILDCACHE_PROGRAM}" CACHE STRING
                                                                     "CUDA compiler launcher"
      )
    endif()
    message(STATUS "buildcache enabled for C/C++ compilation")
  else()
    message(WARNING "buildcache not found - compiler caching disabled")
  endif()

elseif(XSIGMA_CACHE_TYPE STREQUAL "none")
  message(STATUS "No compiler cache selected")
endif()

message("  -cache: ENABLED. program: ${XSIGMA_CACHE_PROGRAM}")
mark_as_advanced(XSIGMA_CACHE_PROGRAM)
