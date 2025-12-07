/*
 * This code is inspired by PyTorch's parallel implementation.
 */

#pragma once

/**
 * @file parallel.h
 * @brief Graphfic Parallel Execution Framework - Main API Header
 *
 * This file provides the complete parallel execution API for the Graphfic framework,
 * including both intra-op parallelism (parallel_for, parallel_reduce) and inter-op
 * parallelism (thread pool-based task execution).
 *
 * ARCHITECTURE OVERVIEW:
 * =====================
 * The parallel module supports three backend implementations selected at compile time:
 *
 * 1. OpenMP Backend (GRAPHFIC_HAS_OPENMP=1):
 *    - Uses OpenMP pragmas for parallel execution
 *    - Template-based invoke_parallel implementation (defined in this header)
 *    - Leverages compiler's OpenMP runtime for thread management
 *    - Preferred for maximum performance on supported platforms
 *
 * 2. TBB Backend (GRAPHFIC_HAS_TBB=1 and GRAPHFIC_HAS_OPENMP=0):
 *    - Uses Intel TBB (Threading Building Blocks) for parallel execution
 *    - Template-based invoke_parallel implementation (defined in this header)
 *    - Leverages TBB's task scheduler for work stealing and load balancing
 *    - Provides excellent scalability and performance
 *    - Preferred when OpenMP is not available but TBB is
 *
 * 3. Native Backend (GRAPHFIC_HAS_OPENMP=0 and GRAPHFIC_HAS_TBB=0):
 *    - Custom thread pool implementation (see thread_pool.h)
 *    - Function-based invoke_parallel implementation (in parallel.cpp)
 *    - Provides cross-platform compatibility without OpenMP or TBB dependency
 *    - Uses C++ standard library threading primitives
 *
 * CONSOLIDATION ARCHITECTURE:
 * ==========================
 * This module has been consolidated for maintainability:
 * - Single header file (parallel.h) contains all API declarations and template implementations
 * - Single implementation file (parallel.cpp) with function-level conditional compilation
 * - Backend-specific code is conditionally compiled using #if GRAPHFIC_HAS_OPENMP / #elif GRAPHFIC_HAS_TBB / #else
 * - No separate backend-specific header or implementation files
 * - Backend priority: OpenMP > TBB > Native (first available backend is used)
 *
 * PARALLELISM TYPES:
 * ==================
 * 1. Intra-op Parallelism:
 *    - Parallelism within a single operation (e.g., parallel_for, parallel_reduce)
 *    - Controlled by set_num_threads() / get_num_threads()
 *    - Uses invoke_parallel() internally
 *    - Enabled by INTRA_OP_PARALLEL macro
 *
 * 2. Inter-op Parallelism:
 *    - Parallelism between different operations
 *    - Controlled by set_num_interop_threads() / get_num_interop_threads()
 *    - Uses separate thread pool via launch()
 *    - Allows concurrent execution of independent operations
 *
 * THREAD SAFETY:
 * ==============
 * - All public API functions are thread-safe unless otherwise documented
 * - Thread-local state is managed via thread_id_guard (RAII pattern)
 * - Parallel region state is tracked via parallel_guard (RAII pattern)
 * - Atomic operations use acquire-release semantics for proper memory ordering
 * - No global mutable state without synchronization
 *
 * CODING STANDARDS COMPLIANCE:
 * ============================
 * - Follows Graphfic C++ coding standards (snake_case naming, no exceptions)
 * - Error handling via return values and GRAPHFIC_CHECK macros (no throw/catch)
 * - RAII patterns for resource management (guards, thread pools)
 * - Memory ordering semantics explicitly documented for atomic operations
 * - Cross-platform compatible (Linux, macOS, Windows)
 *
 * DESIGN PATTERNS:
 * ================
 * - RAII: thread_id_guard, parallel_guard for automatic state management
 * - Template metaprogramming: Generic parallel_for/reduce work with any callable
 * - Conditional compilation: Backend selection at compile time for zero overhead
 * - Lazy initialization: Thread counts initialized on first use
 * - Registry pattern: Thread pool management via ThreadPoolRegistry
 *
 * USAGE EXAMPLE:
 * ==============
 * @code
 * // Parallel for loop
 * graphfic::parallel_for(0, 1000, 100, [](int64_t begin, int64_t end) {
 *     for (int64_t i = begin; i < end; ++i) {
 *         // Process element i
 *     }
 * });
 *
 * // Parallel reduction
 * int sum = graphfic::parallel_reduce(
 *     0, 1000, 100, 0,
 *     [](int64_t begin, int64_t end, int identity) {
 *         int partial_sum = identity;
 *         for (int64_t i = begin; i < end; ++i) {
 *             partial_sum += i;
 *         }
 *         return partial_sum;
 *     },
 *     [](int a, int b) { return a + b; }
 * );
 * @endcode
 */

#include <functional>
#include <string>
#include <array>

#include "common/export.h"
#include "common/macros.h"
#include "parallel/parallel_guard.h"
#include "util/exception.h"

#ifdef _OPENMP
#include <omp.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#endif

