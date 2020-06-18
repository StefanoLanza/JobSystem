#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <include/jobSystem.h>
#include <iostream>
#include <mutex>
#include <thread>

#define CATCH_CONFIG_RUNNER
#include <Catch-master/single_include/catch2/catch.hpp>

using namespace Typhoon;

namespace {

#define LOG_LEVEL 1

struct Test {
	static constexpr size_t maxJobs = defaultMaxJobs;
	static constexpr int    numSkeletons = 256;
	static constexpr int    numRigidBodies = 64;
	static constexpr int    numFrames = 2;
	static constexpr int    numLambdas = 1024;
	static constexpr int    numModels = 600;
};

std::atomic<size_t> completeCount;
std::mutex          coutMutex;

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
	std::cout << "[Thread " << std::this_thread::get_id() << "] " << msgBuffer << std::endl;
	std::cout.flush();

	va_end(msgArgs);
}

struct Particle {
	float x, y;
	float vx, vy;
};

void updateParticles(size_t offset, size_t count, const void* args, size_t threadIndex) {
	auto [particles, dt] = unpackJobArgs<Particle*, float>(args);
	particles += offset;
#if LOG_LEVEL > 1
	tsPrint("Update particles. offset: %zd count: %zd; dt: %.2f", offset, count, dt);
#endif
	for (size_t i = 0; i < count; ++i) {
		particles[i].x += particles[i].vx * dt;
		particles[i].y += particles[i].vy * dt;
	}
}

void checkParticles(const Particle* particles, size_t particleCount) {
	float v = 0.f;
	for (size_t i = 0; i < particleCount; ++i) {
		CHECK(particles[i].x == v);
		CHECK(particles[i].y == v);
		v += 0.05f;
	}
}

void animateSkeleton(int index) {
#if LOG_LEVEL > 1
	tsPrint("Animate skeleton: %d", index);
#endif
	std::atomic_fetch_add<size_t>(&completeCount, 1);
}

struct Model {
	float x, y, z;
};

void cullModels(size_t offset, size_t count, const void* args, size_t threadIndex) {
#if LOG_LEVEL > 1
	tsPrint("Cull models. offset: %zd count: %zd", offset, count);
#endif
}

void drawModels(size_t offset, size_t count, const void* args, size_t threadIndex) {
#if LOG_LEVEL > 1
	tsPrint("Draw models. offset: %zd count: %zd", offset, count);
#endif
}

void updateRigidBody(const JobParams& prm) {
	const int bodyIndex = unpackJobArg<int>(prm.args);
	(void)bodyIndex;
#if LOG_LEVEL > 1
	tsPrint("Update rigid body: %d", bodyIndex);
#endif
	std::this_thread::sleep_for(std::chrono::microseconds(20));
	std::atomic_fetch_add<size_t>(&completeCount, 1);
}

void jobSimulate(const JobParams& prm) {
	tsPrint("Simulate");
}

void jobPhysics(const JobParams& prm) {
	tsPrint("Physics");
	const int numPhysicsJobs = unpackJobArg<int>(prm.args);
	for (int i = 0; i < numPhysicsJobs; ++i) {
		const JobId childJob = createChildJob(prm.job, updateRigidBody, i);
		startJob(childJob);
	}
}

void jobAnimation(const JobParams& prm) {
	tsPrint("Animation");
	const int numAnimationJobs = unpackJobArg<int>(prm.args);
	for (int i = 0; i < numAnimationJobs; ++i) {
		startFunction(prm.job, [i](size_t threadIndex) { animateSkeleton(i); });
	}
}

void jobSyncSimAndRendering(const JobParams& prm) {
	tsPrint("Sync simulation & rendering");
}

void present(size_t threadIndex) {
	(void)threadIndex;
	tsPrint("VSync");
}

