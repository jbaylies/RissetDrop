/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#include "dsp/RissetEngine.h"

#include <algorithm>
#include <cmath>

namespace risset
{

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int F = Engine::positionHeadroom;

// Quintic smootherstep: 0 -> 1 with zero slope and curvature at both ends
inline double smootherstep (double u) noexcept
{
    u = std::clamp (u, 0.0, 1.0);
    return u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
}

inline double smootherstepSlope (double u) noexcept
{
    u = std::clamp (u, 0.0, 1.0);
    const double v = u * (1.0 - u);
    return 30.0 * v * v;
}

// Quintic Hermite basis for the start slope: 0 at both ends, slope 1 at the
// start, zero slope at the end, zero curvature at both ends
inline double slopeBasis (double u) noexcept
{
    const double u2 = u * u, u3 = u2 * u;
    return u - 6.0 * u3 + 8.0 * u3 * u - 3.0 * u3 * u2;
}

inline double slopeBasisSlope (double u) noexcept
{
    const double u2 = u * u;
    return 1.0 - 18.0 * u2 + 32.0 * u2 * u - 15.0 * u2 * u2;
}

/** The release trajectory, phi relative to its start, at normalised time u:
    leaves with slope m0 (octaves per release), arrives at delta at rest. */
inline double releaseCurve (double u, double m0, double delta) noexcept
{
    return m0 * slopeBasis (u) + delta * smootherstep (u);
}

// The release integral at the coarse resolution the planner scans with:
// Simpson weights and both basis functions, at compile time
constexpr int coarseIntervals = 12;

struct CoarseTable
{
    std::array<double, coarseIntervals + 1> weight {}, slope {}, smooth {};
};

constexpr CoarseTable makeCoarseTable()
{
    CoarseTable t;
    for (int k = 0; k <= coarseIntervals; ++k)
    {
        const double u = static_cast<double> (k) / coarseIntervals;
        const double u2 = u * u, u3 = u2 * u;
        t.weight[static_cast<size_t> (k)] = ((k == 0 || k == coarseIntervals) ? 1.0 : (k % 2 == 1 ? 4.0 : 2.0)) / (3.0 * coarseIntervals);
        t.slope[static_cast<size_t> (k)] = u - 6.0 * u3 + 8.0 * u3 * u - 3.0 * u3 * u2;
        t.smooth[static_cast<size_t> (k)] = u3 * (u * (u * 6.0 - 15.0) + 10.0);
    }
    return t;
}

constexpr CoarseTable coarseTable = makeCoarseTable();

inline double wrapPositive (double x, double length) noexcept
{
    x = std::fmod (x, length);
    return x < 0.0 ? x + length : x;
}

inline double wrapSymmetric (double x, double length) noexcept
{
    return wrapPositive (x + 0.5 * length, length) - 0.5 * length;
}

inline double onePoleCoefficient (double seconds, double sampleRate) noexcept
{
    return 1.0 - std::exp (-1.0 / std::max (1.0, seconds * sampleRate));
}
} // namespace

Engine::Engine() = default;

void Engine::prepare (double newSampleRate, int)
{
    sampleRate = std::max (8000.0, newSampleRate);

    // Room for two loops of the longest length plus the filter and crossfade margins
    constexpr double maxLoopSeconds = 10.0;
    history.prepare (static_cast<int64_t> (sampleRate * (2.0 * maxLoopSeconds + 0.5)));
    maxLoopSamples = std::floor (static_cast<double> (history.getCapacity() - 8192) * 0.5);

    rateCoefficient = onePoleCoefficient (0.04, sampleRate);
    controlCoefficient = onePoleCoefficient (0.02, sampleRate);
    engageFadeSamples = 0.015 * sampleRate;
    landFadeSamples = 0.030 * sampleRate;
    grainLength = std::round (0.040 * sampleRate);

    reset();
}

void Engine::reset() noexcept
{
    history.clear();
    recording = true;
    phase = Phase::idle;
    wetRamp = 0.0;
    windowAmount = 0.0;
    phiVelocity = 0.0;
    lastDrop = false;
    rateSmooth1 = rateSmooth2 = rateTarget;
    mixSmooth = mixTarget;
    focusSmooth = focusTarget;
    pitchLockSmooth = pitchLockTarget;
    gains.fill (0.0);
    publishTelemetry();
}

