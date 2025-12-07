/*
 * This code is inspired by PyTorch's parallel implementation.
 */

/**
 * @file parallel.cpp
 * @brief Implementation of Graphfic Parallel Execution Framework
 *
 * This file contains the implementation of the parallel execution framework for Graphfic.
 * It provides both OpenMP and native backend implementations selected at compile time
 * via the GRAPHFIC_HAS_OPENMP flag.
 *
 * ARCHITECTURE:
 * =============
 * The implementation uses function-level conditional compilation to support three backends:
 *
 * 1. OpenMP Backend (GRAPHFIC_HAS_OPENMP=1):
 *    - Uses OpenMP pragmas for parallel execution
 *    - Template implementation of invoke_parallel in header (parallel.h)
 *    - Leverages OpenMP runtime for thread management
 *    - Provides omp_get_num_threads(), omp_set_num_threads(), etc.
 *
 * 2. TBB Backend (GRAPHFIC_HAS_TBB=1 and GRAPHFIC_HAS_OPENMP=0):
 *    - Uses Intel TBB (Threading Building Blocks) for parallel execution
 *    - Template implementation of invoke_parallel in header (parallel.h)
 *    - Leverages TBB's task scheduler for work stealing and load balancing
 *    - Provides TBB-based thread management with task arena
 *
 * 3. Native Backend (GRAPHFIC_HAS_OPENMP=0 and GRAPHFIC_HAS_TBB=0):
 *    - Uses custom thread pool implementation
 *    - Function implementation of invoke_parallel in this file
 *    - Manual thread management via thread_pool class
 *    - Emulates OpenMP API using thread-local storage
 *
 * CONDITIONAL COMPILATION STRATEGY:
 * ==================================
 * This file uses FUNCTION-LEVEL conditional compilation:
 * - Each function contains #if GRAPHFIC_HAS_OPENMP / #elif GRAPHFIC_HAS_TBB / #else / #endif blocks
 * - All three backends are in the same file for easier maintenance
 * - Clear separation between backend-specific code
 * - Shared utility functions (get_env_var, get_env_num_threads) are backend-agnostic
 * - Backend priority: OpenMP > TBB > Native (first available backend is used)
 *
 * CONSOLIDATION HISTORY:
 * ======================
 * This file was created by consolidating:
 * - parallel/openmp/parallel_openmp.cpp (OpenMP implementation)
 * - parallel/native/parallel_native.cpp (Native implementation)
 *
 * The consolidation provides:
 * - Single source file for all parallel implementations
 * - Easier maintenance and code review
 * - Reduced code duplication
 * - Clear backend comparison
 *
 * THREAD SAFETY:
 * ==============
 * - All public API functions are thread-safe
 * - Thread-local storage used for per-thread state (thread ID, parallel region flag)
 * - Atomic operations used for shared counters
 * - Mutex protection for thread pool access
 * - Condition variables for synchronization
 *
 * MEMORY ORDERING:
 * ================
 * - Native backend uses acquire-release semantics for task synchronization
 * - Atomic operations use appropriate memory ordering (relaxed, acquire, release, acq_rel)
 * - OpenMP backend relies on OpenMP's implicit barriers
 *
 * RACE CONDITION FIX:
 * ===================
 * The native invoke_parallel implementation was fixed to prevent a race condition:
 * - BEFORE: state.remaining was set AFTER launching tasks (race condition)
 * - AFTER: state.remaining is initialized BEFORE launching tasks (correct)
 * - Uses std::memory_order_release for initialization
 * - Uses std::memory_order_acq_rel for decrement
 * - Uses predicate with std::memory_order_acquire for wait
 *
 * CODING STANDARDS:
 * =================
 * - Follows Graphfic C++ coding standards
 * - snake_case naming convention
 * - No exceptions in public API (uses return values for errors)
 * - RAII for resource management
 * - Proper const correctness
 * - Comprehensive error handling
 *
 * ENVIRONMENT VARIABLES:
 * ======================
 * The implementation respects these environment variables:
 * - OMP_NUM_THREADS: Sets number of intra-op threads (OpenMP standard)
 * - GRAPHFIC_NUM_THREADS: Graphfic-specific thread count override
 * - GRAPHFIC_NUM_INTEROP_THREADS: Sets number of inter-op threads
 *
 * DEPENDENCIES:
 * =============
 * - parallel.h: API declarations and template implementations
 * - thread_pool.h: Native backend thread pool
 * - parallel_guard.h: RAII guard for parallel region state
 * - OpenMP library (if GRAPHFIC_HAS_OPENMP=1)
 * - MKL library (if GRAPHFIC_HAS_MKL=1)
 */

