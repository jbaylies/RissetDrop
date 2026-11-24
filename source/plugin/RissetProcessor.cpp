/*
    RissetDrop - endless Risset accelerando for drops, risers and fills
    Copyright (c) 2026 RissetDrop contributors. MIT License, see LICENSE.
*/

#include "plugin/RissetProcessor.h"

#include <algorithm>
#include <cmath>

namespace rissetdrop
{

namespace
{
using Unit = yup::AudioParameter::ParameterUnit;

yup::AudioBusLayout makeBusLayout()
{
    using Bus = yup::AudioBus;
    return yup::AudioBusLayout ({ Bus ("input", Bus::Audio, Bus::Input, 2) },
                                { Bus ("output", Bus::Audio, Bus::Output, 2) });
}

yup::AudioParameter::Ptr toggle (const char* id, const char* name, yup::uint32 hostId, bool defaultOn,
                                 const char* offText, const char* onText)
{
    return yup::AudioParameterBuilder()
        .withID (id)
        .withName (name)
        .withHostID (hostId)
        .withRange (yup::NormalisableRange<float> (0.0f, 1.0f, 1.0f))
        .withDefault (defaultOn ? 1.0f : 0.0f)
        .withStepped (true)
        .withEnum (true)
        .withValueToString ([offText, onText] (float v) { return yup::String (v >= 0.5f ? onText : offText); })
        .withStringToValue ([onText] (const yup::String& text)
                            { return text.equalsIgnoreCase (onText) || text.getFloatValue() >= 0.5f ? 1.0f : 0.0f; })
        .build();
}

yup::String formatNumber (double value, int decimals)
{
    return yup::String (value, decimals);
}

yup::String describeBeats (double beats)
{
    if (beats < 1.0)
        return beats == 0.25 ? "1/4 beat" : "1/2 beat";
    if (beats == 1.0)
        return "1 beat";
    if (beats == 4.0)
        return "1 bar";
    if (beats == 8.0)
        return "2 bars";
    return formatNumber (beats, 0) + " beats";
}
} // namespace

RissetProcessor::RissetProcessor()
    : yup::AudioProcessor ("RissetDrop", makeBusLayout())
{
    drop = toggle ("drop", "Drop", 1, false, "Off", "Drop");

    // Fine control near zero: the knob's middle half covers +-1 octave per bar
    rate = yup::AudioParameterBuilder()
               .withID ("rate")
               .withName ("Rate")
               .withHostID (2)
               .withRange (yup::NormalisableRange<float> (-4.0f, 4.0f, 0.0f, 0.5f, true))
               .withDefault (1.0f)
               .withUnit (Unit::Custom, "oct/bar")
               .withValueToString ([] (float v)
                                   { return (v > 0.0005f ? "+" : "") + formatNumber (static_cast<double> (v), 2) + " oct/bar"; })
               .withStringToValue ([] (const yup::String& t) { return std::clamp (t.getFloatValue(), -4.0f, 4.0f); })
               .build();

    {
        // Skewed so 4 beats sits at the middle of the knob
        const float skew = std::log (0.5f) / std::log ((4.0f - 0.25f) / (32.0f - 0.25f));
        ramp = yup::AudioParameterBuilder()
                   .withID ("ramp")
                   .withName ("Ramp")
                   .withHostID (3)
                   .withRange (yup::NormalisableRange<float> (0.25f, 32.0f, 0.0f, skew))
                   .withDefault (4.0f)
                   .withUnit (Unit::Beats)
                   .withValueToString ([] (float v) { return formatNumber (static_cast<double> (v), v < 10.0f ? 2 : 1) + " beats"; })
                   .withStringToValue ([] (const yup::String& t) { return std::clamp (t.getFloatValue(), 0.25f, 32.0f); })
                   .build();
    }

    loop = yup::AudioParameterBuilder()
               .withID ("loop")
               .withName ("Loop")
               .withHostID (4)
               .withRange (yup::NormalisableRange<float> (0.0f, 5.0f, 1.0f))
               .withDefault (4.0f)
               .withStepped (true)
               .withEnum (true)
               .withValueToString ([] (float v)
                                   { return describeBeats (loopChoices[static_cast<size_t> (std::clamp (static_cast<int> (std::lround (v)), 0, 5))]); })
               .withStringToValue ([] (const yup::String& t)
                                   {
                                       for (int i = 0; i < 6; ++i)
                                           if (t.equalsIgnoreCase (describeBeats (loopChoices[i])))
                                               return static_cast<float> (i);
                                       return std::clamp (t.getFloatValue(), 0.0f, 5.0f);
                                   })
               .build();

    voices = yup::AudioParameterBuilder()
                 .withID ("voices")
                 .withName ("Voices")
                 .withHostID (5)
                 .withRange (yup::NormalisableRange<float> (static_cast<float> (risset::minVoices), static_cast<float> (risset::maxVoices), 1.0f))
                 .withDefault (5.0f)
                 .withStepped (true)
                 .withValueToString ([] (float v) { return yup::String (static_cast<int> (std::lround (v))) + " voices"; })
                 .withStringToValue ([] (const yup::String& t) { return static_cast<float> (std::clamp (t.getIntValue(), risset::minVoices, risset::maxVoices)); })
                 .build();

    const auto percent = [] (const char* id, const char* name, yup::uint32 hostId, float defaultValue)
    {
        return yup::AudioParameterBuilder()
            .withID (id)
            .withName (name)
            .withHostID (hostId)
            .withRange (0.0f, 100.0f)
            .withDefault (defaultValue)
            .withUnit (Unit::Percent)
            .withValueToString ([] (float v) { return yup::String (static_cast<int> (std::lround (v))) + " %"; })
            .withStringToValue ([] (const yup::String& t) { return std::clamp (t.getFloatValue(), 0.0f, 100.0f); })
            .build();
    };
    focus = percent ("focus", "Focus", 6, 35.0f);
    mix = percent ("mix", "Mix", 7, 100.0f);

    sync = toggle ("sync", "Host Sync", 8, true, "Manual", "Host");

    tempo = yup::AudioParameterBuilder()
                .withID ("tempo")
                .withName ("Tempo")
                .withHostID (9)
                .withRange (40.0f, 240.0f)
                .withDefault (120.0f)
                .withUnit (Unit::BPM)
                .withValueToString ([] (float v) { return formatNumber (static_cast<double> (v), 1) + " BPM"; })
                .withStringToValue ([] (const yup::String& t) { return std::clamp (t.getFloatValue(), 40.0f, 240.0f); })
                .build();

    landOnGrid = toggle ("land", "Land on Grid", 10, true, "Free", "Grid");
    mode = toggle ("mode", "Mode", 11, false, "Varispeed", "Pitch Lock");

    for (const auto& p : { drop, rate, ramp, loop, voices, focus, mix, sync, tempo, landOnGrid, mode })
        addParameter (p);
}

RissetProcessor::~RissetProcessor() = default;

//==============================================================================
void RissetProcessor::prepareToPlay (const yup::AudioSpec& spec)
{
    engine.prepare (static_cast<double> (spec.sampleRate), spec.maxBlockSize);
    prepared = true;
    setLatencySamples (0); // the drop plays recorded audio; nothing looks ahead
}

void RissetProcessor::flush()
{
    if (prepared)
        engine.reset();
}

void RissetProcessor::processBlock (yup::AudioProcessContext<float>& context)
{
    const yup::ScopedNoDenormals noDenormals;

    auto& buffer = context.audio;
    const int numChannels = std::min (buffer.getNumChannels(), 2);
    const int numSamples = buffer.getNumSamples();
    if (! prepared || numChannels == 0 || numSamples == 0)
        return;

    risset::EngineParams params;
    params.drop = drop->getValue() >= 0.5f;
    params.rateOctavesPerBar = static_cast<double> (rate->getValue());
    params.rampBeats = static_cast<double> (ramp->getValue());
    params.loopBeats = loopChoices[static_cast<size_t> (std::clamp (static_cast<int> (std::lround (loop->getValue())), 0, 5))];
    params.voices = static_cast<int> (std::lround (voices->getValue()));
    params.focus = static_cast<double> (focus->getValue()) / 100.0;
    params.mix = static_cast<double> (mix->getValue()) / 100.0;
    params.landOnGrid = landOnGrid->getValue() >= 0.5f;
    params.pitchLock = mode->getValue() >= 0.5f;

    risset::Transport transport;
    transport.bpm = static_cast<double> (tempo->getValue());
    bool hostTempo = false;

    if (sync->getValue() >= 0.5f && context.playHead != nullptr)
    {
        if (const auto position = context.playHead->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.has_value() && *bpm > 1.0)
            {
                transport.bpm = *bpm;
                hostTempo = true;
            }
            if (const auto signature = position->getTimeSignature())
                transport.beatsPerBar = std::max (1, signature->numerator * 4 / std::max (1, signature->denominator));
            if (const auto ppq = position->getPpqPosition(); ppq.has_value() && position->getIsPlaying())
            {
                transport.hasPosition = true;
                transport.ppq = *ppq;
            }
        }
    }

