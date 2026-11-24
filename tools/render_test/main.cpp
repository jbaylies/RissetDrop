/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.

    risset_render_test: runs the engine offline over test signals and checks
    what a listener would hear as artifacts.

      risset_render_test            run every check, exit 1 on a failure
      risset_render_test <folder>   also write WAV renders there to listen to

    Checks, per scenario:
      - clicks: a pure tone in should come out with nothing above 12 kHz
        (speeds stay below 16x of 250 Hz); any step or kink is broadband
      - engage: the 1x voice plays the grid-aligned loop, so on a tone whose
        period divides the loop, wet equals dry while the drop fades in
      - landing: with Land on Grid, the 1x voice is back in phase with the
        transport, so the crossfade to dry is again between equal signals
      - rate: the acceleration (octaves/s) never jumps between blocks
      - real time: process() allocates nothing
*/

#include "dsp/RissetEngine.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <random>
#include <string>
#include <vector>

//==============================================================================
// Counts heap allocations made while the engine processes
static std::atomic<bool> countingAllocations { false };
static std::atomic<long> allocationCount { 0 };

void* operator new (std::size_t size)
{
    if (countingAllocations.load (std::memory_order_relaxed))
        allocationCount.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double risset_loop_choices[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    static Biquad highPass (double frequency, double q, double sampleRate)
    {
        const double w = 2.0 * pi * frequency / sampleRate;
        const double alpha = std::sin (w) / (2.0 * q);
        const double c = std::cos (w);
        const double a0 = 1.0 + alpha;
        Biquad f;
        f.b0 = (1.0 + c) / 2.0 / a0;
        f.b1 = -(1.0 + c) / a0;
        f.b2 = (1.0 + c) / 2.0 / a0;
        f.a1 = -2.0 * c / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

void writeWav (const std::string& path, const std::vector<float>& left, const std::vector<float>& right, int sampleRate)
{
    std::ofstream out (path, std::ios::binary);
    const auto frames = static_cast<uint32_t> (left.size());
    const uint32_t dataBytes = frames * 2 * 4;
    const auto put32 = [&] (uint32_t v) { out.write (reinterpret_cast<const char*> (&v), 4); };
    const auto put16 = [&] (uint16_t v) { out.write (reinterpret_cast<const char*> (&v), 2); };
    out.write ("RIFF", 4);
    put32 (36 + dataBytes);
    out.write ("WAVEfmt ", 8);
    put32 (16);
    put16 (3); // IEEE float
    put16 (2);
    put32 (static_cast<uint32_t> (sampleRate));
    put32 (static_cast<uint32_t> (sampleRate) * 8);
    put16 (8);
    put16 (32);
    out.write ("data", 4);
    put32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
    {
        out.write (reinterpret_cast<const char*> (&left[i]), 4);
        out.write (reinterpret_cast<const char*> (&right[i]), 4);
    }
}

//==============================================================================
enum class Signal
{
    alignedTone, // 250 Hz: a whole number of cycles per beat at 120 BPM
    seamTone,    // 251.3 Hz: the loop point lands mid-cycle
    drums,
};

float signalAt (Signal signal, int64_t n, double sampleRate, double bpm, std::mt19937& rng)
{
    const double t = static_cast<double> (n) / sampleRate;
    switch (signal)
    {
        case Signal::alignedTone: return static_cast<float> (0.5 * std::sin (2.0 * pi * 250.0 * t));
        case Signal::seamTone: return static_cast<float> (0.5 * std::sin (2.0 * pi * 251.3 * t));
        case Signal::drums:
        {
            const double beat = 60.0 / bpm;
            const double inBeat = std::fmod (t, beat);
            const double inEighth = std::fmod (t, beat * 0.5);
            const auto beatIndex = static_cast<int64_t> (t / beat);
            double y = 0.0;
            // kick on every beat
            y += 0.8 * std::sin (2.0 * pi * (50.0 * inBeat + 60.0 * (1.0 - std::exp (-inBeat * 30.0)) / 30.0 * 2.0)) * std::exp (-inBeat * 9.0);
            // snare on 2 and 4
            if (beatIndex % 2 == 1)
            {
                std::uniform_real_distribution<double> noise (-1.0, 1.0);
                y += 0.35 * noise (rng) * std::exp (-inBeat * 18.0) + 0.2 * std::sin (2.0 * pi * 190.0 * inBeat) * std::exp (-inBeat * 25.0);
            }
            // hats on eighths
            {
                std::uniform_real_distribution<double> noise (-1.0, 1.0);
                y += 0.12 * noise (rng) * std::exp (-inEighth * 80.0);
            }
            // a bass note per bar
            const double bar = std::fmod (t, beat * 4.0);
            y += 0.18 * std::sin (2.0 * pi * 55.0 * t) * std::exp (-bar * 1.2);
            return static_cast<float> (0.7 * y);
        }
    }
    return 0.0f;
}

struct Event
{
    double seconds;
    bool drop;
};

struct Scenario
{
    std::string name;
    double sampleRate = 48000.0;
    double bpm = 120.0;
    int blockSize = 256;
    Signal signal = Signal::alignedTone;
    risset::EngineParams params;
    std::vector<Event> events;
    double seconds = 20.0;
    bool hasTransport = true;
    bool expectAligned = true; // landing must match dry
    double clickLimitDb = -50.0;
    double landLimitDb = -50.0;
    std::vector<std::pair<double, double>> rateChanges; // seconds, octaves per bar
    bool fuzz = false;
};

struct Result
{
    double hfPeakDb = -240.0;
    double hfPeakSeconds = 0.0;
    int hfPeakPhase = 0;
    double engageDiffDb = -240.0;
    double landDiffDb = -240.0;
    double maxRateJump = 0.0; // octaves per second between blocks
    double landingError = 0.0;
    bool aligned = false;
    double releaseSeconds = 0.0;
    long allocations = 0;
    bool finite = true;
    double peak = 0.0;
    double worstBlockLoad = 0.0; // processing time / block duration
    int worstBlockPhase = 0;
    double worstBlockSeconds = 0.0;
};

Result run (Scenario s, const std::string& wavFolder)
{
    // Pre-roll, so there is a loop's worth of history when the first drop comes
    constexpr double preroll = 4.0;
    for (auto& e : s.events)
        e.seconds += preroll;
    for (auto& change : s.rateChanges)
        change.first += preroll;
    s.seconds += preroll;

    risset::Engine engine;
    engine.prepare (s.sampleRate, s.blockSize);

    const auto total = static_cast<int64_t> (s.seconds * s.sampleRate);
    std::vector<float> inLeft (static_cast<size_t> (s.blockSize)), inRight (inLeft.size());
    std::vector<float> outLeft (inLeft.size()), outRight (inLeft.size());
    std::vector<float> wavLeft, wavRight;
    std::mt19937 rng (1234);
    std::mt19937 fuzzRng (99);

    Biquad hp1 = Biquad::highPass (12000.0, 0.541, s.sampleRate);
    Biquad hp2 = Biquad::highPass (12000.0, 1.307, s.sampleRate);

    risset::EngineParams params = s.params;
    Result result;
    double lastRate = 0.0;
    bool haveRate = false;
    risset::Phase lastPhase = risset::Phase::idle;
    const int64_t settle = static_cast<int64_t> (0.05 * s.sampleRate);
    int64_t hpStart = 0;

    for (int64_t start = 0; start < total; start += s.blockSize)
    {
        const double now = static_cast<double> (start) / s.sampleRate;
        for (const auto& e : s.events)
            if (e.seconds <= now)
                params.drop = e.drop;
        for (const auto& [when, rate] : s.rateChanges)
            if (when <= now)
                params.rateOctavesPerBar = rate;

        const int n = static_cast<int> (std::min<int64_t> (s.blockSize, total - start));
        for (int i = 0; i < n; ++i)
        {
            const float x = signalAt (s.signal, start + i, s.sampleRate, s.bpm, rng);
            inLeft[static_cast<size_t> (i)] = x;
            inRight[static_cast<size_t> (i)] = x;
        }

        risset::Transport transport;
        transport.bpm = s.bpm;
        transport.hasPosition = s.hasTransport;
        transport.ppq = static_cast<double> (start) / s.sampleRate * s.bpm / 60.0;

        const float* inputs[] = { inLeft.data(), inRight.data() };
        float* outputs[] = { outLeft.data(), outRight.data() };

        if (s.fuzz)
        {
            // Every so often, change something at random
            std::uniform_real_distribution<double> unit (0.0, 1.0);
            if (unit (fuzzRng) < 0.01)
                params.drop = ! params.drop;
            if (unit (fuzzRng) < 0.004)
                params.rateOctavesPerBar = unit (fuzzRng) * 8.0 - 4.0;
            if (unit (fuzzRng) < 0.002)
                params.voices = 3 + static_cast<int> (unit (fuzzRng) * 6.0);
            if (unit (fuzzRng) < 0.003)
                params.focus = unit (fuzzRng);
            if (unit (fuzzRng) < 0.002)
                params.pitchLock = ! params.pitchLock;
            if (unit (fuzzRng) < 0.002)
                params.rampBeats = 0.25 + unit (fuzzRng) * 12.0;
            if (unit (fuzzRng) < 0.001)
                params.loopBeats = risset_loop_choices[static_cast<size_t> (unit (fuzzRng) * 5.99)];
            if (unit (fuzzRng) < 0.002)
                params.landOnGrid = ! params.landOnGrid;
        }

        countingAllocations = true;
        const auto before = std::chrono::steady_clock::now();
        engine.process (inputs, outputs, 2, n, params, transport);
        const auto after = std::chrono::steady_clock::now();
        countingAllocations = false;
        const double load = std::chrono::duration<double> (after - before).count() / (n / s.sampleRate);
        if (load > result.worstBlockLoad)
        {
            result.worstBlockLoad = load;
            result.worstBlockPhase = static_cast<int> (engine.getPhase());
            result.worstBlockSeconds = now - preroll;
        }

        const auto t = engine.readTelemetry();
        if (t.phase == risset::Phase::landing && lastPhase == risset::Phase::releasing)
        {
            result.landingError = engine.getCentreVoiceGridError();
            result.aligned = engine.isReleaseAligned();
            result.releaseSeconds = engine.getPlannedReleaseSamples() / s.sampleRate;
        }
        if (t.phase != risset::Phase::idle)
        {
            if (haveRate)
                result.maxRateJump = std::max (result.maxRateJump, std::abs (static_cast<double> (t.octavesPerSecond) - lastRate));
            lastRate = static_cast<double> (t.octavesPerSecond);
            haveRate = true;
        }
        else
        {
            haveRate = false;
        }

        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t> (i);
            const double y = static_cast<double> (outLeft[k]);
            if (! std::isfinite (y))
                result.finite = false;
            result.peak = std::max (result.peak, std::abs (y));

            const double hp = hp2.process (hp1.process (y));
            if (start + i > hpStart + settle && s.signal != Signal::drums)
            {
                if (toDb (std::abs (hp)) > result.hfPeakDb)
                {
                    result.hfPeakDb = toDb (std::abs (hp));
                    result.hfPeakSeconds = static_cast<double> (start + i) / s.sampleRate - preroll;
                    result.hfPeakPhase = static_cast<int> (t.phase);
                }
            }

            // While the wet fades in or out, wet should equal dry on the aligned tone
            const double diff = std::abs (y - static_cast<double> (inLeft[k]));
            if (s.signal == Signal::alignedTone)
            {
                if (t.phase == risset::Phase::engaging && t.wet < 0.999f)
                    result.engageDiffDb = std::max (result.engageDiffDb, toDb (diff));
                if (t.phase == risset::Phase::landing)
                    result.landDiffDb = std::max (result.landDiffDb, toDb (diff));
            }

            if (! wavFolder.empty())
            {
                wavLeft.push_back (outLeft[k]);
                wavRight.push_back (outRight[k]);
            }
        }
        lastPhase = t.phase;
    }

    result.allocations = allocationCount.exchange (0);

    if (! wavFolder.empty())
        writeWav (wavFolder + "/" + s.name + ".wav", wavLeft, wavRight, static_cast<int> (s.sampleRate));

    return result;
}
} // namespace

int main (int argc, char** argv)
{
    const std::string wavFolder = argc > 1 ? argv[1] : "";

    std::vector<Scenario> scenarios;
    const auto add = [&] (Scenario s) { scenarios.push_back (std::move (s)); };

    {
        Scenario s;
        s.name = "accelerate_5v";
        s.params.rateOctavesPerBar = 1.0;
        s.events = { { 2.0, true }, { 12.0, false } };
        add (s);
    }
    {
        Scenario s;
        s.name = "decelerate_5v";
        s.params.rateOctavesPerBar = -1.0;
        s.events = { { 2.0, true }, { 12.0, false } };
        add (s);
    }
    for (int v = 3; v <= 8; ++v)
    {
        Scenario s;
        s.name = "voices_" + std::to_string (v);
        s.params.voices = v;
        s.params.rateOctavesPerBar = 2.0;
        s.params.rampBeats = 2.0;
        s.events = { { 1.5, true }, { 9.3, false } };
        s.seconds = 18.0;
        add (s);
    }
    {
        Scenario s;
        s.name = "fast_rate_long_ramp";
        s.params.rateOctavesPerBar = 4.0;
        s.params.rampBeats = 16.0;
        s.events = { { 1.0, true }, { 14.0, false } };
        s.seconds = 32.0;
        add (s);
    }
    {
        Scenario s;
        s.name = "rate_through_zero";
        s.params.rateOctavesPerBar = 1.5;
        s.rateChanges = { { 5.0, 0.0 }, { 7.0, -1.5 }, { 10.0, 0.5 } };
        s.events = { { 1.0, true }, { 13.0, false } };
        add (s);
    }
    {
        Scenario s;
        s.name = "reengage_during_release";
        s.params.rateOctavesPerBar = 1.0;
        s.events = { { 1.0, true }, { 6.0, false }, { 6.9, true }, { 11.0, false } };
        s.seconds = 22.0;
        add (s);
    }
    {
        Scenario s;
        s.name = "quick_tap";
        s.params.rampBeats = 0.25;
        s.events = { { 1.0, true }, { 1.2, false }, { 1.5, true }, { 3.0, false } };
        s.seconds = 10.0;
        add (s);
    }
    {
        Scenario s;
        s.name = "short_loop_96k";
        s.sampleRate = 96000.0;
        s.params.loopBeats = 1.0;
        s.params.rateOctavesPerBar = 1.0;
        s.events = { { 1.0, true }, { 8.0, false } };
        s.seconds = 14.0;
        add (s);
    }
    {
        Scenario s;
        s.name = "odd_tempo_44k";
        s.sampleRate = 44100.0;
        s.bpm = 93.0;
        s.blockSize = 117;
        s.params.loopBeats = 2.0;
        s.events = { { 1.7, true }, { 10.0, false } };
        s.signal = Signal::seamTone; // 251.3 Hz isn't loop-periodic here: tests the seam
        s.expectAligned = false;
        s.seconds = 20.0;
        add (s);
    }
    {
        Scenario s;
        s.name = "seam_tone_narrow_focus";
        s.params.focus = 1.0;
        s.params.rateOctavesPerBar = 0.75;
        s.signal = Signal::seamTone;
        s.expectAligned = false;
        s.events = { { 1.0, true }, { 11.0, false } };
        add (s);
    }
    {
        Scenario s;
        s.name = "pitch_lock";
        s.params.pitchLock = true;
        s.params.rateOctavesPerBar = 1.0;
        s.events = { { 1.0, true }, { 10.0, false } };
        s.clickLimitDb = -40.0; // grains overlapping at unrelated positions comb a little
        s.landLimitDb = -40.0;  // grains started on the way in carry sub-sample offsets
        add (s);
    }
    {
        Scenario s;
        s.name = "no_land_on_grid";
        s.params.landOnGrid = false;
        s.expectAligned = false;
        s.events = { { 1.0, true }, { 8.3, false } };
        s.seconds = 14.0;
        add (s);
    }
    {
        Scenario s;
        s.name = "half_beat_loop_seam";
        s.params.loopBeats = 0.5; // 62.5 cycles of 250 Hz: the loop point is mid-cycle
        s.params.rateOctavesPerBar = 2.0;
        s.signal = Signal::seamTone;
        s.expectAligned = true;
        s.events = { { 1.0, true }, { 9.0, false } };
        s.seconds = 14.0;
        add (s);
    }
    {
        Scenario s;
        s.name = "fuzz_drums_2min";
        s.signal = Signal::drums;
        s.fuzz = true;
        s.expectAligned = false;
        s.blockSize = 64;
        s.seconds = 120.0;
        add (s);
    }
    {
        Scenario s;
        s.name = "fuzz_tone_2min";
        s.signal = Signal::seamTone;
        s.fuzz = true;
        s.expectAligned = false;
        s.clickLimitDb = -40.0; // pitch lock flips in and out at random
        s.blockSize = 32;
        s.seconds = 120.0;
        add (s);
    }
    {
        Scenario s;
        s.name = "no_transport";
        s.hasTransport = false;
        s.expectAligned = false;
        s.events = { { 1.0, true }, { 8.3, false } };
        s.seconds = 14.0;
        add (s);
    }

    int failures = 0;
    std::printf ("%-26s %8s %8s %8s %9s %9s %7s %6s %6s %6s\n", "scenario", "HF dB", "engage", "land", "rateJump", "gridErr", "relSec", "align", "alloc", "cpu%");
    for (const auto& s : scenarios)
    {
        const auto r = run (s, wavFolder);

        std::vector<std::string> problems;
        if (! r.finite)
            problems.push_back ("non-finite output");
        if (r.hfPeakDb > s.clickLimitDb)
            problems.push_back ("clicks");
        if (s.signal == Signal::alignedTone && r.engageDiffDb > -60.0)
            problems.push_back ("engage mismatch");
        if (s.expectAligned && s.params.landOnGrid && ! r.aligned)
            problems.push_back ("return not aligned");
        if (s.expectAligned && s.signal == Signal::alignedTone && r.landDiffDb > s.landLimitDb)
            problems.push_back ("landing mismatch");
        if (r.maxRateJump > 0.25 && ! s.fuzz)
            problems.push_back ("rate jump");
        if (r.worstBlockLoad > 0.5)
            problems.push_back ("a block took over half its real-time budget (at " + std::to_string (r.worstBlockSeconds)
                                + " s, phase " + std::to_string (r.worstBlockPhase) + ")");
        if (r.allocations != 0)
            problems.push_back ("allocates");
        if (r.peak > 1.5)
            problems.push_back ("too loud");

        std::printf ("%-26s %8.1f %8.1f %8.1f %9.4f %9.4f %7.2f %6s %6ld %6.1f  %s\n", s.name.c_str(), r.hfPeakDb, r.engageDiffDb,
                     r.landDiffDb, r.maxRateJump, r.landingError, r.releaseSeconds, r.aligned ? "yes" : "no", r.allocations,
                     100.0 * r.worstBlockLoad, problems.empty() ? "ok" : "FAIL");
        if (r.hfPeakDb > s.clickLimitDb)
            std::printf ("    HF peak at %.4f s, phase %d\n", r.hfPeakSeconds, r.hfPeakPhase);
        for (const auto& p : problems)
            std::printf ("    - %s\n", p.c_str());
        failures += problems.empty() ? 0 : 1;
    }

    // Listening renders of a drum loop
    if (! wavFolder.empty())
    {
        Scenario s;
        s.signal = Signal::drums;
        s.seconds = 26.0;
        s.events = { { 4.0, true }, { 16.0, false } };

        s.name = "drums_accelerate";
        s.params.rateOctavesPerBar = 1.0;
        run (s, wavFolder);

        s.name = "drums_decelerate";
        s.params.rateOctavesPerBar = -1.0;
        run (s, wavFolder);

        s.name = "drums_pitch_lock";
        s.params.rateOctavesPerBar = 1.0;
        s.params.pitchLock = true;
        run (s, wavFolder);
        std::printf ("\nWrote WAV renders to %s\n", wavFolder.c_str());
    }

    std::printf ("\n%d of %zu scenarios failed\n", failures, scenarios.size());
    return failures == 0 ? 0 : 1;
}
