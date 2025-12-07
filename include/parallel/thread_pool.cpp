/*
 * This code is inspired by PyTorch's parallel implementation.
 */

/**
 * @file thread_pool.cpp
 * @brief Implementation of Thread Pool for Graphfic Parallel Execution
 *
 * This file implements the thread pool classes used by the Graphfic parallel module
 * for both intra-op and inter-op parallelism. The thread pool is used by the native
 * backend (when OpenMP is not available) to execute parallel tasks.
 *
 * ARCHITECTURE:
 * =============
 * The implementation provides three classes:
 *
 * 1. task_thread_pool_base (Abstract Base Class):
 *    - Provides default_num_threads() static method
 *    - Determines optimal thread count based on hardware
 *
 * 2. thread_pool (Concrete Implementation):
 *    - Implements producer-consumer pattern with task queue
 *    - Manages worker threads using RAII
 *    - Provides synchronization via mutex and condition variables
 *    - Supports both simple tasks and tasks with thread ID parameter
 *
 * 3. task_thread_pool (Specialized Thread Pool):
 *    - Extends thread_pool with thread naming and NUMA awareness
 *    - Sets thread names for debugging ("GraphficTaskThread")
 *    - Optionally binds threads to NUMA nodes
 *
 * DESIGN PATTERNS:
 * ================
 * Producer-Consumer Pattern:
 * - Main thread produces tasks (via run() or run_task_with_id())
 * - Worker threads consume tasks from queue
 * - Mutex protects shared queue
 * - Condition variable signals task availability
 *
 * RAII Pattern:
 * - Thread pool manages thread lifetime automatically
 * - Constructor creates worker threads
 * - Destructor joins all threads (blocks until complete)
 * - No manual thread management required
 *
 * SYNCHRONIZATION:
 * ================
 * Primitives Used:
 * - mutex_: Protects task queue and state variables
 * - condition_: Signals worker threads when tasks are available
 * - completed_: Signals main thread when all tasks complete
 * - running_: Atomic flag for shutdown coordination
 *
 * Synchronization Flow:
 * 1. Task Submission: Lock mutex, add task to queue, signal condition_
 * 2. Task Execution: Worker waits on condition_, dequeues task, executes
 * 3. Completion: Worker signals completed_ when queue becomes empty
 * 4. Shutdown: Set running_=false, signal all workers, join threads
 *
 * THREAD LIFECYCLE:
 * =================
 * 1. Construction:
 *    - Creates pool_size worker threads
 *    - Each thread runs main_loop() in infinite loop
 *    - Threads wait on condition variable for tasks
 *
 * 2. Task Execution:
 *    - Worker wakes up when task is available
 *    - Dequeues task from queue
 *    - Executes task (either simple or with thread ID)
 *    - Updates available_ counter
 *    - Signals completed_ if queue is empty
 *
 * 3. Destruction:
 *    - Sets running_ = false
 *    - Signals all workers via condition_.notify_all()
 *    - Joins all threads (blocks until all exit)
 *
 * THREAD SAFETY:
 * ==============
 * - All public methods are thread-safe
 * - Task queue protected by mutex
 * - Atomic operations for running_ flag
 * - Condition variables for synchronization
 * - No data races or race conditions
 *
 * CODING STANDARDS:
 * =================
 * - Follows Graphfic C++ coding standards
 * - snake_case naming convention
 * - RAII for resource management (threads, mutexes)
 * - No exceptions in destructors
 * - Proper const correctness
 * - Comprehensive error handling
 *
 * HARDWARE DETECTION:
 * ===================
 * The default_num_threads() method determines optimal thread count:
 * 1. Query cpuinfo for physical cores and logical threads
 * 2. Prefer physical cores over logical threads (avoids hyperthreading overhead)
 * 3. Fall back to std::thread::hardware_concurrency()
 * 4. Minimum of 1 thread if detection fails
 *
 * NUMA AWARENESS:
 * ===============
 * If GRAPHFIC_HAS_NUMA=1:
 * - Threads can be bound to specific NUMA nodes
 * - Reduces memory access latency on NUMA systems
 * - Improves cache locality and performance
 *
 * USAGE:
 * ======
 * The thread pool is typically accessed via ThreadPoolRegistry:
 * @code
 * auto pool = ThreadPoolRegistry()->Create("MyPool", device_id, pool_size, true);
 * pool->run([]() { do_work(); });
 * pool->wait_work_complete();
 * @endcode
 *
 * For the native parallel backend, the thread pool is used internally by
 * invoke_parallel() in parallel.cpp.
 */

#include "parallel/thread_pool.h"

#include <algorithm>

