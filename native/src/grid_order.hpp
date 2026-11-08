// SPDX-License-Identifier: MIT
// Turns the starting grid round, or shuffles it.
#pragma once
#include "grid_table.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace starting_grid {

inline void shuffle_grid(uint8_t *grid)
{
    int count = grid_count(grid);
    if (count < 2) {
        return;
    }
    std::vector<uint8_t> cars(grid, grid + count);
    std::mt19937 random(uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::shuffle(cars.begin(), cars.end(), random);
    std::copy(cars.begin(), cars.end(), grid);
}

inline void reverse_grid(uint8_t *grid)
{
    int count = grid_count(grid);
    std::reverse(grid, grid + count);
}

inline std::string grid_text(const uint8_t *grid)
{
    std::string text;
    int count = grid_count(grid);
    for (int i = 0; i < count; i++) {
        text += std::to_string(grid[i]);
        if (i + 1 < count) {
            text += " ";
        }
    }
    return text;
}

} // namespace starting_grid
