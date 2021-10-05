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
#include <latch>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>

#include "zib/overflow_mpsc_queue.hpp"
#include "zib/spin_mpsc_queue.hpp"
#include "zib/wait_mpsc_queue.hpp"

#include "dmitry_mpsc.hpp"
#include "multi_list.hpp"
#include "naive_queue.hpp"

namespace zib::benchmark {

    struct LessMarker {
            std::uint64_t data_;
    };

    template <typename Queue>
    std::size_t
    benchmark_multi_thread(std::size_t _threads, std::size_t _elements);

    void
    run_benchmarks(std::size_t _threads, std::size_t _elements)
    {
        std::map<std::string, std::vector<std::uint64_t>> times_;
        size_t                                            count = 0;

        static constexpr auto kNumberOfQueues = 7;
        static constexpr auto kNumberOfRounds = 10;

        while (count < kNumberOfQueues * kNumberOfRounds)
        {

            // std::cout << "Iteration: " << count << "\n";

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
            else if (count % kNumberOfQueues == 5)
            {

                auto time =
                    benchmark_multi_thread<overflow_mpsc_queue<std::uint64_t>>(_threads, _elements);
                times_["overflow_mpsc_queue[normal]"].emplace_back(time);
            }
            else if (count % kNumberOfQueues == 6)
            {

                auto time =
                    benchmark_multi_thread<overflow_mpsc_queue<LessMarker>>(_threads, _elements);
                times_["overflow_mpsc_queue[overflow]"].emplace_back(time);
            }

            ++count;
        }

        for (const auto& [name, times] : times_)
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

        static constexpr bool is_overflow =
            std::is_same_v<Queue, overflow_mpsc_queue<typename Queue::value_type>>;

        static constexpr bool has_less = std::is_same_v<LessMarker, typename Queue::value_type>;

        auto queue_threads = _threads;
        if constexpr (has_less) { queue_threads = (_threads * 2 / 3); }

        std::vector<std::jthread> threads(_threads);
        Queue                     queue(queue_threads);
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
                        if constexpr (has_less) {

                            queue.safe_enqueue(LessMarker{i + (_elements * index)}, index - 1);

                        } else if constexpr (is_overflow)
                        {
                            queue.safe_enqueue(i + (_elements * index), index - 1);
                        } 
                        else
                        {
                            queue.enqueue(i + (_elements * index), index - 1);
                        }
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
                    if constexpr (
                        std::is_same_v<Queue, wait_mpsc_queue<typename Queue::value_type>> ||
                        is_overflow)
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
    for (auto i = 1; i <= 32; i *= 2)
    {
        std::cout << "\nTest with " << i << " threads\n";
        zib::benchmark::run_benchmarks(i, 1000000);
    }

    return 0;
}

/* Example Output

Test with 1 threads
dmitry_mpsc: 248968107
multi_list: 260574559
naive_queue: 145162176
overflow_mpsc_queue[normal]: 8437036
overflow_mpsc_queue[overflow]: 343425668
spin_mpsc_queue: 8799726
wait_mpsc_queue: 18506289

Test with 2 threads
dmitry_mpsc: 298044771
multi_list: 284009251
naive_queue: 275710240
overflow_mpsc_queue[normal]: 61502005
overflow_mpsc_queue[overflow]: 332162559
spin_mpsc_queue: 35076795
wait_mpsc_queue: 36393572

Test with 4 threads
dmitry_mpsc: 359110318
multi_list: 281302716
naive_queue: 709754049
overflow_mpsc_queue[normal]: 129284483
overflow_mpsc_queue[overflow]: 404591218
spin_mpsc_queue: 93744059
wait_mpsc_queue: 85257270

Test with 8 threads
dmitry_mpsc: 511026145
multi_list: 586108162
naive_queue: 1283734269
overflow_mpsc_queue[normal]: 273085780
overflow_mpsc_queue[overflow]: 732836789
spin_mpsc_queue: 263580060
wait_mpsc_queue: 198014556

Test with 16 threads
dmitry_mpsc: 1122112285
multi_list: 1781343219
naive_queue: 2427009567
overflow_mpsc_queue[normal]: 734528407
overflow_mpsc_queue[overflow]: 1584028861
spin_mpsc_queue: 836404944
wait_mpsc_queue: 698885054

Test with 32 threads
dmitry_mpsc: 2015844887
multi_list: 3440911807
naive_queue: 4696012228
overflow_mpsc_queue[normal]: 2039450883
overflow_mpsc_queue[overflow]: 4078389913
spin_mpsc_queue: 2153269691
wait_mpsc_queue: 154316575

*/
