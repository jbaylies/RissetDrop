/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#include "ui/RissetEditor.h"

#include <algorithm>
#include <cmath>

namespace rissetdrop
{

namespace
{
constexpr float headerHeight = 56.0f;
constexpr float footerHeight = 86.0f;
constexpr float sideWidth = 230.0f;
} // namespace

RissetEditor::Content::Content (RissetEditor& editor, RissetProcessor& p)
    : yup::Component ("RissetDropContent")
    , processor (p)
    , spiral (p)
    , rate (editor, p.rate, "Rate", theme::accent, true)
    , ramp (editor, p.ramp, "Ramp", theme::voiceColor (1))
    , loop (editor, p.loop, "Loop", theme::voiceColor (3))
    , focus (editor, p.focus, "Focus", theme::voiceColor (5))
    , voices (editor, p.voices, "Voices", theme::voiceColor (6))
    , mix (editor, p.mix, "Mix", theme::voiceColor (4))
    , tempo (editor, p.tempo, "Tempo", theme::dim)
    , sync (editor, p.sync, "Tempo source", "Manual", "Host", theme::voiceColor (4))
    , landOnGrid (editor, p.landOnGrid, "Return", "Free", "On grid", theme::voiceColor (2))
    , mode (editor, p.mode, "Mode", "Varispeed", "Pitch lock", theme::voiceColor (5))
    , live (editor, p.live, "Capture", "Freeze", "Live", theme::voiceColor (3))
{
    setOpaque (true);
    for (auto* c : std::initializer_list<yup::Component*> { &spiral, &rate, &ramp, &loop, &focus, &voices, &mix,
                                                           &tempo, &sync, &landOnGrid, &mode, &live })
        addAndMakeVisible (*c);
}

void RissetEditor::Content::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (headerHeight);
    auto footer = bounds.removeFromBottom (footerHeight).reduced (28.0f, 14.0f);

    auto left = bounds.removeFromLeft (sideWidth).reduced (18.0f, 10.0f);
    auto right = bounds.removeFromRight (sideWidth).reduced (18.0f, 10.0f);
    spiral.setBounds (bounds.reduced (6.0f));

    // Left: the motion. Rate is the big one; Capture sits under Loop.
    rate.setBounds (left.removeFromTop (left.getHeight() * 0.5f).reduced (8.0f, 0.0f));
    live.setBounds (left.removeFromBottom (46.0f).reduced (6.0f, 0.0f));
    left.removeFromBottom (6.0f);
    left.removeFromTop (8.0f);
    ramp.setBounds (left.removeFromLeft (left.getWidth() * 0.5f));
    loop.setBounds (left);

    // Right: the blend. Focus is the big one.
    focus.setBounds (right.removeFromTop (right.getHeight() * 0.5f).reduced (8.0f, 0.0f));
    right.removeFromBottom (52.0f);
    right.removeFromTop (8.0f);
    voices.setBounds (right.removeFromLeft (right.getWidth() * 0.5f));
    mix.setBounds (right);

    // Bottom: tempo, return, mode
    const float gap = 24.0f;
    const float switchWidth = (footer.getWidth() - 3.0f * gap - 90.0f) / 3.0f;
    sync.setBounds (footer.removeFromLeft (switchWidth).withSizeKeepingCenter (switchWidth, 50.0f));
    footer.removeFromLeft (gap * 0.5f);
    tempo.setBounds (footer.removeFromLeft (90.0f).withSizeKeepingCenter (90.0f, footer.getHeight() + 16.0f));
    footer.removeFromLeft (gap * 0.5f);
    landOnGrid.setBounds (footer.removeFromLeft (switchWidth).withSizeKeepingCenter (switchWidth, 50.0f));
    footer.removeFromLeft (gap);
    mode.setBounds (footer.removeFromLeft (switchWidth).withSizeKeepingCenter (switchWidth, 50.0f));
}

