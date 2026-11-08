// SPDX-License-Identifier: MIT
// Reads and sets Starting Grid in the game's own Grand Prix settings menu.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <cstdint>

namespace starting_grid {

namespace mem = swofty::mem;

// The Starting Grid choices, in the menu's order.
enum class StartingGrid { unknown = -1, qualifying = 0, realistic = 1, reversed = 2, random = 3, custom = 4 };

constexpr uintptr_t kOptionsStoreRva = 0x6079258;
constexpr uintptr_t kFindOptionRva = 0x2b5dfd0;
constexpr uintptr_t kOptionMap = 0x270;
constexpr uintptr_t kOptionEntries = 0x288;
constexpr uintptr_t kOptionEntriesEnd = 0x290;
constexpr int kGetValueSlot = 2;
constexpr int kSetValueSlot = 3;

using FindOption = uint64_t (*)(void *map, const char **name);
using GetValue = int (*)(void *option, uint64_t *context);
using SetValue = void (*)(void *option, int value, uint64_t *context);

inline uintptr_t resolve_or(const char *name, uintptr_t rva)
{
    uintptr_t address = swofty::api->resolve(name);
    if (!address) {
        address = swofty::api->module_base() + rva;
    }
    return address;
}

// The game's Starting Grid option, or 0 when it can't be found.
inline uintptr_t starting_grid_option()
{
    static uintptr_t store_slot = resolve_or("g_Game_OptionsStore", kOptionsStoreRva);
    static auto find_option = reinterpret_cast<FindOption>(resolve_or("Game_OptionsStore_FindOption", kFindOptionRva));
    uintptr_t store = 0;
    if (!mem::read(store_slot, store) || !store) {
        return 0;
    }
    const char *name = "starting_grid";
    uint64_t index = find_option(reinterpret_cast<void *>(store + kOptionMap), &name);
    uintptr_t entries = 0;
    uintptr_t end = 0;
    if (!mem::read(store + kOptionEntries, entries) || !mem::read(store + kOptionEntriesEnd, end) || !entries) {
        return 0;
    }
    uintptr_t entry = entries + index * 16;
    uintptr_t option = 0;
    if (entry >= end || !mem::read(entry + 8, option)) {
        return 0;
    }
    return option;
}

inline uintptr_t option_function(uintptr_t option, int slot)
{
    uintptr_t vtable = 0;
    uintptr_t function = 0;
    if (!option || !mem::read(option, vtable) || !mem::read(vtable + 8 * uintptr_t(slot), function)) {
        return 0;
    }
    return function;
}

inline StartingGrid read_starting_grid()
{
    uintptr_t option = starting_grid_option();
    uintptr_t get_value = option_function(option, kGetValueSlot);
    if (!get_value) {
        return StartingGrid::unknown;
    }
    uint64_t context = 0;
    int value = reinterpret_cast<GetValue>(get_value)(reinterpret_cast<void *>(option), &context);
    if (value < 0 || value > 4) {
        return StartingGrid::unknown;
    }
    return StartingGrid(value);
}

// Picks a Starting Grid value in the menu, as if the player had.
inline bool set_starting_grid(StartingGrid choice)
{
    uintptr_t option = starting_grid_option();
    uintptr_t set_value = option_function(option, kSetValueSlot);
    if (!set_value) {
        return false;
    }
    uint64_t context = 0;
    reinterpret_cast<SetValue>(set_value)(reinterpret_cast<void *>(option), int(choice), &context);
    return read_starting_grid() == choice;
}

} // namespace starting_grid
