#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==============================================================================
// // UI IMPLEMENTATION & REAL-TIME GRAPHICS
// ==============================================================================
AuraPitchAudioProcessorEditor::AuraPitchAudioProcessorEditor(AuraPitchAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&luxuryLnf);
    setSize(760, 480);

    setupKnob(centerKnob, centerLabel, "PITCH CENTER");
    setupKnob(driftKnob, driftLabel, "PITCH DRIFT");
    setupKnob(vibratoKnob, vibratoLabel, "NATURAL EXPR");
    setupKnob(transitionKnob, transitionLabel, "TRANSITION");

    addAndMakeVisible(rootCombo);
    rootCombo.addItemList({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    
    addAndMakeVisible(scaleCombo);
    scaleCombo.addItemList({ "Chromatic (Auto-Free)", "Major", "Minor (Urban/Natural)" }, 1);

    centerAtt     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "CENTER_SNAP", centerKnob);
    driftAtt      = std::make_unique<SliderAttachment>(audioProcessor.apvts, "DRIFT_CORR", driftKnob);
    vibratoAtt    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "VIBRATO_PRESERVE", vibratoKnob);
    transitionAtt = std::make_unique<SliderAttachment>(audioProcessor.apvts, "TRANSITION_SMOOTH", transitionKnob);
    rootAtt       = std::make_unique<ComboAttachment>(audioProcessor.apvts, "ROOT_NOTE", rootCombo);
    scaleAtt      = std::make_unique<ComboAttachment>(audioProcessor.apvts, "SCALE_TYPE", scaleCombo);

    startTimerHz(30);
}

AuraPitchAudioProcessorEditor::~AuraPitchAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void AuraPitchAudioProcessorEditor::setupKnob(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffd4af37));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, juce::Colour(0xff8e93a4));
    addAndMakeVisible(label);
}

void AuraPitchAudioProcessorEditor::timerCallback()
{
    repaint();
}

void AuraPitchAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient bgGrad(juce::Colour(0xff0d0e12), 0.0f, 0.0f,
                                juce::Colour(0xff151821), 0.0f, static_cast<float>(getHeight()), false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    g.setColour(juce::Colour(0xffd4af37).withAlpha(0.7f));
    g.drawRect(getLocalBounds().reduced(6), 1.0f);

    g.setColour(juce::Colour(0xfff5e6c8));
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("AURAPITCH PRO", 26, 18, 260, 28, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xffd4af37));
    g.setFont(juce::FontOptions(11.0f, juce::Font::plain));
    g.drawText("ORGANIC VOCAL PITCH & FORMANT ENGINE", 28, 42, 280, 16, juce::Justification::centredLeft);

    bool sibilantProtected = audioProcessor.isSibilantBypass.load();
    juce::Rectangle<float> badgeBounds(325.0f, 22.0f, 155.0f, 26.0f);
    g.setColour(sibilantProtected ? juce::Colour(0xff26d0ce).withAlpha(0.2f) : juce::Colour(0xff181b24));
    g.fillRoundedRectangle(badgeBounds, 6.0f);
    g.setColour(sibilantProtected ? juce::Colour(0xff26d0ce) : juce::Colour(0xff3a3f50));
    g.drawRoundedRectangle(badgeBounds, 6.0f, 1.0f);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(sibilantProtected ? "SIBILANT GUARD: ON" : "TRANSPARENT GUARD", badgeBounds, juce::Justification::centred);

    // ==============================================================================
    // // MELODYNE BLOB GRAPHICAL DISPLAY
    // ==============================================================================
    juce::Rectangle<float> displayRect(26.0f, 72.0f, static_cast<float>(getWidth() - 52), 220.0f);
    g.setColour(juce::Colour(0xff090a0d));
    g.fillRoundedRectangle(displayRect, 8.0f);

    g.setColour(juce::Colour(0xff191c26));
    int numLanes = 7;
    float laneHeight = displayRect.getHeight() / static_cast<float>(numLanes);
    for (int i = 1; i < numLanes; ++i)
    {
        float y = displayRect.getY() + i * laneHeight;
        g.drawHorizontalLine(static_cast<int>(y), displayRect.getX(), displayRect.getRight());
    }

    const auto& history = audioProcessor.visualHistory;
    int histSize = static_cast<int>(history.size());
    int readIdx = audioProcessor.historyWriteIndex;
    float centerMidiRef = std::round(audioProcessor.currentMidiNote.load());

    juce::Path originalCurve;
    juce::Path correctedCurve;
    bool pathStarted = false;

    float stepX = displayRect.getWidth() / static_cast<float>(histSize);

    for (int i = 0; i < histSize; ++i)
    {
        int idx = (readIdx + i) % histSize;
        const auto& pt = history[idx];
        float x = displayRect.getX() + i * stepX;

        if (pt.voiced && pt.amplitude > 0.02f)
        {
            float relOrig = juce::jlimit(-3.2f, 3.2f, pt.originalMidi - centerMidiRef);
            float relCorr = juce::jlimit(-3.2f, 3.2f, pt.correctedMidi - centerMidiRef);

            float yOrig = displayRect.getCentreY() - (relOrig * laneHeight);
            float yCorr = displayRect.getCentreY() - (relCorr * laneHeight);
            float blobThickness = pt.amplitude * (laneHeight * 0.85f);

            g.setColour(juce::Colour(0xffd4af37).withAlpha(0.32f));
            g.drawVerticalLine(static_cast<int>(x), yCorr - blobThickness * 0.5f, yCorr + blobThickness * 0.5f);

            if (!pathStarted)
            {
                originalCurve.startNewSubPath(x, yOrig);
                correctedCurve.startNewSubPath(x, yCorr);
                pathStarted = true;
            }
            else
            {
                originalCurve.lineTo(x, yOrig);
                correctedCurve.lineTo(x, yCorr);
            }
        }
        else
        {
            pathStarted = false;
        }
    }

    g.setColour(juce::Colour(0xff6c738a).withAlpha(0.55f));
    g.strokePath(originalCurve, juce::PathStrokeType(1.5f));

    g.setColour(juce::Colour(0xffffdf73));
    g.strokePath(correctedCurve, juce::PathStrokeType(2.3f));

    g.setColour(juce::Colour(0xffd4af37).withAlpha(0.35f));
    g.drawRoundedRectangle(displayRect, 8.0f, 1.2f);
}

void AuraPitchAudioProcessorEditor::resized()
{
    rootCombo.setBounds(500, 22, 75, 26);
    scaleCombo.setBounds(585, 22, 148, 26);

    int knobY = 315;
    int knobW = 115;
    int knobH = 115;
    int spacing = 165;
    int startX = 65;

    centerKnob.setBounds(startX, knobY, knobW, knobH);
    centerLabel.setBounds(startX, knobY + knobH, knobW, 20);

    driftKnob.setBounds(startX + spacing, knobY, knobW, knobH);
    driftLabel.setBounds(startX + spacing, knobY + knobH, knobW, 20);

    vibratoKnob.setBounds(startX + spacing * 2, knobY, knobW, knobH);
    vibratoLabel.setBounds(startX + spacing * 2, knobY + knobH, knobW, 20);

    transitionKnob.setBounds(startX + spacing * 3, knobY, knobW, knobH);
    transitionLabel.setBounds(startX + spacing * 3, knobY + knobH, knobW, 20);
}