//==============================================================================
void Engine::process (const float* const* input, float* const* output, int numChannels, int numSamples,
                      const EngineParams& params, const Transport& transport) noexcept
{
    updateTargets (params, transport);

    if (params.drop != lastDrop)
    {
        lastDrop = params.drop;
        if (params.drop)
            startDrop (params, transport);
        else if (phase == Phase::engaging || phase == Phase::engaged)
            beginRelease (params.landOnGrid);
    }

    const bool stereo = numChannels > 1;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inLeft = input[0][i];
        const float inRight = stereo ? input[1][i] : inLeft;

        if (recording)
        {
            history.write (inLeft, inRight);
            if (phase != Phase::idle && history.getWritten() >= recordUntil)
                recording = false; // any further would overwrite the loop
        }

        rateSmooth1 += (rateTarget - rateSmooth1) * rateCoefficient;
        rateSmooth2 += (rateSmooth1 - rateSmooth2) * rateCoefficient;
        mixSmooth += (mixTarget - mixSmooth) * controlCoefficient;
        focusSmooth += (focusTarget - focusSmooth) * controlCoefficient;
        pitchLockSmooth += (pitchLockTarget - pitchLockSmooth) * controlCoefficient;

        float wetLeft = 0.0f, wetRight = 0.0f;
        if (phase != Phase::idle)
        {
            renderWet (wetLeft, wetRight);
            advanceControl();
        }

        const double fade = smootherstep (wetRamp);
        const auto amount = static_cast<float> (mixSmooth * fade);
        output[0][i] = inLeft + (wetLeft - inLeft) * amount;
        if (stereo)
            output[1][i] = inRight + (wetRight - inRight) * amount;
    }

    publishTelemetry();
}

void Engine::updateTargets (const EngineParams& params, const Transport& transport) noexcept
{
    const double bpm = std::clamp (transport.bpm, 20.0, 999.0);
    const double beatSamples = 60.0 / bpm * sampleRate;
    const double barSamples = std::max (1, transport.beatsPerBar) * beatSamples;

    rateTarget = params.rateOctavesPerBar / barSamples;
    rampTargetSamples = std::max (params.rampBeats * beatSamples, 0.01 * sampleRate);
    mixTarget = std::clamp (params.mix, 0.0, 1.0);
    focusTarget = std::clamp (params.focus, 0.0, 1.0);
    pitchLockTarget = params.pitchLock ? 1.0 : 0.0;
}

//==============================================================================
void Engine::startDrop (const EngineParams& params, const Transport& transport) noexcept
{
    if (phase == Phase::engaging || phase == Phase::engaged)
        return;

    if (phase == Phase::idle)
    {
        captureLoop (params, transport);
        engageStartVelocity = 0.0;
        engageStartWindow = 0.0;
    }
    else
    {
        // Back in from a release or landing, from wherever it has got to
        engageStartVelocity = phiVelocity;
        engageStartWindow = windowAmount;
    }

    rampSamples = rampTargetSamples;
    engageProgress = 0.0;
    phase = Phase::engaging;
}

