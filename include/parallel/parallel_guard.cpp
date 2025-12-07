/*
 * This code is inspired by PyTorch's parallel implementation.
 */

/**
 * @file parallel_guard.cpp
 * @brief Implementation of RAII Guard for Parallel Region State Management
 *
 * This file implements the parallel_guard class, which provides automatic management
 * of thread-local parallel region state using the RAII (Resource Acquisition Is
 * Initialization) pattern.
 *
 * PURPOSE:
 * ========
 * The parallel_guard implementation serves two critical functions:
 *
 * 1. Nested Parallelism Prevention:
 *    - Tracks whether code is executing within a parallel_for or parallel_reduce
 *    - Prevents nested parallel regions which cause performance degradation
 *    - Allows parallel algorithms to check in_parallel_region() and fall back to sequential
 *
 * 2. Automatic State Management:
 *    - Saves current parallel region state on construction
 *    - Restores previous state on destruction (RAII pattern)
 *    - Exception-safe (destructor always runs)
 *    - No manual cleanup required
 *
 * IMPLEMENTATION DETAILS:
 * =======================
 * Thread-Local Storage:
 * - Uses thread_local variable in_parallel_region_ for per-thread state
 * - Each thread has independent parallel region state
 * - No synchronization needed between threads
 * - No shared mutable state
 *
 * RAII Pattern:
 * - Constructor: Saves current state, sets new state
 * - Destructor: Restores previous state automatically
 * - Works correctly even with exceptions or early returns
 * - Guarantees state consistency
 *
 * THREAD SAFETY:
 * ==============
 * - Thread-safe via thread-local storage
 * - Each thread's state is independent
 * - No race conditions possible
 * - No synchronization overhead
 *
 * CODING STANDARDS:
 * =================
 * - Follows Graphfic C++ coding standards
 * - snake_case naming convention
 * - RAII pattern for resource management
 * - No exceptions in destructor (required for RAII correctness)
 * - Proper const correctness
 *
 * USAGE PATTERN:
 * ==============
 * The parallel_guard is used internally by parallel_for and parallel_reduce:
 * @code
 * void parallel_for(...) {
 *     if (use_parallel) {
 *         invoke_parallel(..., [&](int64_t begin, int64_t end) {
 *             parallel_guard guard(true);  // Mark as in parallel region
 *             user_function(begin, end);
 *         });  // guard destructor restores state
 *     }
 * }
 * @endcode
 */

#include "parallel_guard.h"

namespace graphfic
{

namespace
{
/**
 * @brief Thread-local state tracking whether we're in a parallel region.
 *
 * This variable is thread-local, meaning each thread has its own independent copy.
 * It is used by parallel_guard to track whether the current thread is executing
 * within a parallel_for or parallel_reduce.
 *
 * Initial value: false (not in parallel region)
 * Modified by: parallel_guard constructor/destructor
 * Queried by: parallel_guard::is_enabled(), in_parallel_region()
 */
thread_local bool in_parallel_region_ = false;
}  // namespace

/**
 * @brief Returns the current thread-local parallel region state.
 *
 * This static method provides read-only access to the thread-local parallel
 * region state. It is used by in_parallel_region() API function.
 *
 * @return true if currently in a parallel region, false otherwise
 *
 * Thread Safety: Thread-safe (returns thread-local value).
 */
bool parallel_guard::is_enabled()
{
    return in_parallel_region_;
}

/**
 * @brief Constructs guard, saves current state, and sets new state.
 *
 * Saves the current parallel region state in previous_state_ and sets the
 * thread-local state to the new value. The previous state will be automatically
 * restored when the guard is destroyed.
 *
 * @param state New parallel region state to set (true = in parallel region)
 *
 * Thread Safety: Thread-safe (modifies thread-local storage).
 */
parallel_guard::parallel_guard(bool state) : previous_state_(in_parallel_region_)
{
    in_parallel_region_ = state;
}

/**
 * @brief Destructor restores the previous parallel region state.
 *
 * Automatically restores the parallel region state that was saved during
 * construction. This ensures proper state management even in the presence
 * of exceptions or early returns (RAII pattern).
 *
 * Thread Safety: Thread-safe (modifies thread-local storage).
 *
 * @note Never throws exceptions (required for RAII correctness).
 */
parallel_guard::~parallel_guard()
{
    in_parallel_region_ = previous_state_;
}

}  // namespace graphfic
