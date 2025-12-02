# ============================================================================= XSigma Clang-Tidy
# Static Analysis Configuration Module
# =============================================================================
# This module configures clang-tidy for static code analysis and automated fixes. It enables code
# quality checks and optional automatic error correction.
# =============================================================================

# Include guard to prevent multiple inclusions
include_guard(GLOBAL)

# Clang-Tidy Static Analysis Flag Controls whether clang-tidy static analysis is enabled during
# compilation. When enabled, performs comprehensive code quality checks on all targets.
option(XSIGMA_ENABLE_CLANGTIDY "enable clangtidy check" OFF)
mark_as_advanced(XSIGMA_ENABLE_CLANGTIDY)

# Clang-Tidy Auto-Fix Flag Controls whether clang-tidy automatically fixes detected errors. WARNING:
# This modifies source files. Use with caution in version control.
option(XSIGMA_ENABLE_FIX "Enable clang-tidy fix-errors and fix options" OFF)
mark_as_advanced(XSIGMA_ENABLE_FIX)

if(NOT XSIGMA_ENABLE_CLANGTIDY)
  return()
endif()

# XSigma ClangTidy Configuration
find_program(CLANG_TIDY_PATH NAMES clang-tidy DOC "Path to clang-tidy.")

if(NOT CLANG_TIDY_PATH)
  message(FATAL_ERROR "Could not find clang-tidy.")
endif()
set(CLANG_TIDY_FOUND ON CACHE BOOL "Found clang-tidy.")
mark_as_advanced(CLANG_TIDY_FOUND)

function(xsigma_target_clang_tidy target_name)
  if(XSIGMA_ENABLE_FIX)
    message(WARNING "Applying clang-tidy fix to target: ${target_name}")
    set_target_properties(
      ${target_name}
      PROPERTIES C_CLANG_TIDY "${CLANG_TIDY_PATH};-fix-errors;-fix;-warnings-as-errors=*"
                 CXX_CLANG_TIDY "${CLANG_TIDY_PATH};-fix-errors;-fix;-warnings-as-errors=*"
    )
  else()
    set_target_properties(
      ${target_name} PROPERTIES C_CLANG_TIDY "${CLANG_TIDY_PATH};-warnings-as-errors=*"
                                CXX_CLANG_TIDY "${CLANG_TIDY_PATH};-warnings-as-errors=*"
    )
  endif()
endfunction()

function(disable_clang_tidy_for_target target_name)
  set_target_properties(${target_name} PROPERTIES C_CLANG_TIDY "" CXX_CLANG_TIDY "")
endfunction()
