#include "aurora/dsp/KarmaControlState.h"
#include "aurora/dsp/KarmaEffects.h"
#include "aurora/dsp/PcmWav.h"
#include "aurora/dsp/StereoLooper.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool Near(float actual, float expected, float tolerance = 0.0001f)
{
    return std::fabs(actual - expected) < tolerance;
}

using Looper = aurora::dsp::StereoLooper;
using Parameters = Looper::ChannelParameters;

void Process(Looper& looper,
             const float* inputLeft,
             const float* inputRight,
             float* outputLeft,
             float* outputRight,
             std::size_t frames,
             const Parameters& left,
             const Parameters& right)
{
    looper.ProcessBlock(inputLeft,
                        inputRight,
                        outputLeft,
                        outputRight,
                        frames,
                        left,
                        right);
}

void TestInitializationAndValidation()
{
    std::array<float, 8> left;
    std::array<float, 8> right;
    left.fill(1.0f);
    right.fill(1.0f);

    Looper looper;
    Require(!looper.Initialize(nullptr, right.data(), right.size(), 4),
            "null buffer is rejected");
    Require(!looper.Initialize(left.data(), right.data(), 0, 0),
            "zero capacity is rejected");
    Require(!looper.Initialize(left.data(), right.data(), 8, 9),
            "loop length beyond capacity is rejected");
    Require(looper.Initialize(left.data(), right.data(), 8, 4),
            "valid buffers initialize");
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        Require(Near(left[index], 0.0f), "left buffer is cleared");
        Require(Near(right[index], 0.0f), "right buffer is cleared");
    }
    Require(looper.LoopLength() == 4, "initial loop length is retained");
}

void TestRecordPlaybackAndWrap()
{
    std::array<float, 4> leftBuffer{};
    std::array<float, 4> rightBuffer{};
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(), rightBuffer.data(), 4, 4),
            "looper initializes");

    const float inputLeft[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    const float inputRight[] = {-1.0f, -2.0f, -3.0f, -4.0f, -5.0f};
    float outputLeft[5]{};
    float outputRight[5]{};
    Parameters left;
    left.length = 1.0f;
    left.mix = 1.0f;
    left.record = true;
    Parameters right = left;
    right.pan = 1.0f;
    Process(looper, inputLeft, inputRight, outputLeft, outputRight, 5, left, right);

    Require(looper.IsRecording(), "record state is exposed");
    Require(Near(outputLeft[0], inputLeft[0]) && Near(outputRight[0], inputRight[0]),
            "record monitoring remains dry and stereo");
    Require(Near(leftBuffer[0], 5.0f), "record head wraps over the loop length");
    Require(Near(rightBuffer[0], -5.0f), "right record remains independent");
    const auto leftDirty = looper.TakeDirtyRange(0);
    const auto rightDirty = looper.TakeDirtyRange(1);
    Require(leftDirty.dirty && leftDirty.begin == 0 && leftDirty.end == 4,
            "recording exposes the modified left-buffer range");
    Require(rightDirty.dirty && rightDirty.begin == 0 && rightDirty.end == 4,
            "recording exposes the modified right-buffer range");

    left.record = false;
    right.record = false;
    const float silence[] = {0.0f, 0.0f, 0.0f};
    Process(looper, silence, silence, outputLeft, outputRight, 3, left, right);
    Require(!looper.IsRecording(), "record stops when control is released");
    Require(Near(outputLeft[0], 2.0f) && Near(outputLeft[1], 3.0f)
                && Near(outputLeft[2], 4.0f),
            "wrapped recording begins playback at the oldest sample");
    Require(Near(outputRight[0], -2.0f) && Near(outputRight[2], -4.0f),
            "right playback uses its own recorded samples");
}

void TestUnitySpeedRecordingPreservesEverySample()
{
    constexpr std::size_t capacity = 192000;
    constexpr std::size_t blockSize = 96;
    std::vector<float> leftBuffer(capacity, 0.0f);
    std::vector<float> rightBuffer(capacity, 0.0f);
    std::vector<float> inputLeft(blockSize);
    std::vector<float> inputRight(blockSize);
    std::vector<float> outputLeft(blockSize);
    std::vector<float> outputRight(blockSize);
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(),
                              rightBuffer.data(),
                              capacity,
                              0),
            "looper initializes for long unity-speed recording");

    Parameters left;
    left.length = 1.0f;
    left.regionLength = 1.0f;
    left.speed = 1.0f;
    left.mix = 1.0f;
    left.record = true;
    left.recordFromStart = true;
    Parameters right = left;
    right.pan = 1.0f;
    for (std::size_t block = 0; block < capacity / blockSize; ++block)
    {
        for (std::size_t frame = 0; frame < blockSize; ++frame)
        {
            const auto index = block * blockSize + frame;
            inputLeft[frame] = static_cast<float>((index % 997) + 1) / 1000.0f;
            inputRight[frame] = -inputLeft[frame];
        }
        Process(looper,
                inputLeft.data(),
                inputRight.data(),
                outputLeft.data(),
                outputRight.data(),
                blockSize,
                left,
                right);
    }

    for (std::size_t index = 0; index < capacity; ++index)
    {
        const float expected = static_cast<float>((index % 997) + 1) / 1000.0f;
        Require(leftBuffer[index] == expected && rightBuffer[index] == -expected,
                "unity-speed recording writes every input sample to its matching frame");
    }
}

