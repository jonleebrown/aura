#pragma once

#include <cmath>
#include <cstddef>

namespace aurora
{
namespace dsp
{
class KarmaControlState
{
  public:
    enum class Mode
    {
        Left,
        Right,
        Both
    };

    enum Control : std::size_t
    {
        Warp,
        Time,
        Blur,
        Reflect,
        Atmosphere,
        Mix,
        ControlCount
    };

    struct Channel
    {
        float controls[ControlCount]{};
        float pan = 0.0f;
        float crushMix = 0.0f;
        float glitchDuration = 0.5f;
    };

    static float MapTimeRatio(float control)
    {
        control = Clamp(control);
        return control * control;
    }

    static float MapBlurSpeed(float control)
    {
        control = Clamp(control);
        constexpr float reverseEdge = 0.48f;
        constexpr float forwardEdge = 0.52f;
        if (control < reverseEdge)
        {
            const float amount = (reverseEdge - control) / reverseEdge;
            return -2.0f * (std::pow(10.0f, amount) - 1.0f) / 9.0f;
        }
        if (control > forwardEdge)
        {
            const float amount = (control - forwardEdge) / (1.0f - forwardEdge);
            return 2.0f * (std::pow(10.0f, amount) - 1.0f) / 9.0f;
        }
        return 0.0f;
    }

    void Initialize(const float (&inputs)[ControlCount])
    {
        for (std::size_t control = 0; control < ControlCount; ++control)
        {
            channels_[0].controls[control] = Clamp(inputs[control]);
            channels_[1].controls[control] = Clamp(inputs[control]);
            lrLatch_[control] = Clamp(inputs[control]);
        }
        channels_[0].pan = 0.0f;
        channels_[1].pan = 1.0f;
        mode_ = Mode::Both;
        initialized_ = true;
        SaveCombinedBase();
    }

    void Update(const float (&inputs)[ControlCount], bool shiftHeld)
    {
        if (!initialized_)
        {
            Initialize(inputs);
            return;
        }

        if (shiftHeld && !shiftWasHeld_)
        {
            shiftBase_[0] = channels_[0];
            shiftBase_[1] = channels_[1];
            for (std::size_t control = 0; control < ControlCount; ++control)
                shiftLatch_[control] = Clamp(inputs[control]);
        }
        shiftWasHeld_ = shiftHeld;

        if (mode_ == Mode::Both)
        {
            for (std::size_t control = 0; control < ControlCount; ++control)
            {
                if (shiftHeld && IsSecondary(control))
                    ApplySecondary(control,
                                   Clamp(inputs[control]) - shiftLatch_[control],
                                   Clamp(inputs[control]));
                else
                {
                    for (std::size_t channel = 0; channel < 2; ++channel)
                        channels_[channel].controls[control] =
                            Clamp(combinedBase_[channel].controls[control]
                                  + Clamp(inputs[control]) - lrLatch_[control]);
                }
            }
        }
        else
        {
            const std::size_t channel = mode_ == Mode::Left ? 0 : 1;
            for (std::size_t control = 0; control < ControlCount; ++control)
            {
                if (shiftHeld && IsSecondary(control))
                    ApplySecondaryToChannel(channel,
                                            control,
                                            Clamp(inputs[control])
                                                - shiftLatch_[control],
                                            Clamp(inputs[control]));
                else
                    channels_[channel].controls[control] = Clamp(inputs[control]);
            }
        }
    }

    void CycleMode(const float (&inputs)[ControlCount])
    {
        if (mode_ == Mode::Both)
            mode_ = Mode::Left;
        else if (mode_ == Mode::Left)
            mode_ = Mode::Right;
        else
        {
            mode_ = Mode::Both;
            SaveCombinedBase();
            for (std::size_t control = 0; control < ControlCount; ++control)
                lrLatch_[control] = Clamp(inputs[control]);
        }
    }

    Mode GetMode() const { return mode_; }
    bool IsChannelActive(std::size_t channel) const
    {
        return channel == 0 ? mode_ != Mode::Right
                            : channel == 1 && mode_ != Mode::Left;
    }

    const Channel& GetChannel(std::size_t index) const { return channels_[index]; }

  private:
    static float Clamp(float value)
    {
        return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
    }

    static bool IsSecondary(std::size_t control)
    {
        return control == Reflect || control == Atmosphere || control == Mix;
    }

    void SaveCombinedBase()
    {
        combinedBase_[0] = channels_[0];
        combinedBase_[1] = channels_[1];
    }

    void ApplySecondary(std::size_t control,
                        float delta,
                        float physicalValue)
    {
        for (std::size_t channel = 0; channel < 2; ++channel)
        {
            auto& target = channels_[channel];
            const auto& base = shiftBase_[channel];
            if (control == Reflect)
                target.crushMix = physicalValue;
            else if (control == Atmosphere)
                target.glitchDuration = Clamp(base.glitchDuration + delta);
            else
                target.pan = Clamp(base.pan + delta);
        }
    }

    void ApplySecondaryToChannel(std::size_t channel,
                                 std::size_t control,
                                 float delta,
                                 float physicalValue)
    {
        auto& target = channels_[channel];
        const auto& base = shiftBase_[channel];
        if (control == Reflect)
            target.crushMix = physicalValue;
        else if (control == Atmosphere)
            target.glitchDuration = Clamp(base.glitchDuration + delta);
        else
            target.pan = Clamp(base.pan + delta);
    }

    Channel channels_[2]{};
    Channel combinedBase_[2]{};
    Channel shiftBase_[2]{};
    float lrLatch_[ControlCount]{};
    float shiftLatch_[ControlCount]{};
    Mode mode_ = Mode::Both;
    bool initialized_ = false;
    bool shiftWasHeld_ = false;
};
}
}
