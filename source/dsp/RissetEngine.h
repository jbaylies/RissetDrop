/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#pragma once

#include "dsp/HistoryBuffer.h"
#include "dsp/TripleBuffer.h"

#include <array>
#include <cstdint>

namespace risset
{

inline constexpr int maxVoices = 8;
inline constexpr int minVoices = 3;

/** What the engine is doing. */
enum class Phase : int
{
    idle,      // dry; the history records
    engaging,  // ramping the acceleration in and widening the window
    engaged,   // steady Risset acceleration at the set rate
    releasing, // easing back to 1x on a grid-aligned trajectory
    landing,   // crossfading from the 1x loop back to the live input
};

/** Settings, in musical units, read once per block. */
struct EngineParams
{
    bool drop = false;
    double rateOctavesPerBar = 1.0; // negative slows down
    double rampBeats = 4.0;
    double loopBeats = 4.0;
    int voices = 5;          // latched when a drop starts
    double focus = 0.35;     // 0 = wide Hann blend, 1 = narrow
    double mix = 1.0;        // 0..1
    bool landOnGrid = true;  // phase-align the return to the transport
    bool pitchLock = false;  // granular: speed changes time, not pitch
    bool liveLoop = false;   // keep recording into the loop: each loop period, the newest take replaces it
};

/** The host's transport, or the manual tempo when there is none. */
struct Transport
{
    double bpm = 120.0;
    int beatsPerBar = 4;
    bool hasPosition = false; // playing, with a musical position
    double ppq = 0.0;         // quarter notes since the song start, at the block start
};

/** What the visualiser needs, published once per block. */
struct Telemetry
{
    struct Voice
    {
        float logPosition = 0.0f; // 0..1 along the octave ladder (the window's x)
        float gain = 0.0f;        // after normalisation
        float speed = 1.0f;       // playback rate, 1 = original
        float loopPhase = 0.0f;   // read position in the loop, 0..1
        int identity = 0;         // stays with a voice as it climbs, for colour
    };

    Phase phase = Phase::idle;
    int numVoices = 0;
    float phi = 0.0f;                // fractional octave phase
    float octavesPerSecond = 0.0f;   // current acceleration
    float window = 0.0f;             // 0 = only the 1x voice, 1 = full blend
    float wet = 0.0f;                // dry -> wet crossfade
    float gridPhase = 0.0f;          // transport position in the loop, 0..1
    float loopSeconds = 0.0f;
    float releaseProgress = 0.0f;    // 0..1 while releasing
    float releaseSeconds = 0.0f;     // length of the planned return
    bool releaseAligned = false;     // the return lands on the grid
    bool liveLoop = false;           // the loop refreshes from the input
    int refreshCount = 0;            // live takes swapped in since the drop started
    std::array<Voice, maxVoices> voices {};
};

/** The Risset rhythm engine. Process audio with process(); everything it does
    on the audio thread is allocation- and lock-free.

    How it works, in short (DESIGN.md has the long version):

    - The input always records into a mipmapped HistoryBuffer. A drop plays a
      loop of the last loopBeats beats, aligned to the bar grid. Frozen, that
      loop stays put; live, it moves on by a loop each loop period, so the
      voices play the newest complete take, with a short crossfade between
      takes (the read heads never cross the write head).
    - V voices play the loop one octave apart: voice j at speed
      2^(j + phi - V/2). phi climbs at the acceleration rate (octaves per
      sample) and wraps at 1, when the voices move up a slot and the top one
      becomes the bottom one. Every position comes from one accumulator M,
      P_j = 2^(j+F) M mod L, so all voices stay rhythmically locked.
    - A window over the ladder fades voices in at the bottom and out at the
      top. While ramping in or out it narrows to the single voice at 1x.
    - Engaging blends the acceleration in with smootherstep; releasing follows
      a quintic Hermite back to phi' = 0 with the 1x voice in phase with the
      transport, then crossfades to the live input. */
class Engine
{
public:
    Engine();

    /** Allocates; call off the audio thread. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Back to idle, history cleared. */
    void reset() noexcept;

    /** Processes in place or out of place. @p numChannels is 1 or 2. */
    void process (const float* const* input, float* const* output, int numChannels, int numSamples,
                  const EngineParams& params, const Transport& transport) noexcept;

    /** Any thread: the latest telemetry. */
    Telemetry readTelemetry() noexcept { return telemetry.read(); }

    Phase getPhase() const noexcept { return phase; }
    double getSampleRate() const noexcept { return sampleRate; }

    /** For tests: the planned return in samples, and whether it is aligned. */
    double getPlannedReleaseSamples() const noexcept { return releaseLength; }
    bool isReleaseAligned() const noexcept { return releaseAligned; }

    /** For tests: the loop-position error of the 1x voice against the grid, in samples. */
    double getCentreVoiceGridError() const noexcept;