void TestMixWindowAndPan()
{
    std::array<float, 8> leftBuffer{{0, 1, 2, 3, 4, 5, 6, 7}};
    std::array<float, 8> rightBuffer{{10, 11, 12, 13, 14, 15, 16, 17}};
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(), rightBuffer.data(), 8, 8),
            "looper initializes");
    for (std::size_t index = 0; index < 8; ++index)
    {
        leftBuffer[index] = static_cast<float>(index);
        rightBuffer[index] = 10.0f + static_cast<float>(index);
    }

    const float dryLeft[] = {20.0f, 20.0f};
    const float dryRight[] = {-20.0f, -20.0f};
    float outputLeft[2]{};
    float outputRight[2]{};
    Parameters left;
    left.start = 0.5f;
    left.length = 0.5f;
    left.speed = 0.0f;
    left.mix = 0.0f;
    Parameters right = left;
    right.pan = 1.0f;
    Process(looper, dryLeft, dryRight, outputLeft, outputRight, 1, left, right);
    Require(Near(outputLeft[0], dryLeft[0]) && Near(outputRight[0], dryRight[0]),
            "zero mix passes through the direct stereo inputs");

    left.mix = 0.5f;
    right.mix = 0.5f;
    Process(looper, dryLeft, dryRight, outputLeft, outputRight, 1, left, right);
    constexpr float equalPower = 0.70710678118f;
    Require(Near(outputLeft[0], (20.0f + 4.0f) * 0.5f),
            "half mix uses a peak-safe linear blend and Warp start");
    Require(Near(outputRight[0], (-20.0f + 14.0f) * 0.5f),
            "right channel retains independent window and hard-right pan");

    left.mix = 1.0f;
    right.mix = 1.0f;
    Process(looper, dryLeft, dryRight, outputLeft, outputRight, 1, left, right);
    Require(Near(outputLeft[0], 4.0f) && Near(outputRight[0], 14.0f),
            "full wet output reads each channel's selected window");

    left.pan = 0.5f;
    left.mix = 0.0f;
    right.mix = 0.0f;
    Process(looper, dryLeft, dryRight, outputLeft, outputRight, 1, left, right);
    Require(Near(outputLeft[0], 20.0f * equalPower),
            "Shift+Mix pan moves the left channel toward the right output");
    Require(Near(outputRight[0], 20.0f * equalPower - 20.0f),
            "equal-power pan preserves channel gain");
}

void TestLoopWindowWrapsAcrossBufferEnd()
{
    std::array<float, 8> leftBuffer{{0, 1, 2, 3, 4, 5, 6, 7}};
    std::array<float, 8> rightBuffer{{10, 11, 12, 13, 14, 15, 16, 17}};
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(), rightBuffer.data(), 8, 8),
            "looper initializes");
    for (std::size_t index = 0; index < 8; ++index)
    {
        leftBuffer[index] = static_cast<float>(index);
        rightBuffer[index] = 10.0f + static_cast<float>(index);
    }

    Parameters left;
    left.start = 0.75f;
    left.length = 0.5f;
    left.speed = 1.0f;
    left.mix = 1.0f;
    Parameters right = left;
    right.pan = 1.0f;
    const float silence[4]{};
    float outputLeft[4]{};
    float outputRight[4]{};
    Process(looper, silence, silence, outputLeft, outputRight, 4, left, right);
    Require(Near(outputLeft[0], 6.0f) && Near(outputLeft[1], 7.0f)
                && Near(outputLeft[2], 0.0f) && Near(outputLeft[3], 1.0f),
            "loop start plus window length wraps from the buffer end to index zero");
    Require((looper.TakeLoopEndEvents() & 1U) != 0,
            "forward playback reports its loop-end crossing");
}

void TestFullLengthWindowWrapsWithinWorkingLoop()
{
    std::array<float, 16> leftBuffer{};
    std::array<float, 16> rightBuffer{};
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(), rightBuffer.data(), 16, 16),
            "looper initializes");
    for (std::size_t index = 0; index < leftBuffer.size(); ++index)
        leftBuffer[index] = static_cast<float>(index + 1);

    Parameters left;
    left.start = 0.25f;
    left.length = 0.5f;
    left.regionLength = 0.5f;
    left.speed = 1.0f;
    left.mix = 1.0f;
    Parameters right = left;
    right.pan = 1.0f;
    const float silence[8]{};
    float outputLeft[8]{};
    float outputRight[8]{};
    Process(looper,
            silence,
            silence,
            outputLeft,
            outputRight,
            8,
            left,
            right);
    const float expected[] = {5, 6, 7, 8, 1, 2, 3, 4};
    for (std::size_t index = 0; index < 8; ++index)
        Require(Near(outputLeft[index], expected[index]),
                "full-length Warp window wraps at the working-loop boundary");
}

