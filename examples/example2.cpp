// This example shows how to execute concurrently jobs coded as C++ lambdas

#include <jobSystem/jobSystem.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdarg>
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
	vsnprintf(msgBuffer, std::size(msgBuffer), msgFormat, msgArgs);
	std::cout << msgBuffer << std::endl;
	std::cout.flush();

	va_end(msgArgs);
}

void tsPrint(const char* msgFormat, ...) {
	va_list msgArgs;
	va_start(msgArgs, msgFormat);

	char msgBuffer[256];
	vsnprintf(msgBuffer, std::size(msgBuffer), msgFormat, msgArgs);
	std::lock_guard<std::mutex> lock { coutMutex };
	std::cout << msgBuffer << std::endl;
	std::cout.flush();

	va_end(msgArgs);
}

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

	destroyJobSystem();
	return 0;
}
