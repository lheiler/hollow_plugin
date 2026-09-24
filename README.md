# Hollow

A creative distortion and texture plugin (VST3 + standalone) in the spirit of iZotope Trash 2, built with JUCE 9 and
the same toolchain and design language as [Supervisor](../supervisor).

Eight effect modules sit in a chain you can **reorder by dragging**. Two LFOs, an envelope follower and a macro feed
a modulation matrix. That combination is the point: fuzz *after* a cistern reverb, chorus *into* a wavefolder, an
envelope opening a vowel filter in front of a bitcrusher.

## Modules

| Module | What it does |
|---|---|
| **Trash** | 26 distortion algorithms in 7 families (saturate, clip, fuzz, fold, rectify, digital, harmonic). Up to 3 bands (phase-aligned Linkwitz-Riley crossovers), each blending two algorithms (A/B morph) with drive, bias, tone, mix and level. 4x linear-phase oversampling. Auto gain is calibrated by measurement per algorithm and drive, so drive changes the character rather than just the level. |
| **Filter 1 / Filter 2** | LP/HP/BP 12 and 24 dB, notch, comb +/-, vowel formants. The resonance saturates like an analog filter (it screams but stays bounded), with drive, mix and per-sample cutoff glide for fast modulation. |
| **Convolve** | 20 *synthesised* impulses: speaker cabinets, radio, telephone, megaphone, torn cone, tin can, metal bowl, steel pipe, glass jar, cardboard box, gong, spring tank, steel plate, closet, tiled room, tunnel, concrete hall, cistern. Because they're modelled, **Size** genuinely rescales the object, **Damp** darkens the tail over time and **Reverse** flips it. Zero-latency partitioned convolution; impulses are built on a worker thread and crossfaded in. |
| **Motion** | Chorus, flanger, phaser, vibrato, tremolo (feedback squares it into a chop, spread turns it into auto-pan), ring mod, and a Hilbert frequency shifter (feedback gives barber-pole swirls). Tempo sync. |
| **Degrade** | Tape wow and flutter, age (bandwidth loss + saturation), hiss, vinyl crackle, random dropouts, and **glitch**: a tempo-synced sampler that randomly stutters, reverses and half-speeds slices on a 16th-note grid. |
| **Dynamics** | Gate into a stereo-linked soft-knee compressor with parallel mix: pull a distortion's noise floor up into sustain, or chop it into gated textures. |
| **Echo** | Tape echo: time changes glide (pitch bends), drive and tone inside the loop degrade every repeat, wobble, ping-pong, tempo sync. Feedback goes to 120%: it runs away and self-oscillates, but the saturating loop keeps it bounded. |

**Modulation**: LFO 1 and 2 (sine, triangle, ramps, square, sample & hold, smooth random; free or host-synced and
phase-locked to the song position), an envelope follower and a macro knob. The 8-slot matrix reaches ~40
destinations. Right-click any knob to route a source to it. Modulated knobs show a live orange ring, and every
display (filter curve, transfer curve, morph pad) follows the modulation.

**Modulation sources**: LFO 1 and 2, the envelope follower and two macro knobs (Macro 1 / Macro 2).

**Presets and dice**: 27 factory presets in five groups (Drive, Lo-Fi, Motion, Space, Chaos), several with unusual chain
orders. **Save** stores the current sound in `Documents/Hollow/Presets` (`.hollowpreset`, plain XML); saved presets appear
under "Saved" at the end of the preset list and the arrows step through both. **Randomize** rolls a new chain:
modules, order, algorithms and modulation routes, within ranges that stay musical.

**Presets on disk**: `.hollowpreset` files (plain XML) in `Documents/Hollow/Presets` by default, the same kind of place
FabFilter, Serum, Vital or u-he use on Windows. The folder can be changed in the menu. **Import...** (or dropping files
onto the window) copies presets in without ever overwriting: a clash gets a " (2)" suffix and identical copies are
skipped. The preset list re-reads the folder each time it opens.

**Undo / redo**: Ctrl+Z / Ctrl+Y (Cmd on macOS) or the arrows in the header. Each knob drag, preset load, dice roll,
import or reorder is one step (up to 100); host automation doesn't create steps.

**Menu**: Auto Level and Clip Guard; oversampling for playback (1x/2x/4x/8x) and for rendering (same, or 8x);
interface size (80-150%, true zoom) and tooltips; what the dice may change (chain order, modulation); the preset
folder (import, open, change, default, delete); version/sample rate/latency. Interface settings are shared by all
Hollow windows on the machine. Bypass, Auto Level, Clip Guard and oversampling are settings that presets and the
dice leave alone.

**Oversampling**: 1x skips it (the distortion aliases: harder, metallic), 8x is the cleanest. The latency is the same
at every setting (68 samples, the 8x round trip; lower factors are padded), so switching, or rendering at 8x, never
shifts the audio or disturbs the host's delay compensation.

The sample rate comes from the host (`prepareToPlay`) whenever it changes, and everything is rebuilt for it
immediately; nothing needs to poll.

