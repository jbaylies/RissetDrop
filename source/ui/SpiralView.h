/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#pragma once

#include "plugin/RissetProcessor.h"
#include "ui/Theme.h"

namespace rissetdrop::ui
{

/** The voices on a spiral, one turn per octave, with the DROP button in the
    middle.

    Slow voices sit near the centre and fast ones outside; since they are
    octaves apart they line up on one ray, which turns as the acceleration
    carries them outwards (or inwards when slowing). The spiral glows where the
    window lets voices through. Each voice's dot grows with its gain, and its
    hand shows where it is reading in the loop. The outer ring is the loop,
    with the transport's position on it.

    tick() and paint() run on the render thread; clicks on the message thread
    only flip the Drop parameter. */
class SpiralView final : public yup::Component
{
public:
    explicit SpiralView (RissetProcessor& processor);

    /** Render thread: takes the newest telemetry. */
    void tick (double frameSeconds);

    void paint (yup::Graphics& g) override;
    void mouseDown (const yup::MouseEvent& event) override;
    void mouseMove (const yup::MouseEvent& event) override;
    void mouseExit (const yup::MouseEvent& event) override;

private:
    struct Geometry
    {
        yup::Point<float> centre;
        float radius = 0.0f;       // the whole view
        float buttonRadius = 0.0f; // the DROP button
        float spiralInner = 0.0f;
        float spiralOuter = 0.0f;
    };

    Geometry geometry() const;
    bool isOverButton (yup::Point<float> position) const;
    void paintRing (yup::Graphics& g, const Geometry& geo);
    void paintSpiral (yup::Graphics& g, const Geometry& geo, int numVoices);
    void paintVoices (yup::Graphics& g, const Geometry& geo);
    void paintIdleVoices (yup::Graphics& g, const Geometry& geo, int numVoices);
    void paintButton (yup::Graphics& g, const Geometry& geo);

    RissetProcessor& processor;
    risset::Telemetry telemetry;
    double clock = 0.0;
    float glow = 0.0f;  // eased wet, for the button
    float flash = 0.0f; // brightens the ring when a live take comes in
    int lastRefreshCount = 0;
    bool hoverButton = false;
};

} // namespace rissetdrop::ui
