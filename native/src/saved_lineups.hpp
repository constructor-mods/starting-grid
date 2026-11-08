// SPDX-License-Identifier: MIT
// Keeps each Grand Prix save's Starting Grid and custom lineup, like the game keeps the save itself: continuing the
// save brings them back, a new Grand Prix starts fresh, and when the save is gone (deleted or finished) they go too.
// They live in the addon's own folder, one line per save: the save's id, the Starting Grid, then the drivers in order.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

#include "menu_option.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace starting_grid {

// The Grand Prix "Continue / Start new" screen.
constexpr uintptr_t kContinueVtable = 0x5739fa0;
constexpr int kPressSlot = 24;                 // a card pressed: continue a save, or start a new Grand Prix
constexpr uintptr_t kPressFunction = 0x1bce150;
constexpr int kFindModeSlot = 31;              // the Grand Prix mode's entity
constexpr uintptr_t kCards = 0x328;
constexpr uintptr_t kCardSize = 0x540;
constexpr uintptr_t kCardSlotId = 0x8;         // the save shown on a card; the card's first byte is 0 when it has none

// The mode's save slot list.
constexpr uintptr_t kModeObject = 0xf08;
constexpr uintptr_t kModeChildren = 0x238;
constexpr uintptr_t kModeChildrenEnd = 0x240;
constexpr uintptr_t kSaveSlotsVtable = 0x57b6060;
constexpr uintptr_t kSlotsBegin = 0x268;
constexpr uintptr_t kSlotsEnd = 0x270;
constexpr uintptr_t kCurrentSlot = 0x280;
constexpr uintptr_t kSlotGuid = 0x8;           // a save's unique id
constexpr const char *kLineupsFile = "lineups.txt";

struct SavedGrid {
    StartingGrid choice = StartingGrid::unknown;
    std::vector<int> lineup;
};

inline std::atomic<uintptr_t> g_save_slots{0};  // the Grand Prix save slot list, found when its screen is used
inline std::atomic<bool> g_new_grand_prix{false}; // "Start new Grand Prix" pressed, its save not made yet
inline std::atomic<uint64_t> g_save_before_new{0};

inline bool is_save_slots(uintptr_t object)
{
    uintptr_t vtable = 0;
    return object && swofty::mem::read(object, vtable) && vtable == swofty::api->module_base() + kSaveSlotsVtable;
}

// The save slot list of a mode entity: the mode object itself, or one of its parts.
inline uintptr_t find_save_slots(uintptr_t entity)
{
    uintptr_t mode = 0;
    if (!entity || !swofty::mem::read(entity + kModeObject, mode) || !mode) {
        return 0;
    }
    if (is_save_slots(mode)) {
        return mode;
    }
    uintptr_t begin = 0, end = 0;
    if (!swofty::mem::read(mode + kModeChildren, begin) || !swofty::mem::read(mode + kModeChildrenEnd, end) ||
        end < begin || (end - begin) / 8 > 256) {
        return 0;
    }
    for (uintptr_t at = begin; at < end; at += 8) {
        uintptr_t child = 0;
        if (swofty::mem::read(at, child) && is_save_slots(child)) {
            return child;
        }
    }
    return 0;
}

// Reads which Grand Prix save is in use and which exist. False until the list is known.
inline bool read_saves(uint64_t &current, std::set<uint64_t> &all)
{
    uintptr_t slots = g_save_slots;
    current = 0;
    all.clear();
    if (!is_save_slots(slots)) {
        return false;
    }
    uintptr_t begin = 0, end = 0, slot = 0;
    if (!swofty::mem::read(slots + kSlotsBegin, begin) || !swofty::mem::read(slots + kSlotsEnd, end) || end < begin ||
        (end - begin) / 8 > 64) {
        return false;
    }
    for (uintptr_t at = begin; at < end; at += 8) {
        uint64_t id = 0;
        if (swofty::mem::read(at, slot) && slot && swofty::mem::read(slot + kSlotGuid, id) && id) {
            all.insert(id);
        }
    }
    if (swofty::mem::read(slots + kCurrentSlot, slot) && slot) {
        swofty::mem::read(slot + kSlotGuid, current);
    }
    return true;
}