void jobCull(const JobParams& prm) {
	tsPrint("Cull");
	const int    numModels = unpackJobArg<int>(prm.args);
	static Model models[1024];
	const JobId  cullLoop = parallelFor(prm.job, numModels, defaultParallelForSplitThreshold, cullModels, models);
	startJob(cullLoop);
}

void jobDraw(const JobParams& prm) {
	tsPrint("Draw");
	const int    numModels = unpackJobArg<int>(prm.args);
	static Model models[1024];
	const JobId  drawLoop = parallelFor(prm.job, numModels, defaultParallelForSplitThreshold, drawModels, models);
	startJob(drawLoop);
}

void jobSubmitCommandBuffers(const JobParams& prm) {
	tsPrint("Submit rendering");
}

void jobRender(const JobParams& prm) {
	tsPrint("Render");
	// cull -> draw -> submit
	const int   numModels = unpackJobArg<int>(prm.args);
	const JobId cullJob = createChildJob(prm.job, jobCull, numModels);
	const JobId drawJob = addContinuation(cullJob, jobDraw, numModels);
	const JobId submitJob = addContinuation(drawJob, jobSubmitCommandBuffers);
	(void)submitJob;
	startJob(cullJob);
}

void launchMissile(int index, float /*velocity*/) {
	std::this_thread::sleep_for(std::chrono::microseconds(10));
	std::atomic_fetch_add<size_t>(&completeCount, 1);
}

JobId addTestJobs(Test& test) {
	const JobId rootJob = createJob();
	const JobId animationJob = createChildJob(rootJob);
	for (int i = 0; i < test.numSkeletons; ++i) {
		startFunction(animationJob, [i](size_t threadIndex) { animateSkeleton(i); });
	}
	startJob(animationJob);
	return rootJob;
}

JobId addParallelParticleJobs(JobId parentJob, Particle* particles, size_t particleCount) {
	// Reset particles
	float v = 0.f;
	for (size_t i = 0; i < particleCount; ++i) {
		particles[i].x = 0.f;
		particles[i].y = 0.f;
		particles[i].vx = v;
		particles[i].vy = v;
		v += 0.05f;
	}
	constexpr float dt = 1.f;
	const JobId     job = parallelFor(parentJob, particleCount, 2048, updateParticles, particles, dt);
	startJob(job);
	return job;
}

JobId simulateGameFrame(Test& test) {
	/*
	root
	    simulate
	        physics, particles
	        animation
	    sync
	    render
	        cull[0], cull[1]...cull[n]
	        draw[0], draw[1]... draw[n]
	    vsync
	*/
	alignas(16) static Particle particles[8192];

	const JobId rootJob = createJob();
	const JobId simulateJob = createChildJob(rootJob, jobSimulate);
	const JobId physicsJob = createChildJob(simulateJob, jobPhysics, test.numRigidBodies);
	const JobId animationJob = addContinuation(physicsJob, jobAnimation, test.numSkeletons);
	const JobId particleJob = addParallelParticleJobs(simulateJob, particles, std::size(particles));
	const JobId syncJob = addContinuation(simulateJob, jobSyncSimAndRendering);
	const JobId renderJob = addContinuation(syncJob, jobRender, test.numModels);
	const JobId vsyncJob = addContinuation(renderJob, present);
	(void)animationJob;
	(void)particleJob;
	(void)vsyncJob;

	startJob(physicsJob);
	startJob(simulateJob);

	return rootJob;
}

} // namespace

