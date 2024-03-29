#include "jobSystem.h"
#include "utils.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace Typhoon {

namespace Jobs {

namespace {

constexpr size_t jobAlignment = TY_JS_JOB_ALIGNMENT;
static_assert(jobAlignment >= 128 && detail::isPowerOfTwo(jobAlignment), "Job aligment must be a power of 2");

#ifdef _DEBUG
constexpr size_t jobPadding =
    jobAlignment - sizeof(JobFunction) - sizeof(std::atomic_int_fast32_t) - sizeof(JobId) * 3 - sizeof(bool) - sizeof(bool) * 2;
#else
constexpr size_t jobPadding = jobAlignment - sizeof(JobFunction) - sizeof(std::atomic_int_fast32_t) - sizeof(JobId) * 3 - sizeof(bool);
#endif

struct alignas(jobAlignment) Job {
	JobFunction              func;
	std::atomic_int_fast32_t unfinished;
	JobId                    parent;
	JobId                    continuation;
	JobId                    next;
	bool                     isLambda;
#ifdef _DEBUG
	bool started;
	bool isContinuation;
#endif
	char data[jobPadding];
};

constexpr size_t sizeJob = sizeof(Job);

struct JobQueue {
	JobId* jobIds;
	size_t jobPoolOffset;
	size_t jobPoolCapacity;
	size_t jobPoolMask;
	size_t jobIndex;
	int    top;
	int    bottom;
#if TY_JS_STEALING
	std::mutex mutex; // in case other threads steal a job from this queue
#endif
	std::thread::id threadId;
	size_t          index;
	ThreadStats     stats;
#if TY_JS_PROFILE
	std::chrono::steady_clock::time_point startTime;
#endif
};

thread_local size_t tl_threadIndex = 0;

} // namespace

struct JobSystem {
	JobSystemAllocator                 allocator;
	std::vector<std::thread>           workerThreads;
	void*                              jobPoolMemory;
	Job*                               jobPool;
	JobId*                             jobIdPool;
	size_t                             threadCount; // main + worker threads
	size_t                             jobsPerThread;
	size_t                             jobCapacity;
	JobQueue                           queues[maxThreads];
	std::mutex                         cv_m;
	std::condition_variable            semaphore;
	std::atomic_int32_t                activeJobCount { 0 };
	std::mt19937                       randomEngine { std::random_device {}() };
	std::uniform_int_distribution<int> dist;
	bool                               isRunning;
};

JobQueue& getQueue(JobId jobId, JobSystem& js) {
	assert(jobId);
	return js.queues[(jobId - 1) / js.jobsPerThread];
}

namespace {

Job& getJob(Job* jobPool, JobId jobId) {
	assert(jobId);
	return jobPool[jobId - 1];
}

JobQueue& getThisThreadQueue(JobSystem& js) {
	return js.queues[tl_threadIndex];
}

// Adds a job to the private end of the queue (LIFO)
void pushJob(JobQueue& queue, JobId jobId, JobSystem& js) {
	assert(queue.threadId == std::this_thread::get_id());
	++queue.stats.numEnqueuedJobs;
	{
#if TY_JS_STEALING
		std::lock_guard lock { queue.mutex };
#endif
		assert(queue.top <= queue.bottom);
		// TODO check capacity
		queue.jobIds[queue.bottom & queue.jobPoolMask] = jobId;
		++queue.bottom;
	}
	js.activeJobCount.fetch_add(1);
	js.semaphore.notify_all(); // wake up working threads
}

// Pops a job from the private end of the queue (LIFO)
JobId popJob(JobQueue& queue, JobSystem& js) {
	assert(queue.threadId == std::this_thread::get_id());
#if TY_JS_STEALING
	std::lock_guard lock { queue.mutex };
#endif
	if (queue.bottom <= queue.top) {
		return nullJobId;
	}
	--queue.bottom;
	js.activeJobCount.fetch_sub(1);
	return queue.jobIds[queue.bottom & queue.jobPoolMask];
}

#if TY_JS_STEALING
JobId stealJob(JobQueue& queue) {
	std::lock_guard lock { queue.mutex };
	if (queue.bottom <= queue.top) {
		return nullJobId;
	}
	const JobId job = queue.jobIds[queue.top & queue.jobPoolMask];
	++queue.top;
	return job;
}
#endif

void finishJob(JobSystem& js, JobId jobId, JobQueue& queue) {
	Job&          job = getJob(js.jobPool, jobId);
	const int32_t unfinishedJobCount = --(job.unfinished);
	assert(unfinishedJobCount >= 0);
	if (unfinishedJobCount == 0) {
		// Push continuations
		for (JobId c = job.continuation; c; c = getJob(js.jobPool, c).next) {
			pushJob(queue, c, js);
		}
		// Notify parent
		if (job.parent) {
			finishJob(js, job.parent, queue);
		}
	}
}

void executeJob(JobId jobId, JobSystem& js, JobQueue& queue) {
#if TY_JS_PROFILE
	const auto startTime = std::chrono::steady_clock::now();
#endif
	Job& job = getJob(js.jobPool, jobId);
	assert(job.unfinished > 0);
	const JobParams prm { jobId, queue.index, job.data };
	if (job.isLambda) {
		void*      ptr = detail::alignPointer(job.data, alignof(JobLambda));
		JobLambda* lambda = static_cast<JobLambda*>(ptr);
		(*lambda)(queue.index); // call
		lambda->~JobLambda();   // destruct
		job.isLambda = false;
	}
	else {
		job.func(prm);
	}
	finishJob(js, jobId, queue);
#if TY_JS_PROFILE
	queue.stats.runningTime += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - startTime);
#endif
}

JobId getNextJob(JobQueue& queue, JobSystem& js) {
	JobId job = popJob(queue, js);
	if (! job) {
#if TY_JS_STEALING
		// This worker's queue is empty. Steal from other queues
		// TODO How to steal from the queue with most jobs (usually the main one)
		const size_t offset = js.dist(js.randomEngine);
		const size_t otherQueueIndex = queue.index == 0 ? (queue.index + offset) % js.threadCount : 0;
		assert(otherQueueIndex != queue.index);
		++queue.stats.numAttemptedStealings;
		job = stealJob(js.queues[otherQueueIndex]);
		if (job) {
			++queue.stats.numStolenJobs;
			++js.queues[otherQueueIndex].stats.numGivenJobs;
			return job;
		}
#endif // TY_JS_STEALING
	}
	return job;
}

// Function run by a worker thread
void worker(JobQueue& queue, size_t threadIndex, JobSystem& js) {
	tl_threadIndex = threadIndex;
	queue.threadId = std::this_thread::get_id();
	while (true) {
		std::unique_lock lk { js.cv_m };
		js.semaphore.wait(lk, [&js] { return ! js.isRunning || js.activeJobCount.load() > 0; });
		if (! js.isRunning) {
			break;
		}
		if (JobId job = getNextJob(queue, js); job) {
			// Release lock
			lk.unlock();
			executeJob(job, js, queue);
			++queue.stats.numExecutedJobs;
		}
		else {
			lk.unlock();
			std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
		}
	}
}

void stopThreads(JobSystem& js) {
	{
		std::unique_lock lock { js.cv_m };
		js.isRunning = false;
	}
	js.semaphore.notify_all(); // notify working threads

	for (auto& thread : js.workerThreads) {
		thread.join();
	}
}

bool isJobFinished(JobSystem& js, JobId jobId) {
	const Job& job = getJob(js.jobPool, jobId);
	return (job.unfinished == 0);
}

void nullFunction(const JobParams& /*prm*/) {
}

void* mallocWrap(size_t size) {
	return malloc(size);
}

void freeWrap(void* ptr) {
	free(ptr);
}

JobSystem* jobSystem = nullptr;

} // namespace

