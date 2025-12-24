/* Copyright 2018 The Serialization Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#pragma once

#include <cstring>
#include <string>
#include <type_traits>  // for std::underlying_type
#include <typeinfo>

#include "util/configure.h"

//----------------------------------------------------------------------------
// Check for unsupported old compilers - updated minimum requirements
#if defined(_MSC_VER) && _MSC_VER < 1910  // VS2017 15.0 minimum for C++17
#error SERIALIZATION requires MSVC++ 15.0 (Visual Studio 2017) or newer for C++17 support
#endif

#if !defined(__clang__) && defined(__GNUC__) && \
    (__GNUC__ < 7 || (__GNUC__ == 7 && __GNUC_MINOR__ < 1))  // GCC 7.1+ for C++17
#error SERIALIZATION requires GCC 7.1 or newer for C++17 support
#endif

#if defined(__clang__) && (__clang_major__ < 5)  // Clang 5.0+ for C++17
#error SERIALIZATION requires Clang 5.0 or newer for C++17 support
#endif

//------------------------------------------------------------------------
#ifdef SERIALIZATION_MOBILE
// Use 16-byte alignment on mobile
// - ARM NEON AArch32 and AArch64
// - x86[-64] < AVX
inline constexpr size_t SERIALIZATION_ALIGNMENT = 16;
#else
// Use 64-byte alignment should be enough for computation up to AVX512.
inline constexpr size_t SERIALIZATION_ALIGNMENT = 64;
#endif

//------------------------------------------------------------------------
// DLL Export/Import macros - include from separate export header
#include "export.h"

//------------------------------------------------------------------------
#if defined(_MSC_VER)
#if (_MSC_VER < 1900)
// Visual studio until 2015 is not supporting standard 'alignas' keyword
#ifdef alignas
// This check can be removed when verified that for all other versions alignas
// works as requested
#error "SERIALIZATION error: alignas already defined"
#else
#define alignas(alignment) __declspec(align(alignment))
#endif
#endif

#ifdef alignas
#define SERIALIZATION_ALIGN(alignment) alignas(alignment)
#else
#define SERIALIZATION_ALIGN(alignment) __declspec(align(alignment))
#endif
#elif defined(__GNUC__)
#define SERIALIZATION_ALIGN(alignment) __attribute__((aligned(alignment)))
#elif defined(__ICC) || defined(__INTEL_COMPILER)
#endif

//------------------------------------------------------------------------
#if defined(_MSC_VER)
#define SERIALIZATION_RESTRICT __restrict
#elif defined(__cplusplus)
// C++ doesn't have standard restrict, use compiler extensions
#if defined(__GNUC__) || defined(__clang__)
#define SERIALIZATION_RESTRICT __restrict__
#else
#define SERIALIZATION_RESTRICT
#endif
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
// C99 and later
#define SERIALIZATION_RESTRICT restrict
#else
// Fallback for older compilers
#define SERIALIZATION_RESTRICT
#endif

//------------------------------------------------------------------------
inline constexpr int SERIALIZATION_COMPILE_TIME_MAX_GPUS = 16;

//------------------------------------------------------------------------
/* Various compiler-specific performance hints. keep compiler order! */
#ifdef __SERIALIZATION_WRAP__
#define SERIALIZATION_VECTORCALL
#define SERIALIZATION_FORCE_INLINE inline

#elif defined(_MSC_VER)
#define SERIALIZATION_VECTORCALL __vectorcall
#define SERIALIZATION_FORCE_INLINE __forceinline

#elif defined(__INTEL_COMPILER)
#define SERIALIZATION_VECTORCALL
#define SERIALIZATION_FORCE_INLINE inline __attribute__((always_inline))

#elif defined(__clang__)
#define SERIALIZATION_VECTORCALL
#define SERIALIZATION_FORCE_INLINE inline __attribute__((always_inline))

#elif defined(__GNUC__)
#define SERIALIZATION_VECTORCALL
#define SERIALIZATION_FORCE_INLINE inline __attribute__((always_inline))

#else
#define SERIALIZATION_VECTORCALL
#define SERIALIZATION_FORCE_INLINE inline
#endif