void TestZeroSpeedRecordAndErase()
{
    std::array<float, 4> leftBuffer{};
    std::array<float, 4> rightBuffer{};
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(), rightBuffer.data(), 4, 4),
            "looper initializes");
    const float source[] = {1.0f, 2.0f, 3.0f, 4.0f};
    float outputLeft[4]{};
    float outputRight[4]{};
    Parameters left;
    left.length = 1.0f;
    left.speed = 0.0f;
    left.record = true;
    Parameters right = left;
    right.pan = 1.0f;
    Process(looper, source, source, outputLeft, outputRight, 4, left, right);
    Require(Near(leftBuffer[0], 4.0f) && Near(leftBuffer[1], 0.0f),
            "zero-speed recording repeatedly overwrites one sample");
    left.record = false;
    right.record = false;
    left.mix = 1.0f;
    right.mix = 1.0f;
    Process(looper, source, source, outputLeft, outputRight, 2, left, right);
    Require(Near(outputLeft[0], 4.0f) && Near(outputLeft[1], 4.0f),
            "zero-speed playback holds one sample");

    left.speed = 1.0f;
    right.speed = 1.0f;
    left.erase = true;
    right.erase = true;
    Process(looper, source, source, outputLeft, outputRight, 2, left, right);
    Require(looper.IsErasing(), "erase state is exposed");
    Require(Near(leftBuffer[0], 0.0f) && Near(leftBuffer[1], 0.0f),
            "Freeze progressively erases samples under the head");
}

void TestModeSpecificRecording()
{
    using Controls = aurora::dsp::KarmaControlState;
    float inputs[Controls::ControlCount] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    Controls controls;
    controls.Initialize(inputs);
    controls.CycleMode(inputs);
    Require(controls.IsChannelActive(0) && !controls.IsChannelActive(1),
            "L mode enables only the left recording channel");
    Require(Near(controls.MapTimeRatio(inputs[Controls::Time]), 0.25f),
            "single-channel Time controls map directly over the working loop");
    controls.CycleMode(inputs);
    Require(!controls.IsChannelActive(0) && controls.IsChannelActive(1),
            "R mode enables only the right recording channel");
    controls.CycleMode(inputs);
    Require(controls.IsChannelActive(0) && controls.IsChannelActive(1),
            "LR mode enables both recording channels");

    std::array<float, 4> leftBuffer{};
    std::array<float, 4> rightBuffer{};
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(), rightBuffer.data(), 4, 4),
            "looper initializes");
    const float leftInput[] = {1, 2};
    const float rightInput[] = {3, 4};
    float outputLeft[2]{};
    float outputRight[2]{};
    Parameters left;
    left.length = 1.0f;
    Parameters right = left;

    controls.CycleMode(inputs);
    left.record = controls.IsChannelActive(0);
    right.record = controls.IsChannelActive(1);
    right.pan = 1.0f;
    Process(looper, leftInput, rightInput, outputLeft, outputRight, 2, left, right);
    Require(Near(leftBuffer[0], 1.0f) && Near(leftBuffer[1], 2.0f)
                && Near(rightBuffer[0], 0.0f) && Near(rightBuffer[1], 0.0f),
            "L mode records only the left physical input");

    controls.CycleMode(inputs);
    left.record = controls.IsChannelActive(0);
    right.record = controls.IsChannelActive(1);
    Process(looper, leftInput, rightInput, outputLeft, outputRight, 2, left, right);
    Require(Near(leftBuffer[0], 1.0f) && Near(leftBuffer[1], 2.0f)
                && Near(rightBuffer[0], 0.0f) && Near(rightBuffer[1], 0.0f)
                && Near(rightBuffer[2], 3.0f) && Near(rightBuffer[3], 4.0f),
            "R mode records only the right input at its current playback head");

    controls.CycleMode(inputs);
    left.record = controls.IsChannelActive(0);
    right.record = controls.IsChannelActive(1);
    Process(looper, leftInput, rightInput, outputLeft, outputRight, 2, left, right);
    Require(Near(leftBuffer[0], 1.0f) && Near(leftBuffer[1], 2.0f)
                && Near(leftBuffer[2], 1.0f) && Near(leftBuffer[3], 2.0f)
                && Near(rightBuffer[0], 3.0f) && Near(rightBuffer[1], 4.0f),
            "LR mode records both inputs into their respective active heads");
}

