// This example shows how to execute a parallel for loop

#include <atomic>
#include <cassert>
#include <chrono>
#include <include/jobSystem.h>
#include <iostream>
#include <mutex>
#include <thread>

#define CATCH_CONFIG_RUNNER
#include <Catch-master/single_include/catch2/catch.hpp>

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

struct Particle {
	float x, y;
	float vx, vy;
};

void resetParticles(Particle* particles, size_t particleCount) {
	float v = 0.f;
	for (size_t i = 0; i < particleCount; ++i) {
		particles[i].x = 0.f;
		particles[i].y = 0.f;
		particles[i].vx = v;
		particles[i].vy = v;
		v += 0.05f;
	}
}

void updateParticles(size_t offset, size_t count, const void* args, size_t threadIndex) {
	auto [particles, dt] = unpackJobArgs<Particle*, float>(args);
	particles += offset;
	tsPrint("[thread %zd] Update particles. offset: %zd count: %zd; dt: %.2f", threadIndex, offset, count, dt);
	for (size_t i = 0; i < count; ++i) {
		particles[i].x += particles[i].vx * dt;
		particles[i].y += particles[i].vy * dt;
	}
	std::this_thread::sleep_for(std::chrono::microseconds(20)); // simulate more work
}

} // namespace

int __cdecl main(int /*argc*/, char* /*argv*/[]) {
	const size_t numWorkerThreads = std::thread::hardware_concurrency() - 1;
	initJobSystem(defaultMaxJobs, numWorkerThreads);

	print("Worker threads: %zd", numWorkerThreads);

	constexpr float             dt = 1.f / 60.f; // seconds
	constexpr size_t            numParticles = 16384;
	alignas(16) static Particle particles[numParticles];
	resetParticles(particles, numParticles);

	const auto startTime = std::chrono::steady_clock::now();

	const JobId rootJob = createJob();
	const JobId particleJob = parallelFor(rootJob, numParticles, 1024 /*split threshold*/, updateParticles, particles, dt);
	startJob(particleJob);
	startAndWaitForJob(rootJob);

	const auto endTime = std::chrono::steady_clock::now();
	const auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
	print("Elapsed time: %.4f sec", static_cast<double>(elapsedMicros) / 1e6);

	destroyJobSystem();
	return 0;
}
