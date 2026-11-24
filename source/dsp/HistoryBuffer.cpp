/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#include "dsp/HistoryBuffer.h"

#include <algorithm>
#include <cmath>

namespace risset
{

namespace
{
constexpr double pi = 3.14159265358979323846;

double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 32; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

inline float cubic (float y0, float y1, float y2, float y3, float t) noexcept
{
    // Catmull-Rom
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}
} // namespace

void HistoryBuffer::prepare (int64_t minimumSamples)
{
    int64_t size = 1;
    while (size < minimumSamples)
        size <<= 1;
    size = std::max<int64_t> (size, int64_t { 1 } << (numLevels + 8));

    // Half-band low-pass: a Kaiser-windowed sinc cut at a quarter of the rate.
    // Every other tap away from the centre is exactly zero.
    constexpr double beta = 8.0;
    double sum = 0.0;
    for (int i = 0; i < numTaps; ++i)
    {
        const double n = i - centerTap;
        const double sinc = n == 0 ? 0.5 : std::sin (0.5 * pi * n) / (pi * n);
        const double r = n / centerTap;
        const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
        const double tap = (i != centerTap && (i - centerTap) % 2 == 0) ? 0.0 : sinc * window;
        taps[static_cast<size_t> (i)] = static_cast<float> (tap);
        sum += tap;
    }
    for (auto& tap : taps)
        tap = static_cast<float> (static_cast<double> (tap) / sum);

    for (int l = 0; l < numLevels; ++l)
    {
        auto& level = levels[static_cast<size_t> (l)];
        const auto levelSize = static_cast<size_t> (size >> l);
        for (auto& channel : level.data)
            channel.assign (levelSize, 0.0f);
        level.mask = static_cast<int64_t> (levelSize) - 1;
    }

    clear();
}

void HistoryBuffer::clear() noexcept
{
    double origin = 0.0, step = 1.0;
    for (auto& level : levels)
    {
        for (auto& channel : level.data)
            std::fill (channel.begin(), channel.end(), 0.0f);
        level.written = 0;
        level.origin = origin;
        level.step = step;

        // The next level's sample k is filtered around this level's sample 2k + 1 - centerTap
        origin += (1.0 - centerTap) * step;
        step *= 2.0;
    }
    spliceLeft = 0;
    lastWritten = {};
}

void HistoryBuffer::beginSplice() noexcept
{
    spliceHold = lastWritten;
    spliceLeft = spliceLength;
}

void HistoryBuffer::write (float left, float right) noexcept
{
    if (spliceLeft > 0)
    {
        // From the frame held before the pause to the live input, smoothly
        const float t = 1.0f - static_cast<float> (spliceLeft) / spliceLength;
        const float fade = t * t * (3.0f - 2.0f * t);
        left = spliceHold[0] + (left - spliceHold[0]) * fade;
        right = spliceHold[1] + (right - spliceHold[1]) * fade;
        --spliceLeft;
    }

    lastWritten = { left, right };
    push (0, left, right);
}

void HistoryBuffer::push (int l, float left, float right) noexcept
{
    auto& level = levels[static_cast<size_t> (l)];
    const auto index = static_cast<size_t> (level.written & level.mask);
    level.data[0][index] = left;
    level.data[1][index] = right;
    ++level.written;

    // An odd index completes a pair: filter and hand one sample up
    if (l + 1 >= numLevels || (level.written & 1) != 0)
        return;

    const int64_t newest = level.written - 1;
    const int64_t centre = newest - centerTap;
    const auto at = [&] (int64_t i) { return static_cast<size_t> (i & level.mask); };

    float out[numChannels];
    for (int c = 0; c < numChannels; ++c)
    {
        const auto& x = level.data[static_cast<size_t> (c)];
        float acc = taps[centerTap] * x[at (centre)];
        for (int k = 1; k <= centerTap; k += 2)
            acc += taps[static_cast<size_t> (centerTap + k)] * (x[at (centre - k)] + x[at (centre + k)]);
        out[c] = acc;
    }
    push (l + 1, out[0], out[1]);
}

double HistoryBuffer::getLatestSafeTime() const noexcept
{
    double latest = 1.0e300;
    for (const auto& level : levels)
        latest = std::min (latest, level.origin + static_cast<double> (level.written - 3) * level.step);
    return latest;
}

double HistoryBuffer::getOldestSafeTime() const noexcept
{
    double oldest = -1.0e300;
    for (const auto& level : levels)
        oldest = std::max (oldest, level.origin + static_cast<double> (level.written - (level.mask + 1) + 2) * level.step);
    return oldest;
}

void HistoryBuffer::readLevel (int l, double time, float& left, float& right) const noexcept
{
    const auto& level = levels[static_cast<size_t> (l)];
    const double position = (time - level.origin) / level.step;
    const double whole = std::floor (position);
    const auto t = static_cast<float> (position - whole);
    const auto i = static_cast<int64_t> (whole);
    const auto at = [&] (int64_t k) { return static_cast<size_t> (k & level.mask); };
    const size_t i0 = at (i - 1), i1 = at (i), i2 = at (i + 1), i3 = at (i + 2);

    const auto& a = level.data[0];
    const auto& b = level.data[1];
    left = cubic (a[i0], a[i1], a[i2], a[i3], t);
    right = cubic (b[i0], b[i1], b[i2], b[i3], t);
}

void HistoryBuffer::read (double level, double time, float& left, float& right) const noexcept
{
    level = std::clamp (level, 0.0, static_cast<double> (numLevels - 1));
    const int lower = static_cast<int> (level);
    const auto blend = static_cast<float> (level - lower);

    readLevel (lower, time, left, right);
    if (blend > 1.0e-4f && lower + 1 < numLevels)
    {
        float upperLeft, upperRight;
        readLevel (lower + 1, time, upperLeft, upperRight);
        left += (upperLeft - left) * blend;
        right += (upperRight - right) * blend;
    }
}

} // namespace risset
