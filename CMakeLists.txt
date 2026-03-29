cmake_minimum_required(VERSION 3.14)
project(ThreadPool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_executable(threadpool src/main.cpp)
target_include_directories(threadpool PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(threadpool PRIVATE Threads::Threads)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()
