// SPDX-License-Identifier: MIT
// The Grid Editor's rows: naming the drivers, redrawing the rows, reading and shuffling the order.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "drivers.hpp"
#include "lineup.hpp"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace starting_grid {

using swofty::log;

constexpr uintptr_t kEditorPlayers = 0x668;    // the lobby's player slots the rows take names from
constexpr uintptr_t kPlayerSlots = 0x60;
constexpr uintptr_t kPlayerSlotSize = 0xa8;
constexpr int kPlayerSlotCount = 70;
constexpr uintptr_t kEntries = 0x678;          // the editor's own order, one entry per row
constexpr uintptr_t kEntriesEnd = 0x680;
constexpr uintptr_t kEntrySize = 0x58;
constexpr uintptr_t kEntryAllocator = 0x670;
constexpr uintptr_t kEntryCapacity = 0x688;
constexpr uintptr_t kEntryCount = 0x764;
constexpr uintptr_t kEntryVtable = 0x5612e20;
constexpr uintptr_t kEntryStart = 0x50;          // an entry's starting position, from 1
constexpr uintptr_t kBuildRows = 0x221a9b0;      // the game's row builder for the editor
constexpr uintptr_t kRecordName = 0x50;         // in a player slot: the name string
constexpr uintptr_t kRecordNameIsKey = 0x58;   // in a player slot: the name is a translation key
constexpr int kMaxDrivers = 32;

struct GridDriver {
    int driver = -1;       // the game's driver number (stat_id)
    std::string name_key;  // the translation key of the full name
};

using MakeText = uint32_t (*)(void **out, const char *text, uint32_t flags, uintptr_t length);
using BuildRows = void (*)(void *editor);

// Gives each driver a spare lobby player slot holding their identity and name, so the editor's rows show the name.
inline void name_drivers(uint8_t *editor, const std::vector<GridDriver> &drivers)
{
    static auto make_text = reinterpret_cast<MakeText>(swofty::api->resolve("Ne_Make_StrFromCStr"));
    uintptr_t players = 0;
    if (!make_text || !swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEditorPlayers), players) || !players) {
        log("[starting-grid] grid editor: no player slots, the rows stay unnamed");
        return;
    }
    int slot = 0;
    for (const GridDriver &driver : drivers) {
        if (slot >= kPlayerSlotCount) {
            break;
        }
        uint8_t *record = reinterpret_cast<uint8_t *>(players + kPlayerSlots + uintptr_t(slot) * kPlayerSlotSize);
        uint32_t identity = 0;
        swofty::mem::read(reinterpret_cast<uintptr_t>(record + kIdentity), identity);
        if (identity & kHumanBit) {
            slot++;
            continue;
        }
        void *text = nullptr;
        make_text(&text, driver.name_key.c_str(), 0, 0);
        *reinterpret_cast<uint32_t *>(record + kIdentity) = identity_of(driver.driver);
        *reinterpret_cast<void **>(record + kRecordName) = text;
        record[kRecordNameIsKey] = 1;
        slot++;
    }
}

// Names the player's row: a spare lobby slot gets a copy of the player's identity and their driver's name.
inline void name_player(uint8_t *editor, uintptr_t player_entry, int slot)
{
    static auto make_text = reinterpret_cast<MakeText>(swofty::api->resolve("Ne_Make_StrFromCStr"));
    uintptr_t players = 0;
    int driver = player_driver();
    std::string key;
    if (!make_text || driver < 0 || !driver_name_key(driver, key) || slot >= kPlayerSlotCount ||
        !swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEditorPlayers), players) || !players) {
        return;
    }
    uint8_t *record = reinterpret_cast<uint8_t *>(players + kPlayerSlots + uintptr_t(slot) * kPlayerSlotSize);
    std::memcpy(record + 8, reinterpret_cast<void *>(player_entry + 8), kIdentity + 4 - 8);
    void *text = nullptr;
    make_text(&text, key.c_str(), 0, 0);
    *reinterpret_cast<void **>(record + kRecordName) = text;
    record[kRecordNameIsKey] = 1;
}

// Reads the editor's current order into the lineup.
inline void remember_lineup(uint8_t *editor)
{
    uintptr_t begin = 0, end = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEntries), begin) ||
        !swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEntriesEnd), end) || end < begin ||
        (end - begin) / kEntrySize > 64) {
        return;
    }
    std::vector<int> lineup;
    for (uintptr_t entry = begin; entry < end; entry += kEntrySize) {
        uint32_t identity = 0;
        swofty::mem::read(entry + kIdentity, identity);
        if (identity & kHumanBit) {
            lineup.push_back(-1);
        } else if ((identity & 0xff000000) == kComputerTag) {
            lineup.push_back(int(identity & 0xffff) - 1);
        }
    }
    std::lock_guard<std::mutex> lock(g_editor_mutex);
    g_lineup = lineup;
    save_lineup(g_lineup_save, lineup);
}

