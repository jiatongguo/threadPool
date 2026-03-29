cmake_minimum_required(VERSION 3.14)
project(ThreadPool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

add_library(threadpool_headers INTERFACE)
target_include_directories(threadpool_headers INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(threadpool_headers INTERFACE Threads::Threads)

add_executable(threadpool src/main.cpp)
target_link_libraries(threadpool PRIVATE threadpool_headers)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()
