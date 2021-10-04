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
 *  @file dmitry_mpsc.hpp
 *
 * This is an implementation of Dmitry Vyukov's Non-intrusive mpsc queue.
 *
 * Whilst I have re-written the code in c++, I will include his license here too since it is his
 design.
 *
 * Copyright (c) 2010-2011 Dmitry Vyukov. All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without modification, are permitted
 provided that the following conditions are met:
 *
 *      1. Redistributions of source code must retain the above copyright notice, this list of
 *
 *         conditions and the following disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above copyright notice, this list
 *
 *         of conditions and the following disclaimer in the documentation and/or other materials
 *
 *        provided with the distribution.
 *
 *    THIS SOFTWARE IS PROVIDED BY DMITRY VYUKOV "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL DMITRY VYUKOV OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. *

 *    The views and conclusions contained in the software and documentation are those of the authors
 and should not be interpreted as representing official policies, either expressed or implied, of
 Dmitry Vyukov.
 *
 *
 *
 */

#ifndef ZIB_DMITRY_MPSC_HPP_
#define ZIB_DMITRY_MPSC_HPP_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace zib {

    namespace dmitry_details {
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

    }   // namespace dmitry_details

    template <typename T>
    class dmitry_mpsc {

            static constexpr auto kEmpty = std::numeric_limits<std::size_t>::max();

            static constexpr auto kAlignment =
                dmitry_details::hardware_destructive_interference_size;

            struct alignas(kAlignment) node {

                    node() : next_(nullptr) { }

                    node(T _value) : next_(nullptr), data_(_value) { }

                    std::atomic<node*> next_ alignas(kAlignment);

                    T data_;
            };

        public:

            using value_type         = T;

            dmitry_mpsc(std::uint16_t) : head_(new node), tail_(head_) { }

            ~dmitry_mpsc()
            {

                while (head_.load())
                {
                    auto tmp = head_.load();
                    head_.store(tmp->next_.load());
                    delete tmp;
                }
            }

            void
            enqueue(T _data, std::uint16_t) noexcept
            {
                auto p   = new node(_data);
                auto old = head_.exchange(p, std::memory_order_acq_rel);
                old->next_.store(p, std::memory_order_release);
            }

            std::optional<T>
            dequeue() noexcept
            { 
                auto next = tail_->next_.load(std::memory_order_acquire);

                if (next) {

                    auto tmp = tail_;

                    tail_ = next;

                    auto data = next->data_;

                    delete tmp;

                    return data;

                } else {

                    return std::nullopt;
                }
            }

        private:

            std::atomic<node*> head_ alignas(kAlignment);
            node*              tail_ alignas(kAlignment);

            char padding_[kAlignment - sizeof(tail_)];
    };

};   // namespace zib

#endif