/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#pragma once

#include "ui/Theme.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <memory>

namespace rissetdrop::ui
{

/** A rotary knob bound to a parameter, with its name and value underneath.

    Drag up/down (shift for fine), scroll, double-click for the default. A drag
    is one host gesture. The knob draws the parameter's normalised value, so
    any skew lives in the parameter. Bipolar knobs fill from the middle. */
class ParamKnob final : public yup::Component
{
public:
    ParamKnob (yup::AudioProcessorEditor& editor, yup::AudioParameter::Ptr parameter,
               yup::String caption, yup::Color color, bool bipolar = false);
    ~ParamKnob() override;

    void paint (yup::Graphics& g) override;
    void mouseDown (const yup::MouseEvent& event) override;
    void mouseDrag (const yup::MouseEvent& event) override;
    void mouseUp (const yup::MouseEvent& event) override;
    void mouseDoubleClick (const yup::MouseEvent& event) override;
    bool mouseWheel (const yup::MouseEvent& event, const yup::MouseWheelData& wheel) override;
    void mouseEnter (const yup::MouseEvent& event) override;
    void mouseExit (const yup::MouseEvent& event) override;

    /** Dims the knob, for a control that has no effect right now. */
    void setDimmed (bool shouldBeDimmed);

private:
    void setNormalized (float normalized);

    yup::AudioParameter::Ptr parameter;
    yup::String caption;
    yup::Color color;
    bool bipolar;
    bool dragging = false, hover = false, dimmed = false;
    float dragStartValue = 0.0f;
    float dragStartY = 0.0f;
    std::unique_ptr<yup::AudioParameterAttachment> attachment;
};

/** A two-way switch bound to a 0/1 parameter, drawn as a pill with both
    choices, the active one lit. Click anywhere to flip it. */
class ParamSwitch final : public yup::Component
{
public:
    ParamSwitch (yup::AudioProcessorEditor& editor, yup::AudioParameter::Ptr parameter,
                 yup::String caption, yup::String offText, yup::String onText, yup::Color color);
    ~ParamSwitch() override;

    void paint (yup::Graphics& g) override;
    void mouseDown (const yup::MouseEvent& event) override;
    void mouseEnter (const yup::MouseEvent& event) override;
    void mouseExit (const yup::MouseEvent& event) override;

private:
    yup::AudioParameter::Ptr parameter;
    yup::String caption, offText, onText;
    yup::Color color;
    bool hover = false;
    std::unique_ptr<yup::AudioParameterAttachment> attachment;
};

/** Sets a parameter to a real value as one host gesture. */
void setParameterAsGesture (yup::AudioParameter& parameter, float value);

} // namespace rissetdrop::ui