//------------------------------------------------------------------------
#if (defined(__GNUC__) || defined(__APPLE__)) && !defined(SWIG)
// Compiler supports GCC-style attributes
#define SERIALIZATION_NORETURN __attribute__((noreturn))
#define SERIALIZATION_NOINLINE __attribute__((noinline))
#define SERIALIZATION_COLD __attribute__((cold))
#elif defined(_MSC_VER)
// Non-GCC equivalents
#define SERIALIZATION_NORETURN __declspec(noreturn)
#define SERIALIZATION_NOINLINE
#define SERIALIZATION_COLD
#else
// Non-GCC equivalents
#define SERIALIZATION_NORETURN
#define SERIALIZATION_NOINLINE
#define SERIALIZATION_COLD
#endif

//------------------------------------------------------------------------
#ifdef NDEBUG
#define SERIALIZATION_SIMD_RETURN_TYPE \
    SERIALIZATION_FORCE_INLINE static void SERIALIZATION_VECTORCALL
#else
#define SERIALIZATION_SIMD_RETURN_TYPE static void
#endif

//------------------------------------------------------------------------
// set function attribute for CUDA
#if defined(__CUDACC__) || defined(__HIPCC__)
#define SERIALIZATION_CUDA_DEVICE __device__
#define SERIALIZATION_CUDA_HOST __host__
#define SERIALIZATION_CUDA_FUNCTION_TYPE __host__ __device__
#else
#define SERIALIZATION_CUDA_DEVICE
#define SERIALIZATION_CUDA_HOST
#define SERIALIZATION_CUDA_FUNCTION_TYPE
#endif

#ifdef NDEBUG
#define SERIALIZATION_FUNCTION_ATTRIBUTE SERIALIZATION_FORCE_INLINE SERIALIZATION_CUDA_FUNCTION_TYPE
#else
#define SERIALIZATION_FUNCTION_ATTRIBUTE SERIALIZATION_CUDA_FUNCTION_TYPE
#endif

//----------------------------------------------------------------------------
#if defined(__GNUC__) && \
    (defined(__i386__) || defined(__x86_64__) || defined(__arm__) || defined(__aarch64__))
#define SERIALIZATION_ASM_COMMENT(X) __asm__("#" X)
#else
#define SERIALIZATION_ASM_COMMENT(X)
#endif

//----------------------------------------------------------------------------
#define SERIALIZATION_CONCATENATE_IMPL(s1, s2) s1##s2
#define SERIALIZATION_CONCATENATE(s1, s2) SERIALIZATION_CONCATENATE_IMPL(s1, s2)

//----------------------------------------------------------------------------
#ifdef __COUNTER__
#define SERIALIZATION_UID __COUNTER__
#define SERIALIZATION_ANONYMOUS_VARIABLE(str) SERIALIZATION_CONCATENATE(str, __COUNTER__)
#else
#define SERIALIZATION_UID __LINE__
#define SERIALIZATION_ANONYMOUS_VARIABLE(str) SERIALIZATION_CONCATENATE(str, __LINE__)
#endif

//----------------------------------------------------------------------------
#if !SERIALIZATION_HAS_THREE_WAY_COMPARISON
#if defined(__cpp_impl_three_way_comparison) && __cpp_impl_three_way_comparison >= 201907L && \
    defined(__cpp_lib_three_way_comparison) && __cpp_lib_three_way_comparison >= 201907L
#define SERIALIZATION_HAS_THREE_WAY_COMPARISON 1
#else
#define SERIALIZATION_HAS_THREE_WAY_COMPARISON 0
#endif
#endif

//----------------------------------------------------------------------------
// A function level attribute to disable checking for use of uninitialized
// memory when built with MemorySanitizer.
#if defined(__clang__)
#if __has_feature(memory_sanitizer)
#define SERIALIZATION_NO_SANITIZE_MEMORY __attribute__((no_sanitize_memory))
#else
#define SERIALIZATION_NO_SANITIZE_MEMORY
#endif  // __has_feature(memory_sanitizer)
#else
#define SERIALIZATION_NO_SANITIZE_MEMORY
#endif  // __clang__

