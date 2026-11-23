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

    enum class WindowUpdateMode
    {
        Smooth,
        Jump
    };

    enum class BlurSpeedMode
    {
        Continuous,
        Quantized
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
        float crushMix = 1.0f;
        float glitchDuration = 0.5f;
    };

    static float MapTimeRatio(float control)
    {
        control = Clamp(control);
        return control * control;
    }

    static float MapDegradeAmount(float control)
    {
        constexpr float offThreshold = 0.02f;
        control = Clamp(control);
        return control <= offThreshold
             ? 0.0f
             : (control - offThreshold) / (1.0f - offThreshold);
    }

    static float MapMix(float control)
    {
        control = Clamp(control);
        constexpr float endpointRange = 0.01f;
        if (control <= endpointRange)
            return 0.0f;
        if (control >= 1.0f - endpointRange)
            return 1.0f;
        return (control - endpointRange) / (1.0f - 2.0f * endpointRange);
    }

    static float MapBlurSpeed(float control)
    {
        control = Clamp(control);
        return (control - 0.5f) * 4.0f;
    }

    static float MapQuantizedBlurSpeed(float control)
    {
        static const float speeds[] = {
            -2.0f, -1.5f, -1.25f, -1.0f, -0.5f, -0.25f, 0.01f,
             0.25f, 0.5f, 1.0f, 1.25f, 1.5f, 2.0f};
        const float scaled = Clamp(control) * 12.0f;
        const auto index = static_cast<std::size_t>(std::floor(scaled + 0.5f));
        return speeds[index > 12 ? 12 : index];
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

    void Update(const float (&inputs)[ControlCount],
                bool shiftHeld,
                float blurKnobValue = -1.0f)
    {
        shiftActionTriggeredThisUpdate_ = false;
        const float physicalBlur = blurKnobValue < 0.0f
                                ? Clamp(inputs[Blur])
                                : Clamp(blurKnobValue);
        if (!initialized_)
        {
            Initialize(inputs);
            return;
        }

        if (shiftHeld && !shiftWasHeld_)
        {
            shiftBase_[0] = channels_[0];
            shiftBase_[1] = channels_[1];
            shiftWindowModeToggled_ = false;
            shiftBlurSpeedModeToggled_ = false;
            shiftBlurKnobAnchor_ = physicalBlur;
            for (std::size_t control = 0; control < ControlCount; ++control)
                shiftLatch_[control] = Clamp(inputs[control]);
        }
        shiftWasHeld_ = shiftHeld;

        if (shiftHeld && !shiftWindowModeToggled_
            && (std::fabs(Clamp(inputs[Warp]) - shiftLatch_[Warp]) > 0.02f
                || std::fabs(Clamp(inputs[Time]) - shiftLatch_[Time]) > 0.02f))
        {
            windowUpdateMode_ = windowUpdateMode_ == WindowUpdateMode::Smooth
                              ? WindowUpdateMode::Jump
                              : WindowUpdateMode::Smooth;
            shiftWindowModeToggled_ = true;
            shiftActionTriggeredThisUpdate_ = true;
        }

        if (shiftHeld && !shiftBlurSpeedModeToggled_
            && std::fabs(physicalBlur - shiftBlurKnobAnchor_) > 0.02f)
        {
            blurSpeedMode_ = blurSpeedMode_ == BlurSpeedMode::Continuous
                           ? BlurSpeedMode::Quantized
                           : BlurSpeedMode::Continuous;
            shiftBlurSpeedModeToggled_ = true;
            shiftActionTriggeredThisUpdate_ = true;
        }

        if (mode_ == Mode::Both)
        {
            for (std::size_t control = 0; control < ControlCount; ++control)
            {
                if (shiftHeld && IsShiftActionControl(control))
                    continue;
                else if (shiftHeld && IsSecondary(control))
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
                if (shiftHeld && IsShiftActionControl(control))
                    continue;
                else if (shiftHeld && IsSecondary(control))
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
    WindowUpdateMode GetWindowUpdateMode() const { return windowUpdateMode_; }
    BlurSpeedMode GetBlurSpeedMode() const { return blurSpeedMode_; }
    bool ShiftActionTriggeredThisUpdate() const
    {
        return shiftActionTriggeredThisUpdate_;
    }
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

    static bool IsShiftActionControl(std::size_t control)
    {
        return control == Warp || control == Time || control == Blur;
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
            {
                if (std::fabs(delta) > 0.02f)
                {
                    target.crushMix = physicalValue;
                    shiftActionTriggeredThisUpdate_ = true;
                }
            }
            else if (control == Atmosphere)
            {
                if (std::fabs(delta) > 0.02f)
                {
                    target.glitchDuration = physicalValue;
                    shiftActionTriggeredThisUpdate_ = true;
                }
            }
            else
            {
                target.pan = Clamp(base.pan + delta);
                if (std::fabs(delta) > 0.02f)
                    shiftActionTriggeredThisUpdate_ = true;
            }
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
        {
            if (std::fabs(delta) > 0.02f)
            {
                target.crushMix = physicalValue;
                shiftActionTriggeredThisUpdate_ = true;
            }
        }
        else if (control == Atmosphere)
        {
            if (std::fabs(delta) > 0.02f)
            {
                target.glitchDuration = physicalValue;
                shiftActionTriggeredThisUpdate_ = true;
            }
        }
        else
        {
            target.pan = Clamp(base.pan + delta);
            if (std::fabs(delta) > 0.02f)
                shiftActionTriggeredThisUpdate_ = true;
        }
    }

    Channel channels_[2]{};
    Channel combinedBase_[2]{};
    Channel shiftBase_[2]{};
    float lrLatch_[ControlCount]{};
    float shiftLatch_[ControlCount]{};
    Mode mode_ = Mode::Both;
    bool initialized_ = false;
    bool shiftWasHeld_ = false;
    bool shiftActionTriggeredThisUpdate_ = false;
    bool shiftWindowModeToggled_ = false;
    WindowUpdateMode windowUpdateMode_ = WindowUpdateMode::Smooth;
    bool shiftBlurSpeedModeToggled_ = false;
    BlurSpeedMode blurSpeedMode_ = BlurSpeedMode::Continuous;
    float shiftBlurKnobAnchor_ = 0.0f;
};
}
}
