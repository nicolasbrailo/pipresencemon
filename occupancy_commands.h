#pragma once

#include <stdbool.h>

struct Config;
struct OccupancyCommands;

struct OccupancyCommands* occupancy_commands_init(const struct Config* cfg);
void occupancy_commands_free(struct OccupancyCommands* self);
void occupancy_commands_on_occupancy(struct OccupancyCommands* self);
void occupancy_commands_on_vacancy(struct OccupancyCommands* self);