void Engine::captureLoop (const EngineParams& params, const Transport& transport) noexcept
{
    const double beatSamples = 60.0 / std::clamp (transport.bpm, 20.0, 999.0) * sampleRate;
    loopLength = std::clamp (params.loopBeats * beatSamples, 0.05 * sampleRate, maxLoopSamples);
    crossfade = std::min (0.010 * sampleRate, 0.25 * loopLength);

    const auto written = static_cast<double> (history.getWritten());

    // The loop position now. With a transport it follows the bar grid, so the
    // loop holds whole bars and plays on in time; without one the loop is the
    // most recent audio.
    const double now = transport.hasPosition ? wrapPositive (transport.ppq * beatSamples, loopLength) : 0.0;

    // Position "now" plays what was heard one loop ago. The newest few
    // milliseconds of it are still in the mipmap filters; recording goes on
    // through the drop, so they arrive long before a voice reaches them, and
    // stops only before it would overwrite the loop.
    loopStart = written - loopLength - now;
    recordUntil = static_cast<int64_t> (std::floor (loopStart - crossfade)) - 1024 + history.getCapacity();

    grid = now;

    numVoices = std::clamp (params.voices, minVoices, maxVoices);
    centre = 0.5 * numVoices;
    centreLabel = static_cast<int> (std::floor (centre));
    phiAligned = centre - centreLabel;
    phi = phiAligned;
    phiVelocity = 0.0;
    rotation = 0;
    wraps = 0;
    master = std::ldexp (now, -(centreLabel + F)); // the 1x voice reads "now"
    windowAmount = 0.0;
    wetRamp = 0.0;

    grainPhase = 0.0;
    for (int j = 0; j < numVoices; ++j)
    {
        const double position = voicePosition (j);
        auto& start = grainStart[static_cast<size_t> (identityOf (j))];
        start[0] = position;
        start[1] = wrapPositive (position - 0.5 * grainLength, loopLength);
    }

    computeWindow();
}

//==============================================================================
double Engine::releaseError (double length, double delta, int intervals) const noexcept
{
    const double m0 = releaseStartVelocity * length;
    const double n = std::round (releaseStartPhi + delta - phiAligned);
    const int endLabel = centreLabel - static_cast<int> (n);
    if (endLabel + F < 1)
        return 1.0e300; // beyond the exact range of the position accumulator

    // Loop samples the landing voice travels during the release (Simpson's rule)
    double travelled = 0.0;
    if (intervals == coarseIntervals)
    {
        for (size_t k = 0; k <= coarseIntervals; ++k)
            travelled += coarseTable.weight[k] * std::exp2 (m0 * coarseTable.slope[k] + delta * (coarseTable.smooth[k] - 1.0));
        travelled *= length;
    }
    else
    {
        intervals += intervals % 2;
        double sum = 0.0;
        for (int k = 0; k <= intervals; ++k)
        {
            const double u = static_cast<double> (k) / intervals;
            const double weight = (k == 0 || k == intervals) ? 1.0 : (k % 2 == 1 ? 4.0 : 2.0);
            sum += weight * std::exp2 (releaseCurve (u, m0, delta) - delta);
        }
        travelled = length * sum / (3.0 * intervals);
    }

    const double landing = std::ldexp (releaseStartMaster, endLabel + F) + travelled;
    const double target = releaseStartGrid + length;
    return wrapSymmetric (landing - target, loopLength);
}

double Engine::gentlestDelta (double length) const noexcept
{
    const double m0 = releaseStartVelocity * length;
    const double above = std::ceil (releaseStartPhi - phiAligned - 1.0e-9);
    double best = 1.0e300, chosen = phiAligned + above - releaseStartPhi;
    for (int k = -3; k <= 8; ++k)
    {
        const double delta = phiAligned + above + k - releaseStartPhi;
        const double cost = releaseCost (m0, delta);
        if (cost < best - 1.0e-12)
        {
            best = cost;
            chosen = delta;
        }
    }
    return chosen;
}

double Engine::releaseCost (double m0, double delta) noexcept
{
    // Peak speed of change, and turning back counts triple: it is heard as a
    // pitch U-turn
    const double direction = m0 >= 0.0 ? 1.0 : -1.0;
    double peak = std::abs (m0), reverse = 0.0;
    for (int k = 0; k <= 32; ++k)
    {
        const double u = k / 32.0;
        const double velocity = m0 * slopeBasisSlope (u) + delta * smootherstepSlope (u);
        peak = std::max (peak, std::abs (velocity));
        reverse = std::max (reverse, -velocity * direction);
    }
    return peak + 3.0 * reverse;
}

