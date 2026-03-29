#include "ThreadPool.h"

#include <iostream>

int main()
{
    tp::ThreadPool pool(4, 16);

    auto result = pool.submit([](int a, int b) {
        return a + b;
    }, 1, 2);

    std::cout << "1 + 2 = " << result.get() << '\n';
    return 0;
}