void TestEraseOutputFade()
{
    std::array<float, 4> leftBuffer{{1, 1, 1, 1}};
    std::array<float, 4> rightBuffer{{1, 1, 1, 1}};
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(), rightBuffer.data(), 4, 4),
            "looper initializes");
    leftBuffer.fill(1.0f);
    rightBuffer.fill(1.0f);
    const float silence[240]{};
    float outputLeft[240]{};
    float outputRight[240]{};
    Parameters left;
    left.length = 1.0f;
    left.speed = 0.0f;
    left.mix = 1.0f;
    left.erase = true;
    Parameters right = left;
    right.pan = 1.0f;
    Process(looper,
            silence,
            silence,
            outputLeft,
            outputRight,
            240,
            left,
            right);
    Require(outputLeft[0] > outputLeft[100] && outputLeft[100] > outputLeft[239],
            "Freeze fades wet playback down instead of clicking");
    Require(Near(outputLeft[239], 0.0f, 0.01f),
            "wet playback reaches silence while erase is held");
    Require(Near(leftBuffer[0], 0.0f) && Near(rightBuffer[0], 0.0f),
            "erase still clears the buffer beneath a stopped head");
}

void TestReversePlaybackAndControlCurves()
{
    std::array<float, 8> leftBuffer{{0, 1, 2, 3, 4, 5, 6, 7}};
    std::array<float, 8> rightBuffer{};
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(), rightBuffer.data(), 8, 8),
            "looper initializes");
    for (std::size_t index = 0; index < leftBuffer.size(); ++index)
        leftBuffer[index] = static_cast<float>(index);

    Parameters left;
    left.length = 1.0f;
    left.speed = -1.0f;
    left.mix = 1.0f;
    Parameters right;
    right.length = 1.0f;
    right.pan = 1.0f;
    const float silence[] = {0, 0, 0, 0};
    float outputLeft[4]{};
    float outputRight[4]{};
    Process(looper, silence, silence, outputLeft, outputRight, 4, left, right);
    Require(Near(outputLeft[0], 0.0f) && Near(outputLeft[1], 7.0f)
                && Near(outputLeft[2], 6.0f) && Near(outputLeft[3], 5.0f),
            "negative speed plays backward across the circular boundary");

    using Controls = aurora::dsp::KarmaControlState;
    Require(Near(Controls::MapTimeRatio(0.0f), 0.0f)
                && Near(Controls::MapTimeRatio(0.5f), 0.25f)
                && Near(Controls::MapTimeRatio(1.0f), 1.0f),
            "Time follows the documented squared curve");
    Require(Near(Controls::MapBlurSpeed(0.0f), -2.0f)
                && Near(Controls::MapBlurSpeed(0.48f), 0.0f)
                && Near(Controls::MapBlurSpeed(0.5f), 0.0f)
                && Near(Controls::MapBlurSpeed(0.52f), 0.0f)
                && Near(Controls::MapBlurSpeed(1.0f), 2.0f),
            "Blur maps to reverse, stop deadband, and forward ranges");
}

void TestSeamCrossfadeDoesNotCreatePeakBoost()
{
    std::array<float, 16> leftBuffer;
    std::array<float, 16> rightBuffer;
    leftBuffer.fill(0.75f);
    rightBuffer.fill(0.0f);
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(), rightBuffer.data(), 16, 16),
            "looper initializes");
    leftBuffer.fill(0.75f);

    Parameters left;
    left.length = 1.0f;
    left.speed = 1.0f;
    left.mix = 1.0f;
    Parameters right;
    right.length = 1.0f;
    right.pan = 1.0f;
    const float silence[16]{};
    float outputLeft[16]{};
    float outputRight[16]{};
    Process(looper,
            silence,
            silence,
            outputLeft,
            outputRight,
            16,
            left,
            right);
    for (float sample : outputLeft)
        Require(sample <= 0.7501f && sample >= 0.7499f,
                "seam crossfade does not boost a constant waveform");
}

void TestReflectBitReductionIsOptIn()
{
    aurora::dsp::KarmaEffects effects;
    effects.Reset(1234U);
    aurora::dsp::KarmaEffects::Parameters parameters;
    parameters.loopSamples = 48000;
    parameters.speed = 1.0f;
    const float source = 0.401f;
    const auto bypassed = effects.Process(source, parameters);
    Require(Near(bypassed.sample, source) && Near(bypassed.speed, 1.0f),
            "zero effect controls preserve the clean sample and speed");

    parameters.reflect = 1.0f;
    parameters.reflectMix = 0.0f;
    Require(Near(effects.Process(source, parameters).sample, source),
            "Shift+Reflect at zero keeps bit reduction bypassed");

    parameters.reflectMix = 1.0f;
    Require(Near(effects.Process(source, parameters).sample, 0.0f),
            "maximum Reflect reduces the sample to the selected two-bit grid");

    parameters.reflect = 0.5f;
    parameters.reflectMix = 0.5f;
    const float partial = effects.Process(source, parameters).sample;
    Require(partial > 0.0f && partial < source,
            "Reflect wet/dry control blends the reduced signal");
}

