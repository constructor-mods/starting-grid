// SPDX-License-Identifier: MIT
// The Grand Prix "Continue / Start new" screen: finds the Grand Prix save list when the screen asks for its mode, and
// notes a new Grand Prix when its card is pressed.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "saved_lineups.hpp"

#include <windows.h>

#include <cstdint>
#include <set>

namespace starting_grid {

constexpr uintptr_t kContinueVtable = 0x5739fa0;
constexpr int kPressSlot = 24;                 // a card pressed: continue a save, or start a new Grand Prix
constexpr uintptr_t kPressFunction = 0x1bce150;
constexpr int kFindModeSlot = 31;              // the Grand Prix mode's entity
constexpr uintptr_t kFindModeFunction = 0x1bccc00;
constexpr uintptr_t kCards = 0x328;
constexpr uintptr_t kCardSize = 0x540;

using PressCard = uint64_t (*)(void *screen, uint64_t card, void *first, void *second);
using FindMode = uintptr_t (*)(void *screen);
inline PressCard g_original_press = nullptr;
inline FindMode g_original_find_mode = nullptr;

// The screen asks for the Grand Prix mode (the game and other addons do): its save list is kept from it.
inline uintptr_t find_mode(void *screen)
{
    uintptr_t entity = g_original_find_mode(screen);
    uintptr_t slots = find_save_slots(entity);
    if (slots) {
        g_save_slots = slots;
    }
    return entity;
}

// A card on the Grand Prix screen pressed: notes a new Grand Prix (it starts with no lineup).
inline uint64_t press_card(void *screen, uint64_t card, void *first, void *second)
{
    uintptr_t vtable = 0;
    uintptr_t find = 0;
    if (swofty::mem::read(reinterpret_cast<uintptr_t>(screen), vtable) &&
        swofty::mem::read(vtable + 8 * uintptr_t(kFindModeSlot), find) && find) {
        uintptr_t slots = find_save_slots(reinterpret_cast<FindMode>(find)(screen));
        if (slots) {
            g_save_slots = slots;
        }
    }
    uint8_t has_save = 0;
    auto *card_data = static_cast<uint8_t *>(screen) + kCards + uintptr_t(card & 0xff) * kCardSize;
    swofty::mem::read(reinterpret_cast<uintptr_t>(card_data), has_save);
    if (has_save) {
        g_new_grand_prix = false;
    } else {
        uint64_t current = 0;
        std::set<uint64_t> all;
        read_saves(current, all);
        g_save_before_new = current;
        g_new_grand_prix = true;
    }
    swofty::log("[starting-grid] Grand Prix screen: %s (save list %s)", has_save ? "continue" : "start new",
                g_save_slots.load() ? "found" : "not found");
    return g_original_press(screen, card, first, second);
}

inline bool swap_screen_entry(int slot, uintptr_t stock, void *replacement, void **original)
{
    uintptr_t base = swofty::api->module_base();
    auto *entry = reinterpret_cast<void **>(base + kContinueVtable + 8 * uintptr_t(slot));
    if (*entry != reinterpret_cast<void *>(base + stock)) {
        return false;
    }
    DWORD old_protect = 0;
    if (!VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &old_protect)) {
        return false;
    }
    *original = *entry;
    *entry = replacement;
    VirtualProtect(entry, sizeof(void *), old_protect, &old_protect);
    return true;
}

inline void catch_grand_prix_screen()
{
    void *original = nullptr;
    bool find_ok = swap_screen_entry(kFindModeSlot, kFindModeFunction, reinterpret_cast<void *>(&find_mode), &original);
    if (find_ok) {
        g_original_find_mode = reinterpret_cast<FindMode>(original);
    }
    bool press_ok = swap_screen_entry(kPressSlot, kPressFunction, reinterpret_cast<void *>(&press_card), &original);
    if (press_ok) {
        g_original_press = reinterpret_cast<PressCard>(original);
    }
    if (!find_ok || !press_ok) {
        swofty::log("[starting-grid] Grand Prix screen: this game build is not supported");
    }
}

} // namespace starting_grid
