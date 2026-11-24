/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#include "ui/Controls.h"

#include <algorithm>
#include <cmath>

namespace rissetdrop::ui
{

void setParameterAsGesture (yup::AudioParameter& parameter, float value)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (value);
    parameter.endChangeGesture();
}

void runWithGraphicsLock (yup::Component& component, const std::function<void()>& fn)
{
    if (auto* native = component.getNativeComponent())
        native->runWithGraphicsContext (fn);
    else
        fn();
}

namespace
{
constexpr float knobCaptionHeight = 13.0f;
constexpr float knobValueHeight = 16.0f;
constexpr float startAngle = -0.75f * theme::pi; // 7:30, measured from 12 o'clock
constexpr float endAngle = 0.75f * theme::pi;    // 4:30

yup::Point<float> onCircle (yup::Point<float> centre, float radius, float angle)
{
    // Angle 0 is 12 o'clock, clockwise
    return { centre.getX() + radius * std::sin (angle), centre.getY() - radius * std::cos (angle) };
}

yup::Path arc (yup::Point<float> centre, float radius, float from, float to)
{
    yup::Path path;
    const int segments = std::max (2, static_cast<int> (std::abs (to - from) / 0.05f));
    for (int i = 0; i <= segments; ++i)
    {
        const auto p = onCircle (centre, radius, from + (to - from) * static_cast<float> (i) / static_cast<float> (segments));
        if (i == 0)
            path.moveTo (p);
        else
            path.lineTo (p);
    }
    return path;
}
} // namespace

//==============================================================================
ParamKnob::ParamKnob (yup::AudioProcessorEditor& editor, yup::AudioParameter::Ptr p,
                      yup::String captionText, yup::Color knobColor, bool isBipolar)
    : yup::Component (captionText)
    , parameter (std::move (p))
    , caption (std::move (captionText))
    , color (knobColor)
    , bipolar (isBipolar)
{
    setOpaque (false);
    attachment = std::make_unique<yup::AudioParameterAttachment> (editor, parameter, [this] { repaint(); });

    using Style = yup::TextEditor::Style;
    valueEditor.setMultiLine (false);
    valueEditor.setFont (theme::font (14.0f));
    valueEditor.setJustification (yup::Justification::horizontalCenter);
    valueEditor.setColor (Style::backgroundColorId, theme::background);
    valueEditor.setColor (Style::outlineColorId, color);
    valueEditor.setColor (Style::focusedOutlineColorId, color);
    valueEditor.setColor (Style::textColorId, theme::text);
    valueEditor.setColor (Style::caretColorId, color);
    valueEditor.setColor (Style::selectionColorId, color.withAlpha (0.35f));
    valueEditor.onDone = [this] (bool commit) { endTyping (commit); };
    addChildComponent (valueEditor);
}

ParamKnob::~ParamKnob()
{
    if (dragging)
        parameter->endChangeGesture();
}

void ParamKnob::setDimmed (bool shouldBeDimmed)
{
    if (dimmed != shouldBeDimmed)
    {
        dimmed = shouldBeDimmed;
        repaint();
    }
}

yup::Rectangle<float> ParamKnob::getValueArea() const
{
    const auto bounds = getLocalBounds();
    const float size = std::min (bounds.getWidth(), bounds.getHeight() - knobCaptionHeight - knobValueHeight - 4.0f);
    const float radius = size * 0.5f - 3.0f;
    const float top = bounds.getY() + size * 0.5f + radius + 3.0f + knobCaptionHeight;
    return { bounds.getX(), top, bounds.getWidth(), knobValueHeight };
}

void ParamKnob::resized()
{
    const auto area = getValueArea();
    const float width = std::min (area.getWidth(), 110.0f);
    valueEditor.setBounds (area.withSizeKeepingCenter (width, 14.0f * 1.35f + 8.0f));
}

