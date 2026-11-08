// SPDX-License-Identifier: MIT
// Fills the game's Grid Editor (made for online lobbies) with the Grand Prix's drivers, and remembers the order the
// player sets. Offline the editor only knows the player, so when it opens we hand it a list of every driver, and give
// each one a name in the spare player slots the editor reads names from. Moving, randomising and showing the rows are
// all the game's own code.
#pragma once
#include <swofty/mem.hpp>
#include <swofty/mixin.hpp>

#include "drivers.hpp"
#include "editor_rows.hpp"
#include "lineup.hpp"
#include "menu_option.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace starting_grid {

constexpr uintptr_t kEditorVtable = 0x5775780;
constexpr int kOpenSlot = 19;
constexpr uintptr_t kOpenFunction = 0x2218f20;
constexpr int kIsBoundSlot = 4;
constexpr uintptr_t kIsBoundFunction = 0x4321a70;
constexpr int kCommandSlot = 10;
constexpr uintptr_t kCommandFunction = 0x221dd70;

using EditorOpen = uint64_t (*)(void *editor, void *first, void *second, void *third);
using EditorCommand = uint64_t (*)(void *editor, void *command, void *second, void *third);

inline EditorOpen g_original_open = nullptr;
inline EditorCommand g_original_command = nullptr;

inline std::atomic<uint8_t *> g_editor{nullptr};

inline uint64_t editor_open(void *self, void *first, void *second, void *third)
{
    // Opening the Grid Editor means the player wants a custom grid: switch Starting Grid to it.
    if (!g_editor_wanted) {
        bool switched = set_starting_grid(StartingGrid::custom);
        log("[starting-grid] grid editor opened: Starting Grid switched to Custom Grid%s", switched ? "" : " (failed)");
        if (switched) {
            g_editor_wanted = true;
            load_name_keys();
        }
    }
    g_editor = static_cast<uint8_t *>(self);
    return g_original_open(self, first, second, third);
}

using Allocate = void *(*)(void *allocator, size_t size, size_t align);
using Release = void (*)(void *allocator, void *memory);
using BuildRows = void (*)(void *editor);

// Gives the editor a new list: the player's entry first (if it has one), then every computer driver, made with the
// editor's own allocator so the game frees it as usual. Then names the drivers and redraws the rows.
inline void fill_editor(uint8_t *editor)
{
    uintptr_t base = swofty::api->module_base();
    uintptr_t vtable = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(editor), vtable) || vtable != base + kEditorVtable) {
        return;
    }
    if (has_our_drivers(editor)) {
        return;
    }
    follow_save(false);
    std::vector<int> numbers = session_drivers();
    const char *source = "session";
    if (numbers.empty()) {
        numbers = database_drivers();
        source = "2025 database list";
    }
    if (numbers.empty() || numbers.size() > size_t(kMaxDrivers)) {
        return;
    }
    std::vector<GridDriver> drivers;
    for (int number : numbers) {
        GridDriver driver;
        driver.driver = number;
        if (!driver_name_key(number, driver.name_key)) {
            driver.name_key = "lng_dash";
        }
        drivers.push_back(driver);
    }
    uintptr_t allocator = 0, begin = 0, end = 0, allocator_vtable = 0;
    if (!swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEntryAllocator), allocator) || !allocator ||
        !swofty::mem::read(allocator, allocator_vtable) ||
        !swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEntries), begin) ||
        !swofty::mem::read(reinterpret_cast<uintptr_t>(editor + kEntriesEnd), end) || end < begin) {
        return;
    }
    auto allocate = *reinterpret_cast<Allocate *>(allocator_vtable + 0x18);
    auto release = *reinterpret_cast<Release *>(allocator_vtable + 0x38);
    // Wait until the game has put the player's own entry in the list, so the player keeps their place.
    if (end == begin) {
        return;
    }
    size_t player_entries = 1;
    size_t count = player_entries + drivers.size();
    auto *list = static_cast<uint8_t *>(allocate(reinterpret_cast<void *>(allocator), count * kEntrySize, 8));
    if (!list) {
        return;
    }
    // The order: the one set last time in the editor (it keeps it when reopened), then anyone new at the back.
    std::vector<int> order;
    std::vector<int> all;
    if (player_entries) {
        all.push_back(-1);
    }
    for (const GridDriver &driver : drivers) {
        all.push_back(driver.driver);
    }
    // The game's own order (the player, then the drivers as the session lists them), kept from the first fill.
    if (g_start_order.size() != all.size() || !std::is_permutation(all.begin(), all.end(), g_start_order.begin())) {
        g_start_order = all;
    }
    {
        std::lock_guard<std::mutex> lock(g_editor_mutex);
        for (int driver : g_lineup) {
            if (std::find(all.begin(), all.end(), driver) != all.end() &&
                std::find(order.begin(), order.end(), driver) == order.end()) {
                order.push_back(driver);
            }
        }
    }
    for (int driver : all) {
        if (std::find(order.begin(), order.end(), driver) == order.end()) {
            order.push_back(driver);
        }
    }
    std::memset(list, 0, count * kEntrySize);
    uintptr_t player_entry = 0;
    for (size_t i = 0; i < count; i++) {
        uint8_t *entry = list + i * kEntrySize;
        if (order[i] == -1) {
            std::memcpy(entry, reinterpret_cast<void *>(begin), kEntrySize);
            player_entry = reinterpret_cast<uintptr_t>(entry);
        } else {
            *reinterpret_cast<uintptr_t *>(entry) = base + kEntryVtable;
            *reinterpret_cast<uint32_t *>(entry + kIdentity) = identity_of(order[i]);
        }
        // The previous position shows where the driver was in the game's own order, so the arrows show the change.
        auto start = std::find(g_start_order.begin(), g_start_order.end(), order[i]);
        uint32_t start_position = uint32_t(i + 1);
        if (start != g_start_order.end()) {
            start_position = uint32_t(start - g_start_order.begin()) + 1;
        }
        *reinterpret_cast<uint32_t *>(entry + kEntryStart) = start_position;
    }
    name_drivers(editor, drivers);
    if (player_entry) {
        name_player(editor, player_entry, int(drivers.size()));
    }
    *reinterpret_cast<uintptr_t *>(editor + kEntries) = reinterpret_cast<uintptr_t>(list);
    *reinterpret_cast<uintptr_t *>(editor + kEntriesEnd) = reinterpret_cast<uintptr_t>(list) + count * kEntrySize;
    *reinterpret_cast<uint64_t *>(editor + kEntryCapacity) = count;
    *reinterpret_cast<int32_t *>(editor + kEntryCount) = int32_t(count);
    if (begin) {
        release(reinterpret_cast<void *>(allocator), reinterpret_cast<void *>(begin));
    }
    rebuild_rows(editor);
    remember_lineup(editor);
    log("[starting-grid] grid editor filled: %zu rows (%zu drivers from the %s)", count, drivers.size(), source);
}

