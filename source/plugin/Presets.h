#pragma once

#include "dsp/Chain.h"

#include <juce_core/juce_core.h>

namespace hl::presets
{
struct Preset
{
    const char* name;
    const char* category;
    std::vector<std::pair<juce::String, float>> values; // plain values on top of the defaults
    std::vector<int> order;                              // leading module ids; the rest follow in default order
};

const std::vector<Preset>& all();

/** Expands a partial module order into a full, valid one. */
dsp::ModuleOrder completeOrder (const std::vector<int>& leading);

} // namespace hl::presets