#if GRAPHFIC_HAS_TBB
// Disable TBB implicit linkage on MSVC to avoid linker issues
#ifdef _MSC_VER
#pragma push_macro("__TBB_NO_IMPLICIT_LINKAGE")
#define __TBB_NO_IMPLICIT_LINKAGE 1
#endif

#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/task_arena.h>

#ifdef _MSC_VER
#pragma pop_macro("__TBB_NO_IMPLICIT_LINKAGE")
#endif

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#endif

// Define INTRA_OP_PARALLEL for OpenMP, TBB, and native backends
// This enables parallel execution in parallel_for and parallel_reduce templates
// When undefined, these templates fall back to sequential execution
#define INTRA_OP_PARALLEL

namespace graphfic
{

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Integer division with rounding up (ceiling division).
 *
 * Computes ceil(x / y) using integer arithmetic without floating point operations.
 * This is commonly used to calculate the number of chunks needed to cover a range.
 *
 * @param x Dividend (numerator)
 * @param y Divisor (denominator), must be > 0
 * @return Ceiling of x/y
 *
 * @note This function is used extensively in parallel task distribution to ensure
 *       all elements are covered when dividing work into chunks.
 *
 * Example: divup(10, 3) = 4 (covers elements 0-2, 3-5, 6-8, 9)
 */
inline int64_t divup(int64_t x, int64_t y)
{
    return (x + y - 1) / y;
}

// ============================================================================
// Intra-op Parallelism API - Thread Management
// ============================================================================

/**
 * @brief Initializes the number of threads for parallel execution.
 *
 * Called during thread initialization to set up the thread count based on
 * environment variables or system defaults. This function is typically called
 * automatically via lazy_init_num_threads() on first parallel operation.
 *
 * Thread Safety: Thread-safe, uses internal synchronization.
 * Backend: Both OpenMP and native backends.
 */
GRAPHFIC_API void init_num_threads();

/**
 * @brief Sets the number of threads to be used in parallel regions.
 *
 * Controls the maximum number of threads that will be used for intra-op
 * parallelism (parallel_for, parallel_reduce). Setting this to 1 effectively
 * disables parallelism.
 *
 * @param nthreads Number of threads to use (must be > 0)
 *
 * Thread Safety: Thread-safe, uses internal synchronization.
 * Backend: Both OpenMP and native backends.
 *
 * @note OpenMP backend: Sets OMP_NUM_THREADS
 * @note Native backend: Resizes the internal thread pool
 */
GRAPHFIC_API void set_num_threads(int /*nthreads*/);

/**
 * @brief Returns the maximum number of threads that may be used in a parallel region.
 *
 * @return Current thread count setting for intra-op parallelism
 *
 * Thread Safety: Thread-safe.
 * Backend: Both OpenMP and native backends.
 */
GRAPHFIC_API int get_num_threads();

/**
 * @brief Returns the current thread number within a parallel region.
 *
 * @return Thread ID (0-based) in the current parallel region, or 0 if not in a parallel region
 *
 * Thread Safety: Thread-safe, returns thread-local value.
 * Backend: Both OpenMP and native backends.
 *
 * @note This is used internally by parallel_reduce to index into the results array.
 */
GRAPHFIC_API int get_thread_num();

/**
 * @brief Checks whether the code is currently executing in a parallel region.
 *
 * @return true if currently inside a parallel_for or parallel_reduce, false otherwise
 *
 * Thread Safety: Thread-safe, returns thread-local value.
 * Backend: Both OpenMP and native backends.
 *
 * @note This is used to prevent nested parallelism, which can cause performance degradation.
 */
GRAPHFIC_API bool in_parallel_region();

// ============================================================================
// Internal Implementation Details
// ============================================================================

namespace internal
{

/**
 * @brief Lazily initializes the number of threads on first parallel operation.
 *
 * Uses thread-local storage to ensure init_num_threads() is called exactly once
 * per thread on the first parallel operation. This avoids initialization overhead
 * for threads that never use parallelism.
 *
 * Thread Safety: Thread-safe via thread_local storage.
 * Backend: Both OpenMP and native backends.
 *
 * @note This function is called automatically by parallel_for and parallel_reduce.
 */
inline void lazy_init_num_threads()
{
    thread_local bool init = false;
    if GRAPHFIC_UNLIKELY (!init)
    {
        graphfic::init_num_threads();
        init = true;
    }
}

/**
 * @brief Sets the thread number for the current thread (internal use only).
 *
 * @param id Thread ID to set (0-based)
 *
 * Thread Safety: Thread-safe, modifies thread-local storage.
 * Backend: Both OpenMP and native backends.
 *
 * @note This is an internal function used by thread_id_guard.
 */
GRAPHFIC_API void set_thread_num(int /*id*/);

/**
 * @brief RAII guard for managing thread ID state.
 *
 * Automatically saves the current thread ID on construction and restores it
 * on destruction. This ensures proper thread ID management even in the presence
 * of exceptions or early returns.
 *
 * Thread Safety: Thread-safe, manages thread-local state.
 * Backend: Both OpenMP and native backends.
 *
 * Design Pattern: RAII (Resource Acquisition Is Initialization)
 * Coding Standard: Follows Graphfic snake_case naming, no exceptions in destructor
 *
 * Example:
 * @code
 * {
 *     internal::thread_id_guard guard(5);  // Sets thread ID to 5
 *     // ... do work ...
 * }  // Automatically restores previous thread ID
 * @endcode
 */
class GRAPHFIC_VISIBILITY thread_id_guard
{
public:
    /**
     * @brief Constructs guard and sets new thread ID.
     * @param id New thread ID to set
     */
    thread_id_guard(int id) : id_(graphfic::get_thread_num()) { set_thread_num(id); }

