/**
 * @file
 *
 * Public interface.
 */

#pragma once

#include "config.h"
#include <cstdint>
#include <functional>
#include <tuple>
#if TY_JS_PROFILE
#include <chrono>
#endif

namespace Typhoon {

namespace Jobs {

using JobId = uint16_t;
constexpr JobId nullJobId = 0;

struct JobSystem;

/**
 * @brief Job parameters

job can be used to add child jobs on the fly <br>
threadIndex can be used to fetch from or store data into per-thread buffers  <br>
*/
struct JobParams {
	JobId       job;
	size_t      threadIndex;
	const void* args;
};

/**
 * @brief Job function
 */
using JobFunction = void (*)(const JobParams&);

/**
 * @brief Job lambda
 */
using JobLambda = std::function<void(size_t threadIndex)>;

/**
 * @brief Parallel for function.
 */
using ParallelForFunction = void (*)(size_t elementCount, size_t splitThreshold, const void* functionArgs, size_t threadIndex);

/**
 * @brief Custom allocator
 */
struct JobSystemAllocator {
	std::function<void*(size_t)> alloc;
	std::function<void(void*)>   free;
};

// Pass this to initJobSystem to let the library initialize the number of worker threads
constexpr size_t defaultNumWorkerThreads = (size_t)-1;

/**
 * @brief Initialize the job system with a custom allocator
 * @param numJobsPerThread maximum number of jobs that a worker thread can execute
 * @param numWorkerThreads number of worker threads. Pass defaultNumWorkerThreads as default
 * @param allocator
 */
void initJobSystem(size_t numJobsPerThread, size_t numWorkerThreads, const JobSystemAllocator& allocator);

/**
 * @brief Initialize the job system with the default allocator (malloc and free)
 * @param numJobsPerThread maximum number of jobs that a worker thread can execute
 * @param numWorkerThreads number of worker threads. Pass defaultNumWorkerThreads as default
 */
void initJobSystem(size_t numJobsPerThread, size_t numWorkerThreads);

/**
 * @brief Destroy the job system
 */
void destroyJobSystem();

/**
 * @brief Return the number of worker threads
 * @return number of worker threads
 */
size_t getWorkerThreadCount();

/**
 * @brief Create an empty job
 * @return job identifier
 */
JobId createJob();

/**
 * @brief Create a job executing a function with arguments
 * @param function function associated with the job
 * @param ...args function arguments
 * @return new job identifier
 */
template <typename... ArgType>
JobId createJob(JobFunction function, ArgType... args);

/**
 * @brief Create a child job executing a function with no arguments
 * @param parentJobId parent job identifier
 * @param function function associated with the job
 * @return new job identifier
 */
JobId createChildJob(JobId parentJobId, JobFunction function = nullptr);

/**
 * @brief Create a child job executing a function with arguments
 * @tparam ...ArgType
 * @param parentJobId parent job identifier
 * @param function function associated with the job
 * @param ...args
 * @return new job identifier
 */
template <typename... ArgType>
JobId createChildJob(JobId parentJobId, JobFunction function, ArgType... args);

/**
 * @brief Start a job
 * @param jobId job identifier
 */
void startJob(JobId jobId);

/**
 * @brief Wait for a job to complete
 * @param jobId job identifier
 */
void waitForJob(JobId jobId);

/**
 * @brief Helper: start a job and wait for its completion
 * @param jobId job identifier
 */
void startAndWaitForJob(JobId jobId);

/**
 * @brief Create and start a child job executing a lambda function
 * @param parentJobId parent job identifier
 * @param lambda lambda function
 */
void startFunction(JobId parentJobId, JobLambda&& lambda);

/**
 * @brief Add a continuation to a job, with no arguments
 * @param job previous job identifier
 * @param function function associated with the continuation
 * @return continuation identifier
 */
JobId addContinuation(JobId job, JobFunction function);

/**
 * @brief Add a continuation to a job, with arguments
 * @param job previous job identifier
 * @param function function associated with the continuation
 * @param ...args function arguments
 * @return continuation identifier
 */
template <typename... ArgType>
JobId addContinuation(JobId job, JobFunction function, ArgType... args);

/**
 * @brief Add a lambda continuation to a job
 Note: prefer lambdas with few captures to avoid heap allocations
 * @param jobId previous job identifier
 * @param lambda lambda associated with the continuation
 * @return continuation identifier
*/
JobId addContinuation(JobId jobId, JobLambda&& lambda);

/**
 * @brief Helper: create and start a child job executing a function with arguments
 * @param parentJobId parent job identifier
 * @param function function associated with the job
 * @param ...args function arguments
 */
template <typename... ArgType>
void startChildJob(JobId parentJobId, JobFunction function, ArgType... args);

/**
 * @brief Execute a parallel for loop
 * @param parentJobId parent job identifier
 * @param elementCount element count
 * @param splitThreshold split threshold used to break the loop into threads, in elements
 * @param function associated with the job
 * @param ...args  function arguments
 * @return
 */
template <typename... ArgType>
JobId parallelFor(JobId parentJobId, size_t splitThreshold, ParallelForFunction function, size_t elementCount, const ArgType&... args);

/**
 * @brief Utility to unpack arguments
 * @param args pointer to a buffer containing arguments
 * @return a tuple with the unpacked arguments
 */
template <typename... ArgType>
std::tuple<ArgType...> unpackJobArgs(const void* args);

struct ThreadStats {
	size_t numEnqueuedJobs;
	size_t numExecutedJobs;
#if TY_JS_STEALING
	size_t numStolenJobs;
	size_t numAttemptedStealings;
	size_t numGivenJobs;
#endif
#if TY_JS_PROFILE
	std::chrono::microseconds totalTime;
	std::chrono::microseconds runningTime;
#endif
};

/**
 * @param thread index
 * @return statistics about a worker thread
 */
ThreadStats getThreadStats(size_t threadIdx);

/**
 * @return the index of the currently active worker thread
 */
size_t getThisThreadIndex();

} // namespace Jobs

} // namespace Typhoon

#include "jobSystem.inl"
