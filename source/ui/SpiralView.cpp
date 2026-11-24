/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#include "ui/SpiralView.h"
#include "ui/Controls.h"

#include <algorithm>
#include <cmath>

namespace rissetdrop::ui
{

namespace
{
using theme::pi;

yup::Point<float> polar (yup::Point<float> centre, float radius, float angle)
{
    // Angle 0 is 12 o'clock, clockwise
    return { centre.getX() + radius * std::sin (angle), centre.getY() - radius * std::cos (angle) };
}

void fillCircle (yup::Graphics& g, yup::Point<float> centre, float radius)
{
    g.fillEllipse (centre.getX() - radius, centre.getY() - radius, radius * 2.0f, radius * 2.0f);
}

void strokeCircle (yup::Graphics& g, yup::Point<float> centre, float radius)
{
    g.strokeEllipse (centre.getX() - radius, centre.getY() - radius, radius * 2.0f, radius * 2.0f);
}

/** The engine's window, for drawing: the steady blend set by focus,
    narrowed by the transition amount. */
float windowAt (float x, float focus, float amount)
{
    const float sigma = std::exp (std::log (0.9f) + (std::log (0.11f) - std::log (0.9f)) * focus);
    const float focusWidth = 1.0f / (2.0f * sigma * sigma);
    const float lockWidth = 1.0f / (2.0f * 0.02f * 0.02f);
    const float k = std::exp (std::log (lockWidth) + (std::log (focusWidth) - std::log (lockWidth)) * amount);
    const float s = std::sin (pi * x);
    const float d = x - 0.5f;
    return s * s * std::exp (-k * d * d);
}

const char* phaseText (risset::Phase phase, bool aligned)
{
    switch (phase)
    {
        case risset::Phase::idle: return "READY";
        case risset::Phase::engaging: return "RAMPING IN";
        case risset::Phase::engaged: return "DROPPING";
        case risset::Phase::releasing: return aligned ? "CATCHING THE GRID" : "EASING OUT";
        case risset::Phase::landing: return "LANDED";
    }
    return "";
}
} // namespace

SpiralView::SpiralView (RissetProcessor& p)
    : yup::Component ("SpiralView")
    , processor (p)
{
    setOpaque (false);
}

void SpiralView::tick (double frameSeconds)
{
    telemetry = processor.readTelemetry();
    clock += frameSeconds;
    const float target = telemetry.wet;
    glow += (target - glow) * std::min (1.0f, static_cast<float> (frameSeconds) * 12.0f);
    if (telemetry.refreshCount != lastRefreshCount)
    {
        lastRefreshCount = telemetry.refreshCount;
        if (telemetry.refreshCount > 0)
            flash = 1.0f;
    }
    flash *= std::exp (-static_cast<float> (frameSeconds) * 6.0f);
    repaint();
}

SpiralView::Geometry SpiralView::geometry() const
{
    Geometry geo;
    const auto bounds = getLocalBounds();
    geo.centre = bounds.getCenter();
    geo.radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f - 4.0f;
    geo.buttonRadius = geo.radius * 0.24f;
    geo.spiralInner = geo.radius * 0.31f;
    geo.spiralOuter = geo.radius * 0.86f;
    return geo;
}

bool SpiralView::isOverButton (yup::Point<float> position) const
{
    const auto geo = geometry();
    return position.distanceTo (geo.centre) <= geo.buttonRadius;
}

void SpiralView::mouseDown (const yup::MouseEvent& event)
{
    if (isOverButton (event.getPosition()))
        setParameterAsGesture (*processor.drop, processor.drop->getValue() >= 0.5f ? 0.0f : 1.0f);
}

void SpiralView::mouseMove (const yup::MouseEvent& event)
{
    hoverButton = isOverButton (event.getPosition());
}

void SpiralView::mouseExit (const yup::MouseEvent&)
{
    hoverButton = false;
}

//==============================================================================
void SpiralView::paint (yup::Graphics& g)
{
    const auto geo = geometry();
    if (geo.radius < 20.0f)
        return;

    const bool active = telemetry.phase != risset::Phase::idle && telemetry.numVoices > 0;
    const int numVoices = active ? telemetry.numVoices : static_cast<int> (std::lround (processor.voices->getValue()));

    paintRing (g, geo);
    paintSpiral (g, geo, numVoices);
    if (active)
        paintVoices (g, geo);
    else
        paintIdleVoices (g, geo, numVoices);
    paintButton (g, geo);
}

void SpiralView::paintRing (yup::Graphics& g, const Geometry& geo)
{
    const float ringRadius = geo.radius * 0.95f;

    // Live, the ring is lit, and flashes as each new take comes in
    if (telemetry.liveLoop)
    {
        g.setStrokeColor (theme::voiceColor (3).withAlpha (0.35f + 0.65f * flash));
        g.setStrokeWidth (1.5f + 2.0f * flash);
    }
    else
    {
        g.setStrokeColor (theme::hairline);
        g.setStrokeWidth (1.5f);
    }
    strokeCircle (g, geo.centre, ringRadius);

    // Beat ticks around the loop
    const double loopBeats = loopBeatsFor (processor.loop->getValue());
    const int ticks = std::clamp (static_cast<int> (std::lround (loopBeats < 1.0 ? 4.0 : loopBeats)), 4, 16);
    for (int i = 0; i < ticks; ++i)
    {
        const float a = 2.0f * pi * static_cast<float> (i) / static_cast<float> (ticks);
        const float inner = i == 0 ? ringRadius - 10.0f : ringRadius - 5.0f;
        g.setStrokeColor (i == 0 ? theme::dim : theme::faint);
        g.setStrokeWidth (i == 0 ? 2.0f : 1.0f);
        g.strokeLine (polar (geo.centre, inner, a), polar (geo.centre, ringRadius, a));
    }

    // The transport's place in the loop
    if (telemetry.phase != risset::Phase::idle)
    {
        const float a = 2.0f * pi * telemetry.gridPhase;
        g.setFillColor (theme::text);
        fillCircle (g, polar (geo.centre, ringRadius, a), 4.0f);
    }
}

void SpiralView::paintSpiral (yup::Graphics& g, const Geometry& geo, int numVoices)
{
    // One turn per octave, rotated so the voices sit on the spiral. Each
    // segment is its own Path: YUP records draws and renders at the end of
    // the frame, so a Path must not change after it has been drawn.
    const float focus = processor.focus->getValue() / 100.0f;
    const float amount = telemetry.phase == risset::Phase::idle ? 1.0f : telemetry.window;
    const bool slowing = telemetry.octavesPerSecond < -0.001f;
    const auto tint = slowing ? theme::accentCool : theme::accent;

    constexpr int segmentsPerTurn = 40;
    const int segments = segmentsPerTurn * numVoices;
    const float span = geo.spiralOuter - geo.spiralInner;

    // The window peaks at 1/1 for a lone voice; scale so the full blend glows too
    float peak = 1.0e-6f;
    for (int i = 0; i <= 64; ++i)
        peak = std::max (peak, windowAt (static_cast<float> (i) / 64.0f, focus, amount));

    g.setStrokeCap (yup::StrokeCap::Butt); // round caps overlap into beads at the joints
    for (int i = 0; i < segments; ++i)
    {
        const float x0 = static_cast<float> (i) / static_cast<float> (segments);
        const float x1 = static_cast<float> (i + 1) / static_cast<float> (segments);
        const float w = windowAt (0.5f * (x0 + x1), focus, amount) / peak;

        // x * V turns from the centre out; voices at x = (j + phi) / V sit on it
        const float a0 = 2.0f * pi * x0 * static_cast<float> (numVoices);
        const float a1 = 2.0f * pi * x1 * static_cast<float> (numVoices);

        yup::Path segment;
        segment.moveTo (polar (geo.centre, geo.spiralInner + span * x0, a0));
        segment.lineTo (polar (geo.centre, geo.spiralInner + span * x1, a1));

        g.setStrokeColor (theme::faint.withAlpha (0.35f).mixedWith (tint, 0.85f * w, yup::ColorSpace::LinearRGB).withAlpha (0.25f + 0.65f * w));
        g.setStrokeWidth (1.2f + 3.2f * w);
        g.strokePath (segment);
    }
}

void SpiralView::paintVoices (yup::Graphics& g, const Geometry& geo)
{
    const float span = geo.spiralOuter - geo.spiralInner;
    const int numVoices = telemetry.numVoices;

    for (int j = 0; j < numVoices; ++j)
    {
        const auto& v = telemetry.voices[static_cast<size_t> (j)];
        const float angle = 2.0f * pi * v.logPosition * static_cast<float> (numVoices);
        const auto at = polar (geo.centre, geo.spiralInner + span * v.logPosition, angle);
        const auto color = theme::voiceColor (v.identity);
        const float gain = std::clamp (v.gain, 0.0f, 1.0f);
        const float dot = geo.radius * (0.014f + 0.045f * gain);

        g.setFillColor (color.withAlpha (0.16f * gain));
        fillCircle (g, at, dot * 2.4f);
        g.setFillColor (color.withAlpha (0.25f + 0.75f * gain));
        fillCircle (g, at, dot);

        // Where the voice is reading in the loop
        if (gain > 0.02f)
        {
            const float hand = 2.0f * pi * v.loopPhase;
            g.setStrokeColor (theme::text.withAlpha (0.4f + 0.6f * gain));
            g.setStrokeWidth (1.5f);
            g.strokeLine (at, polar (at, dot + 6.0f, hand));
        }

        if (gain > 0.25f && dot > 6.0f)
        {
            const yup::String speed = "x" + yup::String (static_cast<double> (v.speed), v.speed < 10.0f ? 2 : 1);
            const auto label = yup::Rectangle<float> (at.getX() + dot + 4.0f, at.getY() - 8.0f, 46.0f, 16.0f);
            theme::drawText (g, speed, theme::font (11.0f), label, theme::text.withAlpha (0.85f), yup::Justification::centerLeft);
        }
    }
}

void SpiralView::paintIdleVoices (yup::Graphics& g, const Geometry& geo, int numVoices)
{
    // Where the voices will start: lined up at the top, the middle one at 1x
    const float span = geo.spiralOuter - geo.spiralInner;
    const float phi = (numVoices % 2 == 0) ? 0.0f : 0.5f;
    for (int j = 0; j < numVoices; ++j)
    {
        const float x = (static_cast<float> (j) + phi) / static_cast<float> (numVoices);
        const float angle = 2.0f * pi * (static_cast<float> (j) + phi);
        const auto at = polar (geo.centre, geo.spiralInner + span * x, angle);
        g.setFillColor (theme::voiceColor (j).withAlpha (0.45f));
        fillCircle (g, at, geo.radius * 0.016f);
    }
}

void SpiralView::paintButton (yup::Graphics& g, const Geometry& geo)
{
    const bool armed = processor.drop->getValue() >= 0.5f;
    const bool slowing = telemetry.octavesPerSecond < -0.001f
                      || (telemetry.phase == risset::Phase::idle && processor.rate->getValue() < 0.0f);
    const auto tint = slowing ? theme::accentCool : theme::accent;
    const float r = geo.buttonRadius;

    // A soft halo with the wet signal, pulsing on the loop's beats
    if (glow > 0.01f)
    {
        const double loopBeats = std::max (1.0, loopBeatsFor (processor.loop->getValue()));
        const float beat = static_cast<float> (std::fmod (static_cast<double> (telemetry.gridPhase) * loopBeats, 1.0));
        const float pulse = std::exp (-beat * 5.0f);
        g.setFillColor (tint.withAlpha (0.10f * glow + 0.12f * glow * pulse));
        fillCircle (g, geo.centre, r * (1.18f + 0.06f * pulse));
    }

    const auto body = theme::panelRaised.mixedWith (tint, 0.75f * glow, yup::ColorSpace::LinearRGB);
    g.setFillColor (hoverButton ? body.brighter (0.08f) : body);
    fillCircle (g, geo.centre, r);

    g.setStrokeColor (armed ? tint : theme::hairline.brighter (0.2f));
    g.setStrokeWidth (armed ? 3.0f : 1.5f);
    strokeCircle (g, geo.centre, r);

    const auto label = glow > 0.5f ? theme::background : theme::text;
    const auto textArea = yup::Rectangle<float> (geo.centre.getX() - r, geo.centre.getY() - r * 0.36f, r * 2.0f, r * 0.56f);
    theme::drawText (g, "DROP", theme::font (r * 0.46f), textArea, label);

    const auto stateArea = yup::Rectangle<float> (geo.centre.getX() - r * 0.9f, geo.centre.getY() + r * 0.24f, r * 1.8f, r * 0.26f);
    theme::drawText (g, phaseText (telemetry.phase, telemetry.releaseAligned), theme::font (std::max (9.0f, r * 0.13f)),
                     stateArea, label.withAlpha (0.8f));
}

} // namespace rissetdrop::ui
