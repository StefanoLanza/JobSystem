// This example shows how to process an image with multiple threads

#include <include/jobSystem.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdarg>
#include <iostream>
#include <mutex>
#include <thread>
#include <new>

using namespace Typhoon;

namespace {

std::mutex coutMutex;

void print(const char* msgFormat, ...) {
	va_list msgArgs;
	va_start(msgArgs, msgFormat);

	char msgBuffer[256];
#ifdef MSVC
	vsnprintf_s(msgBuffer, std::size(msgBuffer), std::size(msgBuffer) - 1, msgFormat, msgArgs);
#endif
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

struct Image {
	int width;
	int height;
	int bpp;
	uint8_t* data;
};

uint32_t makeColor(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
	return (a << 24) | (b << 16) | (g << 8) | (r << 0);
}

void decomposeColor(uint32_t color, uint32_t rgba[4]) {
	rgba[0] = (color >> 0) & 255;
	rgba[1] = (color >> 8) & 255;
	rgba[2] = (color >> 16) & 255;
	rgba[3] = (color >> 24) & 255;
}

void clearImage(Image& image) {
	assert(image.bpp == 32);
	uint32_t* data = reinterpret_cast<uint32_t*>(image.data);
	for (int i = 0; i < image.width * image.height; ++i) {
		data[i] = makeColor(255, 127, 64, 255);
	}
}

Image allocImage(int width, int height, int bpp) {
	assert(width > 0);
	assert(height > 0);
	assert(bpp % 8 == 0);
	return { width, height, bpp, static_cast<uint8_t*>(malloc(width * height * bpp / 8)) };
}

void freeImage(Image& image) {
	free(image.data);
	image.data = nullptr;
}

void processData(const Image& image, size_t offset, size_t count) {
	uint32_t* data = reinterpret_cast<uint32_t*>(image.data + offset);
	for (size_t i = 0; i < count; ++i) {
		// To greyscale
		uint32_t rgba[4];
		decomposeColor(data[i], rgba);
		uint32_t lum = (rgba[0] + rgba[1] + rgba[2]) / 3;
		data[i] = makeColor(lum, lum, lum, rgba[3]);
	}
}

void processImage(size_t offset, size_t count, const void* args, size_t threadIndex) {
	Image* const image = unpackJobArg<Image*>(args);
	assert(image->bpp == 32);
#if _DEBUG
	tsPrint("[thread %zd] Process image. offset: %zd count: %zd;", threadIndex, offset, count);
#else
	(void)threadIndex;
#endif
	processData(*image, offset, count);
}

void run_st(Image& image) {
	print("Singlethreaded");
	clearImage(image);
	const auto startTime = std::chrono::steady_clock::now();
	processData(image, 0, (size_t)image.width * (size_t)image.height);
	const auto endTime = std::chrono::steady_clock::now();
	const auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
	print("Elapsed time: %.4f sec", static_cast<double>(elapsedMicros) / 1e6);
}

void run_mt(Image& image) {
	const size_t numWorkerThreads = std::thread::hardware_concurrency() - 1;

	print("Multithreaded");
	print("Worker threads: %zd", numWorkerThreads);

	clearImage(image);
	initJobSystem(defaultMaxJobs, numWorkerThreads);

	const auto startTime = std::chrono::steady_clock::now();
    
	const JobId rootJob = createJob();
	const JobId imageJob = parallelFor(rootJob, 8192*4, processImage, (size_t)image.width * (size_t)image.height, &image);
	startJob(imageJob);
	startAndWaitForJob(rootJob);

	const auto endTime = std::chrono::steady_clock::now();
	const auto elapsedMicros = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
	print("Elapsed time: %.4f sec", static_cast<double>(elapsedMicros) / 1e6);

	destroyJobSystem();
}

} // namespace

int __cdecl main(int /*argc*/, char* /*argv*/[]) {
	Image image = allocImage(1024, 1024, 32);
	run_st(image);
	run_mt(image);
	freeImage(image);
	return 0;
}
