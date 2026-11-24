/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#pragma once

#include <yup_gui/yup_gui.h>

#include <array>
#include <cmath>

namespace rissetdrop::theme
{

// Ink-dark, with a warm accent for the drop and a spectrum along the ladder
inline constexpr yup::Color background { 0xff0e1016 };
inline constexpr yup::Color panel { 0xff151821 };
inline constexpr yup::Color panelRaised { 0xff1d212d };
inline constexpr yup::Color hairline { 0xff2a2f3d };
inline constexpr yup::Color track { 0xff2c3241 };
inline constexpr yup::Color text { 0xffe8ebf2 };
inline constexpr yup::Color dim { 0xff9aa2b4 };
inline constexpr yup::Color faint { 0xff5c6477 };
inline constexpr yup::Color accent { 0xffff7a45 };    // the drop
inline constexpr yup::Color accentCool { 0xff5fd3e0 }; // slowing down

/** One colour per voice identity, so a voice keeps its colour as it climbs. */
inline yup::Color voiceColor (int identity)
{
    static constexpr std::array<yup::uint32, 8> colors { 0xffff7a45, 0xffffc145, 0xffb4e05a, 0xff4fd6a8,
                                                         0xff5fb4ff, 0xff9a7cff, 0xffe46cf0, 0xffff5f8f };
    return yup::Color (colors[static_cast<size_t> (((identity % 8) + 8) % 8)]);
}

inline yup::Font font (float height)
{
    return yup::ApplicationTheme::getGlobalTheme()->getDefaultFont().withHeight (height);
}

inline void drawText (yup::Graphics& g, const yup::String& label, const yup::Font& f,
                      yup::Rectangle<float> area, yup::Color color,
                      yup::Justification justification = yup::Justification::center)
{
    if (label.isEmpty() || area.getWidth() <= 1.0f || area.getHeight() <= 1.0f)
        return;
    g.setFillColor (color);
    g.fillFittedText (label, f, area, justification);
}

inline constexpr float pi = 3.14159265358979f;

} // namespace rissetdrop::theme
