// This example shows how to organize a typical game frame into jobs

#include <include/jobSystem.h>

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

struct Model {
	float x, y, z;
};

void cullModels(size_t offset, size_t count, const void* /*args*/, size_t threadIndex) {
	tsPrint("[thread %zd] Cull models. offset: %zd count: %zd", threadIndex, offset, count);
	// SIMD frustum culling of the models
}

void drawModels(size_t offset, size_t count, const void* /*args*/, size_t threadIndex) {
	tsPrint("[thread %zd] Draw models. offset: %zd count: %zd", threadIndex, offset, count);
	// Create a command buffer for rendering the models
}

void updateRigidBody(const JobParams& prm) {
	const int bodyIndex = unpackJobArg<int>(prm.args);
	tsPrint("[thread %zd] Update rigid body: %d", prm.threadIndex, bodyIndex);
	std::this_thread::sleep_for(std::chrono::microseconds(20)); // simulate work
}

void jobPhysics(const JobParams& prm) {
	tsPrint("Physics");
	const int numPhysicsJobs = unpackJobArg<int>(prm.args);
	// We could also use parallelFor instead
	for (int i = 0; i < numPhysicsJobs; ++i) {
		const JobId childJob = createChildJob(prm.job, updateRigidBody, i);
		startJob(childJob);
	}
}

void animateSkeleton(size_t threadIndex, int index) {
	tsPrint("[thread %zd] Animate skeleton: %d", threadIndex, index);
	std::this_thread::sleep_for(std::chrono::microseconds(20)); // simulate work
}

void jobAnimation(const JobParams& prm) {
	tsPrint("Animation");
	const int numAnimationJobs = unpackJobArg<int>(prm.args);
	// We could also use parallelFor instead
	for (int i = 0; i < numAnimationJobs; ++i) {
		startFunction(prm.job, [i](size_t threadIndex) { animateSkeleton(threadIndex, i); });
	}
}

void jobSimulate(const JobParams& prm) {
	tsPrint("Simulate");
	constexpr size_t numRigidBodies = 20;
	constexpr size_t numSkeletons = 20;
	const JobId      physicsJob = createChildJob(prm.job, jobPhysics, numRigidBodies);
	addContinuation(physicsJob, jobAnimation, numSkeletons);
	startJob(physicsJob);
}

void jobSyncSimAndRendering(const JobParams& /*prm*/) {
	tsPrint("Sync simulation & rendering");
	// E.g. copy position and orientation of each rigid body to the associated rendering data structure
}

void jobCull(const JobParams& prm) {
	tsPrint("Cull models");
	auto [models, numModels] = unpackJobArgs<Model*, int>(prm.args);
	const JobId cullLoop = parallelFor(prm.job, defaultParallelForSplitThreshold, cullModels, numModels, models);
	startJob(cullLoop);
}

void jobDraw(const JobParams& prm) {
	tsPrint("Draw models");
	auto [models, numModels] = unpackJobArgs<Model*, int>(prm.args);
	const JobId drawLoop = parallelFor(prm.job, defaultParallelForSplitThreshold, drawModels, numModels, models);
	startJob(drawLoop);
}

void jobSubmitCommandBuffers(const JobParams& /*prm*/) {
	tsPrint("Submit command buffers");
}

void jobRender(const JobParams& prm) {
	tsPrint("Render");
	// cull -> draw -> submit
	auto [models, numModels] = unpackJobArgs<Model*, int>(prm.args);
	const JobId cullJob = createChildJob(prm.job, jobCull, models, numModels);
	const JobId drawJob = addContinuation(cullJob, jobDraw, models, numModels);
	const JobId submitJob = addContinuation(drawJob, jobSubmitCommandBuffers);
	(void)submitJob;
	startJob(cullJob);
}

void present(size_t /*threadIndex*/) {
	tsPrint("Present");
}

JobId simulateGameFrame(Model* models, size_t numModels) {
	/*
	root
	    simulation
	        physics
	        animation
	    sync simulation and rendering
	    render
	        cull models
	        draw models
	        submit command buffers
	    present
	*/
	const JobId rootJob = createJob();
	const JobId simulationJob = createChildJob(rootJob, jobSimulate);
	const JobId syncSimRenderingJob = addContinuation(simulationJob, jobSyncSimAndRendering);
	const JobId renderJob = addContinuation(syncSimRenderingJob, jobRender, models, numModels);
	const JobId presentJob = addContinuation(renderJob, present);
	(void)presentJob;
	startJob(simulationJob);

	return rootJob;
}
} // namespace

int __cdecl main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	const size_t numWorkerThreads = std::thread::hardware_concurrency() - 1;
	initJobSystem(defaultMaxJobs, numWorkerThreads);

	print("Worker threads: %zd", numWorkerThreads);

	const auto startTime = std::chrono::steady_clock::now();

	constexpr size_t numModels = 100;
	Model            models[numModels];
	const JobId      rootJob = simulateGameFrame(models, numModels);
	startAndWaitForJob(rootJob);

	const auto endTime = std::chrono::steady_clock::now();
	const auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
	print("Elapsed time: %.4f sec", static_cast<double>(elapsedMicros) / 1e6);

	destroyJobSystem();
	return 0;
}
