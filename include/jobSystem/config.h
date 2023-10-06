#pragma once

#include <cstddef> // size_t

namespace Typhoon {

namespace Jobs {

// Job system configuration
// Either change the settings here or define the corresponding macros in your build configuration

// Maximum number of pending jobs
#ifdef TY_JS_MAX_JOBS
constexpr size_t defaultMaxJobs = (TY_JS_MAX_JOBS);
#else
constexpr size_t defaultMaxJobs = 4096;
#endif

constexpr size_t maxThreads = 64;
constexpr size_t defaultParallelForSplitThreshold = 256; // TODO elements or bytes?
// Default sleep time in microsecond for idle threads
constexpr int sleep_us = 1;

// Alignment of the Job structure
// The padding bytes are used to hold data for the associated Job function
#ifndef TY_JS_JOB_ALIGNMENT
#define TY_JS_JOB_ALIGNMENT 256
#endif

// Set to 0 to disable profiling of worker threads
#ifndef TY_JS_PROFILE
#define TY_JS_PROFILE 1
#endif

} // namespace Jobs

} // namespace Typhoon

// Alias
namespace jobs = Typhoon::Jobs;
