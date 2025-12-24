/*
 * Serialization DLL Export/Import Header
 *
 * This file defines macros for symbol visibility control across different
 * platforms and build configurations (static vs shared libraries).
 *
 * Inspired by Google Benchmark's export system.
 *
 * Usage:
 * - SERIALIZATION_API: Use for functions and methods that need to be exported/imported
 * - SERIALIZATION_VISIBILITY: Use for class declarations that need to be exported
 * - SERIALIZATION_IMPORT: Explicit import decoration (rarely needed)
 * - SERIALIZATION_HIDDEN: Hide symbols from external visibility
 *
 * This header uses generic macro names and can be reused by any Serialization library project.
 * Each project should define the appropriate macros in their CMakeLists.txt:
 * - SERIALIZATION_STATIC_DEFINE for static library builds
 * - SERIALIZATION_SHARED_DEFINE for shared library builds
 * - SERIALIZATION_BUILDING_DLL when building the shared library
 */

#pragma once

#define SERIALIZATION_VISIBILITY_ENUM

// Platform and build configuration detection
#if defined(SERIALIZATION_STATIC_DEFINE)
// Static library - no symbol decoration needed
#define SERIALIZATION_API
#define SERIALIZATION_VISIBILITY
#define SERIALIZATION_IMPORT
#define SERIALIZATION_HIDDEN

#elif defined(SERIALIZATION_SHARED_DEFINE)
// Shared library - platform-specific symbol decoration
#if defined(_WIN32) || defined(__CYGWIN__)
// Windows DLL export/import
#ifdef SERIALIZATION_BUILDING_DLL
#define SERIALIZATION_API __declspec(dllexport)
#else
#define SERIALIZATION_API __declspec(dllimport)
#endif
#define SERIALIZATION_VISIBILITY
#define SERIALIZATION_IMPORT __declspec(dllimport)
#define SERIALIZATION_HIDDEN
#elif defined(__GNUC__) && __GNUC__ >= 4
// GCC 4+ visibility attributes
#define SERIALIZATION_API __attribute__((visibility("default")))
#define SERIALIZATION_VISIBILITY __attribute__((visibility("default")))
#define SERIALIZATION_IMPORT __attribute__((visibility("default")))
#define SERIALIZATION_HIDDEN __attribute__((visibility("hidden")))
#else
// Fallback for other compilers
#define SERIALIZATION_API
#define SERIALIZATION_VISIBILITY
#define SERIALIZATION_IMPORT
#define SERIALIZATION_HIDDEN
#endif

#else
// Default fallback - assume static linking
#define SERIALIZATION_API
#define SERIALIZATION_VISIBILITY
#define SERIALIZATION_IMPORT
#define SERIALIZATION_HIDDEN
#endif
