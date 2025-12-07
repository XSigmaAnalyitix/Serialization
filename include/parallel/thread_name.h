/*
 * This code is inspired by PyTorch's parallel implementation.
 */

#pragma once

/**
 * @file thread_name.h
 * @brief Thread naming utilities for Graphfic parallel execution
 *
 * This header provides cross-platform thread naming functionality used by
 * the parallel module for debugging and profiling purposes. The implementation
 * is self-contained and has no dependencies on the logging module.
 *
 * Thread names are set per-thread and can be used by debuggers, profilers,
 * and logging systems to identify threads in traces and logs.
 *
 * USAGE:
 * ======
 * @code
 * #include "parallel/thread_name.h"
 *
 * // Set thread name for the current thread
 * graphfic::detail::parallel::set_thread_name("WorkerThread");
 *
 * // Get thread name of the current thread
 * std::string name = graphfic::detail::parallel::get_thread_name();
 * @endcode
 *
 * PLATFORM SUPPORT AND LIMITATIONS:
 * =================================
 * - Windows (10 1607+): Uses SetThreadDescription/GetThreadDescription APIs
 *   - No name length limit
 *   - Supports Unicode via UTF-8 to UTF-16 conversion
 *   - Falls back gracefully on older Windows versions
 *
 * - Unix (Linux, macOS, BSD, etc.): Uses pthread_setname_np/pthread_getname_np
 *   - Thread name limited to 16 characters (including null terminator)
 *   - Names longer than 15 characters are truncated
 *   - macOS uses single-argument version (current thread only)
 *   - Linux and most other Unix systems use two-argument version
 *
 * THREAD SAFETY:
 * ==============
 * - Thread-safe: Each thread has independent name storage
 * - No synchronization needed: Uses thread-local storage
 * - Safe to call from any thread at any time
 *
 * ERROR HANDLING:
 * ===============
 * - Functions never throw exceptions
 * - Invalid or empty names are handled gracefully
 * - Long names are truncated to platform limits
 * - Platform API failures are silently ignored (thread-local fallback used)
 *
 * CODING STANDARDS:
 * =================
 * - Follows Graphfic C++ coding standards
 * - snake_case naming convention
 * - No exceptions (error handling via return values)
 * - Cross-platform compatible
 */

#include <string>

#include "common/export.h"

namespace graphfic::detail::parallel
{

/**
 * @brief Sets the name of the current thread.
 *
 * Sets a human-readable name for the current thread that can be used by
 * debuggers, profilers, and logging systems for identification.
 *
 * The name is set both in the OS thread naming facility (where available)
 * and in thread-local storage for reliable retrieval.
 *
 * @param name The name to assign to the current thread.
 *             - Unix: max 15 characters (truncated if longer)
 *             - Windows: no practical limit
 *             - Empty names are allowed
 *
 * Thread Safety: Thread-safe (operates on current thread only)
 * Platform Support: Windows and Unix (Linux, macOS, BSD, etc.)
 *
 * @note Thread names may be truncated to fit platform-specific limits.
 *       The original (possibly truncated) name is always stored in
 *       thread-local storage for reliable retrieval via get_thread_name().
 *
 * Example:
 * @code
 * // In a worker thread
 * set_thread_name("Worker-" + std::to_string(thread_id));
 * @endcode
 */
GRAPHFIC_API void set_thread_name(const std::string& name);

/**
 * @brief Gets the name of the current thread.
 *
 * Retrieves the name previously set for the current thread via set_thread_name.
 * If no name was set, returns an empty string.
 *
 * @return The name of the current thread, or empty string if not set.
 *
 * Thread Safety: Thread-safe (operates on current thread only)
 * Platform Support: Windows and Unix (Linux, macOS, BSD, etc.)
 *
 * @note On platforms that support OS-level thread naming, this function
 *       attempts to read from the OS first, falling back to thread-local
 *       storage if the OS call fails or returns an empty string.
 *
 * Example:
 * @code
 * std::string name = get_thread_name();
 * if (name.empty()) {
 *     name = "unnamed";
 * }
 * @endcode
 */
GRAPHFIC_API std::string get_thread_name();

}  // namespace graphfic::detail::parallel