void initJobSystem(size_t numJobsPerThread, size_t numWorkerThreads) {
	const JobSystemAllocator allocator { mallocWrap, freeWrap }; // default allocator
	initJobSystem(numJobsPerThread, numWorkerThreads, allocator);
}

void initJobSystem(size_t numJobsPerThread, size_t numWorkerThreads, const JobSystemAllocator& allocator) {
	assert(numJobsPerThread > 0);
	assert(allocator.alloc);
	assert(allocator.free);

	if (numWorkerThreads == defaultNumWorkerThreads) {
		numWorkerThreads = std::thread::hardware_concurrency() - 1; // main thread excluded
	}

	constexpr size_t maxJobs = std::numeric_limits<JobId>::max() - 1; // jobId 0 is reserved

	numJobsPerThread = detail::nextPowerOfTwo(static_cast<uint32_t>(numJobsPerThread));
	while (numJobsPerThread > maxJobs) {
		numJobsPerThread /= 2; // keep pow of 2
	}

	size_t threadCount = numWorkerThreads + 1; // + 1 for main thread
	threadCount = std::min(threadCount, maxThreads);
	threadCount = std::min(threadCount, maxJobs / numJobsPerThread);

	const size_t jobCapacity = threadCount * numJobsPerThread;
	const size_t jobPoolMemorySize = sizeof(Job) * jobCapacity + (jobAlignment - 1);
	void* const  jobPoolMemory = allocator.alloc(jobPoolMemorySize);

	Job* const jobPool = static_cast<Job*>(detail::alignPointer(jobPoolMemory, alignof(Job)));
	for (size_t i = 0; i < jobCapacity; ++i) {
		jobPool[i].unfinished = 0;
	}

	JobId* const jobIdPool = static_cast<JobId*>(allocator.alloc(jobCapacity * sizeof(JobId)));

	auto js = new (allocator.alloc(sizeof(JobSystem))) JobSystem;
	js->jobPoolMemory = jobPoolMemory;
	js->jobsPerThread = numJobsPerThread;
	js->threadCount = threadCount;
	js->jobPool = jobPool;
	js->jobIdPool = jobIdPool;
	js->jobCapacity = jobCapacity;
	js->allocator = allocator;
	js->isRunning = true;

	// Init worker threads and queues
	js->workerThreads.reserve(threadCount - 1);

	if (threadCount > 1) {
		// Init uniform random distribution
		using param_t = std::uniform_int_distribution<>::param_type;
		js->dist.param(param_t { 1, (int)threadCount - 1 });
	}

	for (size_t i = 0; i < threadCount; ++i) {
		JobQueue& q = js->queues[i];
		q.jobIds = jobIdPool + i * numJobsPerThread;
		q.jobPoolOffset = i * numJobsPerThread;
		q.jobPoolCapacity = numJobsPerThread;
		q.jobPoolMask = numJobsPerThread - 1;
		q.top = 0;
		q.bottom = 0;
		q.jobIndex = 0;
		q.index = i;
		q.stats = {};
		if (i == 0) {
			// Main thread
			q.threadId = std::this_thread::get_id();
		}
		else {
			// Worker thread
			js->workerThreads.emplace_back(worker, std::ref(q), i, std::ref(*js));
		}
#if TY_JS_PROFILE
		q.startTime = std::chrono::steady_clock::now();
#endif
	}

	jobSystem = js;
}

