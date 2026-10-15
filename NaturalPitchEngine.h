#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// ==============================================================================
// // AUDIO ENGINE - NATURAL PITCH & FORMANT PRESERVATION
// ==============================================================================
class NaturalPitchEngine
{
public:
    void prepare(double sampleRate, int maxBlockSize)
    {
        (void)maxBlockSize;
        sr = sampleRate;
        bufferSize = 4096;
        analysisWindow = 1024;
        
        inputRing.assign(bufferSize, 0.0f);
        outputRing.assign(bufferSize, 0.0f);
        formantResidual.assign(bufferSize, 0.0f);
        yinBuffer.assign(analysisWindow / 2, 0.0f);
        
        writePos = 0;
        readPhase = 0.0;
        smoothedPitch = 0.0f;
        pitchCenter = 0.0f;
        lastTargetMidi = 60.0f;
        noteHoldCounter = 0;
    }

    // // SCALE MASKING HELPER
    bool isNoteInScale(int midiNote, int rootNote, int scaleType)
    {
        if (scaleType == 0) return true; // Chromatic
        int rel = (midiNote - rootNote + 120) % 12;
        const bool majorMask[12] = {true, false, true, false, true, true, false, true, false, true, false, true};
        const bool minorMask[12] = {true, false, true, true, false, true, false, true, true, false, true, false};
        return (scaleType == 1) ? majorMask[rel] : minorMask[rel];
    }

    float findClosestScaleNote(float currentMidi, int rootNote, int scaleType)
    {
        int base = static_cast<int>(std::round(currentMidi));
        if (isNoteInScale(base, rootNote, scaleType)) return static_cast<float>(base);
        
        for (int offset = 1; offset <= 6; ++offset)
        {
            if (isNoteInScale(base - offset, rootNote, scaleType)) return static_cast<float>(base - offset);
            if (isNoteInScale(base + offset, rootNote, scaleType)) return static_cast<float>(base + offset);
        }
        return static_cast<float>(base);
    }

    // // TRANSIENT & SIBILANCE DETECTOR
    bool detectVoicedState(const float* frame, int length, float& rmsOut)
    {
        float sumSq = 0.0f;
        int zeroCrossings = 0;
        for (int i = 1; i < length; ++i)
        {
            sumSq += frame[i] * frame[i];
            if ((frame[i] >= 0.0f && frame[i - 1] < 0.0f) || (frame[i] < 0.0f && frame[i - 1] >= 0.0f))
                zeroCrossings++;
        }
        rmsOut = std::sqrt(sumSq / static_cast<float>(length));
        float zcr = static_cast<float>(zeroCrossings) / static_cast<float>(length);

        return (rmsOut > 0.003f) && (zcr < 0.22f);
    }

    // // YIN PITCH DETECTOR (SUB-SAMPLE PARABOLIC PRECISION)
    float detectPitchYIN(const float* frame, int length, float& confidenceOut)
    {
        int halfLen = length / 2;
        std::fill(yinBuffer.begin(), yinBuffer.end(), 0.0f);

        for (int tau = 1; tau < halfLen; ++tau)
        {
            float sum = 0.0f;
            for (int i = 0; i < halfLen; ++i)
            {
                float delta = frame[i] - frame[i + tau];
                sum += delta * delta;
            }
            yinBuffer[tau] = sum;
        }

        yinBuffer[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau < halfLen; ++tau)
        {
            runningSum += yinBuffer[tau];
            yinBuffer[tau] *= static_cast<float>(tau) / (runningSum + 1e-9f);
        }

        int minTau = static_cast<int>(sr / 1000.0);
        int maxTau = std::min(halfLen - 1, static_cast<int>(sr / 65.0));

        int bestTau = -1;
        const float threshold = 0.15f;

        for (int tau = minTau; tau < maxTau; ++tau)
        {
            if (yinBuffer[tau] < threshold)
            {
                while (tau + 1 < maxTau && yinBuffer[tau + 1] < yinBuffer[tau])
                    tau++;
                bestTau = tau;
                break;
            }
        }

        if (bestTau == -1)
        {
            confidenceOut = 0.0f;
            return 0.0f;
        }

        float s0 = yinBuffer[std::max(0, bestTau - 1)];
        float s1 = yinBuffer[bestTau];
        float s2 = yinBuffer[std::min(halfLen - 1, bestTau + 1)];
        float denom = 2.0f * (2.0f * s1 - s2 - s0);
        float betterTau = static_cast<float>(bestTau);
        if (std::abs(denom) > 1e-6f)
            betterTau += (s2 - s0) / denom;

        confidenceOut = 1.0f - std::clamp(s1, 0.0f, 1.0f);
        return static_cast<float>(sr) / betterTau;
    }

    // // MAIN REAL-TIME PROCESSING BLOCK
    void processBlock(float* channelData, int numSamples,
                      float driftCorrection, float centerSnap,
                      float vibratoPreserve, float transitionSmooth,
                      int rootNote, int scaleType)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float inSample = channelData[i];
            inputRing[writePos] = inSample;

            int prevPos = (writePos - 1 + bufferSize) % bufferSize;
            float spectralEnv = 0.65f * inSample + 0.35f * inputRing[prevPos];
            formantResidual[writePos] = inSample - spectralEnv;