#include "parallel.h"

#include <atomic>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>

#include "logging/logger.h"
#include "parallel/thread_pool.h"
#include "util/env.h"

#if GRAPHFIC_HAS_MKL
#include <mkl.h>
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

#if GRAPHFIC_HAS_TBB
// Disable TBB implicit linkage on MSVC to avoid linker issues
#ifdef _MSC_VER
#pragma push_macro("__TBB_NO_IMPLICIT_LINKAGE")
#define __TBB_NO_IMPLICIT_LINKAGE 1
#endif

#include <tbb/global_control.h>
#include <tbb/task_arena.h>

#ifdef _MSC_VER
#pragma pop_macro("__TBB_NO_IMPLICIT_LINKAGE")
#endif
#endif

#if defined(__APPLE__) && defined(__aarch64__)
#include <sys/sysctl.h>
#endif

namespace graphfic
{
namespace
{
std::string get_env_var(const char* var_name, const char* def_value = nullptr)
{
    auto env = graphfic::utils::get_env(var_name);
    return env.has_value() ? env.value() : def_value;
}

size_t get_env_num_threads(const char* var_name, size_t def_value = 0)
{
    try
    {
        if (auto value = graphfic::utils::get_env(var_name))
        {
            int nthreads = std::stoi(value.value());
            if (nthreads <= 0)
            {
                GRAPHFIC_LOG_WARNING("Invalid thread count: {}", nthreads);
                return def_value;
            }
            return nthreads;
        }
    }
    catch (const std::exception& e)
    {
        std::ostringstream oss;
        oss << "Invalid " << var_name << " variable value, " << e.what();
        GRAPHFIC_LOG_WARNING("{}", oss.str());
    }
    return def_value;
}

}  // namespace

std::string get_openmp_version()
{
#ifdef _OPENMP
    std::ostringstream ss;
    ss << "OpenMP " << _OPENMP;
    return ss.str();
#else
    return "OpenMP not available";
#endif
}

std::string get_mkl_version()
{
#if GRAPHFIC_HAS_MKL
    std::ostringstream ss;
    ss << "MKL Version";
    return ss.str();
#else
    return "MKL not available";
#endif
}

std::string get_parallel_info()
{
    std::ostringstream ss;

    ss << "Graphfic/Parallel:\n\tat::get_num_threads() : " << graphfic::get_num_threads() << '\n';
    ss << "\tgraphfic::get_num_interop_threads() : " << graphfic::get_num_interop_threads() << '\n';

#ifdef _OPENMP
    ss << graphfic::get_openmp_version() << '\n';
    ss << "\tomp_get_max_threads() : " << omp_get_max_threads() << '\n';
#endif

#if GRAPHFIC_HAS_MKL
    ss << "\tmkl_get_max_threads() : " << mkl_get_max_threads() << '\n';
#endif
    ss << "std::thread::hardware_concurrency() : " << std::thread::hardware_concurrency() << '\n';

    ss << "Environment variables:" << '\n';
    ss << "\tOMP_NUM_THREADS : " << get_env_var("OMP_NUM_THREADS", "[not set]") << '\n';
#if defined(__x86_64__) || defined(_M_X64)
    ss << graphfic::get_mkl_version() << '\n';
    ss << "\tMKL_NUM_THREADS : " << get_env_var("MKL_NUM_THREADS", "[not set]") << '\n';
#endif

    ss << "Graphfic parallel backend: ";
#if GRAPHFIC_HAS_OPENMP
    ss << "OpenMP";
#elif GRAPHFIC_HAS_TBB
    ss << "Intel TBB (Threading Building Blocks)";
#else
    ss << "native thread pool";
#endif
    ss << '\n';

#if GRAPHFIC_HAS_EXPERIMENTAL
    ss << "Experimental: single thread pool" << std::endl;
#endif

    return ss.str();
}

int intraop_default_num_threads()
{
    size_t nthreads = get_env_num_threads("OMP_NUM_THREADS", 0);
    nthreads        = get_env_num_threads("MKL_NUM_THREADS", nthreads);
    if (nthreads == 0)
    {
#if defined(__aarch64__) && defined(__APPLE__)
        // On Apple Silicon there are efficient and performance core
        // Restrict parallel algorithms to performance cores by default
        int32_t num_cores     = -1;
        size_t  num_cores_len = sizeof(num_cores);
        if (sysctlbyname("hw.perflevel0.physicalcpu", &num_cores, &num_cores_len, nullptr, 0) == 0)
        {
            if (num_cores > 1)
            {
                nthreads = num_cores;
                return num_cores;
            }
        }
#endif
        nthreads = graphfic::task_thread_pool_base::default_num_threads();
    }
    return static_cast<int>(nthreads);
}

// ============================================================================
// Parallel Implementation Functions
// ============================================================================
// The following functions have different implementations for OpenMP and native
// thread pool backends, selected via conditional compilation at the function level.

// ============================================================================
// Shared State Variables
// ============================================================================
// These variables are used by OpenMP, TBB, and native implementations

namespace
{
#if GRAPHFIC_HAS_OPENMP
// OpenMP backend state
// Number of threads set by the user
std::atomic<int> num_threads{-1};
thread_local int this_thread_id{0};

#elif GRAPHFIC_HAS_TBB
// TBB backend state
// Number of threads set by the user
std::atomic<int> num_threads{-1};
thread_local int this_thread_id{0};

// TBB global control for thread count management
// This is a unique_ptr to allow lazy initialization
std::unique_ptr<tbb::global_control> tbb_thread_control;

// TBB task arena for thread management
// This is a unique_ptr to allow lazy initialization
std::unique_ptr<tbb::task_arena> tbb_arena;

#else
// Native backend state
// used with _set_in_parallel_region to mark master thread
// as in parallel region while executing parallel primitives
thread_local bool in_parallel_region_ = false;

// thread number (task_id) set by parallel primitive
thread_local int thread_num_ = 0;

void _set_in_parallel_region(bool in_region)
{
    in_parallel_region_ = in_region;
}

void _unset_thread_num()
{
    thread_num_ = 0;
}

const int NOT_SET  = -1;
const int CONSUMED = -2;

// Number of threads set by the user
// NOT_SET -> positive value -> CONSUMED
// or
// NOT_SET -> CONSUMED
// Meaning:
//  - NOT_SET - pool not initialized, user value is not set
//  - positive value - pool not initialized, user value set
//  - CONSUMED - pool is initialized
std::atomic<int> num_intraop_threads{NOT_SET};

// Helper functions for native backend (must be defined before use)
int _num_pool_threads(int nthreads)
{
    if (nthreads == NOT_SET)
    {
        nthreads = intraop_default_num_threads();
    }
    else
    {
        if (nthreads <= 0)
        {
            GRAPHFIC_LOG_ERROR("Invalid number of threads: {}", nthreads);
            nthreads = 1;
        }
    }
    // minus one because of the master thread
    return nthreads - 1;
}

graphfic::task_thread_pool_base& _get_intraop_pool()
{
    static std::shared_ptr<graphfic::task_thread_pool_base> const pool = ThreadPoolRegistry()->run(
        "XSIGMA",
        /* device_id */ 0,
        /* pool_size */ _num_pool_threads(num_intraop_threads.exchange(CONSUMED)),
        /* create_new */ true);  // create a separate thread pool for intra-op
    return *pool;
}

// `fn` will be called with params: task_id.
void _run_with_pool(const std::function<void(size_t)>& fn, size_t range)
{
    for (size_t i = 1; i < range; ++i)
    {
        _get_intraop_pool().run([fn, i]() { fn(i); });
    }
    // Run the first task on the current thread directly.
    fn(0);
}
#endif

}  // namespace

// ============================================================================
// Core Parallel Functions
// ============================================================================
// Each function contains conditional compilation to select between OpenMP
// and native thread pool implementations

void init_num_threads()
{
#if GRAPHFIC_HAS_OPENMP
    // OpenMP implementation
    auto nthreads = num_threads.load();
    if (nthreads > 0)
    {
        set_num_threads(nthreads);
    }
    else
    {
#if defined(_OPENMP) && GRAPHFIC_HAS_MKL && !GRAPHFIC_MKL_SEQUENTIAL
        // If we are using MKL an OpenMP make sure the number of threads match.
        // Otherwise, MKL and our OpenMP-enabled functions will keep changing the
        // size of the OpenMP thread pool, resulting in worse performance (and memory
        // leaks in GCC 5.4)
        omp_set_num_threads(mkl_get_max_threads());
#elif defined(_OPENMP)
        omp_set_num_threads(intraop_default_num_threads());
#endif
    }

#elif GRAPHFIC_HAS_TBB
    // TBB implementation
    auto nthreads = num_threads.load();
    if (nthreads > 0)
    {
        set_num_threads(nthreads);
    }
    else
    {
        // Use default number of threads for TBB
        const int default_threads = intraop_default_num_threads();
        set_num_threads(default_threads);
    }

#else
    // Native implementation
#ifdef _OPENMP
    omp_set_num_threads(1);
#endif

#if GRAPHFIC_HAS_MKL
    mkl_set_num_threads(1);
#endif
#endif
}

void set_num_threads(int nthreads)
{
    // Validate input (common to all implementations)
    if (nthreads <= 0)
    {
        GRAPHFIC_LOG_ERROR("Expected positive number of threads, got {}", nthreads);
        return;
    }

#if GRAPHFIC_HAS_OPENMP
    // OpenMP implementation
    num_threads.store(nthreads);
#ifdef _OPENMP
    omp_set_num_threads(nthreads);
#endif
#if GRAPHFIC_HAS_MKL
    mkl_set_num_threads_local(nthreads);
    mkl_set_dynamic(false);
#endif

#elif GRAPHFIC_HAS_TBB
    // TBB implementation
    num_threads.store(nthreads);

    // Initialize TBB global control to limit thread count
    // Note: TBB uses global_control to set the maximum number of threads
    tbb_thread_control = std::make_unique<tbb::global_control>(
        tbb::global_control::max_allowed_parallelism, nthreads);

    // Initialize TBB task arena with the specified number of threads
    tbb_arena = std::make_unique<tbb::task_arena>(nthreads);

#if GRAPHFIC_HAS_MKL
    mkl_set_num_threads_local(nthreads);
    mkl_set_dynamic(false);
#endif

#else
    // Native implementation
    int no_value = NOT_SET;
    if (!num_intraop_threads.compare_exchange_strong(no_value, nthreads))
    {
        // num_intraop_threads either stores a positive integer or CONSUMED,
        // check that requested size is the same as the current one
        int stored_nthreads = num_intraop_threads.load();
        if (stored_nthreads <= 0)
        {
            // plus one because of master thread
            stored_nthreads = static_cast<int>(_get_intraop_pool().size() + 1);
        }
        if (stored_nthreads != nthreads)
        {
            GRAPHFIC_LOG_WARNING(
                "Cannot set number of intraop threads "
                "after parallel work has started or after set_num_threads call "
                "when using native parallel backend");
        }
    }
#endif
}

int get_num_threads()
{
#if GRAPHFIC_HAS_OPENMP
    // OpenMP implementation
    // Explicitly calling omp_get_max_threads() as the size of the parallel
    // region might be different in the new thread;
    // Use init_num_threads() during thread initialization to ensure
    // consistent size of parallel region in different threads
#ifdef _OPENMP
    graphfic::internal::lazy_init_num_threads();
    return omp_get_max_threads();
#else
    return 1;
#endif

#elif GRAPHFIC_HAS_TBB
    // TBB implementation
    graphfic::internal::lazy_init_num_threads();
    auto nthreads = num_threads.load();
    if (nthreads > 0)
    {
        return nthreads;
    }
    // Return default if not set
    return intraop_default_num_threads();

#else
    // Native implementation
    graphfic::internal::lazy_init_num_threads();
    // not initializing pool unnecessarily,
    // because pool cannot be resized after initialization
    int nthreads = num_intraop_threads.load();
    if (nthreads > 0)
    {
        return nthreads;
    }
    if (nthreads == NOT_SET)
    {
        return intraop_default_num_threads();
    }

    if (nthreads != CONSUMED)
    {
        GRAPHFIC_LOG_ERROR("Unexpected thread count state: {}", nthreads);
    }
    return static_cast<int>(_get_intraop_pool().size() + 1);
#endif
}

int get_thread_num()
{
#if GRAPHFIC_HAS_OPENMP
    // OpenMP implementation
    return this_thread_id;
#elif GRAPHFIC_HAS_TBB
    // TBB implementation
    return this_thread_id;
#else
    // Native implementation
    return thread_num_;
#endif
}

namespace internal
{
void set_thread_num(int id)
{
#if GRAPHFIC_HAS_OPENMP
    // OpenMP implementation
    this_thread_id = id;
#elif GRAPHFIC_HAS_TBB
    // TBB implementation
    this_thread_id = id;
#else
    // Native implementation
    thread_num_ = id;
#endif
}
}  // namespace internal

bool in_parallel_region()
{
#if GRAPHFIC_HAS_OPENMP
    // OpenMP implementation
#ifdef _OPENMP
    return omp_in_parallel();
#else
    return false;
#endif

#elif GRAPHFIC_HAS_TBB
    // TBB implementation
    // TBB doesn't have a direct equivalent to omp_in_parallel()
    // We use the parallel_guard to track parallel region state
    return graphfic::parallel_guard::is_enabled();

#else
    // Native implementation
    return in_parallel_region_ || (num_intraop_threads.load() == CONSUMED &&
                                   // Needed as intraop_launch() doesn't set in_parallel_region().
                                   _get_intraop_pool().in_thread_pool());
#endif
}

void intraop_launch(const std::function<void()>& func)
{
#if GRAPHFIC_HAS_OPENMP
    // OpenMP implementation - execute inline
    func();

#elif GRAPHFIC_HAS_TBB
    // TBB implementation - execute inline
    // TBB handles task scheduling internally
    func();

#else
    // Native implementation - use thread pool if not in parallel region
    if (!in_parallel_region() && get_num_threads() > 1)
    {
        _get_intraop_pool().run(func);
    }
    else
    {
        // execute inline if we're in parallel region
        func();
    }
#endif
}

// ============================================================================
// Native Backend Helper Functions
// ============================================================================
// These functions are only used by the native backend implementation
// Note: _num_pool_threads, _get_intraop_pool, and _run_with_pool are now
// defined earlier in the file (in the namespace block) to avoid forward
// declaration issues.

#if !GRAPHFIC_HAS_OPENMP && !GRAPHFIC_HAS_TBB

namespace
{

// RAII guard helps to support in_parallel_region() and get_thread_num() API.
struct parallel_region_guard
{
    parallel_region_guard(int task_id)
    {
        internal::set_thread_num(task_id);
        _set_in_parallel_region(true);
    }
    parallel_region_guard(const parallel_region_guard&)            = delete;
    parallel_region_guard(parallel_region_guard&&)                 = delete;
    parallel_region_guard& operator=(const parallel_region_guard&) = delete;
    parallel_region_guard& operator=(parallel_region_guard&&)      = delete;

