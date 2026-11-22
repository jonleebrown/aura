#pragma once

#include "aurora/dsp/KarmaEffects.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace aurora
{
namespace dsp
{
class StereoLooper
{
  public:
    enum class WindowUpdateMode
    {
        Smooth,
        Jump
    };

    struct ChannelParameters
    {
        float start = 0.0f;
        float length = 1.0f;
        float regionLength = 1.0f;
        float speed = 1.0f;
        float mix = 0.0f;
        float pan = 0.0f;
        bool record = false;
        bool recordFromStart = false;
        bool finishRecordAtFullWindow = false;
        bool erase = false;
        bool clearAll = false;
        float reflect = 0.0f;
        float reflectMix = 0.0f;
        // Read-head slip amount, normalized from 0 (off) to 1 (full).
        float slippage = 0.0f;
        float atmosphere = 0.0f;
        float dropoutProbabilities[KarmaEffects::DropoutProbabilityCount]{};
        float probabilityModulationDepth = 0.5f;
        WindowUpdateMode windowUpdateMode = WindowUpdateMode::Smooth;
        float glitchDuration = 0.5f;
    };

    struct DirtyRange
    {
        DirtyRange(bool dirtyValue = false,
                   std::size_t beginValue = 0,
                   std::size_t endValue = 0)
            : dirty(dirtyValue), begin(beginValue), end(endValue)
        {
        }

        bool dirty;
        std::size_t begin;
        std::size_t end;
    };

    bool Initialize(float* left,
                    float* right,
                    std::size_t capacity,
                    std::size_t initialLoopLength,
                    float* leftRepeatBuffer = nullptr,
                    float* rightRepeatBuffer = nullptr,
                    std::size_t repeatBufferCapacity = 0)
    {
        if (left == nullptr || right == nullptr || capacity == 0
            || initialLoopLength > capacity)
            return false;

        leftBuffer_ = left;
        rightBuffer_ = right;
        capacity_ = capacity;
        legacyStart_ = 0;
        legacyLength_ = initialLoopLength;
        repeatBuffers_[0] = leftRepeatBuffer;
        repeatBuffers_[1] = rightRepeatBuffer;
        repeatBufferCapacity_ = repeatBufferCapacity;
        Clear();
        return true;
    }

    void Clear()
    {
        if (leftBuffer_ == nullptr || rightBuffer_ == nullptr)
            return;

        for (std::size_t index = 0; index < capacity_; ++index)
        {
            leftBuffer_[index] = 0.0f;
            rightBuffer_[index] = 0.0f;
        }
        channels_[0] = ChannelState{};
        channels_[1] = ChannelState{};
        channels_[0].effects.SetRepeatBuffer(repeatBuffers_[0],
                                             repeatBufferCapacity_);
        channels_[1].effects.SetRepeatBuffer(repeatBuffers_[1],
                                             repeatBufferCapacity_);
        channels_[0].effects.Reset(0x13579BDFU);
        channels_[1].effects.Reset(0x2468ACE1U);
        channels_[0].readJitterRandomState = 0xA341316CU;
        channels_[1].readJitterRandomState = 0xC8013EA4U;
        recording_ = false;
        erasing_ = false;
        clearing_ = false;
        clearCompleted_ = false;
    }

    bool SetLoopRegion(std::size_t start, std::size_t length)
    {
        if (start >= capacity_ || length > capacity_)
            return false;

        legacyStart_ = start;
        legacyLength_ = length;
        channels_[0] = ChannelState{};
        channels_[1] = ChannelState{};
        channels_[0].effects.Reset(0x13579BDFU);
        channels_[1].effects.Reset(0x2468ACE1U);
        channels_[0].readJitterRandomState = 0xA341316CU;
        channels_[1].readJitterRandomState = 0xC8013EA4U;
        clearCompleted_ = false;
        return true;
    }

    std::size_t LoopLength() const { return legacyLength_; }
    bool IsRecording() const { return recording_; }
    bool IsErasing() const { return erasing_; }
    bool IsClearing() const { return clearing_; }

    bool TakeClearCompleted()
    {
        const bool completed = clearCompleted_;
        clearCompleted_ = false;
        return completed;
    }

    void StopPlayback()
    {
        legacyStart_ = 0;
        legacyLength_ = 0;
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            auto& state = channels_[channel];
            state.phase = 0.0f;
            state.recordStartPhase = 0.0f;
            state.writePosition = 0.0;
            state.currentStart = 0.0f;
            state.currentLength = 0.0f;
            state.currentRegionLength = 0.0f;
            state.initializedWindow = true;
            state.recording = false;
            state.recordingFromStart = false;
            state.erased = false;
            state.playbackWrapped = false;
            state.loopEnded = false;
            state.effects.Cancel();
        }
        recording_ = false;
        erasing_ = false;
    }

    uint8_t TakeLoopEndEvents()
    {
        uint8_t events = 0;
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            if (channels_[channel].loopEnded)
            {
                events |= static_cast<uint8_t>(1U << channel);
                channels_[channel].loopEnded = false;
            }
        }
        return events;
    }

    DirtyRange TakeDirtyRange(std::size_t channel)
    {
        if (channel >= 2)
            return {};
        auto& state = channels_[channel];
        const DirtyRange range{state.dirty, state.dirtyBegin, state.dirtyEnd};
        state.dirty = false;
        state.dirtyBegin = capacity_;
        state.dirtyEnd = 0;
        return range;
    }

    bool HasDirtyRange(std::size_t channel) const
    {
        return channel < 2 && channels_[channel].dirty;
    }

    void MarkDirtyRange(std::size_t channel,
                        std::size_t begin,
                        std::size_t end)
    {
        if (channel < 2 && begin < end && end <= capacity_)
            MarkDirty(channels_[channel], begin, end);
    }

    void ProcessBlock(const float* inputLeft,
                      const float* inputRight,
                      float* outputLeft,
                      float* outputRight,
                      std::size_t frameCount,
                      const ChannelParameters& leftParameters,
                      const ChannelParameters& rightParameters)
    {
        if (leftBuffer_ == nullptr || rightBuffer_ == nullptr)
            return;

        recording_ = false;
        erasing_ = false;
        clearing_ = false;
        AdvanceFullErase(channels_[0], leftBuffer_, leftParameters.clearAll);
        AdvanceFullErase(channels_[1], rightBuffer_, rightParameters.clearAll);
        const float leftMix = Clamp(leftParameters.mix);
        const float rightMix = Clamp(rightParameters.mix);
        const float leftDryGain = 1.0f - leftMix;
        const float leftWetGain = leftMix;
        const float rightDryGain = 1.0f - rightMix;
        const float rightWetGain = rightMix;
        const float leftPan = Clamp(leftParameters.pan);
        const float rightPan = Clamp(rightParameters.pan);
        const float leftPanGain = std::cos(leftPan * halfPi);
        const float leftRightGain = std::sin(leftPan * halfPi);
        const float rightPanGain = std::cos(rightPan * halfPi);
        const float rightRightGain = std::sin(rightPan * halfPi);
        for (std::size_t frame = 0; frame < frameCount; ++frame)
        {
            const float left = ProcessChannel(channels_[0],
                                              leftBuffer_,
                                              inputLeft[frame],
                                              leftParameters,
                                              leftDryGain,
                                              leftWetGain);
            const float right = ProcessChannel(channels_[1],
                                               rightBuffer_,
                                               inputRight[frame],
                                               rightParameters,
                                               rightDryGain,
                                               rightWetGain);
            outputLeft[frame] = left * leftPanGain + right * rightPanGain;
            outputRight[frame] = left * leftRightGain + right * rightRightGain;
        }
    }

    void ProcessBlock(const float* inputLeft,
                      const float* inputRight,
                      float* outputLeft,
                      float* outputRight,
                      std::size_t frameCount,
                      float mix,
                      bool record,
                      bool erase)
    {
        const float start = capacity_ == 0
                          ? 0.0f
                          : static_cast<float>(legacyStart_) / capacity_;
        const float length = capacity_ == 0
                           ? 0.0f
                           : static_cast<float>(legacyLength_) / capacity_;
        ChannelParameters left;
        left.start = start;
        left.length = length;
        left.mix = mix;
        left.pan = 0.0f;
        left.record = record;
        left.erase = erase;
        auto right = left;
        right.pan = 1.0f;
        ProcessBlock(inputLeft,
                     inputRight,
                     outputLeft,
                     outputRight,
                     frameCount,
                     left,
                     right);
    }

  private:
    struct ChannelState
    {
        enum class WindowFadePhase
        {
            Idle,
            Out,
            In
        };

        float phase = 0.0f;
        float slipPhase = 0.0f;
        float recordStartPhase = 0.0f;
        float recordSpeed = 1.0f;
        float eraseSpeed = 1.0f;
        double erasePosition = 0.0;
        uint32_t readJitterRandomState = 0xA341316CU;
        std::size_t readJitterSamplesRemaining = 0;
        float readJitterOffset = 0.0f;
        float readJitterTarget = 0.0f;
        float readJitterStep = 0.0f;
        double writePosition = 0.0;
        float currentStart = 0.0f;
        float currentLength = 0.0f;
        float currentRegionLength = 0.0f;
        bool initializedWindow = false;
        bool recording = false;
        bool recordingFromStart = false;
        bool erased = false;
        bool clearRequested = false;
        bool clearing = false;
        bool playbackWrapped = false;
        bool loopEnded = false;
        bool dirty = false;
        float eraseGain = 1.0f;
        float eraseHoldSample = 0.0f;
        WindowFadePhase windowFadePhase = WindowFadePhase::Idle;
        std::size_t windowFadeSamples = 0;
        std::size_t clearProgress = 0;
        std::size_t dirtyBegin = 0;
        std::size_t dirtyEnd = 0;
        KarmaEffects effects;
    };

    static constexpr float halfPi = 1.57079632679489661923f;
    static constexpr float windowSlewSamples = 240.0f;
    static constexpr float eraseFadeOutSamples = 48.0f;
    static constexpr float eraseFadeInSamples = 144.0f;
    static constexpr std::size_t jumpFadeSamples = 240;
    static constexpr float jumpWindowChangeThreshold = 0.00125f;
    static constexpr float readSlipMaxSamples = 8.0f;
    static constexpr float readSlipPhaseStep = 0.0000654498469498f;

    static float Clamp(float value)
    {
        return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
    }

    static void MarkDirty(ChannelState& state,
                          std::size_t begin,
                          std::size_t end)
    {
        if (begin >= end)
            return;
        if (!state.dirty)
        {
            state.dirty = true;
            state.dirtyBegin = begin;
            state.dirtyEnd = end;
        }
        else
        {
            if (begin < state.dirtyBegin)
                state.dirtyBegin = begin;
            if (end > state.dirtyEnd)
                state.dirtyEnd = end;
        }
    }

    static float WrapPhase(float phase)
    {
        phase -= std::floor(phase);
        return phase < 0.0f ? phase + 1.0f : phase;
    }

    static std::size_t RegionFrameCount(float regionLength)
    {
        const auto frames = static_cast<std::size_t>(std::floor(regionLength + 0.5f));
        return frames > 0 ? frames : 1;
    }

    static double WrapPosition(double position, double regionLength)
    {
        position -= std::floor(position / regionLength) * regionLength;
        return position < 0.0f ? position + regionLength : position;
    }

    float Read(const float* buffer,
               float position,
               float regionLength) const
    {
        const auto regionFrames = RegionFrameCount(regionLength);
        position = WrapPosition(position, static_cast<float>(regionFrames));
        const auto first = static_cast<std::size_t>(position);
        const auto second = (first + 1) % regionFrames;
        const float fraction = position - static_cast<float>(first);
        return buffer[first] + (buffer[second] - buffer[first]) * fraction;
    }

    float ReadWindow(const float* buffer,
                     float phase,
                     const ChannelState& state,
                     float speed) const
    {
        if (state.currentLength <= 0.0f)
            return 0.0f;

        const float fadeSamples = state.currentLength >= 2.0f
                                ? std::fmin(240.0f, state.currentLength * 0.125f)
                                : 0.0f;
        const float fadeFraction = fadeSamples / state.currentLength;
        const float start = state.currentStart;
        if (speed < 0.0f && state.playbackWrapped
            && fadeFraction > 0.0f && phase > 1.0f - fadeFraction)
        {
            const float progress = (1.0f - phase) / fadeFraction;
            const float head = Read(buffer,
                                    start + progress * fadeSamples,
                                    state.currentRegionLength);
            const float tail = Read(buffer,
                                    start + phase * state.currentLength,
                                    state.currentRegionLength);
            return head * (1.0f - progress) + tail * progress;
        }
        if (speed > 0.0f && fadeFraction > 0.0f
            && phase >= 1.0f - fadeFraction)
        {
            const float progress = (phase - (1.0f - fadeFraction)) / fadeFraction;
            const float tail = Read(buffer,
                                    start + phase * state.currentLength,
                                    state.currentRegionLength);
            const float head = Read(buffer,
                                    start + progress * fadeSamples,
                                    state.currentRegionLength);
            return tail * (1.0f - progress) + head * progress;
        }
        return Read(buffer,
                    start + phase * state.currentLength,
                    state.currentRegionLength);
    }

    float ProcessChannel(ChannelState& state,
                         float* buffer,
                         float input,
                         const ChannelParameters& parameters,
                         float dryGain,
                         float wetGain)
    {
        const float targetStart = Clamp(parameters.start)
                                * static_cast<float>(capacity_);
        const float targetLength = Clamp(parameters.length)
                                 * static_cast<float>(capacity_);
        const float targetRegionLength = Clamp(parameters.regionLength)
                                       * static_cast<float>(capacity_);
        const bool startingRecording = parameters.record && !state.recording;
        const bool stoppingRecording = !parameters.record && state.recording;
        const bool eraseNow = parameters.erase && !parameters.record;
        if (startingRecording)
        {
            state.windowFadePhase = ChannelState::WindowFadePhase::Idle;
            state.windowFadeSamples = 0;
        }
        if (!state.initializedWindow)
        {
            state.currentStart = targetStart;
            state.currentLength = targetLength;
            state.currentRegionLength = targetRegionLength;
            state.initializedWindow = true;
        }
        else if (stoppingRecording)
        {
            state.currentStart = targetStart;
            state.currentLength = targetLength;
            state.currentRegionLength = targetRegionLength;
            if (parameters.finishRecordAtFullWindow)
                state.phase = 0.0f;
        }
        else if (!state.recording && !startingRecording && !eraseNow)
        {
            if (parameters.windowUpdateMode == WindowUpdateMode::Smooth)
            {
                state.windowFadePhase = ChannelState::WindowFadePhase::Idle;
                state.windowFadeSamples = 0;
                if (targetLength == 0.0f)
                    state.currentLength = 0.0f;
                else
                {
                    state.currentStart += (targetStart - state.currentStart)
                                        / windowSlewSamples;
                    state.currentLength += (targetLength - state.currentLength)
                                         / windowSlewSamples;
                    state.currentRegionLength = targetRegionLength;
                }
            }
            else
            {
                const float changeThreshold =
                    std::fmax(1.0f,
                             targetRegionLength * jumpWindowChangeThreshold);
                const bool targetChanged =
                    std::fabs(targetStart - state.currentStart) > changeThreshold
                    || std::fabs(targetLength - state.currentLength) > changeThreshold
                    || std::fabs(targetRegionLength - state.currentRegionLength)
                           > changeThreshold;
                if (targetChanged
                    && state.windowFadePhase != ChannelState::WindowFadePhase::Out)
                {
                    state.windowFadePhase = ChannelState::WindowFadePhase::Out;
                    state.windowFadeSamples = 0;
                }
            }
        }

        if (startingRecording && parameters.recordFromStart)
        {
            state.currentStart = targetStart;
            state.currentLength = targetLength;
            state.currentRegionLength = targetRegionLength;
            state.phase = 0.0f;
        }

        if (state.clearing)
        {
            state.effects.Cancel();
            state.recording = false;
            state.erased = false;
            clearing_ = true;
            return input * dryGain;
        }

        if (parameters.record != state.recording)
        {
            if (parameters.record)
            {
                // A held overdub uses the speed that was selected when the
                // button went down. Both stereo channels latch independently.
                state.recordSpeed = parameters.speed;
                state.recordStartPhase = parameters.recordFromStart ? 0.0f : state.phase;
                state.writePosition = static_cast<double>(state.recordStartPhase)
                                    * state.currentLength;
                state.phase = state.recordStartPhase;
                state.playbackWrapped = false;
                state.recordingFromStart = parameters.recordFromStart;
            }
            else if (state.recording)
            {
                state.phase = state.recordingFromStart
                            ? 0.0f
                            : static_cast<float>(state.writePosition
                                                 / state.currentLength);
                state.recordingFromStart = false;
            }
            state.recording = parameters.record;
        }

        if (parameters.record && parameters.recordFromStart
            && !state.recordingFromStart)
        {
            state.currentStart = targetStart;
            state.currentLength = targetLength;
            state.currentRegionLength = targetRegionLength;
            state.recordStartPhase = 0.0f;
            state.writePosition = 0.0;
            state.phase = 0.0f;
            state.playbackWrapped = false;
            state.recordingFromStart = true;
        }

        const bool wasErasing = state.erased;
        if (eraseNow && !wasErasing)
        {
            // Freeze erases the playhead's window using the settings present
            // at button-down; later knob changes affect playback after release.
            state.eraseSpeed = parameters.speed;
            state.erasePosition = static_cast<double>(state.phase)
                                * state.currentLength;
        }
        recording_ = recording_ || state.recording;
        erasing_ = erasing_ || eraseNow;

        float wet = 0.0f;
        float playbackSpeed = parameters.speed;
        if (state.currentLength > 0.0f)
        {
            const float slippage = Clamp(parameters.slippage);
            const float readJitter = !state.recording && !eraseNow
                                   ? AdvanceReadJitter(state, slippage)
                                   : 0.0f;
            const float slipOffset = std::sin(state.slipPhase)
                                   * readSlipMaxSamples * slippage
                                   + readJitter;
            const float readPhase = WrapPhase(
                state.phase + slipOffset / state.currentLength);
            wet = ReadWindow(buffer, readPhase, state, parameters.speed);
            if (!state.recording && slippage > 0.0f)
            {
                state.slipPhase += readSlipPhaseStep;
                if (state.slipPhase >= 2.0f * 3.14159265358979323846f)
                    state.slipPhase -= 2.0f * 3.14159265358979323846f;
            }
        }
        if (!state.recording && !eraseNow && state.currentLength > 0.0f)
        {
            KarmaEffects::Parameters effectParameters;
            effectParameters.reflect = parameters.reflect;
            effectParameters.reflectMix = parameters.reflectMix;
            effectParameters.atmosphere = parameters.atmosphere;
            for (std::size_t i = 0;
                 i < KarmaEffects::DropoutProbabilityCount;
                 ++i)
                effectParameters.dropoutProbabilities[i] =
                    parameters.dropoutProbabilities[i];
            effectParameters.probabilityModulationDepth =
                parameters.probabilityModulationDepth;
            effectParameters.glitchDuration = parameters.glitchDuration;
            effectParameters.speed = parameters.speed;
            effectParameters.loopSamples = static_cast<std::size_t>(
                state.currentLength + 0.5f);
            const auto effectResult = state.effects.Process(wet, effectParameters);
            wet = effectResult.sample;
            playbackSpeed = effectResult.speed;
        }
        else
            state.effects.Cancel();
        if (eraseNow && !wasErasing)
            state.eraseHoldSample = wet;

        const float targetEraseGain = eraseNow ? 0.0f : 1.0f;
        const float eraseFadeSamples = eraseNow ? eraseFadeOutSamples
                                                : eraseFadeInSamples;
        const float eraseGainStep = 1.0f / eraseFadeSamples;
        if (state.eraseGain < targetEraseGain)
            state.eraseGain = std::fmin(targetEraseGain,
                                        state.eraseGain + eraseGainStep);
        else if (state.eraseGain > targetEraseGain)
            state.eraseGain = std::fmax(targetEraseGain,
                                        state.eraseGain - eraseGainStep);
        float windowTransitionGain = 1.0f;
        if (parameters.windowUpdateMode == WindowUpdateMode::Jump
            && !state.recording)
        {
            if (state.windowFadePhase == ChannelState::WindowFadePhase::Out)
                windowTransitionGain = 1.0f
                    - static_cast<float>(state.windowFadeSamples)
                        / static_cast<float>(jumpFadeSamples);
            else if (state.windowFadePhase == ChannelState::WindowFadePhase::In)
                windowTransitionGain = static_cast<float>(state.windowFadeSamples)
                                     / static_cast<float>(jumpFadeSamples);
        }
        const float wetOutput = eraseNow
                              ? state.eraseHoldSample * state.eraseGain
                              : wet * state.eraseGain * windowTransitionGain;

        if (state.currentLength > 0.0f)
        {
            if (eraseNow)
            {
                const auto regionFrames = RegionFrameCount(
                    state.currentRegionLength);
                const double windowPosition = WrapPosition(
                    state.erasePosition,
                    static_cast<double>(state.currentLength));
                const auto index = static_cast<std::size_t>(WrapPosition(
                    static_cast<double>(state.currentStart) + windowPosition,
                    static_cast<double>(regionFrames)));
                buffer[index] = 0.0f;
                MarkDirty(state, index, index + 1);
                AdvanceErase(state, state.eraseSpeed);
            }
            else if (state.recording)
            {
                const auto regionFrames = RegionFrameCount(
                    state.currentRegionLength);
                const double position = WrapPosition(
                    static_cast<double>(state.currentStart) + state.writePosition,
                    static_cast<double>(regionFrames));
                const auto index = static_cast<std::size_t>(position);
                buffer[index] = input;
                MarkDirty(state, index, index + 1);
                const float speed = state.recordSpeed;
                if (speed != 0.0f)
                {
                    const double next = state.writePosition + speed;
                    const double loopLength =
                        static_cast<double>(state.currentLength);
                    if ((speed > 0.0f && next >= loopLength)
                        || (speed < 0.0f && next < 0.0))
                    {
                        state.playbackWrapped = true;
                        state.loopEnded = true;
                    }
                    state.writePosition = WrapPosition(next, loopLength);

                    state.phase = static_cast<float>(state.writePosition
                                                     / loopLength);
                }
            }
            else
                AdvancePlayback(state, playbackSpeed);
        }

        if (state.recording)
        {
            state.erased = false;
            return input;
        }
        state.erased = eraseNow;

        if (parameters.windowUpdateMode == WindowUpdateMode::Jump
            && !state.recording
            && state.windowFadePhase != ChannelState::WindowFadePhase::Idle)
        {
            ++state.windowFadeSamples;
            if (state.windowFadeSamples >= jumpFadeSamples)
            {
                if (state.windowFadePhase == ChannelState::WindowFadePhase::Out)
                {
                    state.currentStart = targetStart;
                    state.currentLength = targetLength;
                    state.currentRegionLength = targetRegionLength;
                    state.windowFadePhase = ChannelState::WindowFadePhase::In;
                    state.windowFadeSamples = 0;
                }
                else
                {
                    state.windowFadePhase = ChannelState::WindowFadePhase::Idle;
                    state.windowFadeSamples = 0;
                }
            }
        }
        return input * dryGain + wetOutput * wetGain;
    }

    static void AdvancePlayback(ChannelState& state, float speed)
    {
        if (speed == 0.0f || state.currentLength <= 0.0f)
            return;

        const float next = state.phase + speed / state.currentLength;
        if (speed > 0.0f && next >= 1.0f)
        {
            state.loopEnded = true;
            state.playbackWrapped = true;
            const float overflow = next - std::floor(next);
            const float fade = std::fmin(240.0f, state.currentLength * 0.125f)
                             / state.currentLength;
            state.phase = WrapPhase(fade + overflow);
        }
        else
        {
            if (speed < 0.0f && next < 0.0f)
            {
                state.loopEnded = true;
                state.playbackWrapped = true;
            }
            state.phase = WrapPhase(next);
        }
    }

    static void AdvanceErase(ChannelState& state, float speed)
    {
        if (speed == 0.0f || state.currentLength <= 0.0f)
            return;

        const double loopLength = static_cast<double>(state.currentLength);
        const double next = state.erasePosition + static_cast<double>(speed);
        if ((speed > 0.0f && next >= loopLength)
            || (speed < 0.0f && next < 0.0))
        {
            state.loopEnded = true;
            state.playbackWrapped = true;
        }
        state.erasePosition = WrapPosition(next, loopLength);
        state.phase = static_cast<float>(state.erasePosition / loopLength);
    }

    static float AdvanceReadJitter(ChannelState& state, float amount)
    {
        if (amount <= 0.0f)
        {
            state.readJitterSamplesRemaining = 0;
            state.readJitterOffset = 0.0f;
            state.readJitterTarget = 0.0f;
            state.readJitterStep = 0.0f;
            return 0.0f;
        }

        if (state.readJitterSamplesRemaining == 0)
        {
            uint32_t value = state.readJitterRandomState;
            value ^= value << 13;
            value ^= value >> 17;
            value ^= value << 5;
            state.readJitterRandomState = value == 0 ? 0xA341316CU : value;
            const float unit = static_cast<float>(value >> 8)
                             * (1.0f / 16777215.0f);
            state.readJitterTarget = (unit * 2.0f - 1.0f)
                                   * 3.0f * amount;
            state.readJitterSamplesRemaining = 96;
            state.readJitterStep = (state.readJitterTarget
                                    - state.readJitterOffset)
                                 / 96.0f;
        }

        state.readJitterOffset += state.readJitterStep;
        --state.readJitterSamplesRemaining;
        return state.readJitterOffset;
    }

    void AdvanceFullErase(ChannelState& state, float* buffer, bool requested)
    {
        if (requested && !state.clearRequested)
        {
            state.clearRequested = true;
            state.clearProgress = 0;
            state.clearing = true;
            state.recording = false;
            state.erased = false;
            clearCompleted_ = false;
        }
        else if (!requested)
            state.clearRequested = false;

        if (!state.clearing)
            return;

        constexpr std::size_t samplesPerBlock = 8192;
        const std::size_t end = state.clearProgress + samplesPerBlock < capacity_
                              ? state.clearProgress + samplesPerBlock
                              : capacity_;
        for (std::size_t index = state.clearProgress; index < end; ++index)
            buffer[index] = 0.0f;
        MarkDirty(state, state.clearProgress, end);
        state.clearProgress = end;
        if (state.clearProgress == capacity_)
        {
            state.clearing = false;
            state.clearProgress = 0;
            state.phase = 0.0f;
            state.writePosition = 0.0;
            state.playbackWrapped = false;
            clearCompleted_ = true;
        }
    }

    float* leftBuffer_ = nullptr;
    float* rightBuffer_ = nullptr;
    float* repeatBuffers_[2]{};
    std::size_t repeatBufferCapacity_ = 0;
    std::size_t capacity_ = 0;
    std::size_t legacyStart_ = 0;
    std::size_t legacyLength_ = 0;
    ChannelState channels_[2]{};
    bool recording_ = false;
    bool erasing_ = false;
    bool clearing_ = false;
    bool clearCompleted_ = false;
};
}
}