            if (++hopCounter >= 128)
            {
                hopCounter = 0;
                std::vector<float> analysisFrame(analysisWindow);
                for (int k = 0; k < analysisWindow; ++k)
                {
                    int idx = (writePos - analysisWindow + k + bufferSize) % bufferSize;
                    analysisFrame[k] = inputRing[idx];
                }

                float rms = 0.0f;
                bool isVoiced = detectVoicedState(analysisFrame.data(), analysisWindow, rms);
                float confidence = 0.0f;
                float detectedHz = isVoiced ? detectPitchYIN(analysisFrame.data(), analysisWindow, confidence) : 0.0f;

                if (isVoiced && detectedHz > 65.0f && confidence > 0.65f)
                {
                    voicedActive = true;
                    currentHz = detectedHz;
                    float currentMidi = 69.0f + 12.0f * std::log2(detectedHz / 440.0f);

                    float alphaDrift = 0.04f * (1.0f - transitionSmooth * 0.7f);
                    if (pitchCenter == 0.0f) pitchCenter = currentMidi;
                    pitchCenter = (1.0f - alphaDrift) * pitchCenter + alphaDrift * currentMidi;

                    float naturalVibrato = currentMidi - pitchCenter;

                    float candidateTarget = findClosestScaleNote(pitchCenter, rootNote, scaleType);
                    if (std::abs(candidateTarget - lastTargetMidi) > 0.5f)
                    {
                        if (++noteHoldCounter > 3)
                        {
                            lastTargetMidi = candidateTarget;
                            noteHoldCounter = 0;
                        }
                    }
                    else
                    {
                        noteHoldCounter = 0;
                    }

                    float correctedCenter = pitchCenter + (lastTargetMidi - pitchCenter) * centerSnap;
                    float instantaneousCorrection = currentMidi + (lastTargetMidi - currentMidi) * driftCorrection;

                    float finalMidi = (correctedCenter * 0.7f + instantaneousCorrection * 0.3f)
                                    + (naturalVibrato * vibratoPreserve);

                    float targetHz = 440.0f * std::pow(2.0f, (finalMidi - 69.0f) / 12.0f);
                    float rawRatio = targetHz / detectedHz;

                    rawRatio = std::clamp(rawRatio, 0.75f, 1.35f);
                    pitchRatio = 0.85f * pitchRatio + 0.15f * rawRatio;

                    lastDetectedMidi = currentMidi;
                    lastTargetNoteMidi = finalMidi;
                    centsDeviation = (currentMidi - lastTargetMidi) * 100.0f;
                }
                else
                {
                    voicedActive = false;
                    pitchRatio = 0.90f * pitchRatio + 0.10f * 1.0f;
                    centsDeviation *= 0.85f;
                }
            }

            // // DUAL-TAP OVERLAP-ADD & FORMANT RESTORATION
            float outSample = inSample;
            if (voicedActive || std::abs(pitchRatio - 1.0f) > 0.002f)
            {
                double grainSize = 512.0;
                readPhase += (1.0 - static_cast<double>(pitchRatio)) / grainSize;
                if (readPhase >= 1.0) readPhase -= 1.0;
                if (readPhase < 0.0) readPhase += 1.0;

                double phase2 = std::fmod(readPhase + 0.5, 1.0);

                double delay1 = readPhase * grainSize + 64.0;
                double delay2 = phase2 * grainSize + 64.0;

                float win1 = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * static_cast<float>(readPhase)));
                float win2 = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * static_cast<float>(phase2)));

                float pitchedAudio = win1 * readInterpolated(inputRing, delay1)
                                   + win2 * readInterpolated(inputRing, delay2);

                float originalFormantTexture = formantResidual[(writePos - 256 + bufferSize) % bufferSize];
                outSample = pitchedAudio * 0.82f + originalFormantTexture * 0.18f;
            }
            else
            {
                outSample = inputRing[(writePos - 256 + bufferSize) % bufferSize];
            }

            channelData[i] = outSample;
            writePos = (writePos + 1) % bufferSize;
        }
    }

    float getLastDetectedMidi() const { return lastDetectedMidi; }
    float getLastTargetMidi() const { return lastTargetNoteMidi; }
    float getCentsDeviation() const { return centsDeviation; }
    bool isCurrentlyVoiced() const { return voicedActive; }

private:
    float readInterpolated(const std::vector<float>& buf, double delaySamples)
    {
        double readPos = static_cast<double>(writePos) - delaySamples;
        while (readPos < 0.0) readPos += static_cast<double>(bufferSize);
        int idx0 = static_cast<int>(readPos) % bufferSize;
        int idx1 = (idx0 + 1) % bufferSize;
        float frac = static_cast<float>(readPos - std::floor(readPos));
        return buf[idx0] + frac * (buf[idx1] - buf[idx0]);
    }

    double sr = 44100.0;
    int bufferSize = 4096;
    int analysisWindow = 1024;
    std::vector<float> inputRing, outputRing, formantResidual, yinBuffer;
    int writePos = 0;
    int hopCounter = 0;
    double readPhase = 0.0;

    float pitchRatio = 1.0f;
    float smoothedPitch = 0.0f;
    float pitchCenter = 0.0f;
    float lastTargetMidi = 60.0f;
    int noteHoldCounter = 0;

    bool voicedActive = false;
    float currentHz = 0.0f;
    float lastDetectedMidi = 60.0f;
    float lastTargetNoteMidi = 60.0f;
    float centsDeviation = 0.0f;
};
