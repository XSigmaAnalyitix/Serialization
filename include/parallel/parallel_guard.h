/*
 * This code is inspired by PyTorch's parallel implementation.
 */

#pragma once

/**
 * @file parallel_guard.h
 * @brief RAII Guard for Parallel Region State Management
 *
 * This file provides the parallel_guard class, which is a RAII (Resource Acquisition
 * Is Initialization) guard for managing thread-local parallel region state. It is used
 * internally by parallel_for and parallel_reduce to track whether code is currently
 * executing within a parallel region.
 *
 * PURPOSE:
 * ========
 * The parallel_guard serves two critical functions:
 *
 * 1. Nested Parallelism Prevention:
 *    - Prevents nested parallel regions which can cause performance degradation
 *    - in_parallel_region() returns true when inside a parallel_for/reduce
 *    - Nested calls automatically fall back to sequential execution
 *
 * 2. State Management:
 *    - Automatically saves and restores parallel region state
 *    - Uses RAII pattern for exception safety and correctness
 *    - Thread-local storage ensures per-thread state tracking
 *
 * DESIGN PATTERN:
 * ===============
 * RAII (Resource Acquisition Is Initialization):
 * - Constructor: Saves current state and sets new state
 * - Destructor: Restores previous state automatically
 * - No manual cleanup required
 * - Exception-safe (destructor always runs)
 *
 * THREAD SAFETY:
 * ==============
 * - Thread-safe via thread-local storage
 * - Each thread has independent parallel region state
 * - No synchronization needed between threads
 * - No shared mutable state
 *
 * CODING STANDARDS:
 * =================
 * - Follows Graphfic C++ coding standards
 * - snake_case naming convention
 * - RAII pattern for resource management
 * - No exceptions in destructor
 * - Non-copyable and non-movable (guard semantics)
 *
 * USAGE EXAMPLE:
 * ==============
 * @code
 * void process_data() {
 *     xsigma::parallel_guard guard(true);  // Mark as in parallel region
 *     // ... do work ...
 * }  // Automatically restores previous state
 *
 * // Nested parallelism prevention:
 * xsigma::parallel_for(0, 100, 10, [](int64_t begin, int64_t end) {
 *     // in_parallel_region() returns true here
 *     xsigma::parallel_for(0, 10, 1, [](int64_t b, int64_t e) {
 *         // This will execute sequentially (nested parallelism prevented)
 *     });
 * });
 * @endcode
 */

#include "common/export.h"
#include "common/macros.h"

namespace graphfic
{

/**
 * @brief RAII guard for managing parallel region state.
 *
 * This class provides automatic management of thread-local parallel region state
 * using the RAII pattern. It saves the current state on construction, sets a new
 * state, and restores the previous state on destruction.
 *
 * The parallel region state is used to:
 * - Track whether code is executing inside parallel_for or parallel_reduce
 * - Prevent nested parallelism (which can cause performance issues)
 * - Provide the in_parallel_region() API for user queries
 *
 * Thread Safety: Thread-safe via thread-local storage.
 * Backend: Both OpenMP and native backends.
 * Design Pattern: RAII (Resource Acquisition Is Initialization).
 * Coding Standard: Follows Graphfic C++ standards (snake_case, no exceptions).
 *
 * @note This class is non-copyable and non-movable (guard semantics).
 * @note The destructor never throws exceptions (required for RAII correctness).
 */
class GRAPHFIC_VISIBILITY parallel_guard
{
public:
    /**
     * @brief Checks if parallel region state is currently enabled.
     *
     * Returns the current thread-local parallel region state. This is used
     * by in_parallel_region() to determine if code is executing within a
     * parallel_for or parallel_reduce.
     *
     * @return true if currently in a parallel region, false otherwise
     *
     * Thread Safety: Thread-safe, returns thread-local value.
     */
    static GRAPHFIC_API bool is_enabled();

    /**
     * @brief Constructs guard and sets new parallel region state.
     *
     * Saves the current parallel region state and sets a new state. The previous
     * state will be automatically restored when the guard is destroyed.
     *
     * @param state New parallel region state to set (true = in parallel region)
     *
     * Thread Safety: Thread-safe, modifies thread-local storage.
     *
     * Example:
     * @code
     * {
     *     xsigma::parallel_guard guard(true);  // Set state to true
     *     // ... code executes with state = true ...
     * }  // State automatically restored to previous value
     * @endcode
     */
    GRAPHFIC_API parallel_guard(bool state);

    /**
     * @brief Destructor restores previous parallel region state.
     *
     * Automatically restores the parallel region state that was saved during
     * construction. This ensures proper state management even in the presence
     * of exceptions or early returns.
     *
     * Thread Safety: Thread-safe, modifies thread-local storage.
     *
     * @note Never throws exceptions (required for RAII correctness).
     */
    GRAPHFIC_API ~parallel_guard();

    // Non-copyable and non-movable (RAII guard semantics)
    parallel_guard(const parallel_guard&)            = delete;
    parallel_guard(parallel_guard&&)                 = delete;
    parallel_guard& operator=(const parallel_guard&) = delete;
    parallel_guard& operator=(parallel_guard&&)      = delete;

private:
    bool previous_state_;  ///< Saved state to restore on destruction
};

}  // namespace graphfic