void Engine::beginRelease (bool landOnGrid) noexcept
{
    releaseStartPhi = phi;
    releaseStartVelocity = phiVelocity;
    releaseStartWindow = windowAmount;
    releaseStartMaster = master;
    releaseStartGrid = grid;
    wrapsAtRelease = wraps;

    const double shortest = rampTargetSamples;

    // The return may never sweep faster than the drop was going (or is set
    // to go), with a floor of 1 octave per second: a short ramp then
    // stretches rather than whooshing through octaves.
    const double speedCap = 1.25 * std::max ({ std::abs (releaseStartVelocity), std::abs (rateTarget), 1.0 / sampleRate });
    const auto shortestFor = [&] (double delta)
    {
        // The smootherstep part peaks at 1.875 Delta / T
        return std::max (shortest, 1.875 * std::abs (delta) / speedCap);
    };

    double chosenDelta = gentlestDelta (shortest);
    double chosenLength = shortestFor (chosenDelta);
    releaseAligned = false;

    if (landOnGrid)
    {
        // Each aligned phase phi could land on is a candidate. For each, the
        // landing error drifts as the return lengthens; the first zero
        // crossing within three loops (or three seconds, for short loops) is
        // that candidate's return. Further targets keep rising (or falling)
        // for longer as they ease off, which sweeps the error faster. The
        // shortest return wins, nudged towards smaller sweeps.
        const double v = releaseStartVelocity;
        const double direction = v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0);
        const double above = std::ceil (releaseStartPhi - phiAligned - 1.0e-9);
        const double longest = shortest + std::max (3.0 * loopLength, 3.0 * sampleRate);
        double bestScore = 1.0e300;
        double chosenMaxLength = longest;
        double chosenMinLength = shortest;

        // Candidates: a few just behind (for short taps), and from the nearest
        // monotonic target at the shortest length, five octaves on
        double candidates[10];
        int numCandidates = 0;
        for (int k = -2; k <= 0; ++k)
            candidates[numCandidates++] = above + k + (direction < 0.0 ? 1.0 : 0.0);
        {
            const double reach = 0.5 * std::abs (v) * shortest;
            const double nearest = direction >= 0.0 ? std::ceil (releaseStartPhi + reach - phiAligned - 1.0e-9)
                                                    : std::floor (releaseStartPhi - reach - phiAligned + 1.0e-9);
            const double step = direction >= 0.0 ? 1.0 : -1.0;
            for (int k = 0; k < 7 && numCandidates < 10; ++k)
            {
                const double n = nearest + step * k;
                bool seen = false;
                for (int c = 0; c < numCandidates; ++c)
                    seen = seen || candidates[c] == n;
                if (! seen)
                    candidates[numCandidates++] = n;
            }
        }

        for (int c = 0; c < numCandidates; ++c)
        {
            const double delta = phiAligned + candidates[c] - releaseStartPhi;
            if (std::abs (delta) < 1.0e-9 && std::abs (v) < 1.0e-12)
            {
                // Already at rest on an aligned phase: aligned if the grid agrees
                if (std::abs (releaseError (shortest, delta)) < 0.5 && shortest < bestScore)
                {
                    bestScore = shortest;
                    chosenLength = shortest;
                    chosenDelta = delta;
                    releaseAligned = true;
                }
                continue;
            }

            // How long a return this target allows: monotonic ones need
            // |Delta| >= |v| T / 2; turning back is allowed only for small sweeps
            double maxLength = longest;
            if (direction != 0.0)
            {
                const bool ahead = delta * direction > 0.0;
                maxLength = std::min (maxLength, ahead ? 2.0 * std::abs (delta) / std::abs (v)
                                                       : 0.35 / std::abs (v));
            }
            // A target can only win with a return shorter than the best score allows
            maxLength = std::min (maxLength, bestScore / (1.0 + 0.25 * std::abs (delta)));
            const double minLength = shortestFor (delta);
            if (maxLength < minLength)
                continue;

            constexpr int steps = 64;
            constexpr int coarse = coarseIntervals;
            double previousLength = minLength;
            double previousError = releaseError (minLength, delta, coarse);
            double found = -1.0;

            if (std::abs (previousError) < 0.25)
                found = minLength;

            for (int i = 1; i <= steps && found < 0.0; ++i)
            {
                const double length = minLength + (maxLength - minLength) * i / steps;
                const double error = releaseError (length, delta, coarse);
                const bool crosses = (error > 0.0) != (previousError > 0.0)
                                  && std::abs (error - previousError) < 0.5 * loopLength;
                if (crosses)
                {
                    double lo = previousLength, hi = length, loError = previousError;
                    for (int b = 0; b < 30; ++b)
                    {
                        const double mid = 0.5 * (lo + hi);
                        const double midError = releaseError (mid, delta, coarse);
                        if ((midError > 0.0) == (loError > 0.0))
                        {
                            lo = mid;
                            loError = midError;
                        }
                        else
                        {
                            hi = mid;
                        }
                    }
                    const double solution = 0.5 * (lo + hi);
                    if (std::abs (releaseError (solution, delta, coarse)) < 0.5 * loopLength / steps)
                        found = solution;
                }
                previousLength = length;
                previousError = error;
            }

            if (found < 0.0)
                continue;

            const double score = found * (1.0 + 0.25 * std::abs (delta)) + shortest * releaseCost (v * found, delta);
            if (score < bestScore)
            {
                bestScore = score;
                chosenLength = found;
                chosenDelta = delta;
                chosenMaxLength = maxLength;
                chosenMinLength = minLength;
                releaseAligned = true;
            }
        }

        // The scan's coarse integral is off by up to a sample on long sweeps;
        // polish the winner with a fine one (secant steps)
        if (releaseAligned)
        {
            constexpr int fine = 256;
            double a = chosenLength, fa = releaseError (a, chosenDelta, fine);
            double b = chosenLength + 64.0, fb = releaseError (b, chosenDelta, fine);
            for (int it = 0; it < 6 && std::abs (fb) > 1.0e-4 && std::abs (fb - fa) > 1.0e-12; ++it)
            {
                const double next = b - fb * (b - a) / (fb - fa);
                a = b;
                fa = fb;
                b = std::clamp (next, chosenMinLength, chosenMaxLength);
                fb = releaseError (b, chosenDelta, fine);
            }
            if (std::abs (fb) < 0.5)
                chosenLength = b;
            else if (std::abs (fa) >= 0.5)
                releaseAligned = std::abs (releaseError (chosenLength, chosenDelta, fine)) < 0.5;
        }
    }

    releaseLength = std::max (1.0, chosenLength);
    releaseDelta = chosenDelta;
    releaseSlope = releaseStartVelocity * releaseLength;
    releaseTime = 0.0;
    phase = Phase::releasing;
}

