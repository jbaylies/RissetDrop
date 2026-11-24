/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace risset
{

/** A stereo ring buffer of the plugin's input with a mipmap beside it.

    Level 0 holds the input as it came. Each level above holds the one below
    run through a half-band low-pass and decimated by two, so level n can be
    played back up to 2^n times faster without aliasing. A voice playing at
    speed s reads the level that matches log2(s) (see read()).

    Every level covers the same stretch of time. Times are always given in
    level-0 samples since the buffer was cleared; each level knows which time
    its samples stand for, filter delay included, so reads at different levels
    line up exactly.

    The audio thread writes and reads; prepare() allocates and must be called
    off the audio thread. */
class HistoryBuffer
{
public:
    static constexpr int numLevels = 5; // up to 16x without aliasing
    static constexpr int numChannels = 2;

    /** Allocates room for at least @p minimumSamples at level 0 and clears. */
    void prepare (int64_t minimumSamples);

    /** Silences everything and restarts time at zero. */
    void clear() noexcept;

    /** Appends one stereo frame (time getWritten()). */
    void write (float left, float right) noexcept;

    /** Starts a short splice after writing was paused, so the gap between the
        last frame before the pause and the next one isn't a step. */
    void beginSplice() noexcept;

    /** The time of the next frame write() will append. */
    int64_t getWritten() const noexcept { return levels[0].written; }

    /** The latest time every level can interpolate at. */
    double getLatestSafeTime() const noexcept;

    /** The earliest time every level can still interpolate at. */
    double getOldestSafeTime() const noexcept;

    /** Level-0 samples the buffer holds. */
    int64_t getCapacity() const noexcept { return static_cast<int64_t> (levels[0].data[0].size()); }

    /** Reads both channels at a time, with 4-point cubic interpolation, at a
        fractional level: level 1.3 blends levels 1 and 2. */
    void read (double level, double time, float& left, float& right) const noexcept;

    /** As read(), at one whole level. */
    void readLevel (int level, double time, float& left, float& right) const noexcept;

private:
    struct Level
    {
        std::array<std::vector<float>, numChannels> data;
        int64_t mask = 0;
        int64_t written = 0;
        double origin = 0.0; // the time of sample index 0
        double step = 1.0;   // level-0 samples between this level's samples
    };

    void push (int level, float left, float right) noexcept;

    static constexpr int numTaps = 31;
    static constexpr int centerTap = (numTaps - 1) / 2;

    std::array<Level, numLevels> levels;
    std::array<float, numTaps> taps {};

    int spliceLeft = 0;
    std::array<float, numChannels> lastWritten {};
    std::array<float, numChannels> spliceHold {};
    static constexpr int spliceLength = 256;
};

} // namespace risset
