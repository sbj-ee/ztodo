#pragma once

#include <sodium.h>

namespace ztodo {

// libsodium must be initialised once before use (minisign verification).
inline bool ensureSodium()
{
    static const bool ok = sodium_init() >= 0;
    return ok;
}

} // namespace ztodo