void Engine::finishRelease() noexcept
{
    // Land exactly on the target phase
    const double unwrapped = phi + (wraps - wrapsAtRelease);
    advancePhi (releaseStartPhi + releaseDelta - unwrapped);
    if (std::abs (phi - phiAligned) < 1.0e-6 || std::abs (phi - phiAligned - 1.0) < 1.0e-6)
        phi = phiAligned;

    phiVelocity = 0.0;
    windowAmount = 0.0;
    // Measured against where the grid will be once this sample is done
    lastLandingError = wrapSymmetric (voicePosition (centreLabel) - (grid + 1.0), loopLength);

    phase = Phase::landing;
}

double Engine::getCentreVoiceGridError() const noexcept
{
    return lastLandingError;
}

//==============================================================================
void Engine::advanceControl() noexcept
{
    switch (phase)
    {
        case Phase::engaging:
        {
            engageProgress += 1.0 / rampSamples;
            const double s = smootherstep (engageProgress);
            phiVelocity = engageStartVelocity + (rateSmooth2 - engageStartVelocity) * s;
            windowAmount = engageStartWindow + (1.0 - engageStartWindow) * s;
            advancePhi (phiVelocity);
            if (engageProgress >= 1.0)
                phase = Phase::engaged;
            break;
        }

        case Phase::engaged:
            phiVelocity = rateSmooth2;
            windowAmount = 1.0;
            advancePhi (phiVelocity);
            break;

        case Phase::releasing:
        {
            releaseTime += 1.0;
            const double u = releaseTime / releaseLength;
            if (u >= 1.0)
            {
                finishRelease();
                break;
            }
            const double target = releaseStartPhi + releaseCurve (u, releaseSlope, releaseDelta);
            const double unwrapped = phi + (wraps - wrapsAtRelease);
            advancePhi (target - unwrapped);
            phiVelocity = (releaseSlope * slopeBasisSlope (u) + releaseDelta * smootherstepSlope (u)) / releaseLength;
            windowAmount = releaseStartWindow * (1.0 - smootherstep (u));
            break;
        }

        case Phase::landing:
            advancePhi (0.0);
            wetRamp -= 1.0 / landFadeSamples;
            if (wetRamp <= 0.0)
            {
                wetRamp = 0.0;
                phase = Phase::idle;
                if (! recording)
                {
                    recording = true;
                    history.beginSplice();
                }
            }
            break;

        case Phase::idle:
            break;
    }

    if (phase == Phase::engaging || phase == Phase::engaged || phase == Phase::releasing)
        wetRamp = std::min (1.0, wetRamp + 1.0 / engageFadeSamples);

    grid += 1.0;
    if (grid >= loopLength)
        grid -= loopLength;

    // Grains restart where their voice is now, half a grain apart
    const double previous = grainPhase;
    grainPhase += 1.0 / grainLength;
    const bool restartFirst = grainPhase >= 1.0;
    if (restartFirst)
        grainPhase -= 1.0;
    const bool restartSecond = previous < 0.5 && grainPhase >= 0.5;

    if (restartFirst || restartSecond)
    {
        for (int j = 0; j < numVoices; ++j)
        {
            auto& start = grainStart[static_cast<size_t> (identityOf (j))];
            const double position = voicePosition (j);
            if (restartFirst)
                start[0] = wrapPositive (position - grainPhase * grainLength, loopLength);
            if (restartSecond)
                start[1] = wrapPositive (position - (grainPhase - 0.5) * grainLength, loopLength);
        }
    }
}

