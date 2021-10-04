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
 *  @file multi_list.hpp
 *
 * This is an implementation of multilist mpsc queue by Andreia Coorreia and Pedro Remalhete.
 * link: https://github.com/pramalhe/ConcurrencyFreaks/blob/master/papers/multilist-2017.pdf
 *
 *
 *
 */

#ifndef ZIB_MULTI_LIST_HPP_
#define ZIB_MULTI_LIST_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace zib {

    namespace multi_details {
        /* Shamelssly takend from
         * https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size
         * as in some c++ libraries it doesn't exists
         */
#ifdef __cpp_lib_hardware_interference_size
        using std::hardware_constructive_interference_size;
        using std::hardware_destructive_interference_size;
#else
        // 64 bytes on x86-64 │ L1_CACHE_BYTES │ L1_CACHE_SHIFT │ __cacheline_aligned │
        // ...
        constexpr std::size_t hardware_constructive_interference_size =
            2 * sizeof(std::max_align_t);
        constexpr std::size_t hardware_destructive_interference_size = 2 * sizeof(std::max_align_t);
#endif

    }   // namespace multi_details

    template <typename T>
    class multi_list {

            static constexpr auto kEmpty = std::numeric_limits<std::size_t>::max();

            static constexpr auto kAlignment =
                multi_details::hardware_destructive_interference_size;

            struct alignas(kAlignment) node {

                    node() : next_(nullptr), count_(kEmpty) { }

                    T data_;

                    node* next_;

                    std::atomic<std::uint64_t> count_;
            };

        public:

            using value_type         = T;

            multi_list(std::uint64_t _num_threads) : heads_(_num_threads), tails_(_num_threads), up_to_(0) { 

                for (std::size_t i = 0; i < _num_threads; ++i)
                {
                    auto* buf = new node;
                    heads_[i] = buf;
                    tails_[i] = buf;
                }
            }

            ~multi_list() { 
                for (std::size_t i = 0; i < heads_.size(); ++i)
                {

                    auto p = heads_[i];

                    while (p) {
                        auto tmp = p;
                        p = p->next_;
                        delete tmp;
                    }
                }
            }

            void
            enqueue(T _data, std::uint16_t _tid) noexcept
            {
                auto  ts     = up_to_.load();
                auto* tail   = tails_[_tid];
                tails_[_tid] = new node;
                tail->data_  = _data;
                tail->next_  = tails_[_tid];
                tail->count_.store(ts, std::memory_order_release);
                if (up_to_.load() == ts) { up_to_.fetch_add(1); }
            }

            std::optional<T>
            dequeue() noexcept
            {
                int prev = -2;
                while (true)
                {
                    std::uint64_t min_ts  = kEmpty;
                    int           min_idx = -1;
                    for (auto idx = 0u; idx < heads_.size(); ++idx)
                    {
                        auto idx_ts = heads_[idx]->count_.load();
                        if (idx_ts < min_ts)
                        {
                            min_ts  = idx_ts;
                            min_idx = idx;
                        }
                    }

                    if (min_idx == -1 && prev == min_idx) { return {}; }

                    if (prev == min_idx)
                    {
                        auto* head      = heads_[min_idx];
                        auto  data      = head->data_;
                        heads_[min_idx] = head->next_;
                        delete head;
                        return data;
                    }

                    prev = min_idx;
                }
            }

        private:

            std::vector<node*>         heads_ alignas(kAlignment);
            std::vector<node*>         tails_ alignas(kAlignment);
            std::atomic<std::uint64_t> up_to_ alignas(kAlignment);

            char                         padding_[kAlignment - sizeof(up_to_)];
    };

};   // namespace zib

#endif