// SPDX-License-Identifier: MIT
// The Grand Prix's drivers: who is in the session, their cars, and their names from the game's driver table.
#pragma once
#include <swofty/db.hpp>
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace starting_grid {

using swofty::log;

constexpr uintptr_t kIdentity = 0x48;          // who a row or player slot belongs to
constexpr uint32_t kHumanBit = 0x10000000;
constexpr uint32_t kComputerTag = 0x20000000;

inline uint32_t identity_of(int driver)
{
    return kComputerTag | uint32_t(driver + 1);
}

inline std::mutex g_names_mutex;
inline std::map<int, std::string> g_name_keys; // driver number -> full name key, read once from the driver table

// Reads every driver's full name key in one pass over the game's driver table. Done once, before the editor opens.
inline void load_name_keys()
{
    std::lock_guard<std::mutex> lock(g_names_mutex);
    if (!g_name_keys.empty()) {
        return;
    }
    swofty::db::Cursor cursor{};
    if (swofty::db::open("entrants.drivers", &cursor) != swofty::db::Result::ok) {
        return;
    }
    for (int guard = 0; guard < 4000; guard++) {
        swofty::db::Value id{};
        swofty::db::Value name{};
        if (swofty::db::cell(&cursor, "stat_id", &id) == swofty::db::Result::ok &&
            swofty::db::cell(&cursor, "full_name", &name) == swofty::db::Result::ok && name.s) {
            g_name_keys[int(id.i)] = name.s;
        }
        if (swofty::db::step(&cursor) != swofty::db::Result::ok) {
            break;
        }
    }
}

inline bool driver_name_key(int driver, std::string &key)
{
    load_name_keys();
    std::lock_guard<std::mutex> lock(g_names_mutex);
    auto found = g_name_keys.find(driver);
    if (found == g_name_keys.end()) {
        return false;
    }
    key = found->second;
    return true;
}

// The session's entrants, when the game has them: driver numbers of the computer's drivers.
inline std::vector<int> session_drivers()
{
    std::vector<int> drivers;
    uintptr_t slot = swofty::api->resolve("g_Game_Manager");
    uintptr_t manager = 0, context = 0, holder = 0, setup = 0, begin = 0, end = 0;
    if (!slot || !swofty::mem::read(slot, manager) || !manager || !swofty::mem::read(manager + 0xc0, context) ||
        !context || !swofty::mem::read(context + 0x88, holder) || !holder ||
        !swofty::mem::read(holder + 0x260, setup) || !setup || !swofty::mem::read(setup + 0x2e0, begin) ||
        !swofty::mem::read(setup + 0x2e8, end) || end <= begin || (end - begin) % 0x88 != 0 ||
        (end - begin) / 0x88 > 64) {
        return drivers;
    }
    for (uintptr_t entrant = begin; entrant < end; entrant += 0x88) {
        uint16_t driver = 0;
        uint32_t identity = 0;
        swofty::mem::read(entrant + 0x52, driver);
        swofty::mem::read(entrant + 0x48, identity);
        if (!(identity & kHumanBit)) {
            drivers.push_back(driver);
        }
    }
    return drivers;
}

// The car of each entrant in this session: by driver number, and the player's (-1).
inline bool find_car(int driver, uint8_t &car)
{
    uintptr_t slot = swofty::api->resolve("g_Game_Manager");
    uintptr_t manager = 0, context = 0, holder = 0, setup = 0, begin = 0, end = 0;
    if (!slot || !swofty::mem::read(slot, manager) || !manager || !swofty::mem::read(manager + 0xc0, context) ||
        !context || !swofty::mem::read(context + 0x88, holder) || !holder ||
        !swofty::mem::read(holder + 0x260, setup) || !setup || !swofty::mem::read(setup + 0x2e0, begin) ||
        !swofty::mem::read(setup + 0x2e8, end) || end <= begin || (end - begin) / 0x88 > 64) {
        return false;
    }
    for (uintptr_t entrant = begin; entrant < end; entrant += 0x88) {
        uint16_t entrant_driver = 0;
        uint32_t identity = 0;
        swofty::mem::read(entrant + 0x52, entrant_driver);
        swofty::mem::read(entrant + 0x48, identity);
        bool human = (identity & kHumanBit) != 0;
        if ((driver == -1 && human) || (driver >= 0 && !human && entrant_driver == driver)) {
            return swofty::mem::read(entrant + 0x82, car) && car != 0xff;
        }
    }
    return false;
}

// The 2025 grid from the game's database, when the session has no entrants yet.
inline std::vector<int> database_drivers()
{
    std::vector<int> drivers;
    for (int i = 0; i < 26; i++) {
        std::string key = "entrants.entrant_lists.entrant_list_15.entrant_" + std::to_string(i) + ".entrant_reference";
        swofty::db::Value reference{};
        if (swofty::db::get(key, &reference) != swofty::db::Result::ok || !reference.s || !*reference.s) {
            break;
        }
        swofty::db::Cursor cursor{};
        if (swofty::db::row("entrants.drivers", reference.s, &cursor) != swofty::db::Result::ok) {
            continue;
        }
        swofty::db::Value id{};
        if (swofty::db::cell(&cursor, "stat_id", &id) == swofty::db::Result::ok) {
            drivers.push_back(int(id.i));
        }
    }
    return drivers;
}

// The player's driver number in this session (the person's entrant), or -1.
inline int player_driver()
{
    uintptr_t slot = swofty::api->resolve("g_Game_Manager");
    uintptr_t manager = 0, context = 0, holder = 0, setup = 0, begin = 0, end = 0;
    if (!slot || !swofty::mem::read(slot, manager) || !manager || !swofty::mem::read(manager + 0xc0, context) ||
        !context || !swofty::mem::read(context + 0x88, holder) || !holder ||
        !swofty::mem::read(holder + 0x260, setup) || !setup || !swofty::mem::read(setup + 0x2e0, begin) ||
        !swofty::mem::read(setup + 0x2e8, end) || end <= begin || (end - begin) / 0x88 > 64) {
        return -1;
    }
    for (uintptr_t entrant = begin; entrant < end; entrant += 0x88) {
        uint16_t driver = 0;
        uint32_t identity = 0;
        swofty::mem::read(entrant + 0x52, driver);
        swofty::mem::read(entrant + 0x48, identity);
        if (identity & kHumanBit) {
            return driver;
        }
    }
    return -1;
}

} // namespace starting_grid
