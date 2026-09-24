#pragma once

#include "Common.h"

namespace hl::dsp
{
/** Output safety: untouched below -3 dBFS, then a smooth knee that never exceeds -0.3 dBFS. */
struct ClipGuard
{
    static constexpr float knee = 0.70794578f;     // -3 dBFS
    static constexpr float ceiling = 0.96605088f;  // -0.3 dBFS

    static float process (float x) noexcept
    {
        const float a = std::abs (x);

        if (a <= knee)
            return x;

        constexpr float range = ceiling - knee;
        const float y = knee + range * fastTanh ((a - knee) / range);
        return x < 0.0f ? -y : y;
    }
};

} // namespace hl::dsp