void RissetEditor::Content::paint (yup::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.setFillColor (theme::background);
    g.fillAll();

    // Side panels and footer
    g.setFillColor (theme::panel);
    g.fillRoundedRect (bounds.getX() + 12.0f, headerHeight, sideWidth - 18.0f, bounds.getHeight() - headerHeight - footerHeight - 6.0f, 14.0f);
    g.fillRoundedRect (bounds.getRight() - sideWidth + 6.0f, headerHeight, sideWidth - 18.0f, bounds.getHeight() - headerHeight - footerHeight - 6.0f, 14.0f);
    g.fillRoundedRect (bounds.getX() + 12.0f, bounds.getBottom() - footerHeight + 2.0f, bounds.getWidth() - 24.0f, footerHeight - 14.0f, 14.0f);

    // Header: the name, a one-line description, the tempo
    auto header = yup::Rectangle<float> (bounds.getX() + 24.0f, 0.0f, bounds.getWidth() - 48.0f, headerHeight);
    theme::drawText (g, "RISSETDROP", theme::font (24.0f), header.removeFromLeft (190.0f), theme::text, yup::Justification::centerLeft);
    theme::drawText (g, "endless accelerando / ritardando", theme::font (13.0f), header.removeFromLeft (260.0f), theme::faint,
                     yup::Justification::centerLeft);

    const auto bpm = processor.getCurrentBpm();
    const bool host = processor.isFollowingHost();
    const auto t = processor.readTelemetry();
    yup::String status = yup::String (bpm, 1) + " BPM  " + (host ? "HOST" : "MANUAL");
    if (t.phase != risset::Phase::idle)
    {
        const float rateNow = t.octavesPerSecond;
        status = (rateNow >= 0.0f ? "+" : "") + yup::String (static_cast<double> (rateNow), 2) + " oct/s   "
               + yup::String (static_cast<double> (t.loopSeconds), 2) + (t.liveLoop ? " s live loop   " : " s loop   ") + status;
    }
    theme::drawText (g, status, theme::font (13.0f), header, theme::dim, yup::Justification::centerRight);

    // Column headings
    theme::drawText (g, "MOTION", theme::font (11.0f), { bounds.getX() + 30.0f, headerHeight + 8.0f, 120.0f, 14.0f }, theme::faint,
                     yup::Justification::centerLeft);
    theme::drawText (g, "BLEND", theme::font (11.0f), { bounds.getRight() - sideWidth + 24.0f, headerHeight + 8.0f, 120.0f, 14.0f },
                     theme::faint, yup::Justification::centerLeft);
}

//==============================================================================
RissetEditor::RissetEditor (RissetProcessor& p)
    : processor (p)
{
    setOpaque (true);
    content = std::make_unique<Content> (*this, processor);
    addAndMakeVisible (*content);
    startParameterListening (processor);
    setSize (getPreferredSize().to<float>());
    startTimerHz (10);
}

RissetEditor::~RissetEditor()
{
    stopTimer();
    stopParameterListening();
}

void RissetEditor::resized()
{
    const auto design = getPreferredSize().to<float>();
    const float scale = std::max (0.1f, std::min (getWidth() / design.getWidth(), getHeight() / design.getHeight()));
    content->setBounds ({ (getWidth() - design.getWidth() * scale) * 0.5f, (getHeight() - design.getHeight() * scale) * 0.5f,
                          design.getWidth(), design.getHeight() });
    content->setTransform (yup::AffineTransform::scaling (scale));
}

void RissetEditor::paint (yup::Graphics& g)
{
    g.setFillColor (theme::background);
    g.fillAll();
}

void RissetEditor::refreshDisplay (double frameSeconds)
{
    content->spiral.tick (frameSeconds);
    content->repaint(); // the header readout follows the engine
}

void RissetEditor::timerCallback()
{
    // The manual tempo matters only when the host doesn't give one
    content->tempo.setDimmed (processor.isFollowingHost());
}

//==============================================================================
yup::AudioProcessorEditor* RissetProcessor::createEditor()
{
    return new RissetEditor (*this);
}

} // namespace rissetdrop
