#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==============================================================================
// // PARAMETER TREE & AUDIO ROUTING
// ==============================================================================
AuraPitchAudioProcessor::AuraPitchAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AuraPitchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "CENTER_SNAP", "Pitch Center", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.85f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DRIFT_CORR", "Pitch Drift", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.75f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "VIBRATO_PRESERVE", "Natural Expression", juce::NormalisableRange<float>(0.0f, 1.5f, 0.01f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TRANSITION_SMOOTH", "Note Transition", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.65f));

    juce::StringArray roots = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>("ROOT_NOTE", "Root Key", roots, 0));

    juce::StringArray scales = { "Chromatic (Auto-Free)", "Major", "Minor (Urban/Natural)" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>("SCALE_TYPE", "Scale Mode", scales, 2));

    return { params.begin(), params.end() };
}

void AuraPitchAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    pitchEngine.prepare(sampleRate, samplesPerBlock);
    setLatencySamples(256);
}

bool AuraPitchAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void AuraPitchAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    float centerSnap = apvts.getRawParameterValue("CENTER_SNAP")->load();
    float driftCorr  = apvts.getRawParameterValue("DRIFT_CORR")->load();
    float vibrato    = apvts.getRawParameterValue("VIBRATO_PRESERVE")->load();
    float transition = apvts.getRawParameterValue("TRANSITION_SMOOTH")->load();
    int rootNote     = static_cast<int>(apvts.getRawParameterValue("ROOT_NOTE")->load());
    int scaleType    = static_cast<int>(apvts.getRawParameterValue("SCALE_TYPE")->load());

    auto* channelData = buffer.getWritePointer(0);
    int numSamples = buffer.getNumSamples();

    float rms = buffer.getRMSLevel(0, 0, numSamples);

    pitchEngine.processBlock(channelData, numSamples,
                             driftCorr, centerSnap, vibrato, transition,
                             rootNote, scaleType);

    if (totalNumInputChannels > 1)
    {
        auto* rightChannel = buffer.getWritePointer(1);
        juce::FloatVectorOperations::copy(rightChannel, channelData, numSamples);
    }

    // // UPDATE VISUAL BLOB TELEMETRY
    currentCents.store(pitchEngine.getCentsDeviation());
    currentMidiNote.store(pitchEngine.getLastTargetMidi());
    isSibilantBypass.store(!pitchEngine.isCurrentlyVoiced() && rms > 0.005f);

    BlobHistoryPoint pt;
    pt.originalMidi  = pitchEngine.getLastDetectedMidi();
    pt.correctedMidi = pitchEngine.getLastTargetMidi();
    pt.amplitude     = juce::jlimit(0.0f, 1.0f, rms * 6.0f);
    pt.voiced        = pitchEngine.isCurrentlyVoiced();

    visualHistory[historyWriteIndex] = pt;
    historyWriteIndex = (historyWriteIndex + 1) % static_cast<int>(visualHistory.size());
}

void AuraPitchAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AuraPitchAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorEditor* AuraPitchAudioProcessor::createEditor()
{
    return new AuraPitchAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuraPitchAudioProcessor();
}
