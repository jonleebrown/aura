# Karma Aurora Test Bed

This repository is a test bed for developing firmware and a matching VST
instrument/effect for the Qu-Bit Electronix Aurora eurorack module. Aurora is
Daisy-based; the hardware firmware uses the Aurora SDK and libDaisy, while the
desktop plugin uses JUCE. Platform-independent DSP can be shared between the
two adapters as Karma develops.

The former frequency-modulation/cross-modulation prototype has been removed.
The Aurora firmware now contains the Phase 2 Karma looper controls and Phase 3
optional playback effects; the separate VST test harness remains pass-through
and is not part of the Aurora build.

## Aurora firmware

Requirements: the Daisy ARM toolchain, the Aurora SDK, libDaisy, and DaisySP.
The SDK and its dependencies are tracked as nested Git submodules under `lib/`.
Initialize them after cloning with:

```sh
git clone --recurse-submodules <repository-url>
# Or, for an existing checkout:
git submodule update --init --recursive
```

The regular build/flash script builds and flashes the current Karma firmware:

```sh
./scripts/flash-aurora-dfu.sh
```

The default firmware initializes SDRAM and clears two 40-second buffers. It
starts in **LR** mode with no loop region. Playback is silent until a loop is
explicitly recorded. The first **Reverse** recording (button or gate) on an
empty buffer records from index zero and sets the loop length to the hold
duration. Releasing Reverse starts playback of that loop. Once a loop exists,
Reverse overdubs at the playhead and stays within the active playback window;
each wrap overwrites that same window again. Hold **Shift+Reverse** to replace an
existing loop with a new recording from index zero; its duration is the hold
time. You can press Shift before or after Reverse. Hold **Freeze**
(or its gate) to erase progressively under the playback head. Overdub and
Freeze latch each channel's current playback window and speed when the control
is pressed, then follow that setting for the duration of the hold. Later
changes to Start, Length, or Speed affect playback of the resulting loop
segment. In LR mode both channels begin and end together, with each channel
using its own latched settings. Input routing is direct: L records to the L
buffer and R to the R buffer. No audio-level or jack-presence heuristic is
used.
The selected mode gates recording and erasing: **L** records/erases only
`In L`/the L buffer, **R** only `In R`/the R buffer, and **LR** records or
erases both corresponding channels. The unselected buffer is left unchanged;
stereo playback and output remain available in every mode.

The six knob/CV pairs are combined in software as `clamp(knob + CV, 0, 1)`;
Aurora's knob and CV accessors are independent. Warp controls loop start, Time
controls loop length from 0 to 100% of the current working-loop duration, Blur
controls playback speed/direction (including a zero-speed deadband), and Mix
controls a linear dry/wet blend that cannot boost correlated dry and loop
signals. Mix's first and last 1% of travel snap to exact dry and wet endpoints.
Time uses a squared curve over the current working-loop duration.

### Clean default setup