inline std::map<uint64_t, SavedGrid> load_grids()
{
    std::map<uint64_t, SavedGrid> grids;
    std::string text;
    if (swofty::storage::read(kLineupsFile, text) != swofty::storage::Result::ok) {
        return grids;
    }
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        size_t first = line.find(':');
        size_t second = line.find(':', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            continue;
        }
        uint64_t id = std::strtoull(line.substr(0, first).c_str(), nullptr, 16);
        SavedGrid grid;
        int choice = std::atoi(line.substr(first + 1, second - first - 1).c_str());
        if (choice >= 0 && choice <= 4) {
            grid.choice = StartingGrid(choice);
        }
        std::istringstream drivers(line.substr(second + 1));
        std::string driver;
        while (std::getline(drivers, driver, ',')) {
            if (!driver.empty()) {
                grid.lineup.push_back(std::atoi(driver.c_str()));
            }
        }
        if (id) {
            grids[id] = grid;
        }
    }
    return grids;
}

inline void store_grids(const std::map<uint64_t, SavedGrid> &grids)
{
    std::string text;
    char id[24];
    for (const auto &[save, grid] : grids) {
        std::snprintf(id, sizeof id, "%016llx:", static_cast<unsigned long long>(save));
        text += id;
        text += std::to_string(int(grid.choice)) + ":";
        for (size_t i = 0; i < grid.lineup.size(); i++) {
            text += std::to_string(grid.lineup[i]);
            if (i + 1 < grid.lineup.size()) {
                text += ",";
            }
        }
        text += "\n";
    }
    swofty::storage::write(kLineupsFile, text);
}

// What is kept for a save: nothing (unknown choice, empty lineup) when it has none.
inline SavedGrid saved_grid(uint64_t save)
{
    auto grids = load_grids();
    auto found = grids.find(save);
    if (found == grids.end()) {
        return {};
    }
    return found->second;
}

inline bool has_grid(uint64_t save)
{
    return load_grids().count(save) != 0;
}

// Files what was kept for one save under another id.
inline void move_grid(uint64_t from, uint64_t to)
{
    auto grids = load_grids();
    auto found = grids.find(from);
    if (found == grids.end() || !to) {
        return;
    }
    grids[to] = found->second;
    grids.erase(from);
    store_grids(grids);
}

inline void save_lineup(uint64_t save, const std::vector<int> &lineup)
{
    if (!save) {
        return;
    }
    auto grids = load_grids();
    grids[save].lineup = lineup;
    store_grids(grids);
}

inline void save_choice(uint64_t save, StartingGrid choice)
{
    if (!save || choice == StartingGrid::unknown || choice == StartingGrid::qualifying) {
        return;
    }
    auto grids = load_grids();
    if (grids[save].choice == choice) {
        return;
    }
    grids[save].choice = choice;
    store_grids(grids);
}

inline void save_grid(uint64_t save, const SavedGrid &grid)
{
    if (!save) {
        return;
    }
    auto grids = load_grids();
    grids[save] = grid;
    store_grids(grids);
}

// Drops what was kept for saves that no longer exist (deleted, or finished and removed by the game).
inline void forget_missing(const std::set<uint64_t> &existing)
{
    auto grids = load_grids();
    bool changed = false;
    for (auto at = grids.begin(); at != grids.end();) {
        if (!existing.count(at->first)) {
            swofty::log("[starting-grid] Grand Prix save %016llx is gone: its grid is forgotten",
                        static_cast<unsigned long long>(at->first));
            at = grids.erase(at);
            changed = true;
        } else {
            ++at;
        }
    }
    if (changed) {
        store_grids(grids);
    }
}

using PressCard = uint64_t (*)(void *screen, uint64_t card, void *first, void *second);
using FindMode = uintptr_t (*)(void *screen);
inline PressCard g_original_press = nullptr;

// A card on the Grand Prix screen pressed: finds the save list, and notes a new Grand Prix (it starts with no lineup).
inline uint64_t press_card(void *screen, uint64_t card, void *first, void *second)
{
    uintptr_t vtable = 0;
    uintptr_t find_mode = 0;
    if (swofty::mem::read(reinterpret_cast<uintptr_t>(screen), vtable) &&
        swofty::mem::read(vtable + 8 * uintptr_t(kFindModeSlot), find_mode) && find_mode) {
        uintptr_t slots = find_save_slots(reinterpret_cast<FindMode>(find_mode)(screen));
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

inline void catch_grand_prix_screen()
{
    uintptr_t base = swofty::api->module_base();
    auto *entry = reinterpret_cast<void **>(base + kContinueVtable + 8 * uintptr_t(kPressSlot));
    if (*entry != reinterpret_cast<void *>(base + kPressFunction)) {
        swofty::log("[starting-grid] Grand Prix screen: this game build is not supported");
        return;
    }
    g_original_press = reinterpret_cast<PressCard>(*entry);
    DWORD old_protect = 0;
    if (!VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &old_protect)) {
        return;
    }
    *entry = reinterpret_cast<void *>(&press_card);
    VirtualProtect(entry, sizeof(void *), old_protect, &old_protect);
}

} // namespace starting_grid
