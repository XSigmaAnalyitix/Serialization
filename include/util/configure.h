#pragma once

// Serialization Configuration Header
// This file contains build configuration settings

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
#define SERIALIZATION_PLATFORM_WINDOWS
#elif defined(__linux__)
#define SERIALIZATION_PLATFORM_LINUX
#elif defined(__APPLE__)
#define SERIALIZATION_PLATFORM_MACOS
#endif

// Compiler detection
#if defined(_MSC_VER)
#define SERIALIZATION_COMPILER_MSVC
#elif defined(__clang__)
#define SERIALIZATION_COMPILER_CLANG
#elif defined(__GNUC__)
#define SERIALIZATION_COMPILER_GCC
#endif

// Demangling support (available on GCC, Clang, and compatible compilers)
#if defined(__GNUC__) || defined(__clang__)
#define HAS_DEMANGLE 1
#else
#define HAS_DEMANGLE 0
#endif

// Build configuration
#ifdef NDEBUG
#define SERIALIZATION_BUILD_RELEASE
#else
#define SERIALIZATION_BUILD_DEBUG
#endif
