/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#pragma once

#include "ui/Theme.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <functional>
#include <memory>

namespace rissetdrop::ui
{

/** A rotary knob bound to a parameter, with its name and value underneath.

    Drag up/down (shift for fine), scroll, double-click for the default, or
    click the value to type one (Enter sets it, Escape cancels). A drag is one
    host gesture. The knob draws the parameter's normalised value, so
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

    void resized() override;

    /** Dims the knob, for a control that has no effect right now. */
    void setDimmed (bool shouldBeDimmed);

private:
    /** The text field that opens over the value. */
    class ValueEditor final : public yup::TextEditor
    {
    public:
        ValueEditor() : yup::TextEditor ("ParamKnobValue") {}
        std::function<void (bool commit)> onDone;
        bool keyDown (const yup::KeyPress& key, const yup::Point<float>& position) override;
        void focusLost() override;
    };

    void setNormalized (float normalized);
    yup::Rectangle<float> getValueArea() const;
    void beginTyping();
    void endTyping (bool commit);

    yup::AudioParameter::Ptr parameter;
    yup::String caption;
    yup::Color color;
    bool bipolar;
    bool dragging = false, hover = false, dimmed = false;
    float dragStartValue = 0.0f;
    float dragStartY = 0.0f;
    bool typing = false;
    ValueEditor valueEditor;
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

/** YUP paints on a render thread; changes to which components are shown go
    through here, holding the window's graphics context, so no paint is in
    progress. Message thread only. */
void runWithGraphicsLock (yup::Component& component, const std::function<void()>& fn);

} // namespace rissetdrop::ui
