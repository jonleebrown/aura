#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace aurora
{
namespace dsp
{
class KarmaEffects
{
  public:
    enum DropoutProbability : std::size_t
    {
        StutterProbability,
        NoiseProbability,
        SampleReductionProbability,
        LevelDropProbability,
        DropoutProbabilityCount
    };

    enum class Glitch
    {
        None,
        VolumeCut,
        ReverseBurst,
        SampleRateReduction,
        Noise,
        Repeat
    };

    struct Parameters
    {
        float reflect = 0.0f;
        float reflectMix = 0.0f;
        float atmosphere = 0.0f;
        float dropoutProbabilities[DropoutProbabilityCount]{};
        float probabilityModulationDepth = 0.5f;
        float glitchDuration = 0.0f;
        float speed = 1.0f;
        std::size_t loopSamples = 0;
    };

    struct Result
    {
        Result(float sampleValue = 0.0f, float speedValue = 0.0f)
            : sample(sampleValue), speed(speedValue)
        {
        }

        float sample;
        float speed;
    };

    void SetRepeatBuffer(float* buffer, std::size_t capacity)
    {
        repeatHistory_ = buffer;
        repeatHistoryCapacity_ = capacity < maximumRepeatSamples_
                               ? capacity
                               : maximumRepeatSamples_;
        historyWriteIndex_ = 0;
        historySamples_ = 0;
    }

    void Reset(uint32_t seed)
    {
        randomState_ = seed == 0 ? 0x6D2B79F5U : seed;
        schedulerSamples_ = 0;
        eventSamples_ = 0;
        eventPosition_ = 0;
        holdSamples_ = 0;
        holdPeriod_ = 4;
        eventCount_ = 0;
        historyWriteIndex_ = 0;
        historySamples_ = 0;
        repeatStartIndex_ = 0;
        repeatLengthSamples_ = 0;
        heldSample_ = 0.0f;
        pinkState_[0] = 0.0f;
        pinkState_[1] = 0.0f;
        pinkState_[2] = 0.0f;
        for (std::size_t i = 0; i < DropoutProbabilityCount; ++i)
        {
            lfoPhase_[i] = RandomUnit() * 2.0f * pi_;
            const float periodSeconds = 20.0f + RandomUnit() * 20.0f;
            lfoPhaseStep_[i] = 2.0f * pi_
                             / (static_cast<float>(sampleRate_) * periodSeconds);
        }
        glitch_ = Glitch::None;
        volumeGain_ = 0.0f;
        speedSign_ = 1.0f;
        noiseGain_ = 0.0f;
        currentDuration_ = 0.5f;
    }

    Result Process(float sample, const Parameters& parameters)
    {
        const float atmosphere = Clamp(parameters.atmosphere);
        float baselineTotal = 0.0f;
        for (std::size_t i = 0; i < DropoutProbabilityCount; ++i)
            baselineTotal += Clamp(parameters.dropoutProbabilities[i]);
        const float amount = atmosphere > baselineTotal
                           ? atmosphere
                           : baselineTotal > 1.0f ? 1.0f : baselineTotal;
        const float speed = parameters.speed;
        currentDuration_ = Clamp(parameters.glitchDuration);
        AdvanceLfos();
        if (glitch_ != Glitch::Repeat || eventSamples_ == 0)
            PushHistory(sample);
        if (amount <= 0.0f || parameters.loopSamples == 0)
        {
            CancelGlitch();
            return {ApplyDegrade(sample, parameters.reflect, parameters.reflectMix),
                    speed};
        }

        if (eventSamples_ == 0)
        {
            if (schedulerSamples_ == 0)
                ScheduleNext(amount,
                             parameters.loopSamples,
                             parameters.dropoutProbabilities,
                             Clamp(parameters.probabilityModulationDepth));
            else
                --schedulerSamples_;
        }

        float processed = sample;
        float processedSpeed = speed;
        if (eventSamples_ > 0)
        {
            const float envelope = EventEnvelope();
            switch (glitch_)
            {
                case Glitch::VolumeCut:
                    processed *= 1.0f + (volumeGain_ - 1.0f) * envelope;
                    break;
                case Glitch::ReverseBurst:
                    processedSpeed = speed
                                   * (1.0f + (speedSign_ - 1.0f) * envelope);
                    break;
                case Glitch::SampleRateReduction:
                    if (holdSamples_ == 0)
                    {
                        heldSample_ = sample;
                        holdSamples_ = holdPeriod_;
                    }
                    processed = heldSample_ * envelope + sample * (1.0f - envelope);
                    --holdSamples_;
                    break;
                case Glitch::Noise:
                    processed += NextNoise() * noiseGain_ * envelope;
                    break;
                case Glitch::Repeat:
                {
                    const std::size_t repeatPosition =
                        eventPosition_ % repeatLengthSamples_;
                    float repeated = repeatHistory_[
                        (repeatStartIndex_ + repeatPosition)
                        % repeatHistoryCapacity_];
                    const std::size_t fade = repeatLengthSamples_ / 4 < fadeSamples_
                                           ? repeatLengthSamples_ / 4
                                           : fadeSamples_;
                    if (fade > 0 && repeatPosition < fade)
                    {
                        const float progress =
                            static_cast<float>(repeatPosition + 1)
                            / static_cast<float>(fade);
                        const std::size_t tailPosition =
                            (repeatStartIndex_ + repeatLengthSamples_ - fade
                             + repeatPosition) % repeatHistoryCapacity_;
                        repeated = repeatHistory_[tailPosition] * (1.0f - progress)
                                 + repeated * progress;
                    }
                    processed = sample * (1.0f - envelope) + repeated * envelope;
                    processedSpeed = 0.0f;
                    break;
                }
                case Glitch::None:
                    break;
            }

            ++eventPosition_;
            if (eventPosition_ >= eventSamples_)
                EndGlitch();
        }

        return {ApplyDegrade(processed, parameters.reflect, parameters.reflectMix),
                processedSpeed};
    }

    void Cancel()
    {
        CancelGlitch();
        schedulerSamples_ = 0;
    }

    Glitch ActiveGlitch() const { return glitch_; }
    std::size_t RemainingEventSamples() const
    {
        return eventSamples_ > eventPosition_
             ? eventSamples_ - eventPosition_
             : 0;
    }
    std::size_t RepeatLengthSamples() const { return repeatLengthSamples_; }
    std::size_t EventCount() const { return eventCount_; }

  private:
    static constexpr uint32_t sampleRate_ = 48000;
    static constexpr std::size_t minimumEventSamples_ = sampleRate_ / 100;
    static constexpr std::size_t minimumRepeatSliceSamples_ = sampleRate_ / 100;
    static constexpr std::size_t fadeSamples_ = 144;
    static constexpr std::size_t maximumRepeatSamples_ = sampleRate_ * 2;
    static constexpr std::size_t repeatCount_ = 4;
    static constexpr std::size_t maximumLoopSamples_ = 40U * sampleRate_;
    static constexpr float pi_ = 3.14159265358979323846f;
    static constexpr float maximumEventProbability_ = 0.9f;

    static float Clamp(float value)
    {
        return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
    }

    uint32_t NextRandom()
    {
        randomState_ ^= randomState_ << 13;
        randomState_ ^= randomState_ >> 17;
        randomState_ ^= randomState_ << 5;
        return randomState_;
    }

    float RandomUnit()
    {
        return static_cast<float>(NextRandom() >> 8) / 16777216.0f;
    }

    void AdvanceLfos()
    {
        for (std::size_t i = 0; i < DropoutProbabilityCount; ++i)
        {
            lfoPhase_[i] += lfoPhaseStep_[i];
            if (lfoPhase_[i] >= 2.0f * pi_)
                lfoPhase_[i] -= 2.0f * pi_;
        }
    }

    float NextNoise()
    {
        const float white = RandomUnit() * 2.0f - 1.0f;
        if (!pinkNoise_)
            return white;

        pinkState_[0] = 0.99765f * pinkState_[0] + white * 0.0990460f;
        pinkState_[1] = 0.96300f * pinkState_[1] + white * 0.2965164f;
        pinkState_[2] = 0.57000f * pinkState_[2] + white * 1.0526913f;
        const float pink = (pinkState_[0] + pinkState_[1] + pinkState_[2]
                            + white * 0.1848f) * 0.18f;
        return pink < -1.0f ? -1.0f : pink > 1.0f ? 1.0f : pink;
    }

    void ScheduleNext(float amount,
                      std::size_t loopSamples,
                      const float (&probabilities)[DropoutProbabilityCount],
                      float modulationDepth)
    {
        schedulerSamples_ = static_cast<std::size_t>(
            4800.0f - amount * 4080.0f);

        float weights[DropoutProbabilityCount]{};
        float totalWeight = 0.0f;
        for (std::size_t i = 0; i < DropoutProbabilityCount; ++i)
        {
            const float baseline = Clamp(probabilities[i]);
            const float lfo = std::sin(lfoPhase_[i]);
            weights[i] = baseline * (1.0f + modulationDepth * lfo);
            totalWeight += weights[i];
        }
        const float eventProbability = totalWeight < maximumEventProbability_
                                     ? totalWeight
                                     : maximumEventProbability_;
        if (totalWeight <= 0.0f || RandomUnit() >= eventProbability)
            return;

        float selection = RandomUnit() * totalWeight;
        std::size_t selected = 0;
        for (; selected + 1 < DropoutProbabilityCount; ++selected)
        {
            if (selection < weights[selected])
                break;
            selection -= weights[selected];
        }

        const std::size_t boundedLoop = loopSamples < maximumLoopSamples_
                                      ? loopSamples
                                      : maximumLoopSamples_;
        if (selected == StutterProbability)
        {
            if (historySamples_ == 0)
                return;
            const std::size_t eventDuration = EventDurationSamples(
                boundedLoop,
                minimumRepeatSliceSamples_ * repeatCount_);
            repeatLengthSamples_ = (eventDuration + repeatCount_ - 1)
                                 / repeatCount_;
            if (repeatLengthSamples_ > maximumRepeatSamples_)
                repeatLengthSamples_ = maximumRepeatSamples_;
            if (repeatLengthSamples_ > boundedLoop)
                repeatLengthSamples_ = boundedLoop;
            if (repeatLengthSamples_ == 0
                || historySamples_ < repeatLengthSamples_)
            {
                repeatLengthSamples_ = 0;
                return;
            }
            repeatStartIndex_ = (historyWriteIndex_ + repeatHistoryCapacity_
                                 - repeatLengthSamples_)
                              % repeatHistoryCapacity_;
            eventSamples_ = repeatLengthSamples_ * repeatCount_;
            glitch_ = Glitch::Repeat;
        }
        else
        {
            eventSamples_ = EventDurationSamples(boundedLoop,
                                                  minimumEventSamples_);
            if (eventSamples_ == 0)
                return;

            switch (selected)
            {
            case LevelDropProbability:
                glitch_ = Glitch::VolumeCut;
                volumeGain_ = (NextRandom() & 1U) == 0 ? 0.0f : 0.25f;
                break;
            case SampleReductionProbability:
                glitch_ = Glitch::SampleRateReduction;
                holdPeriod_ = 4U + NextRandom() % 29U;
                holdSamples_ = 0;
                break;
            default:
                glitch_ = Glitch::Noise;
                noiseGain_ = 0.1f + RandomUnit() * 0.15f;
                pinkNoise_ = (NextRandom() & 1U) != 0;
                break;
            }
        }
        eventPosition_ = 0;
        ++eventCount_;
    }

    std::size_t EventDurationSamples(std::size_t loopSamples,
                                     std::size_t minimumSamples)
    {
        const float durationControl = Clamp(currentDuration_);
        const std::size_t lowerBound = loopSamples < minimumSamples
                                     ? loopSamples
                                     : minimumSamples;
        const float durationRange = static_cast<float>(loopSamples
                                                       - lowerBound);
        const float targetDuration = static_cast<float>(lowerBound)
                                   + durationControl * durationRange;
        const float jitterDepth = durationControl * 0.5f;
        const float durationJitter = 1.0f
            + (RandomUnit() * 2.0f - 1.0f) * jitterDepth;
        const float requested = targetDuration * durationJitter;
        std::size_t duration = static_cast<std::size_t>(requested + 0.5f);
        if (duration < lowerBound)
            duration = lowerBound;
        if (duration > loopSamples)
            duration = loopSamples;
        return duration;
    }

    void PushHistory(float sample)
    {
        if (repeatHistory_ == nullptr || repeatHistoryCapacity_ == 0)
            return;
        repeatHistory_[historyWriteIndex_] = sample;
        historyWriteIndex_ = (historyWriteIndex_ + 1) % repeatHistoryCapacity_;
        if (historySamples_ < repeatHistoryCapacity_)
            ++historySamples_;
    }

    float EventEnvelope() const
    {
        const std::size_t fade = eventSamples_ / 2 < fadeSamples_
                               ? eventSamples_ / 2
                               : fadeSamples_;
        if (fade == 0)
            return 1.0f;
        if (eventPosition_ < fade)
            return static_cast<float>(eventPosition_ + 1)
                 / static_cast<float>(fade);
        const std::size_t remaining = eventSamples_ - eventPosition_;
        if (remaining <= fade)
            return static_cast<float>(remaining)
                 / static_cast<float>(fade);
        return 1.0f;
    }

    static float ApplyDegrade(float sample, float reflect, float reflectMix)
    {
        const float depth = Clamp(reflect);
        const float wet = Clamp(reflectMix);
        if (depth <= 0.0f || wet <= 0.0f)
            return sample;

        const float drive = 1.0f + depth * 1.5f;
        const float driven = std::tanh(sample * drive);
        const float bitCurve = std::sqrt(depth)
                             * std::sqrt(std::sqrt(depth));
        const float bits = 16.0f - bitCurve * 14.0f;
        const float bitExponent = bits - 1.0f;
        const int wholeBits = static_cast<int>(bitExponent);
        const float bitFraction = bitExponent - static_cast<float>(wholeBits);
        const float lowerLevels = static_cast<float>((1U << wholeBits) - 1U);
        const float upperLevels = static_cast<float>((1U << (wholeBits + 1)) - 1U);
        const float levels = lowerLevels
                           + (upperLevels - lowerLevels) * bitFraction;
        const float scaled = driven * levels;
        const float quantized = (scaled >= 0.0f ? std::floor(scaled + 0.5f)
                                                : std::ceil(scaled - 0.5f))
                              / levels;
        return sample + (quantized - sample) * wet;
    }

    void CancelGlitch()
    {
        eventSamples_ = 0;
        eventPosition_ = 0;
        holdSamples_ = 0;
        repeatLengthSamples_ = 0;
        glitch_ = Glitch::None;
    }

    void EndGlitch()
    {
        eventSamples_ = 0;
        eventPosition_ = 0;
        holdSamples_ = 0;
        repeatLengthSamples_ = 0;
        glitch_ = Glitch::None;
    }

    uint32_t randomState_ = 0x6D2B79F5U;
    std::size_t schedulerSamples_ = 0;
    std::size_t eventSamples_ = 0;
    std::size_t eventPosition_ = 0;
    std::size_t holdSamples_ = 0;
    std::size_t holdPeriod_ = 4;
    std::size_t eventCount_ = 0;
    std::size_t historyWriteIndex_ = 0;
    std::size_t historySamples_ = 0;
    std::size_t repeatHistoryCapacity_ = 0;
    std::size_t repeatStartIndex_ = 0;
    std::size_t repeatLengthSamples_ = 0;
    float heldSample_ = 0.0f;
    float volumeGain_ = 0.0f;
    float speedSign_ = 1.0f;
    float noiseGain_ = 0.0f;
    float currentDuration_ = 0.5f;
    float pinkState_[3]{};
    float lfoPhase_[DropoutProbabilityCount]{};
    float lfoPhaseStep_[DropoutProbabilityCount]{};
    float* repeatHistory_ = nullptr;
    Glitch glitch_ = Glitch::None;
    bool pinkNoise_ = false;
};
}
}