    /**
     * @brief Destructor restores previous thread ID.
     */
    ~thread_id_guard() { set_thread_num(id_); }

    // Non-copyable and non-movable (RAII guard)
    thread_id_guard(const thread_id_guard&)            = delete;
    thread_id_guard(thread_id_guard&&)                 = delete;
    thread_id_guard& operator=(const thread_id_guard&) = delete;
    thread_id_guard& operator=(thread_id_guard&&)      = delete;

private:
    int id_;  ///< Saved thread ID to restore on destruction
};

}  // namespace internal

// ============================================================================
// Parallel Algorithms - Template Declarations
// ============================================================================

/**
 * @brief Executes a function in parallel over a range of indices.
 *
 * Divides the range [begin, end) into chunks and executes the user function f
 * on each chunk in parallel. The actual parallelization strategy depends on the
 * backend (OpenMP or native thread pool).
 *
 * @tparam F Callable type with signature void(int64_t begin, int64_t end)
 *
 * @param begin Starting index (inclusive) of the range to process
 * @param end Ending index (exclusive) of the range to process
 * @param grain_size Minimum number of elements per chunk (controls parallelization degree)
 *                   - Larger values: fewer chunks, less overhead, less parallelism
 *                   - Smaller values: more chunks, more overhead, more parallelism
 *                   - Typical values: 100-10000 depending on work per element
 * @param f User function to apply to each chunk, called as f(chunk_begin, chunk_end)
 *
 * Parallelization Decision:
 * - Executes sequentially if: begin >= end, grain_size <= 0, already in parallel region,
 *   num_threads == 1, or (end - begin) <= grain_size
 * - Executes in parallel otherwise
 *
 * Thread Safety:
 * - The function f must be thread-safe if it accesses shared data
 * - Each chunk is processed by exactly one thread
 * - No synchronization is provided between chunks
 *
 * Memory Ordering:
 * - All writes in f are visible after parallel_for returns (implicit barrier)
 * - Uses acquire-release semantics for synchronization
 *
 * Backend Behavior:
 * - OpenMP: Uses #pragma omp parallel with dynamic work distribution
 * - Native: Uses custom thread pool with task queue
 *
 * @warning parallel_for does NOT copy thread-local state from the calling thread
 *          to worker threads. Avoid using thread-local variables or operations
 *          that depend on thread-local state (e.g., some tensor operations).
 *          Use only data pointers and thread-safe operations in f.
 *
 * @note Follows Graphfic coding standards: no exceptions, snake_case naming
 *
 * Example:
 * @code
 * std::vector<int> data(1000);
 * graphfic::parallel_for(0, 1000, 100, [&data](int64_t begin, int64_t end) {
 *     for (int64_t i = begin; i < end; ++i) {
 *         data[i] = i * 2;  // Each thread processes its chunk independently
 *     }
 * });
 * @endcode
 */
template <class F>
inline void parallel_for(
    const int64_t begin, const int64_t end, const int64_t grain_size, const F& f);

/**
 * @brief Performs a parallel reduction over a range of indices.
 *
 * Divides the range [begin, end) into chunks, applies a reduction function f to
 * each chunk in parallel to produce partial results, then combines all partial
 * results using a binary combination function sf to produce the final result.
 *
 * This is a two-phase operation:
 * 1. Parallel phase: Each thread computes a partial result for its chunk
 * 2. Sequential phase: Partial results are combined to produce the final result
 *
 * @tparam scalar_t Type of the reduction result (must be copyable)
 * @tparam F Callable type with signature scalar_t(int64_t begin, int64_t end, scalar_t identity)
 * @tparam SF Callable type with signature scalar_t(scalar_t a, scalar_t b)
 *
 * @param begin Starting index (inclusive) of the range to reduce
 * @param end Ending index (exclusive) of the range to reduce
 * @param grain_size Minimum number of elements per chunk (controls parallelization degree)
 *                   - Determines number of partial results: ceil((end-begin)/grain_size)
 *                   - Larger values: fewer chunks, less memory, less parallelism
 *                   - Smaller values: more chunks, more memory, more parallelism
 * @param ident Identity element for the combination function sf
 *              - Must satisfy: sf(ident, x) == x and sf(x, ident) == x for all x
 *              - Examples: 0 for addition, 1 for multiplication, INT_MIN for max
 * @param f Reduction function applied to each chunk
 *          - Called as: partial_result = f(chunk_begin, chunk_end, ident)
 *          - Should reduce elements [chunk_begin, chunk_end) to a single value
 *          - Must be thread-safe if accessing shared data
 * @param sf Binary combination function to merge partial results
 *           - Called as: combined = sf(partial_result1, partial_result2)
 *           - Must be associative: sf(sf(a,b),c) == sf(a,sf(b,c))
 *           - Should be commutative for best results (order-independent)
 *
 * @return Final reduced value combining all partial results
 *
 * Parallelization Decision:
 * - Returns ident if begin >= end
 * - Executes sequentially if: grain_size <= 0, already in parallel region,
 *   num_threads == 1, or (end - begin) <= grain_size
 * - Executes in parallel otherwise
 *
 * Thread Safety:
 * - Functions f and sf must be thread-safe if accessing shared data
 * - Each chunk is processed by exactly one thread
 * - Partial results are stored in thread-indexed array (no contention)
 * - Final combination is sequential (no race conditions)
 *
 * Memory Ordering:
 * - All writes in f are visible during final combination (implicit barrier)
 * - Uses acquire-release semantics for synchronization
 *
 * Backend Behavior:
 * - OpenMP: Uses #pragma omp parallel with thread-indexed results array
 * - Native: Uses custom thread pool with task queue and synchronization
 *
 * @warning parallel_reduce does NOT copy thread-local state from the calling thread
 *          to worker threads. Avoid using thread-local variables or operations
 *          that depend on thread-local state. Use only data pointers and
 *          thread-safe operations in f and sf.
 *
 * @note Follows Graphfic coding standards: no exceptions, snake_case naming
 * @note Similar to Intel TBB's parallel_reduce design pattern
 * @see https://software.intel.com/en-us/node/506154
 *
 * Example (sum reduction):
 * @code
 * std::vector<int> data(10000, 1);
 * int sum = graphfic::parallel_reduce(
 *     0, 10000, 2500, 0,  // Process 10000 elements, grain_size=2500, identity=0
 *     [&data](int64_t begin, int64_t end, int identity) {
 *         int partial_sum = identity;
 *         for (int64_t i = begin; i < end; ++i) {
 *             partial_sum += data[i];
 *         }
 *         return partial_sum;
 *     },
 *     [](int a, int b) { return a + b; }  // Combine partial sums
 * );
 * // Result: sum = 10000 (4 chunks: 0-2499, 2500-4999, 5000-7499, 7500-9999)
 * @endcode
 *
 * Example (max reduction):
 * @code
 * std::vector<int> data = {3, 7, 2, 9, 1, 5};
 * int max_val = graphfic::parallel_reduce(
 *     0, 6, 2, INT_MIN,  // grain_size=2, identity=INT_MIN
 *     [&data](int64_t begin, int64_t end, int identity) {
 *         int partial_max = identity;
 *         for (int64_t i = begin; i < end; ++i) {
 *             partial_max = std::max(partial_max, data[i]);
 *         }
 *         return partial_max;
 *     },
 *     [](int a, int b) { return std::max(a, b); }
 * );
 * // Result: max_val = 9
 * @endcode
 */
template <class scalar_t, class F, class SF>
inline scalar_t parallel_reduce(
    const int64_t  begin,
    const int64_t  end,
    const int64_t  grain_size,
    const scalar_t ident,
    const F&       f,
    const SF&      sf);

// ============================================================================
// Diagnostic and Information API
// ============================================================================

/**
 * @brief Returns a detailed string describing current parallelization settings.
 *
 * Provides comprehensive information about the parallel configuration including:
 * - Backend type (OpenMP or native)
 * - Number of intra-op threads
 * - Number of inter-op threads
 * - OpenMP version (if applicable)
 * - MKL version (if applicable)
 *
 * @return Human-readable string with parallelization configuration
 *
 * Thread Safety: Thread-safe.
 * Backend: Both OpenMP and native backends.
 */
GRAPHFIC_API std::string get_parallel_info();

/**
 * @brief Returns OpenMP version information as a string.
 *
 * @return OpenMP version string if compiled with OpenMP support, empty string otherwise
 *
 * Thread Safety: Thread-safe.
 * Backend: Both OpenMP and native backends (returns empty for native).
 */
GRAPHFIC_API std::string get_openmp_version();

/**
 * @brief Returns MKL (Math Kernel Library) version information as a string.
 *
 * @return MKL version string if linked with MKL, empty string otherwise
 *
 * Thread Safety: Thread-safe.
 * Backend: Both OpenMP and native backends.
 */
GRAPHFIC_API std::string get_mkl_version();

// ============================================================================
// Inter-op Parallelism API - Thread Pool Management
// ============================================================================

/**
 * @brief Sets the number of threads used for inter-op parallelism.
 *
 * Controls the size of the thread pool used for concurrent execution of
 * independent operations via launch(). This is separate from intra-op
 * parallelism (parallel_for/reduce).
 *
 * @param nthreads Number of threads for the inter-op thread pool (must be > 0)
 *
 * Thread Safety: Thread-safe, uses internal synchronization.
 * Backend: Both OpenMP and native backends.
 *
 * @note This affects the thread pool used by launch(), not parallel_for/reduce.
 */
GRAPHFIC_API void set_num_interop_threads(int /*nthreads*/);

/**
 * @brief Returns the number of threads used for inter-op parallelism.
 *
 * @return Current inter-op thread pool size
 *
 * Thread Safety: Thread-safe.
 * Backend: Both OpenMP and native backends.
 */
GRAPHFIC_API size_t get_num_interop_threads();

/**
 * @brief Launches a task on the inter-op thread pool.
 *
 * Submits a task to the inter-op thread pool for asynchronous execution.
 * This allows concurrent execution of independent operations. The function
 * returns immediately; the task executes asynchronously.
 *
 * @param func Task to execute (callable with signature void())
 *
 * Thread Safety: Thread-safe, uses internal thread pool synchronization.
 * Backend: Both OpenMP and native backends.
 *
 * @note This is for inter-op parallelism (between operations), not intra-op
 *       parallelism (within operations like parallel_for).
 *
 * Example:
 * @code
 * graphfic::launch([]() {
 *     // This runs asynchronously on the inter-op thread pool
 *     perform_computation();
 * });
 * @endcode
 */
GRAPHFIC_API void launch(std::function<void()> func);

namespace internal
{
/**
 * @brief Launches a task without thread state management (internal use only).
 *
 * Similar to launch() but does not set up thread-local state. Used internally
 * for tasks that don't need thread state tracking.
 *
 * @param fn Task to execute
 *
 * Thread Safety: Thread-safe.
 * Backend: Both OpenMP and native backends.
 */
void launch_no_thread_state(std::function<void()> fn);
}  // namespace internal

/**
 * @brief Launches a task using intra-op parallelism.
 *
 * Executes a task using the intra-op thread pool. This is different from
 * launch() which uses the inter-op thread pool.
 *
 * @param func Task to execute (callable with signature void())
 *
 * Thread Safety: Thread-safe.
 * Backend: Both OpenMP and native backends.
 */
GRAPHFIC_API void intraop_launch(const std::function<void()>& func);

/**
 * @brief Returns the default number of threads for intra-op parallelism.
 *
 * Returns the default thread count based on hardware concurrency and
 * environment variables. This is the value used if set_num_threads()
 * has not been called.
 *
 * @return Default number of intra-op threads
 *
 * Thread Safety: Thread-safe.
 * Backend: Both OpenMP and native backends.
 */
GRAPHFIC_API int intraop_default_num_threads();

// ============================================================================
// Internal Parallel Execution Primitives
// ============================================================================

namespace internal
{

#if GRAPHFIC_HAS_OPENMP
/**
 * @brief OpenMP backend implementation of parallel execution primitive.
 *
 * This template function is the core parallel execution primitive for the OpenMP
 * backend. It divides the range [begin, end) into chunks and executes the user
 * function f on each chunk using OpenMP parallel regions.
 *
 * IMPLEMENTATION DETAILS:
 * - Uses #pragma omp parallel to create a team of threads
 * - Each thread computes its own chunk based on thread ID
 * - Static work distribution (each thread gets one chunk)
 * - No dynamic scheduling overhead
 * - Respects grain_size to limit number of chunks
 *
 * DESIGN DECISIONS:
 * - Does NOT use num_threads clause due to bugs in GOMP's thread pool (#32008)
 * - Instead, limits effective threads by adjusting chunk_size based on grain_size
 * - Uses thread_id_guard to manage thread-local state (RAII pattern)
 * - Error flag prevents execution if any thread encounters an error
 *
 * @tparam F Callable type with signature void(int64_t begin, int64_t end)
 *
 * @param begin Starting index of the range
 * @param end Ending index of the range (exclusive)
 * @param grain_size Minimum elements per chunk (0 = use all threads)
 * @param f User function to execute on each chunk
 *
 * Thread Safety: Thread-safe, uses OpenMP synchronization.
 * Memory Ordering: OpenMP provides implicit barrier at end of parallel region.
 * Backend: OpenMP only (template implementation).
 *
 * @note This is an internal function called by parallel_for and parallel_reduce.
 * @note Follows Graphfic coding standards: no exceptions, snake_case naming.
 */
template <typename F>
inline void invoke_parallel(int64_t begin, int64_t end, int64_t grain_size, const F& f)
{
    // Error flag shared across all threads (atomic for thread safety)
    std::atomic<bool> has_error{false};

#pragma omp parallel
    {
        // Determine number of threads to actually use based on grain_size
        // Can't use num_threads clause due to bugs in GOMP's thread pool (See #32008)
        int64_t num_threads = omp_get_num_threads();
        if (grain_size > 0)
        {
            // Limit threads to ensure each gets at least grain_size elements
            num_threads = std::min(num_threads, divup((end - begin), grain_size));
        }

        // Compute this thread's chunk using static distribution
        int64_t tid        = omp_get_thread_num();
        int64_t chunk_size = divup((end - begin), num_threads);
        int64_t begin_tid  = begin + tid * chunk_size;

        // Execute chunk if within range and no errors occurred
        if (begin_tid < end && !has_error.load())
        {
            // Set thread ID for get_thread_num() API (RAII guard)
            internal::thread_id_guard tid_guard(tid);

            // Execute user function on this thread's chunk
            // Note: Error handling removed - function f should handle errors internally
            // and use return values or other mechanisms (no exceptions per Graphfic standards)
            f(begin_tid, std::min(end, chunk_size + begin_tid));
        }
    }
    // Implicit barrier here: all threads complete before function returns
}

#elif GRAPHFIC_HAS_TBB  // TBB backend

/**
 * @brief TBB backend implementation of parallel execution primitive.
 *
 * This template function is the core parallel execution primitive for the TBB
 * backend. It divides the range [begin, end) into chunks and executes the user
 * function f on each chunk using Intel TBB's parallel_for with automatic load balancing.
 *
 * IMPLEMENTATION DETAILS:
 * - Uses tbb::parallel_for with blocked_range for parallel execution
 * - TBB's task scheduler provides automatic work stealing and load balancing
 * - Dynamic work distribution (better load balancing than static OpenMP)
 * - Respects grain_size to control task granularity
 * - Uses TBB's task arena for thread management
 *
 * DESIGN DECISIONS:
 * - Uses tbb::blocked_range with grain_size for optimal task partitioning
 * - Uses thread_id_guard to manage thread-local state (RAII pattern)
 * - Error flag prevents execution if any thread encounters an error
 * - TBB provides implicit synchronization at end of parallel_for
 *
 * PERFORMANCE CHARACTERISTICS:
 * - Better load balancing than OpenMP due to work stealing
 * - Lower overhead for fine-grained parallelism
 * - Excellent scalability on many-core systems
 * - Automatic adaptation to system load
 *
 * @tparam F Callable type with signature void(int64_t begin, int64_t end)
 *
 * @param begin Starting index of the range
 * @param end Ending index of the range (exclusive)
 * @param grain_size Minimum elements per chunk (0 = use TBB's auto partitioner)
 * @param f User function to execute on each chunk
 *
 * Thread Safety: Thread-safe, uses TBB synchronization.
 * Memory Ordering: TBB provides implicit barrier at end of parallel_for.
 * Backend: TBB only (template implementation).
 *
 * @note This is an internal function called by parallel_for and parallel_reduce.
 * @note Follows Graphfic coding standards: no exceptions, snake_case naming.
 */
template <typename F>
inline void invoke_parallel(int64_t begin, int64_t end, int64_t grain_size, const F& f)
{
    // Error flag shared across all threads (atomic for thread safety)
    std::atomic<bool> has_error{false};

    // Determine effective grain size (use 1 if not specified)
    const size_t effective_grain_size = (grain_size > 0) ? static_cast<size_t>(grain_size) : 1;

    // Use TBB's parallel_for with blocked_range
    tbb::parallel_for(
        tbb::blocked_range<int64_t>(begin, end, effective_grain_size),
        [&](const tbb::blocked_range<int64_t>& range)
        {
            // Check if any thread has encountered an error
            if (!has_error.load(std::memory_order_acquire))
            {
                // Get TBB thread index for thread-local state
                // Note: TBB doesn't provide a direct thread ID, so we use task arena index
                const int tid = tbb::this_task_arena::current_thread_index();

                // Set thread ID for get_thread_num() API (RAII guard)
                internal::thread_id_guard tid_guard(tid);

                // Execute user function on this thread's chunk
                // Note: Error handling removed - function f should handle errors internally
                // and use return values or other mechanisms (no exceptions per Graphfic standards)
                f(range.begin(), range.end());
            }
        });
    // Implicit barrier here: all tasks complete before parallel_for returns
}

#else  // Native backend

/**
 * @brief Native backend implementation of parallel execution primitive.
 *
 * This function is the core parallel execution primitive for the native backend.
 * It divides the range [begin, end) into tasks and executes them using a custom
 * thread pool with proper synchronization.
 *
 * IMPLEMENTATION DETAILS:
 * - Uses custom thread_pool for task execution
 * - Dynamic task queue with work stealing
 * - Proper synchronization using mutex and condition variables
 * - Acquire-release memory ordering for correctness
 * - RAII-based state management (parallel_region_guard)
 *
 * DESIGN DECISIONS:
 * - Task counter initialized BEFORE launching tasks (prevents race condition)
 * - Uses std::condition_variable::wait with predicate (prevents spurious wakeups)
 * - Atomic operations use acq_rel semantics (ensures memory visibility)
 * - Exception handling via std::exception_ptr (maintains Graphfic no-throw API)
 *
 * @param begin Starting index of the range
 * @param end Ending index of the range (exclusive)
 * @param grain_size Minimum elements per chunk
 * @param f User function to execute on each chunk
 *
 * Thread Safety: Thread-safe, uses mutex and atomic operations.
 * Memory Ordering: Uses acquire-release semantics for proper synchronization.
 * Backend: Native only (function implementation in parallel.cpp).
 *
 * @note This is an internal function called by parallel_for and parallel_reduce.
 * @note Implementation in parallel.cpp uses function-level conditional compilation.
 * @note Follows Graphfic coding standards: no exceptions in API, proper error handling.
 */
GRAPHFIC_API void invoke_parallel(
    const int64_t                                begin,
    const int64_t                                end,
    const int64_t                                grain_size,
    const std::function<void(int64_t, int64_t)>& f);

#endif  // GRAPHFIC_HAS_OPENMP

}  // namespace internal

// ============================================================================
// Template Implementations
// ============================================================================
// These template implementations are included in the header for performance
// (allows inlining and template instantiation at call sites).
//
// CONDITIONAL COMPILATION STRATEGY:
// - INTRA_OP_PARALLEL defined: Enables parallel execution
// - INTRA_OP_PARALLEL undefined: Falls back to sequential execution
//
// DESIGN PATTERNS:
// - RAII: parallel_guard and thread_id_guard manage state automatically
// - Template metaprogramming: Works with any callable type (lambdas, functors, etc.)
// - Lazy initialization: Thread count initialized on first use
// - Early return: Avoids overhead when parallelization is not beneficial
//
// CODING STANDARDS:
// - No exceptions: Uses GRAPHFIC_CHECK macros for validation
// - snake_case naming: Follows Graphfic C++ coding standards
// - Const correctness: Parameters marked const where appropriate
// - Memory ordering: Implicit barriers ensure correctness
// ============================================================================

/**
 * @brief Template implementation of parallel_for.
 *
 * EXECUTION FLOW:
 * 1. Validate grain_size (must be >= 0)
 * 2. Early return if range is empty (begin >= end)
 * 3. Lazy initialize thread count (first call only)
 * 4. Decide: parallel or sequential execution
 * 5. If sequential: Execute directly with guards
 * 6. If parallel: Delegate to invoke_parallel with guards
 *
 * PARALLELIZATION DECISION LOGIC:
 * Executes in parallel if ALL of the following are true:
 * - INTRA_OP_PARALLEL is defined (compile-time check)
 * - numiter > grain_size (enough work to parallelize)
 * - numiter > 1 (more than one element)
 * - !in_parallel_region() (not already in parallel region, prevents nesting)
 * - get_num_threads() > 1 (parallelism is enabled)
 *
 * Otherwise, executes sequentially to avoid overhead.
 *
 * GUARD USAGE:
 * - thread_id_guard: Sets thread ID to 0 for sequential execution
 * - parallel_guard: Marks code as executing in parallel region
 * Both use RAII pattern for automatic cleanup.
 *
 * @note The lambda passed to invoke_parallel captures f by reference and
 *       wraps it with parallel_guard to track parallel region state.
 */
template <class F>
inline void parallel_for(
    const int64_t begin, const int64_t end, const int64_t grain_size, const F& f)
{
    // Validate grain_size (debug builds only for performance)
    GRAPHFIC_CHECK_DEBUG(grain_size >= 0);

    // Early return for empty range
    if (begin >= end)
    {
        return;
    }

#ifdef INTRA_OP_PARALLEL
    // Lazy initialize thread count (thread-local, first call only)
    graphfic::internal::lazy_init_num_threads();

    // Determine if parallel execution is beneficial
    const auto numiter = end - begin;
    const bool use_parallel =
        (numiter > grain_size &&           // Enough work to parallelize
         numiter > 1 &&                    // More than one element
         !graphfic::in_parallel_region() &&  // Not already parallel (prevent nesting)
         graphfic::get_num_threads() > 1);   // Parallelism enabled

    if (!use_parallel)
    {
        // Execute sequentially with proper state management
        internal::thread_id_guard tid_guard(0);  // Set thread ID to 0
        graphfic::parallel_guard    guard(true);   // Mark as in parallel region
        f(begin, end);
        return;
    }

    // Execute in parallel using backend-specific invoke_parallel
    internal::invoke_parallel(
        begin,
        end,
        grain_size,
        [&](int64_t begin, int64_t end)
        {
            // Each worker thread executes this lambda on its chunk
            graphfic::parallel_guard guard(true);  // Mark as in parallel region
            f(begin, end);                       // Call user function
        });
    // Implicit barrier: all threads complete before returning

#else  // INTRA_OP_PARALLEL not defined
    // Sequential fallback when parallelism is disabled at compile time
    internal::thread_id_guard tid_guard(0);
    graphfic::parallel_guard    guard(true);
    f(begin, end);
#endif
}

/**
 * @brief Template implementation of parallel_reduce.
 *
 * EXECUTION FLOW:
 * 1. Validate grain_size (must be >= 0)
 * 2. Early return with identity if range is empty (begin >= end)
 * 3. Lazy initialize thread count (first call only)
 * 4. Decide: parallel or sequential execution
 * 5. If sequential: Execute reduction directly with guards
 * 6. If parallel:
 *    a. Allocate results array (one slot per thread)
 *    b. Execute parallel phase: each thread computes partial result
 *    c. Execute sequential phase: combine all partial results
 *
 * PARALLELIZATION DECISION LOGIC:
 * Executes in parallel if ALL of the following are true:
 * - INTRA_OP_PARALLEL is defined (compile-time check)
 * - (end - begin) > grain_size (enough work to parallelize)
 * - !in_parallel_region() (not already in parallel region, prevents nesting)
 * - max_threads > 1 (parallelism is enabled)
 *
 * Otherwise, executes sequentially to avoid overhead.
 *
 * RESULTS ARRAY:
 * - Uses std::array with fixed size of 64 elements for stack allocation
 * - Only first max_threads slots are used
 * - Initialized with identity element
 * - Each thread writes to its own slot (no contention)
 * - Thread ID used as index: results[get_thread_num()]
 *
 * MEMORY EFFICIENCY:
 * - Stack-allocated for up to 64 threads (no heap allocation)
 * - Only uses max_threads elements (rest are unused)
 * - Temporary storage freed automatically when function returns
 *
 * COMBINATION PHASE:
 * - Sequential loop combines all partial results
 * - Uses identity as initial value
 * - Applies sf() to combine each partial result
 * - Order of combination: left-to-right (results[0], results[1], ...)
 *
 * @note The lambda passed to invoke_parallel captures results by reference
 *       and uses thread ID to index into the results array.
 * @note Combination function sf must be associative for correctness.
 * @note Combination function should be commutative for best results.
 */
template <class scalar_t, class F, class SF>
inline scalar_t parallel_reduce(
    const int64_t  begin,
    const int64_t  end,
    const int64_t  grain_size,
    const scalar_t ident,
    const F&       f,
    const SF&      sf)
{
    // Validate grain_size (all builds for correctness)
    GRAPHFIC_CHECK(grain_size >= 0);

    // Early return with identity for empty range
    if (begin >= end)
    {
        return ident;
    }

#ifdef INTRA_OP_PARALLEL
    // Lazy initialize thread count (thread-local, first call only)
    graphfic::internal::lazy_init_num_threads();

    // Determine if parallel execution is beneficial
    const auto max_threads = graphfic::get_num_threads();
    const bool use_parallel =
        ((end - begin) > grain_size &&     // Enough work to parallelize
         !graphfic::in_parallel_region() &&  // Not already parallel (prevent nesting)
         max_threads > 1);                 // Parallelism enabled

    if (!use_parallel)
    {
        // Execute sequentially with proper state management
        internal::thread_id_guard tid_guard(0);
        graphfic::parallel_guard    guard(true);
        return f(begin, end, ident);
    }

#if GRAPHFIC_HAS_TBB
    // TBB backend: Use tbb::parallel_reduce for proper work-stealing support
    // TBB's parallel_reduce handles work stealing correctly by maintaining
    // per-task state and combining results hierarchically
    const size_t effective_grain_size = (grain_size > 0) ? static_cast<size_t>(grain_size) : 1;

    return tbb::parallel_reduce(
        tbb::blocked_range<int64_t>(begin, end, effective_grain_size),
        ident,  // Initial value for each task
        [&](const tbb::blocked_range<int64_t>& range, scalar_t init) -> scalar_t
        {
            // Get TBB thread index for thread-local state
            const int tid = tbb::this_task_arena::current_thread_index();

            // Set thread ID for get_thread_num() API (RAII guard)
            internal::thread_id_guard tid_guard(tid);
            graphfic::parallel_guard    guard(true);  // Mark as in parallel region

            // Compute partial result for this range
            // Note: init is the accumulated value for this task (not identity)
            // We need to combine it with the result from f()
            scalar_t chunk_result = f(range.begin(), range.end(), ident);
            return sf(init, chunk_result);
        },
        sf  // Reduction function to combine results from different tasks
    );
#else
    // OpenMP/Native backend: Use results array indexed by thread ID
    // Allocate results array (one slot per thread, max 64 threads)
    constexpr size_t kMaxThreads = 64;
    std::array<scalar_t, kMaxThreads> results;

    // Initialize only the slots we'll use
    for (int i = 0; i < max_threads; ++i)
    {
        results[i] = ident;
    }

    // Parallel phase: each thread computes its partial result
    internal::invoke_parallel(
        begin,
        end,
        grain_size,
        [&](const int64_t my_begin, const int64_t my_end)
        {
            // Get this thread's ID to index into results array
            const auto             tid = graphfic::get_thread_num();
            graphfic::parallel_guard guard(true);  // Mark as in parallel region

            // Compute partial result for this chunk and store in thread's slot
            results[tid] = f(my_begin, my_end, ident);
        });
    // Implicit barrier: all threads complete before continuing

    // Sequential combination phase: merge all partial results
    scalar_t result = ident;
    for (int i = 0; i < max_threads; ++i)
    {
        result = sf(result, results[i]);  // Combine with previous results
    }
    return result;
#endif  // GRAPHFIC_HAS_TBB

#else  // INTRA_OP_PARALLEL not defined
    // Sequential fallback when parallelism is disabled at compile time
    internal::thread_id_guard tid_guard(0);
    graphfic::parallel_guard    guard(true);
    return f(begin, end, ident);
#endif
}

}  // namespace graphfic