TEST_CASE("Jobs") {
	size_t numWorkerThreads = 0;
	Test   test;

	// Custom allocator tracking memory
	size_t memory = 0;
	auto   customAlloc = [&memory](size_t size) {
        memory += size;
        return malloc(size);
	};
	auto customFree = [](void* ptr) { free(ptr); };

	SECTION("Single Threaded") {
		numWorkerThreads = 0;
	}
	SECTION("Multi Threaded") {
		numWorkerThreads = std::thread::hardware_concurrency() - 1;
	}

	print("Jobs");
	print("Worker threads: %zd", numWorkerThreads);

	const JobSystemAllocator allocator { customAlloc, customFree };
	initJobSystem(test.maxJobs, numWorkerThreads, allocator);

	std::atomic_store<size_t>(&completeCount, 0);

	auto startTime = std::chrono::steady_clock::now();

	const JobId rootJob = addTestJobs(test);
	startAndWaitForJob(rootJob);
	CHECK(std::atomic_load(&completeCount) == test.numSkeletons);

	auto endTime = std::chrono::steady_clock::now();
	const auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
	print("Elapsed time: %.4f sec", static_cast<double>(elapsedMicros) / 1e6);
	print("");

	destroyJobSystem();
}

TEST_CASE("Lambdas") {
	size_t numWorkerThreads = 0;
	Test   test;

	SECTION("Single Threaded") {
		numWorkerThreads = 0;
	}
	SECTION("Multi Threaded") {
		numWorkerThreads = std::thread::hardware_concurrency() - 1;
	}

	print("Lambdas");
	print("Worker threads: %zd", numWorkerThreads);

	initJobSystem(test.maxJobs, numWorkerThreads);

	std::atomic_store<size_t>(&completeCount, 0);

	auto startTime = std::chrono::steady_clock::now();

	const JobId rootJob = createJob();
	const float velocity = 10.f;
	for (int i = 0; i < test.numLambdas; ++i) {
		startFunction(rootJob, [i, velocity](size_t threadIndex) { launchMissile(i, velocity); });
	}

	startAndWaitForJob(rootJob);
	CHECK(std::atomic_load(&completeCount) == test.numLambdas);

	auto endTime = std::chrono::steady_clock::now();
	const auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
	print("Elapsed time: %.4f sec", static_cast<double>(elapsedMicros) / 1e6);
	print("");

	destroyJobSystem();
}

TEST_CASE("Parallel") {
	size_t numWorkerThreads = 0;
	Test   test;

	SECTION("Single Threaded") {
		numWorkerThreads = 0;
	}
	SECTION("Multi Threaded") {
		numWorkerThreads = std::thread::hardware_concurrency() - 1;
	}

	print("Parallel for");
	print("Worker threads: %zd", numWorkerThreads);

	initJobSystem(test.maxJobs, numWorkerThreads);

	auto startTime = std::chrono::steady_clock::now();

	alignas(16) static Particle particles[8192];
	const JobId                 rootJob = addParallelParticleJobs(nullJobId, particles, std::size(particles));
	waitForJob(rootJob);

	auto endTime = std::chrono::steady_clock::now();
	const auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
	print("Elapsed time: %.4f sec", static_cast<double>(elapsedMicros) / 1e6);
	print("");

	checkParticles(particles, std::size(particles));

	destroyJobSystem();
}

TEST_CASE("Game Frame") {
	size_t numWorkerThreads = 0;
	Test   test;

	SECTION("Single Threaded") {
		numWorkerThreads = 0;
	}
	SECTION("Multi Threaded") {
		numWorkerThreads = std::thread::hardware_concurrency() - 1;
	}

	initJobSystem(test.maxJobs, numWorkerThreads);

	std::atomic_store<size_t>(&completeCount, 0);

	auto startTime = std::chrono::steady_clock::now();

	print("Game frame");
	print("Worker threads: %zd", numWorkerThreads);
	for (size_t f = 0; f < test.numFrames; ++f) {
		print("Begin frame %zd", f);
		const JobId rootJob = simulateGameFrame(test);
		startAndWaitForJob(rootJob);
		print("End frame");
		// CHECK(std::atomic_load(&completeCount) == test.numAnimationJobs);
	}

	auto endTime = std::chrono::steady_clock::now();
	const auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
	print("Elapsed time: %.4f sec", static_cast<double>(elapsedMicros) / 1e6);
	print("");

	destroyJobSystem();
}

int __cdecl main(int argc, char* argv[]) {
	const int result = Catch::Session().run(argc, argv);
	return result;
}
