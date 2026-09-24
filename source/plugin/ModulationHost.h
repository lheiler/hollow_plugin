#pragma once

namespace hl
{
/** What the UI needs to show and edit modulation on a knob. Implemented by the processor. */
class ModulationHost
{
public:
    virtual ~ModulationHost() = default;

    /** Current modulation offset (normalised) applied to a destination. */
    virtual float getModulationOffset (int destination) const = 0;

    /** True if any matrix slot targets the destination with a non-zero amount. */
    virtual bool isModulated (int destination) const = 0;

    /** Routes `source` to `destination` in the first free matrix slot. */
    virtual void addModulation (int destination, int source) = 0;

    /** Removes every matrix slot targeting the destination. */
    virtual void clearModulation (int destination) = 0;
};

} // namespace hl
