#pragma once

#include <stddef.h> // size_t

namespace Typhoon {

// Job system configuration
// Either change the settings here or define the corresponding macros in your build configuration

// Maximum number of pending jobs
#ifdef TY_JS_MAX_JOBS
constexpr size_t defaultMaxJobs = (TY_JS_MAX_JOBS);
#else
constexpr size_t defaultMaxJobs = 4096;
#endif

constexpr size_t maxThreads = 32;
constexpr size_t defaultParallelForSplitThreshold = 256; // TODO elements or bytes?

// alignment of the Job structure
// The padding bytes are used to hold data for the associated Job function
#ifndef TY_JS_JOB_ALIGNMENT
#define TY_JS_JOB_ALIGNMENT 128
#endif

// Set to 0 to disable job stealing (for debugging)
#ifndef TY_JS_STEALING
#define TY_JS_STEALING 1
#endif

} // namespace Typhoon