void TestAtmosphereIsBypassedAndDeterministic()
{
    using Effects = aurora::dsp::KarmaEffects;
    Effects::Parameters parameters;
    parameters.speed = 0.75f;
    parameters.loopSamples = 480;
    Effects bypass;
    bypass.Reset(22U);
    for (int index = 0; index < 1000; ++index)
    {
        const auto result = bypass.Process(0.375f, parameters);
        Require(Near(result.sample, 0.375f) && Near(result.speed, 0.75f),
                "Atmosphere at zero does not alter playback");
    }
    Require(bypass.EventCount() == 0,
            "Atmosphere bypass does not schedule glitch events");

    parameters.atmosphere = 1.0f;
    parameters.glitchDuration = 0.001f;
    std::vector<float> repeatHistoryFirst(24000);
    std::vector<float> repeatHistorySecond(24000);
    Effects first;
    Effects second;
    first.SetRepeatBuffer(repeatHistoryFirst.data(), repeatHistoryFirst.size());
    second.SetRepeatBuffer(repeatHistorySecond.data(), repeatHistorySecond.size());
    first.Reset(9876U);
    second.Reset(9876U);
    for (int index = 0; index < 12000; ++index)
    {
        const auto left = first.Process(0.25f, parameters);
        const auto right = second.Process(0.25f, parameters);
        Require(Near(left.sample, right.sample) && Near(left.speed, right.speed),
                "per-channel effect state is reproducible with a fixed seed");
        const std::size_t durationLimit =
            first.ActiveGlitch() == Effects::Glitch::Repeat
                ? parameters.loopSamples * 4
                : parameters.loopSamples;
        Require(first.RemainingEventSamples() <= durationLimit,
                "Atmosphere event duration stays within its type-specific bound");
    }
    Require(first.EventCount() > 0,
            "Atmosphere probability schedules events when engaged");
}

void TestAtmosphereTransformationCoverage()
{
    using Effects = aurora::dsp::KarmaEffects;
    bool seen[6]{};
    Effects::Parameters parameters;
    parameters.atmosphere = 1.0f;
    parameters.glitchDuration = 0.001f;
    parameters.speed = 1.0f;
    parameters.loopSamples = 240;
    std::vector<float> repeatHistory(24000);

    for (uint32_t seed = 1; seed <= 64; ++seed)
    {
        Effects effects;
        effects.SetRepeatBuffer(repeatHistory.data(), repeatHistory.size());
        effects.Reset(seed);
        for (int sample = 0; sample < 2400; ++sample)
        {
            const auto result = effects.Process(0.25f, parameters);
            const auto glitch = effects.ActiveGlitch();
            if (glitch != Effects::Glitch::None)
                seen[static_cast<std::size_t>(glitch)] = true;
            if (glitch == Effects::Glitch::ReverseBurst)
                Require(result.speed <= 1.0f,
                        "reverse burst transitions speed without exceeding base rate");
        }
    }

    Require(seen[static_cast<std::size_t>(Effects::Glitch::VolumeCut)],
            "Atmosphere can schedule volume cuts");
    Require(seen[static_cast<std::size_t>(Effects::Glitch::ReverseBurst)],
            "Atmosphere can schedule reverse bursts");
    Require(seen[static_cast<std::size_t>(Effects::Glitch::SampleRateReduction)],
            "Atmosphere can schedule sample-rate reduction");
    Require(seen[static_cast<std::size_t>(Effects::Glitch::Noise)],
            "Atmosphere can schedule noise events");
    Require(seen[static_cast<std::size_t>(Effects::Glitch::Repeat)],
            "Atmosphere can schedule micro-loop repeats");
}

void TestRepeatMicroLoopDurationRange()
{
    using Effects = aurora::dsp::KarmaEffects;
    const float durations[] = {0.0f, 1.0f};
    std::vector<float> repeatHistory(24000);
    for (float duration : durations)
    {
        bool foundExpectedDuration = false;
        for (uint32_t seed = 1; seed <= 256 && !foundExpectedDuration; ++seed)
        {
            Effects effects;
            effects.SetRepeatBuffer(repeatHistory.data(), repeatHistory.size());
            effects.Reset(seed);
            Effects::Parameters parameters;
            parameters.loopSamples = 48000;
            parameters.glitchDuration = duration;
            for (std::size_t sampleIndex = 0; sampleIndex < 24000; ++sampleIndex)
            {
                parameters.atmosphere = 0.0f;
                effects.Process(static_cast<float>(sampleIndex % 257) / 257.0f,
                                parameters);
            }

            parameters.atmosphere = 1.0f;
            for (std::size_t sampleIndex = 0;
                 sampleIndex < 12000 && !foundExpectedDuration;
                 ++sampleIndex)
            {
                const auto result = effects.Process(0.25f, parameters);
                if (effects.ActiveGlitch() == Effects::Glitch::Repeat)
                {
                    const std::size_t expectedLength = duration == 0.0f
                                                     ? 240
                                                     : 24000;
                    Require(effects.RepeatLengthSamples() == expectedLength,
                            "repeat micro-loop length maps from 5 ms to 500 ms");
                    Require(result.speed == 0.0f,
                            "repeat event holds the playback head in place");
                    Require(effects.RemainingEventSamples()
                                <= expectedLength * 4,
                            "repeat event is limited to four micro-loop cycles");
                    foundExpectedDuration = true;
                }
            }
        }
        Require(foundExpectedDuration,
                "a deterministic seed schedules a repeat event for duration endpoint");
    }
}