    ~parallel_region_guard()
    {
        _set_in_parallel_region(false);
        _unset_thread_num();
    }
};

}  // namespace

namespace internal
{

static std::tuple<size_t, size_t> calc_num_tasks_and_chunk_size(
    int64_t begin, int64_t end, int64_t grain_size)
{
    if ((end - begin) < grain_size)
    {
        return std::make_tuple(1, std::max((int64_t)0, end - begin));
    }
    // Choose number of tasks based on grain size and number of threads.
    int64_t chunk_size = divup((end - begin), get_num_threads());
    // Make sure each task is xsigma least grain_size size.
    chunk_size           = std::max(grain_size, chunk_size);
    auto const num_tasks = static_cast<size_t>(divup((end - begin), chunk_size));
    return std::make_tuple(num_tasks, chunk_size);
}

void invoke_parallel(
    const int64_t                                begin,
    const int64_t                                end,
    const int64_t                                grain_size,
    const std::function<void(int64_t, int64_t)>& f)
{
    graphfic::internal::lazy_init_num_threads();

    size_t num_tasks  = 0;
    size_t chunk_size = 0;
    std::tie(num_tasks, chunk_size) =
        internal::calc_num_tasks_and_chunk_size(begin, end, grain_size);

    // Synchronization state for parallel execution
    // IMPORTANT: remaining must be initialized to num_tasks BEFORE launching tasks
    // to avoid race condition where tasks complete before counter is set
    struct
    {
        std::atomic_flag        err_flag = ATOMIC_FLAG_INIT;
        std::exception_ptr      eptr;
        std::mutex              mutex;
        std::atomic_size_t      remaining;
        std::condition_variable cv;
    } state;

    // Initialize remaining counter BEFORE launching any tasks to prevent race condition
    state.remaining.store(num_tasks, std::memory_order_release);

    auto task = [f, &state, begin, end, chunk_size](size_t task_id)
    {
        auto const local_start = static_cast<int64_t>(begin + (task_id * chunk_size));
        if (local_start < end)
        {
            int64_t const local_end = std::min(end, static_cast<int64_t>(chunk_size + local_start));
            try
            {
                parallel_region_guard const guard(static_cast<int>(task_id));
                f(local_start, local_end);
            }
            catch (...)
            {
                if (!state.err_flag.test_and_set())
                {
                    state.eptr = std::current_exception();
                }
            }
        }
        // Decrement remaining counter with proper synchronization
        // Use acquire-release semantics to ensure all task work is visible
        {
            std::unique_lock<std::mutex> const lk(state.mutex);
            if (state.remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                // This was the last task to complete
                state.cv.notify_one();
            }
        }
    };

    // Launch all tasks - counter is already initialized above
    _run_with_pool(std::move(task), num_tasks);

    // Wait for all tasks to finish
    // Use acquire semantics to ensure we see all worker thread writes
    {
        std::unique_lock<std::mutex> lk(state.mutex);
        state.cv.wait(
            lk, [&state]() { return state.remaining.load(std::memory_order_acquire) == 0; });
    }

    if (state.eptr)
    {
        std::rethrow_exception(state.eptr);
    }
}

}  // namespace internal

#endif  // !GRAPHFIC_HAS_OPENMP

// ============================================================================
// Inter-op Thread Pool Functions
// ============================================================================
// These functions manage the inter-op thread pool for both OpenMP and native
// backends. The native backend uses a separate thread pool for inter-op tasks.

#if !GRAPHFIC_HAS_OPENMP && !GRAPHFIC_HAS_TBB
// Native backend inter-op thread pool state and helper functions
namespace
{
// Number of inter-op threads set by the user;
// NOT_SET -> positive value -> CONSUMED
// (CONSUMED - thread pool is initialized)
// or
// NOT_SET -> CONSUMED
std::atomic<int> num_interop_threads{NOT_SET};

// thread pool global instance is hidden,
// users should use graphfic::launch and get/set_num_interop_threads interface
graphfic::task_thread_pool_base& get_interop_pool()
{
    static std::shared_ptr<graphfic::task_thread_pool_base> const pool = ThreadPoolRegistry()->run(
        "XSIGMA",
        /* device_id */ 0,
        /* pool_size */ num_interop_threads.exchange(CONSUMED),
        /* create_new */ true);
    return *pool;
}

// Factory function for ThreadPoolRegistry
std::shared_ptr<graphfic::task_thread_pool_base> create_xsigma_threadpool(
    int device_id, int pool_size, bool create_new)
{
    // For now, the only accepted device id is 0
    if (device_id != 0)
    {
        GRAPHFIC_LOG_ERROR("Invalid device_id: {}, expected 0", device_id);
        return nullptr;
    }
    // Create new thread pool
    if (!create_new)
    {
        GRAPHFIC_LOG_ERROR("create_new must be true");
        return nullptr;
    }
    return std::make_shared<graphfic::thread_pool>(pool_size);
}

}  // namespace

GRAPHFIC_REGISTER_CREATOR(ThreadPoolRegistry, XSIGMA, create_xsigma_threadpool)
#endif  // !GRAPHFIC_HAS_OPENMP && !GRAPHFIC_HAS_TBB

void set_num_interop_threads(int nthreads)
{
#if GRAPHFIC_HAS_OPENMP
    // OpenMP backend doesn't use a separate inter-op thread pool
    // This is a no-op for compatibility
    (void)nthreads;  // Suppress unused parameter warning
#elif GRAPHFIC_HAS_TBB
    // TBB backend doesn't use a separate inter-op thread pool
    // This is a no-op for compatibility
    (void)nthreads;  // Suppress unused parameter warning
#else
    // Native backend implementation
    if (nthreads <= 0)
    {
        GRAPHFIC_LOG_ERROR("Expected positive number of threads, got {}", nthreads);
        return;
    }

    int no_value = NOT_SET;
    if (!num_interop_threads.compare_exchange_strong(no_value, nthreads))
    {
        GRAPHFIC_LOG_ERROR(
            "Error: cannot set number of interop threads after parallel work "
            "has started or set_num_interop_threads called");
    }
#endif
}

size_t get_num_interop_threads()
{
#if GRAPHFIC_HAS_OPENMP
    // OpenMP backend - inter-op threads are the same as intra-op threads
    return static_cast<size_t>(get_num_threads());
#elif GRAPHFIC_HAS_TBB
    // TBB backend - inter-op threads are the same as intra-op threads
    return static_cast<size_t>(get_num_threads());
#else
    // Native backend implementation
    graphfic::internal::lazy_init_num_threads();
    int const nthreads = num_interop_threads.load();
    if (nthreads > 0)
    {
        return nthreads;
    }
    if (nthreads == NOT_SET)
    {
        // return default value
        return graphfic::task_thread_pool_base::default_num_threads();
    }

    return get_interop_pool().size();
#endif
}

namespace internal
{
void launch_no_thread_state(std::function<void()> fn)
{
#if GRAPHFIC_HAS_OPENMP
    // OpenMP backend - execute inline
    fn();
#elif GRAPHFIC_HAS_TBB
    // TBB backend - execute inline
    fn();
#else
    // Native backend - use thread pool
#if GRAPHFIC_HAS_EXPERIMENTAL
    intraop_launch(std::move(fn));
#else
    get_interop_pool().run(std::move(fn));
#endif
#endif
}
}  // namespace internal

void launch(std::function<void()> func)
{
    // Both backends use the same implementation
    internal::launch_no_thread_state(std::move(func));
}

}  // namespace graphfic