void destroyJobSystem() {
	if (jobSystem) {
		JobSystemAllocator allocator = jobSystem->allocator;
		stopThreads(*jobSystem);
		allocator.free(jobSystem->jobPoolMemory);
		allocator.free(jobSystem->jobIdPool);
		jobSystem->~JobSystem();
		allocator.free(jobSystem);
		jobSystem = nullptr;
	}
}

size_t getWorkerThreadCount() {
	assert(jobSystem);
	return jobSystem->workerThreads.size();
}

JobId createJob() {
	return detail::createJobImpl(nullFunction, nullptr, 0);
}

JobId createChildJob(JobId parent, JobFunction function) {
	return detail::createChildJobImpl(parent, function ? function : nullFunction, nullptr, 0);
}

void startJob(JobId jobId) {
	assert(jobSystem);
	JobSystem& js = *jobSystem;
#ifdef _DEBUG
	Job& job = getJob(js.jobPool, jobId);
	assert(job.started == false);
	assert(job.isContinuation == false); // cannot start manually a continuation
	job.started = true;
#endif

	JobQueue& queue = getQueue(jobId, js);
	assert(queue.threadId == std::this_thread::get_id());
	pushJob(queue, jobId, js);
}

void waitForJob(JobId jobId) {
	assert(jobId);
	assert(jobSystem);
	JobSystem& js = *jobSystem;

	JobQueue& queue = getQueue(jobId, js);
	assert(queue.threadId == std::this_thread::get_id()); // only the thread that created a job can wait for it
	while (! isJobFinished(js, jobId)) {
		if (JobId nextJob = getNextJob(queue, js); nextJob) {
			executeJob(nextJob, js, queue);
			++queue.stats.numExecutedJobs;
		}
		else {
			std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
		}
	}
}

void startAndWaitForJob(JobId jobId) {
	startJob(jobId);
	waitForJob(jobId);
}

void startFunction(JobId parentJobId, JobLambda&& lambda) {
	assert(jobSystem);

	const JobId jobId = detail::createChildJobImpl(parentJobId, nullFunction, nullptr, 0);
	Job&        job = getJob(jobSystem->jobPool, jobId);
	static_assert(sizeof job.data >= sizeof(JobLambda) + alignof(JobLambda) - 1);
	// in-place move construct lambda into Job::data
	void* const ptr = detail::alignPointer(job.data, alignof(JobLambda));
	new (ptr) JobLambda { std::move(lambda) };
	job.isLambda = true;
	startJob(jobId);
}

JobId addContinuation(JobId job, JobFunction function) {
	return detail::addContinuationImpl(job, function, nullptr, 0);
}

