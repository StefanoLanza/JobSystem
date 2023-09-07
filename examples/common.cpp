#include "common.h"
#include <cstdarg>
#include <iostream>
#include <jobSystem/jobSystem.h>
#include <mutex>

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

void printStats() {
	using namespace Typhoon;
	for (size_t i = 0; i < getWorkerThreadCount(); ++i) {
		const auto stats = getThreadStats(i);
		print("Thread %zd", i);
#if TY_JS_PROFILE
		print("  Total time: %.5f sec", static_cast<double>(stats.totalTime.count()) / 1e6);
		print("  Running time: %.5f sec", static_cast<double>(stats.runningTime.count()) / 1e6);
#endif
		print("  Enqueued jobs: %zd", stats.numEnqueuedJobs);
		print("  Executed jobs: %zd", stats.numExecutedJobs);
		print("  Stolen jobs: %zd", stats.numStolenJobs);
		print("  Attempted stealings: %zd", stats.numAttemptedStealings);
		print("  Stealing efficiency : %.2f %%",
		      static_cast<float>(stats.numStolenJobs) / static_cast<float>(std::max<size_t>(1, stats.numAttemptedStealings)));
	}
}
