// This example shows how to spawn child jobs that update N rigid bodies in parallel

#include <jobSystem/jobSystem.h>

#include "common.h"

#include <cassert>
#include <chrono>
#include <thread>

using namespace Typhoon::Jobs;

namespace {

void updateRigidBody(const JobParams& prm) {
	const int bodyIndex = unpackJobArg<int>(prm.args);
	tsPrint("[thread %zd] Update rigid body [%d]", prm.threadIndex, bodyIndex);
	std::this_thread::sleep_for(std::chrono::microseconds(100)); // simulate work
}

void jobPhysics(const JobParams& prm) {
	tsPrint("Physics");
	const int numRigidBodies = unpackJobArg<int>(prm.args);
	for (int i = 0; i < numRigidBodies; ++i) {
		const JobId childJob = createChildJob(prm.job, updateRigidBody, i);
		startJob(childJob);
	}
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
	// Custom allocator tracking memory
	size_t memory = 0;
	auto   customAlloc = [&memory](size_t size) {
        memory += size;
        return malloc(size);
	};
	auto customFree = [](void* ptr) { free(ptr); };

	const size_t             numWorkerThreads = std::thread::hardware_concurrency() - 1;
	const JobSystemAllocator allocator { customAlloc, customFree };
	initJobSystem(defaultMaxJobs, numWorkerThreads, allocator);

	print("Worker threads: %zd", numWorkerThreads);

	const auto startTime = std::chrono::steady_clock::now();

	const JobId rootJob = createJob();
	const JobId physicsJob = createChildJob(rootJob, jobPhysics, 100);
	startJob(physicsJob);
	startAndWaitForJob(rootJob);

	const auto endTime = std::chrono::steady_clock::now();
	const auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
	print("Elapsed time: %.5f sec", static_cast<double>(elapsedMicros) / 1e6);
	print("");

	printStats();

	destroyJobSystem();
	return 0;
}
