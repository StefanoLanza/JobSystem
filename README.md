**Typhoon JobSystem** is a C++ library for scheduling and executing jobs, either on a single thread or concurrently on multiple threads. It is intended for use in applications with tight performance and memory consumption constraints, like games.

The implementation is based on the job system described in the [Molecular Matters blog](https://blog.molecular-matters.com/2015/08/24/job-system-2-0-lock-free-work-stealing-part-1-basics/).

# Features
- Minimal, free-function API written in C++ 14
- Fixed memory usage
- Zero heap allocations at runtime
- Job stealing
- Support for lambdas 
- Support for parallel loops
- Support for continuations

# INSTALLATION
* Clone the repository to a local folder
* For testing
  * Run ```premake5.exe vs2019``` to generate a Visual Studio 2019 solution
  * Select a build configuration (Release, Debug, Win32, x64)
  * Build and run the UnitTest project
* For integration in your own project
  * Add the contents of the src folder to your project build pipeline.

# CONFIGURATION
Look at the file src/config.h Here you can find configuration settings for the library. You can change these settings by either editing this file or by defining them with the preprocessor in your build configuration.

# USAGE
First, it is convenient to use the ```Typhoon``` namespace.
```
using namespace Typhoon;
```
Initialize the job system
```
initJobSystem(maxJobs, numWorkerThreads);
```
```maxJobs``` is the maximum number of jobs that can exist at the same time. For a typical game frame, use the default defaultMaxJobs in config.h

```numWorkerThreads``` is the number of worker threads (not counting the main thread). We recommend setting it to a number less than or equal to that of available threads in order to avoid oversubscription. Take into account threads created by other systems when setting this number (for example, IO threads, threads created by third-party libraries, etc.).
For a single threaded job system, pass 0.

Create a root job. This typically represents a game frame.
```
const JobId rootJob = createJob();
```
Create child jobs. 
```
void animate(const JobParams& prm) {
	const float dt = unpackArg<float>(prm.args);
	// do some work
	// spawn new jobs
}

void render(const JobParams& prm) {
	auto [models, numModels] = unpackArgs<Model*, int>(prm.args);
	// do some work
	// spawn new jobs
}

const JobId animationJob = createChildJob(rootJob, animate, dt);
const JobId renderJob = createChildJob(rootJob, render, models, numModels);
```
You can pass a variable number of arguments to createChildJob. These can be retrieved with the ```unpackArg``` and ```unpackArgs``` utilities. See the examples above.

Start the jobs.
```
startJob(animationJob);
startJob(renderJob);
startJob(rootJob);
```
The root job and its children are now executed, potentially by multiple threads in parallel.

Wait for the root job to finish. A job is considered finished when its function has been executed and all its child jobs have finished.
```
waitForJob(rootJob); // blocking
```
At this point, the rootJob, animationJob and renderJob have executed. If rootJob represents a game frame, this signals the end of the frame. 

Often it is convenient to schedule the execution of functions without the syntactic overhead of 1) writing a wrapper function, 2) creating a child job 3) starting the job. This can be accomplished as follow.
```
void animate(float dt) {
	// do some work
}

startFunction(rootJob, [dt] { animate(dt); });
```
Note how the lambda captures one variable. The allocation strategy of C++ lambdas is implementation defined. Usually a lambda can capture few variables in an internal storage. Should the captured state exceed the size of the internal storage, the implementation fallbacks to a heap allocation, a costly operation at runtime. For this reason we recommend writing lambdas that capture few variables only (e.g. maximum four pointers).

Destroy the job system.
```
destroyJobSystem();
```
# TODO
- [ ] Fix lockfree queues
- [ ] Port to other platforms (Android, iOS)
- [ ] Investigate better strategies for splitting work in parallel loops
- [ ] Investigate other strategies for stealing jobs
- [ ] Integrate and test in a complex game

# CONTRIBUTE
I would appreciate the help of other programmers to complete the tasks in the todo list and to test the library more extensively.
