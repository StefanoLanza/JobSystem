// This example shows how to spawn child jobs that update N rigid bodies in parallel

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <include/jobSystem.h>
#include <iostream>
#include <mutex>
#include <thread>

using namespace Typhoon;

namespace {

std::mutex coutMutex;

void print(const char* msgFormat, ...) {
	va_list msgArgs;
	va_start(msgArgs, msgFormat);

	char msgBuffer[256];
	vsnprintf_s(msgBuffer, std::size(msgBuffer), std::size(msgBuffer) - 1, msgFormat, msgArgs);
	std::cout << msgBuffer << std::endl;
	std::cout.flush();

	va_end(msgArgs);
}

void tsPrint(const char* msgFormat, ...) {
	va_list msgArgs;
	va_start(msgArgs, msgFormat);

	char msgBuffer[256];
	vsnprintf_s(msgBuffer, std::size(msgBuffer), std::size(msgBuffer) - 1, msgFormat, msgArgs);
	std::lock_guard<std::mutex> lock { coutMutex };
	std::cout << msgBuffer << std::endl;
	std::cout.flush();

	va_end(msgArgs);
}

void updateRigidBody(const JobParams& prm) {
	const int bodyIndex = unpackJobArg<int>(prm.args);
	tsPrint("[thread %zd] Update rigid body [%d]", prm.threadIndex, bodyIndex);
	std::this_thread::sleep_for(std::chrono::microseconds(20)); // simulate work
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

int __cdecl main(int /*argc*/, char* /*argv*/[]) {
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
	const JobId physicsJob = createChildJob(rootJob, jobPhysics, 1000);
	startJob(physicsJob);
	startAndWaitForJob(rootJob);

	const auto endTime = std::chrono::steady_clock::now();
	const auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
	print("Elapsed time: %.4f sec", static_cast<double>(elapsedMicros) / 1e6);
	print("");

	destroyJobSystem();
	return 0;
}
