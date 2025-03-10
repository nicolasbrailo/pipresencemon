#pragma once

#include <stdbool.h>

struct PiPresenceMonConfig;
struct OccupancyCommands;

struct OccupancyCommands *occupancy_commands_init(const struct PiPresenceMonConfig *cfg);
void occupancy_commands_free(struct OccupancyCommands *self);

// Call when occupancy is detected
void occupancy_commands_on_occupancy(struct OccupancyCommands *self);

// Call when vacancy detected
void occupancy_commands_on_vacancy(struct OccupancyCommands *self);

// Call periodically; used to respawn crashed commands
void occupancy_commands_tick(struct OccupancyCommands *self);