using EditorIsBound = uint64_t (*)(void *editor, void *first, void *second, void *third);
inline EditorIsBound g_original_is_bound = nullptr;

// The UI asks the editor this while it shows it: the right moment to fill the editor once the game has built its list.
inline uint64_t editor_is_bound(void *self, void *first, void *second, void *third)
{
    uint64_t result = g_original_is_bound(self, first, second, third);
    if (g_editor_wanted && self == g_editor.load()) {
        fill_editor(static_cast<uint8_t *>(self));
    }
    return result;
}

constexpr uint32_t kRandomiseCommand = 0x6a505d6c; // the Randomise Grid button

inline uint64_t editor_command(void *self, void *command, void *second, void *third)
{
    uint64_t result = g_original_command(self, command, second, third);
    uint32_t hash = 0;
    uintptr_t text = 0;
    if (command && swofty::mem::read(reinterpret_cast<uintptr_t>(command), text) && text) {
        swofty::mem::read(text, hash);
    }
    auto *editor = static_cast<uint8_t *>(self);
    if (g_editor_wanted && hash == kRandomiseCommand) {
        fill_editor(editor);
        shuffle_entries(editor);
    }
    if (g_editor_wanted && has_our_drivers(editor)) {
        rebuild_rows(editor);
        remember_lineup(editor);
    }
    return result;
}

inline bool swap_slot(int slot, uintptr_t expected_rva, void *replacement, void **original)
{
    uintptr_t base = swofty::api->module_base();
    auto *entry = reinterpret_cast<void **>(base + kEditorVtable + 8 * uintptr_t(slot));
    if (*entry != reinterpret_cast<void *>(base + expected_rva)) {
        log("[starting-grid] grid editor: this game build is not supported (slot %d)", slot);
        return false;
    }
    *original = *entry;
    DWORD old_protect = 0;
    if (!VirtualProtect(entry, sizeof(void *), PAGE_READWRITE, &old_protect)) {
        return false;
    }
    *entry = replacement;
    VirtualProtect(entry, sizeof(void *), old_protect, &old_protect);
    return true;
}

inline void catch_grid_editor()
{
    void *open = nullptr;
    void *command = nullptr;
    void *is_bound = nullptr;
    if (swap_slot(kOpenSlot, kOpenFunction, reinterpret_cast<void *>(&editor_open), &open)) {
        g_original_open = reinterpret_cast<EditorOpen>(open);
    }
    if (swap_slot(kCommandSlot, kCommandFunction, reinterpret_cast<void *>(&editor_command), &command)) {
        g_original_command = reinterpret_cast<EditorCommand>(command);
    }
    if (swap_slot(kIsBoundSlot, kIsBoundFunction, reinterpret_cast<void *>(&editor_is_bound), &is_bound)) {
        g_original_is_bound = reinterpret_cast<EditorIsBound>(is_bound);
    }
}

} // namespace starting_grid
