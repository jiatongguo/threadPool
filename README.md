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

## Requirements

- CMake 3.10 or newer
- A C++17 compiler

## Build

```bash
cmake -S . -B build
cmake --build build
```

The current top-level CMake target builds:

```bash
./build/threadpool
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