For a clean, full-loop reference, leave CV inputs unpatched and set Start fully
counter-clockwise, Length fully clockwise, Blur/Speed near `+1×` (about 3
o'clock in Continuous mode), and Mix fully clockwise. Set Degrade (Reflect) and
Drop Out (Atmosphere) fully counter-clockwise. At zero, their Shift-controlled
wet mix and event duration do not enable either effect. Shift alone should not
change this sound. This setup should record and play back a loop close to the
input; the normal control values are smoothed over about 20 ms to prevent ADC
steps from modulating the read window or speed.

Holding Shift+Reverse records from buffer index zero at 1x and captures a new
working-loop duration. If Shift is released first, ordinary
Reverse recording continues; when Reverse (button or gate) is released,
playback snaps to the captured region. In L or R mode,
Time follows the knob directly over the full range. In LR mode, moving a
control applies a clamped offset to each channel's stored setting, so its
travel is relative to the values latched on entering LR mode. Window changes
are slewed during playback, and the loop seam has a short crossfade. During
recording, the current write window stays fixed; Warp and Time changes apply
immediately on ordinary Record release using the current working-loop size.
If the selected start plus length crosses the end of the active working-loop
region, playback wraps to the beginning of that region; it does not read the
unused remainder of the 40-second backing buffer. At a 100% Time setting and
50% Warp, playback should play the latter half of the region followed by its
first half.

Hold Shift and move either Warp or Time to toggle the window transition mode
between Smooth and Jump. This gesture does not change the stored knob value,
and toggles once per Shift hold. Smooth moves the read window gradually,
producing tape-style pitch movement. Jump fades the wet signal out over 5 ms,
moves directly to the new window, then fades back in over 5 ms to avoid clicks.
Hold Shift and turn Blur to toggle between Continuous and Quantized speed modes.
The mode stays selected after Shift is released, and the Blur knob continues to
control speed normally. Continuous mode ranges from `-2×` at minimum through
`0×` at noon to `+2×` at maximum; `-1×` and `+1×` are approximately at 9 and
3 o'clock. Quantized mode steps through `-2, -1.5, -1.25, -1, -0.5, -0.25,
0.01, 0.25, 0.5, 1, 1.25, 1.5, 2×` across the knob travel.

Degrade (the Reflect control) combines smooth read-head slip, controlled
random read-head jitter, soft overdrive, and 16-to-2-bit quantization on loop
playback. Both the jitter amount and bit-depth reduction increase continuously
with the Degrade knob. Its bottom 2% is a hard bypass to absorb small ADC
offsets at the knob stop. Hold Shift and use the same control to set its wet/dry
blend, which defaults to 100% wet. Shift+Degrade changes the blend only after
you move the knob; pressing Shift alone leaves it unchanged.
Drop Out (the Atmosphere
control) schedules volume cuts, sample-rate reduction, white or pink noise,
and short repeat/stutter events. Four slow
LFOs modulate the event probabilities from the shared Drop Out baseline; set
Drop Out fully counter-clockwise for bypass. Hold Shift and move Drop Out to
adjust event duration as a proportion of the active loop window. At minimum
Shift+Drop Out, Noise, Level Drop, and Sample Reduction target 10 ms regardless
of loop-window length. Turning it up grows the target smoothly toward the
active loop-window length, with randomized variation increasing along the
range. Stutter uses the same loop-relative scale, split into four repeats;
each slice has a 10 ms minimum and 2-second cap. Playback speed does not change
event duration. The playhead pauses for the repeats, then playback resumes.
Drop Out events are cancelled during
record and erase. Effects are not applied to input monitoring or written into
the loop buffer.

LED 1 glows green in L mode, LED 4 glows blue in R mode, and both glow in LR
mode. LED 2 flashes green when the L loop restarts; LED 3 flashes blue when
the R loop restarts.

## USB persistence (parked)

USB loop persistence is disabled in the current firmware: it does not read a
file at startup, write audio to disk, or create a log file. Recorded loops
remain in RAM until power-off. The earlier WAV load/save and `KARMA.LOG`
implementation is retained in the source for later experimentation, but it
caused unwanted playback interruption and is not active.

The previous build did not guarantee `KARMA.LOG` would appear. Logging was
best-effort: it silently returned if opening the log failed and ignored write,
sync, and close failures. It only attempted a log write after a storage
operation had run, which depended on USB being reported ready. Thus an empty
drive or Freeze LED alone could not show whether logging was attempted or why
it failed. The current build avoids that USB activity entirely.

With Reflect bypassed, the clean DSP path contains no intentional saturation
or bit crushing. The previous equal-power dry/wet and seam blends could add up
to 3 dB when their signals
were correlated, so the clean default now uses linear blends. This addresses a
specific possible source of overdrive, but the hardware crackle is not yet
confirmed fixed. The latest hardware report says pass-through remains fine,
but loop playback is still louder and "squelchy." The latest report adds that
full-wet Mix sounds too loud, while a Mix position around 2 o'clock seems less
squelchy; the cause remains unknown. No output attenuation has been added
pending more level-matched hardware testing, since the user also likes the
possibility of intentionally overdriving the sound. This character is not
enabled intentionally; keep Reflect's blend at zero and Atmosphere fully
counter-clockwise to hear the clean path. Root-cause diagnosis is deferred
until after playback behavior is stable. A more recent observation isolates
the crackles to loop playback: the analog oscillator passes cleanly, and the
crackles recur at the same recorded positions across takes. Diagnostic 10
targets the playback output without changing it.

Press and release **Shift** to cycle modes, regardless of how long it is held.
If a Shift secondary control, Shift+Reverse recording, or Shift+Freeze full
erase is engaged during the hold, its function runs and the channel mode does
not change. The firmware starts in LR to retain Phase 1 stereo behavior; the
cycle is LR → L → R → LR. LED 1 green indicates L, LED 4 blue indicates R, and
both indicate LR. Hold Shift with Mix to pan; Shift+Reverse records at 1x from
the start of the buffer and sets the working-loop duration to the held duration
(up to 40 seconds).
Shift+Freeze on the button starts a bounded, incremental full-buffer erase;
the loop component is cut immediately, while any dry mix remains audible. The
full erase clears both channels, stops playback, and removes pending loop-end
LED flashes; the gate alone does not trigger a full erase. Shift+Degrade sets
the degradation wet/dry blend, and Shift+Drop Out sets the dropout duration ratio. The Reverse
LED is red only while recording; Freeze indicates erase activity only, blue
while progressively erasing and magenta during a full erase. Neither LED
indicates USB or persistence status. Input is monitored at unity while
recording, and the Aurora build does not build or flash the VST.

Optional hardware checks can still be selected with `DIAGNOSTIC`:

```sh
# Audio callback/input activity
DIAGNOSTIC=1 ./scripts/flash-aurora-dfu.sh

# 440 Hz tone
DIAGNOSTIC=2 ./scripts/flash-aurora-dfu.sh

# Right input to both outputs at 4x gain (start mixer low)
DIAGNOSTIC=3 ./scripts/flash-aurora-dfu.sh

# Mix/Blur knob readings
DIAGNOSTIC=5 ./scripts/flash-aurora-dfu.sh

# Stereo inputs to outputs at 8x gain (start source and mixer low)
DIAGNOSTIC=6 ./scripts/flash-aurora-dfu.sh

# SDRAM and callback-block-size diagnostic
DIAGNOSTIC=7 ./scripts/flash-aurora-dfu.sh

# Mix knob and bipolar CV readings
DIAGNOSTIC=8 ./scripts/flash-aurora-dfu.sh

# USB mass-storage presence/readiness (connect a drive to Aurora)
DIAGNOSTIC=9 ./scripts/flash-aurora-dfu.sh

# Full-wet, effects-off looper playback with a discontinuity LED
DIAGNOSTIC=10 ./scripts/flash-aurora-dfu.sh

# Compare a first recording against the samples written to the loop buffer
DIAGNOSTIC=11 ./scripts/flash-aurora-dfu.sh
```

Diagnostics 3 and 6 can clip; start with low source and mixer levels.
Diagnostic 10 is the playback investigation build: record normally, then play
back at full wet with Reflect and Atmosphere bypassed. Mix is forced fully wet.
The Freeze LED is normally green and flashes red when the playback output's
second sample difference exceeds 0.25 (normalized full scale). This marks a
sharp waveform change, which can be a click or a legitimate sharp transient;
it does not alter audio or measure callback timing. Use a steady, low-level
oscillator, Warp fully counter-clockwise, Time fully clockwise, and Blur near
1x when comparing the regular and diagnostic builds.
Diagnostic 11 audits the first recording made into an empty buffer. After
full-erasing, record the steady oscillator and release Reverse; the build
stores a reference copy of the input and compares it with the loop buffer's
sequential samples while playback proceeds. Freeze is blue while waiting or
auditing, green when every written sample matches the reference, and red if
any sample differs (including a loop-duration mismatch). It forces full-wet,
effects-off, 1x playback and uses two additional SDRAM reference buffers. A
green result means the buffer write matches the captured digital input; it
does not distinguish an ADC/input artifact from an artifact already present
in those input samples. The audit exposed precision loss in the normalized
floating-point recording head at unity speed; the write cursor now advances
in sample units using double precision. A regression test checks every sample
in a four-second recording. Hardware testing after flashing the corrected
build confirmed the playback crackles are gone.
Diagnostic 7 uses LED 1 green/red for SDRAM pass/fail, and LED 2 blue for
48-sample blocks, green for 96, or red for another block size. The Freeze LED
indicates audio callback activity. In diagnostic 8, LED 1 displays Mix knob
level and LED 2 shows bipolar CV polarity. In diagnostic 9, LEDs 1 and 2 show
USB device presence and readiness, respectively. Initial hardware testing
reported diagnostic 7 all green (SDRAM passed, 96-sample blocks, callback
active) and independent knob/CV readings in diagnostic 8. Diagnostic 9
initially showed both LEDs red when the drive was connected after flashing;
reflashing with the drive already connected produced both green LEDs (present
and ready). Hot-plug was then confirmed: with Aurora running diagnostic 9 and
no drive, both LEDs were red; connecting the drive changed them to green;
removing it returned them to red; reconnecting changed them to green again.

To test hot-plug, flash diagnostic 9 with no USB storage device connected.
Leave Aurora powered and the diagnostic running: LEDs 1 and 2 should start red.
Connect the drive without resetting or reflashing, then wait several seconds;
LED 1 should turn green when it is detected and LED 2 green when it is ready.
Unplug and reconnect the drive while leaving Aurora powered. The LEDs should
return to red and then green/green again. If they only change after a restart,
hot-plug enumeration is not working. This sequence has now been confirmed on
the target module.

### Build and flash quick start

Build without flashing with `./scripts/build-aurora.sh`. To build and flash the
normal looper, run `./scripts/flash-aurora-dfu.sh`; for a diagnostic, prefix it
with its selector, e.g. `DIAGNOSTIC=10` for playback or `DIAGNOSTIC=11` for the
record audit. To build without flashing, run
`DIAGNOSTIC=11 ./scripts/build-aurora.sh`. The flash script builds first, then
waits for the module's Daisy bootloader.

The flash script uses the Daisy bootloader (`0483:df11`), not STM32 ROM DFU.
Keep the Daisy connected to the computer's USB device port, restart with RESET,
then press BOOT while the bootloader's pulsing grace-period LED pattern is
active. `dfu-util` is required; on macOS, install it with `brew install
dfu-util`. The firmware uses `APP_TYPE=BOOT_SRAM` and is written to QSPI at
`0x90040000`.

To build without flashing, run `./scripts/build-aurora.sh`. To stage the binary
on the Aurora USB update volume, first build and then run:

```sh
./scripts/stage-aurora-firmware.sh /Volumes/AURORA
```

The staged binary is `Karma.bin`; safely eject the drive, insert it into Aurora,
and power-cycle the module. Override the ARM toolchain path with
`GCC_PATH=/path/to/gcc-arm-none-eabi/bin`.

Run portable looper/control tests with `make test`. For Phase 2 hardware
testing, flash with `./scripts/flash-aurora-dfu.sh`. For L mode, patch a source
to In L and monitor Out L; for R mode use In R and Out R. LR uses both direct
routes. Check recording with Mix fully wet for playback and test Freeze erase
at full wet to distinguish erased content from the dry input. Tap Shift
through LR, L, and R; record and erase in each mode. Test
Warp and Time changes, Blur forward/reverse/stop, Shift+Mix panning,
Shift+Reverse length capture, and Shift+Freeze full erase. The earlier Phase 1
hardware checks reported clear playback, working Mix, and working playback
Plus progressive erase. Initial Phase 2 hardware feedback: L/R-mode recording
was not apparent while LR recording worked; playback sounded rough, Freeze
crackled and seemed to reduce level, and the Time range felt inconsistent.
The current update scales Time within the working-loop duration, starts at a
four-second working loop, uses a peak-safe linear crossfade at loop seams and
for dry/wet mix, and fades the wet output during progressive erase.
The repeated playback crackles were traced to precision loss in the recording
write cursor; a sample-unit cursor fixed the issue, confirmed on hardware.
Reflect/Atmosphere operate only on loop playback. Full 40-second capacity and
seam behavior remain unverified.

## VST test build

The JUCE VST3 currently provides stereo pass-through and an Aurora-style
layout as a desktop test harness. It does not yet contain the Karma looper.
Build/install it separately with:

```sh
./scripts/build-vst.sh
```

Requirements: CMake 3.22+, a C++17 compiler, and JUCE 9 (or compatible). Set
`JUCE_SOURCE_DIR` if JUCE is not at the default local path. JUCE licensing
terms apply; review the [JUCE license](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)
before distributing the plugin.

In VS Code, `Cmd+Shift+B` runs **Build & Flash Aurora Firmware**. Choose
**Build & Stage Aurora Firmware** from `Terminal > Run Task...` for the USB-drive
update path, or **Build and Install VST3** for the desktop harness.

## Resources

The [`reference-docs/`](./reference-docs/) directory indexes project
documentation and external references for Aurora, alternate firmware, JUCE,
and Daisy/libDaisy.

## Status

Phase 0 diagnostics and Phase 1 stereo looper were hardware-tested. Phase 2
mode selection, per-channel controls, loop-window/speed controls, and Shift
transport gestures are implemented and covered by portable tests; Phase 3
effects are implemented, with Atmosphere heard on hardware and Reflect still
awaiting a clearer check. USB WAV persistence is implemented but parked and
disabled because file I/O interrupted playback; the active firmware performs
no USB reads or writes. The Karma looper in the JUCE harness remains
unimplemented.