void TestRepeatCyclesCapturedLoopHistory()
{
    using Effects = aurora::dsp::KarmaEffects;
    constexpr std::size_t repeatLength = 240;
    constexpr std::size_t repeatEventLength = repeatLength * 4;
    std::vector<float> repeatHistory(24000);
    bool verified = false;
    for (uint32_t seed = 1; seed <= 256 && !verified; ++seed)
    {
        Effects effects;
        effects.SetRepeatBuffer(repeatHistory.data(), repeatHistory.size());
        effects.Reset(seed);
        Effects::Parameters parameters;
        parameters.loopSamples = 48000;
        parameters.glitchDuration = 0.0f;
        for (std::size_t sampleIndex = 0; sampleIndex < repeatHistory.size(); ++sampleIndex)
        {
            parameters.atmosphere = 0.0f;
            effects.Process(static_cast<float>(sampleIndex % repeatLength)
                                / static_cast<float>(repeatLength),
                            parameters);
        }

        parameters.atmosphere = 1.0f;
        for (std::size_t sampleIndex = 0;
             sampleIndex < 12000 && !verified;
             ++sampleIndex)
        {
            const float source = static_cast<float>(sampleIndex % repeatLength)
                               / static_cast<float>(repeatLength);
            const auto result = effects.Process(source, parameters);
            if (effects.ActiveGlitch() != Effects::Glitch::Repeat)
                continue;

            float repeatedOutput[repeatEventLength]{};
            repeatedOutput[0] = result.sample;
            for (std::size_t index = 1; index < repeatEventLength; ++index)
            {
                const auto repeatResult = effects.Process(source, parameters);
                Require(repeatResult.speed == 0.0f,
                        "repeat keeps the playback head fixed for all cycles");
                repeatedOutput[index] = repeatResult.sample;
            }
            for (std::size_t index = 150; index < repeatLength; ++index)
                Require(Near(repeatedOutput[index],
                             repeatedOutput[index + repeatLength])
                            && Near(repeatedOutput[index],
                                    repeatedOutput[index + 2 * repeatLength]),
                        "micro-loop cycles repeat the same captured samples");
            verified = true;
        }
    }
    Require(verified, "repeat event can be observed for deterministic seeds");
}

void TestAtmosphereEventsCancelCleanly()
{
    using Effects = aurora::dsp::KarmaEffects;
    Effects effects;
    effects.Reset(1U);
    Effects::Parameters parameters;
    parameters.atmosphere = 1.0f;
    parameters.glitchDuration = 1.0f;
    parameters.loopSamples = 48000;

    for (int sample = 0; sample < 20000
         && effects.ActiveGlitch() == Effects::Glitch::None; ++sample)
        effects.Process(0.25f, parameters);
    Require(effects.ActiveGlitch() != Effects::Glitch::None,
            "Atmosphere schedules an event before cancellation");

    effects.Cancel();
    Require(effects.ActiveGlitch() == Effects::Glitch::None
                && effects.RemainingEventSamples() == 0,
            "recording or erasing cancels an active event instead of pausing it");
}

void TestFirstRecordCreatesLoopFromEmptyBuffer()
{
    std::array<float, 8> leftBuffer;
    std::array<float, 8> rightBuffer;
    leftBuffer.fill(-1.0f);
    rightBuffer.fill(-1.0f);
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(), rightBuffer.data(), 8, 0),
            "looper initializes");

    const float silence[] = {0, 0};
    float outputLeft[2]{};
    float outputRight[2]{};
    Parameters left;
    left.length = 0.0f;
    left.speed = 1.0f;
    Parameters right = left;
    right.pan = 1.0f;
    Process(looper, silence, silence, outputLeft, outputRight, 2, left, right);
    Require(looper.LoopLength() == 0,
            "an empty startup buffer has no loop length");

    const float recording[] = {11.0f, 12.0f};
    left.start = 0.0f;
    left.length = 1.0f;
    left.record = true;
    left.recordFromStart = true;
    right = left;
    right.pan = 1.0f;
    Process(looper, recording, recording, outputLeft, outputRight, 2, left, right);
    Require(Near(leftBuffer[0], 11.0f) && Near(leftBuffer[1], 12.0f),
            "first ordinary recording writes from buffer index zero");
    Require(looper.HasDirtyRange(0) && looper.HasDirtyRange(1),
            "first ordinary recording marks the empty buffers as written");

    left.record = false;
    left.recordFromStart = false;
    left.finishRecordAtFullWindow = true;
    left.length = 0.25f;
    left.speed = 0.0f;
    left.mix = 1.0f;
    right = left;
    right.pan = 1.0f;
    Process(looper, silence, silence, outputLeft, outputRight, 1, left, right);
    Require(Near(outputLeft[0], 11.0f),
            "first-record release starts playback at the captured loop");
}

