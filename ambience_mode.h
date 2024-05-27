#pragma once

struct AmbienceModeCfg;
struct AmbienceModeCfg *ambience_mode_init(void *fcfg);
void ambience_mode_free(struct AmbienceModeCfg *cfg);

void ambience_mode_enter();
void ambience_mode_leave();