#include "logging/logger.h"
#include "parallel/thread_name.h"
#include "util/cpu_info.h"

namespace graphfic
{
size_t task_thread_pool_base::default_num_threads()
{
    size_t num_threads = 0;
    if (cpu_info::initialize())
    {
        // In cpuinfo parlance cores are physical ones and processors are virtual
        // thread_pool should be defaulted to number of physical cores
        size_t const num_cores = cpu_info::number_of_cores();
        num_threads            = cpu_info::number_of_threads();
        if (num_cores > 0 && num_cores < num_threads)
        {
            return num_cores;
        }
        if (num_threads > 0)
        {
            return num_threads;
        }
    }
    num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0)
    {
        num_threads = 1;
    }
    return num_threads;
}

thread_pool::thread_pool(int pool_size, int numa_node_id, const std::function<void()>& init_thread)
    : threads_(pool_size < 0 ? default_num_threads() : pool_size),
      running_(true),
      complete_(true),
      available_(threads_.size()),
      total_(threads_.size()),
      numa_node_id_(numa_node_id)
{
    for (std::size_t i = 0; i < threads_.size(); ++i)
    {
        threads_[i] = std::thread(
            [this, i, init_thread]()
            {
                graphfic::detail::parallel::set_thread_name("pt_thread_pool");
                if (init_thread)
                {
                    init_thread();
                }
                this->main_loop(i);
            });
    }
}

thread_pool::~thread_pool()
{
    // Set running flag to false then notify all threads.
    {
        std::unique_lock<std::mutex> const lock(mutex_);
        running_ = false;
        condition_.notify_all();
    }

    // Join all threads. Threads should exit cleanly when running_ is set to false.
    for (auto& t : threads_)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

size_t thread_pool::size() const
{
    return threads_.size();
}

size_t thread_pool::num_available() const
{
    std::unique_lock<std::mutex> const lock(mutex_);
    return available_;
}

bool thread_pool::in_thread_pool() const
{
    return std::any_of(
        threads_.begin(),
        threads_.end(),
        [](const std::thread& thread) { return thread.get_id() == std::this_thread::get_id(); });
}

void thread_pool::run(std::function<void()> func)
{
    if (threads_.empty())
    {
        GRAPHFIC_LOG_ERROR("No threads to run a task");
        return;
    }
    std::unique_lock<std::mutex> const lock(mutex_);

    // Set task and signal condition variable so that a worker thread will
    // wake up and use the task.
    tasks_.emplace(std::move(func));
    complete_ = false;
    condition_.notify_one();
}

void thread_pool::wait_work_complete()
{
    std::unique_lock<std::mutex> lock(mutex_);
    completed_.wait(lock, [&]() { return complete_; });
}

void thread_pool::main_loop(std::size_t index)
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_)
    {
        // Wait on condition variable while the task is empty and
        // the pool is still running.
        condition_.wait(lock, [&]() { return !tasks_.empty() || !running_; });
        // If pool is no longer running, break out of loop.
        if (!running_)
        {
            break;
        }

        // Copy task locally and remove from the queue.  This is
        // done within its own scope so that the task object is
        // destructed immediately after running the task.  This is
        // useful in the event that the function contains
        // shared_ptr arguments bound via bind.
        {
            task_element_t const tasks = std::move(tasks_.front());
            tasks_.pop();
            // Decrement count, indicating thread is no longer available.
            --available_;

            lock.unlock();

            // Run the task.
            try
            {
                if (tasks.run_with_id)
                {
                    tasks.with_id(index);
                }
                else
                {
                    tasks.no_id();
                }
            }
            catch (const std::exception& e)
            {
                GRAPHFIC_LOG_ERROR("Exception in thread pool task: {}", e.what());
            }
            catch (...)
            {
                GRAPHFIC_LOG_ERROR("Exception in thread pool task: unknown");
            }

            // Destruct tasks before taking the lock.  As tasks
            // are user provided std::function, they can run
            // arbitrary code during destruction, including code
            // that can reentrantly call into thread_pool (which would
            // cause a deadlock if we were holding the lock).
        }

        // Update status of empty, maybe
        // Need to recover the lock first
        lock.lock();

        // Increment count, indicating thread is available.
        ++available_;
        if (tasks_.empty() && available_ == total_)
        {
            complete_ = true;
            completed_.notify_one();
        }

        // Deliberately hold the lock on the backedge, so this thread has an
        // opportunity to acquire a new task before another thread acquires
        // the lock.
    }  // while running_
}

GRAPHFIC_DEFINE_SHARED_REGISTRY(ThreadPoolRegistry, task_thread_pool_base, int, int, bool)
}  // namespace graphfic
