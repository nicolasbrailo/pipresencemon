#include "config_base.h"

#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Config {
  struct json_object *json;
};


struct Config *cfg_init(const char *fpath) {
  struct Config *h = malloc(sizeof(struct Config));
  if (!h) {
    perror("config: malloc");
    goto err;
  }

  h->json = json_object_from_file(fpath);
  if (!h->json) {
    fprintf(stderr, "Config fail: can't parse JSON %s\n", fpath);
    goto err;
  }

  return h;

err:
  cfg_free(h);
  return NULL;
}

void cfg_free(struct Config *h) {
  if (!h) {
    return;
  }

  json_object_put(h->json);
  free(h);
}


#define CFG_GET_STR(key)                                                       \
  {                                                                            \
    const char *tmp = NULL;                                                    \
    if (!config_get_string(cfgbase, #key, &tmp)) {                             \
      goto err;                                                                \
    } else {                                                                   \
      /* Make a copy; the other string belongs to jsonc */                     \
      cfg->key = strdup(tmp);                                                  \
    }                                                                          \
  }

bool cfg_get_string_strdup(struct Config *h, const char *k, const char **v) {
  struct json_object *n;
  if (!json_object_object_get_ex(h->json, k, &n)) {
    fprintf(stderr,
            "Failed to read config: can't find str value %s\n", k);
    return false;
  }

  *v = strdup(json_object_get_string(n));
  if (!*v) {
    fprintf(stderr,
            "Failed to read config: can't alloc for value %s\n", k);
    return false;
  }

  return true;
}

bool cfg_get_int(struct Config *h, const char *k, int *v) {
  struct json_object *n;
  if (json_object_object_get_ex(h->json, k, &n)) {
    *v = json_object_get_int(n);
    return true;
  }

  return false;
}

bool cfg_get_size_t(struct Config *h, const char *k, size_t *v, size_t min, size_t max) {
  int iv;
  if (!cfg_get_int(h, k, &iv)) {
    fprintf(stderr, "Failed to read config: can't find value %s of type size_t\n", k);
    return false;
  }

  if ((iv < 0) || ((size_t)iv < min) || ((size_t)iv > max)) {
    fprintf(stderr,
            "Bad config value: invalid value %d for %s, expected interval is [%zu, %zu]\n", iv, k, min, max);
    return false;
  }

  *v = (size_t)iv;
  return true;
}

bool cfg_get_bool(struct Config *h, const char *k, bool *v) {
  struct json_object *n;
  if (json_object_object_get_ex(h->json, k, &n)) {
    *v = json_object_get_boolean(n);
    return true;
  }

  fprintf(stderr, "Failed to read config: can't find bool value %s\n", k);
  return false;
}

bool cfg_get_arr(struct Config *h, const char *k, arr_parse_cb cb, void* usr) {
  struct json_object *arr;
  if (!json_object_object_get_ex(h->json, k, &arr) || !json_object_is_type(arr, json_type_array)) {
    fprintf(stderr, "Failed to read config: can't find array %s\n", k);
    return false;
  }

  const size_t arr_len = json_object_array_length(arr);
  for (size_t i = 0; i < arr_len; i++) {
      struct Config tmp = {
        .json = json_object_array_get_idx(arr, i)
      };
      if (!cb(arr_len, i, &tmp, usr)) {
        fprintf(stderr, "Failed to read config: invalid value at %s[%zu]\n", k, i);
        return false;
      }
  }

  return true;
}

