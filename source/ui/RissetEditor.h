/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#pragma once

#include "plugin/RissetProcessor.h"
#include "ui/Controls.h"
#include "ui/SpiralView.h"

#include <memory>

namespace rissetdrop
{

/** The window: the spiral and DROP button in the middle, the motion controls
    (Rate, Ramp, Loop, Capture) on the left, the blend (Focus, Voices, Mix) on the
    right, timing and mode along the bottom.

    The layout is designed at getPreferredSize() and scaled to fit, keeping
    its aspect ratio. */
class RissetEditor final : public yup::AudioProcessorEditor
                         , private yup::Timer
{
public:
    explicit RissetEditor (RissetProcessor& processor);
    ~RissetEditor() override;

    bool isResizable() const override { return true; }
    yup::Size<int> getPreferredSize() const override { return { 980, 640 }; }
    bool shouldPreserveAspectRatio() const override { return true; }

    void resized() override;
    void paint (yup::Graphics& g) override;
    void refreshDisplay (double lastFrameTimeSeconds) override;

private:
    /** Holds the controls at the design size; the editor scales it. */
    class Content final : public yup::Component
    {
    public:
        Content (RissetEditor& editor, RissetProcessor& processor);
        void resized() override;
        void paint (yup::Graphics& g) override;

        RissetProcessor& processor;
        ui::SpiralView spiral;
        ui::ParamKnob rate, ramp, loop, focus, voices, mix, tempo;
        ui::ParamSwitch sync, landOnGrid, mode, live;
    };

    void timerCallback() override;

    RissetProcessor& processor;
    std::unique_ptr<Content> content;

    YUP_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RissetEditor)
};

} // namespace rissetdrop