**Auto Level** (on by default) keeps every patch, preset or dice roll, at roughly the loudness of what goes in. It never
listens to the audio: each module estimates from its settings how it changes a typical music signal (the median
spectrum of 7,992 released tracks, at about -18 LUFS), a 1/3-octave spectrum is passed through the chain in order
(filters, tone controls, the echo loop and the loaded impulse shape it band by band; distortion keeps a measured
share of its input and moves the rest into harmonics; the steady part of the modulation counts), and the result is
compensated at the output. It is known before anything plays, and it's recomputed only when a setting changes.
So the **Input** knob is pure drive (more grit, same loudness), and **Output** is a trim on top.

On 480 renders of real songs through random patches (`tools/EvalLevel.cpp`), without Auto Level the median patch is
6.6 dB off the input loudness (90th percentile 18.9 dB); with it the median error is 1.5 dB and 94% land within 6 dB.
The rest are mostly very narrow resonances whose loudness depends on what a particular song has at that frequency.
Because it's a guess from settings, much hotter or quieter input than -18 LUFS lands a few dB off in the direction of
the distortion's compression.

**Clip Guard** (on by default) is a soft ceiling at the very end: untouched below -3 dBFS, never above -0.3 dBFS.
Push the Output knob into it if you want output clipping.

Latency is constant (68 samples, ~1.4 ms at 48 kHz) whether or not modules are enabled and whatever the oversampling,
and it is reported to the host.
Global mix and bypass are latency-aligned.

## Install (Windows)

Copy the whole `Hollow.vst3` folder to `C:\Program Files\Common Files\VST3\` (needs admin), or add the folder containing
it to your DAW's VST3 search paths, then rescan plugins. `Hollow.exe` is a standalone version for quick tests.

## Building

Everything is cross-compiled from Linux/WSL, with the same toolchain as Supervisor (no Visual Studio needed).

```bash
scripts/setup-toolchain.sh   # once: CMake, Ninja, LLVM (clang-cl), MSVC CRT + Windows SDK via xwin, JUCE, pluginval
scripts/test-dsp.sh          # native unit tests of the DSP core (g++, no JUCE)
scripts/build-windows.sh     # -> dist/Hollow.vst3, dist/Hollow.exe, dist/HollowSnapshot.exe
scripts/test-windows.sh      # harness + pluginval on Windows via WSL interop, screenshots to build/screenshots
```

`HollowSnapshot.exe <dir> --renders` also writes a 6-second WAV of the test song through every preset.

Auto Level accuracy on real music (needs the Free Music Archive `fma_small` set and the list Supervisor's corpus tools
write):

```bash
g++ -std=c++20 -O2 -Isource -Itools tools/EvalLevel.cpp -o build/tools/EvalLevel -pthread
build/tools/EvalLevel ../supervisor/build/tools/fma_list.csv 48 10 -18 --trace   # tracks, patches per track, input LUFS
```

## macOS

Build on a Mac (Apple's toolchain can't legally or practically be used from Linux/Windows):

```bash
xcode-select --install && brew install cmake   # once
scripts/build-mac.sh                           # universal VST3 + AU + app -> dist/mac, installed to ~/Library/Audio/Plug-Ins
```

The build is universal (Apple Silicon and Intel, macOS 11+), ad-hoc signed so it runs on the Mac that built it, and the
AU is checked with `auval`. To give it to other Macs it has to be signed with a Developer ID certificate (Apple
Developer Program) and notarized; otherwise Gatekeeper blocks it, and the only way around is removing the quarantine
flag (`xattr -dr com.apple.quarantine Hollow.component`). Without a Mac, a GitHub Actions macOS runner can run the
same script.

## Layout

```
source/dsp/     JUCE-free DSP core: shapers, trash, filters, motion, degrade, impulses + convolver, dynamics, echo,
                modulation, chain
source/plugin/  AudioProcessor, parameters, presets, editor
source/gui/     look & feel (Supervisor's), chain strip, module panels, modulation strip, meters
tests/          DSP unit tests (shaper bounds, auto gain, oversampled latency, filter curves vs. audio, frequency
                shifter image rejection, convolution vs. direct convolution, runaway echo bounds, chain latency...)
tools/          HollowSnapshot: headless preset renders, sample rates, automation stress, dice, state, bypass, screenshots;
                EvalLevel: Auto Level estimate vs. measured loudness on real songs
resources/      fonts: Oxanium (interface) and Martian Mono (readouts), SIL Open Font License 1.1
```

## Ideas for later

- User preset saving/browsing, and A/B compare
- Custom drawable waveshaper and loading your own impulse responses
- More modulators (step sequencer, MIDI note/velocity), per-band modulation targets
- Parallel routing (split the chain into two lanes)

## Licensing

JUCE 9 is dual-licensed (AGPLv3 or commercial JUCE licence). Distributing closed-source binaries requires a JUCE
licence. The embedded fonts are under the SIL Open Font License 1.1 (`resources/fonts/OFL-*.txt`), which allows
bundling them in commercial software; keep the licence texts with any distribution. The plugin/manufacturer codes and company name in `CMakeLists.txt` (`Lhei` / `Hlw1`, "lheiler") are
placeholders.
