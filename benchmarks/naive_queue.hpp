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
 *  @file naive_queue.hpp
 *
 */

#ifndef ZIB_NAIVE_QUEUE_HPP_
#define ZIB_NAIVE_QUEUE_HPP_


#include <queue>
#include <mutex>
#include <condition_variable>

namespace zib {

    template <typename T>
    class naive_queue {

        public:

            using value_type         = T;

            naive_queue(std::uint16_t) {};

            ~naive_queue() = default;

            void
            enqueue(T _data, std::uint16_t) noexcept
            {
                std::unique_lock lck(mtx_);
                items_.emplace_back(_data);
                if (items_.size() == 1) {
                    cv_.notify_one();
                }
            }

            T
            dequeue() noexcept
            {
                std::unique_lock lck(mtx_);

                if (!items_.size()) {
                    cv_.wait(
                        lck,
                        [this]
                        ()
                            {
                                return items_.size();
                            }
                        );
                }

                auto d = items_.front();
                items_.pop_front();
                return d;
            }

        private:

            std::deque<T> items_;
            std::mutex    mtx_;
            std::condition_variable cv_;
    };

}   // namespace zib

#endif /* ZIB_NAIVE_QUEUE_HPP_ */