#pragma once

#include "plugin/Presets.h"

namespace hl::presets
{
/** The expansion pack: presets shipped as importable .hollowpreset files (presets/ in the repository),
    not built into the plugin. HollowSnapshot --write-pack renders, checks and writes them. */
const std::vector<Preset>& pack();

} // namespace hl::presets
