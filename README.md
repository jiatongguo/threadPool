# ThreadPool

A simple C++ thread pool implementation built on top of a bounded blocking queue.

## Features

- Fixed-size worker thread pool
- Bounded task queue
- `submit()` returns `std::future`
- Graceful shutdown and immediate shutdown
- Header-defined core logic, easy to read and extend

## Project Structure

- `include/BlockingQueue.h`: bounded blocking queue
- `include/ThreadPool.h`: thread pool interface and worker loop
- `src/`: executable entry files
- `test/`: test-related files
- `.github/workflows/`: CI configuration

## Requirements

- CMake 3.14 or newer
- A C++17 compiler

## Build

```bash
cmake -S . -B build
cmake --build build
```

The project defines a header-only library target `threadpool_headers` and a demo executable `threadpool`.

Run the demo executable:

```bash
./build/threadpool
```

## Test

Build with tests enabled and run all tests:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the GoogleTest binary directly:

```bash
./build/test/threadpool_tests
```

Run a single test case:

```bash
./build/test/threadpool_tests --gtest_filter=ThreadPoolTest.ImmediateShutdownDiscardsQueuedTasks
```

## Usage

```cpp
#include "ThreadPool.h"
#include <iostream>

int main()
{
    tp::ThreadPool pool(4, 16);

    auto fut = pool.submit([](int a, int b) 
    {
        return a + b;
    }, 1, 2);

    std::cout << fut.get() << std::endl;

    pool.shutdown(tp::ThreadPool::ShutdownMode::Graceful);
}
```

## Shutdown Modes

- `Graceful`: stop accepting new tasks and finish queued work
- `Immediate`: stop accepting new tasks and discard queued work as soon as possible

## Notes

- Calling `submit()` after shutdown throws `std::runtime_error`
- Pushing into a closed queue fails
- Exceptions thrown by tasks are handled through `std::packaged_task` and the returned `std::future`
- CI builds the project and runs `ctest` on every push and pull request