//----------------------------------------------------------------------------
#define MACRO_CORE_TYPE_ID_NAME(x) typeid(x).name()

//----------------------------------------------------------------------------
#define SERIALIZATION_DELETE_CLASS(type)     \
    type()                         = delete; \
    type(const type&)              = delete; \
    type& operator=(const type& a) = delete; \
    type(type&&)                   = delete; \
    type& operator=(type&&)        = delete; \
    ~type()                        = delete;

#define SERIALIZATION_DELETE_COPY_AND_MOVE(type) \
private:                                         \
    type(const type&)              = delete;     \
    type& operator=(const type& a) = delete;     \
    type(type&&)                   = delete;     \
    type& operator=(type&&)        = delete;     \
                                                 \
public:

#define SERIALIZATION_DELETE_COPY(type)      \
    type(const type&)              = delete; \
    type& operator=(const type& a) = delete;

//----------------------------------------------------------------------------
// format string checking.
#if !defined(MACRO_CORE_PRINTF_FORMAT)
#if defined(__GNUC__)
#define MACRO_CORE_PRINTF_FORMAT(a, b) __attribute__((format(printf, a, b)))
#else
#define MACRO_CORE_PRINTF_FORMAT(a, b)
#endif
#endif

//----------------------------------------------------------------------------
namespace serialization
{
template <typename...>
using void_t = std::void_t<>;
}  // namespace serialization

//----------------------------------------------------------------------------
#if (!defined(__INTEL_COMPILER)) & defined(_MSC_VER)
#define serialization_int __int64
#define serialization_long unsigned __int64
#else
#define serialization_int long long int
#define serialization_long unsigned long long int
#endif

//----------------------------------------------------------------------------
#if defined(__cplusplus) && defined(__has_cpp_attribute)
#define SERIALIZATION_HAVE_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define SERIALIZATION_HAVE_CPP_ATTRIBUTE(x) 0
#endif

//----------------------------------------------------------------------------
#ifdef __has_attribute
#define SERIALIZATION_HAVE_ATTRIBUTE(x) __has_attribute(x)
#else
#define SERIALIZATION_HAVE_ATTRIBUTE(x) 0
#endif

//------------------------------------------------------------------------
// C++20 [[likely]] and [[unlikely]] attributes are only available in C++20 and later
#if __cplusplus >= 202002L && SERIALIZATION_HAVE_CPP_ATTRIBUTE(likely) && \
    SERIALIZATION_HAVE_CPP_ATTRIBUTE(unlikely)
#define SERIALIZATION_LIKELY(expr) (expr) [[likely]]
#define SERIALIZATION_UNLIKELY(expr) (expr) [[unlikely]]
#elif defined(__GNUC__) || defined(__ICL) || defined(__clang__)
#define SERIALIZATION_LIKELY(expr) (__builtin_expect(static_cast<bool>(expr), 1))
#define SERIALIZATION_UNLIKELY(expr) (__builtin_expect(static_cast<bool>(expr), 0))
#else
#define SERIALIZATION_LIKELY(expr) (expr)
#define SERIALIZATION_UNLIKELY(expr) (expr)
#endif

//----------------------------------------------------------------------------
#if __cplusplus >= 202002L
#define SERIALIZATION_FUNCTION_CONSTEXPR constexpr
#else
#define SERIALIZATION_FUNCTION_CONSTEXPR
#endif

//----------------------------------------------------------------------------
#if __cplusplus >= 201703L
#define SERIALIZATION_NODISCARD [[nodiscard]]
#else
#define SERIALIZATION_NODISCARD
#endif

//----------------------------------------------------------------------------
#if __cplusplus >= 201703L
#define SERIALIZATION_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
// For GCC or Clang: use __attribute__
#define SERIALIZATION_UNUSED __attribute__((unused))
#elif defined(_MSC_VER)
// For MSVC
#define SERIALIZATION_UNUSED __pragma(warning(suppress : 4100))
#else
// Fallback for other compilers
#define SERIALIZATION_UNUSED
#endif