// Whether the editor's list already holds our drivers.
inline bool has_our_drivers(uint8_t *editor)
{
    uintptr_t begin = 0, end = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEntries), begin) ||
        !swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEntriesEnd), end)) {
        return true;
    }
    for (uintptr_t entry = begin; entry < end; entry += kEntrySize) {
        uint32_t identity = 0;
        swofty::mem::read(entry + kIdentity, identity);
        if ((identity & 0xff000000) == kComputerTag && (identity & 0xffff) != 0) {
            return true;
        }
    }
    return false;
}

// The editor only numbers a row, and lets it move, when the lobby has a member with that row's identity. Offline the
// lobby has only the player, so while the game redraws the rows it gets a member list with a stand-in for every driver.
constexpr uintptr_t kEditorLobby = 0x650;
constexpr uintptr_t kLobbyMembers = 0x100;
constexpr uintptr_t kMembersBegin = 0x58;
constexpr uintptr_t kMembersEnd = 0x60;
constexpr uintptr_t kMemberIdentity = 0x58;
constexpr int kMaxMembers = 64;

inline uint8_t g_stand_ins[kMaxMembers][0x80];
inline uintptr_t g_member_list[kMaxMembers];

inline void rebuild_rows(uint8_t *editor)
{
    uintptr_t base = swofty::api->module_base();
    uintptr_t lobby = 0, members = 0, begin = 0, end = 0, entries = 0, entries_end = 0;
    bool swap = swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEditorLobby), lobby) && lobby &&
                swofty::mem::read(lobby + kLobbyMembers, members) && members &&
                swofty::mem::read(members + kMembersBegin, begin) && swofty::mem::read(members + kMembersEnd, end) &&
                end >= begin && (end - begin) / 8 < 8 &&
                swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEntries), entries) &&
                swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEntriesEnd), entries_end);
    if (swap) {
        int count = 0;
        for (uintptr_t member = begin; member < end && count < kMaxMembers; member += 8) {
            g_member_list[count] = *reinterpret_cast<uintptr_t *>(member);
            count++;
        }
        int stand_in = 0;
        for (uintptr_t entry = entries; entry < entries_end && count < kMaxMembers; entry += kEntrySize) {
            uint32_t identity = *reinterpret_cast<uint32_t *>(entry + kIdentity);
            if ((identity & 0xff000000) != kComputerTag) {
                continue;
            }
            std::memset(g_stand_ins[stand_in], 0, sizeof g_stand_ins[stand_in]);
            *reinterpret_cast<uint32_t *>(g_stand_ins[stand_in] + kMemberIdentity) = identity;
            g_member_list[count] = reinterpret_cast<uintptr_t>(g_stand_ins[stand_in]);
            stand_in++;
            count++;
        }
        *reinterpret_cast<uintptr_t *>(members + kMembersBegin) = reinterpret_cast<uintptr_t>(g_member_list);
        *reinterpret_cast<uintptr_t *>(members + kMembersEnd) = reinterpret_cast<uintptr_t>(g_member_list + count);
    }
    reinterpret_cast<BuildRows>(base + kBuildRows)(editor);
    if (swap) {
        *reinterpret_cast<uintptr_t *>(members + kMembersBegin) = begin;
        *reinterpret_cast<uintptr_t *>(members + kMembersEnd) = end;
    }
}

// Shuffles the editor's list for Randomise Grid; each entry keeps its starting position for the "previous" column.
inline void shuffle_entries(uint8_t *editor)
{
    uintptr_t begin = 0, end = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEntries), begin) ||
        !swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEntriesEnd), end) || end <= begin ||
        (end - begin) / kEntrySize > 64) {
        return;
    }
    size_t count = (end - begin) / kEntrySize;
    std::mt19937 random{uint32_t(GetTickCount64())};
    for (size_t i = count - 1; i > 0; i--) {
        size_t other = size_t(random() % (i + 1));
        if (i == other) {
            continue;
        }
        uint8_t keep[kEntrySize];
        auto *first = reinterpret_cast<uint8_t *>(begin + i * kEntrySize);
        auto *second = reinterpret_cast<uint8_t *>(begin + other * kEntrySize);
        std::memcpy(keep, first, kEntrySize);
        std::memcpy(first, second, kEntrySize);
        std::memcpy(second, keep, kEntrySize);
    }
}

} // namespace starting_grid