    currentBpm.store (transport.bpm, std::memory_order_relaxed);
    followingHost.store (hostTempo, std::memory_order_relaxed);
    beatsPerBar.store (transport.beatsPerBar, std::memory_order_relaxed);

    float* const* channels = buffer.getArrayOfWritePointers();
    engine.process (channels, channels, numChannels, numSamples, params, transport);

    // Anything past the stereo pair is passed through untouched
}

//==============================================================================
yup::Result RissetProcessor::saveStateIntoDataTree (yup::DataTree& state)
{
    state = yup::DataTree ("RissetDropState");
    auto transaction = state.beginTransaction();
    transaction.setProperty ("version", 1);
    for (const auto& parameter : getParameters())
        transaction.setProperty (parameter->getID(), static_cast<double> (parameter->getValue()));
    return yup::Result::ok();
}

yup::Result RissetProcessor::loadStateFromDataTree (const yup::DataTree& state)
{
    if (state.getType() != yup::Identifier ("RissetDropState"))
        return yup::Result::fail ("Not a RissetDrop state");

    for (const auto& parameter : getParameters())
    {
        const auto id = yup::Identifier (parameter->getID());
        if (state.hasProperty (id))
            parameter->setValueNotifyingHost (static_cast<float> (static_cast<double> (state.getProperty (id))));
    }
    return yup::Result::ok();
}

} // namespace rissetdrop

//==============================================================================
extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new rissetdrop::RissetProcessor();
}
