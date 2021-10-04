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
 * MIT License
 *
 * Copyright (c) 2021 Donald-Rupin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *
 *  @file benchmarks.cpp
 *
 *
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <latch>

#include "zib/spin_mpsc_queue.hpp"
#include "zib/wait_mpsc_queue.hpp"

#include "dmitry_mpsc.hpp"
#include "multi_list.hpp"
#include "naive_queue.hpp"

namespace zib::benchmark {

    template <typename Queue>
    std::size_t
    benchmark_multi_thread(std::size_t _threads, std::size_t _elements);

    void
    run_benchmarks(std::size_t _threads, std::size_t _elements)
    {
        std::map<std::string, std::vector<std::uint64_t>> times_;
        size_t                                            count = 0;

        static constexpr auto kNumberOfQueues = 5;
        static constexpr auto kNumberOfRounds = 10;

        while (count < kNumberOfQueues * kNumberOfRounds)
        {

            //std::cout << "Iteration: " << count << "\n";

            if (count % kNumberOfQueues == 0)
            {

                auto time = benchmark_multi_thread<naive_queue<std::uint64_t>>(_threads, _elements);
                times_["naive_queue"].emplace_back(time);
            }
            else if (count % kNumberOfQueues == 1)
            {

                auto time =
                    benchmark_multi_thread<wait_mpsc_queue<std::uint64_t>>(_threads, _elements);
                times_["wait_mpsc_queue"].emplace_back(time);
            }
            else if (count % kNumberOfQueues == 2)
            {

                auto time = benchmark_multi_thread<dmitry_mpsc<std::uint64_t>>(_threads, _elements);
                times_["dmitry_mpsc"].emplace_back(time);
            }
            else if (count % kNumberOfQueues == 3)
            {

                auto time = benchmark_multi_thread<multi_list<std::uint64_t>>(_threads, _elements);
                times_["multi_list"].emplace_back(time);
            }
            else if (count % kNumberOfQueues == 4)
            {

                auto time =
                    benchmark_multi_thread<spin_mpsc_queue<std::uint64_t>>(_threads, _elements);
                times_["spin_mpsc_queue"].emplace_back(time);
            }

            ++count;
        }

        for (const auto&[name, times] : times_)
        {

            std::uint64_t average = 0;
            for (const auto t : times)
            {
                average += t;
            }
            average = average / times.size();

            std::cout << name << ": " << average << "\n";
        }
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
    std::size_t
    benchmark_multi_thread(std::size_t _threads, std::size_t _elements)
    {

        std::vector<std::jthread> threads(_threads);
        Queue                     queue(_threads);
        std::latch                lch(_threads + 2);
        auto                      number_of_cores = core_count();

        size_t index = 1;
        for (auto& t : threads)
        {
            t = std::jthread(
                [&, index]()
                {
                    lch.arrive_and_wait();
                    using namespace std::chrono_literals;
                    for (size_t i = 0; i < _elements; ++i)
                    {
                        queue.enqueue(i + (_elements * index), index - 1);
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

        std::jthread executer(
            [&]()
            {
                lch.arrive_and_wait();
                size_t amount = 0;
                while (amount != _elements * _threads)
                {
                    if constexpr (std::
                                      is_same_v<Queue, wait_mpsc_queue<typename Queue::value_type>>)
                    {
                        queue.dequeue();
                        ++amount;
                    }
                    else
                    {
                        if (queue.dequeue()) { ++amount; }
                    }
                }
            }

        );

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(0, &cpuset);
        int rc = pthread_setaffinity_np(executer.native_handle(), sizeof(cpu_set_t), &cpuset);
        if (rc != 0) { std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n"; }

        auto start = std::chrono::high_resolution_clock::now();
        lch.arrive_and_wait();

        if (executer.joinable()) { executer.join(); }

        auto end = std::chrono::high_resolution_clock::now();

        auto total_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

        for (auto& t : threads)
        {
            if (t.joinable()) { t.join(); }
        }

        return total_time;
    }

}   // namespace zib::benchmark

int
main()
{
    for (auto i = 1; i <= 32; i*=2) {
        std::cout << "Test with " << i << " threads\n";
        zib::benchmark::run_benchmarks(i, 1000000);
    }
    
    return 0;
}

/* Example Output

dmitry_mpsc: 254819192
multi_list: 252835174
naive_queue: 199452915
spin_mpsc_queue: 8722351
wait_mpsc_queue: 13274709
Test with 2 threads
dmitry_mpsc: 304331177
multi_list: 280580925
naive_queue: 299178981
spin_mpsc_queue: 37922308
wait_mpsc_queue: 38167835
Test with 4 threads
dmitry_mpsc: 366495022
multi_list: 315894548
naive_queue: 724980671
spin_mpsc_queue: 96374587
wait_mpsc_queue: 79676840
Test with 8 threads
dmitry_mpsc: 519373840
multi_list: 633102534
naive_queue: 1352006855
spin_mpsc_queue: 267449632
wait_mpsc_queue: 199714334
Test with 16 threads
dmitry_mpsc: 1165982729
multi_list: 1792171799
naive_queue: 2434262999
spin_mpsc_queue: 796652621
wait_mpsc_queue: 681314188
Test with 32 threads
dmitry_mpsc: 2058273292
multi_list: 3588852466
naive_queue: 4654888003
spin_mpsc_queue: 2113349557
wait_mpsc_queue: 1547190110

*/
