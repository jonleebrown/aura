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
    enum class Glitch
    {
        None,
        VolumeCut,
        ReverseBurst,
        SampleRateReduction,
        Noise
    };

    struct Parameters
    {
        float reflect = 0.0f;
        float reflectMix = 0.0f;
        float atmosphere = 0.0f;
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

    void Reset(uint32_t seed)
    {
        randomState_ = seed == 0 ? 0x6D2B79F5U : seed;
        schedulerSamples_ = 0;
        eventSamples_ = 0;
        eventPosition_ = 0;
        holdSamples_ = 0;
        holdPeriod_ = 4;
        eventCount_ = 0;
        heldSample_ = 0.0f;
        pinkState_[0] = 0.0f;
        pinkState_[1] = 0.0f;
        pinkState_[2] = 0.0f;
        glitch_ = Glitch::None;
        volumeGain_ = 0.0f;
        speedSign_ = 1.0f;
        noiseGain_ = 0.0f;
        currentDuration_ = 0.5f;
    }

    Result Process(float sample, const Parameters& parameters)
    {
        const float amount = Clamp(parameters.atmosphere);
        const float speed = parameters.speed;
        currentDuration_ = Clamp(parameters.glitchDuration);
        if (amount <= 0.0f || parameters.loopSamples == 0)
        {
            CancelGlitch();
            return {ApplyReflect(sample, parameters.reflect, parameters.reflectMix),
                    speed};
        }

        if (eventSamples_ == 0)
        {
            if (schedulerSamples_ == 0)
                ScheduleNext(amount, parameters.loopSamples);
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
                case Glitch::None:
                    break;
            }

            ++eventPosition_;
            if (eventPosition_ >= eventSamples_)
                EndGlitch();
        }

        return {ApplyReflect(processed, parameters.reflect, parameters.reflectMix),
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
    std::size_t EventCount() const { return eventCount_; }

  private:
    static constexpr uint32_t sampleRate_ = 48000;
    static constexpr std::size_t minimumEventSamples_ = 240;
    static constexpr std::size_t fadeSamples_ = 144;
    static constexpr std::size_t maximumLoopSamples_ = 40U * sampleRate_;

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

    void ScheduleNext(float amount, std::size_t loopSamples)
    {
        schedulerSamples_ = static_cast<std::size_t>(
            4800.0f - amount * 4080.0f);
        if (RandomUnit() >= amount * 0.9f)
            return;

        const std::size_t boundedLoop = loopSamples < maximumLoopSamples_
                                      ? loopSamples
                                      : maximumLoopSamples_;
        const float durationRatio = 0.001f + Clamp(currentDuration_) * 0.999f;
        const float durationJitter = 0.5f + RandomUnit();
        const float requested = static_cast<float>(boundedLoop)
                              * durationRatio * durationJitter;
        eventSamples_ = static_cast<std::size_t>(requested);
        if (eventSamples_ < minimumEventSamples_)
            eventSamples_ = boundedLoop < minimumEventSamples_
                          ? boundedLoop
                          : minimumEventSamples_;
        if (eventSamples_ > boundedLoop)
            eventSamples_ = boundedLoop;
        if (eventSamples_ == 0)
            return;

        switch (NextRandom() % 4U)
        {
            case 0:
                glitch_ = Glitch::VolumeCut;
                volumeGain_ = (NextRandom() & 1U) == 0 ? 0.0f : 0.25f;
                break;
            case 1:
                glitch_ = Glitch::ReverseBurst;
                speedSign_ = -1.0f;
                break;
            case 2:
                glitch_ = Glitch::SampleRateReduction;
                holdPeriod_ = 4U + NextRandom() % 29U;
                holdSamples_ = 0;
                break;
            default:
                glitch_ = Glitch::Noise;
                noiseGain_ = 0.2f + RandomUnit() * 0.3f;
                pinkNoise_ = (NextRandom() & 1U) != 0;
                break;
        }
        eventPosition_ = 0;
        ++eventCount_;
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

    static float ApplyReflect(float sample, float reflect, float reflectMix)
    {
        const float depth = Clamp(reflect);
        const float wet = Clamp(reflectMix);
        if (depth <= 0.0f || wet <= 0.0f)
            return sample;

        const int bits = 16 - static_cast<int>(std::floor(depth * 14.0f + 0.5f));
        const int levels = (1 << (bits - 1)) - 1;
        const float scaled = sample * static_cast<float>(levels);
        const float quantized = (scaled >= 0.0f ? std::floor(scaled + 0.5f)
                                                : std::ceil(scaled - 0.5f))
                              / static_cast<float>(levels);
        return sample + (quantized - sample) * wet;
    }

    void CancelGlitch()
    {
        eventSamples_ = 0;
        eventPosition_ = 0;
        holdSamples_ = 0;
        glitch_ = Glitch::None;
    }

    void EndGlitch()
    {
        eventSamples_ = 0;
        eventPosition_ = 0;
        holdSamples_ = 0;
        glitch_ = Glitch::None;
    }

    uint32_t randomState_ = 0x6D2B79F5U;
    std::size_t schedulerSamples_ = 0;
    std::size_t eventSamples_ = 0;
    std::size_t eventPosition_ = 0;
    std::size_t holdSamples_ = 0;
    std::size_t holdPeriod_ = 4;
    std::size_t eventCount_ = 0;
    float heldSample_ = 0.0f;
    float volumeGain_ = 0.0f;
    float speedSign_ = 1.0f;
    float noiseGain_ = 0.0f;
    float currentDuration_ = 0.5f;
    float pinkState_[3]{};
    Glitch glitch_ = Glitch::None;
    bool pinkNoise_ = false;
};
}
}
