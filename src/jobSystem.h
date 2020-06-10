#pragma once

#include "config.h"
#include <cstdint>
#include <functional>
#include <tuple>

namespace Typhoon {

using JobId = uint16_t;
constexpr JobId nullJobId = 0;

struct JobSystem;

// Job parameters
// job can be used to add child jobs on the fly
// threadIndex can be used to fetch from or store data into per-thread buffers
struct JobParams {
	JobId       job;
	size_t      threadIndex;
	const void* args;
};

using JobFunction = void (*)(const JobParams&);
using JobLambda = std::function<void()>;
using ParallelForFunction = void (*)(size_t, size_t, const void* functionArgs);

// Custom allocator
struct JobSystemAllocator {
	std::function<void*(size_t)> alloc;
	std::function<void(void*)>   free;
};

// Initialize the job system with a custom allocator
void initJobSystem(size_t numJobsPerThread, size_t numWorkerThreads, const JobSystemAllocator& allocator);

// Initialize the job system with the default allocator (malloc and free)
void initJobSystem(size_t numJobsPerThread, size_t numWorkerThreads);

// Destroy the job system
void destroyJobSystem();

// Return the number of worker threads
size_t getWorkerThreadCount();

// Create an empty job
JobId createJob();

// Create a job executing a function with arguments
template <typename... ArgType>
JobId createJob(JobFunction function, ArgType... args);

// Create a child job executing a function with no arguments
JobId createChildJob(JobId parent, JobFunction function = nullptr);

// Create a child job executing a function with arguments
template <typename... ArgType>
JobId createChildJob(JobId parent, JobFunction function, ArgType... args);

// Start a job
void startJob(JobId jobId);

// Wait for a job to complete
void waitForJob(JobId jobId);

// Helper: start a job and wait for its completion
void startAndWaitForJob(JobId jobId);

// Create and start a child job executing a lambda function
void startFunction(JobId parent, JobLambda&& lambda);

// Add a continuation with no arguments to a job
JobId addContinuation(JobId job, JobFunction function);

// Add a continuation with arguments to a job
template <typename... ArgType>
JobId addContinuation(JobId job, JobFunction function, ArgType... args);

// Add a lambda continuation to a job
// Note: prefer lambdas with few captures to avoid heap allocations
JobId addContinuation(JobId job, JobLambda&& function);

// Helper: create and start a child job executing a function with arguments
template <typename... ArgType>
void startChildJob(JobId parent, JobFunction function, ArgType... args);

// Execute a parallel for loop
template <typename... ArgType>
JobId parallelFor(JobId parent, size_t elementCount, size_t splitThreshold, ParallelForFunction function, const ArgType&... args);

// Utility
template <typename... ArgType>
std::tuple<ArgType...> unpackJobArgs(const void* args);

} // namespace Typhoon

#include "jobSystem.inl"
