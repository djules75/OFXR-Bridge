#pragma once
namespace DlssNr
{
// Source reset augments, never clears, a locally latched initialization/skip reset.
inline bool NrResetWithSource(bool pending, bool available, int sourceReset)
{
    return pending || (available && sourceReset != 0);
}
}
