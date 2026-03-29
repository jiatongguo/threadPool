include(FetchContent)

find_package(GTest CONFIG QUIET)

if(NOT GTest_FOUND)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(threadpool_tests test.cpp)
target_include_directories(threadpool_tests PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(threadpool_tests PRIVATE GTest::gtest_main Threads::Threads)

include(GoogleTest)
gtest_discover_tests(threadpool_tests)
