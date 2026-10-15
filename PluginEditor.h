#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ==============================================================================
// // LUXURY LOOK AND FEEL (OBSIDIAN & CHAMPAGNE GOLD)
// ==============================================================================
class LuxuryLookAndFeel : public juce::LookAndFeel_V4
{
public:
    LuxuryLookAndFeel()
    {
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff14161c));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xffd4af37).withAlpha(0.4f));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xfff5e6c8));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff121318));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xfff5e6c8));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xffd4af37).withAlpha(0.3f));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, const float rotaryStartAngle,
                          const float rotaryEndAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(8.0f);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto lineW = 3.5f;
        auto arcRadius = radius - lineW * 0.5f;

        g.setColour(juce::Colour(0xff0b0c10));
        g.fillEllipse(bounds.reduced(4.0f));

        g.setColour(juce::Colour(0xff262936));
        g.drawEllipse(bounds.reduced(4.0f), 1.5f);

        juce::Path backgroundArc;
        backgroundArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(),
                                    arcRadius, arcRadius, 0.0f,
                                    rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff1e212b));
        g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (sliderPos > 0.0f)
        {
            juce::Path valueArc;
            valueArc.addCentredArc(bounds.getCentreX(), bounds.getCentreY(),
                                   arcRadius, arcRadius, 0.0f,
                                   rotaryStartAngle, toAngle, true);
            g.setColour(juce::Colour(0xffd4af37));
            g.strokePath(valueArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        juce::Point<float> thumbPoint(bounds.getCentreX() + (arcRadius - 8.0f) * std::cos(toAngle - juce::MathConstants<float>::halfPi),
                                      bounds.getCentreY() + (arcRadius - 8.0f) * std::sin(toAngle - juce::MathConstants<float>::halfPi));
        g.setColour(juce::Colour(0xfffff0c2));
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(thumbPoint));
    }
};

// ==============================================================================
// // UI CONTROLS & MELODYNE-STYLE BLOB VIEWER
// ==============================================================================
class AuraPitchAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    AuraPitchAudioProcessorEditor(AuraPitchAudioProcessor&);
    ~AuraPitchAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupKnob(juce::Slider& slider, juce::Label& label, const juce::String& text);

    AuraPitchAudioProcessor& audioProcessor;
    LuxuryLookAndFeel luxuryLnf;

    juce::Slider centerKnob, driftKnob, vibratoKnob, transitionKnob;
    juce::Label centerLabel, driftLabel, vibratoLabel, transitionLabel;
    juce::ComboBox rootCombo, scaleCombo;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SliderAttachment> centerAtt, driftAtt, vibratoAtt, transitionAtt;
    std::unique_ptr<ComboAttachment> rootAtt, scaleAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuraPitchAudioProcessorEditor)
};