void Engine::advancePhi (double delta) noexcept
{
    double next = phi + delta;

    // dM/dt = 2^(phi - C - F); trapezoidal, so long sweeps stay exact
    const double before = std::exp2 (phi - centre - F);
    const double after = std::exp2 (next - centre - F);
    master += 0.5 * (before + after);

    // Every voice climbs a slot; the top one (silent) wraps to the bottom
    while (next >= 1.0)
    {
        next -= 1.0;
        master *= 0.5;
        ++rotation;
        ++wraps;
        const double position = voicePosition (0);
        grainStart[static_cast<size_t> (identityOf (0))] = { position, position };
    }
    while (next < 0.0)
    {
        next += 1.0;
        master = std::fmod (master * 2.0, loopLength);
        --rotation;
        --wraps;
        const double position = voicePosition (numVoices - 1);
        grainStart[static_cast<size_t> (identityOf (numVoices - 1))] = { position, position };
    }

    // Any multiple of L in M moves every voice by whole loops
    if (master >= loopLength)
        master = std::fmod (master, loopLength);

    phi = next;
}

double Engine::voicePosition (int label) const noexcept
{
    return wrapPositive (std::ldexp (master, label + F), loopLength);
}

int Engine::identityOf (int label) const noexcept
{
    return ((label - rotation) % numVoices + numVoices) % numVoices;
}

//==============================================================================
void Engine::computeWindow() noexcept
{
    // Gaussian width over the ladder: focus picks the steady blend, and the
    // window narrows towards a lone 1x voice as windowAmount goes to 0.
    const double sigma = std::exp (std::log (0.9) + (std::log (0.11) - std::log (0.9)) * focusSmooth);
    const double focusWidth = 1.0 / (2.0 * sigma * sigma);
    constexpr double lockWidth = 1.0 / (2.0 * 0.02 * 0.02);
    const double k = std::exp (std::log (lockWidth) + (std::log (focusWidth) - std::log (lockWidth)) * windowAmount);
    focusK = k;

    double sum = 0.0, sumSquares = 0.0;
    for (int j = 0; j < numVoices; ++j)
    {
        const double x = (j + phi) / numVoices;
        const double s = std::sin (pi * x);
        const double d = x - 0.5;
        const double w = s * s * std::exp (-k * d * d);
        gains[static_cast<size_t> (j)] = w;
        sum += w;
        sumSquares += w * w;
    }

    // Mostly power normalisation, a quarter amplitude: the voices share
    // downbeats, so they are partly correlated. A lone voice gets exactly 1.
    const double norm = sumSquares > 1.0e-300 ? 1.0 / (std::pow (sumSquares, 0.375) * std::pow (sum, 0.25)) : 0.0;
    for (int j = 0; j < numVoices; ++j)
        gains[static_cast<size_t> (j)] *= norm;
}

