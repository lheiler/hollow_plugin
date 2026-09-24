# How it works

The parts of Hollow that aren't obvious from the interface, and how they were checked.

## Signal flow and latency

The input gain, then the eight modules in the order of the chain strip, then Auto Level and the output trim, the global
dry/wet mix (against the untouched, latency-aligned input) and finally the clip guard. A module that is switched off
crossfades out over 20 ms and is skipped.

The only latency is the distortion's oversampler: **68 samples**, whatever the sample rate, the oversampling factor or
which modules are on. It is reported to the host, and the dry paths (bypass, global mix, a switched-off distortion)
are delayed by the same amount, so nothing ever smears.

## Trash

- **Oversampling**: cascaded linear-phase half-band filters (Kaiser-windowed, about 90 dB rejection) for 2x, 4x and 8x.
  Every factor reports the 8x round trip; lower factors are padded with a plain delay, so switching never moves the
  audio. 1x runs the shapers at the host rate and lets them alias on purpose.
- **Bands**: 4th-order Linkwitz-Riley crossovers with all-pass compensation, so untouched bands sum back flat.
- **Algorithms**: 26 memoryless shapers (plus a sample-and-hold decimator). Each band blends two of them sample by
  sample; the blend keeps polarity, so inverted shapes (Fuzz against Cheby 3) partly cancel, as they really do.
- **Auto gain** inside the module is measured per algorithm and drive with a -12 dBFS sine, so the drive knob changes
  the character rather than the level.
- **Parallel mix**: the clean part follows the distorted part's loudness downwards (never upwards, slowly enough to keep
  transients), so driving a band harder makes it dirtier instead of letting the clean signal take over.

## Filters

A per-sample topology-preserving state-variable filter (after Andrew Simper) whose band-pass integrator saturates, so
high resonance squashes like an analog filter instead of running away. Cutoff and resonance glide per sample, which
keeps fast modulation free of zipper noise. Combs have a one-pole damper in the loop; the vowel filter runs three
formant band-passes between A-E-I-O-U. The response drawn on screen is the exact response of the audio path (checked in
the unit tests within 1 dB).

## Convolve

The impulses are synthesised: speaker cabinets from filter chains and cone reflections, objects from modal resonators,
the spring from a dispersive all-pass chain over repeating echoes, rooms from early reflections and a decaying noise
tail. So *Size* rescales resonances and decay times, *Damp* closes a low-pass over the tail and *Reverse* flips it.
All impulses are normalised to the same loudness for pink noise.

The convolver has no latency: the first 128 taps run as a direct filter, the next 1,920 in 128-sample FFT partitions and
the rest in 2,048-sample partitions, each stage starting exactly one block into the impulse. New impulses are built on
a worker thread and crossfaded in over 50 ms; in the tests the result matches direct convolution to within 0.01 %.

## Motion, Degrade, Echo

- The frequency shifter uses a pair of all-pass chains (Olli Niemitalo's 90-degree network) to build an analytic
  signal; the unwanted sideband is more than 35 dB down.
- Degrade's tape transport is a modulated delay (wow: slow sine plus drift; flutter: fast sine plus jitter). The glitch
  engine keeps 2.5 s of audio and, on each 16th note of the host tempo, may repeat, reverse or half-speed a slice.
- The echo's delay time is smoothed, so changing it bends the pitch like tape. Every repeat passes the tone filter and
  a saturator; with feedback above 100 % the loop self-oscillates, and `tanh` keeps it bounded.

## Auto Level

Auto Level has to be right the moment a patch is loaded, even before anything plays, so it never measures the audio.
Instead each module *estimates* what it does to typical music:

1. "Typical music" is a 1/3-octave spectrum: the median long-term spectrum of 7,992 released tracks (the Free Music
   Archive `fma_small` set, measured by Supervisor's analysis tools), at about -18 LUFS.
2. That spectrum is passed through the chain in its current order. Filters, tone controls, the echo loop (a geometric
   series per band) and the loaded impulse (its measured band response, including how its phase adds to the dry) shape
   it band by band. The distortion keeps a measured share of its input's shape and moves the rest into harmonics one,
   1.6 and 2.3 octaves up; the level change and that share come from tables measured per algorithm and input level.
   The compressor uses its static curve.
3. Steady modulation counts too: the envelope at its typical value, the macros at their knobs, LFO sweeps averaged over
   their centre and extremes.
4. The result is read like a loudness meter would (BS.1770 K-weighting) and compensated at the output, gliding over
   about 40 ms. It is recomputed only when a setting changes.

**Accuracy** (`tools/EvalLevel.cpp`: 48 real songs x 10 random patches, input normalised to -18 LUFS):

| | median | 90 % of patches within | within 6 dB |
|---|---|---|---|
| without Auto Level (how far patches move the loudness) | 6.6 dB | 18.9 dB | |
| with Auto Level (remaining error) | 1.5 dB | 4.9 dB | 94 % |

The remaining misses are mostly very narrow resonances whose loudness depends on what a particular song has at that
frequency, which can't be known without listening. Much hotter or quieter input than about -18 LUFS lands a few dB off
in the direction of the distortion's compression; turning the Input knob towards that range improves the estimate.

## Presets and undo

Presets are small XML files (`.hollowpreset`) holding every sound parameter and the chain order. Bypass, Auto Level,
Clip Guard and oversampling are settings, not sound, so presets and the dice leave them alone. Import copies files into
the preset folder without overwriting (a clash becomes " (2)", an identical file isn't copied twice).

Undo keeps whole snapshots (every parameter, the order, the preset name) and records one after each finished gesture,
preset, dice roll or reorder. Host automation doesn't create steps.

## How it's tested

- **DSP unit tests** (`scripts/test-dsp.sh`, 135 checks, native): shaper bounds and auto gain, oversampler alignment at
  every factor and aliasing at 1x against 8x, filter curves against the audio, frequency shifter image rejection,
  convolution against direct convolution, runaway echo bounds, dynamics, degrade, chain latency and bypass, and every
  Auto Level estimate against measured loudness.
- **Plugin harness** (`tools/Snapshot.cpp`, run on Windows by `scripts/test-windows.sh`): every factory preset,
  40 dice rolls, 44.1 to 192 kHz, parameter automation every block, state save and load, user presets and import,
  oversampling and render mode, undo and redo, bypass and mono, and screenshots.
- **pluginval** at strictness 10.
- **Auto Level on real music**: `tools/EvalLevel.cpp` (needs the `fma_small` set):

  ```bash
  g++ -std=c++20 -O2 -Isource -Itools tools/EvalLevel.cpp -o build/tools/EvalLevel -pthread
  build/tools/EvalLevel <fma_list.csv> 48 10 -18 --trace   # tracks, patches per track, input LUFS, stage-by-stage trace
  ```
