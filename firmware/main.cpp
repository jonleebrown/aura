#include "aurora.h"
#include "aurora/dsp/KarmaControlState.h"
#include "aurora/dsp/StereoLooper.h"
#include "LoopWavStore.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

using namespace daisy;
using namespace aurora;

namespace
{
enum StorageStatus : uint8_t
{
    StorageNoFile,
    StorageReading,
    StorageWriting,
    StorageSaved,
    StorageUnsaved,
    StorageError
};

Hardware hardware;
#if !defined(AURORA_DIAGNOSTIC) || AURORA_DIAGNOSTIC == 10 || AURORA_DIAGNOSTIC == 11
// USB loop persistence is parked for later experimentation; keep all USB
// storage access disabled in the normal firmware.
constexpr bool usbPersistenceEnabled = false;
constexpr std::size_t sampleRate = 48000;
constexpr std::size_t maxLoopSamples = 40U * sampleRate;
constexpr std::size_t maxRepeatSamples = sampleRate * 2;
std::size_t workingLoopSamples = 0;
float loopBufferLeft[maxLoopSamples] DSY_SDRAM_BSS;
float loopBufferRight[maxLoopSamples] DSY_SDRAM_BSS;
float repeatBufferLeft[maxRepeatSamples] DSY_SDRAM_BSS;
float repeatBufferRight[maxRepeatSamples] DSY_SDRAM_BSS;
aurora::dsp::StereoLooper looper;
aurora::dsp::KarmaControlState controlState;
volatile bool recording_status = false;
volatile bool erasing_status = false;
volatile bool clearing_status = false;
volatile uint8_t mode_indicator = 0x03;
volatile bool clear_loop_led_pulses = false;
bool control_state_initialized = false;
float filtered_control_inputs[aurora::dsp::KarmaControlState::ControlCount]{};
bool control_input_filter_initialized = false;
uint32_t shift_held_samples = 0;
bool shift_consumed = false;
float shift_start_controls[aurora::dsp::KarmaControlState::ControlCount]{};
bool shift_record_was_active = false;
uint32_t shift_record_samples = 0;
bool empty_buffer_record_was_active = false;
uint32_t empty_buffer_record_samples = 0;
bool captured_window_pending = false;
LoopWavStore loopWavStore;
volatile bool storage_io_busy = false;
volatile bool storage_audio_quiesced = false;
volatile uint8_t storage_status = StorageNoFile;
volatile uint8_t loop_end_events = 0;
bool storage_load_attempted = false;
bool storage_ram_only_changes = false;
bool storage_blocked = false;
uint32_t storage_status_until = 0;
uint32_t storage_activity_led_until = 0;
bool storage_activity_led_on = false;
uint32_t loop_end_pulse_until[2]{};
char loopFilePath[16]{};
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 10
volatile bool playback_discontinuity_seen = false;
float playback_previous[2]{};
float playback_previous_previous[2]{};
uint8_t playback_history_samples = 0;
#endif
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 11
float record_reference_left[maxLoopSamples] DSY_SDRAM_BSS;
float record_reference_right[maxLoopSamples] DSY_SDRAM_BSS;
volatile bool record_audit_active = false;
volatile bool record_audit_complete = false;
volatile bool record_audit_failed = false;
std::size_t record_reference_samples = 0;
std::size_t record_audit_samples = 0;
std::size_t record_audit_index = 0;
bool record_reference_left_active = false;
bool record_reference_right_active = false;
#endif
#endif
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC > 0
volatile bool audio_callback_seen = false;
#endif
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 1
volatile uint8_t input_activity = 0;
#endif
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 5
volatile float mix_control_level = 0.0f;
volatile float depth_control_level = 0.0f;
#endif
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 7
volatile bool sdram_test_passed = false;
volatile uint8_t callback_block_size = 0;
#endif
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 8
volatile float mix_knob_value = 0.0f;
volatile float mix_cv_value = 0.0f;
#endif
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 2
float diagnostic_phase = 0.0f;
#endif

#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 7
constexpr std::size_t sdramTestWordCount = 2U * 40U * 48000U;
constexpr std::uintptr_t sdramBaseAddress = 0xC0000000U;
volatile uint32_t sdramFailureIndex = 0;

void MaintainSdramTestCache()
{
    SCB_CleanInvalidateDCache_by_Addr(
        reinterpret_cast<uint32_t*>(sdramBaseAddress),
        static_cast<int32_t>(sdramTestWordCount * sizeof(uint32_t)));
    __DSB();
    __ISB();
}

bool TestAndClearSdram()
{
    auto* const memory = reinterpret_cast<volatile uint32_t*>(sdramBaseAddress);
    for (std::size_t index = 0; index < sdramTestWordCount; ++index)
    {
        const auto pattern = 0xA5A50000U
                           ^ (static_cast<uint32_t>(index) * 0x9E3779B9U);
        memory[index] = pattern;
    }
    MaintainSdramTestCache();

    for (std::size_t index = 0; index < sdramTestWordCount; ++index)
    {
        const auto expected = 0xA5A50000U
                            ^ (static_cast<uint32_t>(index) * 0x9E3779B9U);
        if (memory[index] != expected)
        {
            sdramFailureIndex = static_cast<uint32_t>(index);
            return false;
        }
    }

    for (std::size_t index = 0; index < sdramTestWordCount; ++index)
    {
        const auto pattern = 0xA5A50000U
                           ^ (static_cast<uint32_t>(index) * 0x9E3779B9U);
        memory[index] = ~pattern;
    }
    MaintainSdramTestCache();

    for (std::size_t index = 0; index < sdramTestWordCount; ++index)
    {
        const auto expected = ~(0xA5A50000U
                              ^ (static_cast<uint32_t>(index) * 0x9E3779B9U));
        if (memory[index] != expected)
        {
            sdramFailureIndex = static_cast<uint32_t>(index);
            return false;
        }
    }

    for (std::size_t index = 0; index < sdramTestWordCount; ++index)
        memory[index] = 0U;
    MaintainSdramTestCache();

    for (std::size_t index = 0; index < sdramTestWordCount; ++index)
    {
        if (memory[index] != 0U)
        {
            sdramFailureIndex = static_cast<uint32_t>(index);
            return false;
        }
    }
    return true;
}
#endif

#if !defined(AURORA_DIAGNOSTIC) || AURORA_DIAGNOSTIC == 10 || AURORA_DIAGNOSTIC == 11
using ControlState = aurora::dsp::KarmaControlState;
using LooperParameters = aurora::dsp::StereoLooper::ChannelParameters;

float ClampControl(float value)
{
    return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

float ReadControl(int knob, int cv)
{
    return ClampControl(hardware.GetKnobValue(knob) + hardware.GetCvValue(cv));
}

LooperParameters MakeLooperParameters(std::size_t channel,
                                      bool active,
                                      bool record,
                                      bool erase,
                                      bool clearAll,
                                      bool fullLength,
                                      bool finishCapturedWindow)
{
    const auto& controls = controlState.GetChannel(channel);
    LooperParameters parameters;
    parameters.start = fullLength || finishCapturedWindow
                     ? 0.0f
                     : controls.controls[ControlState::Warp]
                           * static_cast<float>(workingLoopSamples)
                           / static_cast<float>(maxLoopSamples);
    parameters.length = fullLength
                      ? 1.0f
                      : finishCapturedWindow
                      ? static_cast<float>(workingLoopSamples)
                            / static_cast<float>(maxLoopSamples)
                      : ControlState::MapTimeRatio(controls.controls[ControlState::Time])
                            * static_cast<float>(workingLoopSamples)
                            / static_cast<float>(maxLoopSamples);
    parameters.regionLength = fullLength
                            ? 1.0f
                            : static_cast<float>(workingLoopSamples)
                                  / static_cast<float>(maxLoopSamples);
    parameters.speed = fullLength
                     ? 1.0f
                     : controlState.GetBlurSpeedMode()
                               == ControlState::BlurSpeedMode::Quantized
                         ? ControlState::MapQuantizedBlurSpeed(
                               controls.controls[ControlState::Blur])
                         : ControlState::MapBlurSpeed(
                               controls.controls[ControlState::Blur]);
    parameters.mix = ControlState::MapMix(controls.controls[ControlState::Mix]);
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 11
    parameters.start = 0.0f;
    parameters.length = fullLength
                      ? 1.0f
                      : static_cast<float>(workingLoopSamples)
                            / static_cast<float>(maxLoopSamples);
    parameters.speed = 1.0f;
#endif
    parameters.pan = controls.pan;
    parameters.reflect = ControlState::MapDegradeAmount(
        controls.controls[ControlState::Reflect]);
    parameters.reflectMix = controls.crushMix;
    // Reflect controls the paired read-head slip and bit-crush amount.
    parameters.slippage = parameters.reflect;
    parameters.atmosphere = controls.controls[ControlState::Atmosphere];
    for (std::size_t i = 0;
         i < aurora::dsp::KarmaEffects::DropoutProbabilityCount;
         ++i)
        parameters.dropoutProbabilities[i] = parameters.atmosphere * 0.25f;
    parameters.windowUpdateMode =
        controlState.GetWindowUpdateMode() == ControlState::WindowUpdateMode::Jump
            ? aurora::dsp::StereoLooper::WindowUpdateMode::Jump
            : aurora::dsp::StereoLooper::WindowUpdateMode::Smooth;
    parameters.glitchDuration = controls.glitchDuration;
#if defined(AURORA_DIAGNOSTIC) && (AURORA_DIAGNOSTIC == 10 || AURORA_DIAGNOSTIC == 11)
    parameters.mix = 1.0f;
    parameters.reflect = 0.0f;
    parameters.reflectMix = 0.0f;
    parameters.slippage = 0.0f;
    parameters.atmosphere = 0.0f;
#endif
    parameters.record = active && record && !clearAll;
    parameters.recordFromStart = active && fullLength;
    parameters.finishRecordAtFullWindow = finishCapturedWindow;
    parameters.erase = active && erase;
    parameters.clearAll = clearAll;
    return parameters;
}

bool LooperBusy()
{
    const uint32_t interruptState = __get_PRIMASK();
    __disable_irq();
    const bool busy = looper.IsRecording() || looper.IsErasing()
                   || looper.IsClearing();
    __set_PRIMASK(interruptState);
    return busy;
}

bool HasDirtyRanges()
{
    const uint32_t interruptState = __get_PRIMASK();
    __disable_irq();
    const bool dirty = looper.HasDirtyRange(0) || looper.HasDirtyRange(1);
    __set_PRIMASK(interruptState);
    return dirty;
}

const char* StorageResultName(LoopWavStore::Result result)
{
    switch (result)
    {
        case LoopWavStore::Result::Loaded: return "loaded";
        case LoopWavStore::Result::NoFile: return "no_file";
        case LoopWavStore::Result::Ok: return "ok";
        case LoopWavStore::Result::InvalidFile: return "invalid_file";
        case LoopWavStore::Result::IoError: return "io_error";
    }
    return "unknown";
}

const char* FatFsResultName(FRESULT result)
{
    switch (result)
    {
        case FR_OK: return "FR_OK";
        case FR_DISK_ERR: return "FR_DISK_ERR";
        case FR_INT_ERR: return "FR_INT_ERR";
        case FR_NOT_READY: return "FR_NOT_READY";
        case FR_NO_FILE: return "FR_NO_FILE";
        case FR_NO_PATH: return "FR_NO_PATH";
        case FR_INVALID_NAME: return "FR_INVALID_NAME";
        case FR_DENIED: return "FR_DENIED";
        case FR_EXIST: return "FR_EXIST";
        case FR_INVALID_OBJECT: return "FR_INVALID_OBJECT";
        case FR_WRITE_PROTECTED: return "FR_WRITE_PROTECTED";
        case FR_INVALID_DRIVE: return "FR_INVALID_DRIVE";
        case FR_NOT_ENABLED: return "FR_NOT_ENABLED";
        case FR_NO_FILESYSTEM: return "FR_NO_FILESYSTEM";
        case FR_MKFS_ABORTED: return "FR_MKFS_ABORTED";
        case FR_TIMEOUT: return "FR_TIMEOUT";
        case FR_LOCKED: return "FR_LOCKED";
        case FR_NOT_ENOUGH_CORE: return "FR_NOT_ENOUGH_CORE";
        case FR_TOO_MANY_OPEN_FILES: return "FR_TOO_MANY_OPEN_FILES";
        case FR_INVALID_PARAMETER: return "FR_INVALID_PARAMETER";
    }
    return "FR_UNKNOWN";
}

void AppendStorageLog(const char* action,
                      LoopWavStore::Result result,
                      const char* operation,
                      FRESULT fatFsResult)
{
    char logPath[32]{};
    std::snprintf(logPath,
                  sizeof(logPath),
                  "%s/KARMA.LOG",
                  fatfs_interface.GetUSBPath());
    FIL file;
    if (f_open(&file, logPath, FA_WRITE | FA_OPEN_APPEND) != FR_OK)
        return;

    char line[128]{};
    const int lineLength = std::snprintf(
        line,
        sizeof(line),
        "%lu %s %s op=%s fatfs=%s(%u)\r\n",
        static_cast<unsigned long>(HAL_GetTick()),
        action,
        StorageResultName(result),
        operation,
        FatFsResultName(fatFsResult),
        static_cast<unsigned>(fatFsResult));
    if (lineLength > 0
        && static_cast<std::size_t>(lineLength) < sizeof(line))
    {
        UINT bytesWritten = 0;
        if (f_write(&file,
                    line,
                    static_cast<UINT>(lineLength),
                    &bytesWritten) == FR_OK
            && bytesWritten == static_cast<UINT>(lineLength))
            f_sync(&file);
    }
    f_close(&file);
}

bool QuiesceAudioForStorage()
{
    storage_io_busy = true;
    const uint32_t start = HAL_GetTick();
    while (!storage_audio_quiesced)
    {
        usb.Process();
        if (HAL_GetTick() - start > 1000)
        {
            storage_io_busy = false;
            storage_status = StorageError;
            storage_blocked = true;
            return false;
        }
    }
    return true;
}

void UpdateStorageActivityLed()
{
    const uint32_t now = HAL_GetTick();
    if (now < storage_activity_led_until)
        return;
    storage_activity_led_on = !storage_activity_led_on;
    storage_activity_led_until = now + 150;
    hardware.SetLed(LED_FREEZE,
                    0.0f,
                    storage_activity_led_on ? 1.0f : 0.0f,
                    0.0f);
    hardware.WriteLeds();
}

void ResumeAudioAfterStorage()
{
    storage_audio_quiesced = false;
    storage_io_busy = false;
}

void TakeDirtyRanges(aurora::dsp::StereoLooper::DirtyRange& left,
                     aurora::dsp::StereoLooper::DirtyRange& right)
{
    const uint32_t interruptState = __get_PRIMASK();
    __disable_irq();
    left = looper.TakeDirtyRange(0);
    right = looper.TakeDirtyRange(1);
    __set_PRIMASK(interruptState);
}

void LoadLoopFile()
{
    if (!QuiesceAudioForStorage())
        return;

    storage_status = StorageReading;
    storage_activity_led_on = false;
    storage_activity_led_until = 0;
    UpdateStorageActivityLed();
    std::size_t loadedFrames = 0;
    const auto result = loopWavStore.Load(loopFilePath,
                                           loopBufferLeft,
                                           loopBufferRight,
                                           maxLoopSamples,
                                           loadedFrames,
                                           UpdateStorageActivityLed);
    AppendStorageLog("LOAD",
                     result,
                     loopWavStore.LastErrorOperation(),
                     loopWavStore.LastFatFsResult());
    if (result == LoopWavStore::Result::Loaded)
    {
        if (!looper.SetLoopRegion(0, loadedFrames))
        {
            storage_status = StorageError;
            storage_blocked = true;
        }
        else
        {
            workingLoopSamples = loadedFrames;
            storage_status = StorageSaved;
            storage_status_until = HAL_GetTick() + 1000;
        }
    }
    else if (result == LoopWavStore::Result::NoFile)
        storage_status = StorageNoFile;
    else
    {
        looper.Clear();
        looper.SetLoopRegion(0, 0);
        workingLoopSamples = 0;
        storage_status = StorageError;
        storage_blocked = true;
    }
    ResumeAudioAfterStorage();
}

void ServiceLoopStorage()
{
    const bool ready = usb.GetReady();
    const bool dirty = HasDirtyRanges();
    const bool busy = LooperBusy();

    if (!storage_load_attempted && ready)
    {
        if (busy)
            return;
        storage_load_attempted = true;
        if (storage_ram_only_changes || dirty)
        {
            storage_ram_only_changes = true;
            storage_blocked = true;
            storage_status = StorageUnsaved;
            return;
        }
        const char* volumePath = fatfs_interface.GetUSBPath();
        std::snprintf(loopFilePath,
                      sizeof(loopFilePath),
                      "%s/LOOP.WAV",
                      volumePath);
        LoadLoopFile();
    }

    if (busy || !dirty)
        return;

    if (!ready)
    {
        storage_ram_only_changes = true;
        storage_blocked = true;
        storage_status = StorageUnsaved;
        return;
    }
    if (storage_blocked || !storage_load_attempted)
    {
        storage_status = storage_ram_only_changes
                       ? StorageUnsaved
                       : StorageError;
        return;
    }

    if (!QuiesceAudioForStorage())
        return;
    if (LooperBusy())
    {
        ResumeAudioAfterStorage();
        return;
    }
    aurora::dsp::StereoLooper::DirtyRange leftDirty;
    aurora::dsp::StereoLooper::DirtyRange rightDirty;
    TakeDirtyRanges(leftDirty, rightDirty);
    if (!leftDirty.dirty && !rightDirty.dirty)
    {
        ResumeAudioAfterStorage();
        return;
    }

    storage_status = StorageWriting;
    storage_activity_led_on = false;
    storage_activity_led_until = 0;
    UpdateStorageActivityLed();
    const auto result = loopWavStore.Save(loopFilePath,
                                          loopBufferLeft,
                                          loopBufferRight,
                                          maxLoopSamples,
                                          workingLoopSamples,
                                          leftDirty,
                                          rightDirty,
                                          UpdateStorageActivityLed);
    ResumeAudioAfterStorage();
    AppendStorageLog("SAVE",
                     result,
                     loopWavStore.LastErrorOperation(),
                     loopWavStore.LastFatFsResult());
    if (result == LoopWavStore::Result::Ok)
    {
        storage_status = StorageSaved;
        storage_status_until = HAL_GetTick() + 1000;
    }
    else
    {
        storage_blocked = true;
        storage_status = StorageError;
    }
}

void UpdateModeIndicator()
{
    const auto mode = controlState.GetMode();
    mode_indicator = mode == ControlState::Mode::Left ? 0x01
                   : mode == ControlState::Mode::Right ? 0x02
                                                       : 0x03;
}

uint8_t TakeLoopEndEvents()
{
    const uint32_t interruptState = __get_PRIMASK();
    __disable_irq();
    const uint8_t events = loop_end_events;
    loop_end_events = 0;
    __set_PRIMASK(interruptState);
    return events;
}

void UpdateChannelLeds()
{
    const uint32_t now = HAL_GetTick();
    if (clear_loop_led_pulses)
    {
        const uint32_t interruptState = __get_PRIMASK();
        __disable_irq();
        loop_end_pulse_until[0] = 0;
        loop_end_pulse_until[1] = 0;
        loop_end_events = 0;
        clear_loop_led_pulses = false;
        __set_PRIMASK(interruptState);
    }
    const uint8_t events = TakeLoopEndEvents();
    for (std::size_t channel = 0; channel < 2; ++channel)
        if ((events & (1U << channel)) != 0)
            loop_end_pulse_until[channel] = now + 120;

    const bool leftSelected = (mode_indicator & 0x01U) != 0;
    const bool rightSelected = (mode_indicator & 0x02U) != 0;
    const bool leftLoopRestart = now < loop_end_pulse_until[0];
    const bool rightLoopRestart = now < loop_end_pulse_until[1];
    hardware.SetLed(LED_1, 0.0f, leftSelected ? 1.0f : 0.0f, 0.0f);
    hardware.SetLed(LED_2, 0.0f, leftLoopRestart ? 1.0f : 0.0f, 0.0f);
    hardware.SetLed(LED_3, 0.0f, 0.0f, rightLoopRestart ? 1.0f : 0.0f);
    hardware.SetLed(LED_4, 0.0f, 0.0f, rightSelected ? 1.0f : 0.0f);
}
#endif

void AudioCallback(AudioHandle::InputBuffer input,
                   AudioHandle::OutputBuffer output,
                   size_t frameCount)
{
#if !defined(AURORA_DIAGNOSTIC) || AURORA_DIAGNOSTIC == 10 || AURORA_DIAGNOSTIC == 11
    if (storage_io_busy)
    {
        storage_audio_quiesced = true;
        for (size_t frame = 0; frame < frameCount; ++frame)
        {
            output[0][frame] = input[0][frame];
            output[1][frame] = input[1][frame];
        }
        return;
    }
    storage_audio_quiesced = false;
    hardware.ProcessAllControls();
    const float rawControlInputs[ControlState::ControlCount] = {
        ReadControl(KNOB_WARP, CV_WARP),
        ReadControl(KNOB_TIME, CV_TIME),
        ReadControl(KNOB_BLUR, CV_BLUR),
        ReadControl(KNOB_REFLECT, CV_REFLECT),
        ReadControl(KNOB_ATMOSPHERE, CV_ATMOSPHERE),
        ReadControl(KNOB_MIX, CV_MIX)};
    if (!control_input_filter_initialized)
    {
        for (std::size_t control = 0; control < ControlState::ControlCount; ++control)
            filtered_control_inputs[control] = rawControlInputs[control];
        control_input_filter_initialized = true;
    }
    else
    {
        constexpr float controlSmoothingSeconds = 0.02f;
        const float elapsedSeconds = static_cast<float>(frameCount)
                                   / static_cast<float>(sampleRate);
        const float smoothing = 1.0f
                              - std::exp(-elapsedSeconds
                                         / controlSmoothingSeconds);
        for (std::size_t control = 0; control < ControlState::ControlCount; ++control)
            filtered_control_inputs[control] += smoothing
                * (rawControlInputs[control] - filtered_control_inputs[control]);
    }
    float controlInputs[ControlState::ControlCount];
    for (std::size_t control = 0; control < ControlState::ControlCount; ++control)
        controlInputs[control] = filtered_control_inputs[control];
    if (!control_state_initialized)
    {
        controlState.Initialize(controlInputs);
        control_state_initialized = true;
    }

    const bool shiftHeld = hardware.GetButton(SW_SHIFT).Pressed();
    if (hardware.GetButton(SW_SHIFT).RisingEdge())
    {
        shift_held_samples = 0;
        shift_consumed = false;
        for (std::size_t control = 0; control < ControlState::ControlCount; ++control)
            shift_start_controls[control] = controlInputs[control];
    }

    const bool recordHeld = hardware.GetButton(SW_REVERSE).Pressed()
                         || hardware.GetGateState(GATE_REVERSE);
    const bool eraseHeld = hardware.GetButton(SW_FREEZE).Pressed()
                        || hardware.GetGateState(GATE_FREEZE);
    const bool fullErase = shiftHeld && hardware.GetButton(SW_FREEZE).Pressed();
    bool shiftedControlChanged = false;
    if (shiftHeld)
    {
        shift_held_samples += static_cast<uint32_t>(frameCount);
        for (std::size_t control = ControlState::Warp;
             control <= ControlState::Mix;
             ++control)
        {
            if (std::fabs(controlInputs[control] - shift_start_controls[control]) > 0.02f)
                shiftedControlChanged = true;
        }
    }

    const bool shiftRecordActive = shiftHeld && recordHeld;
    const bool emptyBufferRecordActive = workingLoopSamples == 0 && recordHeld;
    const bool wasEmptyBufferRecord = empty_buffer_record_was_active;
    if (emptyBufferRecordActive != empty_buffer_record_was_active)
    {
        if (emptyBufferRecordActive)
            empty_buffer_record_samples = 0;
        else
        {
            workingLoopSamples = empty_buffer_record_samples > 0
                               ? empty_buffer_record_samples
                               : 1;
            captured_window_pending = true;
        }
        empty_buffer_record_was_active = emptyBufferRecordActive;
    }
    if (emptyBufferRecordActive)
    {
        const uint32_t remaining =
            static_cast<uint32_t>(maxLoopSamples) - empty_buffer_record_samples;
        empty_buffer_record_samples += static_cast<uint32_t>(frameCount) < remaining
                                     ? static_cast<uint32_t>(frameCount)
                                     : remaining;
    }
    if (shiftRecordActive != shift_record_was_active)
    {
        if (shiftRecordActive)
            shift_record_samples = 0;
        else if (shift_record_was_active && !wasEmptyBufferRecord)
        {
            workingLoopSamples = shift_record_samples > 0
                               ? shift_record_samples
                               : 1;
            captured_window_pending = true;
        }
        shift_record_was_active = shiftRecordActive;
    }
    if (shiftRecordActive)
    {
        shift_consumed = true;
        const uint32_t remaining = static_cast<uint32_t>(maxLoopSamples) - shift_record_samples;
        shift_record_samples += static_cast<uint32_t>(frameCount) < remaining
                             ? static_cast<uint32_t>(frameCount)
                             : remaining;
    }
    controlState.Update(controlInputs,
                        shiftHeld,
                        hardware.GetKnobValue(KNOB_BLUR));
    if (shiftedControlChanged || fullErase)
        shift_consumed = true;

    if (!shiftHeld && hardware.GetButton(SW_SHIFT).FallingEdge())
    {
        if (shift_held_samples < 14400 && !shift_consumed)
            controlState.CycleMode(controlInputs);
        shift_held_samples = 0;
        shift_consumed = false;
    }
    UpdateModeIndicator();

    const bool leftActive = controlState.IsChannelActive(0);
    const bool rightActive = controlState.IsChannelActive(1);
    const bool normalErase = eraseHeld && !fullErase;
    const bool finishCapturedWindow = captured_window_pending && !recordHeld;
    const auto leftParameters = MakeLooperParameters(0,
                                                      leftActive,
                                                      recordHeld,
                                                      normalErase,
                                                      fullErase,
                                                      shiftRecordActive
                                                          || emptyBufferRecordActive,
                                                      finishCapturedWindow);
    const auto rightParameters = MakeLooperParameters(1,
                                                       rightActive,
                                                       recordHeld,
                                                       normalErase,
                                                       fullErase,
                                                       shiftRecordActive
                                                           || emptyBufferRecordActive,
                                                       finishCapturedWindow);
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 11
    if (emptyBufferRecordActive && !wasEmptyBufferRecord)
    {
        record_reference_samples = 0;
        record_audit_samples = 0;
        record_audit_index = 0;
        record_audit_active = false;
        record_audit_complete = false;
        record_audit_failed = false;
        record_reference_left_active = leftActive;
        record_reference_right_active = rightActive;
    }
    if (emptyBufferRecordActive)
    {
        for (size_t frame = 0;
             frame < frameCount && record_reference_samples < maxLoopSamples;
             ++frame, ++record_reference_samples)
        {
            record_reference_left[record_reference_samples] = input[0][frame];
            record_reference_right[record_reference_samples] = input[1][frame];
        }
    }
    if (wasEmptyBufferRecord && !emptyBufferRecordActive)
    {
        record_audit_samples = workingLoopSamples;
        record_audit_index = 0;
        record_audit_active = record_audit_samples > 0;
        record_audit_complete = false;
        record_audit_failed = record_reference_samples != record_audit_samples;
    }
#endif
    looper.ProcessBlock(input[0],
                        input[1],
                        output[0],
                        output[1],
                        frameCount,
                        leftParameters,
                        rightParameters);
    if (looper.TakeClearCompleted())
    {
        workingLoopSamples = 0;
        looper.StopPlayback();
        loop_end_events = 0;
        clear_loop_led_pulses = true;
    }
    if (finishCapturedWindow)
        captured_window_pending = false;
    loop_end_events |= looper.TakeLoopEndEvents();
    recording_status = looper.IsRecording();
    erasing_status = looper.IsErasing();
    clearing_status = looper.IsClearing();
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 11
    if (record_audit_active)
    {
        const std::size_t remaining = record_audit_samples - record_audit_index;
        const std::size_t framesToCheck = frameCount < remaining
                                        ? frameCount
                                        : remaining;
        for (std::size_t offset = 0; offset < framesToCheck; ++offset)
        {
            const std::size_t index = record_audit_index + offset;
            if ((record_reference_left_active
                 && loopBufferLeft[index] != record_reference_left[index])
                || (record_reference_right_active
                    && loopBufferRight[index] != record_reference_right[index]))
                record_audit_failed = true;
        }
        record_audit_index += framesToCheck;
        if (record_audit_index == record_audit_samples)
        {
            record_audit_active = false;
            record_audit_complete = true;
        }
    }
#endif
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 10
    if (!looper.IsRecording() && !looper.IsErasing() && !looper.IsClearing())
    {
        bool discontinuityDetected = false;
        for (size_t frame = 0; frame < frameCount; ++frame)
        {
            const float current[2] = {output[0][frame], output[1][frame]};
            for (size_t channel = 0; channel < 2; ++channel)
            {
                if (playback_history_samples >= 2
                    && std::fabs(current[channel]
                                  - 2.0f * playback_previous[channel]
                                  + playback_previous_previous[channel]) > 0.25f)
                    discontinuityDetected = true;
                playback_previous_previous[channel] = playback_previous[channel];
                playback_previous[channel] = current[channel];
            }
            if (playback_history_samples < 2)
                ++playback_history_samples;
        }
        if (discontinuityDetected)
            playback_discontinuity_seen = true;
    }
    else
        playback_history_samples = 0;
#endif
#elif defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 1
    audio_callback_seen = true;
    constexpr float input_activity_threshold = 0.01f;
    uint8_t activity = 0;
    for(size_t frame = 0; frame < frameCount; ++frame)
    {
        output[0][frame] = input[0][frame];
        output[1][frame] = input[1][frame];
        if(input[0][frame] > input_activity_threshold
           || input[0][frame] < -input_activity_threshold)
            activity |= 1;
        if(input[1][frame] > input_activity_threshold
           || input[1][frame] < -input_activity_threshold)
            activity |= 2;
    }
    input_activity = activity;
#elif defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 2
    audio_callback_seen = true;
    constexpr float phase_increment = 2.0f * 3.14159265358979323846f * 440.0f / 48000.0f;
    for(size_t frame = 0; frame < frameCount; ++frame)
    {
        const float sample = 0.5f * std::sin(diagnostic_phase);
        diagnostic_phase += phase_increment;
        if(diagnostic_phase >= 2.0f * 3.14159265358979323846f)
            diagnostic_phase -= 2.0f * 3.14159265358979323846f;
        output[0][frame] = sample;
        output[1][frame] = sample;
    }
#elif defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 3
    audio_callback_seen = true;
    for(size_t frame = 0; frame < frameCount; ++frame)
    {
        const float sample = input[1][frame] * 4.0f;
        output[0][frame] = sample;
        output[1][frame] = sample;
    }
#elif defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 6
    audio_callback_seen = true;
    for(size_t frame = 0; frame < frameCount; ++frame)
    {
        output[0][frame] = input[0][frame] * 8.0f;
        output[1][frame] = input[1][frame] * 8.0f;
    }
#elif defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 5
    audio_callback_seen = true;
    hardware.ProcessAllControls();
    mix_control_level = hardware.GetKnobValue(KNOB_MIX);
    depth_control_level = hardware.GetKnobValue(KNOB_BLUR);
    for(size_t frame = 0; frame < frameCount; ++frame)
    {
        output[0][frame] = input[0][frame];
        output[1][frame] = input[1][frame];
    }
#elif defined(AURORA_DIAGNOSTIC) && (AURORA_DIAGNOSTIC == 7 || AURORA_DIAGNOSTIC == 8 || AURORA_DIAGNOSTIC == 9)
    audio_callback_seen = true;
#if AURORA_DIAGNOSTIC == 7
    callback_block_size = static_cast<uint8_t>(frameCount);
#elif AURORA_DIAGNOSTIC == 8
    hardware.ProcessAllControls();
    mix_knob_value = hardware.GetKnobValue(KNOB_MIX);
    mix_cv_value = hardware.GetCvValue(CV_MIX);
#endif
    for(size_t frame = 0; frame < frameCount; ++frame)
    {
        output[0][frame] = input[0][frame];
        output[1][frame] = input[1][frame];
    }
#else
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC > 0
    audio_callback_seen = true;
#endif
    for(size_t frame = 0; frame < frameCount; ++frame)
    {
        output[0][frame] = input[0][frame];
        output[1][frame] = input[1][frame];
    }
#endif
}
}

int main(void)
{
    hardware.Init();
#if !defined(AURORA_DIAGNOSTIC) || AURORA_DIAGNOSTIC == 10 || AURORA_DIAGNOSTIC == 11
    if (hardware.seed.sdram_handle.Init() != SdramHandle::Result::OK
        || !looper.Initialize(loopBufferLeft,
                              loopBufferRight,
                              maxLoopSamples,
                              0,
                              repeatBufferLeft,
                              repeatBufferRight,
                              maxRepeatSamples))
    {
        hardware.SetLed(LED_1, 1.0f, 0.0f, 0.0f);
        hardware.WriteLeds();
        while (1)
            System::Delay(250);
    }
    hardware.SetLed(LED_1, 0.0f, 1.0f, 0.0f);
    hardware.WriteLeds();
    if (usbPersistenceEnabled)
        hardware.PrepareMedia();
#endif
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 7
    hardware.SetLed(LED_1, 1.0f, 0.4f, 0.0f);
    hardware.WriteLeds();
    sdram_test_passed = hardware.seed.sdram_handle.Init() == SdramHandle::Result::OK
                     && TestAndClearSdram();
#elif defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 9
    hardware.PrepareMedia();
#endif
    hardware.StartAudio(AudioCallback);
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 9
    uint32_t lastLedUpdate = HAL_GetTick();
#endif

    while (1)
    {
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC > 0 && AURORA_DIAGNOSTIC != 10 && AURORA_DIAGNOSTIC != 11
#if AURORA_DIAGNOSTIC == 9
        usb.Process();
        const uint32_t now = HAL_GetTick();
        if(now - lastLedUpdate < 250)
            continue;
        lastLedUpdate = now;
#endif
        if(audio_callback_seen)
            hardware.SetLed(LED_FREEZE, 0.0f, 1.0f, 0.0f);
        else
            hardware.SetLed(LED_FREEZE, 1.0f, 0.0f, 0.0f);
#if AURORA_DIAGNOSTIC == 1
        hardware.SetLed(LED_1, 0.0f, (input_activity & 1) ? 1.0f : 0.0f, 0.0f);
        hardware.SetLed(LED_2, 0.0f, (input_activity & 2) ? 1.0f : 0.0f, 0.0f);
#elif AURORA_DIAGNOSTIC == 5
        hardware.SetLed(LED_1,
                        mix_control_level,
                        0.0f,
                        0.0f);
        hardware.SetLed(LED_2,
                        0.0f,
                        depth_control_level,
                        0.0f);
#elif AURORA_DIAGNOSTIC == 7
        hardware.SetLed(LED_1,
                        sdram_test_passed ? 0.0f : 1.0f,
                        sdram_test_passed ? 1.0f : 0.0f,
                        0.0f);
        if(callback_block_size == 48)
            hardware.SetLed(LED_2, 0.0f, 0.0f, 1.0f);
        else if(callback_block_size == 96)
            hardware.SetLed(LED_2, 0.0f, 1.0f, 0.0f);
        else if(callback_block_size > 0)
            hardware.SetLed(LED_2, 1.0f, 0.0f, 0.0f);
#elif AURORA_DIAGNOSTIC == 8
        const float knob = mix_knob_value < 0.0f ? 0.0f
                         : mix_knob_value > 1.0f ? 1.0f
                                                 : mix_knob_value;
        const float cv = mix_cv_value < -1.0f ? -1.0f
                       : mix_cv_value > 1.0f ? 1.0f
                                             : mix_cv_value;
        hardware.SetLed(LED_1, knob, 0.0f, 0.0f);
        hardware.SetLed(LED_2, cv < 0.0f ? -cv : 0.0f,
                        cv > 0.0f ? cv : 0.0f,
                        0.0f);
#elif AURORA_DIAGNOSTIC == 9
        const bool usbPresent = usb.GetPresent();
        const bool usbReady = usb.GetReady();
        hardware.SetLed(LED_1,
                        usbPresent ? 0.0f : 1.0f,
                        usbPresent ? 1.0f : 0.0f,
                        0.0f);
        hardware.SetLed(LED_2,
                        usbReady ? 0.0f : 1.0f,
                        usbReady ? 1.0f : 0.0f,
                        0.0f);
#endif
        hardware.WriteLeds();
#if AURORA_DIAGNOSTIC != 9
        System::Delay(250);
#endif
#else
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 10
        static uint32_t discontinuityLedUntil = 0;
        if (playback_discontinuity_seen)
        {
            playback_discontinuity_seen = false;
            discontinuityLedUntil = HAL_GetTick() + 120;
        }
#endif
        if (usbPersistenceEnabled)
        {
            usb.Process();
            ServiceLoopStorage();
        }
        UpdateChannelLeds();
        hardware.SetLed(LED_REVERSE,
                        recording_status ? 1.0f : 0.0f,
                        0.0f,
                        0.0f);
#if defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 10
        const bool discontinuityLedOn = HAL_GetTick() < discontinuityLedUntil;
        hardware.SetLed(LED_FREEZE,
                        discontinuityLedOn ? 1.0f : 0.0f,
                        discontinuityLedOn ? 0.0f : 1.0f,
                        0.0f);
#elif defined(AURORA_DIAGNOSTIC) && AURORA_DIAGNOSTIC == 11
        const bool auditFailed = record_audit_failed;
        const bool auditFinished = record_audit_complete;
        hardware.SetLed(LED_FREEZE,
                        auditFailed ? 1.0f : 0.0f,
                        auditFinished && !auditFailed ? 1.0f : 0.0f,
                        record_audit_active || !auditFinished ? 1.0f : 0.0f);
#else
        hardware.SetLed(LED_FREEZE,
                        clearing_status ? 1.0f : 0.0f,
                        0.0f,
                        erasing_status || clearing_status ? 1.0f : 0.0f);
#endif
        hardware.WriteLeds();
        System::Delay(20);
#endif
    }
}