void ParamKnob::paint (yup::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const float size = std::min (bounds.getWidth(), bounds.getHeight() - knobCaptionHeight - knobValueHeight - 4.0f);
    const auto centre = yup::Point<float> (bounds.getCenterX(), bounds.getY() + size * 0.5f);
    const float radius = size * 0.5f - 3.0f;
    const float thickness = std::clamp (size * 0.07f, 3.0f, 8.0f);
    const float value = parameter->getNormalizedValue();
    const float angle = startAngle + (endAngle - startAngle) * value;
    const auto ink = dimmed ? color.withAlpha (0.35f) : color;

    g.setStrokeCap (yup::StrokeCap::Round);

    // Track
    g.setStrokeColor (theme::track);
    g.setStrokeWidth (thickness);
    g.strokePath (arc (centre, radius, startAngle, endAngle));

    // Value
    const float from = bipolar ? 0.0f : startAngle;
    if (std::abs (angle - from) > 0.01f)
    {
        g.setStrokeColor (ink);
        g.strokePath (arc (centre, radius, std::min (from, angle), std::max (from, angle)));
    }

    // Body and pointer
    const float bodyRadius = radius - thickness * 1.6f;
    g.setFillColor (hover || dragging ? theme::panelRaised.brighter (0.15f) : theme::panelRaised);
    g.fillEllipse (centre.getX() - bodyRadius, centre.getY() - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);
    g.setStrokeColor (theme::hairline);
    g.setStrokeWidth (1.0f);
    g.strokeEllipse (centre.getX() - bodyRadius, centre.getY() - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);

    g.setStrokeColor (dimmed ? theme::faint : theme::text);
    g.setStrokeWidth (std::max (2.0f, thickness * 0.6f));
    g.strokeLine (onCircle (centre, bodyRadius * 0.35f, angle), onCircle (centre, bodyRadius * 0.9f, angle));

    // Name, then value
    auto text = bounds.withTrimmedTop (centre.getY() + radius + 3.0f - bounds.getY());
    theme::drawText (g, caption.toUpperCase(), theme::font (11.0f), text.removeFromTop (knobCaptionHeight),
                     dimmed ? theme::faint : theme::dim);
    if (! typing)
        theme::drawText (g, parameter->toString(), theme::font (14.0f), text.removeFromTop (knobValueHeight),
                         dimmed ? theme::faint : theme::text);
}

void ParamKnob::beginTyping()
{
    if (typing)
        return;
    typing = true;
    runWithGraphicsLock (*this, [this]
    {
        valueEditor.setText (parameter->toString(), yup::dontSendNotification);
        valueEditor.setVisible (true);
    });
    valueEditor.takeKeyboardFocus();
    valueEditor.selectAll();
    repaint();
}

void ParamKnob::endTyping (bool commit)
{
    if (! typing)
        return;
    typing = false; // first: hiding the editor loses its focus, which ends typing again

    if (commit)
    {
        const auto text = valueEditor.getText().trim();
        if (text.isNotEmpty())
        {
            const float lowest = parameter->convertToDenormalizedValue (0.0f);
            const float highest = parameter->convertToDenormalizedValue (1.0f);
            const float value = parameter->convertFromString (text);
            if (std::isfinite (value))
                setParameterAsGesture (*parameter, std::clamp (value, std::min (lowest, highest), std::max (lowest, highest)));
        }
    }

    runWithGraphicsLock (*this, [this] { valueEditor.setVisible (false); });
    repaint();
}

bool ParamKnob::ValueEditor::keyDown (const yup::KeyPress& key, const yup::Point<float>& position)
{
    if (key.getKey() == yup::KeyPress::enterKey || key.getKey() == yup::KeyPress::escapeKey)
    {
        if (onDone)
            onDone (key.getKey() == yup::KeyPress::enterKey);
        return true;
    }
    return yup::TextEditor::keyDown (key, position);
}

void ParamKnob::ValueEditor::focusLost()
{
    yup::TextEditor::focusLost();
    if (onDone)
        onDone (true); // clicking elsewhere keeps what was typed
}

void ParamKnob::setNormalized (float normalized)
{
    parameter->setValueNotifyingHost (parameter->convertToDenormalizedValue (std::clamp (normalized, 0.0f, 1.0f)));
    repaint();
}

