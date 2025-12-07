/*
 * This code is inspired by PyTorch's parallel implementation.
 */

#pragma once

/**
 * @file thread_pool.h
 * @brief Thread Pool Implementation for Graphfic Parallel Execution
 *
 * This file provides thread pool implementations used by the Graphfic parallel module
 * for both intra-op and inter-op parallelism. The thread pool is used by the native
 * backend (when OpenMP is not available) to execute parallel tasks.
 *
 * ARCHITECTURE:
 * =============
 * The thread pool architecture consists of three main classes:
 *
 * 1. task_thread_pool_base (Abstract Base Class):
 *    - Defines the interface for all thread pools
 *    - Provides virtual methods for task submission and queries
 *    - Used for polymorphic thread pool management
 *
 * 2. thread_pool (Concrete Implementation):
 *    - Implements the core thread pool functionality
 *    - Manages worker threads, task queue, and synchronization
 *    - Uses mutex and condition variables for thread coordination
 *    - Supports both simple tasks and tasks with thread ID parameter
 *
 * 3. task_thread_pool (Specialized Thread Pool):
 *    - Extends thread_pool with NUMA awareness and thread naming
 *    - Sets thread names for debugging ("XsigmaTaskThread")
 *    - Optionally binds threads to NUMA nodes for performance
 *
 * DESIGN PATTERNS:
 * ================
 * - Producer-Consumer: Main thread produces tasks, worker threads consume
 * - RAII: Thread pool manages thread lifetime automatically
 * - Registry Pattern: ThreadPoolRegistry manages pool instances
 * - Virtual Interface: Polymorphic access via task_thread_pool_base
 *
 * SYNCHRONIZATION:
 * ================
 * - Task Queue: Protected by mutex, accessed via condition variable
 * - Completion Tracking: Uses atomic counter and condition variable
 * - Thread Safety: All public methods are thread-safe
 * - Memory Ordering: Uses standard mutex/condition_variable semantics
 *
 * THREAD LIFECYCLE:
 * =================
 * 1. Construction: Creates worker threads, starts main_loop
 * 2. Task Submission: Tasks added to queue, condition variable signaled
 * 3. Task Execution: Worker threads dequeue and execute tasks
 * 4. Completion Wait: wait_work_complete() blocks until queue is empty
 * 5. Destruction: Sets running_ = false, joins all worker threads
 *
 * CODING STANDARDS:
 * =================
 * - Follows Graphfic C++ coding standards
 * - snake_case naming convention
 * - RAII for resource management (threads, mutexes)
 * - No exceptions in destructors
 * - Proper use of const and noexcept
 *
 * USAGE:
 * ======
 * The thread pool is typically accessed via ThreadPoolRegistry, not directly:
 * @code
 * auto pool = ThreadPoolRegistry()->run("MyPool", device_id, pool_size, true);
 * pool->run([]() { do_work(); });
 * @endcode
 *
 * For the native parallel backend, the thread pool is used internally by
 * invoke_parallel() in parallel.cpp.
 */

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "common/export.h"
#include "memory/numa.h"
#include "parallel/thread_name.h"
#include "util/registry.h"

namespace graphfic
{
/**
 * @brief Abstract base class for thread pool implementations.
 *
 * Defines the interface that all thread pool implementations must provide.
 * This allows polymorphic access to different thread pool types through a
 * common interface.
 *
 * Thread Safety: All methods must be thread-safe in derived classes.
 * Design Pattern: Abstract base class with virtual interface.
 * Coding Standard: Follows Graphfic C++ standards (snake_case, virtual destructor).
 */
class GRAPHFIC_VISIBILITY task_thread_pool_base
{
public:
    /**
     * @brief Submits a task to the thread pool for asynchronous execution.
     *
     * The task is added to the thread pool's queue and will be executed by an
     * available worker thread. This method returns immediately; the task executes
     * asynchronously.
     *
     * @param func Task to execute (callable with signature void())
     *
     * Thread Safety: Must be thread-safe (implementation-dependent).
     */
    virtual void run(std::function<void()> func) = 0;

