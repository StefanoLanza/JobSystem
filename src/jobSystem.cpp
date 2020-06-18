#include "jobSystem.h"
#include "utils.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <mutex>
#include <thread>
#include <vector>

namespace Typhoon {

namespace {

constexpr size_t jobAlignment = TY_JS_JOB_ALIGNMENT;

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
static_assert(sizeJob == jobAlignment);

struct JobQueue {
	JobId* jobIds;
	size_t jobPoolOffset;
	size_t jobPoolCapacity;
	size_t jobPoolMask;
	size_t jobIndex;
#if TY_JS_LOCKFREE
	std::atomic<int> top;
	std::atomic<int> bottom;
#else
	int                         top;
	int                         bottom;
	std::mutex                  mutex;
#endif
	std::thread::id threadId;
	size_t          index;
	size_t          unfinishedJobs;
	bool            isRunning;
};

thread_local size_t tl_threadIndex = 0;

} // namespace

struct JobSystem {
	JobSystemAllocator       allocator;
	std::vector<std::thread> workerThreads;
	void*                    jobPoolMemory;
	Job*                     jobPool;
	JobId*                   jobIdPool;
	size_t                   threadCount; // main + worker threads
	size_t                   jobsPerThread;
	size_t                   jobCapacity;
	JobQueue                 queues[maxThreads];
	std::mutex               cv_m;
	std::condition_variable  semaphore;
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
void pushJob(JobQueue& queue, JobId jobId) {
	assert(queue.threadId == std::this_thread::get_id());

	// TODO check capacity
#if TY_JS_LOCKFREE
	const int bottom = queue.bottom.load(std::memory_order_relaxed);
	queue.jobIds[bottom & queue.jobPoolMask] = jobId; // TODO atomic store ?
	// This fence ensures that an object isn't stolen before we update `bottom`.
	std::atomic_thread_fence(std::memory_order_release);
	queue.bottom.store(bottom + 1, std::memory_order_relaxed);
#else
	std::lock_guard<std::mutex> lock { queue.mutex };
	queue.jobIds[queue.bottom & queue.jobPoolMask] = jobId;
	++queue.bottom;
#endif
}

// Pops a job from the private end of the queue (LIFO)
JobId popJob(JobQueue& queue) {
	assert(queue.threadId == std::this_thread::get_id());

#if TY_JS_LOCKFREE
	int bottom = queue.bottom.load(std::memory_order_relaxed);
	queue.bottom.store(bottom - 1, std::memory_order_relaxed);
	std::atomic_thread_fence(std::memory_order_seq_cst);
	int top = queue.top.load(std::memory_order_relaxed);

	JobId job = nullJobId;
	if (bottom <= top) {
		// Deque empty: reverse the decrement to bottom.
		queue.bottom.store(bottom, std::memory_order_relaxed);
	}
	else if (bottom == top) {
		// Race against steals.
		if (queue.top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
			job = queue.jobIds[top & queue.jobPoolMask];
		}
		queue.bottom.store(bottom, std::memory_order_relaxed);
	}
	else {
		job = queue.jobIds[(bottom - 1) & queue.jobPoolMask];
	}
	return job;

#else
	std::lock_guard<std::mutex> lock { queue.mutex };
	if (queue.bottom <= queue.top) {
		return nullJobId;
	}
	--queue.bottom;
	return queue.jobIds[queue.bottom & queue.jobPoolMask];
#endif
}

#if TY_JS_STEALING
JobId stealJob(JobQueue& queue) {
#if TY_JS_LOCKFREE
	int top = queue.top.load(std::memory_order_acquire);
	std::atomic_thread_fence(std::memory_order_seq_cst);
	int   bottom = queue.bottom.load(std::memory_order_acquire);
	JobId job = nullJobId;
	if (bottom > top) {
		// Race against other steals and a pop.
		if (queue.top.compare_exchange_strong(top, top + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
			job = queue.jobIds[top & queue.jobPoolMask];
		}
	}
	return job;
#else
	std::lock_guard<std::mutex> lock { queue.mutex };
	if (queue.bottom <= queue.top) {
		return nullJobId;
	}
	const JobId job = queue.jobIds[queue.top & queue.jobPoolMask];
	++queue.top;
	return job;
#endif
}
#endif

void finishJob(JobSystem& js, JobId jobId) {
	Job&          job = getJob(js.jobPool, jobId);
	const int32_t unfinishedJobs = --(job.unfinished);
	assert(unfinishedJobs >= 0);
	if (unfinishedJobs == 0) {
		// Push continuations
		JobQueue& queue = getThisThreadQueue(js);
		for (JobId c = job.continuation; c; c = getJob(js.jobPool, c).next) {
			pushJob(queue, c);
		}

		if (job.parent) {
			finishJob(js, job.parent);
		}
	}
}

void executeJob(JobId jobId, JobSystem& js) {
	Job& job = getJob(js.jobPool, jobId);
	assert(job.unfinished > 0);
	const JobParams prm { jobId, tl_threadIndex, job.data };
	if (job.isLambda) {
		void* const      ptr = detail::alignPointer(job.data, alignof(JobLambda));
		JobLambda* const lambda = static_cast<JobLambda*>(ptr);
		(*lambda)(tl_threadIndex); // call
		lambda->~JobLambda();      // destruct
		job.isLambda = false;
	}
	else {
		assert(job.func);
		job.func(prm);
	}
	finishJob(js, jobId);
}

JobId getNextJob(JobQueue& queue, JobSystem& js) {
	JobId job = popJob(queue);
	if (! job) {
#if TY_JS_STEALING
		// Steal from other queues
		// TODO Best strategy ?
#if 0
		const size_t otherQueue = (queue.index + 1) % js.threadCount;
#else
		const size_t otherQueue = (queue.index + rand()) % js.threadCount;
#endif
		if (otherQueue != queue.index && js.queues[otherQueue].isRunning) {
			job = stealJob(js.queues[otherQueue]);
			if (job) {
				return job;
			}
		}
#endif
		std::this_thread::yield();
	}
	return job;
}

// Function run by a worker thread
void worker(JobQueue& queue, size_t threadIndex, JobSystem& js) {
	tl_threadIndex = threadIndex;
	queue.threadId = std::this_thread::get_id();
	while (queue.isRunning) {
		// TODO
		// std::unique_lock<std::mutex> lk(js.cv_m);
		// js.semaphore.wait(lk);

		const JobId job = getNextJob(queue, js);
		if (job) {
			executeJob(job, js);
		}
	}
}

void joinWorkerThreads(JobSystem& js) {
	for (size_t i = 0; i < js.threadCount; ++i) {
		js.queues[i].isRunning = false;
	}
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

void* _cdecl mallocWrap(size_t size) {
	return malloc(size);
}

void _cdecl freeWrap(void* ptr) {
	free(ptr);
}

std::unique_ptr<JobSystem> jobSystem;

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

	auto js = std::make_unique<JobSystem>();
	js->jobPoolMemory = jobPoolMemory;
	js->jobsPerThread = numJobsPerThread;
	js->threadCount = threadCount;
	js->jobPool = jobPool;
	js->jobIdPool = jobIdPool;
	js->jobCapacity = jobCapacity;
	js->allocator = allocator;

	{
		// Main thread queue
		JobQueue& q = js->queues[0];
		q.jobIds = jobIdPool;
		q.jobPoolCapacity = numJobsPerThread;
		q.jobPoolMask = numJobsPerThread - 1;
		q.top = 0;
		q.bottom = 0;
		q.jobPoolOffset = 0;
		q.jobIndex = 0;
		q.isRunning = true;
		q.index = 0;
		q.unfinishedJobs = 0;
		q.threadId = std::this_thread::get_id();
	}

	// Init worker threads and queues
	js->workerThreads.reserve(threadCount - 1);
	for (size_t i = 1; i < threadCount; ++i) {
		JobQueue& q = js->queues[i];
		q.jobPoolOffset = i * numJobsPerThread;
		q.jobIds = jobIdPool + i * numJobsPerThread;
		q.jobPoolCapacity = numJobsPerThread;
		q.jobPoolMask = numJobsPerThread - 1;
		q.top = 0;
		q.bottom = 0;
		q.jobIndex = 0;
		q.isRunning = true;
		q.index = i;
		q.unfinishedJobs = 0;
		js->workerThreads.emplace_back(worker, std::ref(q), i, std::ref(*js));
	}

	jobSystem = std::move(js);
}

void destroyJobSystem() {
	if (jobSystem) {
		joinWorkerThreads(*jobSystem);
		jobSystem->allocator.free(jobSystem->jobPoolMemory);
		jobSystem->allocator.free(jobSystem->jobIdPool);
		jobSystem.reset();
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
	pushJob(queue, jobId);
	// TODO js.semaphore.notify_all(); // wake up working threads TODO all ?
}

void waitForJob(JobId jobId) {
	assert(jobId);
	assert(jobSystem);
	JobSystem& js = *jobSystem;

	JobQueue& queue = getQueue(jobId, js);
	assert(queue.threadId == std::this_thread::get_id()); // only the thread that created a job can wait for it
	while (! isJobFinished(js, jobId)) {
		const JobId nextJob = getNextJob(queue, js);
		if (nextJob) {
			executeJob(nextJob, js);
		}
		else {
			// FIXME Sleep ?
			// std::this_thread::sleep_for(std::chrono::microseconds(1667));
		}
	}
}

void startAndWaitForJob(JobId jobId) {
	startJob(jobId);
	waitForJob(jobId);
}

void startFunction(JobId parent, JobLambda&& lambda) {
	assert(jobSystem);

	const JobId jobId = detail::createChildJobImpl(parent, nullFunction, nullptr, 0);
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

JobId addContinuation(JobId job, JobLambda&& function) {
	const JobId continuationId = detail::addContinuationImpl(job, nullFunction, nullptr, 0);
	Job&        continuation = getJob(jobSystem->jobPool, continuationId);
	static_assert(sizeof continuation.data >= sizeof(JobLambda) + alignof(JobLambda) - 1);
	// in-place move construct lambda into Job::data
	void* const ptr = detail::alignPointer(continuation.data, alignof(JobLambda));
	new (ptr) JobLambda { std::move(function) };
	continuation.isLambda = true;
	return continuationId;
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
	++queue.unfinishedJobs;
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

void parallel_for_job(const JobParams& prm) {
	const ParallelForJobData* data = static_cast<const ParallelForJobData*>(prm.args);
	if (data->count > data->splitThreshold) {
		// split in two
		const uint32_t     leftCount = data->count / 2u;
		ParallelForJobData leftData { data->function, data->splitThreshold, data->offset, leftCount };
		std::memcpy(leftData.functionArgs, data->functionArgs, sizeof leftData.functionArgs);
		JobId left = createChildJob(prm.job, parallel_for_job, leftData);
		startJob(left);

		const uint32_t     rightCount = data->count - leftCount;
		ParallelForJobData rightData { data->function, data->splitThreshold, data->offset + leftCount, rightCount };
		std::memcpy(rightData.functionArgs, data->functionArgs, sizeof rightData.functionArgs);
		JobId right = createChildJob(prm.job, parallel_for_job, rightData);
		startJob(right);
	}
	else {
		// execute the function on the range of data
		(data->function)(data->offset, data->count, data->functionArgs, prm.threadIndex);
	}
}

} // namespace detail

} // namespace Typhoon