void TestRecordingDefersWindowChangesUntilRelease()
{
    constexpr std::size_t capacity = 1024;
    std::array<float, capacity> leftBuffer{};
    std::array<float, capacity> rightBuffer{};
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(),
                              rightBuffer.data(),
                              capacity,
                              capacity),
            "looper initializes");

    Parameters left;
    left.length = 0.5f;
    left.speed = 0.0f;
    Parameters right = left;
    right.pan = 1.0f;
    const float silence[1]{};
    float outputLeft[1]{};
    float outputRight[1]{};
    Process(looper, silence, silence, outputLeft, outputRight, 1, left, right);

    std::array<float, 300> recording{};
    for (std::size_t index = 0; index < recording.size(); ++index)
        recording[index] = static_cast<float>(index + 1);
    std::array<float, 300> recordingRight = recording;
    std::array<float, 300> recordedOutputLeft{};
    std::array<float, 300> recordedOutputRight{};
    left.record = true;
    right.record = true;
    Process(looper,
            recording.data(),
            recordingRight.data(),
            recordedOutputLeft.data(),
            recordedOutputRight.data(),
            recording.size(),
            left,
            right);

    left.start = 0.5f;
    left.length = 0.25f;
    right = left;
    right.pan = 1.0f;
    std::array<float, 100> changedWindowInput{};
    changedWindowInput.fill(901.0f);
    std::array<float, 100> changedWindowOutputLeft{};
    std::array<float, 100> changedWindowOutputRight{};
    Process(looper,
            changedWindowInput.data(),
            changedWindowInput.data(),
            changedWindowOutputLeft.data(),
            changedWindowOutputRight.data(),
            changedWindowInput.size(),
            left,
            right);
    Require(Near(leftBuffer[0], 901.0f)
                && Near(leftBuffer[1], 0.0f)
                && Near(leftBuffer[512], 0.0f),
            "Warp and Time changes do not move the write window during recording");

    left.record = false;
    right.record = false;
    left.speed = 0.0f;
    left.mix = 1.0f;
    right = left;
    right.pan = 1.0f;
    leftBuffer[512] = 7.0f;
    rightBuffer[512] = 8.0f;
    Process(looper, silence, silence, outputLeft, outputRight, 1, left, right);
    Require(Near(outputLeft[0], 7.0f) && Near(outputRight[0], 8.0f),
            "release snaps playback to the latest Warp and Time selection");
}

void TestFullEraseIsIncremental()
{
    std::array<float, 20000> leftBuffer;
    std::array<float, 20000> rightBuffer;
    leftBuffer.fill(1.0f);
    rightBuffer.fill(-1.0f);
    Looper looper;
    Require(looper.Initialize(leftBuffer.data(), rightBuffer.data(), leftBuffer.size(), 4),
            "looper initializes");
    Parameters left;
    left.clearAll = true;
    left.mix = 1.0f;
    Parameters right = left;
    right.pan = 1.0f;
    const float input[] = {0.25f};
    float outputLeft[1]{};
    float outputRight[1]{};
    for (int block = 0; block < 3; ++block)
    {
        Process(looper, input, input, outputLeft, outputRight, 1, left, right);
        Require(looper.IsClearing() == (block < 2),
                "full erase reports progress until complete");
        Require(Near(outputLeft[0], 0.0f) && Near(outputRight[0], 0.0f),
                "full erase mutes the loop output instead of passing input through");
    }
    Require(!looper.IsClearing(), "full erase completes over bounded chunks");
    Require(looper.TakeClearCompleted(),
            "full erase reports its completion once");
    const auto leftDirty = looper.TakeDirtyRange(0);
    const auto rightDirty = looper.TakeDirtyRange(1);
    Require(leftDirty.dirty && leftDirty.begin == 0
                && leftDirty.end == leftBuffer.size()
                && rightDirty.dirty && rightDirty.begin == 0
                && rightDirty.end == rightBuffer.size(),
            "full-buffer erase marks both channels for file persistence");
    for (std::size_t index = 0; index < leftBuffer.size(); ++index)
        Require(Near(leftBuffer[index], 0.0f) && Near(rightBuffer[index], 0.0f),
                "full erase clears both requested buffers");

    looper.StopPlayback();
    Require(looper.LoopLength() == 0,
            "full erase leaves no active playback region");
    left.clearAll = false;
    left.length = 0.0f;
    left.regionLength = 0.0f;
    left.mix = 1.0f;
    right = left;
    right.pan = 1.0f;
    const float silence[4]{};
    float stoppedOutputLeft[4]{};
    float stoppedOutputRight[4]{};
    Process(looper,
            silence,
            silence,
            stoppedOutputLeft,
            stoppedOutputRight,
            4,
            left,
            right);
    Require(looper.TakeLoopEndEvents() == 0,
            "empty playback after full erase generates no loop-end flashes");
}

