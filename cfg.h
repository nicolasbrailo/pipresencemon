#pragma once

#include <stdbool.h>
#include <stddef.h>

void *cfg_init(const char *fpath);
bool cfg_read_size_t(const void *handle, const char *key_name, size_t *val);
bool cfg_read_str(const void *handle, const char *key_name, char **val);
void cfg_free(void *handle);
