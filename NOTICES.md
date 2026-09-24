# Notices

Hollow is Copyright (C) 2026 Lorenz Heiler and is free software: you can redistribute it and/or modify it under the
terms of the GNU Affero General Public License, version 3 (see [LICENSE](LICENSE)). It comes with no warranty.

It builds on, or includes, the following work by others.

## JUCE

Hollow is built with the [JUCE](https://juce.com) framework (version 9), which is used here under the AGPLv3.
JUCE is not part of this repository; the build scripts download it.

## VST3 SDK

The VST3 SDK (part of JUCE) is Copyright (c) 2025 Steinberg Media Technologies GmbH, MIT License.
VST is a registered trademark of Steinberg Media Technologies GmbH.

## Audio Units

Audio Units is a trademark of Apple Inc.

## Fonts

The interface embeds two fonts, both under the SIL Open Font License 1.1 (licence texts in
[`resources/fonts`](resources/fonts)):

- **Oxanium**, Copyright 2019 The Oxanium Project Authors (https://github.com/sevmeyer/oxanium)
- **Martian Mono**, Copyright 2021 The Martian Mono Project Authors (https://github.com/evilmartians/mono)

## minimp3

`tools/third_party/minimp3*.h` (used only by the offline evaluation tool) is dedicated to the public domain (CC0) by
its authors.

## Music spectrum data

`source/dsp/Spectrum.h` contains 61 numbers: the median long-term spectrum of 7,992 tracks from the Free Music Archive
`fma_small` set (Creative Commons licensed music), measured by the author's Supervisor project. Only this aggregate
statistic is included, no audio.

## Other names

iZotope, Trash, FabFilter, Serum, Vital, u-he, Decimort and other product names mentioned in the documentation are
trademarks of their respective owners and are mentioned only for comparison. Hollow is not affiliated with them.
