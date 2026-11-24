/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#pragma once

#include <atomic>

namespace risset
{

/** Hands a value from one writer thread to one reader thread without locks
    or data races: the writer fills a back slot and swaps it into the middle,
    the reader swaps the middle out when it holds something new. Neither side
    ever waits. T must be trivially copyable. */
template <typename T>
class TripleBuffer
{
public:
    /** Writer thread only. */
    T& back() noexcept { return slots[backIndex]; }

    /** Writer thread only: publishes back(). */
    void publish() noexcept
    {
        backIndex = middle.exchange (backIndex | freshBit, std::memory_order_acq_rel) & indexMask;
    }

    /** Reader thread only: the newest published value. */
    T read() noexcept
    {
        if ((middle.load (std::memory_order_relaxed) & freshBit) != 0)
            frontIndex = middle.exchange (frontIndex, std::memory_order_acq_rel) & indexMask;
        return slots[frontIndex];
    }

private:
    static constexpr int freshBit = 4;
    static constexpr int indexMask = 3;

    T slots[3] {};
    int backIndex = 0;
    int frontIndex = 1;
    std::atomic<int> middle { 2 };
};

} // namespace risset
