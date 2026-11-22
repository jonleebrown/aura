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
* **Trigger:** Falling edge (release) of the `Shift` button, **only if** held for `< 300ms` and no secondary Shift+Control parameter was modified during the press.
* **Cycle Order:** `L Mode` $\rightarrow$ `R Mode` $\rightarrow$ `LR Mode` $\rightarrow$ `L Mode`
* **LED Feedback:**
  * **L Mode:** Leftmost LED lit **Green**.
  * **R Mode:** Rightmost LED lit **Blue**.
  * **LR Mode:** Both Leftmost (**Green**) and Rightmost (**Blue**) LEDs lit.

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
| **Warp** | Loop Start Position | `[0.0, 1.0]` mapped across `working_buffer_length`. Circular wrapping preserves loop length. Slewed slightly for smooth tape-scrubbing without clicks. |
| **Time** | Loop Size / Window | Exponential curve from `0.0` (0 samples / silent) at CCW to `100%` of `working_buffer_length` at CW. High resolution at the bottom end for micro-looping. Uses a short dual-tap crossfade (`5–10ms`) on boundary changes to prevent clicks and unwanted pitch sweeps. |
| **Blur** | Playback Speed & Direction | Bipolar exponential curve. **CCW:** `-2.0x` (200% reverse). **Noon (`0.48–0.52` deadband):** `0.0x` (stopped). **CW:** `+2.0x` (200% forward). |
| **Reflect** | Bit Reduction Depth | Maps from 16-bit quantization at CCW to 2-bit quantization at CW. Shift+Reflect controls the wet/dry blend; both controls default to bypass. |
| **Atmosphere** | Glitch Probability | Schedules events from 0% at CCW to at most 90% at CW. Shift+Atmosphere controls the event-duration ratio. |
| **Mix** | Dry / Wet Balance | Clean default uses a linear crossfade between live dry input and wet buffer playback; correlated signals cannot exceed their original peak from the blend. The initial equal-power proposal is deferred until headroom behavior is measured. |

### 3.2 Momentary Buttons & Gate Jacks

| Control | Function | Behavior |
| :--- | :--- | :--- |
| **Reverse** (Btn + Gate) | Momentary Overwrite Record | Replaces buffer contents at the active channel's write head while held/high. If `Blur` speed is `0.0x`, the write head stays at one buffer sample and each incoming sample overwrites that same location; the resulting click or other artifact is acceptable for the first implementation. |
| **Freeze** (Btn + Gate) | Momentary Erase | Progressively zeros the buffer at the active channel's playback head while held/high. |

### 3.3 Shift Secondary Controls (`Hold Shift + Control`)

| Combination | Function | Behavior |
| :--- | :--- | :--- |
| **Shift + Atmosphere** | Glitch Duration | Sets a proportional duration ratio (`[0.001, 1.0]`) of the active loop, with randomized duration from 0.5x to 1.5x that ratio, bounded by the loop length and a 5 ms minimum where the loop permits. |
| **Shift + Reflect** | Bit Reduction Wet/Dry Mix | Linear crossfade (`0%` to `100%`) blending clean and bit-reduced loop audio. The knob position directly sets the blend while Shift is held; Reflect depth retains its unshifted setting. |
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
progressively clears the loop and fades its wet output to silence over 5 ms
while held; Shift+Freeze full erase cuts the wet loop component immediately.
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

Each channel LED briefly flashes at that channel's loop-end crossing. A
selected channel flashes off from its normal mode-indicator state; an
unselected channel flashes on.

**Clean-playback diagnostic:** With Reflect bypassed, the audio path has no
bit reduction, waveshaper, saturation, or other intentional distortion.
Investigation found
two possible gain-boost sources in the previous implementation: equal-power
dry/wet gains can add up to +3 dB for correlated live and recorded signals,
and the equal-power seam blend could similarly boost correlated samples.
The firmware now uses linear dry/wet and linear seam crossfades, which keep
same-polarity correlated samples within their original peak level. These
linear blends replace the equal-power curves specified in the initial design
for the clean default path; equal-power stereo panning remains unchanged.
This removes known algorithmic peak boosts but does not prove the heard
crackle was caused by them. We do not yet have a recorded hardware output or
measured callback timing to establish whether the remaining artifact is
buffer discontinuity, source level, or callback underrun.

**Latest hardware follow-up:** After trying the peak-safe blend update, the
user reports the loop is still louder and has a "squelchy" quality; the earlier
pass-through output remains fine. Full-wet Mix seems too loud, while a Mix
setting around 2 o'clock seems less squelchy. The artifact is not diagnosed
and the proposed blend gain boost was not a sufficient explanation. The user
likes the possibility of intentionally overdriving the sound but wants more
testing before deciding. No attenuation or intentional drive has been added;
do not tune new effects to conceal or depend on this sound. Keep clean/bypass
behavior available and record level-matched comparisons.

Reflect bit reduction is an optional Phase 3 effect. Set its depth without
Shift, then hold Shift and set its wet mix with the same knob; the wet mix
follows the full knob position while shifted. For a maximum-effect check, set
Reflect fully clockwise, then hold Shift and leave it fully clockwise. The
uncontrolled louder/squelchy character is not the intentional effect. Do not
treat clipping or callback underruns as the intended effect.

