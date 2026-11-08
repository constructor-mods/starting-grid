// SPDX-License-Identifier: MIT
// Starting Grid: makes the game's own Starting Grid setting work in an offline Grand Prix.
// The database file shows the setting in the Grand Prix menu. This file follows what the player picks:
// Realistic keeps the game's usual grid (roughly in performance order), Reversed turns it round, Random shuffles it,
// and Custom Grid fills the game's Grid Editor and puts the cars in the order set there.
// Include mixin.hpp first: registry.hpp redefines SWOFTY_ADDON so the DLL exports SwoftyAddonRegister.
#include <swofty/mixin.hpp>
#include <swofty/mem.hpp>

#include <swofty/db.hpp>
#include <swofty/events.hpp>
#include <swofty/registry.hpp>

#include "entrants.hpp"
#include "grid_editor.hpp"
#include "grid_order.hpp"
#include "grid_table.hpp"
#include "menu_option.hpp"

#include <cstring>
#include <mutex>
#include <set>
#include <span>

using namespace swofty;
using namespace starting_grid;

namespace {

constexpr double kMenuCheckSeconds = 0.5;

std::mutex g_mutex;
StartingGrid g_choice = StartingGrid::unknown;
double g_since_menu_check = 0;
uint8_t g_last_seen[kSlots];
bool g_have_last = false;

const char *name_of(StartingGrid choice)
{
    switch (choice) {
    case StartingGrid::qualifying:
        return "qualifying";
    case StartingGrid::realistic:
        return "realistic";
    case StartingGrid::reversed:
        return "reversed";
    case StartingGrid::random:
        return "random";
    case StartingGrid::custom:
        return "custom";
    default:
        return "unknown";
    }
}

void follow_menu(StartingGrid choice)
{
    log("[starting-grid] Starting Grid: %s", name_of(choice));
}

std::set<uint64_t> g_known_saves;
bool g_have_saves = false;

// Follows which Grand Prix save is in use, and forgets the grids of saves that are gone.
void follow_saves()
{
    uint64_t save = 0;
    std::set<uint64_t> saves;
    if (!read_saves(save, saves)) {
        return;
    }
    if (save != g_save_id) {
        log("[starting-grid] Grand Prix save in use: %016llx (%zu saves)", static_cast<unsigned long long>(save),
            saves.size());
    }
    g_save_id = save;
    // The game can write a save again under a new id: the save in use is gone and a new one took its place.
    uint64_t previous = g_lineup_save;
    if (save && previous && save != previous && !saves.count(previous) && !has_grid(save)) {
        move_grid(previous, save);
        log("[starting-grid] Grand Prix save %016llx is now %016llx: its grid moves with it",
            static_cast<unsigned long long>(previous), static_cast<unsigned long long>(save));
    }
    if (!g_have_saves || saves != g_known_saves) {
        g_known_saves = saves;
        g_have_saves = true;
        forget_missing(saves);
    }
    follow_save(false);
}

// The menu shows the game's default again: puts back the Starting Grid kept for this save, if it has one.
StartingGrid restore_choice(StartingGrid shown)
{
    if (g_new_grand_prix || !g_lineup_save) {
        return shown;
    }
    StartingGrid kept = saved_grid(g_lineup_save).choice;
    if (kept == StartingGrid::unknown || kept == StartingGrid::qualifying || kept == shown) {
        return shown;
    }
    bool restored = set_starting_grid(kept);
    log("[starting-grid] Starting Grid back to %s for this Grand Prix save%s", name_of(kept),
        restored ? "" : " (failed)");
    if (restored) {
        return kept;
    }
    return shown;
}

// The Starting Grid for this race: the one kept with the Grand Prix save, else the last one picked in the menu
// (during a race the menu is gone and reads as Qualifying).
StartingGrid race_choice()
{
    if (!g_new_grand_prix && g_lineup_save) {
        StartingGrid kept = saved_grid(g_lineup_save).choice;
        if (kept != StartingGrid::unknown && kept != StartingGrid::qualifying) {
            return kept;
        }
    }
    if (g_last_choice >= 0) {
        return StartingGrid(g_last_choice.load());
    }
    return g_choice;
}

// Reversed, Random and Custom Grid: changes a race grid. True when it did.
bool change_grid(uint8_t *grid)
{
    if (grid_count(grid) < 2 || !is_race(session_number())) {
        return false;
    }
    // A new Grand Prix's race is here: its save exists now, so what was set for it is kept with that save.
    follow_save(true);
    StartingGrid choice = race_choice();
    std::string before = grid_text(grid);
    if (choice == StartingGrid::reversed) {
        // The game's usual grid is roughly in performance order: turned round, the slowest cars start at the front.
        reverse_grid(grid);
    } else if (choice == StartingGrid::random) {
        shuffle_grid(grid);
    } else if (choice == StartingGrid::custom) {
        if (!apply_lineup(grid, grid_count(grid))) {
            log("[starting-grid] custom grid: no lineup set in the Grid Editor, grid left as %s", before.c_str());
            return false;
        }
    } else {
        log("[starting-grid] %s grid: left as %s", name_of(choice), before.c_str());
        return false;
    }
    log("[starting-grid] %s grid: %s (was %s)", name_of(choice), grid_text(grid).c_str(), before.c_str());
    return true;
}

// Changes a new race grid as soon as the game writes it, before the cars are placed.
void check_grid()
{
    uint8_t *grid = grid_table();
    if (!grid || !mem::readable(grid, kSlots)) {
        return;
    }
    if (g_have_last && std::memcmp(grid, g_last_seen, kSlots) == 0) {
        return;
    }
    change_grid(grid);
    std::memcpy(g_last_seen, grid, kSlots);
    g_have_last = true;
}

void frame(double seconds, void *)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    // The game's database and systems live for the whole session: checking their memory once a minute is enough.
    swofty::mem::Trust trust(60000);
    g_since_menu_check += seconds;
    if (g_since_menu_check >= kMenuCheckSeconds && db::available()) {
        g_since_menu_check = 0;
        follow_saves();
        StartingGrid choice = read_starting_grid();
        if (choice != StartingGrid::unknown && choice != StartingGrid::qualifying && g_menu_reset_wanted) {
            // A new Grand Prix starts on the game's default, whatever the last one used.
            g_menu_reset_wanted = false;
            g_restore_wanted = false;
            if (choice != StartingGrid::realistic && set_starting_grid(StartingGrid::realistic)) {
                choice = StartingGrid::realistic;
                log("[starting-grid] new Grand Prix: Starting Grid back to realistic");
            }
            g_last_choice = int(choice);
        } else if (choice == StartingGrid::qualifying) {
            // The menu is being built again (it comes back on the game's default): put the save's choice back then.
            g_restore_wanted = true;
        } else if (choice != StartingGrid::unknown && g_restore_wanted) {
            g_restore_wanted = false;
            choice = restore_choice(choice);
            g_last_choice = int(choice);
        } else if (choice != StartingGrid::unknown && choice != g_choice) {
            g_last_choice = int(choice);
            if (!g_new_grand_prix) {
                save_choice(g_lineup_save, choice);
            }
        }
        if (choice != StartingGrid::unknown && choice != g_choice) {
            g_choice = choice;
            g_editor_wanted = choice == StartingGrid::custom;
            if (g_editor_wanted) {
                load_name_keys(); // ready before the Grid Editor opens, so it opens without a pause
            }
            follow_menu(choice);
        }
    }
    check_grid();
}

} // namespace

SWOFTY_ON_LOAD
{
    catch_placement_system();
    catch_grid_editor();
    catch_grand_prix_screen();
    swofty::events::on_frame(frame);
}

SWOFTY_REGISTER
{
}

SWOFTY_ADDON()