void Engine::readLoop (double position, double level, float& left, float& right) const noexcept
{
    position = wrapPositive (position, loopLength);
    const double time = loopStart + position;
    history.read (level, time, left, right);

    // Across the loop point: what followed the loop end fades into its start
    const double fadeStart = loopLength - crossfade;
    if (position > fadeStart)
    {
        const double f = (position - fadeStart) / crossfade;
        const auto a = static_cast<float> (0.5 - 0.5 * std::cos (pi * f));
        float l2, r2;
        history.read (level, time - loopLength, l2, r2);
        left += (l2 - left) * a;
        right += (r2 - right) * a;
    }
}

void Engine::renderWet (float& left, float& right) noexcept
{
    computeWindow();

    const double lock = pitchLockSmooth;
    const bool varispeed = lock < 0.999;
    const bool granular = lock > 0.001;
    double sumLeft = 0.0, sumRight = 0.0;

    for (int j = 0; j < numVoices; ++j)
    {
        const double gain = gains[static_cast<size_t> (j)];
        if (gain < 1.0e-5)
            continue;

        const double octave = j + phi - centre; // log2 of the speed
        const double position = voicePosition (j);
        float l, r;

        if (varispeed)
        {
            readLoop (position, octave + 0.25, l, r);
            const double g = gain * (1.0 - lock);
            sumLeft += g * static_cast<double> (l);
            sumRight += g * static_cast<double> (r);
        }

        if (granular)
        {
            const auto& start = grainStart[static_cast<size_t> (identityOf (j))];
            for (int k = 0; k < 2; ++k)
            {
                double p = grainPhase + 0.5 * k;
                if (p >= 1.0)
                    p -= 1.0;
                const double s = std::sin (pi * p);
                readLoop (start[static_cast<size_t> (k)] + p * grainLength, 0.0, l, r);
                const double g = gain * lock * s * s;
                sumLeft += g * static_cast<double> (l);
                sumRight += g * static_cast<double> (r);
            }
        }
    }

    left = static_cast<float> (sumLeft);
    right = static_cast<float> (sumRight);
}

//==============================================================================
void Engine::publishTelemetry() noexcept
{
    auto& t = telemetry.back();
    t.phase = phase;
    t.numVoices = phase == Phase::idle ? 0 : numVoices;
    t.phi = static_cast<float> (phi);
    t.octavesPerSecond = static_cast<float> (phiVelocity * sampleRate);
    t.window = static_cast<float> (windowAmount);
    t.wet = static_cast<float> (smootherstep (wetRamp));
    t.gridPhase = static_cast<float> (grid / loopLength);
    t.loopSeconds = static_cast<float> (loopLength / sampleRate);
    t.releaseProgress = phase == Phase::releasing ? static_cast<float> (releaseTime / releaseLength) : 0.0f;
    t.releaseSeconds = static_cast<float> (releaseLength / sampleRate);
    t.releaseAligned = releaseAligned;

    for (int j = 0; j < maxVoices; ++j)
    {
        auto& v = t.voices[static_cast<size_t> (j)];
        if (j < t.numVoices)
        {
            v.logPosition = static_cast<float> ((j + phi) / numVoices);
            v.gain = static_cast<float> (gains[static_cast<size_t> (j)]);
            v.speed = static_cast<float> (std::exp2 (j + phi - centre));
            v.loopPhase = static_cast<float> (voicePosition (j) / loopLength);
            v.identity = identityOf (j);
        }
        else
        {
            v = {};
        }
    }

    telemetry.publish();
}

} // namespace risset
