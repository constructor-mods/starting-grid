// SPDX-License-Identifier: MIT
// Whether the session being set up is a race.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <cstdint>

namespace starting_grid {

constexpr uintptr_t kManagerSettings = 0x1a8;
constexpr uintptr_t kSettingsSession = 0x4;

// The game's session number: 1 to 4 practice, 5 to 14 qualifying, 15 to 17 race. 0 when it can't be read.
inline uint32_t session_number()
{
    uintptr_t slot = swofty::api->resolve("g_Game_Manager");
    uintptr_t manager = 0;
    uintptr_t settings = 0;
    uint32_t session = 0;
    if (!slot || !swofty::mem::read(slot, manager) || !manager) {
        return 0;
    }
    if (!swofty::mem::read(manager + kManagerSettings, settings) || !settings) {
        return 0;
    }
    if (!swofty::mem::read(settings + kSettingsSession, session)) {
        return 0;
    }
    return session;
}

inline bool is_race(uint32_t session)
{
    return session >= 15 && session <= 17;
}

} // namespace starting_grid
