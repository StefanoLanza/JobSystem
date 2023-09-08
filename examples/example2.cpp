// This example shows how to execute concurrently jobs coded as C++ lambdas

#include <jobSystem/jobSystem.h>

#include "common.h"

#include <cassert>
#include <chrono>
#include <thread>

using namespace jobs;

namespace {

// Lambda
void launchMissile(size_t threadIndex, int index, float velocity) {
	std::this_thread::sleep_for(std::chrono::microseconds(10));
	tsPrint("[thread %zd] Launching missile [%d] with a velocity %f m/s", threadIndex, index, velocity);
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
	const size_t numWorkerThreads = std::thread::hardware_concurrency() - 1;
	initJobSystem(defaultMaxJobs, numWorkerThreads);

	print("Worker threads: %zd", numWorkerThreads);

	const auto startTime = std::chrono::steady_clock::now();

	const JobId rootJob = createJob();
	const float minVelocity = 10.f;
	for (int i = 0; i < 100; ++i) {
		// Start executing lambdas concurrently as children of rootJob
		startFunction(rootJob, [i, velocity = minVelocity + i * 0.1f](size_t threadIndex) { launchMissile(threadIndex, i, velocity); });
	}
	startAndWaitForJob(rootJob);

	const auto endTime = std::chrono::steady_clock::now();
	const auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
	print("Elapsed time: %.4f sec", static_cast<double>(elapsedMicros) / 1e6);

	printStats();

	destroyJobSystem();
	return 0;
}
