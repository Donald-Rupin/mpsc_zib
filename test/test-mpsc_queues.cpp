/*
 * [....... [..[..[.. [..
 *        [..  [..[.    [..
 *       [..   [..[.     [..
 *     [..     [..[... [.
 *    [..      [..[.     [..
 *  [..        [..[.      [.
 * [...........[..[.... [..
 *
 *
 *  MIT License
 *
 *  Copyright (c) 2021 Donald-Rupin
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 *  @file test-mpsc_queue.cpp
 *
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <iostream>
#include <fstream>
#include <type_traits>

#include "zib/spin_mpsc_queue.hpp"
#include "zib/wait_mpsc_queue.hpp"

namespace zib::test {

    template <typename Queue>
    int
    test_single_thread();

    template <typename Queue>
    int
    test_multi_thread();

    int
    run_test()
    {
        return test_single_thread<spin_mpsc_queue<std::uint64_t>>() ||
               test_single_thread<wait_mpsc_queue<std::uint64_t>>() ||
               test_multi_thread<spin_mpsc_queue<std::uint64_t>>() ||
               test_multi_thread<wait_mpsc_queue<std::uint64_t>>();
    }

    inline std::uint16_t
    core_count() noexcept
    {
        auto count = std::thread::hardware_concurrency();

        if (!count)
        {
            std::ifstream cpuinfo("/proc/cpuinfo");
            count = std::count(
                std::istream_iterator<std::string>(cpuinfo),
                std::istream_iterator<std::string>(),
                std::string("processor"));
        }

        return count;
    }

    template <typename Queue>
    int
    test_single_thread()
    {
        static constexpr auto kElements = 100000;
        Queue                 queue(1);

        for (size_t i = 0; i < kElements; ++i)
        {

            queue.enqueue(i, 0);
            queue.enqueue(i + kElements, 0);


            size_t element = 0;

            if constexpr (std::is_same_v<Queue, wait_mpsc_queue<typename Queue::value_type>>) {

                element = queue.dequeue();

            } else {

                auto result = queue.dequeue();

                if (!result) {
                    return true;
                }
                element = *result;
            }

            if (i != element) { return true; }

            if constexpr (std::is_same_v<Queue, wait_mpsc_queue<typename Queue::value_type>>) {

                element = queue.dequeue();

            } else {

                auto result = queue.dequeue();

                if (!result) {
                    return true;
                }
                element = *result;
            }

            if (i + kElements != element) { return true; }
        }

        return false;
    }

    template <typename Queue>
    int
    test_multi_thread()
    {
        static constexpr auto kElements      = 1000000;
        static constexpr auto kNumberThreads = 16;

        std::array<std::jthread, kNumberThreads> threads;
        Queue                                    queue(kNumberThreads);

        auto number_of_cores = core_count();

        size_t index = 1;
        for (auto& t : threads)
        {
            t = std::jthread(
                [&, index]()
                {
                    using namespace std::chrono_literals;
                    for (size_t i = 0; i < kElements; ++i)
                    {
                        queue.enqueue(i + (kElements * index), index - 1);
                    }
                });

            // /* Force the computer to utilize all cores... */
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(index % number_of_cores, &cpuset);
            int rc = pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
            if (rc != 0) { std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n"; }

            ++index;
        }

        bool result = false;

        std::jthread executer(
            [&]()
            {
                size_t amount = 0;
                while (amount != kElements * kNumberThreads)
                {
                    if constexpr (std::is_same_v<Queue, wait_mpsc_queue<typename Queue::value_type>>) {

                        queue.dequeue();
                        ++amount;

                    } else {

                        auto result = queue.dequeue();
                        if (result) {
                            ++amount;
                        }

                        if (amount == kElements * kNumberThreads) {
                            auto result = queue.dequeue();

                            if (result) {

                                result = true;
                            }
                        }
                    }
                    
                }
            }

        );

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(0, &cpuset);
        int rc = pthread_setaffinity_np(executer.native_handle(), sizeof(cpu_set_t), &cpuset);
        if (rc != 0) { std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n"; }

        if (executer.joinable()) { executer.join(); }

        for (auto& t : threads)
        {
            if (t.joinable()) { t.join(); }
        }

        return result;
    }

}   // namespace zib::test

int
main()
{
    return zib::test::run_test();
}