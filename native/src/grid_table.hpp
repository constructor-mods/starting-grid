// SPDX-License-Identifier: MIT
// The game's starting grid: a table of 32 slots, each holding the car that starts there (or none).
// It lives in the system that places the cars on the track. We catch that system when the game sets it up for a
// session, then watch its table every frame: when the game fills it in, we change it before the cars are placed.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace starting_grid {

using swofty::log;

constexpr uintptr_t kPlacementVtableInitSlot = 0x57fa5d0; // where the system's setup function is listed
constexpr uintptr_t kPlacementInit = 0x2f61640;          // that setup function on this game build
constexpr size_t kGridTable = 0x988;
constexpr int kSlots = 32;
constexpr uint8_t kEmpty = 0xff;

using PlacementInit = uint64_t (*)(void *system, void *first, void *second, void *third);

inline PlacementInit g_original_init = nullptr;
inline std::atomic<uint8_t *> g_placement{nullptr};

inline uint64_t placement_init(void *system, void *first, void *second, void *third)
{
    uint64_t result = g_original_init(system, first, second, third);
    g_placement = static_cast<uint8_t *>(system);
    return result;
}

// Puts our function in the game's list for that system. Does nothing on a game build it doesn't know.
inline bool catch_placement_system()
{
    uintptr_t base = swofty::api->module_base();
    auto *entry = reinterpret_cast<void **>(base + kPlacementVtableInitSlot);
    void *expected = reinterpret_cast<void *>(base + kPlacementInit);
    if (*entry != expected) {
        log("[starting-grid] this game build is not supported: the grid can't be changed");
        return false;
    }
    g_original_init = reinterpret_cast<PlacementInit>(*entry);
    DWORD old_protect = 0;
    if (!VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &old_protect)) {
        log("[starting-grid] can't change the game's function list");
        return false;
    }
    *entry = reinterpret_cast<void *>(&placement_init);
    VirtualProtect(entry, sizeof(void *), old_protect, &old_protect);
    return true;
}

constexpr uintptr_t kPlacementVtable = 0x57fa5c0;

// The grid table, or nullptr before the game has set it up. The game keeps the same system from race to race, so it
// is kept too, as long as it still is that system.
inline uint8_t *grid_table()
{
    uint8_t *system = g_placement;
    uintptr_t vtable = 0;
    if (!system || !swofty::mem::read(reinterpret_cast<uintptr_t>(system), vtable) ||
        vtable != swofty::api->module_base() + kPlacementVtable) {
        return nullptr;
    }
    return system + kGridTable;
}

// How many cars the grid holds: the slots before the first empty one.
inline int grid_count(const uint8_t *grid)
{
    int count = 0;
    while (count < kSlots && grid[count] != kEmpty) {
        count++;
    }
    return count;
}

} // namespace starting_grid