    /** For tests: live takes swapped in since the drop started, and the loop length in samples. */
    int getRefreshCount() const noexcept { return refreshCount; }
    double getLoopSamples() const noexcept { return loopLength; }

    static constexpr int positionHeadroom = 16; // F: octaves of exact positions below the ladder

private:
    void updateTargets (const EngineParams& params, const Transport& transport) noexcept;
    void startDrop (const EngineParams& params, const Transport& transport) noexcept;
    void captureLoop (const EngineParams& params, const Transport& transport) noexcept;
    void beginRelease (bool landOnGrid) noexcept;
    void finishRelease() noexcept;
    void maybeRefreshLoop() noexcept;
    void finishRefresh() noexcept;
    void updateRecordLimit() noexcept;

    void advanceControl() noexcept;
    void advancePhi (double delta) noexcept;
    void renderWet (float& left, float& right) noexcept;
    void readLoop (double position, double level, float& left, float& right) const noexcept;
    void readTake (double start, double position, double level, float& left, float& right) const noexcept;
    void computeWindow() noexcept;

    double voicePosition (int label) const noexcept;
    int identityOf (int label) const noexcept;
    double releaseError (double length, double delta, int intervals = 24) const noexcept;
    double gentlestDelta (double length) const noexcept;
    static double releaseCost (double m0, double delta) noexcept;
    void publishTelemetry() noexcept;

    double sampleRate = 48000.0;
    double maxLoopSamples = 0.0;     // two of them fit in the history
    double recordGuard = 0.0;        // samples kept clear between the write head and the oldest read
    HistoryBuffer history;
    bool recording = true;
    int64_t recordUntil = 0; // during a drop, the last history time that spares the loop

    Phase phase = Phase::idle;

    // Loop
    double loopStart = 0.0;  // A, history time of loop position 0
    double loopLength = 1.0; // L, samples
    double crossfade = 1.0;  // X, samples of loop-boundary crossfade
    double grid = 0.0;       // D, the transport's position in the loop

    // Live loop: the previous take fades out as the newest fades in
    bool liveTarget = false;
    bool refreshing = false;
    double previousLoopStart = 0.0;
    double loopStartReal = 0.0;   // A plus the samples not recorded before it: where it sits on the grid
    double unrecorded = 0.0;      // samples not recorded since reset (recording paused mid-drop)
    double resumedAt = 0.0;       // history time after which recording is continuous again
    double refreshProgress = 0.0; // 0..1 through the take crossfade
    double refreshMix = 1.0;      // its gain on the new take
    double refreshFadeSamples = 1.0;
    int refreshCount = 0;

    // Ladder
    int numVoices = 5;
    double centre = 2.5;     // C = V/2
    double phiAligned = 0.5; // phi when a voice sits at 1x in the middle
    int centreLabel = 2;     // its label then
    double phi = 0.5;
    double phiVelocity = 0.0; // octaves per sample
    double master = 0.0;      // M
    int rotation = 0;

    // Window
    double windowAmount = 0.0; // e
    double focusK = 0.0;
    std::array<double, maxVoices> gains {};

    // Transitions
    double rampSamples = 1.0;
    double engageProgress = 0.0;
    double engageStartVelocity = 0.0;
    double engageStartWindow = 0.0;

    double releaseLength = 1.0; // T
    double releaseTime = 0.0;
    double releaseStartPhi = 0.0;
    double releaseDelta = 0.0;
    double releaseSlope = 0.0;  // m0 = v T
    double releaseStartWindow = 0.0;
    double releaseStartVelocity = 0.0;
    double releaseStartMaster = 0.0;
    double releaseStartGrid = 0.0;
    int wraps = 0;              // net octave wraps since the drop started
    int wrapsAtRelease = 0;
    bool releaseAligned = false;
    double lastLandingError = 0.0;

    double wetRamp = 0.0; // linear 0..1, shaped when applied
    double engageFadeSamples = 1.0;
    double landFadeSamples = 1.0;

    // Smoothed controls
    double rateTarget = 0.0, rateSmooth1 = 0.0, rateSmooth2 = 0.0; // octaves per sample
    double rateCoefficient = 0.0;
    double mixTarget = 1.0, mixSmooth = 1.0;
    double focusTarget = 0.35, focusSmooth = 0.35;
    double pitchLockTarget = 0.0, pitchLockSmooth = 0.0;
    double controlCoefficient = 0.0;
    double rampTargetSamples = 1.0;
    bool lastDrop = false;

    // Granular pitch lock: two overlapping grains per voice identity
    double grainLength = 1.0;
    double grainPhase = 0.0;
    std::array<std::array<double, 2>, maxVoices> grainStart {};

    TripleBuffer<Telemetry> telemetry;
};

} // namespace risset
