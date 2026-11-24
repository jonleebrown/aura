# Qu-Bit Aurora: Dual-Channel Looper & Generative Glitch Engine
**Target Hardware:** Qu-Bit Aurora (Electrosmith Daisy Seed / STM32H750)  
**Reference DSP Base:** [hirnlego/repetita-versio](https://github.com/hirnlego/repetita-versio) (`wreath` circular buffer engine)  
**Audio Format:** 48 kHz, 32-bit float, Stereo (40 seconds max per channel)

---

## 1. System Architecture & Memory Allocation

### 1.1 SDRAM Buffer Layout
The Daisy Seed provides 64 MB of external SDRAM. Two independent circular buffers are allocated in SDRAM (`DSY_SDRAM_BSS`):
* **Buffer Left (`buf_L`):** `1,920,000` floats (`40.0s @ 48kHz`, ~7.68 MB)
* **Buffer Right (`buf_R`):** `1,920,000` floats (`40.0s @ 48kHz`, ~7.68 MB)
* **Default Working Buffer Length:** `192,000` samples (`4.0s @ 48kHz`), initialized to silence (`0.0f`).

### 1.2 Boot & USB Audio File Loading
1. On power-up, initialize hardware peripherals, SDRAM, and USB Host (FatFS).
2. Scan root directory of connected USB storage for a default WAV file (e.g., `loop.wav`, 16-bit or 24-bit PCM, 48 kHz).
3. **If USB file exists:**
   * Load audio data into `buf_L` and `buf_R` up to the 40-second maximum (`1,920,000` samples). If mono, duplicate across both buffers.
   * Set `working_buffer_length` to the file's sample length.
   * Immediately begin playback.
4. **If no USB file is found:**
   * Initialize `buf_L` and `buf_R` to `0.0f` with `working_buffer_length = 4.0s`.
   * Enter playback/idle state waiting for Record input. The Atmosphere glitch engine remains active even on an empty buffer (allowing noise injection and rhythmic gating on silence).

---

## 2. Channel Control Modes & UI State Machine

### 2.1 Channel Selection (`Shift` Tap)
* **Trigger:** Every `Shift` release cycles the channel mode unless a Shift secondary action, Shift+Reverse recording, or Shift+Freeze full erase was engaged during that hold. Hold duration does not affect the cycle.
* **Cycle Order:** `L Mode` $\rightarrow$ `R Mode` $\rightarrow$ `LR Mode` $\rightarrow$ `L Mode`
* **LED Feedback:**
  * **L Mode:** LED 1 lit **Green**.
  * **R Mode:** LED 4 lit **Blue**.
  * **LR Mode:** LED 1 (**Green**) and LED 4 (**Blue**) lit.
  * **Loop restart:** LED 2 flashes **Green** for L; LED 3 flashes **Blue** for R.

### 2.2 Parameter Storage & LR Mode "Pickup" Offset Behavior
Aurora exposes knob and CV readings independently. The current firmware
combines each pair as `clamp(knob + CV, 0.0, 1.0)`; knob values are normalized
to `[0.0, 1.0]` and CV values are bipolar `[-1.0, 1.0]`.

* **Single-Channel Modes (`L` or `R`):** Moving a control directly updates the active channel's target parameter (`param_L` or `param_R`).
* **Combined Mode (`LR`):**
  * Upon entering `LR Mode`, latch the current physical input values as `latch[i]`.
  * Track movement as `delta = current_input[i] - latch[i]`, applied as an offset to the stored `param_L[i]` and `param_R[i]` values.
  * **Clamping & Pickup Rule:** Effective channel values are clamped to `[0.0, 1.0]`. If one channel hits a boundary (`0.0` or `1.0`) while the other still has travel remaining, the clamped channel **holds at the boundary** and does not move in the opposite direction until the offset trajectory returns past the exact point where clamping occurred (true value pickup).

---

## 3. Control Mapping

### 3.1 Primary Controls (Active Channel: L, R, or LR)

| Control (Knob + CV) | Function | Curve & Behavior |
| :--- | :--- | :--- |
| **Warp** | Loop Start Position | `[0.0, 1.0]` mapped across `working_buffer_length`. Circular wrapping preserves loop length. Smooth mode slews the read head for tape-style movement; Jump mode fades around a direct window change. |
| **Time** | Loop Size / Window | Exponential curve from `0.0` (0 samples / silent) at CCW to `100%` of `working_buffer_length` at CW. High resolution at the bottom end for micro-looping. Smooth mode slews window changes; Jump mode fades around a direct change. |
| **Blur** | Playback Speed & Direction | Continuous mode is linear: **CCW:** `-2.0x` (200% reverse); **noon:** `0.0x` (stopped); **CW:** `+2.0x` (200% forward). `-1x` and `+1x` are approximately at 9 and 3 o'clock. Shift+Blur latches Continuous or Quantized mode. |
| **Degrade** (Reflect control) | Smooth slip, random read-head jitter, soft overdrive, and bit reduction | A 2% bottom dead zone guarantees bypass at the knob stop; the remaining range is remapped continuously. A random read offset is selected every 96 samples and ramped over that interval, reaching at most ±3 samples at full Degrade. Fractional bit depth follows `16 - 14 × Degrade^0.75`, interpolating between integer quantizer grids. Shift+Degrade controls the wet/dry blend, which defaults to 100% wet; the main Degrade control at zero bypasses all effects. |
| **Drop Out** (Atmosphere control) | Dropout probability | The knob sets a shared baseline; four independent slow LFOs modulate the individual probabilities of stutter, noise, sample-rate reduction, and level drop. Shift+Drop Out controls event duration. |
| **Mix** | Dry / Wet Balance | Linear crossfade between live dry input and wet buffer playback; the first and last 1% of travel snap to exact dry and wet endpoints. Correlated signals cannot exceed their original peak from the blend. |

### 3.2 Momentary Buttons & Gate Jacks

| Control | Function | Behavior |
| :--- | :--- | :--- |
| **Reverse** (Btn + Gate) | Momentary Overwrite Record | Replaces buffer contents at the active channel's write head while held/high. If `Blur` speed is `0.0x`, the write head stays at one buffer sample and each incoming sample overwrites that same location; the resulting click or other artifact is acceptable for the first implementation. |
| **Freeze** (Btn + Gate) | Momentary Erase | Progressively zeros the buffer at the active channel's playback head while held/high. |

### 3.3 Shift Secondary Controls (`Hold Shift + Control`)

| Combination | Function | Behavior |
| :--- | :--- | :--- |
| **Shift + Drop Out** | Dropout Duration | After moving the knob while Shift is held, its absolute position smoothly maps event duration from 10 ms at minimum to the active loop-window length at maximum. Random duration variation grows from zero at minimum to ±50% at maximum, bounded by the active window. Shift alone preserves the current duration. |
| **Shift + Degrade** | Degrade Wet/Dry Mix | Linear crossfade (`0%` to `100%`) blending clean and degraded loop audio. Defaults to `100%` wet. Moving the knob while Shift is held sets the blend; pressing Shift alone leaves the mix unchanged. The Degrade amount retains its unshifted setting. |
| **Shift + Warp or Time** | Window Transition Mode | Moving either control while Shift is held toggles the shared mode between **Smooth** and **Jump** once per Shift hold. The Warp and Time values do not change during this gesture. Smooth slews the read window and produces tape-style pitch movement. Jump fades the wet signal out for 5 ms, moves directly to the new window, then fades back in over 5 ms to avoid clicks. |
| **Shift + Blur** | Speed Mode Toggle | Turning the physical Blur knob while Shift is held toggles between **Continuous** and **Quantized** speed modes once per Shift hold. The selected mode stays latched after Shift is released, while Blur controls speed normally. Continuous mode ranges from `-2x` at minimum through `0x` at noon to `+2x` at maximum, with `-1x` and `+1x` approximately at 9 and 3 o'clock. Quantized mode steps through `-2, -1.5, -1.25, -1, -0.5, -0.25, 0.01, 0.25, 0.5, 1, 1.25, 1.5, 2x` across the knob travel. |
| **Shift + Mix** | Stereo Panning | Equal-power panning (`cos` / `sin`) of the active channel across the stereo output field. (Default: L channel hard left, R channel hard right). |
| **Shift + Reverse** (Btn or Gate) | Redefine Buffer Length | Resets `working_buffer_length` to the exact duration the button/gate is held (clamped to `40.0s` max), recording fresh audio into the new window and applying a `2–5ms` crossfade at the loop seam. |
| **Shift + Freeze** (Btn ONLY) | Full Buffer Erase | Starts a bounded, incremental erase of the full active buffer (`buf_L`, `buf_R`, or both). Ignores the Freeze gate jack. |

---

## 4. Input Routing & Recording Logic

**Implementation routing decision:** Aurora SDK does not expose a reliable
input-jack presence signal, and the user selected direct routing instead of
inferring patch state from audio level. In all modes, `In L` records to
`buf_L` and `In R` records to `buf_R`; L/R/LR mode selects which channel's
controls and transport are active, not which physical input feeds a buffer.
In L mode, record/erase affects only the L channel; in R mode, only the R
channel; and in LR mode, both channels record/erase independently. The
unselected buffer remains unchanged while stereo playback continues. LR is
true stereo. No input summing or channel normalization is performed.

The working-loop duration is zero until a file is loaded or an initial Reverse
recording captures a duration. That first recording starts at index zero and
sets the loop length to the hold time; releasing Reverse begins playback.
Shift+Reverse can later replace the working duration and loop contents with the
captured hold time (maximum 40 seconds). Time scales the active window from
zero to 100% of the current working duration. Warp is scaled within that
duration.
L/R modes apply Time as an absolute normalized
control, while LR mode applies a clamped offset from its entry latch. Freeze
progressively clears the loop and fades its wet output to silence over 1 ms
while held, then restores it over 3 ms after release; Shift+Freeze full erase
cuts the wet loop component immediately.
Full erase clears both channels, resets the working-loop duration to zero, and
stops playback/loop-end LED events. The dry signal remains controlled by Mix.
During recording,
the write window is held at its value from Record press, so moving Warp or
Time cannot move the write head or resize the active recording. On Record
release, the current Warp and Time values immediately set the playback start
and length, computed against the final working-loop size (including a
Shift+Reverse capture). The window is circular within the active working-loop
region: if start plus length passes its end, playback wraps to the region's
beginning rather than reading the unused remainder of the backing buffer.
During an overdub, the write cursor follows the playback phase and wraps
directly to the beginning of the active window, without skipping a seam region.
It repeatedly overwrites that same section of the existing loop. On
record or Freeze press, each channel latches its current window and speed; that
configuration remains fixed for the hold. In LR mode both channels start and
stop on the same audio frames while traversing their independently latched
windows at their respective speeds. Changing controls after release changes
playback of the written or erased segment without changing the segment itself.

Each channel's restart pulse is independent of the steady mode indicators:
LED 2 flashes green at an L loop restart, and LED 3 flashes blue at an R loop
restart.

**Clean playback path:** With Reflect bypassed, the audio path has no
bit reduction, waveshaper, saturation, or other intentional distortion.
Investigation found
two possible gain-boost sources in the previous implementation: equal-power
dry/wet gains can add up to +3 dB for correlated live and recorded signals,
and the equal-power seam blend could similarly boost correlated samples.
The firmware now uses linear dry/wet and linear seam crossfades, which keep
same-polarity correlated samples within their original peak level. These
linear blends replace the equal-power curves specified in the initial design
for the clean default path; equal-power stereo panning remains unchanged.
This removes known algorithmic peak boosts; the later Shift-dependent
listening report pointed to unsmoothed control updates as a stronger cause of
the remaining artifact, described below.

**Clean default setup:** Leave CV inputs unpatched. Set Start fully
counter-clockwise, Length fully clockwise, Blur/Speed near `+1x` (about 3
o'clock in Continuous mode), Mix fully clockwise, and Degrade (Reflect) and
Drop Out (Atmosphere) fully counter-clockwise. At zero, the main effect knobs
bypass their effects regardless of the Shift-controlled Degrade mix and Drop
Out duration. Shift alone should not change the audio. This should provide a
near-input-faithful record/playback reference.

**Control-motion finding:** The audio callback runs at 500 Hz (48 kHz / 96
frames). The SDK's default 2 ms analog-control smoothing computes a coefficient
of `2` at that rate, which is clamped to `1`; knob and CV readings therefore
reach the control state without smoothing. Shift freezes Warp, Time, and Blur
updates, so it can temporarily stop ADC variation from moving the read window
or speed. Firmware now applies a 20 ms one-pole filter to combined knob/CV
values before they reach the control state. Hardware listening is still needed
to confirm that this removes the reported squelch.

**Latest hardware follow-up:** The user reports the loop sounds squelchy with
Degrade and Drop Out at minimum, and becomes cleaner while Shift is held. This
matches the control-motion finding above: Shift freezes Warp, Time, and Blur
while unsmoothed ADC values otherwise update those parameters. The firmware
filter is intended to stabilize the clean default path; hardware listening is
still required to confirm the cause and fix. Degrade's soft drive is a separate
intentional effect with a clean zero-depth bypass.

---

## 5. Drop Out Generative Glitch Engine

The Drop Out engine runs independently per channel to allow asynchronous stereo glitching. Four LFOs use independently randomized periods between 20 and 40 seconds. The Drop Out knob supplies a shared baseline, split evenly among the four effects; each LFO scales its effect's baseline probability, so a zero baseline always disables that effect. Combined event probability is capped at 90%.

1. **Scheduler:** Evaluates event opportunities every `15ms` to `100ms`, with the interval shortening as **Drop Out** increases. LFO-scaled probabilities set the chance and weighted selection of the event type.
2. **Duration Calculation:** The Shift+Drop Out control maps smoothly from a 10 ms minimum for Noise, Level Drop, and Sample Reduction to the active playback-window duration. Random variation grows with the control from 0% to ±50%, and durations are clamped to the active window. Playback speed does not affect event duration. Stutter uses the same loop-relative scale split over four slices, with a 10 ms minimum and 2-second cap per slice.
3. **Transformation Selection:** On trigger, randomly activate **one** of the following transformations, crossfading in/out over `3ms` to prevent clicks:
   * **Level Drop:** Attenuate playback gain to `0.0f` (mute) or `0.25f` (`-12 dB`).
   * **Sample Rate Reduction (SRR):** Sample-and-hold playback output for $N$ ticks (randomized between `4x` and `32x` decimation).
   * **Noise Injection:** Mix in generated noise (`10%–25%` level), randomly selecting between **White Noise** (uniform PRNG) and **Pink Noise** (3-pole Paul Kellet filter) per event.
   * **Stutter:** Repeat a short captured slice four times while the playback head pauses.

---

## 6. Per-Channel Audio DSP Signal Flow

Executed for every sample in the audio callback (96-sample / 2 ms blocks at
48 kHz on the tested Aurora setup):

```text
[Physical Inputs L/R] 
       │
       ▼
(1) Input Router ──► [Buffer Write / Momentary Erase] ──► (SDRAM buf_L / buf_R)
                                                                 │
       ┌─────────────────────────────────────────────────────────┘
       ▼
(2) Circular Buffer Read (Warp Start, Time Window, Blur Speed + Deadband)
       │
       ▼
(3) Drop Out Engine (Stutter, Noise, SRR, Level Drop)
       │
       ▼
(4) Degrade (Read Slip + Soft Overdrive + Bit Reduction)
    ──► [Shift + Degrade Wet/Dry Blend]
       │
       ▼
(5) Mix (Peak-Safe Linear Dry Input vs. Wet Processed Buffer)
       │
       ▼
(6) Stereo Panner (Shift + Mix Equal-Power Pan) ──► [Sum to Physical Out L/R]
```

---

## 7. Phased Implementation Approach

Implement and validate Karma as Aurora firmware first. The VST is not required
for this project or for Aurora builds. Keep the DSP core independent of JUCE
and libDaisy where practical, and run portable unit tests for buffer and
parameter behavior alongside firmware builds.

### Phase 0: Hardware and memory foundation

* Confirm the target Aurora revision, audio/control behavior, USB-host
  connection, and available SDRAM on the correctly powered module.
* Verify SDRAM initialization in the firmware's `BOOT_SRAM` startup mode before
  using any SDRAM-backed buffers. Explicitly initialize and clear loop memory
  after SDRAM is ready; do not assume the startup code zeroes external SDRAM.
* Exercise SDRAM with bounded test patterns and verify the configured
  `48 kHz` audio callback and actual block size. The current Aurora SDK's
  `Hardware::Init()` selects 96 samples per block, whereas this design assumes
  48; explicitly configure or revise that assumption and measure callback
  timing.
* Measure the separate knob and CV readings for representative controls.
  Confirm how knob-plus-CV values should be normalized and clamped before
  treating each pair as one control.
* Verify whether USB mass storage can be enumerated on the target hardware.
  The Aurora SDK exposes USB-host/FatFS support, but the board connection and
  startup sequence must be confirmed on the actual module.
* SDRAM initialization and memory integrity, the 96-sample callback size,
  separate knob/CV readings, USB-host startup and hot-plug, and stereo
  pass-through routing were checked on the target hardware.
* Stereo pass-through audio was confirmed on hardware. The user reported
  signal from left input to left output, left input to right output, and right
  input to right output. These observed routes are accepted as the Phase 0
  pass-through result.

**Exit check:** hardware checks recorded SDRAM integrity, a 96-sample callback,
knob/CV readings are independent, USB mass-storage works both at startup and
through hot-plug, and pass-through audio was confirmed on left-to-left,
left-to-right, and right-to-right routes. Phase 0 is complete; proceed to the
stereo circular looper while retaining the observed routing behavior for
testing.

### Phase 1: Minimal stereo circular looper

* Implemented a fixed-capacity stereo buffer abstraction in
  `shared/include/aurora/dsp/StereoLooper.h`, with two 40-second float buffers
  in SDRAM and a zero-length working loop until a file is loaded or recorded.
  The memory region is explicitly
  initialized and cleared before audio starts.
* Implemented fixed-region circular recording, loop-start/length selection in
  the DSP API, forward playback, a 5 ms equal-power loop-seam crossfade,
  equal-power dry/wet blending, and progressive erase under the playback head.
  The Aurora adapter uses Reverse to record,
  Freeze to erase, and Mix knob for dry/wet; both audio channels are recorded
  independently. CV composition, variable loop size, and speed control remain
  for later phases.
* Processing uses the actual 96-sample hardware callback and performs no
  allocation, memory clearing, or file I/O in the audio callback.
* Added host-portable tests for initialization/clear, bounds, wraparound,
  deterministic playback, channel independence, dry/wet output, erase, and
  zero-length loop behavior.
* Hardware playback was tested: the user reports clear sound, no meaningful
  clicks or dropouts, and a sound close to the original pass-through. Mix
  operation was confirmed. Left input was heard at left and right outputs;
  right input was heard at right output. Playback and progressive erase were
  also tested and confirmed working; the user reported liking the result.

**Exit check:** a 4-second stereo loop records, wraps, and plays without clicks
at the seam or audio dropouts; memory use and callback timing are measured.
The user reports the audio quality, Mix behavior, and observed routes are good,
and playback plus progressive Freeze erase work, so Phase 1 is accepted for
initial hardware testing. Long-duration capacity and full four-second
wraparound/seam behavior remain to be tested explicitly.

### Phase 2: Transport, recording, and core controls

* Add L, R, and LR mode state, Shift tap/hold gesture recognition, and
  per-channel parameter storage.
* Implement direct L-to-L and R-to-R recording, overwrite recording on
  Reverse, and erase on Freeze. At `0.0x` speed, keep the record head fixed
  and overwrite its one sample for each incoming frame; clicks are acceptable
  in this first pass.
* Add Warp loop-start positioning, Time loop length, Blur speed/direction,
  Mix, and the specified stop-speed deadband. Slew or crossfade read-window
  changes where needed to prevent unwanted clicks.
* Implement LR pickup/offset handling using the observed independent knob and
  CV readings, combined as a clamped sum.
* Use LEDs for explicit mode/state feedback and verify button/gate edge
  handling on hardware.

**Implementation status:** Phase 2 firmware now has L/R/LR mode state
(initially LR), Shift tap and secondary-control cancellation, per-channel
control storage and LR offsets, direct channel routing, Warp/Time/Blur/Mix,
progressive erase, Shift+Mix pan, Shift+Reverse loop-length capture, and
incremental Shift+Freeze full-buffer erase. Portable tests pass. No Phase 2
hardware acceptance has been performed yet; controls and transport still need
validation on the Aurora before Phase 2 can be marked complete.

Initial hardware feedback on the first Phase 2 build: recording was apparent
in LR mode but not in L/R modes; playback sounded rough; progressive Freeze
caused crackles and seemed to reduce level; and Time did not feel consistent
across its range. Follow-up updates have mode-specific regression coverage,
map Time to the current working-loop duration (previously four seconds by
default; now zero until a file is loaded or a loop is recorded), fade the wet
output during progressive erase, and remove known equal-power mix/seam peak
boosts. The user later reported that loop length and erase were working;
the louder, squelchy loop character remained. For L-mode tests, patch the
source to In L and monitor Out L; for R mode, use In R and monitor Out R. LR
mode records both direct routes. Check Mix fully wet when judging the erased
loop; the dry signal intentionally remains according to Mix.

Phase 3 effects are now in the loop-playback path. They do not affect live input
monitoring or recorded buffer contents, and are cancelled while recording or
erasing. Degrade combines read-head slippage, soft overdrive, and bit reduction;
the slippage follows Degrade amount, while Shift+Degrade blends the driven and
quantized audio. Drop Out is fully bypassed
at zero and otherwise schedules bounded events with four slow probability
LFOs. Hardware testing confirms Drop Out events are audible and Shift+Drop Out
increases their duration. Degrade's new overdrive and bit reduction still need
hardware listening. The rough playback reported during Phase 2 remains an
unresolved clean-path artifact, not evidence that the effects are working as
intended.

**Exit check:** L, R, and LR recording/playback behaviors, control ranges,
Shift gestures, and zero-speed recording match the design; changes between
loop windows behave predictably. The user reports loop length and erase are
working well. Playback character remains an open hardware observation and is
deferred until after Phase 3 effects and Phase 4 loop saving; it is not
accepted as the clean baseline.

### Phase 3: Buffer processing and generative effects

* Implemented Degrade with read-head slippage, soft overdrive before bit
  reduction, and a Shift-controlled wet/dry blend.
* Implemented Drop Out probability modulation for level drops, sample-rate
  reduction, white/pink noise, and four-cycle repeat/stutter events. Four
  independently randomized 20-40 second LFOs scale effect probabilities.
  All event durations use the active loop window and Shift+Drop Out duration,
  independent of playback speed, with a 10 ms minimum. Stutter uses that same
  total duration for four repeats; each captured slice has a 10 ms minimum and
  2-second cap. Slices play
  from recent loop history while the playback head is held, then normal playback
  resumes. Events are independently randomized per channel and bounded by the
  active loop (up to 40 seconds).
* Effects process loop playback only. Events are cancelled during recording
  and erasing and when Drop Out is returned to zero.
* Added portable tests for bypass behavior, deterministic scheduling,
  transformation coverage, duration bounds, and cancellation.
* Hardware feedback confirms Drop Out events are audible and Shift+Drop Out
  increases dropout duration; the user is satisfied with this mapping for now.
  Degrade's overdrive and bit reduction need another hardware check; Shift+Degrade maps its
  blend over the full knob range for a more conclusive test.
* Firmware build and portable tests pass. Drop Out has been heard on
  hardware; its duration control is acceptable for now. Degrade audibility,
  output levels, and callback timing still need further testing. Test one
  effect at a time and compare against bypass at matched level. Full-wet Mix
  is reported too loud; no attenuation change has been made while the user
  evaluates the looper's possible overdrive character. The louder/squelchy
  clean-loop playback remains unexplained and is not an intended effect.

**Exit check:** each effect can be tested independently, combinations stay
within CPU and memory budgets, and glitch transitions do not cause unexpected
clicks or persistent channel coupling. Hardware acceptance remains pending.

### Phase 4: USB WAV persistence (parked)

* WAV load/save and `KARMA.LOG` code is retained for later experimentation,
  but is disabled in the normal firmware. The current firmware performs no USB
  initialization, startup reads, audio writes, or log writes.
* Reverse indicates recording only. Freeze indicates progressive/full erase
  only and does not show persistence status.
* Persistence was observed to interrupt playback and is shelved until it can
  be redesigned without disrupting audio. Recorded loops are RAM-only and
  lost at power-off.
* The earlier log writer was best-effort and suppressed its own open/write/
  sync/close errors. It only ran after the USB storage service was eligible to
  perform a load or save, so a missing `KARMA.LOG` does not identify which
  operation failed—or establish that logging was attempted.

**Exit check:** resume only after storage I/O can be isolated from audio
playback and log failures can be reported through a channel other than the
same USB device being diagnosed.

### Phase 5: Integration and hardware acceptance

* Run the complete control and signal-flow matrix on the Aurora: L, R, and LR;
  button and gate actions; forward, reverse, and stopped speed; loop resizing;
  effects; and dry/wet/pan behavior.
* Measure worst-case callback time with the maximum loop size and effects
  enabled. Check SDRAM integrity and startup behavior across repeated power
  cycles.
* Keep the firmware build, DFU flash, and hardware test workflow independent
  of building or installing the VST.

**Exit check:** the documented design is usable on hardware, with known
limitations and supported WAV/media behavior recorded here.

## v0.2 Goal: Expressive Overdubbing and Time Manipulation

This is a planning goal; the behavior below is not implemented yet. It builds
on the existing L/R/LR channel selection and keeps input monitoring, buffer
writing, and playback effects as distinct signal paths.

### L/R performance controls and overdub levels

Move effect editing to its own control page/bank. This frees the L/R channel
page for six per-channel controls. A proposed physical mapping is Warp/Start,
Time/Length, Blur/Speed, Reflect/Pitch, Atmosphere/Input Level, and Mix/Loop
Level. These assignments are a starting point and can be revised during
hardware use.

Input Level and Loop Level set the sources written into the selected channel
while Reverse is held. Input Level controls the external input contribution;
Loop Level controls the existing loop return, optionally after its selected
effects. A default of Loop Level at zero and Input Level at unity preserves
the current punch-in behavior. Raising Loop Level adds the loop return to the
write signal; lowering Input Level removes external input. These are record
gains, separate from live monitoring Mix.

Treat them as source gains: `write = input * input_level + loop_return *
loop_level`. This gives direct control of both sources, but both at unity can
sum correlated signals above full scale. Decide whether that extra gain is
intentional or whether the gains should be normalized; document the choice and
test repeated passes for level growth. With Loop Level at maximum and Input
Level at zero, the loop is reprocessed and replaced rather than accumulated,
so repeated passes may progressively compound Degrade and other effects. L
and R write only to their selected channel; LR writes both channels over the
same held time span using each channel's own playback state. L/R/LR selection
does not change the monitoring Mix. Because Mix is proposed as Loop Level on
the L/R page, the live monitoring Mix needs a new assignment (for example, on
a utility/monitor page). Panning is not needed: preserve the two independent
channels and their hard L-to-L, R-to-R output routing, with no pan control.

Pitch is a new parameter for pitch-independent stretching and should be
implemented with that mode. In varispeed mode, playback pitch remains linked
to Speed unless a separate pitch-shift mode is explicitly designed.

### FX control page

Move Drop Out and Degrade controls into a dedicated FX page/bank so their
parameters can be edited independently per channel. Add separate overdrive
amounts for the external-input and loop-return write paths, so either source
can be driven before its Input Level or Loop Level is applied. This allows
recording a driven input, reprocessing and recording the existing loop, or
blending both. Keep these write-path drive stages distinct from live monitor
level and make it clear when their sound is being written into the buffer.
Keep channel selection (L, R, LR) separate from the selected control page:
opening the FX page should not change which channels are playing or targeted
by Reverse. The earlier
proposal split Warp, Blur, and Mix across the L FX controls and Time, Reflect,
and Atmosphere across R FX controls; the new six-control performance page
replaces that split, so the FX page's knob assignments and navigation need to
be designed anew. Prioritize independent Drop Out controls, then decide which
Degrade parameters need independent per-channel values.

### Recording the effected signal

FX overdubbing uses the source-specific drive stages on the write path. Decide
which other effects, such as Degrade or Drop Out, are also printed into the
buffer. Apply each effect once to the signal being written; avoid accidentally
processing it again on the monitor path while Reverse is held. Respect L/R/LR
target selection and LR shared press/release timing, and verify that an effect
event or its state cannot leak between channels.

### Pickup when changing control layers

Add soft pickup independently for each physical knob and control context
(normal, Shift secondary, and any FX control bank). After a layer or channel
selection changes, keep its stored parameter fixed until the physical knob
reaches or crosses that parameter's stored control position; then resume
continuous tracking. Define pickup targets for LR offsets and for controls
whose physical mapping changes by mode. This prevents releasing Shift or
switching modes from snapping the unshifted parameter to the knob's current
position. Mode feedback should also indicate when a control is waiting for
pickup.

### Overdub latency compensation

Measure the round-trip delay of the intended overdub route before advancing
the write position. A fixed sample offset can align a stable external
output-to-input loop, but it cannot correct frequency-dependent phase shift
from effects or a time-stretch algorithm. Keep direct-input punch-in at zero
compensation unless measurement shows it needs adjustment. Validate with an
impulse or correlation-based loopback measurement, then test alignment across
speed, direction, and effect settings; make any user trim a bounded sample
offset rather than assuming one setting fits every signal path.

### Pitch-independent stretch prototype

Prototype a pitch-independent stretch mode while retaining varispeed for
immediate tape-like pitch changes. Prioritize responsive forward/reverse
scrubbing, smooth zero-speed behavior, and stable control changes. [Bungee](https://github.com/bungee-audio-stretch/bungee)
is a promising first candidate: its documented granular API exposes per-grain
position and pitch, and it supports continuously changing, zero, and negative
playback speeds. [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch)
is a useful comparison for quality and resource use, though its project
documentation says time-stretching sounds best over more modest rate changes
(about 0.75x–1.5x), while supporting wider pitch shifts. Before selecting
either library, benchmark stereo CPU load, memory, latency, licensing, and
control response on the STM32H750. Keep the
current playback path available as a fallback and do not add a library to the
firmware until a hardware prototype meets those constraints.

### Suggested implementation order and acceptance checks

1. Specify and measure record-layer pickup behavior, including Shift release
   and LR offsets.
2. Implement the Input Level and Loop Level write gains and confirm their
   endpoints, monitoring independence, channel targeting, and headroom behavior.
3. Add effected-signal overdubbing, then measure and expose compensation only
   for routes where a repeatable delay exists.
4. Finalize the FX page assignments, including the two write-path drive
   amounts; add independent Drop Out controls first, then separate Degrade
   controls as needed.
5. Build a standalone stretch prototype and compare Bungee and Signalsmith on
   real recorded material before integrating one into the looper.

**v0.2 exit check:** knob-layer and channel changes do not cause parameter
jumps; Input Level and Loop Level have predictable endpoints and remain
independent of monitoring Mix; external-input and effected-signal overdubs
target the selected channels without unintended cross-channel writes;
compensation is based on measured latency; and any stretch mode maintains the
desired scrub response within measured CPU, memory, and latency budgets.

### Initial implementation risks and assumptions

* `BOOT_SRAM` startup must be checked carefully: the normal Daisy Seed init
  path may skip SDRAM initialization for programs not executing from internal
  flash. Do not place or access the loop buffers until external SDRAM is
  initialized.
* A zero-length Time setting is specified as silent; the implementation must
  avoid invalid read positions and still define whether recording continues.
* Cable-presence-dependent input routing has been dropped because the SDK does
  not expose jack detection; direct L-to-L and R-to-R routing is the chosen
  fallback.
* Button/gate precedence, Shift cancellation after secondary-control changes,
  and simultaneous Record/Erase behavior should be tested and documented as
  part of the transport state machine.
