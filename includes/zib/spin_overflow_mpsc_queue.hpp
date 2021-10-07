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
 *  @file spin_overflow_mpsc_queue.hpp
 *
 */

#ifndef ZIB_SPIN_OVERFLOW_MPSC_QUEUE_HPP_
#define ZIB_SPIN_OVERFLOW_MPSC_QUEUE_HPP_

#include <assert.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace zib {

    namespace spin_overflow_details {

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

        template <typename Dec, typename F>
        concept Deconstructor = requires(const Dec _dec, F* _ptr)
        {
            {
                _dec(_ptr)
            }
            noexcept->std::same_as<void>;
            {
                Dec { }
            }
            noexcept->std::same_as<Dec>;
        };

        template <typename T>
        struct deconstruct_noop {
                void
                operator()(T*) const noexcept {};
        };

        static constexpr std::size_t kDefaultMPSCSize                 = 4096;
        static constexpr std::size_t kDefaultMPSCAllocationBufferSize = 16;

    }   // namespace spin_overflow_details

    template <
        typename T,
        spin_overflow_details::Deconstructor<T> F = spin_overflow_details::deconstruct_noop<T>,
        std::size_t BufferSize                    = spin_overflow_details::kDefaultMPSCSize,
        std::size_t AllocationSize = spin_overflow_details::kDefaultMPSCAllocationBufferSize>
    class spin_overflow_mpsc_queue {

        private:

            static constexpr auto kEmpty   = std::numeric_limits<std::size_t>::max();
            static constexpr auto kUnknown = std::numeric_limits<std::size_t>::max();

            static constexpr auto kAlignment =
                spin_overflow_details::hardware_destructive_interference_size;

            struct alignas(kAlignment) node {

                    node() : count_(kEmpty) { }

                    T data_;

                    std::atomic<std::uint64_t> count_;
            };

            struct alignas(kAlignment) node_buffer {

                    node_buffer() : read_head_(0), next_(nullptr), elements_{}, write_head_(0) { }

                    std::size_t read_head_ alignas(kAlignment);

                    node_buffer* next_ alignas(kAlignment);

                    node elements_[BufferSize];

                    std::size_t write_head_ alignas(kAlignment);
            };

            struct alignas(kAlignment) allocation_pool {

                    std::atomic<std::uint64_t> read_count_ alignas(kAlignment);

                    std::atomic<std::uint64_t> write_count_ alignas(kAlignment);

                    struct alignas(kAlignment) aligned_ptr {
                            node_buffer* ptr_;
                    };

                    aligned_ptr items_[AllocationSize];

                    void
                    push(node_buffer* _ptr)
                    {
                        auto write_idx = write_count_.load(std::memory_order_relaxed);

                        if (write_idx + 1 == read_count_.load(std::memory_order_relaxed))
                        {
                            delete _ptr;
                            return;
                        }

                        _ptr->~node_buffer();
                        new (_ptr) node_buffer();

                        items_[write_idx].ptr_ = _ptr;

                        if (write_idx + 1 != AllocationSize)
                        {
                            write_count_.store(write_idx + 1, std::memory_order_release);
                        }
                        else
                        {

                            write_count_.store(0, std::memory_order_release);
                        }
                    }

                    node_buffer*
                    pop()
                    {
                        auto read_idx = read_count_.load(std::memory_order_relaxed);
                        if (read_idx == write_count_.load(std::memory_order_acquire))
                        {
                            return new node_buffer;
                        }

                        auto tmp = items_[read_idx].ptr_;

                        if (read_idx + 1 != AllocationSize)
                        {

                            read_count_.store(read_idx + 1, std::memory_order_release);
                        }
                        else
                        {

                            read_count_.store(0, std::memory_order_release);
                        }

                        return tmp;
                    }

                    node_buffer*
                    drain()
                    {
                        auto read_idx = read_count_.load(std::memory_order_relaxed);
                        if (read_idx == write_count_.load(std::memory_order_relaxed))
                        {
                            return nullptr;
                        }

                        auto tmp = items_[read_idx].ptr_;

                        if (read_idx + 1 != AllocationSize)
                        {

                            read_count_.store(read_idx + 1, std::memory_order_relaxed);
                        }
                        else
                        {

                            read_count_.store(0, std::memory_order_relaxed);
                        }

                        return tmp;
                    }
            };

            struct alignas(kAlignment) extra_node {

                    extra_node() : next_(nullptr), count_(kEmpty) { }

                    extra_node(T _value, std::uint64_t _count)
                        : next_(nullptr), count_(_count), data_(_value)
                    { }

                    std::atomic<extra_node*> next_ alignas(kAlignment);

                    std::atomic<std::uint64_t> count_ alignas(kAlignment);

                    T data_;
            };

        public:

            using value_type         = T;
            using deconstructor_type = F;

            spin_overflow_mpsc_queue(std::uint64_t _num_threads)
                : heads_(_num_threads), extra_head_(new extra_node), tails_(_num_threads),
                  up_to_(0), buffers_(_num_threads), extra_tail_(extra_head_)
            {
                for (std::size_t i = 0; i < _num_threads; ++i)
                {
                    auto* buf = new node_buffer;
                    heads_[i] = buf;
                    tails_[i] = buf;
                }
            }

            ~spin_overflow_mpsc_queue()
            {
                deconstructor_type t;

                for (auto h : heads_)
                {
                    while (h)
                    {

                        for (std::size_t i = h->read_head_; i < BufferSize; ++i)
                        {
                            if (h->elements_[i].count_.load() != kEmpty)
                            {
                                t(&h->elements_[i].data_);
                            }
                            else
                            {
                                break;
                            }
                        }

                        auto tmp = h->next_;
                        delete h;
                        h = tmp;
                    }
                }

                for (auto& q : buffers_)
                {
                    node_buffer* to_delete = nullptr;
                    while ((to_delete = q.drain()))
                    {
                        delete to_delete;
                    }
                }

                while (extra_head_)
                {
                    auto tmp    = extra_head_;
                    extra_head_ = tmp->next_.load();
                    delete tmp;
                }
            }

            void
            safe_enqueue(T _data, std::uint16_t _t_id) noexcept
            {
                if (_t_id < tails_.size()) { unsafe_enqueue(_data, _t_id); }
                else
                {

                    overflow_enqueue(_data);
                }
            }

            void
            unsafe_enqueue(T _data, std::uint16_t _t_id) noexcept
            {
                auto* buffer = tails_[_t_id];
                if (buffer->write_head_ == BufferSize - 1)
                {
                    tails_[_t_id] = buffers_[_t_id].pop();
                    buffer->next_ = tails_[_t_id];
                    assert(tails_[_t_id]);
                }

                auto cur = up_to_.load(std::memory_order_acquire);

                buffer->elements_[buffer->write_head_].data_ = _data;

                buffer->elements_[buffer->write_head_++].count_.store(
                    cur,
                    std::memory_order_release);

                if (cur == up_to_.load(std::memory_order_acq_rel))
                {
                    up_to_.fetch_add(1, std::memory_order_release);
                }
            }

            void
            overflow_enqueue(T _data)
            {
                auto cur = up_to_.load(std::memory_order_acquire);

                auto ptr = new extra_node(_data, cur);
                auto old = extra_tail_.exchange(ptr, std::memory_order_acq_rel);
                old->next_.store(ptr, std::memory_order_release);

                if (cur == up_to_.load(std::memory_order_acq_rel))
                {
                    up_to_.fetch_add(1, std::memory_order_release);
                }
            }

            void
            enqueue(T _data) noexcept
            {
                return safe_enqueue(_data, kUnknown);
            }

            std::optional<T>
            dequeue() noexcept
            {
                while (true)
                {
                    std::int64_t prev_index = -2;
                    while (true)
                    {
                        auto         min_count = kEmpty;
                        std::int64_t min_index = -1;

                        /* Check Unbounded */
                        auto extra_next = extra_head_->next_.load(std::memory_order_acquire);
                        if (extra_next)
                        {
                            min_count = extra_next->count_.load(std::memory_order_acquire);
                            min_index = prev_index;
                        }

                        /* Check bounded */
                        for (std::uint64_t i = 0; i < heads_.size(); ++i)
                        {
                            assert(heads_[i]->read_head_ < BufferSize);

                            auto count = heads_[i]->elements_[heads_[i]->read_head_].count_.load(
                                std::memory_order_acquire);

                            if (count < min_count)
                            {
                                min_count = count;
                                min_index = i;
                            }
                        }

                        if (min_index == -1 && prev_index == min_index) { return std::nullopt; }

                        if (prev_index == min_index)
                        {
                            T data;

                            if (min_index >= 0)
                            {
                                data = heads_[min_index]
                                           ->elements_[heads_[min_index]->read_head_++]
                                           .data_;

                                if (heads_[min_index]->read_head_ == BufferSize)
                                {

                                    auto tmp          = heads_[min_index];
                                    heads_[min_index] = tmp->next_;

                                    assert(heads_[min_index]);

                                    buffers_[min_index].push(tmp);
                                }
                            }
                            else
                            {
                                auto tmp    = extra_head_;
                                extra_head_ = extra_next;

                                data = extra_next->data_;

                                delete tmp;
                            }

                            return data;
                        }

                        prev_index = min_index;
                    }
                }
            }

        private:

            std::vector<node_buffer*> heads_ alignas(kAlignment);
            extra_node*               extra_head_ alignas(kAlignment);

            std::vector<node_buffer*>  tails_ alignas(kAlignment);
            std::atomic<std::uint64_t> up_to_ alignas(kAlignment);

            std::vector<allocation_pool> buffers_ alignas(kAlignment);

            std::atomic<extra_node*> extra_tail_ alignas(kAlignment);

            char padding_[kAlignment - sizeof(extra_head_)];
    };

}   // namespace zib

#endif /* ZIB_SPIN_OVERFLOW_MPSC_QUEUE_HPP_ */