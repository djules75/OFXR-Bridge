#pragma once
#include <cstdint>

namespace DlssNr
{
struct NrRetirementCadence
{
    std::uint64_t calls = 0, waits = 0, skipped = 0, pending = 0;
    std::uint64_t totalMs = 0, maximumMs = 0;

    void Observe(std::uint64_t elapsedMs, bool admitted, bool waited, bool unsubmitted)
    {
        ++calls;
        waits += waited ? 1 : 0;
        skipped += admitted ? 0 : 1;
        pending += unsubmitted ? 1 : 0;
        totalMs += elapsedMs;
        if (elapsedMs > maximumMs) maximumMs = elapsedMs;
    }
};
}
