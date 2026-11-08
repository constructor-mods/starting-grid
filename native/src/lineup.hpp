// SPDX-License-Identifier: MIT
// The custom lineup set in the Grid Editor, which Grand Prix save it belongs to, and putting the cars in its order.
#pragma once
#include <swofty/mixin.hpp>

#include "drivers.hpp"
#include "menu_option.hpp"
#include "saved_lineups.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace starting_grid {

using swofty::log;

inline std::mutex g_editor_mutex;
inline bool g_editor_wanted = false;     // set while the menu says Custom Grid
inline std::vector<int> g_lineup;        // the order set in the editor: driver numbers, -1 for the player
inline std::vector<int> g_start_order;   // the game's own order, for the previous position column
inline uint64_t g_lineup_save = 0;       // the Grand Prix save the lineup belongs to, 0 when not known
inline bool g_fresh = false;             // the lineup was emptied for a new Grand Prix
inline std::atomic<uint64_t> g_save_id{0};             // the Grand Prix save in use, 0 when none is known
inline std::atomic<int> g_last_choice{-1};             // the last Starting Grid the player picked
inline std::atomic<bool> g_restore_wanted{true};       // put the save's Starting Grid back when the menu shows it
inline std::atomic<bool> g_menu_reset_wanted{false};   // a new Grand Prix: put Starting Grid on the game's default

// Makes the lineup the one kept for the Grand Prix save in use: a new Grand Prix starts empty, and keeps what was set
// once the game has made its save (at the latest when its race starts); a continued one gets its own back.
inline void follow_save(bool race_started)
{
    uint64_t save = g_save_id;
    std::lock_guard<std::mutex> lock(g_editor_mutex);
    if (g_new_grand_prix) {
        if (save && (save != g_save_before_new || race_started)) {
            g_new_grand_prix = false;
            g_fresh = false;
            g_lineup_save = save;
            SavedGrid grid;
            grid.choice = StartingGrid(g_last_choice.load());
            grid.lineup = g_lineup;
            save_grid(save, grid);
            log("[starting-grid] new Grand Prix save %016llx: its grid is kept (%zu drivers in the lineup)",
                static_cast<unsigned long long>(save), g_lineup.size());
        } else if (!g_fresh) {
            g_lineup.clear();
            g_start_order.clear();
            g_lineup_save = 0;
            g_fresh = true;
            g_menu_reset_wanted = true;
            log("[starting-grid] new Grand Prix: the Grid Editor starts empty");
        }
        return;
    }
    g_fresh = false;
    if (!save || save == g_lineup_save) {
        return;
    }
    SavedGrid grid = saved_grid(save);
    g_lineup = grid.lineup;
    g_lineup_save = save;
    g_start_order.clear();
    g_restore_wanted = true;
    log("[starting-grid] Grand Prix save %016llx: %zu drivers in its custom lineup",
        static_cast<unsigned long long>(save), g_lineup.size());
}

// Puts the cars in the order set in the Grid Editor; cars the editor didn't list keep their order at the back.
inline bool apply_lineup(uint8_t *grid, int count)
{
    follow_save(true);
    std::vector<int> lineup;
    {
        std::lock_guard<std::mutex> lock(g_editor_mutex);
        lineup = g_lineup;
    }
    if (lineup.empty()) {
        return false;
    }
    std::vector<uint8_t> ordered;
    for (int driver : lineup) {
        uint8_t car = 0xff;
        if (!find_car(driver, car)) {
            continue;
        }
        bool on_grid = false;
        for (int i = 0; i < count; i++) {
            if (grid[i] == car) {
                on_grid = true;
            }
        }
        bool already = false;
        for (uint8_t placed : ordered) {
            if (placed == car) {
                already = true;
            }
        }
        if (on_grid && !already) {
            ordered.push_back(car);
        }
    }
    for (int i = 0; i < count; i++) {
        bool placed = false;
        for (uint8_t car : ordered) {
            if (car == grid[i]) {
                placed = true;
            }
        }
        if (!placed) {
            ordered.push_back(grid[i]);
        }
    }
    std::memcpy(grid, ordered.data(), size_t(count));
    return true;
}

} // namespace starting_grid