    /**
     * @brief Returns the total number of worker threads in the pool.
     *
     * @return Total number of threads (does not include the main thread)
     *
     * Thread Safety: Must be thread-safe (implementation-dependent).
     */
    virtual size_t size() const = 0;

    /**
     * @brief Returns the number of idle (available) threads in the pool.
     *
     * An available thread is one that is not currently executing a task.
     * This can be used to determine if the pool has capacity for more work.
     *
     * @return Number of idle threads
     *
     * Thread Safety: Must be thread-safe (implementation-dependent).
     */
    virtual size_t num_available() const = 0;

    /**
     * @brief Checks if the current thread is a worker thread from this pool.
     *
     * @return true if the calling thread is a worker thread, false otherwise
     *
     * Thread Safety: Must be thread-safe (implementation-dependent).
     */
    virtual bool in_thread_pool() const = 0;

    /**
     * @brief Virtual destructor for proper cleanup of derived classes.
     *
     * @note noexcept ensures destructor never throws (required for RAII).
     */
    virtual ~task_thread_pool_base() noexcept = default;

    /**
     * @brief Returns the default number of threads for a thread pool.
     *
     * Determines the default thread count based on hardware concurrency
     * and system configuration. This is typically used when the user does
     * not specify an explicit thread count.
     *
     * @return Default number of threads (usually hardware_concurrency - 1)
     *
     * Thread Safety: Thread-safe.
     */
    static GRAPHFIC_API size_t default_num_threads();
};

/**
     * @brief Concrete thread pool implementation with task queue and worker threads.
     *
     * This class implements a producer-consumer thread pool where the main thread
     * submits tasks to a queue and worker threads dequeue and execute them. It
     * provides both simple task execution and task execution with thread ID parameter.
     *
     * ARCHITECTURE:
     * - Task Queue: FIFO queue protected by mutex
     * - Worker Threads: Created at construction, run until destruction
     * - Synchronization: Mutex + condition variables for coordination
     * - Completion Tracking: Atomic counter + condition variable
     *
     * THREAD LIFECYCLE:
     * 1. Construction: Creates pool_size worker threads
     * 2. Each thread runs main_loop() waiting for tasks
     * 3. Tasks submitted via run() or run_task_with_id()
     * 4. Worker threads dequeue and execute tasks
     * 5. Destruction: Sets running_=false, joins all threads
     *
     * SYNCHRONIZATION PRIMITIVES:
     * - mutex_: Protects task queue and state variables
     * - condition_: Signals worker threads when tasks are available
     * - completed_: Signals main thread when all tasks complete
     * - running_: Atomic flag for shutdown coordination
     *
     * Thread Safety: All public methods are thread-safe.
     * Design Pattern: Producer-Consumer with RAII thread management.
     * Coding Standard: Follows Graphfic C++ standards (snake_case, RAII).
     */
class GRAPHFIC_VISIBILITY thread_pool : public graphfic::task_thread_pool_base
{
protected:
    /**
         * @brief Task element that can hold either a simple task or a task with thread ID.
         *
         * This union-like structure allows the task queue to hold two types of tasks:
         * 1. Simple tasks: void() - no parameters
         * 2. Tasks with ID: void(size_t) - receives thread ID as parameter
         *
         * The run_with_id flag determines which function pointer is valid.
         */
    struct task_element_t
    {
        bool run_with_id;  ///< true if with_id is valid, false if no_id is valid

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
        const std::function<void()> no_id;  ///< Simple task (no parameters)

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
        const std::function<void(std::size_t)> with_id;  ///< Task with thread ID parameter

        /**
             * @brief Constructs task element for simple task (no ID).
             * @param f Task function with signature void()
             */
        explicit task_element_t(std::function<void()> f)
            : run_with_id(false), no_id(std::move(f)), with_id(nullptr)
        {
        }

