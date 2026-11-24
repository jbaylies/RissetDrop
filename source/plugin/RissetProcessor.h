/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#pragma once

#include "dsp/RissetEngine.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <atomic>

namespace rissetdrop
{

/** Loop lengths offered by the Loop parameter, in beats. */
inline constexpr double loopChoices[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };

/** Stereo in, stereo out. Owns the engine and the parameters. */
class RissetProcessor final : public yup::AudioProcessor
{
public:
    RissetProcessor();
    ~RissetProcessor() override;

    // Parameters, in the order hosts list them. Their host IDs never change.
    yup::AudioParameter::Ptr drop, rate, ramp, loop, voices, focus, mix, sync, tempo, landOnGrid, mode;

    /** Any thread: what the engine is doing, for the visualiser. */
    risset::Telemetry readTelemetry() noexcept { return engine.readTelemetry(); }

    /** The tempo the last block ran at, and whether the host supplied it. */
    double getCurrentBpm() const noexcept { return currentBpm.load (std::memory_order_relaxed); }
    bool isFollowingHost() const noexcept { return followingHost.load (std::memory_order_relaxed); }
    int getBeatsPerBar() const noexcept { return beatsPerBar.load (std::memory_order_relaxed); }

    //==============================================================================
    void prepareToPlay (const yup::AudioSpec& spec) override;
    void releaseResources() override {}
    void processBlock (yup::AudioProcessContext<float>& context) override;
    void flush() override;

    bool hasEditor() const override { return true; }
    yup::AudioProcessorEditor* createEditor() override;

    int getCurrentPreset() const noexcept override { return 0; }
    void setCurrentPreset (int) noexcept override {}
    int getNumPresets() const override { return 0; }
    yup::String getPresetName (int) const override { return {}; }
    void setPresetName (int, yup::StringRef) override {}

    bool supportsDataTreeState() const noexcept override { return true; }
    yup::Result saveStateIntoDataTree (yup::DataTree& state) override;
    yup::Result loadStateFromDataTree (const yup::DataTree& state) override;

private:
    risset::Engine engine;
    bool prepared = false;

    std::atomic<double> currentBpm { 120.0 };
    std::atomic<bool> followingHost { false };
    std::atomic<int> beatsPerBar { 4 };

    YUP_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RissetProcessor)
};

} // namespace rissetdrop