void TestPcmWavFormatAndConversion()
{
    uint8_t bytes[aurora::dsp::PcmWav::headerBytes]{};
    aurora::dsp::PcmWav::WriteHeader(bytes, 48000);
    aurora::dsp::PcmWav::Header header;
    Require(aurora::dsp::PcmWav::ParseHeader(bytes, sizeof(bytes), header)
                && header.frameCount == 48000,
            "PCM WAV header encodes 48 kHz stereo 16-bit audio");
    Require(aurora::dsp::PcmWav::ToPcm16(-1.0f) == -32768
                && aurora::dsp::PcmWav::ToPcm16(1.0f) == 32767
                && Near(aurora::dsp::PcmWav::FromPcm16(16384), 0.5f),
            "PCM16 conversion handles signed limits and scale");
    bytes[22] = 1;
    Require(!aurora::dsp::PcmWav::ParseHeader(bytes, sizeof(bytes), header),
            "unsupported mono WAV header is rejected for safe in-place edits");
}

void TestChannelModesAndPickup()
{
    using Controls = aurora::dsp::KarmaControlState;
    float inputs[Controls::ControlCount] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    Controls controls;
    controls.Initialize(inputs);
    Require(controls.GetMode() == Controls::Mode::Both, "default mode is LR");
    Require(Near(controls.GetChannel(0).controls[Controls::Time], inputs[Controls::Time])
                && Near(controls.GetChannel(1).controls[Controls::Time],
                        inputs[Controls::Time]),
            "initial channel parameters follow the physical control positions");

    controls.CycleMode(inputs);
    Require(controls.GetMode() == Controls::Mode::Left, "LR tap selects L");
    inputs[Controls::Warp] = 0.8f;
    controls.Update(inputs, false);
    Require(Near(controls.GetChannel(0).controls[Controls::Warp], 0.8f)
                && Near(controls.GetChannel(1).controls[Controls::Warp], 0.5f),
            "L mode updates only the left parameter set");

    controls.CycleMode(inputs);
    inputs[Controls::Warp] = 0.2f;
    controls.Update(inputs, false);
    Require(Near(controls.GetChannel(0).controls[Controls::Warp], 0.8f)
                && Near(controls.GetChannel(1).controls[Controls::Warp], 0.2f),
            "R mode retains independent parameters");

    controls.CycleMode(inputs);
    inputs[Controls::Warp] = 0.25f;
    controls.Update(inputs, false);
    Require(Near(controls.GetChannel(0).controls[Controls::Warp], 0.85f)
                && Near(controls.GetChannel(1).controls[Controls::Warp], 0.25f),
            "LR mode applies a latched relative offset to each channel");

    controls.Update(inputs, true);
    Require(Near(controls.GetChannel(0).pan, 0.0f)
                && Near(controls.GetChannel(1).pan, 1.0f),
            "entering a Shift control preserves its stored value");
    inputs[Controls::Mix] = 0.7f;
    controls.Update(inputs, true);
    Require(Near(controls.GetChannel(0).pan, 0.2f)
                && Near(controls.GetChannel(1).pan, 1.0f),
            "Shift+Mix applies a clamped LR pan offset");

    inputs[Controls::Reflect] = 0.25f;
    controls.Update(inputs, false);
    const float depth = controls.GetChannel(0).controls[Controls::Reflect];
    controls.Update(inputs, true);
    Require(Near(controls.GetChannel(0).crushMix, 0.25f)
                && Near(controls.GetChannel(1).crushMix, 0.25f),
            "Shift+Reflect sets wet mix from the full knob position");
    inputs[Controls::Reflect] = 1.0f;
    controls.Update(inputs, true);
    Require(Near(controls.GetChannel(0).crushMix, 1.0f)
                && Near(controls.GetChannel(0).controls[Controls::Reflect], depth),
            "Reflect wet mix reaches fully wet without changing bit depth");
}
}

int main()
{
    TestInitializationAndValidation();
    TestRecordPlaybackAndWrap();
    TestUnitySpeedRecordingPreservesEverySample();
    TestMixWindowAndPan();
    TestLoopWindowWrapsAcrossBufferEnd();
    TestFullLengthWindowWrapsWithinWorkingLoop();
    TestZeroSpeedRecordAndErase();
    TestModeSpecificRecording();
    TestEraseOutputFade();
    TestReversePlaybackAndControlCurves();
    TestSeamCrossfadeDoesNotCreatePeakBoost();
    TestReflectBitReductionIsOptIn();
    TestAtmosphereIsBypassedAndDeterministic();
    TestAtmosphereTransformationCoverage();
    TestRepeatMicroLoopDurationRange();
    TestRepeatCyclesCapturedLoopHistory();
    TestAtmosphereEventsCancelCleanly();
    TestPcmWavFormatAndConversion();
    TestFirstRecordCreatesLoopFromEmptyBuffer();
    TestRecordingDefersWindowChangesUntilRelease();
    TestFullEraseIsIncremental();
    TestChannelModesAndPickup();
    std::cout << "Stereo looper tests passed.\n";
}