        /**
             * @brief Constructs task element for task with thread ID.
             * @param f Task function with signature void(size_t)
             */
        explicit task_element_t(std::function<void(std::size_t)> f)
            : run_with_id(true), no_id(nullptr), with_id(std::move(f))
        {
        }
    };

    // Member variables (protected for derived class access)
    std::queue<task_element_t> tasks_;      ///< FIFO task queue (protected by mutex_)
    std::vector<std::thread>   threads_;    ///< Worker threads (managed via RAII)
    mutable std::mutex         mutex_;      ///< Protects task queue and state
    std::condition_variable    condition_;  ///< Signals workers when tasks available
    std::condition_variable    completed_;  ///< Signals main thread when work complete
    std::atomic_bool           running_;    ///< Shutdown flag (atomic for lock-free check)
    bool                       complete_;   ///< true if task queue is empty (protected by mutex_)
    std::size_t                available_;  ///< Number of idle threads (protected by mutex_)
    std::size_t                total_;      ///< Total number of worker threads
    int numa_node_id_;                      ///< NUMA node ID for thread affinity (-1 = no affinity)

public:
    /**
         * @brief Default constructor is deleted (pool size must be specified).
         */
    thread_pool() = delete;

    /**
         * @brief Constructs thread pool with specified size and optional NUMA affinity.
         *
         * Creates pool_size worker threads, each running main_loop(). Optionally
         * calls init_thread() on each worker thread for custom initialization
         * (e.g., setting thread name, NUMA binding).
         *
         * @param pool_size Number of worker threads to create (must be > 0)
         * @param numa_node_id NUMA node ID for thread affinity (-1 = no affinity)
         * @param init_thread Optional initialization function called on each worker thread
         *
         * Thread Safety: Not thread-safe (constructor).
         *
         * @note Worker threads start immediately and wait for tasks.
         */
    GRAPHFIC_API explicit thread_pool(
        int pool_size, int numa_node_id = -1, const std::function<void()>& init_thread = nullptr);

    /**
         * @brief Destructor joins all worker threads.
         *
         * Sets running_ = false to signal shutdown, notifies all worker threads,
         * and joins them. Ensures all threads complete before destruction.
         *
         * Thread Safety: Not thread-safe (destructor).
         *
         * @note Blocks until all worker threads exit (may take time if tasks are running).
         */
    GRAPHFIC_API ~thread_pool() override;

    /**
         * @brief Returns total number of worker threads.
         * @return Total thread count
         * Thread Safety: Thread-safe.
         */
    GRAPHFIC_API size_t size() const override;

    /**
         * @brief Returns number of idle (available) worker threads.
         * @return Number of threads not currently executing tasks
         * Thread Safety: Thread-safe (protected by mutex).
         */
    GRAPHFIC_API size_t num_available() const override;

    /**
         * @brief Checks if current thread is a worker thread from this pool.
         * @return true if calling thread is a worker thread, false otherwise
         * Thread Safety: Thread-safe.
         */
    GRAPHFIC_API bool in_thread_pool() const override;

    /**
         * @brief Submits a simple task to the thread pool.
         *
         * Adds the task to the queue and signals a worker thread to execute it.
         * Returns immediately; task executes asynchronously.
         *
         * @param func Task to execute (signature: void())
         *
         * Thread Safety: Thread-safe (protected by mutex).
         */
    GRAPHFIC_API void run(std::function<void()> func) override;

    /**
         * @brief Submits a task with thread ID parameter to the thread pool.
         *
         * Similar to run() but the task receives the worker thread's index as a
         * parameter. This is useful for tasks that need to know which thread is
         * executing them (e.g., for thread-local storage indexing).
         *
         * @tparam Task Callable type with signature void(size_t)
         * @param task Task to execute (receives thread index as parameter)
         *
         * Thread Safety: Thread-safe (protected by mutex).
         *
         * Example:
         * @code
         * pool.run_task_with_id([](size_t thread_id) {
         *     results[thread_id] = compute();
         * });
         * @endcode
         */
    template <typename Task>
    void run_task_with_id(Task task)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // Add task to queue and signal a worker thread
        tasks_.emplace(static_cast<std::function<void(std::size_t)>>(task));
        complete_ = false;
        condition_.notify_one();
    }