JobId addContinuation(JobId job, JobLambda&& lambda) {
	const JobId continuationId = detail::addContinuationImpl(job, nullFunction, nullptr, 0);
	Job&        continuation = getJob(jobSystem->jobPool, continuationId);
	static_assert(sizeof continuation.data >= sizeof(JobLambda) + alignof(JobLambda) - 1);
	// in-place move construct lambda into Job::data
	void* const ptr = detail::alignPointer(continuation.data, alignof(JobLambda));
	new (ptr) JobLambda { std::move(lambda) };
	continuation.isLambda = true;
	return continuationId;
}

ThreadStats getThreadStats(size_t threadIdx) {
	auto& queue = jobSystem->queues[threadIdx];
#if TY_JS_PROFILE
	const auto endTime = std::chrono::steady_clock::now();
	queue.stats.totalTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - queue.startTime);
#endif
	return queue.stats;
}

size_t getThisThreadIndex() {
	return getThisThreadQueue(*jobSystem).index;
}

namespace detail {

JobId createJobImpl(JobFunction function, const void* data, size_t dataSize) {
	assert(function);
	assert(dataSize <= sizeof(Job::data));
	assert(data == nullptr || dataSize);

	JobSystem& js = *jobSystem;

	JobQueue&   queue = getThisThreadQueue(js);
	const JobId jobId = static_cast<JobId>(1 + queue.jobPoolOffset + queue.jobIndex);
	queue.jobIndex = (queue.jobIndex + 1) & queue.jobPoolMask; // ring buffer
	assert(jobId <= js.jobCapacity);
	Job& job = getJob(js.jobPool, jobId);
#ifdef _DEBUG
	job.isContinuation = false;
	job.started = false;
	assert(job.unfinished == 0 && "Job queue is full"); // catch full queue
#endif
	job.func = function;
	job.parent = nullJobId;
	job.continuation = nullJobId;
	job.next = nullJobId;
	job.unfinished = 1;
	job.isLambda = false;
	if (data) {
		std::memcpy(job.data, data, dataSize);
	}
	else {
#if _DEBUG
		std::memset(job.data, 0, sizeof job.data);
#endif
	}
	return jobId;
}

JobId createChildJobImpl(JobId parent, JobFunction function, const void* data, size_t dataSize) {
	JobSystem& js = *jobSystem;
	JobId      jobId = createJobImpl(function, data, dataSize);
	Job&       job = getJob(js.jobPool, jobId);
	job.parent = parent;
	if (parent) {
		Job& parentJob = getJob(js.jobPool, parent);
		assert(parentJob.unfinished > 0); // it cannot have finished already
		++parentJob.unfinished;
	}
	return jobId;
}

JobId addContinuationImpl(JobId previousJobId, JobFunction function, const void* data, size_t dataSize) {
	assert(jobSystem);
	assert(previousJobId != nullJobId);
	assert(function);

	Job& previousJob = getJob(jobSystem->jobPool, previousJobId);
	assert(previousJob.started == false);

	const JobId continuationId = createChildJobImpl(previousJob.parent, function, data, dataSize);
#if _DEBUG
	getJob(jobSystem->jobPool, continuationId).isContinuation = true;
#endif

	// Add continuation to linked list
	if (! previousJob.continuation) {
		previousJob.continuation = continuationId;
	}
	else {
		JobId iter = previousJob.continuation;
		while (jobSystem->jobPool[iter].next) {
			iter = jobSystem->jobPool[iter].next;
		}
		jobSystem->jobPool[iter].next = continuationId;
	}
	return continuationId;
}

void parallelForImpl(const JobParams& prm) {
	ParallelForJobData data;
	std::memcpy(&data, prm.args, sizeof data); // copy to avoid misalignment
	if (data.count > data.splitThreshold) {
		// split in two
		const uint32_t     leftCount = data.count / 2u;
		ParallelForJobData leftData { data.function, data.splitThreshold, data.offset, leftCount, {} };
		std::memcpy(leftData.functionArgs, data.functionArgs, sizeof leftData.functionArgs);
		JobId left = createChildJob(prm.job, parallelForImpl, leftData);
		startJob(left);

		const uint32_t     rightCount = data.count - leftCount;
		ParallelForJobData rightData { data.function, data.splitThreshold, data.offset + leftCount, rightCount, {} };
		std::memcpy(rightData.functionArgs, data.functionArgs, sizeof rightData.functionArgs);
		JobId right = createChildJob(prm.job, parallelForImpl, rightData);
		startJob(right);
	}
	else {
		// execute the function on the range of data
		(data.function)(data.offset, data.count, data.functionArgs, prm.threadIndex);
	}
}

} // namespace detail

} // namespace Jobs

} // namespace Typhoon