//----------------------------------------------------------------------------
// Demangle
#if defined(__ANDROID__) && (defined(__i386__) || defined(__x86_64__))
#define SERIALIZATION_HAS_CXA_DEMANGLE 0
#elif (__GNUC__ >= 4 || (__GNUC__ >= 3 && __GNUC_MINOR__ >= 4)) && !defined(__mips__)
#define SERIALIZATION_HAS_CXA_DEMANGLE 1
#elif defined(__clang__) && !defined(_MSC_VER)
#define SERIALIZATION_HAS_CXA_DEMANGLE 1
#else
#define SERIALIZATION_HAS_CXA_DEMANGLE 0
#endif

//----------------------------------------------------------------------------
// Printf-style format checking
#if SERIALIZATION_HAVE_ATTRIBUTE(format)
#define SERIALIZATION_PRINTF_ATTRIBUTE(format_index, first_to_check) \
    __attribute__((format(printf, format_index, first_to_check)))
#else
#define SERIALIZATION_PRINTF_ATTRIBUTE(format_index, first_to_check)
#endif

//----------------------------------------------------------------------------
// Thread safety analysis disable
#if SERIALIZATION_HAVE_ATTRIBUTE(no_thread_safety_analysis)
#define SERIALIZATION_NO_THREAD_SAFETY_ANALYSIS __attribute__((no_thread_safety_analysis))
#else
#define SERIALIZATION_NO_THREAD_SAFETY_ANALYSIS
#endif

//----------------------------------------------------------------------------
// Thread safety - guarded by mutex
#if SERIALIZATION_HAVE_ATTRIBUTE(guarded_by)
#define SERIALIZATION_GUARDED_BY(x) __attribute__((guarded_by(x)))
#else
#define SERIALIZATION_GUARDED_BY(x)
#endif

//----------------------------------------------------------------------------
// Thread safety - exclusive locks required
#if SERIALIZATION_HAVE_ATTRIBUTE(exclusive_locks_required)
#define SERIALIZATION_EXCLUSIVE_LOCKS_REQUIRED(...) \
    __attribute__((exclusive_locks_required(__VA_ARGS__)))
#else
#define SERIALIZATION_EXCLUSIVE_LOCKS_REQUIRED(...)
#endif

//----------------------------------------------------------------------------
#if SERIALIZATION_HAVE_CPP_ATTRIBUTE(clang::lifetimebound)
#define SERIALIZATION_LIFETIME_BOUND [[clang::lifetimebound]]
#elif SERIALIZATION_HAVE_CPP_ATTRIBUTE(msvc::lifetimebound)
#define SERIALIZATION_LIFETIME_BOUND [[msvc::lifetimebound]]
#elif SERIALIZATION_HAVE_ATTRIBUTE(lifetimebound)
#define SERIALIZATION_LIFETIME_BOUND __attribute__((lifetimebound))
#else
#define SERIALIZATION_LIFETIME_BOUND
#endif

//----------------------------------------------------------------------------
#if SERIALIZATION_HAVE_ATTRIBUTE(locks_excluded)
#define SERIALIZATION_LOCKS_EXCLUDED(...) __attribute__((locks_excluded(__VA_ARGS__)))
#else
#define SERIALIZATION_LOCKS_EXCLUDED(...)
#endif

//----------------------------------------------------------------------------
#if SERIALIZATION_HAVE_CPP_ATTRIBUTE(clang::require_constant_initialization)
#define SERIALIZATION_CONST_INIT [[clang::require_constant_initialization]]
#else
#define SERIALIZATION_CONST_INIT
#endif

//----------------------------------------------------------------------------
// Define missing Darwin types for Homebrew Clang (simplified for C++17)
#if defined(__APPLE__) && !defined(__DEFINED_DARWIN_TYPES)
#define __DEFINED_DARWIN_TYPES
using __uint32_t = std::uint32_t;
using __uint64_t = std::uint64_t;
using __int32_t  = std::int32_t;
using __int64_t  = std::int64_t;
using __uint8_t  = std::uint8_t;
using __uint16_t = std::uint16_t;
using __int8_t   = std::int8_t;
using __int16_t  = std::int16_t;
#endif