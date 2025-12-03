# ============================================================================= 
# XSigma Spell
# Checking Configuration Module
# =============================================================================
# This module configures codespell for automated spell checking and correction. WARNING: When
# enabled, automatically modifies source files to fix spelling errors.
# =============================================================================

# Include guard to prevent multiple inclusions
include_guard(GLOBAL)

# Spell Checking Flag Controls whether spell checking with automatic corrections is enabled. When
# enabled, runs codespell during build to fix spelling errors in source files. WARNING: This
# modifies source files directly. Ensure changes are committed first.
option(XSIGMA_ENABLE_SPELL
       "Enable spell checking with automatic corrections (WARNING: modifies source files)" OFF
)
mark_as_advanced(XSIGMA_ENABLE_SPELL)

if(NOT XSIGMA_ENABLE_SPELL)
  return()
endif()

# Skip spell checking for third-party libraries
get_filename_component(CURRENT_DIR_NAME "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
if(CURRENT_DIR_NAME STREQUAL "ThirdParty"
   OR CMAKE_CURRENT_SOURCE_DIR MATCHES ".*/ThirdParty/.*"
   OR CMAKE_CURRENT_SOURCE_DIR MATCHES ".*/third_party/.*"
   OR CMAKE_CURRENT_SOURCE_DIR MATCHES ".*/3rdparty/.*"
)
  return()
endif()

# Find codespell executable
find_program(
  CODESPELL_EXECUTABLE
  NAMES codespell
  PATHS "$ENV{HOME}/.local/bin" "/usr/local/bin" "/usr/bin"
        "$ENV{USERPROFILE}/AppData/Local/Programs/Python/Python*/Scripts"
        "$ENV{PROGRAMFILES}/Python*/Scripts"
  DOC "Path to codespell executable"
)

if(NOT CODESPELL_EXECUTABLE)
  message(
    FATAL_ERROR
      "Codespell requested but not found!

Please install codespell:

  - pip install codespell
  - conda install -c conda-forge codespell
  - Ubuntu/Debian: sudo apt-get install codespell
  - macOS: brew install codespell
  - Windows: pip install codespell

Or set XSIGMA_ENABLE_SPELL=OFF to disable spell checking"
  )
else()
  message(STATUS "Found codespell: ${CODESPELL_EXECUTABLE}")

  # Check if .codespellrc configuration file exists
  set(CODESPELL_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.codespellrc")
  set(CODESPELL_ARGS)

  if(EXISTS ${CODESPELL_CONFIG_FILE})
    message(STATUS "Using codespell configuration: ${CODESPELL_CONFIG_FILE}")
    # codespell automatically reads .codespellrc from the current directory
  else()
    # Default configuration if no .codespellrc exists
    list(APPEND CODESPELL_ARGS "--skip=.git,.augment,.github,.vscode,build,Build,Cmake,ThirdParty"
         "--ignore-words-list=ThirdParty" "--check-hidden=no"
    )
  endif()

  # Add write-changes flag for automatic corrections
  list(APPEND CODESPELL_ARGS "--write-changes")

  # Create a custom target for spell checking
  add_custom_target(
    spell_check
    COMMAND ${CODESPELL_EXECUTABLE} ${CODESPELL_ARGS} ${CMAKE_CURRENT_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running spell check with automatic corrections..."
    VERBATIM
  )

  # Add spell checking to the build process This will run spell check during the build
  add_custom_target(
    spell_check_build ALL
    COMMAND ${CODESPELL_EXECUTABLE} ${CODESPELL_ARGS} ${CMAKE_CURRENT_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running spell check with automatic corrections during build..."
    VERBATIM
  )

  # Warning message about automatic corrections
  message(
    WARNING
      "XSIGMA_ENABLE_SPELL is ON: codespell will modify source files directly to fix spelling errors. "
      "Ensure you have committed your changes before building."
  )
endif()