void ParamKnob::mouseDown (const yup::MouseEvent& event)
{
    if (getValueArea().contains (event.getPosition()))
    {
        beginTyping();
        return;
    }

    dragging = true;
    dragStartValue = parameter->getNormalizedValue();
    dragStartY = event.getPosition().getY();
    parameter->beginChangeGesture();
}

void ParamKnob::mouseDrag (const yup::MouseEvent& event)
{
    if (! dragging)
        return;
    const float range = event.getModifiers().isShiftDown() ? 900.0f : 200.0f;
    setNormalized (dragStartValue + (dragStartY - event.getPosition().getY()) / range);
}

void ParamKnob::mouseUp (const yup::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        parameter->endChangeGesture();
        repaint();
    }
}

void ParamKnob::mouseDoubleClick (const yup::MouseEvent&)
{
    setParameterAsGesture (*parameter, parameter->getDefaultValue());
    repaint();
}

bool ParamKnob::mouseWheel (const yup::MouseEvent&, const yup::MouseWheelData& wheel)
{
    const int steps = parameter->getNumSteps();
    const float step = steps > 0 ? 1.0f / static_cast<float> (steps) : 0.02f;
    const float delta = wheel.getDeltaY() > 0.0f ? step : (wheel.getDeltaY() < 0.0f ? -step : 0.0f);
    if (delta == 0.0f)
        return false;
    parameter->beginChangeGesture();
    setNormalized (parameter->getNormalizedValue() + delta);
    parameter->endChangeGesture();
    return true;
}

void ParamKnob::mouseEnter (const yup::MouseEvent&)
{
    hover = true;
    repaint();
}

void ParamKnob::mouseExit (const yup::MouseEvent&)
{
    hover = false;
    repaint();
}

//==============================================================================
ParamSwitch::ParamSwitch (yup::AudioProcessorEditor& editor, yup::AudioParameter::Ptr p,
                          yup::String captionText, yup::String off, yup::String on, yup::Color switchColor)
    : yup::Component (captionText)
    , parameter (std::move (p))
    , caption (std::move (captionText))
    , offText (std::move (off))
    , onText (std::move (on))
    , color (switchColor)
{
    setOpaque (false);
    attachment = std::make_unique<yup::AudioParameterAttachment> (editor, parameter, [this] { repaint(); });
}

ParamSwitch::~ParamSwitch() = default;

void ParamSwitch::paint (yup::Graphics& g)
{
    auto bounds = getLocalBounds();
    const float captionHeight = std::max (11.0f, bounds.getHeight() * 0.32f);
    theme::drawText (g, caption.toUpperCase(), theme::font (captionHeight * 0.85f), bounds.removeFromTop (captionHeight),
                     theme::dim, yup::Justification::centerLeft);

    const auto pill = bounds.reduced (0.0f, 3.0f);
    const float r = pill.getHeight() * 0.5f;
    g.setFillColor (hover ? theme::panelRaised.brighter (0.1f) : theme::panelRaised);
    g.fillRoundedRect (pill, r);

    const bool on = parameter->getValue() >= 0.5f;
    auto half = pill.reduced (3.0f);
    const auto left = half.removeFromLeft (half.getWidth() * 0.5f);
    const auto right = half;
    const auto lit = on ? right : left;
    g.setFillColor (color.withAlpha (0.9f));
    g.fillRoundedRect (lit, lit.getHeight() * 0.5f);

    const auto textFont = theme::font (std::max (11.0f, pill.getHeight() * 0.42f));
    theme::drawText (g, offText, textFont, left, on ? theme::dim : theme::background);
    theme::drawText (g, onText, textFont, right, on ? theme::background : theme::dim);
}

void ParamSwitch::mouseDown (const yup::MouseEvent&)
{
    setParameterAsGesture (*parameter, parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
    repaint();
}

void ParamSwitch::mouseEnter (const yup::MouseEvent&)
{
    hover = true;
    repaint();
}

void ParamSwitch::mouseExit (const yup::MouseEvent&)
{
    hover = false;
    repaint();
}

} // namespace rissetdrop::ui