---

## 5. Atmosphere Generative Glitch Engine

The Atmosphere engine runs independently per channel to allow asynchronous stereo glitching.

1. **Scheduler:** Evaluates a Bernoulli trial every `15ms` to `100ms`, with the interval shortening as **Atmosphere** increases, against the probability (`0%–90%`).
2. **Duration Calculation:** When triggered, event length is randomized around the target duration:
   $$\text{Event Samples} = \text{WorkingBufferLength} \times \text{Ratio}_{\text{Shift+Atm}} \times \text{Rand}(0.5, 1.5)$$
3. **Transformation Selection:** On trigger, randomly activate **one** of the following transformations, crossfading in/out over `3ms` to prevent clicks:
   * **Volume Cut:** Attenuate playback gain to `0.0f` (mute) or `0.25f` (`-12 dB`).
   * **Reverse Burst:** Invert the sign of the current `Blur` playback speed (`speed = -speed`).
   * **Sample Rate Reduction (SRR):** Sample-and-hold playback output for $N$ ticks (randomized between `4x` and `32x` decimation).
   * **Noise Injection:** Mix in generated noise (`20%–50%` level), randomly selecting between **White Noise** (uniform PRNG) and **Pink Noise** (3-pole Paul Kellet filter) per event.

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
(3) Atmosphere Glitch Engine (Volume Cut, Reverse, SRR, White/Pink Noise)
       │
       ▼
(4) Reflect Bitcrusher ──► [Shift + Reflect Wet/Dry Blend]
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
* `DIAGNOSTIC=7 ./scripts/flash-aurora-dfu.sh` runs the Phase 0 SDRAM test. It
  initializes SDRAM, writes and verifies two patterns across the 15.36 MB
  stereo-buffer region, clears it, and indicates the callback block size.
  `DIAGNOSTIC=8` shows the Mix knob and bipolar CV separately; and
  `DIAGNOSTIC=9` services USB-host enumeration and shows device presence and
  readiness separately. The unparameterized flash script now builds the Phase
  1 stereo looper. See the README for LED meanings and DFU commands.
* Initial hardware results: diagnostic 7 showed all three LEDs green. This
  indicates SDRAM test pass, a 96-sample audio block, and an active audio
  callback. In diagnostic 8, the Mix knob changed LED 1 independently, while
  CV at the Mix jack changed LED 2 independently; the SDK readings are
  separate, so knob/CV composition must be implemented explicitly in Karma.
  Diagnostic 9 initially showed LEDs 1 and 2 red when the USB drive was
  connected after flashing. Reflashing diagnostic 9 with the drive already
  connected resulted in both LEDs green (device present and ready). USB-host
  hot-plug was also verified: with diagnostic 9 running and no drive, both LEDs
  were red; connecting the drive changed both to green; removing it returned
  both to red; reconnecting changed them to green again.
* Stereo pass-through audio was confirmed on hardware. The user reported
  signal from left input to left output, left input to right output, and right
  input to right output. These observed routes are accepted as the Phase 0
  pass-through result.

**Exit check:** the SDRAM test passes on the target module, the audio callback
block size and knob/CV behavior are recorded, and USB-host availability is
verified on the target revision. SDRAM passed, the callback reports 96 samples,
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
erasing. Reflect is fully bypassed unless both its depth and Shift-controlled
wet mix are raised. Atmosphere is fully bypassed at zero and otherwise schedules
bounded events. Hardware testing confirms Atmosphere events are audible and
Shift+Atmosphere increases their duration. Reflect is not yet confirmed
audible. The rough playback reported during Phase 2 remains an unresolved
clean-path artifact, not evidence that the effects are working as intended.

**Exit check:** L, R, and LR recording/playback behaviors, control ranges,
Shift gestures, and zero-speed recording match the design; changes between
loop windows behave predictably. The user reports loop length and erase are
working well. Playback character remains an open hardware observation and is
deferred until after Phase 3 effects and Phase 4 loop saving; it is not
accepted as the clean baseline.

### Phase 3: Buffer processing and generative effects

* Implemented optional Reflect bit reduction with a Shift-controlled wet/dry
  blend. Both controls at zero preserve the clean signal path.
* Implemented Atmosphere event scheduling for volume cuts, reverse bursts,
  sample-rate reduction, white/pink noise, and four-cycle repeat/stutter
  events. Repeat slices span 5-500 ms based on the Shift+Atmosphere duration
  control; they play from the recent loop history while the playback head is
  held, then normal playback resumes. Events are independently randomized per
  channel, crossfaded over 3 ms, and bounded by the active loop (up to 40
  seconds).
* Effects process loop playback only. Events are cancelled during recording
  and erasing and when Atmosphere is returned to zero.
* Added portable tests for bypass behavior, deterministic scheduling,
  transformation coverage, duration bounds, and cancellation.
* Hardware feedback confirms Atmosphere events are audible and Shift+Atmosphere
  increases dropout duration; the user is satisfied with this mapping for now.
  Reflect's audibility needs another hardware check; Shift+Reflect maps its
  blend over the full knob range for a more conclusive test.
* Firmware build and portable tests pass. Atmosphere has been heard on
  hardware; its duration control is acceptable for now. Reflect audibility,
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