    /**
         * @brief Waits until all tasks in the queue have completed.
         *
         * Blocks the calling thread until the task queue is empty and all worker
         * threads are idle. Uses condition variable with predicate to avoid
         * spurious wakeups.
         *
         * Thread Safety: Thread-safe (protected by mutex).
         *
         * @note This does NOT wait for tasks submitted after this call.
         */
    GRAPHFIC_API void wait_work_complete();

private:
    /**
         * @brief Entry point for worker threads (runs in loop until shutdown).
         *
         * Each worker thread runs this function in a loop:
         * 1. Wait for task (condition variable)
         * 2. Dequeue task from queue
         * 3. Execute task
         * 4. Update available count
         * 5. Signal completion if queue is empty
         *
         * @param index Thread index (0 to pool_size-1)
         *
         * Thread Safety: Thread-safe (each thread has unique index).
         */
    void main_loop(std::size_t index);
};

/**
     * @brief Specialized thread pool with thread naming and NUMA awareness.
     *
     * This class extends thread_pool with additional features:
     * - Sets thread names to "XsigmaTaskThread" for debugging
     * - Optionally binds threads to NUMA nodes for performance
     *
     * This is the primary thread pool type used by the Graphfic parallel module
     * for inter-op parallelism. It is typically accessed via ThreadPoolRegistry.
     *
     * Thread Safety: Inherits thread safety from thread_pool.
     * Design Pattern: Decorator pattern (adds functionality to thread_pool).
     * Coding Standard: Follows Graphfic C++ standards.
     */
class GRAPHFIC_VISIBILITY task_thread_pool : public graphfic::thread_pool
{
public:
    /**
         * @brief Constructs task thread pool with thread naming and NUMA binding.
         *
         * Creates a thread pool where each worker thread:
         * 1. Sets its name to "XsigmaTaskThread" (visible in debuggers/profilers)
         * 2. Optionally binds to specified NUMA node (if GRAPHFIC_HAS_NUMA=1)
         *
         * @param pool_size Number of worker threads to create (must be > 0)
         * @param numa_node_id NUMA node ID for thread affinity (-1 = no affinity)
         *
         * Thread Safety: Not thread-safe (constructor).
         *
         * @note Thread naming improves debugging experience (visible in lldb, gdb, etc.)
         * @note NUMA binding can improve performance on NUMA systems by reducing
         *       memory access latency.
         */
    explicit task_thread_pool(int pool_size, int numa_node_id = -1)
        : thread_pool(
              pool_size,
              numa_node_id,
              [numa_node_id]()
              {
                  // Set thread name for debugging/profiling
                  graphfic::detail::parallel::set_thread_name("XsigmaTaskThread");

#if GRAPHFIC_HAS_NUMA
                  // Bind thread to NUMA node if NUMA support is enabled
                  NUMABind(numa_node_id);
#endif
              })
    {
    }
};

/**
     * @brief Registry for managing thread pool instances.
     *
     * This registry provides centralized management of thread pool instances.
     * It allows creating, retrieving, and destroying thread pools by name.
     *
     * USAGE:
     * @code
     * // Create or retrieve thread pool
     * auto pool = ThreadPoolRegistry()->Create("MyPool", device_id, pool_size, true);
     *
     * // Submit task
     * pool->run([]() { do_work(); });
     * @endcode
     *
     * Thread Safety: Thread-safe (registry uses internal synchronization).
     * Design Pattern: Registry pattern for centralized resource management.
     *
     * @note The registry manages shared_ptr instances, so pools are automatically
     *       destroyed when no longer referenced.
     */
GRAPHFIC_DECLARE_SHARED_REGISTRY(ThreadPoolRegistry, task_thread_pool_base, int, int, bool);

}  // namespace graphfic
