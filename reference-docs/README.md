# Reference Documentation

This directory is the starting index for references used to understand Aurora,
its firmware and development stack, and the desktop plugin environment. For
now, these are links to their upstream sources; no external documentation has
been copied into this repository.

## Aurora hardware and firmware

- [Aurora product page](https://www.qubitelectronix.com/shop/p/aurora) —
  manufacturer overview of the module and its capabilities.
- [Aurora user manual (PDF)](https://www.qubitelectronix.com/s/Aurora_v174.pdf)
  — controls, operation, and hardware behavior.
- [Aurora getting-started guide (PDF)](https://www.qubitelectronix.com/s/Aurora_getting_started_v10.pdf)
  — quickstart and operating basics.
- [Aurora Spectral Reverb](https://www.qubitelectronix.com/alternate-firmware/p/aurora-spectral-reverb)
  — official firmware page, including its manual and firmware download.
- [Aurora FDN Verb](https://www.qubitelectronix.com/alternate-firmware/p/fdn-verb)
  — official alternate firmware page and quickstart guide.

## Aurora SDK and community firmware

- [Aurora-SDK repository](https://github.com/Qu-Bit-Electronix/Aurora-SDK) —
  official source, examples, build instructions, and bundled dependencies.
- [Aurora-SDK reference documentation](https://qu-bit-electronix.github.io/Aurora-SDK/)
  — generated SDK/API reference.
- [Aurora SDK Ringmod example](https://github.com/Qu-Bit-Electronix/Aurora-SDK/blob/main/Examples/Ringmod/Ringmod.cpp)
  — official example of a sine-oscillator ring modulator with Mix, Warp, and
  Blur controls; useful alongside this project's cross-channel product effect.
- [Qu-Bit Aurora firmware template](https://github.com/DaveParr/qubit-aurora-template)
  — minimal Aurora firmware starter with build targets, SDK submodule setup,
  host-side tests, and a basic hardware passthrough example.
- [SpectralBandSweep](https://github.com/Tsarpf/SpectralBandSweep) — community
  Aurora firmware project demonstrating FFT band controls and a comb-filter
  section. Its README notes current I/O and optimization limitations.
- [Aurora-Firmwares](https://github.com/jfriess/Aurora-Firmwares) — collection
  of additional community firmware projects, including FataMorgana and Tempest.

## Daisy firmware and DSP

- [libDaisy](https://github.com/electro-smith/libDaisy) — Daisy hardware
  abstraction library source and getting-started links.
- [libDaisy API reference](https://docs.daisy.audio/libDaisy/namespacedaisy/)
  — generated API documentation.
- [Daisy C++ development environment](https://docs.daisy.audio/tutorials/cpp-dev-env/)
  — toolchain setup, examples, and firmware workflow.
- [DaisySP](https://github.com/electro-smith/DaisySP) — portable C++ DSP
  library used in the Daisy ecosystem.

## JUCE and audio plugins

- [JUCE repository](https://github.com/juce-framework/JUCE) — framework source
  and examples.
- [JUCE CMake API documentation](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md)
  — project integration and plugin build setup.
- [JUCE Audio/MIDI plugin tutorial](https://juce.com/learn/tutorials/)
  — official plugin creation tutorials.
- [JUCE licensing](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)
  — review the applicable JUCE licensing terms before distributing a plugin.

## Research notes

- Keep hardware-specific I/O and host-specific services out of shared DSP code
  where practical.
- Use Aurora SDK examples and existing firmware as the source of truth for
  module controls, scaling, and I/O behavior.
- Record versions or commit references when a specific firmware or SDK behavior
  becomes important to implementation or testing.
- When useful, add locally authored summaries or notes here and link them to
  their upstream source; avoid duplicating upstream documentation unnecessarily.
