#pragma once

#include <stdbool.h>
#include <stddef.h>

struct Config;

struct Config *cfg_init(const char *fpath);
void cfg_free(struct Config *h);

bool cfg_get_string_strdup(struct Config *h, const char *k, const char **v);
bool cfg_get_int(struct Config *h, const char *k, int *v);
bool cfg_get_size_t(struct Config *h, const char *k, size_t *v, size_t min, size_t max);
bool cfg_get_bool(struct Config *h, const char *k, bool *v);

typedef bool (*arr_parse_cb)(size_t arr_len, size_t idx, struct Config*, void* usr);
bool cfg_get_arr(struct Config *h, const char *k, arr_parse_cb cb, void* usr);

