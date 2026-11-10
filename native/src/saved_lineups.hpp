// SPDX-License-Identifier: MIT
// Keeps each Grand Prix save's Starting Grid and custom lineup, like the game keeps the save itself: continuing the
// save brings them back, a new Grand Prix starts fresh, and when the save is gone (deleted or finished) they go too.
// They live in the addon's own folder, one line per save: the save's id, the Starting Grid, then the drivers in order.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>
#include <swofty/storage.hpp>

#include "menu_option.hpp"

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

// The mode's save slot list.
constexpr uintptr_t kModeObject = 0xf08;
constexpr uintptr_t kModeChildren = 0x238;
constexpr uintptr_t kModeChildrenEnd = 0x240;
constexpr uintptr_t kSaveSlotsVtable = 0x57b6060;
constexpr uintptr_t kSlotsBegin = 0x268;
constexpr uintptr_t kSlotsEnd = 0x270;
constexpr uintptr_t kCurrentSlot = 0x280;
constexpr uintptr_t kSlotGuid = 0x8;           // a save's unique id
constexpr uintptr_t kSlotMode = 0xa8;
constexpr uintptr_t kSlotHasGame = 0xd2;
constexpr uint8_t kNoMode = 255;               // a save that never held a game
constexpr const char *kLineupsFile = "lineups.txt";

struct SavedGrid {
    StartingGrid choice = StartingGrid::unknown;
    std::vector<int> lineup;
};

inline std::atomic<uintptr_t> g_save_slots{0};  // the Grand Prix save slot list, found when its screen is used
inline std::atomic<bool> g_new_grand_prix{false}; // "Start new Grand Prix" pressed, its save not made yet
inline std::atomic<uint64_t> g_save_before_new{0};
inline std::set<uint64_t> g_saves_before_new;     // the saves there were when a new Grand Prix began

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

// True when the save in use is an empty one, made for a new Grand Prix that has not been set up yet.
inline bool current_save_is_empty()
{
    uintptr_t slots = g_save_slots;
    uintptr_t slot = 0;
    uint8_t mode = 0;
    uint8_t has_game = 1;
    if (!is_save_slots(slots) || !swofty::mem::read(slots + kCurrentSlot, slot) || !slot) {
        return false;
    }
    if (!swofty::mem::read(slot + kSlotMode, mode) || !swofty::mem::read(slot + kSlotHasGame, has_game)) {
        return false;
    }
    return mode == kNoMode && !has_game;
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

} // namespace starting_grid
